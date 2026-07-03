/*
 * Mermaid Tail Game — portable game core.
 *
 * No ESP-IDF dependencies: this file also compiles on a host PC for
 * testing (define GAME_HOST to get non-byte-swapped RGB565 output).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GAME_W 320
#define GAME_H 240

void game_init(void);

/* Feed the current touch state once per frame (coords in screen pixels). */
void game_touch(bool pressed, int x, int y);

/* Advance the simulation by dt seconds. */
void game_update(float dt);

/* Draw the current frame into fb (GAME_W * GAME_H, RGB565).
 * On the ESP32 build the pixel values are byte-swapped so the buffer can be
 * sent to the SPI panel as-is; define GAME_HOST for natural byte order. */
void game_render(uint16_t *fb);
