/* Stage 1 slice driven by recovered iOS tables.

   The packed map already has row 0 at the bottom of the bitmap.
   glOrthox uses top 0 and bottom -height. StoreSprite negates Y,
   so game +Y grows down from the top. The road tiles are the low
   map rows, and FloorGroundSet's 0xEF lands on that road only
   when those rows sit at the bottom.

   0x1faa2 writes 480 at +0x47a8 and 320 at +0x47ac. MapChipDraw
   subtracts 320, which is why row 0 of the packed bitmap is the road.

   Near layer scrolls camera >> 2. The first scroll records spawn
   Williams and BASH at x = 480 + 20. J is button index 4.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 480
#define VIEW_H 320
#define SCALE 2

typedef struct {
    int duration, w, h, ox, oy;
    uint32_t flags;
    uint8_t *px;
} Frame;

typedef struct {
    int x, z, hp, alive, sheet, frame, timer, shown, hit;
} Foe;

typedef struct {
    int map_w, map_h, spawn_x, spawn_z, vel_x, vel_z, z_max;
    int floor_w, floor_h, floor_origin;
    int n_idle, n_walk, n_bash, n_will, n_p28, n_p29, n_p45, n_foes;
    int ax, ay, aw, ah, az, azs, dmg28;
    int cx, cy, cw, ch, cz, czs, dmg45;
    int hx, hy, hw, hh, hz, hzs, grab_x, grab_z;
    int32_t *ground;
    uint8_t *far_px;
    uint8_t *main_px;
    Frame *idle;
    Frame *walk;
    Frame *bash;
    Frame *will;
    Frame *p28;
    Frame *p29;
    Frame *p45;
    Foe foes[4];
} Scene;

static int rd(FILE *fp, void *dst, size_t n) {
    return fread(dst, 1, n, fp) == n;
}

static int ru16(FILE *fp, int *out) {
    uint16_t v;
    if (!rd(fp, &v, 2)) return 0;
    *out = v;
    return 1;
}

static int ri32(FILE *fp, int *out) {
    int32_t v;
    if (!rd(fp, &v, 4)) return 0;
    *out = v;
    return 1;
}

static int rs16(FILE *fp, int *out) {
    int16_t v;
    if (!rd(fp, &v, 2)) return 0;
    *out = v;
    return 1;
}

static int rs8(FILE *fp, int *out) {
    int8_t v;
    if (!rd(fp, &v, 1)) return 0;
    *out = v;
    return 1;
}

static int ru8(FILE *fp, int *out) {
    uint8_t v;
    if (!rd(fp, &v, 1)) return 0;
    *out = v;
    return 1;
}

static Frame *load_frames(FILE *fp, int count) {
    Frame *frames = calloc(count, sizeof(Frame));
    int i;
    if (!frames) return NULL;
    for (i = 0; i < count; i++) {
        uint16_t duration, w, h;
        uint32_t flags;
        int16_t ox, oy;
        if (!rd(fp, &duration, 2) || !rd(fp, &flags, 4) || !rd(fp, &w, 2) ||
            !rd(fp, &h, 2) || !rd(fp, &ox, 2) || !rd(fp, &oy, 2)) {
            return NULL;
        }
        frames[i].duration = duration;
        frames[i].flags = flags;
        frames[i].w = w;
        frames[i].h = h;
        frames[i].ox = ox;
        frames[i].oy = oy;
        frames[i].px = malloc((size_t)w * h * 4);
        if (!frames[i].px || !rd(fp, frames[i].px, (size_t)w * h * 4)) return NULL;
    }
    return frames;
}

static Scene *load_scene(const char *path) {
    FILE *fp = fopen(path, "rb");
    char magic[4];
    int version, vw, vh;
    Scene *scene;
    size_t map_bytes, ground_n;
    if (!fp) return NULL;
    scene = calloc(1, sizeof(Scene));
    if (!scene || !rd(fp, magic, 4) || memcmp(magic, "DD1S", 4) != 0) goto fail;
    if (!ru16(fp, &version) || version != 2) goto fail;
    if (!ru16(fp, &vw) || !ru16(fp, &vh)) goto fail;
    if (!ri32(fp, &scene->map_w) || !ri32(fp, &scene->map_h)) goto fail;
    if (!ri32(fp, &scene->spawn_x) || !ri32(fp, &scene->spawn_z)) goto fail;
    if (!ri32(fp, &scene->vel_x) || !ri32(fp, &scene->vel_z)) goto fail;
    if (!ri32(fp, &scene->z_max)) goto fail;
    if (!ri32(fp, &scene->floor_w) || !ri32(fp, &scene->floor_h) || !ri32(fp, &scene->floor_origin))
        goto fail;
    if (!ru16(fp, &scene->n_idle) || !ru16(fp, &scene->n_walk) || !ru16(fp, &scene->n_bash)) goto fail;
    if (!ru16(fp, &vw)) goto fail;
    if (!ru16(fp, &scene->n_will) || !ru16(fp, &scene->n_p28) || !ru16(fp, &scene->n_p29) ||
        !ru16(fp, &scene->n_p45))
        goto fail;
    if (!rs16(fp, &scene->ax) || !rs16(fp, &scene->ay) || !rs16(fp, &scene->aw) || !rs16(fp, &scene->ah) ||
        !rs16(fp, &scene->az) || !rs16(fp, &scene->azs) || !rs16(fp, &scene->dmg28))
        goto fail;
    if (!rs16(fp, &scene->cx) || !rs16(fp, &scene->cy) || !rs16(fp, &scene->cw) || !rs16(fp, &scene->ch) ||
        !rs16(fp, &scene->cz) || !rs16(fp, &scene->czs) || !rs16(fp, &scene->dmg45))
        goto fail;
    if (!rs8(fp, &scene->hx) || !rs8(fp, &scene->hy) || !ru8(fp, &scene->hw) || !ru8(fp, &scene->hh) ||
        !rs8(fp, &scene->hz) || !ru8(fp, &scene->hzs))
        goto fail;
    if (!rs16(fp, &scene->grab_x) || !rs16(fp, &scene->grab_z)) goto fail;
    if (!ri32(fp, &scene->n_foes) || scene->n_foes < 0 || scene->n_foes > 4) goto fail;
    for (vw = 0; vw < scene->n_foes; vw++) {
        if (!ri32(fp, &scene->foes[vw].x) || !ri32(fp, &scene->foes[vw].z) || !ri32(fp, &scene->foes[vw].hp) ||
            !ri32(fp, &scene->foes[vw].sheet))
            goto fail;
        scene->foes[vw].alive = 1;
    }
    ground_n = (size_t)scene->floor_w * scene->floor_h;
    scene->ground = malloc(ground_n * 4);
    if (!scene->ground || !rd(fp, scene->ground, ground_n * 4)) goto fail;
    map_bytes = (size_t)scene->map_w * scene->map_h * 4;
    scene->far_px = malloc(map_bytes);
    scene->main_px = malloc(map_bytes);
    if (!scene->far_px || !scene->main_px) goto fail;
    if (!rd(fp, scene->far_px, map_bytes) || !rd(fp, scene->main_px, map_bytes)) goto fail;
    scene->idle = load_frames(fp, scene->n_idle);
    scene->walk = load_frames(fp, scene->n_walk);
    scene->bash = load_frames(fp, scene->n_bash);
    scene->will = load_frames(fp, scene->n_will);
    scene->p28 = load_frames(fp, scene->n_p28);
    scene->p29 = load_frames(fp, scene->n_p29);
    scene->p45 = load_frames(fp, scene->n_p45);
    if (!scene->idle || !scene->walk || !scene->bash || !scene->will || !scene->p28 || !scene->p29 ||
        !scene->p45)
        goto fail;
    fclose(fp);
    return scene;
fail:
    fclose(fp);
    free(scene);
    return NULL;
}

static void blit(uint8_t *dst, int dw, int dh, const uint8_t *src, int sw, int sh,
                 int dx, int dy, int flip) {
    int y, x;
    for (y = 0; y < sh; y++) {
        int sy = dy + y;
        if (sy < 0 || sy >= dh) continue;
        for (x = 0; x < sw; x++) {
            int sx = flip ? dx + (sw - 1 - x) : dx + x;
            const uint8_t *p;
            uint8_t *q;
            if (sx < 0 || sx >= dw) continue;
            p = src + ((size_t)y * sw + x) * 4;
            if (p[3] == 0) continue;
            q = dst + ((size_t)sy * dw + sx) * 4;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = 255;
        }
    }
}

static void blit_map(uint8_t *dst, const uint8_t *src, int map_w, int map_h, int camera) {
    int y, x;
    if (camera < 0) camera = 0;
    if (camera > map_w - VIEW_W) camera = map_w - VIEW_W;
    for (y = 0; y < VIEW_H && y < map_h; y++) {
        for (x = 0; x < VIEW_W; x++) {
            const uint8_t *p = src + ((size_t)y * map_w + (camera + x)) * 4;
            uint8_t *q = dst + ((size_t)y * VIEW_W + x) * 4;
            if (p[3] == 0) continue;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = 255;
        }
    }
}

static int ground_at(const Scene *scene, int x, int z) {
    int tile_x = scene->floor_origin + (x >> 4);
    int tile_y = (z >> 4) + 1;
    int32_t value;
    if (tile_x < 0 || tile_y < 0 || tile_x >= scene->floor_w || tile_y >= scene->floor_h)
        return -1000000;
    value = scene->ground[tile_y * scene->floor_w + tile_x];
    return value;
}

typedef struct {
    int x_fp, z_fp, vx, vz, facing, act, frame, timer, shown;
    int punch_act, alt;
} Body;

static void restart(Body *body, int act) {
    body->act = act;
    body->frame = 0;
    body->timer = 0;
    body->shown = 0;
}

static int spans(int a, int aw, int b, int bw) {
    return a < b + bw && b < a + aw;
}

static void strike(Scene *scene, Body *body, const Frame *pose) {
    int i, ax, ay, aw, ah, az, azs, dmg, px, pz;
    if ((pose->flags & 0x28) != 8) return;
    if (body->punch_act == 45) {
        ax = scene->cx;
        ay = scene->cy;
        aw = scene->cw;
        ah = scene->ch;
        az = scene->cz;
        azs = scene->czs;
        dmg = scene->dmg45;
    } else {
        ax = scene->ax;
        ay = scene->ay;
        aw = scene->aw;
        ah = scene->ah;
        az = scene->az;
        azs = scene->azs;
        dmg = scene->dmg28;
    }
    /* EncountEnemyCreate stores -1 at +0x16d. DamageCalcSub adds half
       when that byte is negative and applet+0x2580 is 0. */
    dmg += dmg >> 1;
    px = body->x_fp >> 16;
    pz = body->z_fp >> 16;
    if (body->facing) ax = px - ax - aw;
    else ax = px + ax;
    az = pz + az;
    ay = 0xEF + ay;
    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        int left, top, depth;
        if (!foe->alive || foe->hit) continue;
        /* Enemies are born off the right edge and face left (facing 0). */
        left = foe->x + scene->hx;
        top = 0xEF + scene->hy;
        depth = foe->z + scene->hz;
        if (!spans(ax, aw, left, scene->hw)) continue;
        if (!spans(az, azs, depth, scene->hzs)) continue;
        if (!spans(ay, ah, top, scene->hh)) continue;
        foe->hit = 1;
        foe->hp -= dmg;
        if (foe->hp <= 0) {
            foe->hp = 0;
            foe->alive = 0;
        }
    }
}

