#include "ddpak.h"

#include <stdlib.h>
#include <string.h>

static uint32_t ru32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t ru64(const uint8_t *p) {
    return (uint64_t)ru32(p) | ((uint64_t)ru32(p + 4) << 32);
}

static int append_path(char **dest, const char *prefix, const char *name, int slash) {
    size_t np = strlen(prefix);
    size_t nn = strlen(name);
    char *out = (char *)malloc(np + nn + 2);
    if (!out) {
        return -1;
    }
    memcpy(out, prefix, np);
    memcpy(out + np, name, nn);
    if (slash) {
        out[np + nn] = '/';
        out[np + nn + 1] = 0;
    } else {
        out[np + nn] = 0;
    }
    *dest = out;
    return 0;
}

/* Returns the offset after this subtree, or 0 on failure. */
static size_t parse_node(DdPak *pak, size_t off, const char *prefix, size_t *used, size_t cap) {
    const uint8_t *b = pak->bytes;
    size_t n = pak->nbytes;
    uint32_t recsz;
    uint32_t type;
    const char *name;
    size_t name_at;
    char *path = NULL;

    if (off + 24 > n) {
        return 0;
    }
    recsz = ru32(b + off + 16);
    type = ru32(b + off + 20);
    if (recsz < 24 || off + recsz > pak->index_offset + (n - pak->index_offset)) {
        return 0;
    }
    if (off + recsz > n - 8) {
        return 0;
    }
    if (*used >= cap) {
        return 0;
    }

    if (type == 1) {
        uint64_t size;
        if (recsz < 32) {
            return 0;
        }
        size = ru64(b + off + 24);
        name_at = off + 32;
        if (name_at >= off + recsz) {
            return 0;
        }
        name = (const char *)(b + name_at);
        if (memchr(name, 0, (off + recsz) - name_at) == NULL) {
            return 0;
        }
        if (append_path(&path, prefix, name, 0) != 0) {
            return 0;
        }
        if (size > n || ru64(b + off + 8) > n || ru64(b + off + 8) + size > pak->index_offset) {
            free(path);
            return 0;
        }
        pak->nodes[*used].is_dir = 0;
        pak->nodes[*used].mtime = ru32(b + off);
        pak->nodes[*used].data_offset = ru64(b + off + 8);
        pak->nodes[*used].size = size;
        pak->nodes[*used].child_count = 0;
        pak->nodes[*used].path = path;
        *used += 1;
        pak->file_count += 1;
        return off + recsz;
    }

    if (type == 2) {
        uint32_t children;
        size_t i;
        size_t cursor;
        if (recsz < 32) {
            return 0;
        }
        children = ru32(b + off + 28);
        name_at = off + 32;
        name = (const char *)(b + name_at);
        if (memchr(name, 0, (off + recsz) - name_at) == NULL) {
            return 0;
        }
        if (append_path(&path, prefix, name, name[0] != 0) != 0) {
            return 0;
        }
        pak->nodes[*used].is_dir = 1;
        pak->nodes[*used].mtime = ru32(b + off);
        pak->nodes[*used].data_offset = ru64(b + off + 8);
        pak->nodes[*used].size = 0;
        pak->nodes[*used].child_count = children;
        pak->nodes[*used].path = path;
        *used += 1;
        pak->dir_count += 1;
        cursor = off + recsz;
        for (i = 0; i < children; i++) {
            cursor = parse_node(pak, cursor, path, used, cap);
            if (cursor == 0) {
                return 0;
            }
        }
        return cursor;
    }

    return 0;
}

int dd_pak_open(DdPak *pak, const uint8_t *bytes, size_t nbytes) {
    uint64_t index;
    size_t used = 0;
    size_t cap;
    size_t end;

    memset(pak, 0, sizeof(*pak));
    if (nbytes < 16 || bytes == NULL) {
        return -1;
    }
    index = ru32(bytes + nbytes - 8);
    if (ru32(bytes + nbytes - 4) != 0 || index >= nbytes - 8) {
        return -1;
    }
    /* Upper bound: one node cannot be smaller than 32 bytes. */
    cap = (nbytes - 8 - (size_t)index) / 32 + 8;
    if (cap > 100000) {
        cap = 100000;
    }
    pak->bytes = bytes;
    pak->nbytes = nbytes;
    pak->index_offset = index;
    pak->nodes = (DdNode *)calloc(cap, sizeof(DdNode));
    if (!pak->nodes) {
        return -1;
    }
    end = parse_node(pak, (size_t)index, "", &used, cap);
    pak->count = used;
    if (end == 0 || end != nbytes - 8) {
        dd_pak_close(pak);
        return -1;
    }
    return 0;
}

void dd_pak_close(DdPak *pak) {
    size_t i;
    if (!pak) {
        return;
    }
    for (i = 0; i < pak->count; i++) {
        free(pak->nodes[i].path);
    }
    free(pak->nodes);
    memset(pak, 0, sizeof(*pak));
}

const DdNode *dd_pak_find(const DdPak *pak, const char *path) {
    size_t i;
    for (i = 0; i < pak->count; i++) {
        if (!pak->nodes[i].is_dir && strcmp(pak->nodes[i].path, path) == 0) {
            return &pak->nodes[i];
        }
    }
    return NULL;
}
