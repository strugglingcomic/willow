#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
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

// current touch state, fed by LVGL events on the canvas (LVGL task) and
// consumed once per frame by the app task
static _Atomic bool touch_pressed = false;
static _Atomic int touch_x = 0;
static _Atomic int touch_y = 0;

// full-screen lv_img wrapping the game framebuffer (LV_USE_CANVAS is
// compiled out of willow's LVGL; for a raw full-frame blit img is equivalent)
static lv_img_dsc_t app_img_dsc;

static void cb_app_touch(lv_event_t *ev)
{
    switch (lv_event_get_code(ev)) {
        case LV_EVENT_PRESSED:
        case LV_EVENT_PRESSING: {
            lv_indev_t *indev = lv_indev_get_act();
            if (indev == NULL) {
                break;
            }
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            touch_x = p.x;
            touch_y = p.y;
            touch_pressed = true;
            break;
        }
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            touch_pressed = false;
            break;
        default:
            break;
    }
}

static void app_task(void *data)
{
    willow_app_t app = (willow_app_t)data;

    // LVGL flushes copy through the LVGL port's internal DMA bounce buffer,
    // so the canvas buffer can live in plain (non-DMA) PSRAM
    uint16_t *fb = heap_caps_malloc(GAME_W * GAME_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (fb == NULL) {
        ESP_LOGE(TAG, "failed to allocate framebuffer");
        app_active = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "launching app %d", app);

    // no wake-word interruptions (and less CPU) while playing; the recorder
    // logs "Not in speeching" on every read while wakenet is off, so mute it
    audio_recorder_wakenet_enable(hdl_ar, false);
    esp_log_level_t lvl_rec = esp_log_level_get("AUDIO_RECORDER");
    esp_log_level_set("AUDIO_RECORDER", ESP_LOG_ERROR);

    game_init();
    touch_pressed = false;

    lvgl_port_lock(0);
    reset_timer(hdl_display_timer, 0, true);
    display_set_backlight(true, false);
    lv_obj_t *scr_prev = lv_scr_act();
    lv_obj_t *scr_app = lv_obj_create(NULL);
    lv_obj_clear_flag(scr_app, LV_OBJ_FLAG_SCROLLABLE);
    // CONFIG_LV_COLOR_16_SWAP matches the byte-swapped RGB565 the game renders
    app_img_dsc = (lv_img_dsc_t){
        .header = {
            .cf = LV_IMG_CF_TRUE_COLOR,
            .w = GAME_W,
            .h = GAME_H,
        },
        .data_size = GAME_W * GAME_H * sizeof(uint16_t),
        .data = (const uint8_t *)fb,
    };
    lv_obj_t *img = lv_img_create(scr_app);
    lv_img_set_src(img, &app_img_dsc);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(img, cb_app_touch, LV_EVENT_ALL, NULL);
    lv_scr_load(scr_app);
    lvgl_port_unlock();

    // the launch long-press means a finger is still on the screen; require a
    // release before the exit-corner hold starts counting
    bool exit_armed = false;
    int exit_hold = 0;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        bool pressed = touch_pressed;
        int x = touch_x, y = touch_y;

        if (!pressed) {
            exit_armed = true;
        }

        if (exit_armed && pressed && x > GAME_W - EXIT_ZONE_PX && y < EXIT_ZONE_PX) {
            if (++exit_hold >= EXIT_HOLD_FRAMES) {
                break;
            }
        } else {
            exit_hold = 0;
        }

        game_touch(pressed, x, y);
        game_update(1.0f / APP_FPS);

        if (lvgl_port_lock(lvgl_lock_timeout)) {
            game_render(fb);
            lv_obj_invalidate(img);
            lvgl_port_unlock();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / APP_FPS));
    }

    ESP_LOGI(TAG, "app %d exiting", app);

    lvgl_port_lock(0);
    lv_scr_load(scr_prev);
    lv_obj_del(scr_app);
    reset_timer(hdl_display_timer, config_get_int("display_timeout", DEFAULT_DISPLAY_TIMEOUT), false);
    lvgl_port_unlock();

    esp_log_level_set("AUDIO_RECORDER", lvl_rec);
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
