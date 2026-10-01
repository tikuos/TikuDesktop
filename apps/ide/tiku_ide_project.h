/* TikuBasic/TikuC project descriptions, independent of windows and devices.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IDE_PROJECT_H_
#define TIKU_IDE_PROJECT_H_

#include <stddef.h>

#define IDE_ROWS 64

/** @brief One project entry: a file, or a group's name. */
typedef struct {
    char name[64];
    char path[512];             /* empty for a heading */
    int  heading;
} prow_t;

typedef struct {
    char  title[64];
    char  dir[512];             /* what the file's own paths are from */
    char  target[32];           /* what the program is being built FOR */
    char  board[64];            /* legacy project field; not a run target */
    int   nrow;
    int   unknown;              /* lines this build did not know */
    prow_t row[IDE_ROWS];
} project_t;

#pragma GCC visibility push(hidden)
int tiku_ide_project_parse(project_t *pj, const char *text, const char *dir);
int tiku_ide_project_file_entry(const char *line, char *name, size_t max);
#pragma GCC visibility pop

#endif /* TIKU_IDE_PROJECT_H_ */
