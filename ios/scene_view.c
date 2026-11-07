/* Stage 1 slice driven by recovered iOS tables.

   The packed map already has row 0 at the bottom of the bitmap.
   glOrthox uses top 0 and bottom -height. StoreSprite negates Y,
   so game +Y grows down from the top. The road tiles are the low
   map rows, and FloorGroundSet's 0xEF lands on that road only
   when those rows sit at the bottom.

   0x1faa2 writes 480 at +0x47a8 and 320 at +0x47ac. MapChipDraw
   subtracts 320, which is why row 0 of the packed bitmap is the road.

   Near layer scrolls camera >> 2. The opening kind-3 rows are Bolo
   (draw set 8) and Bash (draw set 6) at x = 480 + 20. The camera
   stays on scroll-lock record 0 until both counting tasks unlink.
   J is button index 4.
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
    int facing, mode, cmd, counts;
    int x_fp, z_fp, y_fp, vx, vz, yv, ya;
} Foe;

typedef struct {
    int map_w, map_h, spawn_x, spawn_z, vel_x, vel_z, z_max;
    int floor_w, floor_h, floor_origin;
    int n_idle, n_walk, n_bash, n_will, n_p28, n_p29, n_p45, n_foes;
    int ax, ay, aw, ah, az, azs, dmg28;
    int cx, cy, cw, ch, cz, czs, dmg45;
    int hx, hy, hw, hh, hz, hzs, grab_x, grab_z;
    int n_zwb, n_zww, n_zpb, n_zpw, n_zdb, n_jd, n_zdw, n_zlb, n_zlw, n_jl;
    int n_zfb, n_zfw, n_zgb, n_zgw, n_jf, n_jg;
    int n_zlib, n_zliw, n_jlie, n_zdib, n_zdiw, n_jdie;
    int zvx, zvz, hero_hp;
    int zx, zy, zw, zh, zz, zzs, zdmg;
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
    Frame *zwb;
    Frame *zww;
    Frame *zpb;
    Frame *zpw;
    Frame *zdb;
    Frame *zdw;
    Frame *jd;
    Frame *zlb;
    Frame *zlw;
    Frame *jl;
    Frame *zfb;
    Frame *zfw;
    Frame *zgb;
    Frame *zgw;
    Frame *jf;
    Frame *jg;
    Frame *zlib;
    Frame *zliw;
    Frame *jlie;
    Frame *zdib;
    Frame *zdiw;
    Frame *jdie;
    Frame *bolo;
    Frame *zwo;
    Frame *zpo;
    Frame *zdo;
    Frame *zlo;
    Frame *zfo;
    Frame *zgo;
    Frame *zlio;
    Frame *zdio;
    Frame *lidle;
    Frame *lwalk;
    Frame *lpunch;
    Frame *ldown;
    Frame *lland;
    Frame *lfall;
    Frame *lget;
    Frame *llie;
    Frame *ldie;
    uint8_t *floor_id;
    uint8_t fmt5[256];
    int ln[9];
    int n_rows, row_i, trigger;
    int rows[24][8];
    int camera, lock_i, quota;
    Foe foes[12];
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

static void consume_rows(Scene *scene);

static Scene *load_scene(const char *path) {
    FILE *fp = fopen(path, "rb");
    char magic[4];
    int version, vw, vh;
    Scene *scene;
    size_t map_bytes, ground_n;
    if (!fp) return NULL;
    scene = calloc(1, sizeof(Scene));
    if (!scene || !rd(fp, magic, 4) || memcmp(magic, "DD1S", 4) != 0) goto fail;
    if (!ru16(fp, &version) || version != 7) goto fail;
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
    if (!ri32(fp, &scene->n_foes) || scene->n_foes < 0 || scene->n_foes > 12) goto fail;
    for (vw = 0; vw < scene->n_foes; vw++) {
        if (!ri32(fp, &scene->foes[vw].x) || !ri32(fp, &scene->foes[vw].z) || !ri32(fp, &scene->foes[vw].hp) ||
            !ri32(fp, &scene->foes[vw].sheet))
            goto fail;
        scene->foes[vw].alive = 1;
        scene->foes[vw].counts = 1;
        scene->foes[vw].x_fp = scene->foes[vw].x << 16;
        scene->foes[vw].z_fp = scene->foes[vw].z << 16;
        scene->foes[vw].y_fp = 0xEF << 16;
    }
    if (!ru16(fp, &scene->n_zwb) || !ru16(fp, &scene->n_zww) || !ru16(fp, &scene->n_zpb) ||
        !ru16(fp, &scene->n_zpw) || !ru16(fp, &scene->n_zdb) || !ru16(fp, &scene->n_jd))
        goto fail;
    if (!ri32(fp, &scene->zvx) || !ri32(fp, &scene->zvz) || !ri32(fp, &scene->hero_hp) || !ri32(fp, &vw))
        goto fail;
    if (!rs16(fp, &scene->zx) || !rs16(fp, &scene->zy) || !rs16(fp, &scene->zw) || !rs16(fp, &scene->zh) ||
        !rs16(fp, &scene->zz) || !rs16(fp, &scene->zzs) || !rs16(fp, &scene->zdmg))
        goto fail;
    if (!ru16(fp, &scene->n_zdw) || !ru16(fp, &scene->n_zlb) || !ru16(fp, &scene->n_zlw) ||
        !ru16(fp, &scene->n_jl))
        goto fail;
    if (!ru16(fp, &scene->n_zfb) || !ru16(fp, &scene->n_zfw) || !ru16(fp, &scene->n_zgb) ||
        !ru16(fp, &scene->n_zgw) || !ru16(fp, &scene->n_jf) || !ru16(fp, &scene->n_jg))
        goto fail;
    if (!ru16(fp, &scene->n_zlib) || !ru16(fp, &scene->n_zliw) || !ru16(fp, &scene->n_jlie) ||
        !ru16(fp, &scene->n_zdib) || !ru16(fp, &scene->n_zdiw) || !ru16(fp, &scene->n_jdie))
        goto fail;
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
    scene->zwb = load_frames(fp, scene->n_zwb);
    scene->zww = load_frames(fp, scene->n_zww);
    scene->zpb = load_frames(fp, scene->n_zpb);
    scene->zpw = load_frames(fp, scene->n_zpw);
    scene->zdb = load_frames(fp, scene->n_zdb);
    scene->zdw = load_frames(fp, scene->n_zdw);
    scene->jd = load_frames(fp, scene->n_jd);
    scene->zlb = load_frames(fp, scene->n_zlb);
    scene->zlw = load_frames(fp, scene->n_zlw);
    scene->jl = load_frames(fp, scene->n_jl);
    scene->zfb = load_frames(fp, scene->n_zfb);
    scene->zfw = load_frames(fp, scene->n_zfw);
    scene->zgb = load_frames(fp, scene->n_zgb);
    scene->zgw = load_frames(fp, scene->n_zgw);
    scene->jf = load_frames(fp, scene->n_jf);
    scene->jg = load_frames(fp, scene->n_jg);
    scene->zlib = load_frames(fp, scene->n_zlib);
    scene->zliw = load_frames(fp, scene->n_zliw);
    scene->jlie = load_frames(fp, scene->n_jlie);
    scene->zdib = load_frames(fp, scene->n_zdib);
    scene->zdiw = load_frames(fp, scene->n_zdiw);
    scene->jdie = load_frames(fp, scene->n_jdie);
    scene->bolo = load_frames(fp, scene->n_will);
    scene->zwo = load_frames(fp, scene->n_zww);
    scene->zpo = load_frames(fp, scene->n_zpw);
    scene->zdo = load_frames(fp, scene->n_zdw);
    scene->zlo = load_frames(fp, scene->n_zlw);
    scene->zfo = load_frames(fp, scene->n_zfw);
    scene->zgo = load_frames(fp, scene->n_zgw);
    scene->zlio = load_frames(fp, scene->n_zliw);
    scene->zdio = load_frames(fp, scene->n_zdiw);
    for (vw = 0; vw < 9; vw++) {
        if (!ru16(fp, &scene->ln[vw])) goto fail;
    }
    scene->lidle = load_frames(fp, scene->ln[0]);
    scene->lwalk = load_frames(fp, scene->ln[1]);
    scene->lpunch = load_frames(fp, scene->ln[2]);
    scene->ldown = load_frames(fp, scene->ln[3]);
    scene->lland = load_frames(fp, scene->ln[4]);
    scene->lfall = load_frames(fp, scene->ln[5]);
    scene->lget = load_frames(fp, scene->ln[6]);
    scene->llie = load_frames(fp, scene->ln[7]);
    scene->ldie = load_frames(fp, scene->ln[8]);
    ground_n = (size_t)scene->floor_w * scene->floor_h;
    scene->floor_id = malloc(ground_n);
    if (!scene->floor_id || !rd(fp, scene->floor_id, ground_n) || !rd(fp, scene->fmt5, 256)) goto fail;
    if (!ri32(fp, &scene->n_rows) || scene->n_rows < 0 || scene->n_rows > 24) goto fail;
    for (vw = 0; vw < scene->n_rows; vw++) {
        int k;
        for (k = 0; k < 8; k++) {
            if (!ri32(fp, &scene->rows[vw][k])) goto fail;
        }
    }
    scene->trigger = -1;
    if (!scene->idle || !scene->walk || !scene->bash || !scene->will || !scene->p28 || !scene->p29 ||
        !scene->p45 || !scene->zwb || !scene->zww || !scene->zpb || !scene->zpw || !scene->zdb ||
        !scene->zdw || !scene->jd || !scene->zlb || !scene->zlw || !scene->jl || !scene->zfb ||
        !scene->zfw || !scene->zgb || !scene->zgw || !scene->jf || !scene->jg || !scene->zlib ||
        !scene->zliw || !scene->jlie || !scene->zdib || !scene->zdiw || !scene->jdie || !scene->bolo ||
        !scene->zwo || !scene->zpo || !scene->zdo || !scene->zlo || !scene->zfo || !scene->zgo ||
        !scene->zlio || !scene->zdio || !scene->lidle || !scene->lwalk || !scene->lpunch ||
        !scene->ldown || !scene->lland || !scene->lfall || !scene->lget || !scene->llie || !scene->ldie)
        goto fail;
    consume_rows(scene);
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
    int punch_act, alt, hp, mode;
    int y_fp, yv, ya;
} Body;

static void restart(Body *body, int act) {
    body->act = act;
    body->frame = 0;
    body->timer = 0;
    body->shown = 0;
}

static int scaled_damage(int raw, int defender_byte);

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
    px = body->x_fp >> 16;
    pz = body->z_fp >> 16;
    if (body->facing) ax = px - ax - aw;
    else ax = px + ax;
    az = pz + az;
    ay = 0xEF + ay;
    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        int left, top, depth;
        if (!foe->alive || foe->hit || foe->mode >= 2) continue;
        /* Enemies are born off the right edge and face left (facing 0). */
        left = foe->x + scene->hx;
        top = 0xEF + scene->hy;
        depth = foe->z + scene->hz;
        if (!spans(ax, aw, left, scene->hw)) continue;
        if (!spans(az, azs, depth, scene->hzs)) continue;
        if (!spans(ay, ah, top, scene->hh)) continue;
        foe->hit = 1;
        foe->hp -= scaled_damage(dmg, -1);
        foe->mode = 2;
        foe->frame = 0;
        foe->timer = 0;
        foe->shown = 0;
        foe->cmd = 0;
        foe->y_fp = 0xEF << 16;
        /* ciFukiData record 1: Jimmy record 5 byte +0x0f is 1. */
        foe->vx = px > foe->x ? 196608 : -196608;
        foe->yv = -1376256;
        foe->ya = 212992;
        if (foe->hp <= 0) foe->hp = 0;
    }
}

