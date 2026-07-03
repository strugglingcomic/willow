/*
 * Mermaid Tail Game — portable game core.
 *
 * A little mermaid swims in an underwater scene. Swipe/drag on the screen
 * to make her tail wiggle: horizontal swipes scoot her left/right (she
 * turns to face the way she swims), vertical swipes pitch her up/down.
 * Holding a finger down also gently pulls her toward it.
 *
 * Everything is drawn from flat-shaded primitives (circles, triangles,
 * gradient rows) into a 320x240 RGB565 framebuffer, so the same code runs
 * on the ESP32-S3-BOX-3 and on a host PC for testing.
 */
#include "game.h"

#include <math.h>

/* ---------------------------------------------------------------- colors */

#define RGB_RAW(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#ifdef GAME_HOST
#define RGB(r, g, b) RGB_RAW(r, g, b)
#else
/* SPI panels take RGB565 big-endian; swap the bytes up front. */
#define RGB(r, g, b) ((uint16_t)(((RGB_RAW(r, g, b) >> 8) | (RGB_RAW(r, g, b) << 8)) & 0xFFFF))
#endif

#define C_SKIN      RGB(255, 205, 170)
#define C_SKIN_DK   RGB(225, 165, 130)
#define C_HAIR      RGB(215, 60, 45)
#define C_HAIR_DK   RGB(180, 40, 35)
#define C_TAIL      RGB(40, 200, 140)
#define C_TAIL_DK   RGB(20, 150, 105)
#define C_SHELL     RGB(190, 120, 220)
#define C_EYE       RGB(40, 35, 45)
#define C_MOUTH     RGB(200, 80, 80)
#define C_BUBBLE    RGB(200, 235, 255)
#define C_SAND      RGB(235, 210, 150)
#define C_SAND_DK   RGB(210, 180, 120)
#define C_WEED      RGB(30, 160, 90)

/* ------------------------------------------------------------- utilities */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t rng_state = 0x1234abcd;

static float frand(void) /* 0..1 */
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / 16777216.0f;
}

/* ------------------------------------------------------------ primitives */

static uint16_t *g_fb;

static void fill_circle(float cx, float cy, float r, uint16_t c)
{
    int y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > GAME_H) y1 = GAME_H;
    for (int y = y0; y < y1; y++) {
        float dy = (float)y + 0.5f - cy;
        float w2 = r * r - dy * dy;
        if (w2 <= 0.0f) continue;
        float w = sqrtf(w2);
        int x0 = (int)(cx - w), x1 = (int)(cx + w) + 1;
        if (x0 < 0) x0 = 0;
        if (x1 > GAME_W) x1 = GAME_W;
        uint16_t *row = g_fb + y * GAME_W;
        for (int x = x0; x < x1; x++) row[x] = c;
    }
}

static void fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
    int minx = (int)fminf(fminf(x0, x1), x2), maxx = (int)fmaxf(fmaxf(x0, x1), x2) + 1;
    int miny = (int)fminf(fminf(y0, y1), y2), maxy = (int)fmaxf(fmaxf(y0, y1), y2) + 1;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > GAME_W) maxx = GAME_W;
    if (maxy > GAME_H) maxy = GAME_H;
    float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (fabsf(d) < 1e-6f) return;
    for (int y = miny; y < maxy; y++) {
        uint16_t *row = g_fb + y * GAME_W;
        float py = (float)y + 0.5f;
        for (int x = minx; x < maxx; x++) {
            float px = (float)x + 0.5f;
            float a = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / d;
            float b = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / d;
            float g = 1.0f - a - b;
            if (a >= 0.0f && b >= 0.0f && g >= 0.0f) row[x] = c;
        }
    }
}

static void ring(float cx, float cy, float r, float thick, uint16_t c)
{
    /* cheap circle outline: dots along the circumference */
    int steps = (int)(r * 7.0f) + 8;
    for (int i = 0; i < steps; i++) {
        float a = (float)i * 6.2831853f / (float)steps;
        fill_circle(cx + cosf(a) * r, cy + sinf(a) * r, thick, c);
    }
}

/* ------------------------------------------------------------ game state */

typedef struct {
    float x, y, r, speed, wob;
    bool alive;
} bubble_t;

#define N_BUBBLES 14

static struct {
    float t;              /* global time */
    float mx, my;         /* mermaid hip position */
    float vx, vy;         /* velocity, px/s */
    float phase;          /* tail wiggle phase */
    float amp;            /* wiggle amplitude 0..1 */
    float facing;         /* +1 faces right, -1 faces left */
    float pitch;          /* body tilt, radians (+ = nose down on screen) */
    bool touch_down;
    int tx, ty;           /* last touch position */
    bool had_prev;        /* previous frame had a touch sample */
    bubble_t bubbles[N_BUBBLES];
} G;

void game_init(void)
{
    G.t = 0.0f;
    G.mx = GAME_W * 0.5f;
    G.my = GAME_H * 0.45f;
    G.vx = G.vy = 0.0f;
    G.phase = 0.0f;
    G.amp = 0.3f;
    G.facing = 1.0f;
    G.pitch = 0.0f;
    G.touch_down = false;
    G.had_prev = false;
    for (int i = 0; i < N_BUBBLES; i++) G.bubbles[i].alive = false;
}

