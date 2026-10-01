/* tiku_ide_project.c -- project-description parsing without window state.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_ide_project.h"
#include <stdio.h>
#include <string.h>

static const char *
tail(const char *p, char *out, size_t max);
static const char *
line_end(const char *p);

/** @brief Take the next blank-separated word of @p p into @p out. */
static const char *
word(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n' && *p != ' ' && *p != '\t') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return p;
}

/** @brief The rest of the line, blanks trimmed off both ends. */
static const char *
tail(const char *p, char *out, size_t max)
{
    size_t n = 0u;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p != '\0' && *p != '\n') {
        if (n + 1u < max) {
            out[n++] = *p;
        }
        p++;
    }
    while (n > 0u && (out[n - 1u] == ' ' || out[n - 1u] == '\t' ||
                      out[n - 1u] == '\r')) {
        n--;
    }
    out[n] = '\0';
    return p;
}

static const char *
line_end(const char *p)
{
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    return (*p == '\n') ? p + 1 : p;
}

/** @brief Whether the line at @p line is a `file` entry; its name, read
 *         as tiku_ide_project_parse() reads it, goes into @p name. */
int
tiku_ide_project_file_entry(const char *line, char *name, size_t max)
{
    char kind[16];
    const char *rest = word(line, kind, sizeof kind);

    if (strcmp(kind, "file") != 0) {
        return 0;
    }
    (void)tail(rest, name, max);
    return 1;
}

/**
 * @brief Read a project description into @p pj.
 *
 * A line this build does not know is skipped and counted rather than
 * refusing the project: the lines a BUILD will need were written before
 * anything here could act on them, and a project that opens showing
 * what it can is worth more than one that refuses to open at all.
 *
 * @return the number of rows read.
 */
int
tiku_ide_project_parse(project_t *pj, const char *text, const char *dir)
{
    const char *p = text;

    memset(pj, 0, sizeof *pj);
    snprintf(pj->dir, sizeof pj->dir, "%s", (dir != NULL) ? dir : ".");
    if (text == NULL) {
        return 0;
    }
    while (*p != '\0') {
        char kind[16];
        const char *rest = word(p, kind, sizeof kind);
        prow_t *row;

        if (kind[0] == '\0' || kind[0] == '#') {
            p = line_end(p);
            continue;               /* a blank line, or a remark */
        }
        if (strcmp(kind, "title") == 0) {
            (void)tail(rest, pj->title, sizeof pj->title);
            p = line_end(p);
            continue;
        }
                if (strcmp(kind, "board") == 0) {
            /* WHICH board this project's programs go to, remembered
             * across openings the way the target is. */
            (void)word(rest, pj->board, sizeof pj->board);
            p = line_end(p);
            continue;
        }
        if (strcmp(kind, "target") == 0) {
            /* What the program is FOR.  It was an unknown line until
             * something could act on it, which is exactly what the
             * skipping rule is for. */
            (void)word(rest, pj->target, sizeof pj->target);
            p = line_end(p);
            continue;
        }
        if (pj->nrow >= IDE_ROWS) {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        row = &pj->row[pj->nrow];
        memset(row, 0, sizeof *row);
        if (strcmp(kind, "group") == 0) {
            row->heading = 1;
            (void)tail(rest, row->name, sizeof row->name);
        } else if (strcmp(kind, "file") == 0) {
            char at[sizeof row->path];

            /* The whole rest of the line, so a name may hold a blank. */
            (void)tail(rest, row->name, sizeof row->name);
            /* Paths are from the project's own directory, so a project
             * is a FOLDER somebody can move.  Built aside and copied,
             * because the two halves live in the same object. */
            snprintf(at, sizeof at, "%s/%s", pj->dir, row->name);
            snprintf(row->path, sizeof row->path, "%s", at);
        } else {
            pj->unknown++;
            p = line_end(p);
            continue;
        }
        pj->nrow++;
        p = line_end(p);
    }
    return pj->nrow;
}