static Frame *hero_pose(const Scene *scene, const Body *body) {
    if (body->punch_act == 28) return &scene->p28[body->frame];
    if (body->punch_act == 29) return &scene->p29[body->frame];
    if (body->punch_act == 45) return &scene->p45[body->frame];
    if (body->act == 1) return &scene->walk[body->frame];
    return &scene->idle[body->frame];
}

static int punch_count(const Scene *scene, int act) {
    if (act == 28) return scene->n_p28;
    if (act == 29) return scene->n_p29;
    return scene->n_p45;
}

static void step_body(Scene *scene, Body *body, int left, int right, int up, int down, int attack) {
    Frame *group;
    int count;
    int any = left || right || up || down;
    int i;
    if (attack && body->punch_act == 0) {
        int px = body->x_fp >> 16;
        int pz = body->z_fp >> 16;
        int close = 0;
        for (i = 0; i < scene->n_foes; i++) {
            Foe *foe = &scene->foes[i];
            int dx, dz;
            if (!foe->alive) continue;
            dx = px - foe->x;
            if (dx < 0) dx = -dx;
            dz = pz - foe->z;
            if (dz < 0) dz = -dz;
            if (dx <= scene->grab_x && dz <= scene->grab_z) close = 1;
            foe->hit = 0;
        }
        body->punch_act = close ? 45 : (body->alt ? 29 : 28);
        body->alt ^= 1;
        body->frame = 0;
        body->timer = 0;
        body->shown = 0;
        body->vx = 0;
        body->vz = 0;
    }
    if (body->punch_act) {
        body->vx = 0;
        body->vz = 0;
        if (body->punch_act == 45 && body->frame < 8) {
            /* SET_MOVE arg1 -393216 is shifted left 1, then facing flips the sign. */
            body->vx = body->facing ? 786432 : -786432;
        }
    } else if (any) {
        body->vx = 0;
        body->vz = 0;
        if (up) body->vz = scene->vel_z;
        if (down) body->vz = -scene->vel_z;
        if (left) {
            body->vx = scene->vel_x;
            body->facing = 0;
        }
        if (right) {
            body->vx = -scene->vel_x;
            body->facing = 1;
        }
        if ((left || right) && body->act != 1) restart(body, 1);
    } else if (!body->punch_act) {
        body->vx = 0;
        body->vz = 0;
        if (body->act != 0) restart(body, 0);
    }
    body->x_fp += body->vx;
    body->z_fp += body->vz;
    if (body->x_fp < 0) body->x_fp = 0;
    if ((body->x_fp >> 16) >= scene->map_w) body->x_fp = (scene->map_w - 1) << 16;
    if (body->z_fp < 0) body->z_fp = 0;
    if ((body->z_fp >> 16) > scene->z_max) body->z_fp = scene->z_max << 16;

    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        Frame *poses = foe->sheet ? scene->will : scene->bash;
        int n = foe->sheet ? scene->n_will : scene->n_bash;
        if (!foe->alive) continue;
        if (!foe->shown) {
            foe->timer = poses[0].duration;
            foe->shown = 1;
        } else {
            foe->timer -= 1;
            if (foe->timer <= 0) {
                foe->frame = (foe->frame + 1) % n;
                foe->timer = poses[foe->frame].duration;
            }
        }
    }
    if (body->punch_act) {
        group = hero_pose(scene, body);
        count = punch_count(scene, body->punch_act);
        if (!body->shown) {
            body->timer = group->duration;
            body->shown = 1;
            strike(scene, body, group);
            return;
        }
        body->timer -= 1;
        if (body->timer > 0) return;
        body->frame += 1;
        if (body->frame >= count) {
            body->punch_act = 0;
            restart(body, 0);
            return;
        }
        body->timer = hero_pose(scene, body)->duration;
        strike(scene, body, hero_pose(scene, body));
        return;
    }
    group = body->act == 1 ? scene->walk : scene->idle;
    count = body->act == 1 ? scene->n_walk : scene->n_idle;
    if (!body->shown) {
        body->timer = group[body->frame].duration;
        body->shown = 1;
        return;
    }
    body->timer -= 1;
    if (body->timer > 0) return;
    body->frame = (body->frame + 1) % count;
    body->timer = group[body->frame].duration;
}