void game_touch(bool pressed, int x, int y)
{
    if (pressed && G.had_prev) {
        float dx = (float)(x - G.tx);
        float dy = (float)(y - G.ty);
        G.vx += dx * 7.0f;
        G.vy += dy * 7.0f;
        float mag = fabsf(dx) + fabsf(dy);
        G.amp = clampf(G.amp + mag * 0.05f, 0.0f, 1.0f);
        if (fabsf(dx) > fabsf(dy) && fabsf(dx) > 1.0f) {
            G.facing = dx > 0.0f ? 1.0f : -1.0f;
        }
    }
    G.had_prev = pressed;
    G.touch_down = pressed;
    G.tx = x;
    G.ty = y;
}

static void spawn_bubble(float x, float y, float rmin, float rmax)
{
    for (int i = 0; i < N_BUBBLES; i++) {
        if (!G.bubbles[i].alive) {
            G.bubbles[i].alive = true;
            G.bubbles[i].x = x;
            G.bubbles[i].y = y;
            G.bubbles[i].r = rmin + frand() * (rmax - rmin);
            G.bubbles[i].speed = 20.0f + frand() * 30.0f;
            G.bubbles[i].wob = frand() * 6.28f;
            return;
        }
    }
}

void game_update(float dt)
{
    G.t += dt;

    /* holding a finger down gently pulls her toward it */
    if (G.touch_down) {
        G.vx += clampf((float)G.tx - G.mx, -60.0f, 60.0f) * 2.2f * dt;
        G.vy += clampf((float)G.ty - G.my, -60.0f, 60.0f) * 2.2f * dt;
    }

    G.mx += G.vx * dt;
    G.my += G.vy * dt;
    float damp = 1.0f - clampf(2.4f * dt, 0.0f, 0.9f);
    G.vx *= damp;
    G.vy *= damp;

    /* keep her on screen (soft bounce) */
    if (G.mx < 55.0f)          { G.mx = 55.0f;          G.vx = fabsf(G.vx) * 0.4f; }
    if (G.mx > GAME_W - 45.0f) { G.mx = GAME_W - 45.0f; G.vx = -fabsf(G.vx) * 0.4f; }
    if (G.my < 40.0f)          { G.my = 40.0f;          G.vy = fabsf(G.vy) * 0.4f; }
    if (G.my > GAME_H - 55.0f) { G.my = GAME_H - 55.0f; G.vy = -fabsf(G.vy) * 0.4f; }

    /* idle bobbing */
    G.my += sinf(G.t * 1.8f) * 5.0f * dt;

    /* tilt nose up/down while moving vertically */
    float ptarget = clampf(G.vy * 0.0045f, -0.55f, 0.55f);
    G.pitch += (ptarget - G.pitch) * clampf(5.0f * dt, 0.0f, 1.0f);

    /* the tail wiggles faster the harder you swipe, and calms back down */
    G.phase += (3.5f + 16.0f * G.amp) * dt;
    G.amp = clampf(G.amp - 0.8f * dt, 0.22f, 1.0f);

    /* bubbles: ambient ones plus a trail off the tail when she's swimming */
    if (frand() < 0.8f * dt) {
        spawn_bubble(frand() * GAME_W, GAME_H - 20.0f, 2.0f, 4.0f);
    }
    if (G.amp > 0.5f && frand() < 6.0f * G.amp * dt) {
        spawn_bubble(G.mx - G.facing * 45.0f, G.my + frand() * 10.0f - 5.0f, 1.5f, 3.0f);
    }
    for (int i = 0; i < N_BUBBLES; i++) {
        bubble_t *b = &G.bubbles[i];
        if (!b->alive) continue;
        b->y -= b->speed * dt;
        b->x += sinf(G.t * 3.0f + b->wob) * 12.0f * dt;
        if (b->y < -5.0f) b->alive = false;
    }
}

/* --------------------------------------------------------------- drawing */

/* local -> screen transform for the mermaid: local +x points out of her
 * face, +y down. Rotated by pitch, mirrored when she faces left. */
static float tf_cs, tf_sn;

static void tf(float lx, float ly, float *sx, float *sy)
{
    *sx = G.mx + G.facing * (lx * tf_cs - ly * tf_sn);
    *sy = G.my + (lx * tf_sn + ly * tf_cs);
}

static void tf_circle(float lx, float ly, float r, uint16_t c)
{
    float sx, sy;
    tf(lx, ly, &sx, &sy);
    fill_circle(sx, sy, r, c);
}

