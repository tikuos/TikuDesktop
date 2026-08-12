/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs.c - the settings table, its file, and the page rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B TIKU_TRK_PREF_BOOL
#define I TIKU_TRK_PREF_INT
#define C TIKU_TRK_PREF_COLOUR

/** @brief Every setting, with the name it has in the file. */
static const tiku_trk_pref_t table[] = {
    { "ShowDisksIcon",           B, TIKU_TRK_PAGE_DESKTOP, 0, 0, 0 },
    { "MountVolumesOntoDesktop", B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    { "MountSharedVolumesOntoDesktop", B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    { "IntegrateNonBootBeOSDesktops", B, TIKU_TRK_PAGE_DESKTOP, 1, 0, 0 },
    { "ShowFullPathInTitleBar",  B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "SingleWindowBrowse",      B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "ShowNavigator",           B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "ShowSelectionWhenInactive", B, TIKU_TRK_PAGE_NONE,  1, 0, 0 },
    { "TransparentSelection",    B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    { "SortFolderNamesFirst",    B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
    { "HideDotFiles",            B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "TypeAheadFiltering",      B, TIKU_TRK_PAGE_WINDOWS, 0, 0, 0 },
    { "GenerateImageThumbnails", B, TIKU_TRK_PAGE_WINDOWS, 1, 0, 0 },
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

void
tiku_trk_prefs_set(tiku_trk_prefs_t *p, const char *name, int32_t v)
{
    tiku_trk_pref_t *f = tiku_trk_prefs_find(p, name);

    if (f != NULL) {
        f->value = v;
    }
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
            pr->value = (int32_t)strtol(val, NULL, 10);
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
    f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    for (i = 0; i < p->n; i++) {
        const tiku_trk_pref_t *pr = &p->pref[i];

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
tiku_trk_prefs_defaultable(tiku_trk_prefs_t *p, tiku_trk_page_t page)
{
    int i;

    if (p == NULL) {
        return 0;
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
    case TIKU_TRK_PAGE_DESKTOP: return "Desktop";
    case TIKU_TRK_PAGE_WINDOWS: return "Windows";
    case TIKU_TRK_PAGE_VOLUMES: return "Volume icons";
    case TIKU_TRK_PAGE_MOUNT:   return "Disk mount";
    default:                    return "";
    }
}
