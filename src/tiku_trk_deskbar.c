/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.c - the Deskbar.
 *
 * Laid out as the original is: the leaf at the head, then the application
 * list -- each application's windows nested underneath when it is expanded --
 * then the tray, whose first row leaves room for the clock.  Behaviour
 * follows the DB-* rows read from Deskbar's own source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar.h"
#include "tiku_trk_iconpaint.h"
#include "tiku_trk_poseview.h"       /* TIKU_TRK_MOD_* */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Metrics read out of Deskbar's own source rather than estimated.  Rows do
 * not tile: each is drawn one pixel short and the seam between them is left
 * unpainted, so the pitch is the row plus SEAM. */
#define BAR_W        134     /* gMinimumWindowWidth at the 12 px face      */
#define LEAF_H        26
#define TEAM_H        20     /* kMinTeamItemHeight; pitch 21               */
#define WIN_H         19     /* pitch 20                                   */
#define SEAM           1     /* kGutter: the unpainted row between items   */
#define WIN_INDENT    22
#define TRAY_ROW      18     /* kGutter + fMaxReplicantHeight + kGutter    */
#define CLOCK_H       20
#define PAD            4
#define EXPANDER_W    13     /* ExpanderBounds() is 13 px, not 12          */
#define EXPANDER_ARM   5
#define EXPANDER_PEN   2
#define TRAY_PAD       3     /* fTrayPadding                               */
#define CLOCK_MARGIN  12     /* fClockMargin = fTrayPadding * 4            */
#define ICON_SZ       16     /* gMinReplicantWidth / kMinimumIconSize      */

struct tiku_trk_deskbar {
    tiku_trk_backend_t  *backend;
    tiku_trk_db_dock_t   dock;
    tiku_trk_team_t      teams[TIKU_TRK_DB_TEAMS_MAX];
    int                  dragging;
    int                  drop_team;
    int                  team_count;
    tiku_trk_replicant_t tray[TIKU_TRK_DB_TRAY_MAX];
    int                  tray_count;
    int                  menu_open;
    int                  calendar_open;
    int                  win_menu_team;    /* -1 = none showing        */
    tiku_desk_rect_t     frame;
};

tiku_trk_deskbar_t *
tiku_trk_deskbar_new(tiku_trk_backend_t *backend)
{
    tiku_trk_deskbar_t *db = calloc(1, sizeof *db);

    if (db != NULL) {
        db->backend = backend;
        db->dock = TIKU_TRK_DB_RIGHT_EXPANDED;
        db->win_menu_team = -1;
    }
    return db;
}

void
tiku_trk_deskbar_free(tiku_trk_deskbar_t *db)
{
    free(db);
}

void
tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db, tiku_trk_db_dock_t dock)
{
    if (db != NULL) {
        db->dock = dock;
    }
}

tiku_trk_db_dock_t
tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->dock : TIKU_TRK_DB_RIGHT_EXPANDED;
}

int
tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->team_count : 0;
}

const char *
tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i)
{
    if (db == NULL || i < 0 || i >= db->team_count) {
        return NULL;
    }
    return db->teams[i].name;
}

int
tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    /* One entry per application however many instances it has (DB-015). */
    for (i = 0; i < db->team_count; i++) {
        if (strcmp(db->teams[i].name, name) == 0) {
            return i;
        }
    }
    if (db->team_count >= TIKU_TRK_DB_TEAMS_MAX) {
        return -1;
    }
    i = db->team_count++;
    memset(&db->teams[i], 0, sizeof db->teams[i]);
    snprintf(db->teams[i].name, sizeof db->teams[i].name, "%s", name);
    /* Tracker opens everything: it is the thing that would be asked to
     * open anything nothing else claims. */
    db->teams[i].takes_anything = (strcmp(name, "Tracker") == 0);
    db->teams[i].expanded = 1;
    return i;
}