static void paint(const Scene *scene, const Body *hero, uint8_t *fb) {
    int camera, ground, sx, sy, i, order[4], norder = 0;
    Frame *pose;
    memset(fb, 0, (size_t)VIEW_W * VIEW_H * 4);
    for (i = 0; i < VIEW_W * VIEW_H; i++) {
        fb[i * 4 + 0] = 24;
        fb[i * 4 + 1] = 28;
        fb[i * 4 + 2] = 36;
        fb[i * 4 + 3] = 255;
    }
    camera = (hero->x_fp >> 16) - 160;
    if (camera < 0) camera = 0;
    if (camera > scene->map_w - VIEW_W) camera = scene->map_w - VIEW_W;
    blit_map(fb, scene->far_px, scene->map_w, scene->map_h, camera >> 2);
    blit_map(fb, scene->main_px, scene->map_w, scene->map_h, camera);

    for (i = 0; i < scene->n_foes; i++) order[norder++] = i;
    for (i = 0; i < norder; i++) {
        int j;
        for (j = i + 1; j < norder; j++) {
            if (scene->foes[order[j]].z > scene->foes[order[i]].z) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }
    for (i = 0; i < norder; i++) {
        const Foe *foe = &scene->foes[order[i]];
        const Frame *poses = foe->sheet ? scene->will : scene->bash;
        const Frame *fr;
        if (!foe->alive) continue;
        fr = &poses[foe->frame];
        ground = ground_at(scene, foe->x, foe->z);
        if (ground < -10000) ground = 0xEF;
        sx = foe->x - camera;
        sy = ground - foe->z;
        blit(fb, VIEW_W, VIEW_H, fr->px, fr->w, fr->h, sx - fr->ox, sy - fr->oy, 0);
    }

    pose = hero_pose(scene, hero);
    ground = ground_at(scene, hero->x_fp >> 16, hero->z_fp >> 16);
    if (ground < -10000) ground = 0xEF;
    sx = (hero->x_fp >> 16) - camera;
    sy = ground - (hero->z_fp >> 16);
    blit(fb, VIEW_W, VIEW_H, pose->px, pose->w, pose->h, sx - pose->ox, sy - pose->oy, hero->facing == 1);
}

static int write_ppm(const char *path, const uint8_t *fb) {
    FILE *fp = fopen(path, "wb");
    int y, x;
    if (!fp) return 0;
    fprintf(fp, "P6\n%d %d\n255\n", VIEW_W * SCALE, VIEW_H * SCALE);
    for (y = 0; y < VIEW_H * SCALE; y++) {
        for (x = 0; x < VIEW_W * SCALE; x++) {
            const uint8_t *p = fb + ((size_t)(y / SCALE) * VIEW_W + (x / SCALE)) * 4;
            fputc(p[0], fp);
            fputc(p[1], fp);
            fputc(p[2], fp);
        }
    }
    fclose(fp);
    return 1;
}

#if defined(_WIN32)
#include <windows.h>

static int g_left, g_right, g_up, g_down, g_atk, g_quit;
static uint8_t *g_fb;

typedef struct {
    BITMAPINFOHEADER header;
    DWORD masks[3];
} DibInfo;

static DibInfo g_bmi;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int down = msg == WM_KEYDOWN;
        if (wp == VK_LEFT || wp == 'A') g_left = down;
        if (wp == VK_RIGHT || wp == 'D') g_right = down;
        if (wp == VK_UP || wp == 'W') g_up = down;
        if (wp == VK_DOWN || wp == 'S') g_down = down;
        if (wp == 'J') g_atk = down;
        if (wp == VK_ESCAPE) g_quit = 1;
        return 0;
    }
    case WM_DESTROY:
        g_quit = 1;
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        StretchDIBits(hdc, 0, 0, VIEW_W * SCALE, VIEW_H * SCALE, 0, 0, VIEW_W, VIEW_H,
                      g_fb, (BITMAPINFO *)&g_bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int run_window(Scene *scene) {
    WNDCLASS wc;
    HWND hwnd;
    Body hero;
    int was_atk = 0;
    MSG msg;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "dd1s";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    hwnd = CreateWindow("dd1s", "Double Dragon stage 1 - first encounter",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                        CW_USEDEFAULT, CW_USEDEFAULT, VIEW_W * SCALE + 16, VIEW_H * SCALE + 39,
                        NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.header.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.header.biWidth = VIEW_W;
    g_bmi.header.biHeight = -VIEW_H;
    g_bmi.header.biPlanes = 1;
    g_bmi.header.biBitCount = 32;
    g_bmi.header.biCompression = BI_BITFIELDS;
    g_bmi.masks[0] = 0x000000FF;
    g_bmi.masks[1] = 0x0000FF00;
    g_bmi.masks[2] = 0x00FF0000;
    g_fb = malloc((size_t)VIEW_W * VIEW_H * 4);
    memset(&hero, 0, sizeof(hero));
    hero.x_fp = scene->spawn_x << 16;
    hero.z_fp = scene->spawn_z << 16;
    hero.facing = 1;
    while (!g_quit) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        step_body(scene, &hero, g_left, g_right, g_up, g_down, g_atk && !was_atk);
        was_atk = g_atk;
        paint(scene, &hero, g_fb);
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        Sleep(16);
    }
    return 0;
}
#else
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

static int run_window(Scene *scene) {
    Display *display = XOpenDisplay(NULL);
    int screen;
    Window window;
    GC gc;
    XImage *image;
    Body hero;
    uint8_t *fb;
    uint8_t *scaled;
    int atk = 0, was_atk = 0;
    int left = 0, right = 0, up = 0, down = 0;
    if (!display) {
        fprintf(stderr, "no X display\n");
        return 1;
    }
    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
                                 VIEW_W * SCALE, VIEW_H * SCALE, 0, 0, 0);
    XStoreName(display, window, "Double Dragon stage 1 — Jimmy / BASH");
    XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask);
    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);
    fb = malloc((size_t)VIEW_W * VIEW_H * 4);
    scaled = malloc((size_t)VIEW_W * SCALE * VIEW_H * SCALE * 4);
    image = XCreateImage(display, DefaultVisual(display, screen), 24, ZPixmap, 0,
                         (char *)scaled, VIEW_W * SCALE, VIEW_H * SCALE, 32, 0);
    memset(&hero, 0, sizeof(hero));
    hero.x_fp = scene->spawn_x << 16;
    hero.z_fp = scene->spawn_z << 16;
    hero.facing = 1;
    for (;;) {
        while (XPending(display)) {
            XEvent event;
            KeySym key;
            int pressed;
            XNextEvent(display, &event);
            if (event.type != KeyPress && event.type != KeyRelease) continue;
            pressed = event.type == KeyPress;
            key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Left || key == XK_a) left = pressed;
            if (key == XK_Right || key == XK_d) right = pressed;
            if (key == XK_Up || key == XK_w) up = pressed;
            if (key == XK_Down || key == XK_s) down = pressed;
            if (key == XK_j) atk = pressed;
            if (key == XK_Escape) goto done;
        }
        step_body(scene, &hero, left, right, up, down, atk && !was_atk);
        was_atk = atk;
        paint(scene, &hero, fb);
        {
            int y, x;
            for (y = 0; y < VIEW_H * SCALE; y++) {
                for (x = 0; x < VIEW_W * SCALE; x++) {
                    const uint8_t *p = fb + ((size_t)(y / SCALE) * VIEW_W + (x / SCALE)) * 4;
                    uint8_t *q = scaled + ((size_t)y * VIEW_W * SCALE + x) * 4;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    q[3] = 255;
                }
            }
        }
        XPutImage(display, window, gc, image, 0, 0, 0, 0, VIEW_W * SCALE, VIEW_H * SCALE);
        usleep(16000);
    }
