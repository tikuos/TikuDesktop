/* tiku_ide_project.h -- the project description TikuBasic and TikuC
 * read: its title, target and rows, parsed without windows or devices.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_IDE_PROJECT_H_
#define TIKU_IDE_PROJECT_H_

#include <stddef.h>

/** The most group and file rows a project holds. */
#define IDE_ROWS 64

/** @brief One project entry: a file, or a group's name. */
typedef struct {
    char name[64];              /**< the file as the description names
                                     it, or the group's name */
    char path[512];             /**< empty for a heading */
    int  heading;               /**< nonzero for a group's name */
} prow_t;

/** @brief A project description, as tiku_ide_project_parse() reads it. */
typedef struct {
    char  title[64];            /**< from the `title` line */
    char  dir[512];             /**< the directory file paths are from */
    char  target[32];           /**< from the `target` line: what Build
                                     builds for, empty for none */
    char  board[64];            /**< from the `board` line; it does not
                                     choose where a program runs */
    int   nrow;                 /**< rows in row[] */
    int   unknown;              /**< lines skipped: unknown kinds, and
                                     rows past IDE_ROWS */
    prow_t row[IDE_ROWS];       /**< groups and files, in file order */
} project_t;

#pragma GCC visibility push(hidden)
/**
 * @brief Read a project description into @p pj.
 *
 * Reads `title`, `target`, `board`, `group` and `file` lines; blank lines
 * and `#` remarks are passed over.  A line of any other kind, and a group
 * or file past IDE_ROWS, is skipped and counted in pj->unknown.  File
 * paths are made from @p dir, or "." when it is NULL.
 *
 * @return the number of rows read.
 */
int tiku_ide_project_parse(project_t *pj, const char *text, const char *dir);
/** @brief Whether the line at @p line is a `file` entry; its name, read
 *         as tiku_ide_project_parse() reads it, goes into @p name. */
int tiku_ide_project_file_entry(const char *line, char *name, size_t max);
#pragma GCC visibility pop

#endif /* TIKU_IDE_PROJECT_H_ */
