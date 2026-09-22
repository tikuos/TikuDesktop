/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_script.c - what a script may ask of the window
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/**
 * @brief The character a key event types, or 0.
 *
 * With no display there is no keyboard map to consult, so a Latin-1 keysym
 * stands for itself -- which is all a script needs in order to type.
 */
char
browser_key_char(const tiku_event_t *event)
{
    return (event != NULL) ? event->text[0] : (char)0;
}

/** @brief Row under a point, or -1 for empty space / the title bar. */
int
browser_row_at(app_t *a, int px, int py)
{
    tiku_rect_t list = browser_list_rect(a);
    int rows, idx;

    if (py < list.y + 2 || py > list.y + list.h ||
        px < list.x || px > list.x + list.w) {
        return -1;
    }
    rows = (py - list.y - 2) / a->row_h;
    idx = tiku_trk_poseview_scroll(a->view) + rows;
    return (idx < tiku_trk_poseview_count(a->view)) ? idx : -1;
}

/** @brief Hit-test the count strip, kept separate from list rows (TS-043). */
int
browser_count_at(app_t *a, int px, int py)
{
    tiku_rect_t list = browser_list_rect(a);

    return a != NULL && px >= list.x && px <= list.x + list.w &&
           py >= list.y + list.h && py <= list.y + list.h + COUNT_H;
}

/** @brief Resolve a visible list row to a point the script can click. */
int
browser_script_row_point(void *ctx, int row, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    tiku_rect_t list;
    int rel;

    if (a == NULL || x == NULL || y == NULL || row < 0 ||
        tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    rel = row - tiku_trk_poseview_scroll(a->view);
    if (rel < 0 || rel >= browser_visible_rows(a) ||
        row >= tiku_trk_poseview_count(a->view)) {
        return 0;
    }
    list = browser_list_rect(a);
    *x = list.x + 20;
    *y = list.y + 2 + rel * a->row_h + a->row_h / 2;
    return 1;
}

/** @brief Resolve a named icon to its view-space location (drop space). */
int
browser_script_pose_at(void *ctx, const char *name, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    int i, n;

    if (a == NULL || name == NULL || x == NULL || y == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(a->view);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

        if (p != NULL && p->placed &&
            strcmp(p->model.name, name) == 0) {
            tiku_trk_poseview_pose_location(a->view, p, x, y);
            return 1;
        }
    }
    return 0;
}

/** @brief One pixel of the window, for a check that asks about ink. */
unsigned
browser_script_pixel_at(void *ctx, int x, int y)
{
    app_t *a = (app_t *)ctx;

    if (a->surf == NULL || a->surf->px == NULL || x < 0 || y < 0 ||
        x >= a->surf->w || y >= a->surf->h) {
        return 0u;
    }
    return (unsigned)tiku_peek(a->surf, x, y) & 0xffffffu;
}

/** @brief The window said from its display list, for expect-narrate.
 *
 * This front-end paints its whole window every frame with no kept
 * pixels, so the same draw() the loop calls is aimed once at a recording
 * scratch surface -- the shown pixels are never touched, and nothing is
 * drained or signed that a real frame would miss. */
static size_t
script_recite(void *ctx, char *out, size_t max, int as_facts)
{
    app_t *a = (app_t *)ctx;
    tiku_surface_t *shown = a->surf;
    tiku_surface_t *scratch;
    tiku_dl_t *dl;
    size_t used;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (shown == NULL) {
        return 0u;
    }
    scratch = tiku_surface_new(shown->w, shown->h, TIKU_C_PANEL);
    if (scratch == NULL) {
        return 0u;
    }
    dl = tiku_dl_new();
    if (dl == NULL) {
        tiku_surface_free(scratch);
        return 0u;
    }
    scratch->record = dl;
    a->surf = scratch;
    browser_draw(a);
    a->surf = shown;
    scratch->record = NULL;
    used = as_facts ? tiku_dl_facts(dl, out, max)
                    : tiku_dl_say(dl, out, max);
    tiku_dl_free(dl);
    tiku_surface_free(scratch);
    return used;
}

/** @brief The window as prose, for a script's narration checks. */
size_t
browser_script_narrate(void *ctx, char *out, size_t max)
{
    return script_recite(ctx, out, max, 0);
}

/** @brief The nouns this front-end can say, spelled as the desktop's
 *         conductor spells them, so a shared script means one thing. */
int
browser_script_say(void *ctx, const char *noun, char *out, size_t max)
{
    app_t *a = (app_t *)ctx;

    if (noun == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(noun, "sounds") == 0) {
        size_t used = 0;
        int i;

        out[0] = '\0';
        for (i = 0; i < TIKU_SOUND_EVENTS && used < max; i++) {
            used += (size_t)snprintf(out + used, max - used, "%s%s:%u",
                                     i ? " " : "",
                                     tiku_sound_name((tiku_sound_event_t)i),
                                     tiku_sound_count((tiku_sound_event_t)i));
        }
        return 1;
    }
    if (strcmp(noun, "mounts") == 0) {
        tiku_volumes_t vs;
        size_t used = 0u;
        int i;

        out[0] = '\0';
        (void)tiku_volumes_scan(&vs);
        for (i = 0; i < vs.n; i++) {
            used += (size_t)snprintf(out + used,
                                     (used < max) ? max - used : 0u,
                                     "%s|", vs.v[i].name);
            if (used >= max) {
                break;
            }
        }
        return 1;
    }
    if (strcmp(noun, "controls") == 0) {
        (void)script_recite(a, out, max, 1);
        return 1;
    }
    if (strcmp(noun, "hot") == 0) {
        /* The open menu's row under the pointer, by label; empty for
         * none, so a pointer that left is provable. */
        const tiku_menu_t *m = (a->popup_sub != NULL && a->popup_sub->hot >= 0)
                                   ? a->popup_sub : a->popup;

        out[0] = '\0';
        if (m != NULL && m->hot >= 0 && m->hot < m->count) {
            snprintf(out, max, "%s", m->item[m->hot].label);
        }
        return 1;
    }
    if (strcmp(noun, "operation") == 0) {
        /* This front-end keeps no progress window; the engine itself is
         * asked, so the noun cannot say idle while a job runs. */
        if (a->fs != NULL && tiku_fs_busy(a->fs)) {
            const tiku_progress_t *pr = tiku_fs_progress(a->fs);
            const char *slash = strrchr(pr->current, '/');

            snprintf(out, max, "%s%s, %d of %d|",
                     tiku_trk_progress_caption(pr->op),
                     (slash != NULL) ? slash + 1 : pr->current,
                     pr->done + pr->skipped, pr->total);
        } else {
            snprintf(out, max, "idle");
        }
        return 1;
    }
    if (noun[0] == '/') {
        return tiku_backend_text(a->backend, noun, out, max);
    }
    if (strcmp(noun, "alert") == 0) {
        tiku_alert_t *alert =
            tiku_trk_status_overlay_alert(&a->status_overlay);

        snprintf(out, max, "%s", alert->open ? alert->text : "");
        return 1;
    }
    return 0;                   /* a noun this front-end cannot say */
}

/** @brief Resolve a named icon to the screen point it is drawn at. */
int
browser_script_pose_screen(void *ctx, const char *name, int *x, int *y)
{
    app_t *a = (app_t *)ctx;
    tiku_rect_t lr;

    if (!browser_script_pose_at(ctx, name, x, y)) {
        return 0;
    }
    /* Converted the way draw_icons converts on the way out, so a test can
     * name a screen point instead of the chrome. */
    lr = browser_list_rect(a);
    *x += lr.x;
    *y += lr.y;
    return 1;
}
