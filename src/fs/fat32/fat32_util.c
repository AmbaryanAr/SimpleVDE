#include "fat32_util.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int strcasecmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        char ca = tolower((unsigned char)*a);
        char cb = tolower((unsigned char)*b);
        if (ca != cb) return (int)(ca - cb);
        a++;
        b++;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

void sfn_to_display_name(const uint8_t sfn[11], char *out, size_t out_size) {
    if (out_size < 13) return;
    int pos = 0;
    for (int i = 0; i < 8 && sfn[i] != ' '; i++) {
        out[pos++] = (char)sfn[i];
    }
    if (sfn[8] != ' ') {
        out[pos++] = '.';
        for (int i = 0; i < 3 && sfn[8 + i] != ' '; i++) {
            out[pos++] = (char)sfn[8 + i];
        }
    }
    out[pos] = '\0';
}

char* extract_lfn_name(const uint8_t *dir_buffer, uint32_t sfn_index) {
    uint32_t lfn_start = sfn_index;
    while (lfn_start > 0) {
        const uint8_t *e = dir_buffer + (lfn_start - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        if (e[0] & 0x40) {
            lfn_start--;
            break;
        }
        lfn_start--;
    }
    if (lfn_start == sfn_index) return NULL;

    uint32_t first_lfn = lfn_start;
    while (first_lfn > 0) {
        const uint8_t *e = dir_buffer + (first_lfn - 1) * 32;
        if (e[11] != FAT32_ATTR_LFN) break;
        first_lfn--;
    }

    uint16_t utf16[256];
    size_t pos = 0;
    for (uint32_t i = first_lfn; i < sfn_index; i++) {
        const Fat32LongEntry *lfn = (const Fat32LongEntry*)(dir_buffer + i * 32);
        if (lfn->attr != FAT32_ATTR_LFN) return NULL;
        for (int j = 0; j < 5; j++) {
            if (lfn->name1[j] != 0xFFFF) utf16[pos++] = lfn->name1[j];
        }
        for (int j = 0; j < 6; j++) {
            if (lfn->name2[j] != 0xFFFF) utf16[pos++] = lfn->name2[j];
        }
        for (int j = 0; j < 2; j++) {
            if (lfn->name3[j] != 0xFFFF) utf16[pos++] = lfn->name3[j];
        }
    }

    char *name = (char*)malloc(pos + 1);
    if (!name) return NULL;
    for (size_t i = 0; i < pos; i++) {
        name[i] = (char)(utf16[i] & 0x7F);
    }
    name[pos] = '\0';
    return name;
}