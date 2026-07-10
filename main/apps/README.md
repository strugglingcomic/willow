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
         mute AUDIO_RECORDER logs ("Not in speeching" floods while off)
         pause display-off timer, backlight on
         load a new LVGL screen holding a full-screen lv_img backed by the
         game framebuffer (LV_USE_CANVAS is compiled out); touch arrives
         via LVGL events on the img
         loop @30fps on core 1: game_touch/update → (under lvgl lock)
                                game_render(fb) → lv_obj_invalidate(img)
exit:    restore previous screen, delete app screen
         restart display timer, restore log level
         audio_recorder_wakenet_enable(true)
```

Rendering goes **through** LVGL, not around it: the canvas buffer is plain
PSRAM (320×240×2 = 150 KB, allocated on launch, freed on exit) and the LVGL
port copies chunks through its internal DMA bounce buffer on flush. Writing
the PSRAM buffer straight to `esp_lcd_panel_draw_bitmap` does NOT work — the
SPI driver rejects non-DMA-capable memory (`spi transmit (queue) color
failed`), and there isn't 150 KB of contiguous internal DMA RAM at runtime.
The game core's byte-swapped RGB565 output matches `CONFIG_LV_COLOR_16_SWAP`,
so the canvas uses it as-is.

The exit-corner hold only starts counting after the first touch release, so
the launch long-press (finger still down) can't immediately exit the app.
