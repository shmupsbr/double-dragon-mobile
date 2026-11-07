#ifndef DDOBM_H
#define DDOBM_H

#include <stddef.h>
#include <stdint.h>

/* OBM ("OI") image inside res.pak.
   Header is exact: magic "OI", u8 version, u8 bpp (4 or 8), u16 w, u16 h,
   then 2^bpp little-endian u16 palette entries, then indices
   (4 bpp: low nibble first). The byte length matches w*h*bpp/8.

   Palette expansion to 8-bit RGBA uses RGBA5551 (bit 0 = alpha).
   That expansion is inferred by earlier visual comparison, not by a
   format field in the file. */

typedef struct DdImage {
    unsigned width;
    unsigned height;
    unsigned bpp;
    unsigned version;
    int palette_rgba5551_inferred;
    uint8_t *rgba; /* width * height * 4, malloc'd */
} DdImage;

int dd_obm_decode(const uint8_t *data, size_t nbytes, DdImage *out);
void dd_image_free(DdImage *image);

/* Write a binary PPM (P6). Drops alpha. Returns 0 on success. */
int dd_image_write_ppm(const DdImage *image, const char *path);

#endif
