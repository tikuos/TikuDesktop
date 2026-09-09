/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_drivers.c - what an outside driver may do to this shell:
 * the conductor channel, and the redraw check the script adapter asks for
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"
#include "tiku_trail.h"
#include "tiku_watch.h"

/** @brief Append one menu's rows to @p out, the way a driver reads them. */
static void
conduct_append_menu(char *out, size_t max, const tiku_menu_t *menu)
{
    int i;

    if (menu == NULL) {
        return;
    }
    for (i = 0; i < menu->count; i++) {
        size_t used = strlen(out);

        if (used + 2u >= max) {
            return;
        }
        snprintf(out + used, max - used, "%s|", menu->item[i].label);
    }
}

/** @brief The labels of whatever menu is open, or the Deskbar leaf's. */
static const char *
conduct_menu_labels(desktop_t *app)
{
    static char labels[512];

    labels[0] = '\0';
    conduct_append_menu(labels, sizeof labels, app->popup);
    conduct_append_menu(labels, sizeof labels, app->popup_sub);
    if (labels[0] == '\0') {
        /* The Deskbar's leaf is a menu too: without it a driver could
         * only reach those rows by pixel, and they move with whatever
         * was recently opened. */
        size_t used;

        (void)tiku_trk_deskbar_menu_labels(app->deskbar, 0, labels,
                                           sizeof labels);
        used = strlen(labels);
        (void)tiku_trk_deskbar_menu_labels(app->deskbar, 1, labels + used,
                                           sizeof labels - used);
    }
    return labels;
}

/** @brief Queue an injected event for the loop that drains the display's. */
void
conduct_inject(void *context, const tiku_event_t *event)
{
    desktop_t *app = context;
    int next = (app->cq_tail + 1) % (int)(sizeof app->cq / sizeof app->cq[0]);

    /* A full queue drops the newest rather than overwriting the oldest:
     * a driver that outruns the shell is told by what it observes next,
     * and an out-of-order gesture would be a worse lie than a missing
     * one. */
    if (next == app->cq_head) {
        return;
    }
    app->cq[app->cq_tail] = *event;
    app->cq_tail = next;
}

/**
 * @brief The pixel a driver asks about, off the same surface the display
 *        is shown.
 *
 * The answer is about the LAST PAINTED frame, and the loop paints every
 * turn it owes one -- so a driver reads what a person would see one turn
 * after acting, which is what the grammar's `wait` is for.  Repainting
 * here instead was tried and removed: nothing could be made to fail with
 * it gone, and an answer that quietly paints is an observation that
 * changes what it observes.
 */
unsigned
conduct_pixel(void *context, int x, int y)
{
    desktop_t *app = context;

    if (app->surf == NULL || x < 0 || y < 0 ||
        x >= app->surf->w || y >= app->surf->h) {
        return 0u;
    }
    return app->surf->px[(size_t)y * (size_t)app->surf->w + (size_t)x] &
           0xffffffu;
}

/**
 * @brief The named things this shell can say about itself.
 *
 * Deliberately a short list of what the shell ALREADY knows -- a title, a
 * count, the labels of the menu that is open.  Everything a driver wants
 * to assert is composed from these on its side; a name this build does
 * not know is refused, so a driver is told rather than guessing.
 */
