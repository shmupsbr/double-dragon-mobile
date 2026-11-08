#include "ddobm.h"
#include "ddpak.h"
#include "ddtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed += 1;
    }
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *b;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static void put_u32(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void test_synthetic(void) {
    /* One file "hi" named a.txt, then a directory record would be heavier.
       Root record is the file itself. Footer points at the index. */
    const char *payload = "hi";
    const char *name = "a.txt";
    uint8_t rec[64];
    uint8_t image[64];
    size_t name_len = strlen(name);
    size_t recsz = 32 + name_len + 1;
    size_t total;
    uint8_t *blob;
    DdPak pak;
    const DdNode *node;
    memset(rec, 0, sizeof(rec));
    put_u32(rec + 0, 7); /* mtime */
    put_u32(rec + 16, (unsigned)recsz);
    put_u32(rec + 20, 1); /* file */
    rec[24] = 2;          /* size 2 */
    memcpy(rec + 32, name, name_len + 1);
    total = 2 + recsz + 8;
    blob = (uint8_t *)calloc(1, total);
    memcpy(blob, payload, 2);
    memcpy(blob + 2, rec, recsz);
    put_u32(blob + 2 + recsz, 2); /* index offset */
    expect(dd_pak_open(&pak, blob, total) == 0, "synthetic open");
    expect(pak.file_count == 1, "synthetic one file");
    node = dd_pak_find(&pak, "a.txt");
    expect(node != NULL && node->size == 2, "synthetic find");
    expect(node && memcmp(blob + node->data_offset, "hi", 2) == 0, "synthetic bytes");
    dd_pak_close(&pak);
    free(blob);

    /* 1x1 OBM, 8 bpp, one palette entry with alpha bit set, index 0. */
    memset(image, 0, sizeof(image));
    image[0] = 'O';
    image[1] = 'I';
    image[2] = 4;
    image[3] = 8;
    image[4] = 1;
    image[6] = 1;
    image[8] = 0x01; /* raw 0x0001: alpha on, rgb 0 */
    {
        DdImage img;
        expect(dd_obm_decode(image, 16, &img) != 0, "truncated palette rejected");
        image[8] = 0x01;
        image[9] = 0x00;
        /* rebuild exact size: 8 + 512 + 1 */
        {
            uint8_t *exact = (uint8_t *)calloc(1, 8 + 512 + 1);
            exact[0] = 'O';
            exact[1] = 'I';
            exact[2] = 4;
            exact[3] = 8;
            exact[4] = 1;
            exact[6] = 1;
            exact[8] = 0xFF;
            exact[9] = 0xFF; /* all channel bits and alpha */
            exact[8 + 512] = 0;
            expect(dd_obm_decode(exact, 8 + 512 + 1, &img) == 0, "1x1 decode");
            expect(img.width == 1 && img.height == 1 && img.bpp == 8, "1x1 size");
            expect(img.palette_rgba5551_inferred == 1, "palette flagged inferred");
            expect(img.rgba[0] == 255 && img.rgba[3] == 255, "5551 expand");
            dd_image_free(&img);
            exact[9] = 0x00; /* alpha bit clear, rgb still high in low bits? raw 0x00FF */
            /* 0x00FF: bit0 of the u16 is the low byte's bit 0. 0xFF has bit0 set.
               Use 0x00FE so bit0 is clear and the 5-bit channels are nonzero. */
            exact[8] = 0xFE;
            exact[9] = 0xFF;
            expect(dd_obm_decode(exact, 8 + 512 + 1, &img) == 0, "alpha off decode");
            expect(img.rgba[3] == 0, "alpha bit clear");
            expect(img.rgba[0] != 0, "color still present");
            dd_image_free(&img);
            free(exact);
        }
    }
}

static void test_original(const char *pak_path) {
    size_t n = 0;
    uint8_t *file = slurp(pak_path, &n);
    DdPak pak;
    const DdNode *node;
    DdImage image;
    DdText text;
        unsigned i;
    unsigned opaque = 0;
    unsigned bad_alpha = 0;
    if (!file) {
        fprintf(stderr, "FAIL cannot read %s\n", pak_path);
        g_failed += 1;
        return;
    }
    expect(dd_pak_open(&pak, file, n) == 0, "original pak open");
    expect(pak.file_count == 292, "292 files");
    expect(pak.dir_count == 7, "7 dirs");
    expect(pak.index_offset == 53366992ull, "index offset from footer");
    node = dd_pak_find(&pak, "res/obm2/BILLY_1.obm");
    expect(node != NULL, "billy sheet present");
    if (node) {
        expect(dd_obm_decode(file + node->data_offset, (size_t)node->size, &image) == 0,
               "billy obm decodes");
        expect(image.width == 512 && image.height == 512 && image.bpp == 4, "billy 512x512 4bpp");
        for (i = 0; i < image.width * image.height; i++) {
            unsigned a = image.rgba[i * 4u + 3];
            if (a != 0 && a != 255) {
                bad_alpha += 1;
            }
            if (a) {
                opaque += 1;
            }
        }
        expect(bad_alpha == 0, "alpha is 5551");
        expect(opaque > 1000 && opaque < image.width * image.height, "billy has transparency");
        dd_image_free(&image);
    }
    node = dd_pak_find(&pak, "res/Language_EN/ddiPhoneAchieve_En.dat");
    expect(node != NULL, "achieve dat present");
    if (node &&
        dd_text_parse(file + node->data_offset, (size_t)node->size, &text) == 0) {
        const char *want = "First time in \"Double Dragon\" Mode.";
        expect(text.string_count == 92, "92 achievement strings");
        expect(text.lengths[0] == strlen(want), "first length");
        expect(memcmp(text.bytes + text.offsets[0], want, text.lengths[0]) == 0, "first string");
        dd_text_free(&text);
    } else {
        expect(0, "achieve table parse");
    }
    node = dd_pak_find(&pak, "res/Language_EN/ddiPhoneHELP_En.dat");
    if (node) {
        expect(dd_text_parse(file + node->data_offset, (size_t)node->size, &text) != 0,
               "HELP table is a different layout");
    }
    node = dd_pak_find(&pak, "res/sound/se21.wav");
    expect(node != NULL && node->size > 12, "se21 present");
    if (node) {
        expect(memcmp(file + node->data_offset, "RIFF", 4) == 0, "se21 is RIFF");
    }
    dd_pak_close(&pak);
    free(file);
}

int main(int argc, char **argv) {
    const char *pak = argc > 1 ? argv[1] : NULL;
    test_synthetic();
    if (pak) {
        test_original(pak);
    } else {
        fprintf(stderr, "FAIL original pak path required\n");
        g_failed += 1;
    }
    if (g_failed) {
        fprintf(stderr, "%d failure(s)\n", g_failed);
        return 1;
    }
    printf("ok\n");
    return 0;
}
