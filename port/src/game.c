#define _POSIX_C_SOURCE 200809L

#include "sim.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 960
#define SCR_H 540
#define SCALE 3
#define MAX_ID 24

typedef struct Frame {
    int id;
    int w, h;
    int ax, ay;
    uint8_t *px; /* RGBA */
} Frame;

static Frame g_frames[MAX_ID];
static uint32_t g_fb[SCR_W * SCR_H];
static int g_have[MAX_ID];

/* Compact 3x5 glyphs for the HUD. This is not the original stage font. */
static const uint8_t GLYPH_BLANK[5] = {0, 0, 0, 0, 0};

static void glyph(char ch, uint8_t out[5]) {
    /* Each row is 3 bits in the low nibble, top row first. */
    static const struct {
        char c;
        uint8_t r[5];
    } table[] = {
        {' ', {0, 0, 0, 0, 0}}, {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
        {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 7, 1, 7}}, {'4', {5, 5, 7, 1, 1}},
        {'5', {7, 4, 7, 1, 7}}, {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
        {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}}, {'A', {7, 5, 7, 5, 5}},
        {'B', {6, 5, 6, 5, 6}}, {'C', {7, 4, 4, 4, 7}}, {'D', {6, 5, 5, 5, 6}},
        {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}}, {'G', {7, 4, 5, 5, 7}},
        {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 7}},
        {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}}, {'M', {5, 7, 7, 5, 5}},
        {'N', {5, 7, 7, 7, 5}}, {'O', {7, 5, 5, 5, 7}}, {'P', {7, 5, 7, 4, 4}},
        {'R', {7, 5, 7, 6, 5}}, {'S', {7, 4, 7, 1, 7}}, {'T', {7, 2, 2, 2, 2}},
        {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}}, {'W', {5, 5, 7, 7, 5}},
        {'X', {5, 5, 2, 5, 5}}, {'Y', {5, 5, 7, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
        {'!', {2, 2, 2, 0, 2}}, {'-', {0, 0, 7, 0, 0}},
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].c == ch) {
            memcpy(out, table[i].r, 5);
            return;
        }
    }
    memcpy(out, GLYPH_BLANK, 5);
}

static void put_px(int x, int y, int r, int g, int b, int a) {
    uint32_t *dst;
    int dr, dg, db;
    if (a <= 0 || x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) {
        return;
    }
    dst = &g_fb[y * SCR_W + x];
    if (a >= 255) {
        *dst = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return;
    }
    dr = (int)((*dst >> 16) & 255);
    dg = (int)((*dst >> 8) & 255);
    db = (int)(*dst & 255);
    dr = dr + ((r - dr) * a) / 255;
    dg = dg + ((g - dg) * a) / 255;
    db = db + ((b - db) * a) / 255;
    *dst = ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
}

static void fill_rect(int x, int y, int w, int h, int r, int g, int b) {
    int yy, xx;
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            put_px(x + xx, y + yy, r, g, b, 255);
        }
    }
}

static void text(int x, int y, const char *s, int scale, int r, int g, int b) {
    int cx = x;
    for (; *s; s++) {
        uint8_t rows[5];
        int row, col;
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        glyph(ch, rows);
        for (row = 0; row < 5; row++) {
            for (col = 0; col < 3; col++) {
                if (rows[row] & (1 << (2 - col))) {
                    fill_rect(cx + col * scale, y + row * scale, scale, scale, r, g, b);
                }
            }
        }
        cx += 4 * scale;
    }
}