static void draw_background(void)
{
    /* water gradient */
    for (int y = 0; y < GAME_H; y++) {
        int k = y * 255 / GAME_H;
        int r = 70 - (55 * k) / 255;
        int g = 190 - (130 * k) / 255;
        int b = 235 - (115 * k) / 255;
        uint16_t c = RGB(r, g, b);
        uint16_t *row = g_fb + y * GAME_W;
        for (int x = 0; x < GAME_W; x++) row[x] = c;
    }

    /* sand */
    for (int y = GAME_H - 18; y < GAME_H; y++) {
        uint16_t *row = g_fb + y * GAME_W;
        for (int x = 0; x < GAME_W; x++) row[x] = C_SAND;
    }
    fill_circle(60.0f, (float)GAME_H - 16.0f, 5.0f, C_SAND_DK);
    fill_circle(210.0f, (float)GAME_H - 13.0f, 4.0f, C_SAND_DK);
    fill_circle(285.0f, (float)GAME_H - 17.0f, 6.0f, C_SAND_DK);

    /* swaying seaweed */
    static const float weed_x[3] = { 35.0f, 160.0f, 290.0f };
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 9; i++) {
            float yy = (float)GAME_H - 14.0f - (float)i * 8.0f;
            float xx = weed_x[w] + sinf(G.t * 1.6f + (float)w * 2.1f + (float)i * 0.55f) * (2.0f + (float)i * 1.1f);
            fill_circle(xx, yy, 4.5f - (float)i * 0.3f, C_WEED);
        }
    }
}

static void draw_mermaid(void)
{
    float pitch = G.pitch * G.facing; /* mirrored pose keeps pitch on screen-y */
    tf_cs = cosf(pitch);
    tf_sn = sinf(pitch);

    /* --- tail: tapered chain of circles whipping with the wiggle phase --- */
    float tipx = 0.0f, tipy = 0.0f, prevx = 0.0f, prevy = 0.0f;
    for (int i = 1; i <= 8; i++) {
        float lx = -(float)i * 5.5f;
        float ly = sinf(G.phase - (float)i * 0.55f) * G.amp * (float)(i * i) * 0.30f;
        float r = 8.5f - (float)i * 0.8f;
        tf_circle(lx, ly, r, (i & 1) ? C_TAIL : C_TAIL_DK);
        prevx = tipx; prevy = tipy;
        tf(lx, ly, &tipx, &tipy);
    }

    /* --- fluke (tail fin): two triangles fanning back from the tip --- */
    {
        float dx = tipx - prevx, dy = tipy - prevy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f) { dx = -G.facing; dy = 0.0f; len = 1.0f; }
        dx /= len; dy /= len;               /* direction the tail points */
        float px = -dy, py = dx;            /* perpendicular */
        float bx = tipx + dx * 14.0f, by = tipy + dy * 14.0f;
        fill_tri(tipx, tipy, bx + px * 13.0f, by + py * 13.0f, bx + px * 3.0f, by + py * 3.0f, C_TAIL);
        fill_tri(tipx, tipy, bx - px * 13.0f, by - py * 13.0f, bx - px * 3.0f, by - py * 3.0f, C_TAIL_DK);
    }

    /* --- hip + torso --- */
    tf_circle(0.0f, 0.0f, 8.5f, C_TAIL);
    tf_circle(4.0f, -1.0f, 7.5f, C_SKIN);
    tf_circle(9.0f, -2.0f, 7.0f, C_SKIN);
    tf_circle(14.0f, -3.0f, 6.5f, C_SKIN);

    /* seashell top */
    tf_circle(12.0f, -6.0f, 3.2f, C_SHELL);
    tf_circle(16.0f, -5.0f, 3.2f, C_SHELL);

    /* little arm resting forward */
    tf_circle(18.0f, 1.0f, 2.6f, C_SKIN_DK);
    tf_circle(21.0f, 3.0f, 2.2f, C_SKIN_DK);

    /* --- hair behind the head, flowing back --- */
    for (int i = 0; i < 4; i++) {
        float lx = 16.0f - (float)i * 5.5f;
        float ly = -14.0f + (float)i * 1.6f + sinf(G.t * 2.5f - (float)i * 0.8f) * 1.8f;
        tf_circle(lx, ly, 6.0f - (float)i * 0.7f, (i & 1) ? C_HAIR_DK : C_HAIR);
    }

    /* --- head --- */
    tf_circle(24.0f, -12.0f, 8.0f, C_SKIN);
    /* hair on top */
    tf_circle(21.0f, -18.0f, 5.5f, C_HAIR);
    tf_circle(26.0f, -18.5f, 4.5f, C_HAIR);
    /* face */
    tf_circle(27.5f, -13.0f, 1.5f, C_EYE);
    tf_circle(29.0f, -9.5f, 1.3f, C_MOUTH);
    tf_circle(24.5f, -9.5f, 1.6f, RGB(255, 170, 160)); /* blush */
}

static void draw_bubbles(void)
{
    for (int i = 0; i < N_BUBBLES; i++) {
        if (!G.bubbles[i].alive) continue;
        ring(G.bubbles[i].x, G.bubbles[i].y, G.bubbles[i].r, 1.0f, C_BUBBLE);
    }
}

void game_render(uint16_t *fb)
{
    g_fb = fb;
    draw_background();
    draw_mermaid();
    draw_bubbles();
}