void
tiku_trk_deskbar_strip_title(const char *app, const char *title, char *out,
                             size_t max)
{
    size_t al, tl;
    const char *p = title;

    if (app == NULL || title == NULL || out == NULL) {
        return;
    }
    al = strlen(app);
    /* "<app>: rest" -> "rest" */
    if (strncmp(p, app, al) == 0 && p[al] == ':') {
        p += al + 1;
        while (*p == ' ') { p++; }
    }
    snprintf(out, max, "%s", p);
    /* "rest - <app>" -> "rest" */
    tl = strlen(out);
    if (tl > al + 3 && strcmp(out + tl - al, app) == 0 &&
        out[tl - al - 1] == ' ' && out[tl - al - 2] == '-') {
        out[tl - al - 3] = '\0';
    }
}

/** @brief Natural order, so "Doc 2" sorts before "Doc 10" (DB row). */
static int
natural_less(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long x = 0, y = 0;
            while (*a >= '0' && *a <= '9') { x = x * 10 + (*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { y = y * 10 + (*b++ - '0'); }
            if (x != y) { return (x < y); }
            continue;
        }
        if (*a != *b) { return ((unsigned char)*a < (unsigned char)*b); }
        a++;
        b++;
    }
    return (*a == '\0' && *b != '\0');
}

void
tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                             const tiku_trk_dbwin_t *wins, int n, int active)
{
    tiku_trk_team_t *t;
    int i, j;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    t = &db->teams[team];
    if (n > TIKU_TRK_DB_WINS_MAX) {
        n = TIKU_TRK_DB_WINS_MAX;
    }
    for (i = 0; i < n; i++) {
        t->wins[i] = wins[i];
    }
    t->win_count = n;
    t->active = active;
    /* Kept in natural title order, so the list does not reshuffle as
     * windows are raised. */
    for (i = 1; i < n; i++) {
        tiku_trk_dbwin_t key = t->wins[i];
        for (j = i - 1; j >= 0 && natural_less(key.title, t->wins[j].title);
             j--) {
            t->wins[j + 1] = t->wins[j];
        }
        t->wins[j + 1] = key;
    }
}

int
tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                               const char *path)
{
    tiku_trk_replicant_t *r;

    if (db == NULL || db->tray_count >= TIKU_TRK_DB_TRAY_MAX) {
        return -1;
    }
    r = &db->tray[db->tray_count++];
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->valid = 0;
    /* Ask the node what its reading MEANS, once, so the tray can show
     * "16h 26m" rather than 59182. */
    if (db->backend != NULL && db->backend->ops->stat != NULL) {
        tiku_trk_model_t m;

        if (db->backend->ops->stat(db->backend, path, &m) == 0) {
            r->desc_known = tiku_trk_desc_parse(m.facts.meta, &r->desc);
        }
    }
    return 0;
}

int
tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        tiku_trk_replicant_t *r = &db->tray[i];
        char buf[64];
        int n = db->backend->ops->read(db->backend, r->path, buf,
                                       sizeof buf - 1);
        char *nl;

        if (n < 0) {
            continue;
        }
        buf[n] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) { *nl = '\0'; }
        buf[sizeof r->value - 1u] = '\0';
        if (strcmp(r->value, buf) != 0) {
            memcpy(r->value, buf, strlen(buf) + 1u);
            changed++;
        }
        r->valid = 1;
    }
    return changed;
}

/*---------------------------------------------------------------------------*/
/* layout                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Rows the application list occupies, windows included. */
static int
list_rows(const tiku_trk_deskbar_t *db)
{
    int i, h = 0;

    for (i = 0; i < db->team_count; i++) {
        h += TEAM_H + SEAM;
        if (db->teams[i].expanded) {
            h += db->teams[i].win_count * (WIN_H + SEAM);
        }
    }
    return h;
}

static int
list_y(const tiku_trk_deskbar_t *db)
{
    (void)db;
    return LEAF_H + PAD;
}

static int
tray_y(const tiku_trk_deskbar_t *db)
{
    return list_y(db) + list_rows(db) + PAD;
}

