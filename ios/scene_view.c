/* Stage 1 slice driven by recovered iOS tables.

   Map: ciStage01MapChip / ciStage01MapChipNear, column-major,
   MapChipDraw at 0x12f24. Near layer scrolls camera >> 2.
   Jimmy: acts 0 and 1, ciBillyMoveData speeds, GT_MoveCheck /
   GT_FMoveSet / GT_ReadySet. BASH uses ciZakoC00* and BASH.obm.
   The enemy X is a viewer offset; the encounter table is not decoded.
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
    uint8_t *px;
} Frame;

typedef struct {
    int map_w, map_h, spawn_x, spawn_z, vel_x, vel_z, z_max;
    int floor_w, floor_h, floor_origin, enemy_x;
    int n_idle, n_walk, n_bash;
    int32_t *ground;
    uint8_t *far_px;
    uint8_t *main_px;
    Frame *idle;
    Frame *walk;
    Frame *bash;
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

static Frame *load_frames(FILE *fp, int count) {
    Frame *frames = calloc(count, sizeof(Frame));
    int i;
    if (!frames) return NULL;
    for (i = 0; i < count; i++) {
        uint16_t duration, w, h;
        int16_t ox, oy;
        if (!rd(fp, &duration, 2) || !rd(fp, &w, 2) || !rd(fp, &h, 2) ||
            !rd(fp, &ox, 2) || !rd(fp, &oy, 2)) {
            return NULL;
        }
        frames[i].duration = duration;
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
    if (!ru16(fp, &version) || version != 1) goto fail;
    if (!ru16(fp, &vw) || !ru16(fp, &vh)) goto fail;
    if (!ri32(fp, &scene->map_w) || !ri32(fp, &scene->map_h)) goto fail;
    if (!ri32(fp, &scene->spawn_x) || !ri32(fp, &scene->spawn_z)) goto fail;
    if (!ri32(fp, &scene->vel_x) || !ri32(fp, &scene->vel_z)) goto fail;
    if (!ri32(fp, &scene->z_max)) goto fail;
    if (!ri32(fp, &scene->floor_w) || !ri32(fp, &scene->floor_h) || !ri32(fp, &scene->floor_origin))
        goto fail;
    if (!ru16(fp, &scene->n_idle) || !ru16(fp, &scene->n_walk) || !ru16(fp, &scene->n_bash)) goto fail;
    if (!ru16(fp, &scene->enemy_x)) goto fail;
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
    if (!scene->idle || !scene->walk || !scene->bash) goto fail;
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
} Body;

static void restart(Body *body, int act) {
    body->act = act;
    body->frame = 0;
    body->timer = 0;
    body->shown = 0;
}

static void step_body(const Scene *scene, Body *body, int left, int right, int up, int down) {
    Frame *group;
    int count;
    int any = left || right || up || down;
    if (any) {
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
    } else {
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

static void paint(const Scene *scene, const Body *hero, int bash_frame, uint8_t *fb) {
    int camera, ground, sx, sy, i;
    Frame *group;
    Frame *bash;
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

    bash = &scene->bash[bash_frame % scene->n_bash];
    ground = ground_at(scene, scene->enemy_x, scene->spawn_z);
    if (ground < -10000) ground = 0xEF;
    sx = scene->enemy_x - camera;
    sy = ground - scene->spawn_z;
    blit(fb, VIEW_W, VIEW_H, bash->px, bash->w, bash->h, sx - bash->ox, sy - bash->oy, 0);

    group = hero->act == 1 ? scene->walk : scene->idle;
    ground = ground_at(scene, hero->x_fp >> 16, hero->z_fp >> 16);
    if (ground < -10000) ground = 0xEF;
    sx = (hero->x_fp >> 16) - camera;
    sy = ground - (hero->z_fp >> 16);
    blit(fb, VIEW_W, VIEW_H, group[hero->frame].px, group[hero->frame].w, group[hero->frame].h,
         sx - group[hero->frame].ox, sy - group[hero->frame].oy, hero->facing == 1);
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

static int g_left, g_right, g_up, g_down, g_quit;
static uint8_t *g_fb;
static BITMAPINFO g_bmi;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int down = msg == WM_KEYDOWN;
        if (wp == VK_LEFT || wp == 'A') g_left = down;
        if (wp == VK_RIGHT || wp == 'D') g_right = down;
        if (wp == VK_UP || wp == 'W') g_up = down;
        if (wp == VK_DOWN || wp == 'S') g_down = down;
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
                      g_fb, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
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
    int bash_timer = 0, bash_frame = 0;
    MSG msg;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "dd1s";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    hwnd = CreateWindow("dd1s", "Double Dragon stage 1 — Jimmy / BASH",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                        CW_USEDEFAULT, CW_USEDEFAULT, VIEW_W * SCALE + 16, VIEW_H * SCALE + 39,
                        NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);
    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = VIEW_W;
    g_bmi.bmiHeader.biHeight = -VIEW_H;
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
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
        step_body(scene, &hero, g_left, g_right, g_up, g_down);
        bash_timer -= 1;
        if (bash_timer <= 0) {
            bash_frame = (bash_frame + 1) % scene->n_bash;
            bash_timer = scene->bash[bash_frame].duration;
        }
        paint(scene, &hero, bash_frame, g_fb);
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
    int bash_timer = 0, bash_frame = 0;
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
            if (key == XK_Escape) goto done;
        }
        step_body(scene, &hero, left, right, up, down);
        bash_timer -= 1;
        if (bash_timer <= 0) {
            bash_frame = (bash_frame + 1) % scene->n_bash;
            bash_timer = scene->bash[bash_frame].duration;
        }
        paint(scene, &hero, bash_frame, fb);
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
    int i, bash_frame = 0, bash_timer = 0;
    memset(&hero, 0, sizeof(hero));
    hero.x_fp = scene->spawn_x << 16;
    hero.z_fp = scene->spawn_z << 16;
    hero.facing = 1;
    for (i = 0; i < frames; i++) {
        int left = hold == 1, right = hold == 2, up = hold == 3, down = hold == 4;
        step_body(scene, &hero, left, right, up, down);
        bash_timer -= 1;
        if (bash_timer <= 0) {
            bash_frame = (bash_frame + 1) % scene->n_bash;
            bash_timer = scene->bash[bash_frame].duration;
        }
        paint(scene, &hero, bash_frame, fb);
    }
    if (!write_ppm(path, fb)) return 1;
    printf("x %d z %d act %d frame %d facing %d\n", hero.x_fp >> 16, hero.z_fp >> 16,
           hero.act, hero.frame, hero.facing);
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
