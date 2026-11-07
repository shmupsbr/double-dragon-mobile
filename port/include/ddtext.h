#ifndef DDTEXT_H
#define DDTEXT_H

#include <stddef.h>
#include <stdint.h>

/* Six of the eight *.dat language tables share this layout, checked
   against the file bytes rather than assumed:

     u16 string_count
     u16 id_count
     u16 ids[id_count]
     then string_count records of { u16 offset, u16 length }
     string bytes are contiguous and consume the file through the last byte

   The integer ids are stored as they appear. Their gameplay meaning is
   not in the table. HELP *.dat files do not satisfy the contiguous
   check and are rejected. Encoding is not declared: English achievement
   strings are ASCII; Japanese bytes are left raw. */

typedef struct DdText {
    uint16_t string_count;
    uint16_t id_count;
    uint16_t *ids;
    uint16_t *offsets;
    uint16_t *lengths;
    const uint8_t *bytes; /* borrowed */
    size_t nbytes;
} DdText;

int dd_text_parse(const uint8_t *data, size_t nbytes, DdText *out);
void dd_text_free(DdText *text);

#endif