static Frame *hero_pose(const Scene *scene, const Body *body) {
    if (body->mode == 2 && scene->n_jd) return &scene->jd[body->frame % scene->n_jd];
    if (body->mode == 5 && scene->n_jf) return &scene->jf[body->frame % scene->n_jf];
    if (body->mode == 3 && scene->n_jl) return &scene->jl[body->frame % scene->n_jl];
    if (body->mode == 7 && scene->n_jlie) return &scene->jlie[body->frame % scene->n_jlie];
    if (body->mode == 6 && scene->n_jg) return &scene->jg[body->frame % scene->n_jg];
    if (body->mode == 8 && scene->n_jdie) return &scene->jdie[body->frame % scene->n_jdie];
    if (body->mode == 4 && scene->n_jdie) return &scene->jdie[scene->n_jdie - 1];
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

static uint32_t mt_state[624];
static int mt_index = 625;

static void mt_seed(uint32_t seed) {
    int i;
    mt_state[0] = seed;
    for (i = 1; i < 624; i++)
        mt_state[i] = 1812433253u * (mt_state[i - 1] ^ (mt_state[i - 1] >> 30)) + (uint32_t)i;
    mt_index = 624;
}

static uint32_t mt_draw(void) {
    uint32_t y;
    int i;
    if (mt_index >= 624) {
        for (i = 0; i < 624; i++) {
            uint32_t x = (mt_state[i] & 0x80000000u) + (mt_state[(i + 1) % 624] & 0x7fffffffu);
            uint32_t xa = x >> 1;
            if (x & 1u) xa ^= 0x9908b0dfu;
            mt_state[i] = mt_state[(i + 397) % 624] ^ xa;
        }
        mt_index = 0;
    }
    y = mt_state[mt_index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    /* COM_Ready calls the wrapper at 0x1379c, which shifts the temper right by 1. */
    return y >> 1;
}

static int scaled_damage(int raw, int defender_byte) {
    /* +0x2580 stays 0 unless +0x2538 bit 0x800000 or 0x200000 is set.
       Those stores are in ReadyCharSelectScreen. A zero flag word skips them,
       so DamageCalcSub takes the difficulty-0 arm. */
    if (defender_byte < 0) return raw + (raw >> 1);
    return raw - (raw >> 2);
}

/* kind: 0 idle, 1 walk, 2 punch, 3 down, 4 land, 5 fall, 6 lie, 7 get, 8 die.
   Sheet 0 is draw set 6 (BASH), 1 is draw set 7 (WILLIAMS), 2 is draw set 8 (BOLO). */
static void sheet_anim(const Scene *scene, int sheet, int kind, const Frame **frames, int *n) {
    const Frame *table[4][9] = {
        {scene->bash, scene->zwb, scene->zpb, scene->zdb, scene->zlb, scene->zfb, scene->zlib, scene->zgb, scene->zdib},
        {scene->will, scene->zww, scene->zpw, scene->zdw, scene->zlw, scene->zfw, scene->zliw, scene->zgw, scene->zdiw},
        {scene->bolo, scene->zwo, scene->zpo, scene->zdo, scene->zlo, scene->zfo, scene->zlio, scene->zgo, scene->zdio},
        {scene->lidle, scene->lwalk, scene->lpunch, scene->ldown, scene->lland, scene->lfall, scene->llie, scene->lget, scene->ldie},
    };
    const int counts[4][9] = {
        {scene->n_bash, scene->n_zwb, scene->n_zpb, scene->n_zdb, scene->n_zlb, scene->n_zfb, scene->n_zlib, scene->n_zgb, scene->n_zdib},
        {scene->n_will, scene->n_zww, scene->n_zpw, scene->n_zdw, scene->n_zlw, scene->n_zfw, scene->n_zliw, scene->n_zgw, scene->n_zdiw},
        {scene->n_will, scene->n_zww, scene->n_zpw, scene->n_zdw, scene->n_zlw, scene->n_zfw, scene->n_zliw, scene->n_zgw, scene->n_zdiw},
        {scene->ln[0], scene->ln[1], scene->ln[2], scene->ln[3], scene->ln[4], scene->ln[5], scene->ln[7], scene->ln[6], scene->ln[8]},
    };
    int bank = sheet >= 0 && sheet <= 3 ? sheet : 0;
    if (kind < 0 || kind > 8) kind = 0;
    *frames = table[bank][kind];
    *n = counts[bank][kind];
}

static int foe_kind(const Foe *foe) {
    if (foe->mode == 9 || foe->mode == 5) return 5;
    if (foe->mode == 8) return 8;
    if (foe->mode == 7) return 6;
    if (foe->mode == 6) return 7;
    if (foe->mode == 5) return 5;
    if (foe->mode == 3 || foe->mode == 4) return 4;
    if (foe->mode == 2) return 3;
    if (foe->mode == 1) return 2;
    if (foe->vx || foe->vz) return 1;
    return 0;
}

static const Frame *foe_pose(const Scene *scene, const Foe *foe) {
    const Frame *frames;
    int n, kind = foe_kind(foe);
    sheet_anim(scene, foe->sheet, kind, &frames, &n);
    if (!frames || n <= 0) return &scene->bash[0];
    if (foe->mode == 4) return &frames[n - 1];
    return &frames[foe->frame % n];
}

static int foe_count(const Scene *scene, const Foe *foe) {
    const Frame *frames;
    int n;
    sheet_anim(scene, foe->sheet, foe_kind(foe), &frames, &n);
    return n > 0 ? n : 1;
}

static void hurt_hero(Scene *scene, Body *hero, int attacker_x) {
    int raw = scene->zdmg;
    hero->hp -= scaled_damage(raw, 0);
    if (hero->hp < 0) hero->hp = 0;
    hero->mode = 2;
    hero->frame = 0;
    hero->timer = 0;
    hero->shown = 0;
    hero->punch_act = 0;
    hero->y_fp = 0xEF << 16;
    /* ciFukiData record 0: zako attack byte +0x0f is 0. X -327680, Y -1048576, accel 212992. */
    hero->vx = attacker_x > (hero->x_fp >> 16) ? -327680 : 327680;
    hero->yv = -1048576;
    hero->ya = 212992;
    hero->vz = 0;
}

static void foe_hits_hero(Scene *scene, Foe *foe, Body *hero) {
    int px, ax, left, depth, top, hx, hy, hz;
    const Frame *pose;
    if (hero->mode != 0 || foe->hit || foe->sheet == 3) return;
    pose = foe_pose(scene, foe);
    if ((pose->flags & 0x28) != 8) return;
    px = foe->x_fp >> 16;
    ax = scene->zx;
    if (foe->facing) left = px - ax - scene->zw;
    else left = px + ax;
    depth = (foe->z_fp >> 16) + scene->zz;
    top = 0xEF + scene->zy;
    hx = (hero->x_fp >> 16) + scene->hx;
    hy = 0xEF + scene->hy;
    hz = (hero->z_fp >> 16) + scene->hz;
    if (!spans(left, scene->zw, hx, scene->hw)) return;
    if (!spans(depth, scene->zzs, hz, scene->hzs)) return;
    if (!spans(top, scene->zh, hy, scene->hh)) return;
    foe->hit = 1;
    hurt_hero(scene, hero, px);
}

static int floor_blocked(const Scene *scene, int x, int z) {
    int tx = scene->floor_origin + (x >> 4);
    int ty = (z >> 4) + 1;
    int id, bit;
    if (tx < 0 || ty < 0 || tx >= scene->floor_w || ty >= scene->floor_h) return 1;
    id = scene->floor_id[ty * scene->floor_w + tx];
    if (id == 1) return 0;
    if (id == 2) {
        bit = scene->fmt5[((z & 15) << 4) + (x & 15)];
        return bit != 0;
    }
    return 1;
}

/* Walk one axis up to the solid cell. FloorWallCheck resolves X and Z apart. */
static int slide_axis(const Scene *scene, int fixed, int from, int to, int vertical) {
    int step, cur;
    if (to == from) return from;
    step = to > from ? 1 : -1;
    cur = from;
    while (cur != to) {
        int nxt = cur + step;
        int px = vertical ? fixed : nxt;
        int pz = vertical ? nxt : fixed;
        if (floor_blocked(scene, px, pz)) break;
        cur = nxt;
    }
    return cur;
}

static void push_mover(Scene *scene, Body *hero, int *x_fp, int *z_fp, int facing, int old_x, int old_z, int skip);

static void slide_body(const Scene *scene, int *x_fp, int *z_fp, int old_x, int old_z) {
    int raw_x = *x_fp >> 16;
    int raw_z = *z_fp >> 16;
    int nx = slide_axis(scene, old_z, old_x, raw_x, 0);
    int nz = slide_axis(scene, nx, old_z, raw_z, 1);
    if (nx < 0) nx = 0;
    if (nx >= scene->map_w) nx = scene->map_w - 1;
    /* A free step keeps the 16.16 fraction. Only a blocked step snaps to the edge. */
    if (nx != raw_x) *x_fp = nx << 16;
    if (nz != raw_z) *z_fp = nz << 16;
    if ((*x_fp >> 16) < 0) *x_fp = 0;
    if ((*x_fp >> 16) >= scene->map_w) *x_fp = (scene->map_w - 1) << 16;
}

/* csStage01ScrollLock, 16-byte records. A negative word is the map end. */
static const int LOCK_MIN[8] = {0, 0, 100, 100, 687, 687, 1055, 0};
static const int LOCK_MAX[8] = {0, 191, 191, 687, 687, 1055, 1055, 1072};

static void spawn_enemy(Scene *scene, int x, int z, int hp, int sheet, int flags, int y_word) {
    Foe *foe;
    if (flags & 1) x = scene->camera - 20;
    if (flags & 2) x = scene->camera + VIEW_W + 20;
    if (scene->n_foes >= 12) return;
    foe = &scene->foes[scene->n_foes++];
    memset(foe, 0, sizeof(*foe));
    foe->x = x;
    foe->z = z;
    foe->hp = hp;
    foe->sheet = sheet;
    foe->alive = 1;
    foe->counts = (flags & 0x20) ? 1 : 0;
    foe->x_fp = x << 16;
    foe->z_fp = z << 16;
    foe->y_fp = 0xEF << 16;
    /* GT_CheckFloor: FloorGroundCheck returned 0, so it stores this fall
       and ActionSets a drop script. Act 12 is the traced damage fall. */
    if (y_word != 0) {
        foe->y_fp = (0xEF - y_word) << 16;
        foe->yv = -262144;
        foe->ya = 212992;
        foe->mode = 9;
    }
}

static void note_ko(Scene *scene, Foe *foe) {
    if (!foe->counts) return;
    foe->counts = 0;
    if (scene->quota > 0) scene->quota--;
    if (scene->quota > 0 || scene->trigger < 0) return;
    /* EncData2 keys 0 and 1 are a kind-5 lock step. Key 1 also creates an
       item; that kind-4 row is deferred. Keys 2 and 3 only reset a quota
       whose extra enemies come from kind-9 objects, which are deferred. */
    if (scene->trigger == 0 || scene->trigger == 1) scene->lock_i++;
    else if (scene->trigger == 2) {
        scene->quota = 2;
        scene->trigger = 3;
        return;
    } else if (scene->trigger == 3) {
        scene->quota = 3;
        scene->trigger = 4;
        return;
    } else if (scene->trigger == 4) scene->lock_i++;
    scene->trigger = -1;
}

static void consume_rows(Scene *scene);

static void update_scroll(Scene *scene, const Body *hero) {
    int x = hero->x_fp >> 16;
    int cam = scene->camera;
    int delta = 0;
    int i, lo, hi;
    /* cStage01ScrollPush: left 80px, right 240px, catch-up 12px. */
    if (x > cam + 240) delta = x - (cam + 240);
    else if (x < cam + 80) delta = x - (cam + 80);
    if (delta > 12) delta = 12;
    if (delta < -12) delta = -12;
    cam += delta;
    i = scene->lock_i;
    if (i < 0) i = 0;
    if (i > 7) i = 7;
    lo = LOCK_MIN[i];
    hi = LOCK_MAX[i];
    if (hi > scene->map_w - VIEW_W) hi = scene->map_w - VIEW_W;
    if (cam < lo) cam = lo;
    if (cam > hi) cam = hi;
    if (cam < 0) cam = 0;
    scene->camera = cam;
    consume_rows(scene);
}

static void consume_rows(Scene *scene) {
    int cam = scene->camera;
    while (scene->row_i < scene->n_rows && scene->rows[scene->row_i][0] <= cam) {
        int *row = scene->rows[scene->row_i++];
        if (row[1] == 0) {
            scene->quota = row[2];
            scene->trigger = row[3];
        } else if (row[1] == 5) {
            if (scene->lock_i < 7) scene->lock_i++;
        } else if (row[1] == 3) {
            spawn_enemy(scene, row[2], row[3], row[4], row[5], row[6], row[7]);
        }
    }
}

static void step_foes(Scene *scene, Body *hero) {
    int i;
    if (mt_index > 624) mt_seed(1);
    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        int n, dx, dz;
        if (!foe->alive || foe->mode == 4) continue;
        foe->vx = 0;
        foe->vz = 0;
        if (foe->mode >= 2) {
            /* GT_FukiUpDamage while vy <= 0, then GT_FukiDownDamage.
               Street floor type is 0, so FloorAttrCause does not OR 0x400200.
               After the bound hop lands, HP left plays the get-up act that
               branch names (0x17). HP 0 stays down and does not resume AI. */
            {
                int ox = foe->x_fp >> 16;
                int oz = foe->z_fp >> 16;
                foe->x_fp += foe->vx;
                slide_body(scene, &foe->x_fp, &foe->z_fp, ox, oz);
            }
            if (foe->mode == 9 || foe->mode == 2 || foe->mode == 5 || foe->mode == 3) {
                foe->y_fp += foe->yv;
                foe->yv += foe->ya;
                if (foe->mode == 9 && foe->y_fp >= (0xEF << 16)) {
                    foe->y_fp = 0xEF << 16;
                    foe->yv = 0;
                    foe->ya = 0;
                    foe->mode = 0;
                    foe->frame = 0;
                    foe->shown = 0;
                    foe->timer = 0;
                } else if (foe->mode != 9 && foe->mode == 2 && foe->yv > 0) {
                    foe->mode = 5;
                    foe->frame = 0;
                    foe->shown = 0;
                    foe->timer = 0;
                } else if (foe->y_fp >= (0xEF << 16)) {
                    foe->y_fp = 0xEF << 16;
                    if (foe->mode == 5) {
                        foe->mode = 3;
                        foe->yv = -589824;
                        foe->ya = 229376;
                        foe->frame = 0;
                        foe->shown = 0;
                        foe->timer = 0;
                    } else if (foe->mode == 3 && foe->yv >= 0) {
                        /* BoundDamage, once the hop is back on the ground, installs
                           GT_Down and act 16. It does not play the 0x400000 get-up. */
                        foe->yv = 0;
                        foe->ya = 0;
                        foe->vx = 0;
                        foe->mode = 7;
                        foe->frame = 0;
                        foe->shown = 0;
                        foe->timer = 0;
                    }
                }
            }
            if (foe->mode == 4) {
                foe->x = foe->x_fp >> 16;
                continue;
            }
            n = foe_count(scene, foe);
            if (foe->mode == 7 && n > 0 && foe->shown && foe->timer <= 1 && foe->frame + 1 >= n) {
                /* GT_Down: STOP on act 16. HP left -> act 17. HP 0 -> act 23 then GT_KO. */
                foe->mode = foe->hp > 0 ? 6 : 8;
                foe->frame = 0;
                foe->shown = 0;
                foe->timer = 0;
                foe->x = foe->x_fp >> 16;
                continue;
            }
            if (foe->mode == 6 && n > 0 && foe->shown && foe->timer <= 1 && foe->frame + 1 >= n) {
                foe->mode = 0;
                foe->frame = 0;
                foe->shown = 0;
                foe->cmd = 0;
                foe->x = foe->x_fp >> 16;
                continue;
            }
            if (foe->mode == 8 && n > 0 && foe->shown && foe->timer <= 1 && foe->frame + 1 >= n) {
                /* GT_KO returns 2 for a negative +0x16d. The task walker unlinks it. */
                foe->mode = 4;
                foe->alive = 0;
                foe->x = foe->x_fp >> 16;
                note_ko(scene, foe);
                continue;
            }
            if (!foe->shown) {
                foe->timer = foe_pose(scene, foe)->duration;
                foe->shown = 1;
            } else {
                foe->timer -= 1;
                if (foe->timer <= 0 && n > 0) {
                    foe->frame = (foe->frame + 1) % n;
                    foe->timer = foe_pose(scene, foe)->duration;
                }
            }
            foe->x = foe->x_fp >> 16;
            continue;
        }
        if (foe->mode == 1) {
            n = foe_count(scene, foe);
            if (!foe->shown) {
                foe->timer = foe_pose(scene, foe)->duration;
                foe->shown = 1;
                foe_hits_hero(scene, foe, hero);
            } else {
                foe->timer -= 1;
                if (foe->timer <= 0) {
                    foe->frame += 1;
                    if (foe->frame >= n) {
                        foe->mode = 0;
                        foe->frame = 0;
                        foe->shown = 0;
                        foe->cmd = 0;
                    } else {
                        foe->timer = foe_pose(scene, foe)->duration;
                        foe_hits_hero(scene, foe, hero);
                    }
                }
            }
            continue;
        }
        dx = (foe->x_fp >> 16) - (hero->x_fp >> 16);
        dz = (foe->z_fp >> 16) - (hero->z_fp >> 16);
        if (foe->cmd == 3) {
            if (dx > 60) foe->vx = scene->zvx;
            else if (dx < -60) foe->vx = -scene->zvx;
            else foe->cmd = 0;
        } else if (foe->cmd == 4) {
            if (dz > 3) foe->vz = -scene->zvz;
            else if (dz < -3) foe->vz = scene->zvz;
            else foe->cmd = 0;
        } else if (hero->mode == 0) {
            int adx = dx < 0 ? -dx : dx;
            int adz = dz < 0 ? -dz : dz;
            uint32_t gate = mt_draw();
            mt_draw();
            if (adz <= 7 && adx < 100) {
                foe->mode = 1;
                foe->frame = 0;
                foe->timer = 0;
                foe->shown = 0;
                foe->hit = 0;
                foe->facing = dx > 0 ? 0 : 1;
            } else if (adx > 60 && (gate & 0xfu) == 0) {
                foe->cmd = 3;
            } else if (adz > 3 && (mt_draw() & 7u) == 0) {
                foe->cmd = 4;
            }
        }
        if (foe->vx < 0) foe->facing = 0;
        if (foe->vx > 0) foe->facing = 1;
        {
            int ox = foe->x_fp >> 16;
            int oz = foe->z_fp >> 16;
            foe->x_fp += foe->vx;
            foe->z_fp += foe->vz;
            slide_body(scene, &foe->x_fp, &foe->z_fp, ox, oz);
            push_mover(scene, hero, &foe->x_fp, &foe->z_fp, foe->facing, ox, oz, i);
        }
        foe->x = foe->x_fp >> 16;
        foe->z = foe->z_fp >> 16;
        n = foe_count(scene, foe);
        if (!foe->shown) {
            foe->timer = foe_pose(scene, foe)->duration;
            foe->shown = 1;
        } else {
            foe->timer -= 1;
            if (foe->timer <= 0) {
                foe->frame = (foe->frame + 1) % n;
                foe->timer = foe_pose(scene, foe)->duration;
            }
        }
    }
}

static void push_box(int x, int z, int facing, int *left, int *right, int *top, int *bot) {
    /* ciZakoC00PushData index 1, also D00 index 1: (-17, -65, 29, 64, -2, 5). */
    int xoff = -17, w = 29;
    *left = facing ? x - xoff - w : x + xoff;
    *right = *left + w;
    *top = z - 2;
    *bot = *top + 5;
}

static int push_overlap(int a0, int a1, int b0, int b1) {
    return a0 < b1 && b0 < a1;
}

static void push_mover(Scene *scene, Body *hero, int *x_fp, int *z_fp, int facing, int old_x, int old_z, int skip) {
    int x = *x_fp >> 16, z = *z_fp >> 16;
    int l, r, t, b, i;
    push_box(x, z, facing, &l, &r, &t, &b);
    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        int fl, fr, ft, fb;
        if (i == skip || !foe->alive || foe->mode >= 2) continue;
        push_box(foe->x, foe->z, foe->facing, &fl, &fr, &ft, &fb);
        if (!push_overlap(l, r, fl, fr) || !push_overlap(t, b, ft, fb)) continue;
        /* Keep the mover on the near side of the other push box. */
        if (x != old_x) {
            x = old_x;
            *x_fp = old_x << 16;
        }
        if (z != old_z) {
            z = old_z;
            *z_fp = old_z << 16;
        }
        break;
    }
    if (hero && skip >= 0 && hero->mode < 2) {
        int hl, hr, ht, hb;
        push_box(hero->x_fp >> 16, hero->z_fp >> 16, hero->facing, &hl, &hr, &ht, &hb);
        push_box(x, z, facing, &l, &r, &t, &b);
        if (push_overlap(l, r, hl, hr) && push_overlap(t, b, ht, hb)) {
            if (x != old_x) *x_fp = old_x << 16;
            if (z != old_z) *z_fp = old_z << 16;
        }
    }
}

