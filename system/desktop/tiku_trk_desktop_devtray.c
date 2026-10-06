/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_devtray.c - the device tray: a cell on the global bar,
 * left of the camera, for each board being connected, connected, lent to
 * another program or just lost; the tip that names it, its menu, and the
 * double-click that opens its folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <strings.h>
#include "tiku_trk_desktop_private.h"
#include "tiku_ui.h"

/* A cell's width, and the least room it leaves after the last title. */
#define TRAY_CELL 24
#define TRAY_GAP 8
/* The radius of the ring around a glyph, and the width of its line. */
#define TRAY_RING 8.0f
#define TRAY_LINE 1.2f
/* In microseconds: how long a cell stays red, how long the ring takes to
 * close once a board lands, how long a connect runs before it counts as
 * failed, one turn of the ring, and the glyph's fade in. */
#define TRAY_RED_US 2000000
#define TRAY_LANDING_US 450000
#define TRAY_CONNECT_US 90000000
#define TRAY_TURN_US 1000000
#define TRAY_FADE_US 300000

/** @brief @p a moved @p t of the way to @p b, @p t held to 0..1. */
static tiku_rgb_t
mix(tiku_rgb_t a, tiku_rgb_t b, float t)
{
    int ar = (int)((a >> 16) & 0xffu), ag = (int)((a >> 8) & 0xffu);
    int ab = (int)(a & 0xffu), br = (int)((b >> 16) & 0xffu);
    int bg = (int)((b >> 8) & 0xffu), bb = (int)(b & 0xffu);

    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    return TIKU_RGB((int)((float)ar + (float)(br - ar) * t + 0.5f),
                    (int)((float)ag + (float)(bg - ag) * t + 0.5f),
                    (int)((float)ab + (float)(bb - ab) * t + 0.5f));
}

/** @brief The cell on @p port, or NULL. */
static struct desktop_devtray *
cell_on_port(desktop_t *app, const char *port)
{
    int i;

    for (i = 0; port != NULL && port[0] != '\0' && i < TIKU_MOUNT_MAX; i++) {
        if (app->devtray[i].state != DESKTOP_DEVTRAY_NONE &&
            strcmp(app->devtray[i].port, port) == 0) {
            return &app->devtray[i];
        }
    }
    return NULL;
}

/** @brief The cell of board @p name, or NULL. */
static struct desktop_devtray *
cell_named(desktop_t *app, const char *name)
{
    int i;

    for (i = 0; name != NULL && name[0] != '\0' && i < TIKU_MOUNT_MAX; i++) {
        if (app->devtray[i].state != DESKTOP_DEVTRAY_NONE &&
            strcmp(app->devtray[i].name, name) == 0) {
            return &app->devtray[i];
        }
    }
    return NULL;
}

/** @brief Close the gaps freed cells left, keeping the order; the boxes
 *         are cleared until the next draw places the cells again. */
static void
tray_compact(desktop_t *app)
{
    int i, n = 0;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->devtray[i].state != DESKTOP_DEVTRAY_NONE) {
            if (i != n) {
                app->devtray[n] = app->devtray[i];
            }
            n++;
        }
    }
    for (; n < TIKU_MOUNT_MAX; n++) {
        memset(&app->devtray[n], 0, sizeof app->devtray[n]);
    }
    memset(app->devtray_box, 0, sizeof app->devtray_box);
}

/**
 * @brief A cleared cell after the others; with every cell taken, the
 *        first red one that is going is given up for it.
 * @return the cell, or NULL when none is free.
 */
static struct desktop_devtray *
cell_new(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->devtray[i].state == DESKTOP_DEVTRAY_GONE) {
            break;
        }
    }
    if (app->devtray[TIKU_MOUNT_MAX - 1].state != DESKTOP_DEVTRAY_NONE &&
        i < TIKU_MOUNT_MAX) {
        app->devtray[i].state = DESKTOP_DEVTRAY_NONE;
        tray_compact(app);
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->devtray[i].state == DESKTOP_DEVTRAY_NONE) {
            memset(&app->devtray[i], 0, sizeof app->devtray[i]);
            return &app->devtray[i];
        }
    }
    return NULL;
}