done:
    free(scaled);
    image->data = NULL;
    XCloseDisplay(display);
    return 0;
}
#endif

static int run_dump(Scene *scene, const char *path, int frames, int hold) {
    Body hero;
    uint8_t *fb = malloc((size_t)VIEW_W * VIEW_H * 4);
    int i, fired = 0;
    memset(&hero, 0, sizeof(hero));
    hero.x_fp = scene->spawn_x << 16;
    hero.z_fp = scene->spawn_z << 16;
    hero.facing = 1;
    for (i = 0; i < frames; i++) {
        int left = hold == 1, right = hold == 2 || hold == 5, up = hold == 3, down = hold == 4;
        int attack = 0;
        if (hold == 5 && !fired && (hero.x_fp >> 16) >= 430) {
            attack = 1;
            fired = 1;
        }
        step_body(scene, &hero, left, right, up, down, attack);
        paint(scene, &hero, fb);
    }
    if (!write_ppm(path, fb)) return 1;
    printf("x %d z %d act %d frame %d facing %d\n", hero.x_fp >> 16, hero.z_fp >> 16,
           hero.act, hero.frame, hero.facing);
    if (hold == 5) {
        printf("hp");
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].hp);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    Scene *scene;
    const char *bin = argc > 1 ? argv[1] : "scene.bin";
    if (argc > 2 && strcmp(argv[2], "--dump") == 0) {
        int frames = argc > 4 ? atoi(argv[4]) : 8;
        int hold = 0;
        const char *path = argc > 3 ? argv[3] : "frame.ppm";
        if (argc > 5 && strcmp(argv[5], "right") == 0) hold = 2;
        if (argc > 5 && strcmp(argv[5], "left") == 0) hold = 1;
        if (argc > 5 && strcmp(argv[5], "up") == 0) hold = 3;
        if (argc > 5 && strcmp(argv[5], "meet") == 0) hold = 5;
        scene = load_scene(bin);
        if (!scene) {
            fprintf(stderr, "could not read %s\n", bin);
            return 1;
        }
        return run_dump(scene, path, frames, hold);
    }
    scene = load_scene(bin);
    if (!scene) {
        fprintf(stderr, "could not read %s\n", bin);
        return 1;
    }
    return run_window(scene);
}