tiku_desk_rect_t
tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db)
{
    tiku_desk_rect_t r = { 0, 0, 0, 0 };

    return (db != NULL) ? db->frame : r;
}

int
tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y)
{
    tiku_desk_rect_t f;

    if (db == NULL) {
        return 0;
    }
    f = db->frame;
    return (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h);
}

int
tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->menu_open : 0;
}

void
tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        /* The same state a click on the leaf sets.  Exposed because the
         * keyboard has to reach it too, and a synthetic click would have
         * to invent coordinates for a button whose position is layout's
         * business (DB-011). */
        db->menu_open = 1;
        db->win_menu_team = -1;
    }
}

void
tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        db->menu_open = 0;
        db->win_menu_team = -1;
    }
}

int
tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->calendar_open : 0;
}

/*---------------------------------------------------------------------------*/
/* drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A window's state icon: filled when shown, hollow when minimised. */
static void
draw_win_icon(tiku_desk_surface_t *s, int x, int y, int minimized,
              int current)
{
    tiku_desk_rect_t b = { x, y, 9, 9 };
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);

    tiku_desk_fill(s, b, minimized ? TIKU_DESK_C_PANEL : TIKU_DESK_C_DOC);
    tiku_desk_frame(s, b, dark);
    if (!minimized) {
        /* A shown window carries a title bar on its glyph; a minimised one
         * is an empty outline, so the two are told apart at a glance. */
        tiku_desk_hline(s, b.x + 1, b.y + 1, b.w - 2, TIKU_DESK_C_TAB);
        tiku_desk_hline(s, b.x + 1, b.y + 2, b.w - 2, TIKU_DESK_C_TAB);
    }
    if (current) {
        tiku_desk_frame(s, (tiku_desk_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_DESK_C_SELECT);
    }
}

/** @brief The expander triangle: right when collapsed, down when expanded. */
/**
 * @brief Where the leaf menu goes, given which edge the bar is docked to.
 *
 * Docked LEFT the panel has to fall to the right of the bar; the fixed
 * "left of the bar" offset put it at a negative x, i.e. off the screen
 * entirely (DB-010).
 */
/**
 * @brief The leaf menu's fixed contents.
 *
 * One list, so the hit test and the drawing cannot drift apart -- a menu
 * whose targets are computed from a second copy is a menu that eventually
 * drops on the wrong thing.
 */
static const char *const *
leaf_items(void)
{
    static const char *const items[] = {
        "About this system", "Find…", "-", "Deskbar preferences…",
        "-", "/sys", "/dev", "/data", "-", "Shut down…"
    };

    return items;
}

#define LEAF_ITEMS 10

static tiku_desk_rect_t
leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows)
{
    tiku_desk_rect_t m;

    m.w = 156;
    m.h = rows * rowh + 4;
    m.y = LEAF_H + 2;
    m.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED)
          ? db->frame.x + db->frame.w + 2
          : db->frame.x - m.w - 2;
    if (m.x < 0) {
        m.x = 0;
    }
    return m;
}

static void
draw_expander(tiku_desk_surface_t *s, int x, int y, int expanded)
{
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);
    int k;

    /* STROKED along an open three-point polyline with a two-pixel pen, not
     * filled: a solid triangle carries far more weight than the original's
     * arrow and is one of the things that reads as a cheap imitation. */
    for (k = 0; k < EXPANDER_PEN; k++) {
        int i;

        for (i = 0; i < EXPANDER_ARM; i++) {
            if (expanded) {           /* pointing down: \ then /            */
                tiku_desk_pixel(s, x + i, y + i + k, dark);
                tiku_desk_pixel(s, x + 2 * EXPANDER_ARM - 2 - i, y + i + k,
                                dark);
            } else {                  /* pointing right: / then \           */
                tiku_desk_pixel(s, x + i + k, y + i, dark);
                tiku_desk_pixel(s, x + i + k, y + 2 * EXPANDER_ARM - 2 - i,
                                dark);
            }
        }
    }
}

