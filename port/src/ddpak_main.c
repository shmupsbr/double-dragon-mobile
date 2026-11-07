#include "ddobm.h"
#include "ddpak.h"
#include "ddtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

static int cmd_list(const DdPak *pak) {
    size_t i;
    printf("files %zu  dirs %zu  index %llu\n", pak->file_count, pak->dir_count,
           (unsigned long long)pak->index_offset);
    for (i = 0; i < pak->count; i++) {
        const DdNode *node = &pak->nodes[i];
        if (node->is_dir) {
            printf("dir  %s  children %u\n", node->path, node->child_count);
        } else {
            printf("file %s  %llu\n", node->path, (unsigned long long)node->size);
        }
    }
    return 0;
}

static int cmd_text(const DdPak *pak, const uint8_t *file, const char *entry) {
    const DdNode *node = dd_pak_find(pak, entry);
    DdText text;
    uint16_t i;
    if (!node) {
        fprintf(stderr, "missing %s\n", entry);
        return 1;
    }
    if (dd_text_parse(file + node->data_offset, (size_t)node->size, &text) != 0) {
        fprintf(stderr, "not a contiguous string table: %s\n", entry);
        return 2;
    }
    printf("strings %u  ids %u\n", text.string_count, text.id_count);
    for (i = 0; i < text.string_count; i++) {
        printf("%u\t%.*s\n", (unsigned)i, (int)text.lengths[i],
               (const char *)(text.bytes + text.offsets[i]));
    }
    dd_text_free(&text);
    return 0;
}

static int cmd_obm(const DdPak *pak, const uint8_t *file, const char *entry, const char *ppm) {
    const DdNode *node = dd_pak_find(pak, entry);
    DdImage image;
    if (!node) {
        fprintf(stderr, "missing %s\n", entry);
        return 1;
    }
    if (dd_obm_decode(file + node->data_offset, (size_t)node->size, &image) != 0) {
        fprintf(stderr, "obm decode failed: %s\n", entry);
        return 2;
    }
    printf("%s %ux%u bpp %u version %u palette rgba5551 inferred\n", entry, image.width,
           image.height, image.bpp, image.version);
    if (ppm && dd_image_write_ppm(&image, ppm) != 0) {
        dd_image_free(&image);
        fprintf(stderr, "ppm write failed\n");
        return 3;
    }
    dd_image_free(&image);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "ddpak list <res.pak>\n"
            "ddpak text <res.pak> <entry>\n"
            "ddpak obm <res.pak> <entry> [out.ppm]\n"
            "Resource tool for the original G-gee res.pak. Not the game.\n");
}

int main(int argc, char **argv) {
    size_t nbytes = 0;
    uint8_t *file;
    DdPak pak;
    int rc;
    if (argc < 3) {
        usage();
        return 2;
    }
    file = read_file(argv[2], &nbytes);
    if (!file) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    if (dd_pak_open(&pak, file, nbytes) != 0) {
        fprintf(stderr, "pak open failed\n");
        free(file);
        return 1;
    }
    if (strcmp(argv[1], "list") == 0) {
        rc = cmd_list(&pak);
    } else if (strcmp(argv[1], "text") == 0 && argc >= 4) {
        rc = cmd_text(&pak, file, argv[3]);
    } else if (strcmp(argv[1], "obm") == 0 && argc >= 4) {
        rc = cmd_obm(&pak, file, argv[3], argc >= 5 ? argv[4] : NULL);
    } else {
        usage();
        rc = 2;
    }
    dd_pak_close(&pak);
    free(file);
    return rc;
}
