/* Binary editing without changing file length. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HEX_MODEL_H_
#define TIKU_HEX_MODEL_H_
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#define HEX_LIMIT (16u * 1024u * 1024u)
#define HEX_HISTORY 512
typedef struct { size_t at; unsigned char before, after; } hex_change_t;
typedef struct {
    unsigned char *bytes, *original;
    size_t size, changed, cursor;
    hex_change_t history[HEX_HISTORY];
    int used, position;
    char path[512], error[192];
    struct stat identity;
} hex_model_t;
void hex_free(hex_model_t *m);
int hex_load(hex_model_t *m, const char *path);
int hex_save(hex_model_t *m, const char *path);
int hex_replace(hex_model_t *m, size_t at, unsigned char byte);
int hex_undo(hex_model_t *m, int redo);
int hex_offset(const char *text, size_t size, size_t *offset);
int hex_pattern(const char *text, unsigned char *bytes, size_t *size);
int hex_find(hex_model_t *m, const unsigned char *pattern, size_t size);
#endif