/**
 * @brief A list row's background.
 *
 * The two recipes are structurally different, not one darker than the other:
 * an idle row is bevelled on every edge over a light gradient, a selected one
 * is a dark slab with a single dark cap and no side bevel at all.  The label
 * colour does NOT change either way -- the original draws selected and
 * unselected menu text in the same black.
 */
static void
draw_row_bg(tiku_desk_surface_t *s, tiku_desk_rect_t r, int selected)
{
    int i;

    for (i = 0; i < r.h; i++) {
        float t = (r.h > 1) ? (float)i / (float)(r.h - 1) : 0.0f;
        float tint = selected ? (1.25f + (1.20f - 1.25f) * t)
                              : (0.69f + (1.03f - 0.69f) * t);

        tiku_desk_hline(s, r.x, r.y + i, r.w,
                        tiku_desk_tint(TIKU_DESK_C_PANEL, tint));
    }
    if (selected) {
        tiku_desk_hline(s, r.x, r.y, r.w,
                        tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f));
        return;
    }
    tiku_desk_hline(s, r.x, r.y, r.w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 0.50f));
    tiku_desk_vline(s, r.x, r.y, r.h,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 0.70f));
    tiku_desk_vline(s, r.x + r.w - 1, r.y, r.h,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 1.08f));
    tiku_desk_hline(s, r.x, r.y + r.h - 1, r.w,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, 1.08f));
}

/** @brief Truncate to a width budget, with an ellipsis (DB row). */
static void
fit_label(const tiku_desk_font_t *f, const char *src, int budget, char *out,
          size_t max)
{
    size_t n;

    snprintf(out, max, "%s", src);
    if (tiku_desk_text_width(f, out) <= budget) {
        return;
    }
    n = strlen(out);
    while (n > 1u) {
        out[--n] = '\0';
        if (n > 3u) {
            out[n - 1u] = '.';
            out[n - 2u] = '.';
            out[n - 3u] = '.';
        }
        if (tiku_desk_text_width(f, out) <= budget) {
            return;
        }
    }
}

