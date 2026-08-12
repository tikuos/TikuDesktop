/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_frame.c - reading and writing window frames and the session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_frame.h"

#include <stdio.h>
#include <string.h>

int
tiku_trk_frame_load(tiku_trk_store_t *s, const char *path,
                    tiku_trk_frame_t *out)
{
    tiku_trk_frame_t f;

    if (s == NULL || path == NULL || out == NULL) {
        return 0;
    }
    if (tiku_trk_state_read(s, path, TIKU_TRK_ATTR_FRAME, &f, sizeof f) !=
            (int)sizeof f) {
        return 0;
    }
    if (f.w < TIKU_TRK_FRAME_MIN_W || f.h < TIKU_TRK_FRAME_MIN_H) {
        /* A record too small to be a window is not one: rather than open a
         * sliver nobody can use, the caller is told there was nothing. */
        return 0;
    }
    *out = f;
    return 1;
}

int
tiku_trk_frame_save(tiku_trk_store_t *s, const char *path,
                    const tiku_trk_frame_t *now, const tiku_trk_frame_t *was)
{
    if (s == NULL || path == NULL || now == NULL) {
        return 0;
    }
    if (was != NULL && was->x == now->x && was->y == now->y &&
        was->w == now->w && was->h == now->h) {
        /* Nothing moved.  A window that opened where it was told and was
         * never touched has nothing to say, and writing it anyway is a
         * store write per window per session (CW-002). */
        return 0;
    }
    return (tiku_trk_state_write(s, path, TIKU_TRK_ATTR_FRAME, now,
                                 sizeof *now) == 0) ? 1 : 0;
}

void
tiku_trk_frame_clamp(tiku_trk_frame_t *f, int sw, int sh)
{
    if (f == NULL || sw <= 0 || sh <= 0) {
        return;
    }
    if (f->w > sw) { f->w = (int32_t)sw; }
    if (f->h > sh) { f->h = (int32_t)sh; }
    if (f->w < TIKU_TRK_FRAME_MIN_W) { f->w = TIKU_TRK_FRAME_MIN_W; }
    if (f->h < TIKU_TRK_FRAME_MIN_H) { f->h = TIKU_TRK_FRAME_MIN_H; }
    /*
     * Enough of the window has to be GRABBABLE, which is a weaker rule than
     * "all of it fits": a window may hang off the bottom, and pulling every
     * such window up would move windows the user deliberately put there.
     * What must be on screen is the tab -- its top edge and a hand's width
     * of its left edge.
     */
    if (f->y < 0) { f->y = 0; }
    if (f->y > sh - TIKU_TRK_FRAME_TAB) {
        f->y = (int32_t)(sh - TIKU_TRK_FRAME_TAB);
    }
    if (f->x + TIKU_TRK_FRAME_GRAB > sw) {
        f->x = (int32_t)(sw - TIKU_TRK_FRAME_GRAB);
    }
    if (f->x + f->w < TIKU_TRK_FRAME_GRAB) {
        f->x = (int32_t)(TIKU_TRK_FRAME_GRAB - f->w);
    }
}

int
tiku_trk_frame_default(tiku_trk_store_t *s, tiku_trk_frame_t *out)
{
    tiku_trk_frame_t f;

    if (s == NULL || out == NULL) {
        return 0;
    }
    if (tiku_trk_state_read(s, TIKU_TRK_KEY_SESSION, TIKU_TRK_ATTR_DEFAULTS,
                            &f, sizeof f) != (int)sizeof f) {
        return 0;
    }
    if (f.w < TIKU_TRK_FRAME_MIN_W || f.h < TIKU_TRK_FRAME_MIN_H) {
        return 0;
    }
    *out = f;
    return 1;
}

void
tiku_trk_frame_set_default(tiku_trk_store_t *s, const tiku_trk_frame_t *f)
{
    if (s == NULL || f == NULL) {
        return;
    }
    (void)tiku_trk_state_write(s, TIKU_TRK_KEY_SESSION,
                               TIKU_TRK_ATTR_DEFAULTS, f, sizeof *f);
}

/** @brief The folder @p path is in, or "" for the root. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash;

    out[0] = '\0';
    if (path == NULL || strcmp(path, "/") == 0) {
        return;                 /* the root has no parent */
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return;
    }
    if (slash == path) {
        snprintf(out, max, "/");
        return;
    }
    {
        size_t n = (size_t)(slash - path);

        if (n >= max) {
            n = max - 1u;
        }
        memcpy(out, path, n);
        out[n] = '\0';
    }
}

int
tiku_trk_frame_inherit(tiku_trk_store_t *s, const char *path,
                       tiku_trk_frame_t *out)
{
    char parent[TIKU_TRK_PATH_MAX];

    if (s == NULL || path == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(path, "/") == 0) {
        /* The root inherits nothing and takes no default: it is the one
         * window whose place is its own, and a template applied to it
         * would move the thing every other window is measured against
         * (CW-069). */
        return 0;
    }
    parent_of(path, parent, sizeof parent);
    if (parent[0] != '\0' && tiku_trk_frame_load(s, parent, out)) {
        /* Offset, so a first opening does not land exactly on the window it
         * came from and look like nothing happened (CW-005). */
        out->x += 22;
        out->y += 22;
        return 1;
    }
    return tiku_trk_frame_default(s, out);
}

/*---------------------------------------------------------------------------*/
/* The session                                                               */
/*---------------------------------------------------------------------------*/

int
tiku_trk_session_save(tiku_trk_store_t *s,
                      const char paths[][TIKU_TRK_PATH_MAX], int n)
{
    char buf[TIKU_TRK_PATH_MAX * TIKU_TRK_SESSION_MAX];
    size_t at = 0u;
    int i;

    if (s == NULL) {
        return 0;
    }
    if (paths == NULL || n <= 0) {
        tiku_trk_session_clear(s);
        return 0;
    }
    for (i = 0; i < n && i < TIKU_TRK_SESSION_MAX && at + 1u < sizeof buf;
         i++) {
        int w = snprintf(buf + at, sizeof buf - at, "%.*s\n",
                         (int)(TIKU_TRK_PATH_MAX - 1), paths[i]);

        if (w <= 0 || (size_t)w >= sizeof buf - at) {
            break;
        }
        at += (size_t)w;
    }
    return (tiku_trk_state_write(s, TIKU_TRK_KEY_SESSION,
                                 TIKU_TRK_ATTR_SESSION, buf, at) == 0)
               ? i : 0;
}

int
tiku_trk_session_load(tiku_trk_store_t *s, char out[][TIKU_TRK_PATH_MAX],
                      int max)
{
    char buf[TIKU_TRK_PATH_MAX * TIKU_TRK_SESSION_MAX];
    int n = 0, len;
    char *line, *save;

    if (s == NULL || out == NULL || max <= 0) {
        return 0;
    }
    len = tiku_trk_state_read(s, TIKU_TRK_KEY_SESSION, TIKU_TRK_ATTR_SESSION,
                              buf, sizeof buf - 1u);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '\0') {
            snprintf(out[n++], TIKU_TRK_PATH_MAX, "%.*s",
                     (int)(TIKU_TRK_PATH_MAX - 1), line);
        }
    }
    return n;
}

void
tiku_trk_session_clear(tiku_trk_store_t *s)
{
    if (s != NULL) {
        (void)tiku_trk_state_write(s, TIKU_TRK_KEY_SESSION,
                                   TIKU_TRK_ATTR_SESSION, "", 0);
    }
}