/** @brief The bar and the band under it where a cell's tip shows. */
static tiku_rect_t
tray_band(desktop_t *app)
{
    int w = (app->surf != NULL) ? app->surf->w : 0;

    return (tiku_rect_t){ 0, 0, w, app->menus.bar.height +
                                   tiku_ui_tip_size("Mg").h + 8 };
}

/** @brief Whether board @p name is mounted under /devices. */
static int
board_mounted(desktop_t *app, const char *name)
{
    int i, slots = tiku_mount_slots(app->backend);

    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);

        if (prefix != NULL && tiku_mount_at(app->backend, i) != NULL &&
            strncmp(prefix, "/devices/", 9) == 0 &&
            strcmp(prefix + 9, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether board @p name is lent: released for another program. */
static int
board_lent(desktop_t *app, const char *name)
{
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        if (app->released[i].used &&
            strcmp(app->released[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Bring the cells into line with the mounts and the lent boards: a
 *        board's cell follows it between connected and lent, a board with
 *        no cell gets one, and a cell whose board has gone is freed.
 * @return 1 when a cell changed.
 */
static int
tray_follow(desktop_t *app, int64_t now)
{
    int i, slots, changed = 0;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        struct desktop_devtray *c = &app->devtray[i];
        int mounted, lent;

        if (c->state != DESKTOP_DEVTRAY_CONNECTED &&
            c->state != DESKTOP_DEVTRAY_LENT) {
            continue;
        }
        mounted = board_mounted(app, c->name);
        lent = board_lent(app, c->name);
        if (c->state == DESKTOP_DEVTRAY_CONNECTED && !mounted) {
            c->state = lent ? DESKTOP_DEVTRAY_LENT : DESKTOP_DEVTRAY_NONE;
            changed = 1;
        } else if (c->state == DESKTOP_DEVTRAY_LENT && mounted) {
            /* A reclaim brought it back. */
            c->state = DESKTOP_DEVTRAY_CONNECTED;
            c->since = now;
            c->red_until = 0;
            tiku_motion_start(&c->landing, now, TRAY_LANDING_US);
            changed = 1;
        } else if (c->state == DESKTOP_DEVTRAY_LENT && !lent) {
            c->state = DESKTOP_DEVTRAY_NONE;
            changed = 1;
        }
    }
    if (changed) {
        tray_compact(app);
    }
    /* A board mounted at startup, dialling in or mounted by a path with no
     * hook here gets its cell now. */
    slots = tiku_mount_slots(app->backend);
    for (i = 0; i < slots; i++) {
        const char *prefix = tiku_mount_prefix_at(app->backend, i);
        struct desktop_devtray *c;

        if (prefix == NULL || tiku_mount_at(app->backend, i) == NULL ||
            strncmp(prefix, "/devices/", 9) != 0 || prefix[9] == '\0' ||
            cell_named(app, prefix + 9) != NULL) {
            continue;
        }
        c = cell_new(app);
        if (c == NULL) {
            break;
        }
        c->state = DESKTOP_DEVTRAY_CONNECTED;
        snprintf(c->name, sizeof c->name, "%s", prefix + 9);
        (void)desktop_board_port(app, c->name, c->port, sizeof c->port);
        c->since = now;
        changed = 1;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        struct desktop_devtray *c;

        if (!app->released[i].used ||
            cell_named(app, app->released[i].name) != NULL) {
            continue;
        }
        c = cell_new(app);
        if (c == NULL) {
            break;
        }
        c->state = DESKTOP_DEVTRAY_LENT;
        snprintf(c->name, sizeof c->name, "%s", app->released[i].name);
        c->since = now;
        changed = 1;
    }
    return changed;
}

/**
 * @brief A connect probe went out on @p port: its cell shows the glyph
 *        fading in inside a turning ring until the board answers.
 */
void
desktop_devtray_connecting(desktop_t *app, const char *port)
{
    struct desktop_devtray *c;

    if (app == NULL || port == NULL || port[0] == '\0') {
        return;
    }
    c = cell_on_port(app, port);
    if (c != NULL && (c->state == DESKTOP_DEVTRAY_CONNECTED ||
                      c->state == DESKTOP_DEVTRAY_LENT)) {
        return;                 /* a board is on the port already */
    }
    if (c == NULL) {
        c = cell_new(app);
    }
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof *c);
    c->state = DESKTOP_DEVTRAY_CONNECTING;
    snprintf(c->port, sizeof c->port, "%s", port);
    c->since = tiku_trk_now();
    desktop_damage(app, tray_band(app));
}

/**
 * @brief The board on @p port is mounted at @p prefix: its cell turns
 *        connected, and the ring closes round the glyph and fades.  A
 *        mount outside /devices is no board, and its cell goes.
 */
void
desktop_devtray_landed(desktop_t *app, const char *port, const char *prefix)
{
    struct desktop_devtray *c;
    const char *leaf;
    int64_t now = tiku_trk_now();

    if (app == NULL || prefix == NULL) {
        return;
    }
    c = cell_on_port(app, port);
    leaf = (strncmp(prefix, "/devices/", 9) == 0 && prefix[9] != '\0')
               ? prefix + 9 : NULL;
    if (leaf == NULL) {
        if (c != NULL) {
            c->state = DESKTOP_DEVTRAY_NONE;
            tray_compact(app);
            desktop_damage(app, tray_band(app));
        }
        return;
    }
    if (c == NULL) {
        c = cell_named(app, leaf);
    }
    if (c == NULL) {
        c = cell_new(app);
    }
    if (c == NULL) {
        return;
    }
    c->state = DESKTOP_DEVTRAY_CONNECTED;
    snprintf(c->name, sizeof c->name, "%s", leaf);
    snprintf(c->port, sizeof c->port, "%s", (port != NULL) ? port : "");
    c->since = now;
    c->red_until = 0;
    c->why[0] = '\0';
    tiku_motion_start(&c->landing, now, TRAY_LANDING_US);
    desktop_damage(app, tray_band(app));
}

/**
 * @brief Something failed for the board on @p port, or board @p name when
 *        @p port is NULL: its cell turns red for two seconds with @p why in
 *        its tip, then goes.  A lent board's cell stays, lent.
 */
void
desktop_devtray_failed(desktop_t *app, const char *port, const char *name,
                       const char *why)
{
    struct desktop_devtray *c;

    if (app == NULL) {
        return;
    }
    c = (port != NULL) ? cell_on_port(app, port) : cell_named(app, name);
    if (c == NULL) {
        c = cell_new(app);
        if (c == NULL) {
            return;
        }
        c->state = DESKTOP_DEVTRAY_GONE;
        snprintf(c->port, sizeof c->port, "%s", (port != NULL) ? port : "");
        snprintf(c->name, sizeof c->name, "%s", (name != NULL) ? name : "");
        c->since = tiku_trk_now();
    } else if (c->state != DESKTOP_DEVTRAY_LENT) {
        c->state = DESKTOP_DEVTRAY_GONE;
    }
    snprintf(c->why, sizeof c->why, "%s", (why != NULL) ? why : "failed");
    c->red_until = tiku_trk_now() + TRAY_RED_US;
    desktop_damage(app, tray_band(app));
}

/** @brief The connect on @p port found a port that carries windows with
 *         no shell: its cell goes, since nothing is mounted. */
void
desktop_devtray_forget(desktop_t *app, const char *port)
{
    struct desktop_devtray *c = (app != NULL) ? cell_on_port(app, port) : NULL;

    if (c != NULL && c->state == DESKTOP_DEVTRAY_CONNECTING) {
        c->state = DESKTOP_DEVTRAY_NONE;
        tray_compact(app);
        desktop_damage(app, tray_band(app));
    }
}

/**
 * @brief Each turn: end the red of cells whose two seconds are up, fail a
 *        connect that has run too long, and follow the mounts and the lent
 *        boards.
 * @return 1 when a cell changed, with the bar and the tip band in
 *         @p damage.
 */
int
desktop_devtray_tick(desktop_t *app, int64_t now, tiku_rect_t *damage)
{
    int i, changed = 0;

    *damage = tiku_trk_damage_none();
    if (app->backend == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        struct desktop_devtray *c = &app->devtray[i];

        if (c->red_until != 0 && now >= c->red_until) {
            c->red_until = 0;
            c->why[0] = '\0';
            if (c->state == DESKTOP_DEVTRAY_GONE) {
                c->state = DESKTOP_DEVTRAY_NONE;
            }
            changed = 1;
        } else if (c->state == DESKTOP_DEVTRAY_CONNECTING &&
                   now - c->since > TRAY_CONNECT_US) {
            c->state = DESKTOP_DEVTRAY_GONE;
            snprintf(c->why, sizeof c->why, "no answer");
            c->red_until = now + TRAY_RED_US;
            changed = 1;
        }
    }
    if (changed) {
        tray_compact(app);
    }
    changed |= tray_follow(app, now);
    if (changed) {
        *damage = tray_band(app);
    }
    return changed;
}

/** @brief The chip at (@p cx, @p cy) in @p ink: a nine-pixel body with a
 *         pin-one mark, and three pins down each side. */
static void
chip(tiku_surface_t *s, int cx, int cy, tiku_rgb_t ink)
{
    int k;

    tiku_frame(s, (tiku_rect_t){ cx - 4, cy - 4, 9, 9 }, ink);
    tiku_fill(s, (tiku_rect_t){ cx - 2, cy - 2, 2, 2 }, ink);
    for (k = -2; k <= 2; k += 2) {
        tiku_hline(s, cx - 6, cy + k, 2, ink);
        tiku_hline(s, cx + 5, cy + k, 2, ink);
        tiku_vline(s, cx + k, cy - 6, 2, ink);
        tiku_vline(s, cx + k, cy + 5, 2, ink);
    }
}

/** @brief The lent badge at the chip's lower right: an arrow out to the
 *         upper right, on a patch of @p ground. */
static void
lent_badge(tiku_surface_t *s, int cx, int cy, tiku_rgb_t ground,
           tiku_rgb_t ink)
{
    int i;

    tiku_fill(s, (tiku_rect_t){ cx + 3, cy + 1, 7, 7 }, ground);
    for (i = 0; i < 4; i++) {
        tiku_fill(s, (tiku_rect_t){ cx + 4 + i, cy + 6 - i, 1, 1 }, ink);
    }
    tiku_hline(s, cx + 5, cy + 3, 3, ink);
    tiku_vline(s, cx + 7, cy + 3, 3, ink);
}

/**
 * @brief The ring round the glyph at (@p cx, @p cy) on @p ground: a faint
 *        track at @p track, an arc in @p ink whose head is at @p head
 *        radians and whose tail fades over @p span radians (none for 0),
 *        and the whole ring in @p ink at @p whole.
 */
static void
ring(tiku_surface_t *s, int cx, int cy, tiku_rgb_t ground, tiku_rgb_t ink,
     float track, float head, float span, float whole)
{
    tiku_rgb_t faint = mix(ground, TIKU_C_TEXT, 0.2f);
    int x, y, reach = (int)TRAY_RING + 2;

    for (y = -reach; y <= reach; y++) {
        for (x = -reach; x <= reach; x++) {
            float d = sqrtf((float)(x * x + y * y));
            float cover = 1.0f - fabsf(d - TRAY_RING) / TRAY_LINE;
            float w = 0.0f;
            tiku_rgb_t c;

            if (cover <= 0.0f) {
                continue;
            }
            if (span > 0.0f) {
                float back = fmodf(head - atan2f((float)y, (float)x),
                                   6.2831853f);

                if (back < 0.0f) {
                    back += 6.2831853f;
                }
                w = (back <= span) ? 1.0f - back / span : 0.0f;
            }
            if (whole > w) {
                w = whole;
            }
            c = mix(ground, faint, cover * track);
            if (w > 0.0f) {
                c = mix(c, ink, cover * w);
            }
            tiku_fill(s, (tiku_rect_t){ cx + x, cy + y, 1, 1 }, c);
        }
    }
}

/**
 * @brief Draw cell @p c in @p box: open as a pill while its menu shows.
 *        @p now is 0 with motion off, which draws every effect at its end.
 * @return 1 while an effect runs and wants the next frame.
 */
static int
cell_draw(desktop_t *app, struct desktop_devtray *c, tiku_rect_t box, int open,
          int64_t now)
{
    tiku_surface_t *s = app->surf;
    tiku_rgb_t ground = TIKU_C_PANEL, ink = TIKU_C_TEXT;
    int cx = box.x + box.w / 2, cy = box.y + box.h / 2;
    int reclaiming = c->state == DESKTOP_DEVTRAY_LENT && app->reclaim.going &&
                     strcmp(app->reclaim.name, c->name) == 0;

    if (open) {
        tiku_fill_round(s, (tiku_rect_t){ box.x, box.y + 1, box.w,
                                          box.h - 2 },
                        3, TIKU_CORNER_ALL, TIKU_C_SELECT);
        ground = TIKU_C_SELECT;
        ink = TIKU_C_SELTEXT;
    }
    if (c->red_until != 0) {
        chip(s, cx, cy, TIKU_C_DANGER);
        if (c->state == DESKTOP_DEVTRAY_LENT) {
            lent_badge(s, cx, cy, ground, TIKU_C_DANGER);
        }
        return 0;
    }
    if (c->state == DESKTOP_DEVTRAY_CONNECTING || reclaiming) {
        float fade = 0.5f;

        if (now != 0) {
            float t = (float)((now - c->since) % TRAY_TURN_US) /
                      (float)TRAY_TURN_US;

            ring(s, cx, cy, ground, TIKU_C_SELECT, 1.0f, t * 6.2831853f,
                 2.4f, 0.0f);
            if (!reclaiming) {
                fade *= tiku_motion_ease_out((float)(now - c->since) /
                                             (float)TRAY_FADE_US);
            }
        } else {
            ring(s, cx, cy, ground, TIKU_C_SELECT, 1.0f, 0.0f, 0.0f, 0.0f);
        }
        chip(s, cx, cy, mix(ground, ink, fade));
        if (reclaiming) {
            lent_badge(s, cx, cy, ground, mix(ground, ink, fade));
        }
        return now != 0;
    }
    if (c->state == DESKTOP_DEVTRAY_LENT) {
        chip(s, cx, cy, tiku_dim(ink, ground));
        lent_badge(s, cx, cy, ground, ink);
        return 0;
    }
    if (c->state == DESKTOP_DEVTRAY_CONNECTED && c->landing.active) {
        float p = tiku_motion_progress(&c->landing, now);

        if (p < 1.0f) {
            ring(s, cx, cy, ground, TIKU_C_SELECT, 1.0f - p, 0.0f, 0.0f,
                 1.0f - p);
            chip(s, cx, cy, mix(tiku_dim(ink, ground), ink, p * 2.0f));
            return 1;
        }
    }
    chip(s, cx, cy, ink);
    return 0;
}

/**
 * @brief Draw the cells right to left from @p right, none starting left of
 *        @p left, and keep where each went for presses.  @p now is 0 with
 *        motion off.
 */
void
desktop_devtray_draw(desktop_t *app, tiku_rect_t strip, int left, int right,
                     int64_t now)
{
    int i, x = right, menu = app->popup == &app->devtraymenu;

    memset(app->devtray_box, 0, sizeof app->devtray_box);
    for (i = 0; i < TIKU_MOUNT_MAX &&
                app->devtray[i].state != DESKTOP_DEVTRAY_NONE; i++) {
        struct desktop_devtray *c = &app->devtray[i];
        tiku_rect_t box = { x - TRAY_CELL, strip.y, TRAY_CELL,
                            strip.h - 1 };
        int open = menu && c->name[0] != '\0' &&
                   strcmp(c->name, app->devtray_menu_of) == 0;

        if (box.x < left + TRAY_GAP) {
            break;
        }
        app->devtray_box[i] = box;
        if (cell_draw(app, c, box, open, now)) {
            desktop_damage(app, box);
        }
        x -= TRAY_CELL;
    }
}

/** @brief The cell drawn under (@p x, @p y), or -1. */
int
desktop_devtray_at(desktop_t *app, int x, int y)
{
    int i;

    for (i = 0; i < TIKU_MOUNT_MAX; i++) {
        tiku_rect_t b = app->devtray_box[i];

        if (b.w > 0 && x >= b.x && x < b.x + b.w && y >= b.y &&
            y < b.y + b.h) {
            return i;
        }
    }
    return -1;
}

/** @brief How long @p us is, for a tip: "just now", "for 5 min" or "for
 *         2 h 10 min". */
static void
tray_age(int64_t us, char *out, size_t max)
{
    long min = (long)(us / 60000000);

    if (min < 1) {
        snprintf(out, max, "just now");
    } else if (min < 60) {
        snprintf(out, max, "for %ld min", min);
    } else {
        snprintf(out, max, "for %ld h %ld min", min / 60, min % 60);
    }
}

/**
 * @brief What cell @p i's tip says: the board's name, its processor when
 *        that is another name than the board's in any case, its port and
 *        how long it has been connected; or what is happening to it, or
 *        what went wrong.
 * @return 1 when there is a tip.
 */
static int
tray_tip_of(desktop_t *app, int i, char *out, size_t max, int64_t now)
{
    const struct desktop_devtray *c;
    char where[64] = "", mcu[64] = "", age[32], who[160];
    const char *named;
    int k;

    if (i < 0 || i >= TIKU_MOUNT_MAX ||
        app->devtray[i].state == DESKTOP_DEVTRAY_NONE) {
        return 0;
    }
    c = &app->devtray[i];
    named = c->name;
    if (c->port[0] != '\0') {
        device_port_where(c->port, where, sizeof where);
    }
    if (named[0] == '\0') {
        /* A board still connecting is called what the scan named it. */
        for (k = 0; k < app->ncandidates && k < TIKU_MOUNT_MAX; k++) {
            if (strcmp(app->candidates[k].port, c->port) == 0) {
                named = app->candidates[k].name;
                break;
            }
        }
    }
    if (c->name[0] != '\0' &&
        desktop_board_mcu(app, c->name, mcu, sizeof mcu) > 0 &&
        strcasecmp(mcu, c->name) != 0) {
        snprintf(who, sizeof who, "%.48s (%.40s)", c->name, mcu);
    } else {
        snprintf(who, sizeof who, "%.48s", named[0] ? named : where);
    }
    if (c->red_until != 0) {
        snprintf(out, max, "%s: %s", who, c->why);
    } else if (c->state == DESKTOP_DEVTRAY_CONNECTING) {
        if (named[0] != '\0' && where[0] != '\0') {
            snprintf(out, max, "Connecting to %s on %s", who, where);
        } else {
            snprintf(out, max, "Connecting to %s", who);
        }
    } else if (c->state == DESKTOP_DEVTRAY_LENT) {
        snprintf(out, max, "%s, lent to another program%s", who,
                 (app->reclaim.going &&
                  strcmp(app->reclaim.name, c->name) == 0)
                     ? ", being reclaimed" : "");
    } else {
        tray_age(now - c->since, age, sizeof age);
        if (where[0] != '\0') {
            snprintf(out, max, "%s on %s, connected %s", who, where, age);
        } else {
            snprintf(out, max, "%s, connected %s", who, age);
        }
    }
    return 1;
}

/** @brief The tip of the cell under the pointer while the global bar
 *         shows and no menu does.
 *  @return 1 when there is one, in @p out. */
int
desktop_devtray_tip(desktop_t *app, char *out, size_t max)
{
    int i = desktop_devtray_at(app, app->ptr_x, app->ptr_y);

    out[0] = '\0';
    return i >= 0 && app->gbar_on == 1 && app->popup == NULL &&
           tray_tip_of(app, i, out, max, tiku_trk_now());
}

/** @brief Draw the tip of the cell under the pointer below it, kept on
 *         the screen. */
void
desktop_devtray_draw_tip(desktop_t *app)
{
    char text[256];
    tiku_rect_t b, box;
    int i;

    if (!desktop_devtray_tip(app, text, sizeof text)) {
        return;
    }
    i = desktop_devtray_at(app, app->ptr_x, app->ptr_y);
    box = app->devtray_box[i];
    b = tiku_ui_tip_size(text);
    b.x = box.x + box.w / 2 - b.w / 2;
    if (b.x + b.w > app->surf->w - 2) {
        b.x = app->surf->w - 2 - b.w;
    }
    if (b.x < 2) {
        b.x = 2;
    }
    b.y = box.y + box.h + 4;
    tiku_ui_tip(app->surf, b, text);
}

/** @brief The pointer moved from (@p was_x, @p was_y): when it went onto
 *         another cell or off one, the tip band is drawn again. */
void
desktop_devtray_hover(desktop_t *app, int was_x, int was_y)
{
    if (desktop_devtray_at(app, was_x, was_y) !=
        desktop_devtray_at(app, app->ptr_x, app->ptr_y)) {
        desktop_damage(app, tray_band(app));
    }
}

/**
 * @brief Fill the tray menu for cell @p c: Open, Devices Preferences and
 *        Disconnect for a connected board, Reclaim for a lent one.
 * @return 1 when the cell has a menu.
 */
static int
tray_menu_build(desktop_t *app, const struct desktop_devtray *c)
{
    char row[TIKU_MENU_LABEL_MAX], port[256];
    tiku_menu_t *m = &app->devtraymenu;

    tiku_menu_clear(m);
    if (c->red_until != 0) {
        return 0;
    }
    if (c->state == DESKTOP_DEVTRAY_CONNECTED) {
        snprintf(row, sizeof row, "Open %.40s", c->name);
        (void)tiku_menu_add(m, row, TIKU_TRK_CMD_DEVTRAY_OPEN, 0, 0u, 1);
        tiku_menu_set_icon(m, TIKU_TRK_CMD_DEVTRAY_OPEN, "microcontroller");
        /* Devices Preferences reads a board over its wire: a mount with
         * no port has nothing to read. */
        (void)tiku_menu_add(m, "Devices Preferences…",
                            TIKU_TRK_CMD_DEVTRAY_PREFS, 0, 0u,
                            desktop_board_port(app, c->name, port,
                                               sizeof port) > 0);
        (void)tiku_menu_add_separator(m);
        (void)tiku_menu_add(m, "Disconnect", TIKU_TRK_CMD_DEVTRAY_DISCONNECT,
                            0, 0u, 1);
        return 1;
    }
    if (c->state == DESKTOP_DEVTRAY_LENT) {
        snprintf(row, sizeof row, "Reclaim %.40s", c->name);
        (void)tiku_menu_add(m, row, TIKU_TRK_CMD_DEVTRAY_RECLAIM, 0, 0u,
                            !app->reclaim.going);
        tiku_menu_set_icon(m, TIKU_TRK_CMD_DEVTRAY_RECLAIM, "microcontroller");
        return 1;
    }
    return 0;
}

/** @brief Open the folder of connected cell @p c. */
static void
tray_open(desktop_t *app, const struct desktop_devtray *c)
{
    char path[TIKU_PATH_MAX];

    if (c->state == DESKTOP_DEVTRAY_CONNECTED && c->red_until == 0 &&
        c->name[0] != '\0') {
        snprintf(path, sizeof path, "/devices/%s", c->name);
        (void)open_folder(app, path);
    }
}

/**
 * @brief A primary press on a cell, at @p now: the second press of a
 *        double-click opens a connected board's folder; otherwise the
 *        cell's menu drops below it.
 * @return 1 when the press was on a cell.
 */
int
desktop_devtray_press(desktop_t *app, const tiku_event_t *ev, int64_t now)
{
    int i = desktop_devtray_at(app, ev->x, ev->y);
    struct desktop_devtray *c;
    tiku_rect_t box;

    if (i < 0) {
        return 0;
    }
    c = &app->devtray[i];
    box = app->devtray_box[i];
    if (tiku_trk_click_press(&app->click, c, ev->x, ev->y, ev->button, 0u,
                             now)) {
        tray_open(app, c);
        return 1;
    }
    if (!tray_menu_build(app, c)) {
        return 1;
    }
    snprintf(app->devtray_menu_of, sizeof app->devtray_menu_of, "%s", c->name);
    app->popup = &app->devtraymenu;
    app->popup_sub = NULL;
    app->popup_frame = tiku_menu_place(&app->devtraymenu, box.x,
        box.y + app->menus.bar.height + 1,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
    app->nav_on = 0;
    return 1;
}

/**
 * @brief A press while the tray menu shows, at @p now: on the cell it
 *        dropped from, the second press of a double-click opens the
 *        board's folder.
 * @return 1 when it did; the menu is then to be closed.
 */
int
desktop_devtray_again(desktop_t *app, const tiku_event_t *ev, int64_t now)
{
    int i = desktop_devtray_at(app, ev->x, ev->y);

    if (i < 0 || strcmp(app->devtray[i].name, app->devtray_menu_of) != 0) {
        return 0;
    }
    if (!tiku_trk_click_press(&app->click, &app->devtray[i], ev->x, ev->y,
                              ev->button, 0u, now)) {
        return 0;
    }
    tray_open(app, &app->devtray[i]);
    return 1;
}

/**
 * @brief Carry out a tray menu @p command on the board the menu was
 *        raised on.
 * @return 1 when @p command is one of the tray menu's.
 */
int
desktop_devtray_invoke(desktop_t *app, int command)
{
    struct desktop_devtray *c;

    if (command < TIKU_TRK_CMD_DEVTRAY_OPEN ||
        command > TIKU_TRK_CMD_DEVTRAY_RECLAIM) {
        return 0;
    }
    c = cell_named(app, app->devtray_menu_of);
    if (c == NULL) {
        return 1;
    }
    switch (command) {
    case TIKU_TRK_CMD_DEVTRAY_OPEN:
        tray_open(app, c);
        break;
    case TIKU_TRK_CMD_DEVTRAY_PREFS:
        desktop_deviceprefs_open_on(app, c->name);
        break;
    case TIKU_TRK_CMD_DEVTRAY_DISCONNECT:
        (void)device_disconnect(app, c->name);
        break;
    default:
        (void)desktop_board_reclaim(app, c->name, -1, NULL);
        break;
    }
    desktop_damage_all(app);
    return 1;
}
