# willow_apps

Launchable mini-apps ("easter eggs") embedded in the Willow firmware —
Solitaire-bundled-with-Windows style. Willow remains the primary firmware;
apps temporarily take over the display and return to normal voice-assistant
operation on exit.

App cores are portable C modules developed and iterated in
[esp32-box-apps](https://github.com/strugglingcomic/esp32-box-apps) (browser
sims + standalone ESP-IDF wrappers live there), then vendored into `apps/`
here unchanged. Cores are pure C with no ESP-IDF dependencies: fixed 320×240
RGB565 framebuffer, entry points `*_init` / `*_touch` / `*_update` / `*_render`.

## Design

### Launch triggers

1. **Touch gesture** (first milestone): long-press on the LVGL screen.
   Willow's `cb_scr` in `main/slvgl.c` already receives press/release events
   for the whole screen; add press-duration tracking there and call
   `willow_apps_launch()`.
2. **Voice phrase** (second milestone): with `was_mode=false` +
   `command_endpoint=REST`, the firmware sees the transcript before POSTing to
   the endpoint. Match a magic phrase (e.g. "mermaid time") and launch instead
   of forwarding.

### Lifecycle

```
launch:  suspend audio pipeline / wake-word task (CPU + PSRAM headroom)
         → acquire LVGL lock (lvgl_port) so LVGL stops touching the panel
         → run app loop: poll GT911 touch → update → render → blit to hdl_lcd
exit:    (corner tap / physical button / inactivity timeout)
         → release LVGL lock, lv_obj_invalidate() to force full redraw
         → resume audio pipeline; back to normal Willow
```

- `hdl_lcd` (`esp_lcd_panel_handle_t`, declared in `main/slvgl.c`) is blitted
  directly with `esp_lcd_panel_draw_bitmap()` — same call the standalone
  wrappers use.
- Framebuffer: 320×240×2 = 150 KB, allocated on launch (DMA-capable preferred,
  PSRAM fallback), freed on exit.
- Touch: read the `esp_lcd_touch` handle directly while LVGL is locked out.

## Status

Skeleton only — API stubs in `include/willow_apps.h`, no integration hooks in
`main/` yet. Milestone 1 is the long-press launcher running the mermaid game.