void
tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_desk_surface_t *s)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    const tiku_desk_font_t *fb = tiku_desk_font_bold();
    tiku_desk_rgb_t dark = tiku_desk_tint(TIKU_DESK_C_PANEL, 1.40f);
    tiku_desk_rect_t leaf;
    int i, j, y, rows;

    if (db == NULL || s == NULL) {
        return;
    }
    rows = (db->dock == TIKU_TRK_DB_RIGHT_MINI) ? 0 : list_rows(db);
    db->frame.w = BAR_W;
    db->frame.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED) ? 0 : s->w - BAR_W;
    db->frame.y = 0;
    db->frame.h = LEAF_H + PAD + rows + PAD +
                  db->tray_count * TRAY_ROW + CLOCK_H + PAD;

    tiku_desk_fill(s, db->frame, TIKU_DESK_C_PANEL);
    tiku_desk_bevel(s, db->frame,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_LIGHTEN_MAX),
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));

    /* The leaf: the one control that is not a list item, so it is the one
     * thing wearing the tab colour. */
    leaf = (tiku_desk_rect_t){ db->frame.x + 2, 2, db->frame.w - 4, LEAF_H };
    tiku_desk_fill(s, leaf, TIKU_DESK_C_TAB);
    tiku_desk_frame(s, leaf, dark);
    tiku_desk_bevel(s, tiku_desk_inset(leaf, 1),
                    tiku_desk_tint(TIKU_DESK_C_TAB, TIKU_DESK_LIGHTEN_1),
                    tiku_desk_tint(TIKU_DESK_C_TAB, TIKU_DESK_DARKEN_2));
    (void)tiku_desk_text_centered(s, fb, leaf, "TikuOS", TIKU_DESK_C_TEXT);

    if (db->dock != TIKU_TRK_DB_RIGHT_MINI) {
        y = list_y(db);
        for (i = 0; i < db->team_count; i++) {
            tiku_trk_team_t *t = &db->teams[i];
            tiku_desk_rect_t row = { db->frame.x + 2, y, db->frame.w - 4,
                                     TEAM_H };
            tiku_desk_rect_t icon = { row.x + 4, y + (TEAM_H - ICON_SZ) / 2,
                                      ICON_SZ, ICON_SZ };
            char label[TIKU_TRK_DB_LABEL_MAX];

            /* A hovering drag lights the row it would be handed to, so
             * the promise the drop makes is visible before it is made
             * (DB-035). */
            if (db->drop_team == i) {
                tiku_desk_fill(s, row, TIKU_DESK_C_SELECT);
            } else {
                draw_row_bg(s, row, t->active);
            }
            /* The application's icon.  Tracker's own team is the one we can
             * name; anything else gets the generic application icon rather
             * than a blank, so the column never has a hole in it. */
            /* DB-018: when even the generic icon is missing, the slot is
             * left as it is.  A coloured rectangle where an icon failed
             * looks like an application whose icon IS a blue square. */
            (void)tiku_trk_icon_paint(s, strcmp(t->name, "Tracker") == 0
                                         ? "folder" : "application",
                                      icon.x, icon.y, ICON_SZ);

            fit_label(f, t->name, row.w - ICON_SZ - EXPANDER_W - 14, label,
                      sizeof label);
            tiku_desk_text(s, f, icon.x + ICON_SZ + 5,
                           y + (TEAM_H - f->height) / 2 + f->ascent, label,
                           TIKU_DESK_C_TEXT);
            draw_expander(s, row.x + row.w - EXPANDER_W - 2,
                          y + (TEAM_H - 9) / 2, t->expanded);
            y += TEAM_H + SEAM;

            if (!t->expanded) {
                continue;
            }
            for (j = 0; j < t->win_count; j++) {
                tiku_trk_dbwin_t *w = &t->wins[j];
                tiku_desk_rect_t wr = { db->frame.x + 2, y, db->frame.w - 4,
                                        WIN_H };
                char wl[TIKU_TRK_DB_LABEL_MAX];

                draw_row_bg(s, wr, w->current);
                draw_win_icon(s, wr.x + WIN_INDENT,
                              y + (WIN_H - 9) / 2,
                              w->state == TIKU_TRK_DBW_MINIMIZED,
                              0);
                fit_label(f, w->title, wr.w - WIN_INDENT - 20, wl,
                          sizeof wl);
                tiku_desk_text(s, f, wr.x + WIN_INDENT + 14,
                               y + (WIN_H - f->height) / 2 + f->ascent, wl,
                               TIKU_DESK_C_TEXT);
                y += WIN_H + SEAM;
            }
        }
    }

    /* The tray, then the clock beneath it: the original reserves the
     * clock's room in the first row, and stacking it here is the vertical
     * equivalent. */
    y = (db->dock == TIKU_TRK_DB_RIGHT_MINI) ? LEAF_H + PAD : tray_y(db);
    tiku_desk_hline(s, db->frame.x + 4, y - 3, db->frame.w - 8,
                    tiku_desk_tint(TIKU_DESK_C_PANEL, TIKU_DESK_DARKEN_2));
    tiku_desk_hline(s, db->frame.x + 4, y - 2, db->frame.w - 8,
                    tiku_desk_tint(TIKU_DESK_C_PANEL,
                                   TIKU_DESK_LIGHTEN_MAX));
    for (i = 0; i < db->tray_count; i++) {
        const tiku_trk_replicant_t *r = &db->tray[i];
        int ty = y + (TRAY_ROW - f->height) / 2 + f->ascent;
        char val[32];
        int vw;

        /* The tray is the narrowest place on screen, so it is the place a
         * formatted reading earns the most: the column is too thin to read
         * a raw second count or byte count against. */
        (void)tiku_trk_attr_format(r->desc_known ? &r->desc : NULL,
                                   r->valid ? r->value : "",
                                   BAR_W - 40, val, sizeof val);
        vw = tiku_desk_text_width(f, val);

        tiku_desk_text(s, f, db->frame.x + 6, ty, r->label, TIKU_DESK_C_TEXT);
        tiku_desk_text(s, f, db->frame.x + db->frame.w - 6 - vw, ty, val,
                       TIKU_DESK_C_TEXT);
        y += TRAY_ROW;
    }
    {
        time_t now = time(NULL);
        struct tm tmv;
        char clock[32];
        int cw;

        (void)localtime_r(&now, &tmv);
        /* Short date and time, as the original shows by default. */
        snprintf(clock, sizeof clock, "%d %s  %02d:%02d", tmv.tm_mday,
                 (const char *[]){ "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov",
                                   "Dec" }[tmv.tm_mon],
                 tmv.tm_hour, tmv.tm_min);
        cw = tiku_desk_text_width(f, clock);
        tiku_desk_text(s, f, db->frame.x + (db->frame.w - cw) / 2,
                       y + (CLOCK_H - f->height) / 2 + f->ascent, clock,
                       TIKU_DESK_C_TEXT);
    }

    /* The leaf menu, with the fixed head the original always shows. */
    if (db->menu_open) {
        const char *const *items = leaf_items();
        int rowh = f->height + 6;
        tiku_desk_rect_t m = leaf_menu_rect(db, rowh, 10);
        tiku_desk_ui_menu(s, m, items, 10, -1);
    }
    /* The window menu for one application: Hide all / Show all / Close all,
     * which the original appends to every application's window list. */
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        static const char *const items[] = {
            "Hide all", "Show all", "-", "Close all"
        };
        int rowh = f->height + 6;
        tiku_desk_rect_t m = { db->frame.x - 130, list_y(db) + TEAM_H, 128,
                               4 * rowh + 4 };
        tiku_desk_ui_menu(s, m, items, 4, -1);
    }
    if (db->calendar_open) {
        /* The clock's calendar: a month grid with steppers, popped beside
         * the bar and dismissed by the next click anywhere. */
        time_t now = time(NULL);
        struct tm tmv;
        tiku_desk_rect_t c = { db->frame.x - 190, db->frame.h - 150, 186,
                               148 };
        int col, rowi, first, days;

        (void)localtime_r(&now, &tmv);
        tiku_desk_fill(s, c, TIKU_DESK_C_PANEL);
        tiku_desk_frame(s, c, dark);
        tiku_desk_bevel(s, tiku_desk_inset(c, 1),
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_LIGHTEN_MAX),
                        tiku_desk_tint(TIKU_DESK_C_PANEL,
                                       TIKU_DESK_DARKEN_2));
        {
            char head[32];
            snprintf(head, sizeof head, "%s %d",
                     (const char *[]){ "January", "February", "March",
                       "April", "May", "June", "July", "August",
                       "September", "October", "November",
                       "December" }[tmv.tm_mon], 1900 + tmv.tm_year);
            (void)tiku_desk_text_centered(s, fb,
                (tiku_desk_rect_t){ c.x, c.y + 2, c.w, 18 }, head,
                TIKU_DESK_C_TEXT);
        }
        first = (tmv.tm_wday - (tmv.tm_mday - 1) % 7 + 7) % 7;
        days = 31;
        for (rowi = 0; rowi < 6; rowi++) {
            for (col = 0; col < 7; col++) {
                int day = rowi * 7 + col - first + 1;
                char num[4];
                tiku_desk_rect_t cell = { c.x + 6 + col * 25,
                                          c.y + 22 + rowi * 20, 24, 18 };
                if (day < 1 || day > days) {
                    continue;
                }
                if (day == tmv.tm_mday) {
                    tiku_desk_fill(s, cell, TIKU_DESK_C_SELECT);
                }
                snprintf(num, sizeof num, "%d", day);
                (void)tiku_desk_text_centered(s, f, cell, num,
                    (day == tmv.tm_mday) ? TIKU_DESK_C_SELTEXT
                                         : TIKU_DESK_C_TEXT);
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* interaction                                                               */
/*---------------------------------------------------------------------------*/

int
tiku_trk_deskbar_click(tiku_trk_deskbar_t *db, int x, int y, unsigned mods,
                       int secondary, tiku_trk_db_pick_t *pick)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    int i, j, ry;

    if (db == NULL || pick == NULL) {
        return 0;
    }
    memset(pick, 0, sizeof *pick);

    /* An open popup owns the next click wherever it lands. */
    if (db->calendar_open) {
        db->calendar_open = 0;
        return 1;
    }
    if (db->win_menu_team >= 0) {
        int rowh = f->height + 6;
        tiku_desk_rect_t m = { db->frame.x - 130, list_y(db) + TEAM_H, 128,
                               4 * rowh + 4 };
        int idx = (y - m.y - 2) / rowh;

        db->win_menu_team = -1;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
            if (idx == 0) { pick->what = TIKU_TRK_DB_HIDE_ALL; }
            else if (idx == 1) { pick->what = TIKU_TRK_DB_SHOW_ALL; }
            else if (idx == 3) { pick->what = TIKU_TRK_DB_CLOSE_ALL; }
        }
        return 1;
    }
    if (db->menu_open) {
        int rowh = f->height + 6;
        tiku_desk_rect_t m = leaf_menu_rect(db, rowh, 10);
        int idx = (y - m.y - 2) / rowh;

        db->menu_open = 0;
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
            switch (idx) {
            case 0: pick->what = TIKU_TRK_DB_SHOW_ABOUT; break;
            case 1: pick->what = TIKU_TRK_DB_FIND; break;
            case 5: pick->what = TIKU_TRK_DB_OPEN_PATH;
                    snprintf(pick->path, sizeof pick->path, "/sys"); break;
            case 6: pick->what = TIKU_TRK_DB_OPEN_PATH;
                    snprintf(pick->path, sizeof pick->path, "/dev"); break;
            case 7: pick->what = TIKU_TRK_DB_OPEN_PATH;
                    snprintf(pick->path, sizeof pick->path, "/data"); break;
            case 9: pick->what = TIKU_TRK_DB_QUIT; break;
            default: break;
            }
        }
        return 1;
    }
    if (!tiku_trk_deskbar_hit(db, x, y)) {
        return 0;
    }
    if (y < LEAF_H + 2) {
        db->menu_open = 1;
        return 1;
    }

    /* The application list: the expander toggles, the name opens the window
     * menu, a window entry raises (or minimises with Ctrl). */
    ry = list_y(db);
    for (i = 0; i < db->team_count; i++) {
        tiku_trk_team_t *t = &db->teams[i];

        /* The seam belongs to neither row; a click on it is a miss, which
         * is why hit-testing uses the row height and stepping uses the
         * pitch. */
        if (y >= ry && y < ry + TEAM_H) {
            if (x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
                t->expanded = !t->expanded;
            } else if (secondary) {
                db->win_menu_team = i;
            }
            return 1;
        }
        ry += TEAM_H + SEAM;
        if (!t->expanded) {
            continue;
        }
        for (j = 0; j < t->win_count; j++) {
            if (y >= ry && y < ry + WIN_H) {
                pick->tag = t->wins[j].tag;
                if (secondary) {
                    pick->what = TIKU_TRK_DB_CLOSE_WINDOW;
                } else if (mods & TIKU_TRK_MOD_CMD) {
                    pick->what = TIKU_TRK_DB_MINIMIZE_WINDOW;
                } else {
                    pick->what = TIKU_TRK_DB_ACTIVATE_WINDOW;
                }
                return 1;
            }
            ry += WIN_H + SEAM;
        }
    }
    /* The clock pops the calendar; the rest of the tray is inert for now. */
    if (y >= db->frame.h - CLOCK_H - PAD) {
        db->calendar_open = 1;
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Drops                                                                     */
/*---------------------------------------------------------------------------*/

tiku_trk_db_drop_t
tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                         int *which)
{
    tiku_desk_rect_t f;
    int ry, i;

    if (which != NULL) {
        *which = -1;
    }
    if (db == NULL || !tiku_trk_deskbar_hit(db, x, y)) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    f = tiku_trk_deskbar_frame(db);
    ry = y - f.y;
    if (ry < LEAF_H) {
        return TIKU_TRK_DB_DROP_LEAF;
    }
    ry -= LEAF_H + PAD;
    for (i = 0; i < db->team_count; i++) {
        if (ry >= 0 && ry < TEAM_H) {
            if (which != NULL) { *which = i; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        ry -= TEAM_H + SEAM;
        if (db->teams[i].expanded) {
            /* A window row is not a drop target of its own: the drop
             * belongs to the application that owns it. */
            ry -= db->teams[i].win_count * (WIN_H + SEAM);
        }
    }
    return TIKU_TRK_DB_DROP_BAR;
}

void
tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging)
{
    if (db != NULL) {
        db->dragging = dragging;
        if (!dragging) {
            /* The highlight belongs to the drag: when one ends there is
             * nothing for it to be promising. */
            db->drop_team = -1;
        }
    }
}

void
tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i)
{
    if (db != NULL) {
        db->drop_team = i;
    }
}

