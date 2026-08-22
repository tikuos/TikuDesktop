/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs.c - the settings table, its file, and the page rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_prefs.h"
#include "tiku_strings.h"
#include "tiku_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B TIKU_TRK_PREF_BOOL
#define I TIKU_TRK_PREF_INT
#define C TIKU_TRK_PREF_COLOUR

/** @brief Every setting, with the name it has in the file. */
static const tiku_trk_pref_t table[] = {
    /* On: ONE way in to the machine's filesystem, for when a person
     * wants it.  The volumes themselves stay off, or the Desktop
     * becomes a view of the host again. */
    { "ShowDisksIcon",           B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    /* Off: this Desktop is a device's, and the machine's volumes are
     * not what it is for.  The Preferences page still offers them. */
    { "MountVolumesOntoDesktop", B, TIKU_TRK_PAGE_DESKTOP, 0, 0, 0 },
    { "MountSharedVolumesOntoDesktop", B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    { "IntegrateNonBootBeOSDesktops", B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    /* Last on its page so the rows above it keep their places. */
    { "SelectedLabelColor",      C, TIKU_TRK_PAGE_DESKTOP,
      (int32_t)0xc01a3350, 0, 0 },
    { "ShowFullPathInTitleBar",  B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "SingleWindowBrowse",      B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "ShowNavigator",           B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "ShowSelectionWhenInactive", B, TIKU_TRK_PAGE_NONE,  1, 0, 0 },
    { "TransparentSelection",    B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    { "SortFolderNamesFirst",    B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    { "HideDotFiles",            B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "TypeAheadFiltering",      B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "GenerateImageThumbnails", B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    /* Not a Haiku setting: the theme swap the toolkit's table makes
     * possible.  Appended so every row above keeps its place.
     *
     * THREE-valued, not a flag: 0 light, 1 dark, 2 without hue.  It kept
     * its name when the third arrived, because the name is what is
     * written in everybody's settings file and 0 and 1 still mean what
     * they meant -- see the read path, which also still answers to the
     * word "on" that a file written by an older build says. */
    { "DarkMode",                I, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    /* Not a Haiku setting either: one menu bar at the top of the screen,
     * fed by whatever the focused window publishes. */
    { "GlobalMenuBar",           B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    { "ShowVolumeSpaceBar",      B, TIKU_TRK_PAGE_VOLUMES, 0, 0, 0 },
    { "UsedSpaceColor",          C, TIKU_TRK_PAGE_VOLUMES,
      (int32_t)0xc000cb00, 0, 0 },
    { "FreeSpaceColor",          C, TIKU_TRK_PAGE_VOLUMES,
      (int32_t)0xc0c0cbc0, 0, 0 },
    { "WarningSpaceColor",       C, TIKU_TRK_PAGE_VOLUMES,
      (int32_t)0xc0cb0000, 0, 0 },
    { "EjectWhenUnmounting",     B, TIKU_TRK_PAGE_MOUNT,   1, 0, 0 },
    { "AutoMount",               B, TIKU_TRK_PAGE_MOUNT,   0, 0, 0 },
    { "RecentApplications",      I, TIKU_TRK_PAGE_NONE,   10, 0, 0 },
    { "RecentDocuments",         I, TIKU_TRK_PAGE_NONE,   10, 0, 0 },
    { "RecentFolders",           I, TIKU_TRK_PAGE_NONE,   10, 0, 0 },
    /* No control of their own, and still read and written so an older
     * file round-trips rather than losing them (TS-062). */
    { "DesktopFilePanelRoot",    B, TIKU_TRK_PAGE_NONE,    1, 0, 0 }
};

#define NPREF ((int)(sizeof table / sizeof table[0]))

void
tiku_trk_prefs_init(tiku_trk_prefs_t *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof *p);
    p->pref = calloc((size_t)NPREF, sizeof *p->pref);
    if (p->pref == NULL) {
        return;
    }
    p->n = NPREF;
    for (i = 0; i < NPREF; i++) {
        p->pref[i] = table[i];
        p->pref[i].value = table[i].def;
        p->pref[i].opened = table[i].def;
    }
}

void
tiku_trk_prefs_free(tiku_trk_prefs_t *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->nunknown; i++) {
        free(p->unknown[i]);
    }
    free(p->unknown);
    free(p->pref);
    memset(p, 0, sizeof *p);
}

tiku_trk_pref_t *
tiku_trk_prefs_find(tiku_trk_prefs_t *p, const char *name)
{
    int i;

    if (p == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < p->n; i++) {
        if (strcmp(p->pref[i].name, name) == 0) {
            return &p->pref[i];
        }
    }
    return NULL;
}

int32_t
tiku_trk_prefs_get(tiku_trk_prefs_t *p, const char *name)
{
    tiku_trk_pref_t *f = tiku_trk_prefs_find(p, name);

    return (f != NULL) ? f->value : 0;
}

/**
 * @brief The rules that keep settings from contradicting each other.
 *
 * Applied after ANY change to @p name, however it arrived -- the window's
 * click and a programmatic set go through the same door (TS-046).
 */
static void
interlock(tiku_trk_prefs_t *p, const char *name, int32_t v)
{
    if (strcmp(name, "SingleWindowBrowse") == 0 && v == 0) {
        tiku_trk_pref_t *nav = tiku_trk_prefs_find(p, "ShowNavigator");

        if (nav != NULL) {
            nav->value = 0;
        }
    }
    /* The Disks icon and volumes-on-Desktop are two answers to one
     * question -- what the Desktop shows for storage -- so turning one
     * on turns the other off (TS-046). */
    if (strcmp(name, "ShowDisksIcon") == 0 && v != 0) {
        tiku_trk_pref_t *m = tiku_trk_prefs_find(p,
                                 "MountVolumesOntoDesktop");

        if (m != NULL) {
            m->value = 0;
            /* And what rode on it goes with it. */
            interlock(p, "MountVolumesOntoDesktop", 0);
        }
    }
    if (strcmp(name, "MountVolumesOntoDesktop") == 0) {
        if (v != 0) {
            tiku_trk_pref_t *d = tiku_trk_prefs_find(p, "ShowDisksIcon");

            if (d != NULL) {
                d->value = 0;
            }
        } else {
            /* Shared volumes ride on volumes-on-Desktop: with the base
             * setting off there is nothing for them to ride (TS-046). */
            tiku_trk_pref_t *sh = tiku_trk_prefs_find(p,
                                      "MountSharedVolumesOntoDesktop");

            if (sh != NULL) {
                sh->value = 0;
            }
        }
    }
}

void
tiku_trk_prefs_set(tiku_trk_prefs_t *p, const char *name, int32_t v)
{
    tiku_trk_pref_t *f = tiku_trk_prefs_find(p, name);

    if (f != NULL) {
        f->value = v;
        interlock(p, name, v);
    }
}

int
tiku_trk_prefs_control_enabled(const tiku_trk_prefs_t *p,
                               const tiku_trk_pref_t *pref)
{
    if (pref != NULL &&
        strcmp(pref->name, "MountSharedVolumesOntoDesktop") == 0) {
        const tiku_trk_pref_t *m = tiku_trk_prefs_find(
            (tiku_trk_prefs_t *)p, "MountVolumesOntoDesktop");

        return m != NULL && m->value != 0;
    }
    return 1;
}

/** @brief Remember a line naming no setting we know. */
static void
keep_unknown(tiku_trk_prefs_t *p, const char *line)
{
    char **grown = realloc(p->unknown, (size_t)(p->nunknown + 1) *
                                       sizeof *p->unknown);
    size_t n;

    if (grown == NULL) {
        return;
    }
    p->unknown = grown;
    n = strlen(line) + 1u;
    p->unknown[p->nunknown] = malloc(n);
    if (p->unknown[p->nunknown] != NULL) {
        memcpy(p->unknown[p->nunknown], line, n);
        p->nunknown++;
    }
}

int
tiku_trk_prefs_load(tiku_trk_prefs_t *p, const char *path)
{
    FILE *f;
    char line[512];

    if (p == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return 0;                   /* no file yet: the defaults stand */
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char name[128], val[128];
        tiku_trk_pref_t *pr;
        size_t n = strlen(line);

        while (n > 0u && (line[n - 1u] == '\n' || line[n - 1u] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0u) {
            continue;
        }
        if (sscanf(line, "%127s %127s", name, val) != 2) {
            keep_unknown(p, line);
            continue;
        }
        pr = tiku_trk_prefs_find(p, name);
        if (pr == NULL) {
            keep_unknown(p, line);
            continue;
        }
        if (pr->type == TIKU_TRK_PREF_BOOL) {
            pr->value = (strcmp(val, "on") == 0);
        } else if (pr->type == TIKU_TRK_PREF_COLOUR) {
            pr->value = (int32_t)strtoul(val, NULL, 0);
        } else {
            /*
             * An older build wrote this one as a flag, so a settings file
             * in the wild says "DarkMode on".  strtol would read that as
             * nought and turn somebody's dark desktop back to white on
             * the first launch after an update, silently, which is the
             * kind of thing nobody reports and everybody notices.
             */
            if (strcmp(val, "on") == 0) {
                pr->value = 1;
            } else if (strcmp(val, "off") == 0) {
                pr->value = 0;
            } else {
                pr->value = (int32_t)strtol(val, NULL, 10);
            }
        }
    }
    (void)fclose(f);
    return 0;
}

int
tiku_trk_prefs_save(tiku_trk_prefs_t *p, const char *path)
{
    FILE *f;
    int i;

    if (p == NULL || path == NULL) {
        return -1;
    }
    (void)tiku_state_mkparents(path);
    f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    for (i = 0; i < p->n; i++) {
        const tiku_trk_pref_t *pr = &p->pref[i];

        /* Only what the user actually moved.  A file that repeats every
         * default freezes TODAY'S defaults into it forever: when a later
         * build ships a better default, every saved file silently vetoes
         * it (this happened -- the global bar's default flipped on and
         * old files kept saying off).  A divergence is a decision and is
         * written down; agreement is not. */
        if (pr->value == pr->def) {
            continue;
        }
        if (pr->type == TIKU_TRK_PREF_BOOL) {
            fprintf(f, "%s %s\n", pr->name, pr->value ? "on" : "off");
        } else if (pr->type == TIKU_TRK_PREF_COLOUR) {
            fprintf(f, "%s 0x%08x\n", pr->name, (unsigned)pr->value);
        } else {
            fprintf(f, "%s %d\n", pr->name, (int)pr->value);
        }
    }
    /* Written back last, so a setting this build does not know survives a
     * round trip through it. */
    for (i = 0; i < p->nunknown; i++) {
        fprintf(f, "%s\n", p->unknown[i]);
    }
    return (fclose(f) == 0) ? 0 : -1;
}

void
tiku_trk_prefs_mark_opened(tiku_trk_prefs_t *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->n; i++) {
        p->pref[i].opened = p->pref[i].value;
    }
}

int
tiku_trk_prefs_seats(const char *name)
{
    if (name != NULL && strcmp(name, "DarkMode") == 0) {
        return 3;               /* light, dark, and without hue */
    }
    return 0;
}

int32_t
tiku_trk_prefs_colour_next(int32_t value)
{
    /* A short walk of readable inks: click the swatch, get the next one.
     * Matching ignores the alpha byte, which the walk preserves. */
    static const int32_t wheel[] = {
        0x1a3350,   /* deep navy (the default label highlight) */
        0x000000, 0x404040, 0x5a1010, 0x104a10, 0x46106a,
        0x105a5a, 0x336698, 0xb05a00, 0x00cb00, 0xcbc0c0, 0xcb0000
    };
    int i, n = (int)(sizeof wheel / sizeof wheel[0]);
    int32_t rgb = value & 0xffffff;
    int32_t alpha = value & (int32_t)0xff000000;

    for (i = 0; i < n; i++) {
        if (wheel[i] == rgb) {
            return alpha | wheel[(i + 1) % n];
        }
    }
    return alpha | wheel[0];
}

int
tiku_trk_prefs_defaultable(tiku_trk_prefs_t *p, tiku_trk_page_t page)
{
    int i;

    if (p == NULL || page == TIKU_TRK_PAGE_MOUNT) {
        return 0;   /* the Mount page is never defaultable (TS-065) */
    }
    for (i = 0; i < p->n; i++) {
        if (p->pref[i].page == page && p->pref[i].value != p->pref[i].def) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_prefs_revertable(tiku_trk_prefs_t *p, tiku_trk_page_t page)
{
    int i;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < p->n; i++) {
        if (p->pref[i].page == page &&
            p->pref[i].value != p->pref[i].opened) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_prefs_any_defaultable(tiku_trk_prefs_t *p)
{
    int k;

    for (k = 0; k < TIKU_TRK_PAGES; k++) {
        if (tiku_trk_prefs_defaultable(p, (tiku_trk_page_t)k)) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_prefs_any_revertable(tiku_trk_prefs_t *p)
{
    int k;

    for (k = 0; k < TIKU_TRK_PAGES; k++) {
        if (tiku_trk_prefs_revertable(p, (tiku_trk_page_t)k)) {
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_prefs_defaults(tiku_trk_prefs_t *p, tiku_trk_page_t page)
{
    int i;

    if (p == NULL) {
        return;
    }
    if (page == TIKU_TRK_PAGE_MOUNT) {
        /* The Disk mount page can NEVER be defaulted: a mount policy set
         * back to a default behind the user's back is a disk appearing or
         * refusing to, which the source hard-codes IsDefaultable()=false
         * for (TS-065). */
        return;
    }
    for (i = 0; i < p->n; i++) {
        if (p->pref[i].page == page) {
            p->pref[i].value = p->pref[i].def;
        }
    }
}

void
tiku_trk_prefs_revert(tiku_trk_prefs_t *p, tiku_trk_page_t page)
{
    int i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->n; i++) {
        if (p->pref[i].page == page) {
            /* Every setting on the page, including the ones with no
             * control: the original's Windows page misses HideDotFiles
             * here and corrupts another, which is a bug rather than a
             * behaviour worth porting (TS-061). */
            p->pref[i].value = p->pref[i].opened;
        }
    }
}

const char *
tiku_trk_prefs_page_name(tiku_trk_page_t page)
{
    switch (page) {
    case TIKU_TRK_PAGE_DESKTOP: return tiku_str("prefs.page.desktop");
    case TIKU_TRK_PAGE_WINDOWS: return tiku_str("prefs.page.windows");
    case TIKU_TRK_PAGE_VOLUMES: return tiku_str("prefs.page.volumes");
    case TIKU_TRK_PAGE_MOUNT:   return tiku_str("prefs.page.mount");
    default:                    return "";
    }
}

/*---------------------------------------------------------------------------*/
/* The window's layout                                                       */
/*---------------------------------------------------------------------------*/

#define PAGE_H   24
#define PAGE_TOP 12
#define CTRL_H   22
#define CTRL_TOP 12
#define BTN_W    88
#define BTN_H    24
#define BTN_PAD  12

void
tiku_trk_prefs_page_rect(int k, int *x, int *y, int *w, int *h)
{
    if (x != NULL) { *x = 8; }
    if (y != NULL) { *y = PAGE_TOP + k * PAGE_H; }
    if (w != NULL) { *w = TIKU_TRK_PREF_LIST_W - 12; }
    if (h != NULL) { *h = PAGE_H - 2; }
}

void
tiku_trk_prefs_control_rect(int nth, int *x, int *y, int *w, int *h)
{
    if (x != NULL) { *x = TIKU_TRK_PREF_LIST_W + 12; }
    if (y != NULL) { *y = CTRL_TOP + nth * CTRL_H; }
    if (w != NULL) { *w = TIKU_TRK_PREF_W - TIKU_TRK_PREF_LIST_W - 24; }
    if (h != NULL) { *h = CTRL_H - 2; }
}

int
tiku_trk_prefs_page_controls(tiku_trk_prefs_t *p, tiku_trk_page_t page,
                             int *out, int max)
{
    int i, n = 0;

    if (p == NULL || out == NULL) {
        return 0;
    }
    /* The table's order IS the window's order: one list to keep in step
     * instead of two. */
    for (i = 0; i < p->n && n < max; i++) {
        if (p->pref[i].page == page) {
            out[n++] = i;
        }
    }
    return n;
}

void
tiku_trk_prefs_button_rect(int revert, int *x, int *y, int *w, int *h)
{
    if (x != NULL) {
        *x = TIKU_TRK_PREF_W - BTN_PAD - BTN_W -
             (revert ? 0 : BTN_W + 8);
    }
    if (y != NULL) { *y = TIKU_TRK_PREF_H - BTN_PAD - BTN_H; }
    if (w != NULL) { *w = BTN_W; }
    if (h != NULL) { *h = BTN_H; }
}

tiku_trk_pref_hit_t
tiku_trk_prefs_hit(tiku_trk_prefs_t *p, tiku_trk_page_t page, int x, int y,
                   int *which)
{
    int idx[32], n, i, bx, by, bw, bh;

    if (p == NULL) {
        return TIKU_TRK_PREF_HIT_NONE;
    }
    for (i = 0; i < 2; i++) {
        tiku_trk_prefs_button_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return i ? TIKU_TRK_PREF_HIT_REVERT : TIKU_TRK_PREF_HIT_DEFAULTS;
        }
    }
    for (i = 0; i < TIKU_TRK_PAGES; i++) {
        tiku_trk_prefs_page_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (which != NULL) { *which = i; }
            return TIKU_TRK_PREF_HIT_PAGE;
        }
    }
    n = tiku_trk_prefs_page_controls(p, page, idx, 32);
    for (i = 0; i < n; i++) {
        tiku_trk_prefs_control_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (which != NULL) { *which = idx[i]; }
            return TIKU_TRK_PREF_HIT_SETTING;
        }
    }
    return TIKU_TRK_PREF_HIT_NONE;
}
