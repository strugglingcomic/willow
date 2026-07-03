# apps — embedded mini-apps ("easter eggs")

Launchable games/animations inside the Willow firmware — Solitaire-bundled-
with-Windows style. Willow stays the primary firmware; an app temporarily
takes over the display and returns to normal voice-assistant operation on
exit.

This lives inside `main/` (not a separate component) because it needs main's
globals (`hdl_lcd`, `hdl_touch`, `hdl_ar`, timers) — a component would create
a circular dependency.

## App cores

Cores are portable C developed and iterated in
[esp32-box-apps](https://github.com/strugglingcomic/esp32-box-apps) (browser
sims, SDL2 desktop runner, standalone ESP-IDF wrappers live there), then
vendored into a subdirectory here unchanged: pure C, no ESP-IDF includes,
fixed 320×240 RGB565 framebuffer, entry points `*_init` / `*_touch` /
`*_update` / `*_render`. Don't edit vendored cores here — fix upstream and
re-copy.

| App | Source |
|---|---|
| `mermaid/` | esp32-box-apps `mermaid-tail-game/main/game.{c,h}` |

## Launch / exit

- **Launch**: hold a finger anywhere on the screen for ~2s
  (`LV_EVENT_LONG_PRESSED_REPEAT` counting in `cb_scr`, `main/slvgl.c`).
- **Exit**: hold the top-right corner (48×48 px) for ~1.5s.
- Launch is refused during an active voice session (`recording`).
- Planned: voice-phrase launch ("mermaid time") by matching the transcript in
  the endpoint path before it is forwarded.

## Lifecycle (apps.c)

```
launch:  audio_recorder_wakenet_enable(false)   — no wake words, less CPU
         lvgl_port_lock(0)                      — parks the LVGL task; we
                                                  own panel + touch reads
         pause display-off timer, backlight on
         loop @30fps on core 1: read GT911 → update → render →
                                esp_lcd_panel_draw_bitmap(hdl_lcd, …)
exit:    lv_obj_invalidate(lv_scr_act())        — force full LVGL redraw
         restart display timer, lvgl_port_unlock()
         audio_recorder_wakenet_enable(true)
```

Framebuffer is 320×240×2 = 150 KB, allocated on launch (DMA-capable
preferred, PSRAM fallback) and freed on exit. The SPI bus is configured with
`max_transfer_sz` = full frame, so a single `draw_bitmap` per frame is fine.

While an app holds the LVGL lock, other Willow code paths that try
`lvgl_port_lock(lvgl_lock_timeout)` time out and skip their UI updates —
they already guard for that.