int
tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->drop_team : -1;
}

int
tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed)
{
    if (db == NULL) {
        return 1;
    }
    /* Only the FIXED items go dead while a drag is in flight; folders and
     * recents stay live, because those are exactly the drop targets the
     * menu exists to offer during one (DB-008). */
    return fixed ? !db->dragging : 1;
}

int
tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i, const char *type)
{
    if (db == NULL || i < 0 || i >= db->team_count || type == NULL ||
        db->teams[i].ntype >= 8) {
        return -1;
    }
    snprintf(db->teams[i].type[db->teams[i].ntype],
             sizeof db->teams[i].type[0], "%s", type);
    db->teams[i].ntype++;
    return 0;
}

int
tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                              const char *const *types, int ntypes,
                              int force)
{
    int t, k;

    if (db == NULL || i < 0 || i >= db->team_count) {
        return 0;
    }
    if (force || db->teams[i].takes_anything) {
        return 1;
    }
    if (ntypes <= 0) {
        return 0;                   /* nothing known about them: refuse */
    }
    /* EVERY type must be one it opens: handing an application a mixed
     * selection it can only half read is a drop that half works. */
    for (t = 0; t < ntypes; t++) {
        int ok = 0;

        for (k = 0; k < db->teams[i].ntype; k++) {
            if (strcmp(db->teams[i].type[k], types[t]) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_deskbar_launchable(const tiku_trk_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* A directory is silently ignored rather than refused: launching one
     * has no meaning, and complaining about something the user very likely
     * did by accident would be noise (DB-066). */
    return !tiku_trk_model_is_container(m);
}

tiku_trk_db_drop_t
tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                              char *path, size_t max)
{
    const tiku_desk_font_t *f = tiku_desk_font_plain();
    const char *const *items = leaf_items();
    tiku_desk_rect_t m;
    int rowh, i;

    if (path != NULL && max > 0u) {
        path[0] = '\0';
    }
    if (db == NULL || !db->menu_open) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    rowh = f->height + 6;
    m = leaf_menu_rect(db, rowh, LEAF_ITEMS);
    if (x < m.x || x >= m.x + m.w || y < m.y + 2 ||
        y >= m.y + 2 + LEAF_ITEMS * rowh) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    i = (y - m.y - 2) / rowh;
    if (i < 0 || i >= LEAF_ITEMS || items[i][0] == '-') {
        return TIKU_TRK_DB_DROP_NONE;   /* a separator is nobody's */
    }
    /* An entry naming a path is a folder and takes the links; everything
     * else is fixed, and fixed items are not drop targets at all while a
     * drag is in flight (DB-008). */
    if (items[i][0] == '/') {
        if (path != NULL) {
            snprintf(path, max, "%s", items[i]);
        }
        return TIKU_TRK_DB_DROP_MENU_DIR;
    }
    return TIKU_TRK_DB_DROP_NONE;
}
