#include "ddobm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned expand5(unsigned v) {
    return (v * 255u) / 31u;
}

int dd_obm_decode(const uint8_t *data, size_t nbytes, DdImage *out) {
    unsigned version, bpp, width, height, colors, i, pixels, need;
    const uint8_t *pal;
    const uint8_t *idx;
    uint8_t *rgba;

    memset(out, 0, sizeof(*out));
    if (nbytes < 8 || data[0] != 'O' || data[1] != 'I') {
        return -1;
    }
    version = data[2];
    bpp = data[3];
    width = (unsigned)data[4] | ((unsigned)data[5] << 8);
    height = (unsigned)data[6] | ((unsigned)data[7] << 8);
    if ((bpp != 4 && bpp != 8) || width == 0 || height == 0) {
        return -1;
    }
    if (width > 4096 || height > 4096) {
        return -1;
    }
    colors = 1u << bpp;
    pixels = width * height;
    if (bpp == 8) {
        need = pixels;
    } else if ((pixels % 2u) != 0) {
        return -1;
    } else {
        need = pixels / 2u;
    }
    if (8u + colors * 2u + need != nbytes) {
        return -1;
    }
    pal = data + 8;
    idx = pal + colors * 2u;
    rgba = (uint8_t *)malloc((size_t)pixels * 4u);
    if (!rgba) {
        return -1;
    }
    for (i = 0; i < pixels; i++) {
        unsigned entry;
        unsigned raw;
        if (bpp == 8) {
            entry = idx[i];
        } else if ((i % 2u) == 0) {
            entry = idx[i / 2u] & 15u;
        } else {
            entry = idx[i / 2u] >> 4;
        }
        raw = (unsigned)pal[entry * 2u] | ((unsigned)pal[entry * 2u + 1u] << 8);
        /* Inferred RGBA5551. Bit 0 is alpha. Channels are 5 bits each. */
        rgba[i * 4u + 0] = (uint8_t)expand5((raw >> 11) & 31u);
        rgba[i * 4u + 1] = (uint8_t)expand5((raw >> 6) & 31u);
        rgba[i * 4u + 2] = (uint8_t)expand5((raw >> 1) & 31u);
        rgba[i * 4u + 3] = (raw & 1u) ? 255u : 0u;
    }
    out->width = width;
    out->height = height;
    out->bpp = bpp;
    out->version = version;
    out->palette_rgba5551_inferred = 1;
    out->rgba = rgba;
    return 0;
}

void dd_image_free(DdImage *image) {
    if (!image) {
        return;
    }
    free(image->rgba);
    memset(image, 0, sizeof(*image));
}

int dd_image_write_ppm(const DdImage *image, const char *path) {
    FILE *f;
    unsigned i;
    if (!image || !image->rgba) {
        return -1;
    }
    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if (fprintf(f, "P6\n%u %u\n255\n", image->width, image->height) < 0) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < image->width * image->height; i++) {
        if (fwrite(image->rgba + i * 4u, 1, 3, f) != 3) {
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0) {
        return -1;
    }
    return 0;
}
