#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "i2c.h"
#include "sensor.h"
#include "slvgl.h"

static const char *TAG = "WILLOW/SENSOR";

// Sensor dock I2C bus (Bus B): separate from main board I2C (Bus A)
#define SENSOR_I2C_PORT I2C_NUM_1
#define SENSOR_I2C_SDA  GPIO_NUM_41
#define SENSOR_I2C_SCL  GPIO_NUM_40

static i2c_bus_handle_t hdl_sensor_bus;

static void sensor_task(void *arg)
{
    uint8_t cmd[] = {0xAC, 0x33, 0x00};
    uint8_t buf[7];
    esp_err_t ret;

    while (true) {
        ret = i2c_bus_write_data(hdl_sensor_bus, AHT30_ADDR_RW, cmd, sizeof(cmd));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "failed to trigger measurement: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(80));

        ret = i2c_bus_read_bytes_directly(hdl_sensor_bus, AHT30_ADDR_RW, buf, sizeof(buf));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "failed to read sensor: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        if (buf[0] & 0x80) {
            ESP_LOGD(TAG, "sensor busy, will retry next cycle");
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        uint32_t raw_hum = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
        float humidity = (raw_hum / 1048576.0f) * 100.0f;

        uint32_t raw_temp = ((uint32_t)(buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];
        float temperature = (raw_temp / 1048576.0f) * 200.0f - 50.0f;

        // Use integer parts for display (LVGL snprintf doesn't support %f)
        int temp_whole = (int)temperature;
        int temp_frac = ((int)(temperature * 10)) % 10;
        if (temp_frac < 0) temp_frac = -temp_frac;
        int hum_whole = (int)humidity;
        int hum_frac = ((int)(humidity * 10)) % 10;

        ESP_LOGI(TAG, "temp=%d.%d°C humidity=%d.%d%%", temp_whole, temp_frac, hum_whole, hum_frac);

        if (lvgl_port_lock(lvgl_lock_timeout)) {
            lv_label_set_text_fmt(lbl_ln4, "Temp: %d.%d °C", temp_whole, temp_frac);
            lv_label_set_text_fmt(lbl_ln5, "Humidity: %d.%d %%", hum_whole, hum_frac);
            lv_obj_clear_flag(lbl_ln4, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_ln5, LV_OBJ_FLAG_HIDDEN);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t init_sensor(void)
{
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SENSOR_I2C_SDA,
        .scl_io_num = SENSOR_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    hdl_sensor_bus = i2c_bus_create(SENSOR_I2C_PORT, &i2c_cfg);
    if (hdl_sensor_bus == NULL) {
        ESP_LOGW(TAG, "failed to create sensor dock I2C bus");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "sensor dock I2C bus initialized (SDA=%d SCL=%d)", SENSOR_I2C_SDA, SENSOR_I2C_SCL);

    esp_err_t ret = i2c_bus_probe_addr(hdl_sensor_bus, AHT30_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT30 not found at 0x%02x, dock not attached?", AHT30_ADDR);
        i2c_bus_delete(hdl_sensor_bus);
        hdl_sensor_bus = NULL;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "AHT30 detected at 0x%02x", AHT30_ADDR);
    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, NULL, 1, NULL, 0);
    return ESP_OK;
}
