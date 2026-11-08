#include "ddtext.h"

#include <stdlib.h>
#include <string.h>

static uint16_t ru16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

int dd_text_parse(const uint8_t *data, size_t nbytes, DdText *out) {
    uint16_t nstrings, nids, i;
    size_t table;
    uint16_t *ids = NULL;
    uint16_t *offs = NULL;
    uint16_t *lens = NULL;

    memset(out, 0, sizeof(*out));
    if (nbytes < 4 || (nbytes % 2u) != 0) {
        return -1;
    }
    nstrings = ru16(data);
    nids = ru16(data + 2);
    if (nstrings == 0 || nstrings > 4000 || nids > 4000) {
        return -1;
    }
    table = 4u + (size_t)nids * 2u;
    if (table + (size_t)nstrings * 4u > nbytes) {
        return -1;
    }
    ids = (uint16_t *)malloc(sizeof(uint16_t) * (nids ? nids : 1));
    offs = (uint16_t *)malloc(sizeof(uint16_t) * nstrings);
    lens = (uint16_t *)malloc(sizeof(uint16_t) * nstrings);
    if (!ids || !offs || !lens) {
        free(ids);
        free(offs);
        free(lens);
        return -1;
    }
    for (i = 0; i < nids; i++) {
        ids[i] = ru16(data + 4u + (size_t)i * 2u);
    }
    for (i = 0; i < nstrings; i++) {
        size_t at = table + (size_t)i * 4u;
        offs[i] = ru16(data + at);
        lens[i] = ru16(data + at + 2);
        if ((size_t)offs[i] + lens[i] > nbytes) {
            free(ids);
            free(offs);
            free(lens);
            return -1;
        }
        if (i > 0 && (uint32_t)offs[i - 1] + lens[i - 1] != offs[i]) {
            free(ids);
            free(offs);
            free(lens);
            return -1;
        }
    }
    if ((size_t)offs[nstrings - 1] + lens[nstrings - 1] != nbytes) {
        free(ids);
        free(offs);
        free(lens);
        return -1;
    }
    out->string_count = nstrings;
    out->id_count = nids;
    out->ids = ids;
    out->offsets = offs;
    out->lengths = lens;
    out->bytes = data;
    out->nbytes = nbytes;
    return 0;
}

void dd_text_free(DdText *text) {
    if (!text) {
        return;
    }
    free(text->ids);
    free(text->offsets);
    free(text->lengths);
    memset(text, 0, sizeof(*text));
}