int
conduct_text(void *context, const char *name, char *out, size_t max)
{
    desktop_t *app = context;

    if (name == NULL || out == NULL) {
        return 0;
    }
            if (strncmp(name, "trail", 5) == 0 &&
        (name[5] == '\0' || name[5] == ':')) {
        /* What this shell has been doing, newest lines last.  A noun
         * rather than a verb of its own: it is one more thing the shell
         * already knows, and a driver reads it as it reads a title.
         * "trail:<word>" is the part of it about that word, because the
         * ring is longer than any one reader's room. */
        (void)tiku_trail_find((name[5] == ':') ? name + 6 : NULL, out, max);
        return 1;
    }
        if (strcmp(name, "turns") == 0) {
        /* The longest turn since last asked, in ms: the number every
         * "the loop never waits" proof reads, and reset by the reading
         * so a script measures the stretch it arranged. */
        snprintf(out, max, "%ld", tiku_watch_longest_ms());
        tiku_watch_longest_reset();
        return 1;
    }
    if (strcmp(name, "title") == 0) {
        tiku_window_t *w = tiku_workspace_focused(app->desk);

        snprintf(out, max, "%s", (w != NULL) ? w->title : "");
        return 1;
    }
    if (strcmp(name, "titles") == 0) {
        /* Every open window, so a driver can assert that one is GONE --
         * which no single focused title can answer. */
        int i;
        size_t used = 0u;

        out[0] = '\0';
        for (i = 0; i < TIKU_WINDOW_MAX; i++) {
            tiku_window_t *w = tiku_workspace_at(app->desk, i);

            if (w == NULL || !w->open) {
                continue;
            }
            used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                     "%s|", w->title);
            if (used >= max) {
                break;
            }
        }
        return 1;
    }
    if (strcmp(name, "status") == 0) {
        snprintf(out, max, "%s", app->status);
        return 1;
    }
    if (strcmp(name, "count") == 0) {
        snprintf(out, max, "%s", app->count_text);
        return 1;
    }
    if (strcmp(name, "menu") == 0) {
        /* Whatever menu is open, as its labels: the popup the shell is
         * showing, or the global bar's dropped one. */
        snprintf(out, max, "%s", conduct_menu_labels(app));
        return 1;
    }
    if (strcmp(name, "alert") == 0) {
        /* The open question's text, or nothing: an agent must never
         * learn about a dialog by staring at pixels -- and as ~alert
         * this is "an alert appeared" pushed, for free. */
        tiku_alert_t *alert = alert_model(app);

        snprintf(out, max, "%s", alert->open ? alert->text : "");
        return 1;
    }
    if (strcmp(name, "sounds") == 0) {
        size_t used = 0;
        int i;

        out[0] = '\0';
        /* name:count:on|off for every moment, then the volume: the
         * count is the proof a moment happened, the flag the proof of
         * a preference, and neither depends on a card. */
        for (i = 0; i < TIKU_SOUND_EVENTS && used < max; i++) {
            used += (size_t)snprintf(out + used, max - used, "%s%s:%u:%s",
                                     i ? " " : "",
                                     tiku_sound_name((tiku_sound_event_t)i),
                                     tiku_sound_count((tiku_sound_event_t)i),
                                     tiku_sound_enabled((tiku_sound_event_t)i)
                                         ? "on" : "off");
        }
        if (used < max) {
            snprintf(out + used, max - used, " volume:%d",
                     tiku_sound_volume());
        }
        return 1;
    }
    if (strcmp(name, "mounts") == 0) {
        /* Every mounted volume by name: as ~mounts, "a mount arrived"
         * is a push rather than a poll. */
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
    if (strcmp(name, "controls") == 0) {
        /* The focused window as machine lines -- kind, label, rect,
         * state: the walkable form of the feed, for the party that
         * wants to click the button it heard about. */
        (void)tiku_workspace_controls(app->desk,
                                           tiku_workspace_focused(app->desk),
                                           out, max);
        return 1;
    }
    if (strcmp(name, "operation") == 0) {
        /* The running job's own lifecycle: "idle", or each job's
         * caption, current leaf and count.  As ~operation, "the copy
         * finished" is a push -- the fact, not the window about it. */
        (void)tiku_trk_progress_say(progress_model(app), out, max);
        return 1;
    }
    if (name[0] == '/') {
        /* The namespace itself is a noun table: any path the shell's
         * mount table serves, answered as a node's value or a
         * container's children -- and through /devices/... that is a
         * BOARD's file or sensor, which is the device idiom riding
         * this channel.  ~/path watches it like any other fact. */
        return tiku_backend_text(app->backend, name, out, max);
    }
    if (strcmp(name, "narrate") == 0) {
        /* The focused window, said from its own display list -- the
         * reader's door of AGENT-SURFACE stage 4: a driver that has
         * never seen a pixel of this window hears what it shows.  No
         * focused window is an empty answer, not a refusal, the same
         * shape "title" gives. */
        (void)tiku_workspace_narrate(app->desk,
                                          tiku_workspace_focused(app->desk),
                                          out, max);
        return 1;
    }
    return 0;                   /* a name this build cannot say */
}

/**
 * @brief Redraw the desktop from nothing and count the pixels that came
 *        out different from the frame on screen.  @return that count,
 *        -1 with no surface or no memory for the copy.
 */
long
script_redraw_diff(void *ctx)
{
    desktop_t *app = ctx;
    tiku_rgb_t *was;
    size_t n, i;
    long differ = 0;

    if (app == NULL || app->surf == NULL || app->surf->px == NULL) {
        return -1;
    }
    n = (size_t)app->surf->w * (size_t)app->surf->h;
    if (app->surf->scale > 1) {
        n *= (size_t)app->surf->scale * (size_t)app->surf->scale;
    }
    was = malloc(n * sizeof *was);
    if (was == NULL) {
        return -1;
    }
    memcpy(was, app->surf->px, n * sizeof *was);
    for (i = 0u; i < (size_t)app->folder_count; i++) {
        tiku_trk_folder_window_forget(&app->folders[i]);
    }
    draw_all(app);
    for (i = 0u; i < n; i++) {
        if (was[i] != app->surf->px[i]) {
            differ++;
        }
    }
    free(was);
    return differ;
}
