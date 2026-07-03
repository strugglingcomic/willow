#include "willow_apps.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "WILLOW/APPS";

static bool app_active = false;

esp_err_t willow_apps_init(void)
{
    ESP_LOGI(TAG, "willow_apps skeleton — no apps vendored yet");
    return ESP_OK;
}

esp_err_t willow_apps_launch(willow_app_t app)
{
    if (app_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (app >= WILLOW_APP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    // TODO milestone 1: suspend audio pipeline, lock LVGL, spawn app task
    // running the vendored core from apps/ (poll touch → update → render →
    // blit to hdl_lcd), restore everything on exit.
    ESP_LOGW(TAG, "launch requested for app %d but not implemented", app);
    return ESP_ERR_NOT_SUPPORTED;
}

bool willow_apps_active(void)
{
    return app_active;
}