static int load_frames(const char *path) {
    FILE *fp = fopen(path, "rb");
    char magic[4];
    uint16_t count = 0;
    unsigned i;
    if (!fp) {
        fprintf(stderr, "missing %s\n", path);
        return -1;
    }
    if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "DDF1", 4) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(&count, 2, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    fseek(fp, 2, SEEK_CUR);
    for (i = 0; i < count; i++) {
        uint16_t id = 0, w = 0, h = 0, pad = 0;
        int16_t ax = 0, ay = 0;
        Frame *fr;
        if (fread(&id, 2, 1, fp) != 1 || fread(&w, 2, 1, fp) != 1 || fread(&h, 2, 1, fp) != 1 ||
            fread(&pad, 2, 1, fp) != 1 || fread(&ax, 2, 1, fp) != 1 || fread(&ay, 2, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
        (void)pad;
        if (id >= MAX_ID || w == 0 || h == 0 || w > 512 || h > 512) {
            fclose(fp);
            return -1;
        }
        fr = &g_frames[id];
        free(fr->px);
        fr->id = (int)id;
        fr->w = (int)w;
        fr->h = (int)h;
        fr->ax = ax;
        fr->ay = ay;
        fr->px = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
        if (!fr->px || fread(fr->px, 4, (size_t)w * (size_t)h, fp) != (size_t)w * (size_t)h) {
            fclose(fp);
            return -1;
        }
        g_have[id] = 1;
    }
    fclose(fp);
    return 0;
}

static void blit(int id, int feet_x, int feet_y, int facing_right, int flash) {
    const Frame *fr;
    int x0, y0, sy, sx;
    if (id < 0 || id >= MAX_ID || !g_have[id]) {
        return;
    }
    fr = &g_frames[id];
    x0 = feet_x - fr->ax * SCALE;
    y0 = feet_y - fr->ay * SCALE;
    if (facing_right) {
        x0 = feet_x - (fr->w - 1 - fr->ax) * SCALE;
    }
    for (sy = 0; sy < fr->h; sy++) {
        for (sx = 0; sx < fr->w; sx++) {
            int src = facing_right ? (fr->w - 1 - sx) : sx;
            const uint8_t *p = fr->px + ((sy * fr->w + src) * 4);
            int rr = p[0], gg = p[1], bb = p[2], aa = p[3];
            int dy, dx;
            if (aa == 0) {
                continue;
            }
            if (flash) {
                rr = (rr + 255) / 2;
                gg = (gg + 255) / 2;
                bb = (bb + 255) / 2;
            }
            for (dy = 0; dy < SCALE; dy++) {
                for (dx = 0; dx < SCALE; dx++) {
                    put_px(x0 + sx * SCALE + dx, y0 + sy * SCALE + dy, rr, gg, bb, aa);
                }
            }
        }
    }
}

static void blit_screen(int id, int x, int y, int scale) {
    const Frame *fr;
    int sy, sx;
    if (id < 0 || id >= MAX_ID || !g_have[id]) {
        return;
    }
    fr = &g_frames[id];
    for (sy = 0; sy < fr->h; sy++) {
        for (sx = 0; sx < fr->w; sx++) {
            const uint8_t *p = fr->px + ((sy * fr->w + sx) * 4);
            int dy, dx;
            if (p[3] == 0) {
                continue;
            }
            for (dy = 0; dy < scale; dy++) {
                for (dx = 0; dx < scale; dx++) {
                    put_px(x + sx * scale + dx, y + sy * scale + dy, p[0], p[1], p[2], p[3]);
                }
            }
        }
    }
}

static void sample_tile(int id, int u, int v, int *r, int *g, int *b, int *a) {
    const Frame *fr = &g_frames[id];
    const uint8_t *p;
    if (!g_have[id]) {
        *r = *g = *b = 0;
        *a = 0;
        return;
    }
    u %= fr->w;
    if (u < 0) {
        u += fr->w;
    }
    v %= fr->h;
    if (v < 0) {
        v += fr->h;
    }
    p = fr->px + (v * fr->w + u) * 4;
    *r = p[0];
    *g = p[1];
    *b = p[2];
    *a = p[3];
}

static int jimmy_frame(const Actor *a) {
    if (a->state == ST_PUNCH) {
        return 5;
    }
    if (a->state == ST_KICK || a->state == ST_AIR) {
        return 6;
    }
    if (a->state == ST_SPECIAL) {
        return 9;
    }
    if (a->state == ST_HURT) {
        return 7;
    }
    if (a->state == ST_DOWN || a->state == ST_DEAD) {
        return 8;
    }
    if (a->state == ST_WALK) {
        return 2 + ((a->x / 8) & 1);
    }
    return 1;
}

static int enemy_frame(const Actor *a) {
    if (a->state == ST_PUNCH) {
        return 14;
    }
    if (a->state == ST_HURT) {
        return 15;
    }
    if (a->state == ST_DOWN || a->state == ST_DEAD) {
        return 16;
    }
    if (a->state == ST_WALK) {
        return 12 + ((a->x / 8) & 1);
    }
    return 11;
}

static int feet_y(const Actor *a) {
    return 230 + a->depth - a->alt;
}

static int screen_x(const Sim *s, int world_x) {
    return (world_x - s->camera_x) * SCALE;
}

static void draw_world(const Sim *s) {
    int y, x;
    for (y = 0; y < SCR_H; y++) {
        for (x = 0; x < SCR_W; x++) {
            g_fb[y * SCR_W + x] = (y < 220) ? 0x141820u : 0x2a241cu;
        }
    }
    for (y = 40; y < 230; y++) {
        for (x = 0; x < SCR_W; x++) {
            int r, g, b, a;
            int u = (x / 2 + s->camera_x / 2);
            int v = (y - 40) / 2;
            sample_tile(22, u, v, &r, &g, &b, &a);
            if (a) {
                put_px(x, y, r, g, b, 255);
            }
        }
    }
    for (y = 250; y < 520; y++) {
        for (x = 0; x < SCR_W; x++) {
            int r, g, b, a;
            int u = x / 2 + s->camera_x;
            int v = (y - 250) / 2;
            sample_tile(21, u, v, &r, &g, &b, &a);
            if (a) {
                put_px(x, y, r, g, b, 255);
            } else {
                put_px(x, y, 70, 68, 62, 255);
            }
        }
    }
    (void)s;
}

static void draw_actor(const Sim *s, const Actor *a, int enemy) {
    int id = enemy ? enemy_frame(a) : jimmy_frame(a);
    if (!a->alive && a->state != ST_DEAD && a->state != ST_DOWN) {
        return;
    }
    if (a->state == ST_DEAD && a->timer <= 0 && enemy) {
        /* Still draw the body that just dropped if hp is 0 and state is dead. */
    }
    if (!a->alive && a->hp <= 0 && a->state == ST_DEAD) {
        blit(id, screen_x(s, a->x), feet_y(a) + 8, a->facing > 0, 0);
        return;
    }
    if (!a->alive) {
        return;
    }
    if (!enemy && a->state == ST_SPECIAL) {
        int ax = screen_x(s, a->x);
        int ay = feet_y(a) - 78;
        int yy, xx;
        for (yy = 0; yy < 64; yy++) {
            for (xx = 0; xx < 36; xx++) {
                put_px(ax - 18 + xx, ay + yy, 70, 140, 255, 70);
            }
        }
    }
    {
        int fx = screen_x(s, a->x);
        int fy = feet_y(a);
        int sx, sy;
        for (sy = 0; sy < 6; sy++) {
            for (sx = -18; sx < 18; sx++) {
                if (sx * sx / 3 + sy * sy < 28) {
                    put_px(fx + sx, fy - 2 + sy, 0, 0, 0, 90);
                }
            }
        }
    }
    blit(id, screen_x(s, a->x), feet_y(a), a->facing > 0, a->flash > 0);
    if (a->flash > 0) {
        fill_rect(screen_x(s, a->x) + a->facing * 18, feet_y(a) - 46, 12, 12, 255, 220, 80);
    }
}

static void draw_hud(const Sim *s) {
    int hp_w;
    int sec;
    char buf[32];
    fill_rect(8, 8, 280, 72, 8, 8, 12);
    blit_screen(20, 14, 14, 2);
    hp_w = (s->player.hp * 140) / 48;
    if (hp_w < 0) {
        hp_w = 0;
    }
    fill_rect(86, 18, 144, 12, 40, 16, 16);
    fill_rect(88, 20, hp_w, 8, 210, 48, 42);
    snprintf(buf, sizeof(buf), "%06d", s->score);
    text(86, 36, buf, 2, 240, 220, 160);
    if (s->chain > 1) {
        snprintf(buf, sizeof(buf), "CHAIN %d", s->chain);
        text(170, 36, buf, 1, 255, 196, 64);
    }
    fill_rect(86, 52, 78, 14, 16, 16, 20);
    text(90, 55, "WEAPON", 1, 150, 150, 160);
    sec = s->time_left / 60;
    snprintf(buf, sizeof(buf), "%02d %02d", sec / 60, sec % 60);
    text(430, 16, buf, 3, 240, 240, 240);
    text(560, 28, "CREDIT 1", 1, 200, 200, 210);
    text(760, 14, "SLUM", 3, 255, 230, 180);
    snprintf(buf, sizeof(buf), "1-%d", s->wave);
    text(820, 36, buf, 2, 220, 220, 230);
    if (s->wave == 1 && s->camera_locked == 0 && s->result == 0) {
        text(620, 200, "GO!", 6, 255, 240, 80);
        fill_rect(760, 210, 28, 8, 255, 240, 80);
        fill_rect(788, 202, 8, 24, 255, 240, 80);
    }
    if (s->result == 1) {
        fill_rect(280, 180, 400, 90, 0, 0, 0);
        text(360, 200, "CLEAR", 5, 255, 220, 80);
        text(330, 240, "ENTER REINICIA", 2, 220, 220, 220);
    } else if (s->result == 2) {
        fill_rect(280, 180, 400, 90, 0, 0, 0);
        text(390, 200, "DOWN", 5, 220, 64, 64);
        text(330, 240, "ENTER REINICIA", 2, 220, 220, 220);
    }
}

static void draw_all(const Sim *s) {
    int order[1 + SIM_ENEMIES];
    int n = 0;
    int i, j;
    draw_world(s);
    order[n++] = -1;
    for (i = 0; i < SIM_ENEMIES; i++) {
        order[n++] = i;
    }
    for (i = 1; i < n; i++) {
        int key = order[i];
        int kd = (key < 0) ? s->player.depth : s->enemy[key].depth;
        j = i - 1;
        while (j >= 0) {
            int jd = (order[j] < 0) ? s->player.depth : s->enemy[order[j]].depth;
            if (jd <= kd) {
                break;
            }
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (i = 0; i < n; i++) {
        if (order[i] < 0) {
            if (s->player.alive || s->player.state == ST_DEAD || s->player.hp == 0) {
                draw_actor(s, &s->player, 0);
            }
        } else {
            const Actor *e = &s->enemy[order[i]];
            if (e->alive || e->state == ST_DEAD) {
                draw_actor(s, e, 1);
            }
        }
    }
    draw_hud(s);
}

static unsigned demo_buttons(int tick) {
    if (tick < 40) {
        return SIM_RIGHT;
    }
    if (tick < 55) {
        return SIM_DOWN;
    }
    if (tick == 70 || tick == 100 || tick == 130 || tick == 160 || tick == 190) {
        return SIM_PUNCH;
    }
    if (tick == 220) {
        return SIM_KICK;
    }
    if (tick == 260) {
        return SIM_SPECIAL;
    }
    if (tick > 300 && tick < 360) {
        return SIM_RIGHT;
    }
    return 0;
}

static int write_ppm(const char *path) {
    FILE *fp = fopen(path, "wb");
    int y, x;
    if (!fp) {
        return -1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (y = 0; y < SCR_H; y++) {
        for (x = 0; x < SCR_W; x++) {
            uint32_t p = g_fb[y * SCR_W + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((p >> 16) & 255);
            rgb[1] = (unsigned char)((p >> 8) & 255);
            rgb[2] = (unsigned char)(p & 255);
            fwrite(rgb, 1, 3, fp);
        }
    }
    fclose(fp);
    return 0;
}

static void run_demo_dump(const char *ppm, int ticks) {
    Sim sim;
    int t;
    sim_init(&sim);
    for (t = 0; t < ticks; t++) {
        sim_tick(&sim, demo_buttons(t));
    }
    draw_all(&sim);
    if (write_ppm(ppm) != 0) {
        fprintf(stderr, "cannot write %s\n", ppm);
        exit(1);
    }
    printf("dump %s tick %d hp %d enemies %d score %d\n", ppm, sim.tick, sim.player.hp,
           sim_living_enemies(&sim), sim.score);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static unsigned vk_buttons(void) {
    unsigned b = 0;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) b |= SIM_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) b |= SIM_RIGHT;
    if (GetAsyncKeyState(VK_UP) & 0x8000) b |= SIM_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) b |= SIM_DOWN;
    if (GetAsyncKeyState('Z') & 0x8000) b |= SIM_PUNCH;
    if (GetAsyncKeyState('X') & 0x8000) b |= SIM_KICK;
    if (GetAsyncKeyState('C') & 0x8000) b |= SIM_SPECIAL;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) b |= SIM_JUMP;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) b |= SIM_RESTART;
    return b;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int run_window(int demo) {
    WNDCLASS wc;
    HWND hwnd;
    HDC hdc;
    BITMAPINFO bmi;
    Sim sim;
    int ticks = 0;
    MSG msg;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "ddslum";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    hwnd = CreateWindow("ddslum", "Double Dragon - rua (reconstrucao)", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, SCR_W + 16, SCR_H + 48, NULL, NULL, wc.hInstance,
                        NULL);
    ShowWindow(hwnd, SW_SHOW);
    hdc = GetDC(hwnd);
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCR_W;
    bmi.bmiHeader.biHeight = -SCR_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    sim_init(&sim);
    for (;;) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        sim_tick(&sim, demo ? demo_buttons(ticks) : vk_buttons());
        ticks++;
        draw_all(&sim);
        StretchDIBits(hdc, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, g_fb, &bmi, DIB_RGB_COLORS,
                      SRCCOPY);
        Sleep(16);
        if (demo && ticks == 180) {
            write_ppm("demo.ppm");
        }
        if (demo && ticks > 200) {
            return 0;
        }
    }
}
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <time.h>

static unsigned x_buttons(Display *dpy) {
    char keys[32];
    unsigned b = 0;
    XQueryKeymap(dpy, keys);
#define DOWN(k) ((keys[(k) / 8] & (1 << ((k) % 8))) != 0)
    if (DOWN(XKeysymToKeycode(dpy, XK_Left))) b |= SIM_LEFT;
    if (DOWN(XKeysymToKeycode(dpy, XK_Right))) b |= SIM_RIGHT;
    if (DOWN(XKeysymToKeycode(dpy, XK_Up))) b |= SIM_UP;
    if (DOWN(XKeysymToKeycode(dpy, XK_Down))) b |= SIM_DOWN;
    if (DOWN(XKeysymToKeycode(dpy, XK_z))) b |= SIM_PUNCH;
    if (DOWN(XKeysymToKeycode(dpy, XK_x))) b |= SIM_KICK;
    if (DOWN(XKeysymToKeycode(dpy, XK_c))) b |= SIM_SPECIAL;
    if (DOWN(XKeysymToKeycode(dpy, XK_space))) b |= SIM_JUMP;
    if (DOWN(XKeysymToKeycode(dpy, XK_Return))) b |= SIM_RESTART;
#undef DOWN
    return b;
}

static int run_window(int demo) {
    Display *dpy = XOpenDisplay(NULL);
    int screen;
    Window win;
    GC gc;
    XImage *image;
    Visual *vis;
    Sim sim;
    int ticks = 0;
    char *data;
    Atom wm;
    if (!dpy) {
        fprintf(stderr, "no DISPLAY\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, screen);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 40, 40, SCR_W, SCR_H, 0,
                              BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    XStoreName(dpy, win, "Double Dragon - rua (reconstrucao)");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    wm = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm, 1);
    XMapWindow(dpy, win);
    gc = XCreateGC(dpy, win, 0, NULL);
    data = (char *)malloc((size_t)SCR_W * SCR_H * 4u);
    image = XCreateImage(dpy, vis, 24, ZPixmap, 0, data, SCR_W, SCR_H, 32, 0);
    image->byte_order = LSBFirst;
    sim_init(&sim);
    for (;;) {
        XEvent ev;
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == ClientMessage) {
                XDestroyImage(image);
                XCloseDisplay(dpy);
                return 0;
            }
        }
        sim_tick(&sim, demo ? demo_buttons(ticks) : x_buttons(dpy));
        ticks++;
        draw_all(&sim);
        {
            int y, x;
            for (y = 0; y < SCR_H; y++) {
                for (x = 0; x < SCR_W; x++) {
                    uint32_t p = g_fb[y * SCR_W + x];
                    /* BGRX little endian */
                    data[(y * SCR_W + x) * 4 + 0] = (char)(p & 255);
                    data[(y * SCR_W + x) * 4 + 1] = (char)((p >> 8) & 255);
                    data[(y * SCR_W + x) * 4 + 2] = (char)((p >> 16) & 255);
                    data[(y * SCR_W + x) * 4 + 3] = 0;
                }
            }
        }
        XPutImage(dpy, win, gc, image, 0, 0, 0, 0, SCR_W, SCR_H);
        XFlush(dpy);
        if (demo && ticks == 120) {
            write_ppm("demo.ppm");
        }
        if (demo && ticks > 150) {
            XDestroyImage(image);
            XCloseDisplay(dpy);
            return 0;
        }
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 16000000L;
            nanosleep(&ts, NULL);
        }
    }
}
#endif

static void usage(void) {
    fprintf(stderr,
            "ddslum frames.bin [--dump out.ppm --ticks N] [--play] [--demo]\n"
            "Setas movem. Z soco, X chute, C especial, espaco pulo, Enter reinicia.\n");
}

int main(int argc, char **argv) {
    const char *frames = "frames.bin";
    const char *dump = NULL;
    int ticks = 140;
    int play = 0;
    int demo = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            dump = argv[++i];
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--play") == 0) {
            play = 1;
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo = 1;
            play = 1;
        } else if (argv[i][0] != '-') {
            frames = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (load_frames(frames) != 0) {
        fprintf(stderr, "bad frames %s\n", frames);
        return 1;
    }
    if (dump) {
        run_demo_dump(dump, ticks);
    }
    if (play || !dump) {
        return run_window(demo);
    }
    return 0;
}