static void step_body(Scene *scene, Body *body, int left, int right, int up, int down, int attack) {
    Frame *group;
    int count;
    int any = left || right || up || down;
    int i;
    step_foes(scene, body);
    if (body->mode == 4) return;
    if (body->mode >= 2) {
        int n = 1;
        {
            int ox = body->x_fp >> 16;
            int oz = body->z_fp >> 16;
            body->x_fp += body->vx;
            slide_body(scene, &body->x_fp, &body->z_fp, ox, oz);
        }
        if (body->mode == 2 || body->mode == 5 || body->mode == 3) {
            body->y_fp += body->yv;
            body->yv += body->ya;
            if (body->mode == 2 && body->yv > 0) {
                body->mode = 5;
                body->frame = 0;
                body->shown = 0;
                body->timer = 0;
            } else if (body->y_fp >= (0xEF << 16)) {
                body->y_fp = 0xEF << 16;
                if (body->mode == 5) {
                    body->mode = 3;
                    body->yv = -589824;
                    body->ya = 229376;
                    body->frame = 0;
                    body->shown = 0;
                    body->timer = 0;
                } else if (body->mode == 3 && body->yv >= 0) {
                    body->yv = 0;
                    body->ya = 0;
                    body->vx = 0;
                    body->mode = 7;
                    body->frame = 0;
                    body->shown = 0;
                    body->timer = 0;
                }
            }
        }
        if (body->mode == 2) n = scene->n_jd;
        else if (body->mode == 5) n = scene->n_jf;
        else if (body->mode == 3) n = scene->n_jl;
        else if (body->mode == 7) n = scene->n_jlie;
        else if (body->mode == 6) n = scene->n_jg;
        else if (body->mode == 8) n = scene->n_jdie;
        if (body->mode == 7 && n > 0 && body->shown && body->timer <= 1 && body->frame + 1 >= n) {
            body->mode = body->hp > 0 ? 6 : 8;
            body->frame = 0;
            body->shown = 0;
            body->timer = 0;
            return;
        }
        if (body->mode == 6 && n > 0 && body->shown && body->timer <= 1 && body->frame + 1 >= n) {
            body->mode = 0;
            restart(body, 0);
            return;
        }
        if (body->mode == 8 && n > 0 && body->shown && body->timer <= 1 && body->frame + 1 >= n) {
            body->mode = 4;
            body->frame = n - 1;
            return;
        }
        if (body->mode != 4 && n > 0) {
            if (!body->shown) {
                body->timer = 1;
                body->shown = 1;
            } else {
                body->timer -= 1;
                if (body->timer <= 0) {
                    body->frame = (body->frame + 1) % n;
                    body->timer = 1;
                }
            }
        }
        return;
    }
    if (attack && body->punch_act == 0) {
        int px = body->x_fp >> 16;
        int pz = body->z_fp >> 16;
        int close = 0;
        for (i = 0; i < scene->n_foes; i++) {
            Foe *foe = &scene->foes[i];
            int dx, dz;
            if (!foe->alive || foe->mode >= 2) continue;
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
    {
        int ox = body->x_fp >> 16;
        int oz = body->z_fp >> 16;
        body->x_fp += body->vx;
        body->z_fp += body->vz;
        slide_body(scene, &body->x_fp, &body->z_fp, ox, oz);
        push_mover(scene, NULL, &body->x_fp, &body->z_fp, body->facing, ox, oz, -1);
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

static void paint(Scene *scene, const Body *hero, uint8_t *fb) {
    int camera, ground, sx, sy, i;
    Frame *pose;
    memset(fb, 0, (size_t)VIEW_W * VIEW_H * 4);
    for (i = 0; i < VIEW_W * VIEW_H; i++) {
        fb[i * 4 + 0] = 24;
        fb[i * 4 + 1] = 28;
        fb[i * 4 + 2] = 36;
        fb[i * 4 + 3] = 255;
    }
    update_scroll(scene, hero);
    camera = scene->camera;
    blit_map(fb, scene->far_px, scene->map_w, scene->map_h, camera >> 2);
    blit_map(fb, scene->main_px, scene->map_w, scene->map_h, camera);

    /* DrawZSort compares +0x80 (Z). A larger Z is drawn first.
       Equal Z falls through to the task id, and the lower id is drawn later. */
    {
        int z[16], id[16], kind[16], n = 0, a, b;
        z[n] = hero->z_fp >> 16;
        id[n] = 0;
        kind[n] = -1;
        n++;
        for (i = 0; i < scene->n_foes; i++) {
            if (!scene->foes[i].alive) continue;
            z[n] = scene->foes[i].z;
            id[n] = i + 1;
            kind[n] = i;
            n++;
        }
        for (a = 0; a < n; a++) {
            for (b = a + 1; b < n; b++) {
                if (z[b] > z[a] || (z[b] == z[a] && id[b] > id[a])) {
                    int tz = z[a], ti = id[a], tk = kind[a];
                    z[a] = z[b];
                    id[a] = id[b];
                    kind[a] = kind[b];
                    z[b] = tz;
                    id[b] = ti;
                    kind[b] = tk;
                }
            }
        }
        for (a = 0; a < n; a++) {
            if (kind[a] < 0) {
                pose = hero_pose(scene, hero);
                ground = ground_at(scene, hero->x_fp >> 16, hero->z_fp >> 16);
                if (ground < -10000) ground = 0xEF;
                sx = (hero->x_fp >> 16) - camera;
                sy = ((hero->y_fp ? hero->y_fp : (0xEF << 16)) >> 16) - (hero->z_fp >> 16);
                blit(fb, VIEW_W, VIEW_H, pose->px, pose->w, pose->h, sx - pose->ox, sy - pose->oy,
                     hero->facing == 1);
            } else {
                const Foe *foe = &scene->foes[kind[a]];
                const Frame *fr = foe_pose(scene, foe);
                ground = ground_at(scene, foe->x, foe->z);
                if (ground < -10000) ground = 0xEF;
                sx = foe->x - camera;
                sy = (foe->y_fp >> 16) - foe->z;
                blit(fb, VIEW_W, VIEW_H, fr->px, fr->w, fr->h, sx - fr->ox, sy - fr->oy, foe->facing == 1);
            }
        }
    }
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
    hero.hp = scene->hero_hp;
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
    hero.hp = scene->hero_hp;
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
    hero.hp = scene->hero_hp;
    for (i = 0; i < frames; i++) {
        int left = hold == 1, right = hold == 2 || hold == 5 || hold == 7, up = hold == 3, down = hold == 4;
        int attack = 0;
        if (hold == 9 && hero.mode == 0) {
            int f, close = 0;
            for (f = 0; f < scene->n_foes; f++) {
                int d = scene->foes[f].x - (hero.x_fp >> 16);
                if (scene->foes[f].mode != 4 && d > -30 && d < 30) close = 1;
            }
            if (!close) right = 1;
        }
        if ((hold == 8 || hold == 12) && hero.mode == 0) {
            int f, best = 100000, gap = 0;
            for (f = 0; f < scene->n_foes; f++) {
                int d;
                if (!scene->foes[f].alive || scene->foes[f].mode == 4) continue;
                d = scene->foes[f].x - (hero.x_fp >> 16);
                if (d < 0) d = -d;
                if (d < best) {
                    best = d;
                    gap = scene->foes[f].x - (hero.x_fp >> 16);
                }
            }
            if (gap > 36) right = 1;
            if (gap < -36) left = 1;
            if (best < 70 && hero.punch_act == 0) attack = 1;
            if (hold == 12 && best == 100000) right = 1;
        }
        if (hold == 5 && !fired && (hero.x_fp >> 16) >= 430) {
            attack = 1;
            fired = 1;
        }
        if (hold == 7 && hero.mode == 0 && hero.punch_act == 0) {
            int f;
            for (f = 0; f < scene->n_foes; f++) {
                int gap = scene->foes[f].x - (hero.x_fp >> 16);
                if (scene->foes[f].alive && scene->foes[f].mode < 2 && gap > 30 && gap < 100) attack = 1;
            }
        }
        step_body(scene, &hero, left, right, up, down, attack);
        paint(scene, &hero, fb);
    }
    if (!write_ppm(path, fb)) return 1;
    printf("x %d z %d act %d frame %d facing %d\n", hero.x_fp >> 16, hero.z_fp >> 16,
           hero.act, hero.frame, hero.facing);
    printf("cam %d lock %d nf %d\n", scene->camera, scene->lock_i, scene->n_foes);
    if ((hold >= 5 && hold <= 9) || hold == 12) {
        printf("hp %d", hero.hp);
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].hp);
        printf(" x");
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].x);
        printf(" m");
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].mode);
        printf(" pm %d cam %d lock %d nf %d\n", hero.mode, scene->camera, scene->lock_i, scene->n_foes);
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
        if (argc > 5 && strcmp(argv[5], "watch") == 0) hold = 6;
        if (argc > 5 && strcmp(argv[5], "fight") == 0) hold = 7;
        if (argc > 5 && strcmp(argv[5], "stand") == 0) hold = 8;
        if (argc > 5 && strcmp(argv[5], "crowd") == 0) hold = 9;
        if (argc > 5 && strcmp(argv[5], "onward") == 0) hold = 12;
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
