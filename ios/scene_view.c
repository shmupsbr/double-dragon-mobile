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
    int no_push, wall_cc, wall_c8, z_vel;
} Foe;

typedef struct {
    int x, z, place_y, alive, phase, extra, fire_mode;
    int frame, timer, playing, shown, fired;
} Prop;

typedef struct {
    int left, key, alive;
} Timer;

typedef struct {
    int map_w, map_h, spawn_x, spawn_z, vel_x, vel_z, z_max;
    int floor_w, floor_h, floor_origin;
    int n_idle, n_walk, n_bash, n_will, n_p28, n_p29, n_p45, n_foes;
    int ax, ay, aw, ah, az, azs, dmg28;
    int cx, cy, cw, ch, cz, czs, dmg45;
    int hx, hy, hw, hh, hz, hzs, grab_x, grab_z;
    int n_zwb, n_zww, n_zpb, n_zpw, n_zdb, n_jd, n_zdw, n_zlb, n_zlw, n_jl;
    int n_zfb, n_zfw, n_zgb, n_zgw, n_jf, n_jg;
    int n_zlib, n_zliw, n_jlie, n_zdib, n_zdiw, n_jdie, n_jstun;
    int n_jump, n_land, n_kick, n_ckick, n_akick;
    int n_s8b, n_s8w, n_s8o, n_s8l, n_s8m, n_dash, n_b48, n_b50, n_crouch;
    int nr8[5][4];
    int k3x, k3y, k3w, k3h, k3z, k3zs, k3dmg, k3react;
    int k4x, k4y, k4w, k4h, k4z, k4zs, k4dmg, k4react;
    int k18x, k18y, k18w, k18h, k18z, k18zs, k18dmg, k18react;
    int k13x, k13y, k13w, k13h, k13z, k13zs, k13dmg, k13react;
    int k14x, k14y, k14w, k14h, k14z, k14zs, k14dmg, k14react;
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
    Frame *jstun;
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
    int mn[9];
    Frame *macho[9];
    Frame *objf[4];
    int objn[4];
    Frame *jump, *land, *kick, *ckick, *akick;
    Frame *s8b, *s8w, *s8o, *s8l, *s8m;
    Frame *dash, *b48, *b50, *crouch;
    Frame *r8[5][4];
    int n_rows, row_i, trigger;
    int rows[28][8];
    int camera, lock_i, quota, phase;
    int n_keys;
    int keys[48][4];
    int edict[20][8];
    int n_props;
    Prop props[6];
    Timer timers[8];
    Foe foes[16];
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
    Frame *frames;
    int i;
    if (count <= 0) return calloc(1, sizeof(Frame));
    frames = calloc(count, sizeof(Frame));
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
    if (!ru16(fp, &version) || version != 14) goto fail;
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
    if (!ri32(fp, &scene->n_foes) || scene->n_foes < 0 || scene->n_foes > 16) goto fail;
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
    if (!ru16(fp, &scene->n_jstun)) goto fail;
    scene->jstun = load_frames(fp, scene->n_jstun);
    ground_n = (size_t)scene->floor_w * scene->floor_h;
    scene->floor_id = malloc(ground_n);
    if (!scene->floor_id || !rd(fp, scene->floor_id, ground_n) || !rd(fp, scene->fmt5, 256)) goto fail;
    if (!ri32(fp, &scene->n_rows) || scene->n_rows < 0 || scene->n_rows > 28) goto fail;
    for (vw = 0; vw < scene->n_rows; vw++) {
        int k;
        for (k = 0; k < 8; k++) {
            if (!ri32(fp, &scene->rows[vw][k])) goto fail;
        }
    }
    for (vw = 0; vw < 9; vw++) {
        if (!ru16(fp, &scene->mn[vw])) goto fail;
    }
    for (vw = 0; vw < 9; vw++) {
        scene->macho[vw] = load_frames(fp, scene->mn[vw]);
        if (!scene->macho[vw]) goto fail;
    }
    if (!ri32(fp, &scene->n_keys) || scene->n_keys < 0 || scene->n_keys > 48) goto fail;
    for (vw = 0; vw < scene->n_keys; vw++) {
        int k;
        for (k = 0; k < 4; k++) {
            if (!ri32(fp, &scene->keys[vw][k])) goto fail;
        }
    }
    if (!ri32(fp, &vw) || vw != 20) goto fail;
    for (vw = 0; vw < 20; vw++) {
        int k;
        for (k = 0; k < 8; k++) {
            if (!ri32(fp, &scene->edict[vw][k])) goto fail;
        }
    }
    for (vw = 0; vw < 4; vw++) {
        if (!ru16(fp, &scene->objn[vw])) goto fail;
    }
    for (vw = 0; vw < 4; vw++) {
        scene->objf[vw] = load_frames(fp, scene->objn[vw]);
        if (!scene->objf[vw] || scene->objn[vw] <= 0) goto fail;
    }
    if (!ru16(fp, &scene->n_jump) || !ru16(fp, &scene->n_land) || !ru16(fp, &scene->n_kick) ||
        !ru16(fp, &scene->n_ckick) || !ru16(fp, &scene->n_akick) || !ru16(fp, &scene->n_s8b) ||
        !ru16(fp, &scene->n_s8w) || !ru16(fp, &scene->n_s8o) || !ru16(fp, &scene->n_s8l) ||
        !ru16(fp, &scene->n_s8m) ||         !ru16(fp, &scene->n_dash) || !ru16(fp, &scene->n_b48) ||
        !ru16(fp, &scene->n_b50) || !ru16(fp, &scene->n_crouch))
        goto fail;
    scene->jump = load_frames(fp, scene->n_jump);
    scene->land = load_frames(fp, scene->n_land);
    scene->kick = load_frames(fp, scene->n_kick);
    scene->ckick = load_frames(fp, scene->n_ckick);
    scene->akick = load_frames(fp, scene->n_akick);
    scene->s8b = load_frames(fp, scene->n_s8b);
    scene->s8w = load_frames(fp, scene->n_s8w);
    scene->s8o = load_frames(fp, scene->n_s8o);
    scene->s8l = load_frames(fp, scene->n_s8l);
    scene->s8m = load_frames(fp, scene->n_s8m);
    scene->dash = load_frames(fp, scene->n_dash);
    scene->b48 = load_frames(fp, scene->n_b48);
    scene->b50 = load_frames(fp, scene->n_b50);
    scene->crouch = load_frames(fp, scene->n_crouch);
    for (vw = 0; vw < 5; vw++) {
        int phase;
        for (phase = 0; phase < 4; phase++) {
            if (!ru16(fp, &scene->nr8[vw][phase])) goto fail;
        }
    }
    for (vw = 0; vw < 5; vw++) {
        int phase;
        for (phase = 0; phase < 4; phase++) {
            scene->r8[vw][phase] = load_frames(fp, scene->nr8[vw][phase]);
            if (!scene->r8[vw][phase] || scene->nr8[vw][phase] <= 0) goto fail;
        }
    }
    if (!rs16(fp, &scene->k3x) || !rs16(fp, &scene->k3y) || !rs16(fp, &scene->k3w) ||
        !rs16(fp, &scene->k3h) || !rs16(fp, &scene->k3z) || !rs16(fp, &scene->k3zs) ||
        !rs16(fp, &scene->k3dmg) || !rs16(fp, &scene->k3react))
        goto fail;
    if (!rs16(fp, &scene->k4x) || !rs16(fp, &scene->k4y) || !rs16(fp, &scene->k4w) ||
        !rs16(fp, &scene->k4h) || !rs16(fp, &scene->k4z) || !rs16(fp, &scene->k4zs) ||
        !rs16(fp, &scene->k4dmg) || !rs16(fp, &scene->k4react))
        goto fail;
    if (!rs16(fp, &scene->k18x) || !rs16(fp, &scene->k18y) || !rs16(fp, &scene->k18w) ||
        !rs16(fp, &scene->k18h) || !rs16(fp, &scene->k18z) || !rs16(fp, &scene->k18zs) ||
        !rs16(fp, &scene->k18dmg) || !rs16(fp, &scene->k18react))
        goto fail;
    if (!rs16(fp, &scene->k13x) || !rs16(fp, &scene->k13y) || !rs16(fp, &scene->k13w) ||
        !rs16(fp, &scene->k13h) || !rs16(fp, &scene->k13z) || !rs16(fp, &scene->k13zs) ||
        !rs16(fp, &scene->k13dmg) || !rs16(fp, &scene->k13react))
        goto fail;
    if (!rs16(fp, &scene->k14x) || !rs16(fp, &scene->k14y) || !rs16(fp, &scene->k14w) ||
        !rs16(fp, &scene->k14h) || !rs16(fp, &scene->k14z) || !rs16(fp, &scene->k14zs) ||
        !rs16(fp, &scene->k14dmg) || !rs16(fp, &scene->k14react))
        goto fail;
    if (!scene->jump || !scene->land || !scene->kick || !scene->ckick || !scene->akick ||
        !scene->s8b || !scene->s8w || !scene->s8o || !scene->s8l || !scene->s8m ||
        !scene->dash || !scene->b48 || !scene->b50 || !scene->crouch)
        goto fail;
    scene->trigger = -1;
    if (!scene->idle || !scene->walk || !scene->bash || !scene->will || !scene->p28 || !scene->p29 ||
        !scene->p45 || !scene->zwb || !scene->zww || !scene->zpb || !scene->zpw || !scene->zdb ||
        !scene->zdw || !scene->jd || !scene->zlb || !scene->zlw || !scene->jl || !scene->zfb ||
        !scene->zfw || !scene->zgb || !scene->zgw || !scene->jf || !scene->jg || !scene->zlib ||
        !scene->zliw || !scene->jlie || !scene->zdib || !scene->zdiw || !scene->jdie || !scene->bolo ||
        !scene->zwo || !scene->zpo || !scene->zdo || !scene->zlo || !scene->zfo || !scene->zgo ||
        !scene->zlio || !scene->zdio || !scene->lidle || !scene->lwalk || !scene->lpunch ||
        !scene->ldown || !scene->lland || !scene->lfall || !scene->lget || !scene->llie || !scene->ldie ||
        !scene->jstun)
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
    int y_fp, yv, ya, xa;
    int was_left, was_right, tap_l, tap_r;
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
    } else if (body->punch_act == 30) {
        ax = scene->k3x; ay = scene->k3y; aw = scene->k3w; ah = scene->k3h;
        az = scene->k3z; azs = scene->k3zs; dmg = scene->k3dmg;
    } else if (body->punch_act == 31) {
        ax = scene->k4x; ay = scene->k4y; aw = scene->k4w; ah = scene->k4h;
        az = scene->k4z; azs = scene->k4zs; dmg = scene->k4dmg;
    } else if (body->punch_act == 46) {
        ax = scene->k18x; ay = scene->k18y; aw = scene->k18w; ah = scene->k18h;
        az = scene->k18z; azs = scene->k18zs; dmg = scene->k18dmg;
    } else if (body->punch_act == 48) {
        ax = scene->k13x; ay = scene->k13y; aw = scene->k13w; ah = scene->k13h;
        az = scene->k13z; azs = scene->k13zs; dmg = scene->k13dmg;
    } else if (body->punch_act == 50) {
        ax = scene->k14x; ay = scene->k14y; aw = scene->k14w; ah = scene->k14h;
        az = scene->k14z; azs = scene->k14zs; dmg = scene->k14dmg;
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
        if (foe->hp < 0) foe->hp = 0;
        if (body->punch_act == 48) {
            /* ciJimmyAtkData record 13: byte +0x0f is 3, byte +0x10 is 1.
               DamagedCheck stores +0x174 from +0x0f and, because +0x10 is
               non-negative, +0x175 = attacker facing XOR that byte.
               GT_SetFukiMove record 3 is vx +131072, xa 0, yv -458752,
               ya +212992. A nonzero +0x175 negates X only. Reaction 8
               then runs GT_BackFukiDamage on defender act 18. */
            int mirror = body->facing ^ 1;
            foe->mode = 17;
            foe->vx = mirror ? -131072 : 131072;
            foe->yv = -458752;
            foe->ya = 212992;
            foe->vz = 0;
            foe->y_fp = 0xEF << 16;
            foe->frame = 0;
            foe->timer = 0;
            foe->shown = 0;
            foe->cmd = 0;
            continue;
        }
        if (body->punch_act == 30) {
            /* Reaction 1: same Noke proc as reaction 2, defender act 8, full enemy timer. */
            foe->mode = 15;
            foe->vx = body->facing ? 262144 : -262144;
            foe->yv = 0;
            foe->ya = 0;
            foe->frame = 0;
            foe->timer = 0;
            foe->shown = 0;
            foe->cmd = 0;
            continue;
        }
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
    if (body->mode == 10 && scene->n_jstun) return &scene->jstun[body->frame % scene->n_jstun];
    if (body->mode == 8 && scene->n_jdie) return &scene->jdie[body->frame % scene->n_jdie];
    if (body->mode == 4 && scene->n_jdie) return &scene->jdie[scene->n_jdie - 1];
    if (body->mode == 11 && scene->n_jump) return &scene->jump[body->frame % scene->n_jump];
    if (body->mode == 13 && scene->n_land) return &scene->land[body->frame % scene->n_land];
    if (body->mode == 16 && scene->n_dash) return &scene->dash[body->frame % scene->n_dash];
    if (body->mode == 21 && scene->n_crouch) return &scene->crouch[0];
    if (body->punch_act == 48 && scene->n_b48) return &scene->b48[body->frame % scene->n_b48];
    if (body->punch_act == 50 && scene->n_b50) return &scene->b50[body->frame % scene->n_b50];
    if (body->punch_act == 30 && scene->n_kick) return &scene->kick[body->frame % scene->n_kick];
    if (body->punch_act == 46 && scene->n_ckick) return &scene->ckick[body->frame % scene->n_ckick];
    if (body->punch_act == 31 && scene->n_akick) return &scene->akick[body->frame % scene->n_akick];
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
    const Frame *table[5][9] = {
        {scene->bash, scene->zwb, scene->zpb, scene->zdb, scene->zlb, scene->zfb, scene->zlib, scene->zgb, scene->zdib},
        {scene->will, scene->zww, scene->zpw, scene->zdw, scene->zlw, scene->zfw, scene->zliw, scene->zgw, scene->zdiw},
        {scene->bolo, scene->zwo, scene->zpo, scene->zdo, scene->zlo, scene->zfo, scene->zlio, scene->zgo, scene->zdio},
        {scene->lidle, scene->lwalk, scene->lpunch, scene->ldown, scene->lland, scene->lfall, scene->llie, scene->lget, scene->ldie},
        {scene->macho[0], scene->macho[1], scene->macho[2], scene->macho[3], scene->macho[4], scene->macho[5], scene->macho[6], scene->macho[7], scene->macho[8]},
    };
    const int counts[5][9] = {
        {scene->n_bash, scene->n_zwb, scene->n_zpb, scene->n_zdb, scene->n_zlb, scene->n_zfb, scene->n_zlib, scene->n_zgb, scene->n_zdib},
        {scene->n_will, scene->n_zww, scene->n_zpw, scene->n_zdw, scene->n_zlw, scene->n_zfw, scene->n_zliw, scene->n_zgw, scene->n_zdiw},
        {scene->n_will, scene->n_zww, scene->n_zpw, scene->n_zdw, scene->n_zlw, scene->n_zfw, scene->n_zliw, scene->n_zgw, scene->n_zdiw},
        {scene->ln[0], scene->ln[1], scene->ln[2], scene->ln[3], scene->ln[4], scene->ln[5], scene->ln[7], scene->ln[6], scene->ln[8]},
        {scene->mn[0], scene->mn[1], scene->mn[2], scene->mn[3], scene->mn[4], scene->mn[5], scene->mn[6], scene->mn[7], scene->mn[8]},
    };
    int bank = sheet >= 0 && sheet <= 4 ? sheet : 0;
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
    int n, kind;
    if (foe->mode >= 17 && foe->mode <= 20) {
        int phase = foe->mode - 17;
        int sheet = foe->sheet;
        int count;
        if (sheet < 0 || sheet > 4 || scene->nr8[sheet][phase] <= 0) sheet = 0;
        count = scene->nr8[sheet][phase];
        if (count <= 0) return &scene->bash[0];
        return &scene->r8[sheet][phase][foe->frame % count];
    }
    if (foe->mode == 15) {
        const Frame *fr = scene->s8b;
        int count = scene->n_s8b;
        if (foe->sheet == 1) { fr = scene->s8w; count = scene->n_s8w; }
        else if (foe->sheet == 2) { fr = scene->s8o; count = scene->n_s8o; }
        else if (foe->sheet == 3) { fr = scene->s8l; count = scene->n_s8l; }
        else if (foe->sheet == 4) { fr = scene->s8m; count = scene->n_s8m; }
        if (!fr || count <= 0) return &scene->bash[0];
        return &fr[foe->frame % count];
    }
    kind = foe_kind(foe);
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
    if (hero->mode != 0 || foe->hit) return;
    pose = foe_pose(scene, foe);
    if ((pose->flags & 0x28) != 8) return;
    px = foe->x_fp >> 16;
    ax = foe->sheet == 3 ? -51 : scene->zx;
    {
        int aw = foe->sheet == 3 ? 50 : scene->zw;
        int az = foe->sheet == 3 ? -3 : scene->zz;
        int azs = foe->sheet == 3 ? 7 : scene->zzs;
        int ay = foe->sheet == 3 ? -58 : scene->zy;
        int ah = foe->sheet == 3 ? 10 : scene->zh;
        if (foe->facing) left = px - ax - aw;
        else left = px + ax;
        depth = (foe->z_fp >> 16) + az;
        top = 0xEF + ay;
        hx = (hero->x_fp >> 16) + scene->hx;
        hy = 0xEF + scene->hy;
        hz = (hero->z_fp >> 16) + scene->hz;
        if (!spans(left, aw, hx, scene->hw)) return;
        if (!spans(depth, azs, hz, scene->hzs)) return;
        if (!spans(top, ah, hy, scene->hh)) return;
    }
    foe->hit = 1;
    if (foe->sheet == 3) {
        /* Reaction 2: act 7, hitstun record 0, player timer halved to 5. */
        hero->hp -= scaled_damage(3, 0);
        if (hero->hp < 0) hero->hp = 0;
        hero->mode = 10;
        hero->frame = 0;
        hero->timer = 0;
        hero->shown = 0;
        hero->punch_act = 0;
        hero->vx = foe->facing ? 262144 : -262144;
        hero->yv = 0;
        hero->ya = 0;
        hero->vz = 0;
        return;
    }
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
        int next_blocked = floor_blocked(scene, px, pz);
        int here_blocked = floor_blocked(scene, vertical ? fixed : cur, vertical ? cur : fixed);
        /* A free cell may be entered. A solid cell stops a step that
           starts outside it. Format 0 has no free subtile, so a body that
           is already inside and moving toward lower Z takes the failed-scan
           store at 0x12ed8: pixel Z = Z - (Z & 15) - 1. */
        if (next_blocked && !here_blocked) break;
        if (next_blocked && here_blocked && vertical && step < 0) {
            /* Downward failed-scan. Format 5 can still have a free
               subtile below this one; format 0 never does, so the
               store is (Z & ~15) - 1. */
            int cell = cur & ~15;
            int s;
            for (s = cur - 1; s >= cell; s--) {
                if (!floor_blocked(scene, fixed, s)) return s;
            }
            if (cell - 1 < 0) return 0;
            return cell - 1;
        }
        if (next_blocked) break;
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
    if (scene->n_foes >= 16) return;
    foe = &scene->foes[scene->n_foes++];
    memset(foe, 0, sizeof(*foe));
    foe->x = x;
    foe->z = z;
    foe->hp = hp;
    foe->sheet = sheet;
    foe->alive = 1;
    foe->counts = (flags & 0x20) ? 1 : 0;
    /* Flag 0x10 sets +0x44 bit 0x400, and PushHitCheckX skips that task.
       Flag 0x100 selects GT_EnemyWallEntry: state 0 stores Z vel -131072,
       state 1 counts d0 (8) down, state 2 counts 3, then ReadySet. */
    foe->no_push = (flags & 0x10) ? 1 : 0;
    foe->wall_cc = (flags & 0x100) ? 0 : -1;
    foe->z_vel = 0;
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

static void apply_key(Scene *scene, int key) {
    int i;
    if (key < 0) return;
    for (i = 0; i < scene->n_keys; i++) {
        int kind, param, extra;
        if (scene->keys[i][0] != key) continue;
        kind = scene->keys[i][1];
        param = scene->keys[i][2];
        extra = scene->keys[i][3];
        if (kind == 0) {
            scene->quota = param;
            scene->trigger = extra >= 65535 ? -1 : extra;
        } else if (kind == 5) {
            if (scene->lock_i < 7) scene->lock_i++;
        } else if (kind == 8) {
            /* SetEncount kind 8 stores the param at applet+0x47f4. */
            scene->phase = param;
        } else if (kind == 3 && param >= 0 && param < 20 && scene->edict[param][3] >= 0) {
            spawn_enemy(scene, scene->edict[param][0], scene->edict[param][1],
                        scene->edict[param][2], scene->edict[param][3],
                        scene->edict[param][4], scene->edict[param][5]);
        } else if (kind == 10 && extra >= 0 && extra < 65535) {
            int t;
            for (t = 0; t < 8; t++) {
                if (scene->timers[t].alive) continue;
                scene->timers[t].alive = 1;
                scene->timers[t].left = param;
                scene->timers[t].key = extra;
                break;
            }
        }
        /* Kind 4 is EncountItemCreate. It is not a quota enemy.
           A key with no kind 0 leaves the live quota trigger alone.
           TriggerConditionCheck is what consumes it, before this call. */
    }
}

static void note_ko(Scene *scene, Foe *foe) {
    int key;
    if (!foe->counts) return;
    foe->counts = 0;
    /* EnemyKOTriggerCheck decrements +0x47f2 while +0x47f0 is 0.
       TriggerConditionCheck then fires +0x47ee once the remaining count
       drops below the threshold kind 0 stored (1). */
    if (scene->quota <= 0 || scene->trigger < 0) return;
    scene->quota--;
    if (scene->quota > 0) return;
    key = scene->trigger;
    scene->trigger = -1;
    apply_key(scene, key);
}

static void step_world(Scene *scene) {
    int i;
    for (i = 0; i < 8; i++) {
        if (!scene->timers[i].alive) continue;
        scene->timers[i].left--;
        if (scene->timers[i].left <= 0) {
            int key = scene->timers[i].key;
            scene->timers[i].alive = 0;
            apply_key(scene, key);
        }
    }
    for (i = 0; i < scene->n_props; i++) {
        Prop *prop = &scene->props[i];
        int group, n;
        Frame *frames;
        if (!prop->alive || prop->phase <= 0 || prop->fire_mode == 0) continue;
        group = prop->fire_mode == 1 ? 1 : 3;
        frames = scene->objf[group];
        n = scene->objn[group];
        if (!prop->playing) {
            if (scene->phase != prop->phase) continue;
            /* ActionSet inside the ready proc shows the first frame this tick. */
            prop->playing = 1;
            prop->frame = 0;
            prop->timer = n > 0 ? frames[0].duration : 1;
            prop->shown = 1;
            continue;
        }
        if (n <= 0) continue;
        prop->timer--;
        if (prop->timer > 0) continue;
        prop->frame++;
        if (prop->frame >= n) {
            /* STOP. WallBreak fires here. DoorOpen already fired on its flag. */
            if (prop->fire_mode == 2 && !prop->fired) {
                prop->fired = 1;
                apply_key(scene, prop->extra);
            }
            prop->alive = 0;
            continue;
        }
        prop->timer = frames[prop->frame].duration;
        if (prop->fire_mode == 1 && !prop->fired && (frames[prop->frame].flags & 0x10000000)) {
            prop->fired = 1;
            apply_key(scene, prop->extra);
        }
    }
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
        } else if (row[1] == 9 && scene->n_props < 6) {
            Prop *prop = &scene->props[scene->n_props++];
            memset(prop, 0, sizeof(*prop));
            prop->x = row[2];
            prop->z = row[3];
            prop->phase = row[4];
            prop->extra = row[5];
            prop->place_y = row[6];
            prop->fire_mode = row[7];
            prop->alive = 1;
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
        if (foe->wall_cc >= 0 && foe->mode < 2) {
            const Frame *walk;
            int wn;
            /* 0xe9c0 integrates +0x84 into Z before the state counter runs.
               State 0 stores that velocity afterwards, so the first tick
               does not move. MoveZ then calls FloorWallCheck. Inside the
               format-0 building row the scan fails and Z becomes
               (Z & ~15) - 1. The later ticks are on the sidewalk. */
            if (foe->wall_cc > 0) {
                int ox = foe->x_fp >> 16;
                int oz = foe->z_fp >> 16;
                foe->z_fp += foe->z_vel;
                slide_body(scene, &foe->x_fp, &foe->z_fp, ox, oz);
                foe->x = foe->x_fp >> 16;
                foe->z = foe->z_fp >> 16;
            }
            if (foe->wall_cc == 0) {
                foe->z_vel = -131072;
                foe->wall_c8 = 8;
                foe->wall_cc = 1;
            } else {
                foe->wall_c8--;
                if (foe->wall_c8 <= 0) {
                    if (foe->wall_cc == 1) {
                        foe->wall_c8 = 3;
                        foe->wall_cc = 2;
                    } else {
                        foe->z_vel = 0;
                        foe->wall_cc = -1;
                        foe->vz = 0;
                        foe->mode = 0;
                        foe->frame = 0;
                        foe->shown = 0;
                        foe->x = foe->x_fp >> 16;
                        continue;
                    }
                }
            }
            foe->vx = 0;
            foe->vz = 1;
            sheet_anim(scene, foe->sheet, 1, &walk, &wn);
            if (walk && wn > 0) {
                if (!foe->shown) {
                    foe->timer = walk->duration;
                    foe->shown = 1;
                } else {
                    foe->timer--;
                    if (foe->timer <= 0) {
                        foe->frame = (foe->frame + 1) % wn;
                        foe->timer = walk[foe->frame].duration;
                    }
                }
            }
            foe->x = foe->x_fp >> 16;
            foe->vz = 1;
            continue;
        }
        if (foe->mode >= 17 && foe->mode <= 20) {
            int phase = foe->mode - 17;
            int sheet = foe->sheet;
            int count;
            const Frame *fr;
            if (foe->mode == 17) {
                int ox = foe->x_fp >> 16;
                int oz = foe->z_fp >> 16;
                foe->x_fp += foe->vx;
                slide_body(scene, &foe->x_fp, &foe->z_fp, ox, oz);
                foe->y_fp += foe->yv;
                foe->yv += foe->ya;
                if (foe->y_fp >= (0xEF << 16)) {
                    /* GT_BackFukiDamage lands into GT_Down, act 0x29.
                       The down timer starts on the next tick. */
                    foe->y_fp = 0xEF << 16;
                    foe->yv = 0;
                    foe->ya = 0;
                    foe->vx = 0;
                    foe->mode = 18;
                    foe->frame = 0;
                    foe->shown = 0;
                    foe->timer = 0;
                    foe->x = foe->x_fp >> 16;
                    foe->z = foe->z_fp >> 16;
                    continue;
                }
            }
            if (sheet < 0 || sheet > 4 || scene->nr8[sheet][phase] <= 0) sheet = 0;
            count = scene->nr8[sheet][phase];
            fr = &scene->r8[sheet][phase][count > 0 ? foe->frame % count : 0];
            if (!foe->shown) {
                foe->timer = fr->duration > 0 ? fr->duration : 1;
                foe->shown = 1;
            } else {
                foe->timer--;
                if (foe->timer <= 0) {
                    if (count <= 0 || foe->frame + 1 >= count) {
                        if (foe->mode == 18) {
                            /* GT_Down on STOP. HP left is GT_Okiagari act 0x14.
                               HP 0 is GT_KO act 0x18. Type 0x10 is not these grunts. */
                            foe->mode = foe->hp > 0 ? 19 : 20;
                            foe->frame = 0;
                            foe->shown = 0;
                            foe->timer = 0;
                        } else if (foe->mode == 19) {
                            foe->mode = 0;
                            foe->frame = 0;
                            foe->shown = 0;
                            foe->cmd = 0;
                            foe->vx = 0;
                        } else if (foe->mode == 20) {
                            foe->mode = 4;
                            foe->alive = 0;
                            note_ko(scene, foe);
                        }
                        foe->x = foe->x_fp >> 16;
                        continue;
                    }
                    foe->frame++;
                    foe->timer = scene->r8[sheet][phase][foe->frame].duration;
                    if (foe->timer <= 0) foe->timer = 1;
                }
            }
            foe->x = foe->x_fp >> 16;
            foe->z = foe->z_fp >> 16;
            continue;
        }
        foe->vx = 0;
        foe->vz = 0;
        if (foe->mode == 15) {
            const Frame *fr = foe_pose(scene, foe);
            int count = 1;
            foe->x_fp += foe->vx;
            if (foe->vx > 0) { foe->vx -= 81920; if (foe->vx < 0) foe->vx = 0; }
            else if (foe->vx < 0) { foe->vx += 81920; if (foe->vx > 0) foe->vx = 0; }
            if (foe->sheet == 1) count = scene->n_s8w;
            else if (foe->sheet == 2) count = scene->n_s8o;
            else if (foe->sheet == 3) count = scene->n_s8l;
            else if (foe->sheet == 4) count = scene->n_s8m;
            else count = scene->n_s8b;
            if (!foe->shown) {
                foe->timer = fr->duration;
                foe->shown = 1;
            } else {
                foe->timer--;
                if (foe->timer <= 0) {
                    if (foe->frame + 1 >= count) {
                        foe->mode = 0;
                        foe->frame = 0;
                        foe->shown = 0;
                        foe->vx = 0;
                    } else {
                        foe->frame++;
                        foe->timer = foe_pose(scene, foe)->duration;
                    }
                }
            }
            foe->x = foe->x_fp >> 16;
            continue;
        }
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
            if (!foe->no_push)
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

/* PushHitCheckX 0x11a8a. Index-1 box xoff=-17, width=29.
   Moved right, both facing 0: new x = other.x - 30. */
static int push_x(int self_x, int self_face, int other_x, int other_face, int delta) {
    const int xoff = -17, width = 29;
    int edge, neu;
    if (delta > 0) {
        edge = other_face ? other_x - xoff - width : other_x + xoff;
        if (self_face) neu = xoff + edge;
        else neu = (edge - width) - xoff;
        return neu - 1;
    }
    if (delta < 0) {
        edge = other_face ? other_x - xoff : other_x + width + xoff;
        if (self_face) neu = xoff + width + edge;
        else neu = edge - xoff;
        return neu + 1;
    }
    return self_x;
}

static void push_mover(Scene *scene, Body *hero, int *x_fp, int *z_fp, int facing, int old_x, int old_z, int skip) {
    int x = *x_fp >> 16, z = *z_fp >> 16;
    int l, r, t, b, i, delta;
    (void)old_z;
    push_box(x, z, facing, &l, &r, &t, &b);
    delta = x - old_x;
    for (i = 0; i < scene->n_foes; i++) {
        Foe *foe = &scene->foes[i];
        int fl, fr, ft, fb;
        if (i == skip || !foe->alive || foe->mode >= 2) continue;
        push_box(foe->x, foe->z, foe->facing, &fl, &fr, &ft, &fb);
        if (!push_overlap(l, r, fl, fr) || !push_overlap(t, b, ft, fb) || delta == 0) continue;
        x = push_x(x, facing, foe->x, foe->facing, delta);
        *x_fp = x << 16;
        push_box(x, z, facing, &l, &r, &t, &b);
        break;
    }
    if (hero && skip >= 0 && hero->mode < 2 && delta != 0) {
        int hl, hr, ht, hb, hx = hero->x_fp >> 16;
        push_box(hx, hero->z_fp >> 16, hero->facing, &hl, &hr, &ht, &hb);
        push_box(x, z, facing, &l, &r, &t, &b);
        if (push_overlap(l, r, hl, hr) && push_overlap(t, b, ht, hb)) {
            x = push_x(x, facing, hx, hero->facing, delta);
            *x_fp = x << 16;
        }
    }
}

/* CTask::Caller walks priority 0 upward, and within a priority the
   oldest task is first (Entry appends). The player is priority 2 and
   enemies are priority 3, so Jimmy's proc runs before theirs. */
static void step_hero(Scene *scene, Body *body, int left, int right, int up, int down, int attack) {
    Frame *group;
    int count;
    int any = left || right || up || down;
    int i;
    if (body->mode == 4) return;
    if (body->mode == 21) {
        /* Act 25 NEXT's into act 26 on the same tick. Act 26 is pattern
           11 for 45 frames, then STOP. GT_Crouch then sets act 27, whose
           NEXT 0 returns to idle. A direction or a jump edge is MoveCheck
           leaving the crouch. Crouch attacks (acts 0x33/0x34) are not wired. */
        if (attack == 3 || left || right || up || down) {
            body->mode = 0;
            body->vx = 0;
            body->timer = 0;
        } else {
            if (body->timer > 0) body->timer--;
            if (body->timer <= 0) {
                body->mode = 0;
                body->vx = 0;
                restart(body, 0);
            }
            return;
        }
    }
    if (body->mode == 11 || body->mode == 14) {
        int n = body->mode == 14 ? scene->n_akick : scene->n_jump;
        body->y_fp += body->yv;
        body->yv += body->ya;
        if (body->vx || body->xa) {
            /* MoveX 0x11b20: x += vx, then vx += accel. It does not zero
               vx when the accel carries it through zero. r2 = 1, so
               FloorWallCheck runs on the new X. */
            int ox = body->x_fp >> 16;
            int oz = body->z_fp >> 16;
            body->x_fp += body->vx;
            body->vx += body->xa;
            slide_body(scene, &body->x_fp, &body->z_fp, ox, oz);
        }
        if (attack == 2 && body->mode == 11) {
            body->mode = 14;
            body->punch_act = 31;
            body->frame = 0;
            body->shown = 0;
            body->timer = 0;
        }
        if (!body->shown) {
            body->timer = hero_pose(scene, body)->duration;
            body->shown = 1;
            if (body->mode == 14) strike(scene, body, hero_pose(scene, body));
        } else {
            body->timer--;
            if (body->timer <= 0 && n > 0 && body->frame + 1 < n) {
                body->frame++;
                body->timer = hero_pose(scene, body)->duration;
                if (body->mode == 14) strike(scene, body, hero_pose(scene, body));
            }
        }
        if (body->y_fp >= (0xEF << 16)) {
            body->y_fp = 0xEF << 16;
            body->yv = 0;
            body->ya = 0;
            body->vx = 0;
            body->xa = 0;
            body->mode = 13;
            body->punch_act = 0;
            body->frame = 0;
            body->shown = 0;
        }
        return;
    }
    if (body->mode == 12 || body->mode == 13) {
        int n = scene->n_kick;
        if (body->mode == 13) n = scene->n_land;
        else if (body->punch_act == 46) n = scene->n_ckick;
        else if (body->punch_act == 48) n = scene->n_b48;
        else if (body->punch_act == 50) n = scene->n_b50;
        if (!body->shown) {
            body->timer = hero_pose(scene, body)->duration;
            body->shown = 1;
            if (body->mode == 12) strike(scene, body, hero_pose(scene, body));
        } else {
            body->timer--;
            if (body->timer <= 0) {
                body->frame++;
                if (n > 0 && body->frame >= n) {
                    body->mode = 0;
                    body->punch_act = 0;
                    body->vx = 0;
                    restart(body, 0);
                    return;
                }
                body->timer = hero_pose(scene, body)->duration;
                if (body->mode == 12) strike(scene, body, hero_pose(scene, body));
            }
        }
        return;
    }
    if (body->mode == 16) {
        /* DashMoveCheck keeps the dash while the matching direction is held.
           Move record +0x28 is X vel -1048576, accel 0. Right negates it.
           This has to run before the mode>=2 damage chain. */
        {
            int ox = body->x_fp >> 16;
            int oz = body->z_fp >> 16;
            body->x_fp += body->vx;
            slide_body(scene, &body->x_fp, &body->z_fp, ox, oz);
        }
        if (scene->n_dash > 0) {
            if (!body->shown) {
                body->timer = scene->dash[0].duration;
                body->shown = 1;
            } else if (--body->timer <= 0) {
                body->frame = (body->frame + 1) % scene->n_dash;
                body->timer = scene->dash[body->frame].duration;
            }
        }
        if (attack == 3) {
            body->mode = 0;
            body->vx = 0;
        } else if ((body->facing == 0 && !left) || (body->facing == 1 && !right)) {
            body->mode = 0;
            body->vx = 0;
            body->punch_act = 0;
            restart(body, 0);
            return;
        } else {
            return;
        }
    }
    if (body->mode >= 2) {
        int n = 1;
        {
            int ox = body->x_fp >> 16;
            int oz = body->z_fp >> 16;
            body->x_fp += body->vx;
            slide_body(scene, &body->x_fp, &body->z_fp, ox, oz);
            if (body->mode == 10) body->vx += 81920;
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
        else if (body->mode == 10) n = scene->n_jstun;
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
        if (body->mode == 10 && n > 0 && body->shown && body->timer <= 1 && body->frame + 1 >= n) {
            /* GT_NokeDamage 0x32cf4 ReadySets on STOP and does not read +0x134.
               A lethal reaction-5 hit still goes through GT_KO. This branch does not. */
            body->mode = 0;
            body->vx = 0;
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
    if (body->mode == 0 && body->punch_act == 0) {
        if (left && !body->was_left) {
            if (body->tap_l > 0) {
                body->mode = 16;
                body->facing = 0;
                body->vx = -1048576;
                body->xa = 0;
                body->frame = 0;
                body->shown = 0;
                body->tap_l = 0;
            } else body->tap_l = 16;
        }
        if (right && !body->was_right && body->mode == 0) {
            if (body->tap_r > 0) {
                body->mode = 16;
                body->facing = 1;
                body->vx = 1048576;
                body->xa = 0;
                body->frame = 0;
                body->shown = 0;
                body->tap_r = 0;
            } else body->tap_r = 16;
        }
    }
    body->was_left = left;
    body->was_right = right;
    if (body->tap_l > 0) body->tap_l--;
    if (body->tap_r > 0) body->tap_r--;
    if (body->mode == 16) return;
    if (body->mode == 0 && body->punch_act == 0 && attack == 4) {
        /* Edge bit 0x80. Held bit 1 (up, +Z) selects act 50. Held bit 4
           (down) selects act 48. The no-direction grab is not recovered. */
        if (up || down) {
            body->mode = 12;
            body->punch_act = up ? 50 : 48;
            body->frame = 0;
            body->timer = 0;
            body->shown = 0;
            body->vx = 0;
            return;
        }
    }
    if (body->mode == 0 && body->punch_act == 0 && attack == 5) {
        /* Button 9. Act 25 NEXT's into act 26 immediately. The hold is
           45 frames of pattern 11. Timer 44 plus this entry tick is 45. */
        body->mode = 21;
        body->timer = 45;
        body->frame = 0;
        body->shown = 1;
        body->vx = 0;
        body->punch_act = 0;
        return;
    }
    if (body->mode == 0 && body->punch_act == 0 && attack == 3) {
        /* ciBillyMoveData +0x18/-0x1c. Left keeps the record: vx -393216,
           x accel +16384. Right negates both, so vx +393216 and accel -16384.
           Y vel -1048576 and Y accel +212992 are not mirrored. */
        body->mode = 11;
        body->y_fp = 0xEF << 16;
        body->yv = -1048576;
        body->ya = 212992;
        body->vx = left ? -393216 : (right ? 393216 : 0);
        body->xa = left ? 16384 : (right ? -16384 : 0);
        if (left) body->facing = 0;
        if (right) body->facing = 1;
        body->frame = 0;
        body->shown = 0;
        body->punch_act = 0;
        return;
    }
    if (body->mode == 0 && body->punch_act == 0 && attack == 2) {
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
            if (dx <= 90 && dz <= 10) close = 1;
            foe->hit = 0;
        }
        body->mode = 12;
        body->punch_act = close ? 46 : 30;
        body->frame = 0;
        body->timer = 0;
        body->shown = 0;
        body->vx = 0;
        return;
    }
    if (attack == 1 && body->punch_act == 0) {
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
        /* ActionSub registers the hit before MoveX's push. A punch frame keeps the lunge. */
        if (!body->punch_act)
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

static void step_body(Scene *scene, Body *body, int left, int right, int up, int down, int attack) {
    step_hero(scene, body, left, right, up, down, attack);
    step_foes(scene, body);
    step_world(scene);
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
        int z[24], id[24], kind[24], n = 0, a, b;
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
        for (i = 0; i < scene->n_props; i++) {
            if (!scene->props[i].alive || scene->props[i].fire_mode == 0) continue;
            z[n] = scene->props[i].z;
            id[n] = 100 + i;
            kind[n] = -2 - i;
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
            if (kind[a] < -1) {
                const Prop *prop = &scene->props[-2 - kind[a]];
                int group = prop->playing ? (prop->fire_mode == 1 ? 1 : 3) : (prop->fire_mode == 1 ? 0 : 2);
                const Frame *fr;
                int fi;
                if (group < 0 || scene->objn[group] <= 0) continue;
                fi = prop->playing ? prop->frame : 0;
                if (fi < 0 || fi >= scene->objn[group]) fi = scene->objn[group] - 1;
                fr = &scene->objf[group][fi];
                sx = prop->x - camera;
                sy = (0xEF - prop->place_y) - prop->z;
                blit(fb, VIEW_W, VIEW_H, fr->px, fr->w, fr->h, sx - fr->ox, sy - fr->oy, 0);
            } else if (kind[a] < 0) {
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

/* Scripted stick and the one punch button. Not a KO hook: punches go through
   the normal lunge and the enemies die only if that hit connects. */
static void route_buttons(Scene *scene, Body *hero, int *left, int *right,
                           int *up, int *down, int *attack) {
    int f, best = 100000, behind = 100000, bdz = 100, bz = 0;
    int hx, hz, door, near = 100000, nz = 0;
    if (hero->mode != 0 || hero->punch_act != 0) return;
    hx = hero->x_fp >> 16;
    hz = hero->z_fp >> 16;
    door = hx > 760;
    for (f = 0; f < scene->n_foes; f++) {
        int dx, adz;
        if (!scene->foes[f].alive || scene->foes[f].mode == 4) continue;
        dx = scene->foes[f].x - hx;
        adz = scene->foes[f].z - hz;
        if (adz < 0) adz = -adz;
        if (adz + (dx < 0 ? -dx : dx) < near) {
            near = adz + (dx < 0 ? -dx : dx);
            nz = scene->foes[f].z;
        }
        if (dx <= -16 && -dx < behind) behind = -dx;
        if (dx > -16 && dx < best && adz < 18) {
            best = dx;
            bdz = adz;
            bz = scene->foes[f].z;
        }
    }
    /* Past the door the pair finishes wall entry below Jimmy's lane.
       Close Z, then button 7 up (act 50) when the box can reach. */
    if (door && best < 70 && best > -10 && bdz <= 6) {
        if (best < 42) {
            *attack = 4;
            *up = 1;
        } else *attack = 2;
    } else if (door && best < 160 && bdz > 6) {
        if (best < 80) *left = 1;
        else *right = 1;
        if (bz < hz) *down = 1;
        else *up = 1;
    } else if (best < 56 && best > -40 && bdz < 14) *attack = 2;
    else if (best < 36) *left = 1;
    else if (best == 100000 && behind < 800) *left = 1;
    else *right = 1;
    if (door && *attack == 0 && near < 400 && nz != hz) {
        if (nz < hz) *down = 1;
        else *up = 1;
    }
}

/* After the real door spawn, stay outside the zako punch Z (7px) until X
   is inside the kick, then step onto their Z and kick on the next tick.
   Player priority 2 starts that kick before they can open a punch. */
static void door_buttons(Scene *scene, Body *hero, int *left, int *right,
                          int *up, int *down, int *attack) {
    int f, best = -1, best_dx = 100000;
    int hx, hz, dx, dz, adx, adz, want;
    if (scene->phase < 1) {
        route_buttons(scene, hero, left, right, up, down, attack);
        return;
    }
    if (hero->mode != 0 || hero->punch_act != 0) return;
    hx = hero->x_fp >> 16;
    hz = hero->z_fp >> 16;
    for (f = 0; f < scene->n_foes; f++) {
        Foe *foe = &scene->foes[f];
        int d;
        if (!foe->alive || foe->hp <= 0 || foe->mode >= 2 || foe->wall_cc >= 0) continue;
        if (foe->x < 700) continue;
        d = foe->x - hx;
        if (d < 0) d = -d;
        if (d < best_dx) {
            best_dx = d;
            best = f;
        }
    }
    if (best < 0) {
        route_buttons(scene, hero, left, right, up, down, attack);
        return;
    }
    dx = scene->foes[best].x - hx;
    dz = scene->foes[best].z - hz;
    adx = dx < 0 ? -dx : dx;
    adz = dz < 0 ? -dz : dz;
    if (adx > 36) {
        if (dx > 0) *right = 1;
        else *left = 1;
        want = scene->foes[best].z + (scene->foes[best].z > 50 ? -10 : 10);
        if (hz < want - 1) *up = 1;
        else if (hz > want + 1) *down = 1;
        return;
    }
    if ((dx > 0 && hero->facing == 0) || (dx < 0 && hero->facing == 1)) {
        if (dx > 0) *right = 1;
        else *left = 1;
        return;
    }
    if (adz > 4) {
        if (dz > 0) *up = 1;
        else *down = 1;
        return;
    }
    *attack = 2;
}

#if defined(_WIN32)
#include <windows.h>

static int g_left, g_right, g_up, g_down, g_atk, g_kick, g_jump, g_b7, g_crouch, g_quit;
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
        if (wp == 'K') g_kick = down;
        if (wp == VK_SPACE) g_jump = down;
        if (wp == 'U') g_b7 = down;
        if (wp == 'C') g_crouch = down;
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
    int was_atk = 0, was_kick = 0, was_jump = 0, was_b7 = 0, was_crouch = 0;
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
        if (getenv("ROUTE")) {
            int left = 0, right = 0, up = 0, down = 0, attack = 0;
            door_buttons(scene, &hero, &left, &right, &up, &down, &attack);
            step_body(scene, &hero, left, right, up, down, was_atk ? 0 : attack);
            was_atk = attack;
        } else {
            {
                int action = 0;
                if (g_jump && !was_jump) action = 3;
                else if (g_b7 && !was_b7) action = 4;
                else if (g_crouch && !was_crouch) action = 5;
                else if (g_kick && !was_kick) action = 2;
                else if (g_atk && !was_atk) action = 1;
                step_body(scene, &hero, g_left, g_right, g_up, g_down, action);
                was_atk = g_atk;
                was_kick = g_kick;
                was_jump = g_jump;
                was_b7 = g_b7;
                was_crouch = g_crouch;
            }
        }
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
    int atk = 0, kick = 0, jump = 0, b7 = 0, crouch = 0;
    int was_atk = 0, was_kick = 0, was_jump = 0, was_b7 = 0, was_crouch = 0;
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
            if (key == XK_k) kick = pressed;
            if (key == XK_space) jump = pressed;
            if (key == XK_u) b7 = pressed;
            if (key == XK_c) crouch = pressed;
            if (key == XK_Escape) goto done;
        }
        {
            int action = 0;
            if (jump && !was_jump) action = 3;
            else if (b7 && !was_b7) action = 4;
            else if (crouch && !was_crouch) action = 5;
            else if (kick && !was_kick) action = 2;
            else if (atk && !was_atk) action = 1;
            step_body(scene, &hero, left, right, up, down, action);
            was_atk = atk;
            was_kick = kick;
            was_jump = jump;
            was_b7 = b7;
            was_crouch = crouch;
        }
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
    if (hold == 13 && scene->n_foes < 12) {
        Foe *linda;
        for (i = 0; i < scene->n_foes; i++) scene->foes[i].alive = 0;
        linda = &scene->foes[scene->n_foes++];
        memset(linda, 0, sizeof(*linda));
        linda->wall_cc = -1;
        linda->x = 120;
        linda->z = 56;
        linda->hp = 25;
        linda->sheet = 3;
        linda->alive = 1;
        linda->facing = 0;
        linda->mode = 1;
        linda->x_fp = 120 << 16;
        linda->z_fp = 56 << 16;
        linda->y_fp = 0xEF << 16;
        hero.x_fp = 80 << 16;
        hero.facing = 1;
    }
    if (hold == 14) {
        int f;
        /* Scroll 100 spawns the door (extra 10) and the wall (extra 20).
           The fixture then spends the last point of a quota whose trigger is
           key 2, which is what four counting KOs do. */
        scene->camera = 100;
        consume_rows(scene);
        scene->camera = 0;
        for (f = 0; f < scene->n_foes; f++) {
            scene->foes[f].alive = 0;
            scene->foes[f].counts = 0;
            scene->foes[f].hp = 0;
        }
        scene->quota = 1;
        scene->trigger = 2;
        spawn_enemy(scene, 10, 8, 1, 0, 0x20, 0);
        scene->foes[scene->n_foes - 1].alive = 0;
        note_ko(scene, &scene->foes[scene->n_foes - 1]);
    }
    if (hold == 17) {
        int f;
        /* Labeled hitbox fixture: one foe inside the 90px close-kick range. */
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        spawn_enemy(scene, 120, 56, 40, 2, 0, 0);
        hero.facing = 1;
    }
    if (hold == 21) {
        int f;
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        spawn_enemy(scene, 90, 56, 40, 2, 0, 0);
        hero.facing = 1;
    }
    if (hold == 22) {
        int f;
        /* Act 48's box only reaches 3px past the origin when facing right. */
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        spawn_enemy(scene, 70, 56, 40, 2, 0, 0);
        hero.facing = 1;
    }
    if (hold == 24) {
        int f;
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        spawn_enemy(scene, 50, 56, 40, 2, 0, 0);
        hero.facing = 0;
    }
    if (hold == 25) {
        int f;
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        spawn_enemy(scene, 70, 56, 22, 2, 0, 0);
        hero.facing = 1;
    }
    if (hold == 23) {
        int f;
        for (f = 0; f < scene->n_foes; f++) scene->foes[f].alive = 0;
        /* Flag 0x130: count, no push, GT_EnemyWallEntry. Born at Z 76. */
        spawn_enemy(scene, 767, 76, 40, 1, 0x130, 0);
    }
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
        if (hold == 13 && i > 40 && hero.mode == 0) attack = 1;
        if (hold == 15) door_buttons(scene, &hero, &left, &right, &up, &down, &attack);
        if (hold == 16 && i == 0) attack = 3;
        if (hold == 17 && i == 2) attack = 2;
        if (hold == 18) { right = 1; if (i == 0) attack = 3; }
        if (hold == 19) { left = 1; if (i == 0) attack = 3; }
        if (hold == 20 && (i == 0 || i >= 8)) right = 1;
        if (hold == 21 && i == 0) { up = 1; attack = 4; }
        if (hold == 22 && i == 0) { down = 1; attack = 4; }
        if (hold == 24 && i == 0) { down = 1; attack = 4; }
        if (hold == 25 && i == 0) { down = 1; attack = 4; }
        if (hold == 26 && i == 0) attack = 5;
        if (hold == 27) { if (i == 0) attack = 5; else right = 1; }
        if (hold == 14 && (i == 43 || i == 132)) {
            int f;
            for (f = 0; f < scene->n_foes; f++) {
                if (!scene->foes[f].alive || !scene->foes[f].counts) continue;
                scene->foes[f].hp = 0;
                scene->foes[f].alive = 0;
                scene->foes[f].mode = 4;
                note_ko(scene, &scene->foes[f]);
            }
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
    if ((hold >= 5 && hold <= 9) || hold == 12 || hold == 13 || hold == 14 || hold == 15 ||
        hold == 16 || hold == 17 || hold == 21 || hold == 22 || hold == 23 ||
        hold == 24 || hold == 25 || hold == 26 || hold == 27) {
        printf("hp %d", hero.hp);
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].hp);
        printf(" x");
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].x);
        printf(" m");
        for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].mode);
        printf(" pm %d y %d cam %d lock %d nf %d ph %d q %d\n", hero.mode, hero.y_fp >> 16,
               scene->camera, scene->lock_i, scene->n_foes, scene->phase, scene->quota);
        if (hold == 23) {
            printf("fz");
            for (i = 0; i < scene->n_foes; i++) printf(" %d", scene->foes[i].z);
            printf("\n");
        }
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
        if (argc > 5 && strcmp(argv[5], "react") == 0) hold = 13;
        if (argc > 5 && strcmp(argv[5], "chain") == 0) hold = 14;
        if (argc > 5 && strcmp(argv[5], "route") == 0) hold = 15;
        if (argc > 5 && strcmp(argv[5], "jump") == 0) hold = 16;
        if (argc > 5 && strcmp(argv[5], "kick") == 0) hold = 17;
        if (argc > 5 && strcmp(argv[5], "jumpr") == 0) hold = 18;
        if (argc > 5 && strcmp(argv[5], "jumpl") == 0) hold = 19;
        if (argc > 5 && strcmp(argv[5], "dash") == 0) hold = 20;
        if (argc > 5 && strcmp(argv[5], "b7") == 0) hold = 21;
        if (argc > 5 && strcmp(argv[5], "b7d") == 0) hold = 22;
        if (argc > 5 && strcmp(argv[5], "b7l") == 0) hold = 24;
        if (argc > 5 && strcmp(argv[5], "b7k") == 0) hold = 25;
        if (argc > 5 && strcmp(argv[5], "crouch") == 0) hold = 26;
        if (argc > 5 && strcmp(argv[5], "cwalk") == 0) hold = 27;
        if (argc > 5 && strcmp(argv[5], "entry") == 0) hold = 23;
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
