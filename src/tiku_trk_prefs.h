/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs.h - the settings, their file, and what a page owes.
 *
 * The settings themselves are a small typed table rather than a struct of
 * fields, because the preferences window's rules are all ABOUT the table:
 * whether any page differs from its defaults, whether any differs from what
 * it was when the window opened, and which page to show in bold.  A struct
 * would make each of those a hand-written comparison per field.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PREFS_H_
#define TIKU_TRK_PREFS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The four pages the window has (TS-058). */
typedef enum {
    TIKU_TRK_PAGE_DESKTOP = 0,
    TIKU_TRK_PAGE_WINDOWS,
    TIKU_TRK_PAGE_VOLUMES,
    TIKU_TRK_PAGE_MOUNT,
    TIKU_TRK_PAGE_NONE          /* settings with no control of their own */
} tiku_trk_page_t;

#define TIKU_TRK_PAGES 4

/** @brief What a setting holds. */
typedef enum {
    TIKU_TRK_PREF_BOOL = 0,
    TIKU_TRK_PREF_INT,
    TIKU_TRK_PREF_COLOUR
} tiku_trk_pref_type_t;

/** @brief One setting: its name in the file, its page, and three values. */
typedef struct {
    const char          *name;
    tiku_trk_pref_type_t type;
    tiku_trk_page_t      page;
    int32_t              def;       /* the compiled-in default            */
    int32_t              value;     /* what it is now                     */
    int32_t              opened;    /* what it was when the window opened */
} tiku_trk_pref_t;

/** @brief Every setting, plus whatever an older file carried. */
typedef struct {
    tiku_trk_pref_t *pref;
    int              n;
    /* Lines from the file that name no setting we know.  Kept and written
     * back so an older or newer Tracker's file round-trips rather than
     * being quietly emptied (TS-062). */
    char           **unknown;
    int              nunknown;
} tiku_trk_prefs_t;

void tiku_trk_prefs_init(tiku_trk_prefs_t *p);
void tiku_trk_prefs_free(tiku_trk_prefs_t *p);

/** @brief A setting by name, or NULL. */
tiku_trk_pref_t *tiku_trk_prefs_find(tiku_trk_prefs_t *p, const char *name);

int32_t tiku_trk_prefs_get(tiku_trk_prefs_t *p, const char *name);
void tiku_trk_prefs_set(tiku_trk_prefs_t *p, const char *name, int32_t v);

/** @brief Read the settings file; missing is not an error. */
int tiku_trk_prefs_load(tiku_trk_prefs_t *p, const char *path);

/** @brief Write it, booleans as on/off and colours as 0x%08x (TS-045). */
int tiku_trk_prefs_save(tiku_trk_prefs_t *p, const char *path);

/** @brief Take the current values as the revert baseline (TS-059). */
void tiku_trk_prefs_mark_opened(tiku_trk_prefs_t *p);

/** @brief Whether @p page has anything differing from its defaults. */
int tiku_trk_prefs_defaultable(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief Whether @p page differs from what it was when the window opened. */
int tiku_trk_prefs_revertable(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief Whether ANY page is defaultable; what enables the button. */
int tiku_trk_prefs_any_defaultable(tiku_trk_prefs_t *p);
int tiku_trk_prefs_any_revertable(tiku_trk_prefs_t *p);

/** @brief Put @p page back to its defaults, or to its opened values. */
void tiku_trk_prefs_defaults(tiku_trk_prefs_t *p, tiku_trk_page_t page);
void tiku_trk_prefs_revert(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief The name a page shows in the list. */
const char *tiku_trk_prefs_page_name(tiku_trk_page_t page);

#endif /* TIKU_TRK_PREFS_H_ */
