#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Embedded mini-apps ("easter eggs") that temporarily take over the display,
// then return control to Willow. See main/apps/README.md for the design.

typedef enum {
    WILLOW_APP_MERMAID = 0,
    WILLOW_APP_MAX,
} willow_app_t;

// One-time setup; call from app_main after display, touch and audio init.
esp_err_t willow_apps_init(void);

// Launch an app: disables wake-word detection, locks LVGL away from the
// panel, runs the app loop in its own task until the exit gesture (hold the
// top-right corner ~1.5s), then restores Willow. Returns ESP_ERR_INVALID_STATE
// if an app is already running or a voice session is in progress.
esp_err_t willow_apps_launch(willow_app_t app);

// True while an app owns the display.
bool willow_apps_active(void);
