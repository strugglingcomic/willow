#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "audio.h"
#include "config.h"
#include "display.h"
#include "slvgl.h"
#include "timer.h"

#include "apps.h"
#include "game.h"

#define APP_FPS          30
#define APP_TASK_STACK   8192
#define APP_TASK_PRIO    4
#define APP_TASK_CORE    1
// hold the top-right corner this long to exit
#define EXIT_ZONE_PX     48
#define EXIT_HOLD_FRAMES (APP_FPS * 3 / 2)

static const char *TAG = "WILLOW/APPS";

static _Atomic bool app_active = false;

static void app_task(void *data)
{
    willow_app_t app = (willow_app_t)data;

    uint16_t *fb = heap_caps_malloc(GAME_W * GAME_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (fb == NULL) {
        ESP_LOGW(TAG, "no DMA-capable framebuffer memory, falling back to PSRAM");
        fb = heap_caps_malloc(GAME_W * GAME_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if (fb == NULL) {
        ESP_LOGE(TAG, "failed to allocate framebuffer");
        app_active = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "launching app %d", app);

    // no wake-word interruptions (and less CPU) while playing
    audio_recorder_wakenet_enable(hdl_ar, false);
    // holding the LVGL mutex parks the LVGL task, making us the only writer
    // to the panel and the only reader of the touch controller
    lvgl_port_lock(0);
    reset_timer(hdl_display_timer, 0, true);
    display_set_backlight(true, false);

    game_init();

    int exit_hold = 0;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        bool pressed = false;
        int x = 0, y = 0;

        if (hdl_touch != NULL) {
            uint16_t tx = 0, ty = 0;
            uint8_t cnt = 0;
            esp_lcd_touch_read_data(hdl_touch);
            if (esp_lcd_touch_get_coordinates(hdl_touch, &tx, &ty, NULL, &cnt, 1) && cnt > 0) {
                pressed = true;
                x = tx;
                y = ty;
            }
        }

        if (pressed && x > GAME_W - EXIT_ZONE_PX && y < EXIT_ZONE_PX) {
            if (++exit_hold >= EXIT_HOLD_FRAMES) {
                break;
            }
        } else {
            exit_hold = 0;
        }

        game_touch(pressed, x, y);
        game_update(1.0f / APP_FPS);
        game_render(fb);
        esp_lcd_panel_draw_bitmap(hdl_lcd, 0, 0, GAME_W, GAME_H, fb);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / APP_FPS));
    }

    ESP_LOGI(TAG, "app %d exiting", app);

    lv_obj_invalidate(lv_scr_act());
    reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false);
    lvgl_port_unlock();
    audio_recorder_wakenet_enable(hdl_ar, true);

    heap_caps_free(fb);
    app_active = false;
    vTaskDelete(NULL);
}

esp_err_t willow_apps_init(void)
{
    if (hdl_lcd == NULL || hdl_touch == NULL) {
        ESP_LOGW(TAG, "display or touch not initialized, apps disabled");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "initialized (long-press the screen ~2s to launch)");
    return ESP_OK;
}

esp_err_t willow_apps_launch(willow_app_t app)
{
    if (app >= WILLOW_APP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (hdl_lcd == NULL || hdl_touch == NULL || hdl_ar == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (recording) {
        ESP_LOGI(TAG, "not launching app during active voice session");
        return ESP_ERR_INVALID_STATE;
    }
    if (app_active) {
        return ESP_ERR_INVALID_STATE;
    }
    app_active = true;

    if (xTaskCreatePinnedToCore(app_task, "willow_app", APP_TASK_STACK, (void *)app, APP_TASK_PRIO,
                                NULL, APP_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "failed to create app task");
        app_active = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool willow_apps_active(void)
{
    return app_active;
}
