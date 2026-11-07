#ifndef DDPAK_H
#define DDPAK_H

#include <stddef.h>
#include <stdint.h>

/* On-disk res.pak, recovered from the original container.
   Bytes are concatenated, 8-byte aligned. The last 8 bytes are
   u32 index_offset, u32 0. The index is a preorder tree:

     u32 mtime, u32 zero, u64 data_offset, u32 record_bytes, u32 type
     type 1 file: u64 size, NUL-terminated name, pad to record_bytes
     type 2 dir:  u32 zero, u32 child_count, NUL-terminated name, pad

   This is not a game. It does not contain the encrypted module. */

typedef struct DdNode {
    int is_dir;
    uint32_t mtime;
    uint64_t data_offset;
    uint64_t size;
    uint32_t child_count;
    char *path;
} DdNode;

typedef struct DdPak {
    const uint8_t *bytes;
    size_t nbytes;
    uint64_t index_offset;
    DdNode *nodes;
    size_t count;
    size_t file_count;
    size_t dir_count;
} DdPak;

/* bytes are borrowed for the lifetime of the pak. Returns 0 on success. */
int dd_pak_open(DdPak *pak, const uint8_t *bytes, size_t nbytes);
void dd_pak_close(DdPak *pak);
const DdNode *dd_pak_find(const DdPak *pak, const char *path);

#endif
