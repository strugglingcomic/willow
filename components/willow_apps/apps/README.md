# apps/

Vendored portable app cores from
[esp32-box-apps](https://github.com/strugglingcomic/esp32-box-apps).

Copy each app's `game.c` / `game.h` (the portable core only — not
`app_main.c`, which is the standalone wrapper) into a subdirectory here and
add it to the component's `SRCS`. Cores are pure C and must compile unchanged;
develop and iterate on them in esp32-box-apps (browser sim), not here.
