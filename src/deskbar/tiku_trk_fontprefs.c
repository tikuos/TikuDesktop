/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fontprefs.c - the letters the interface is drawn in.
 *
 * The values only.  Where the controls sit, and what a click on one of
 * them means, belongs to the window (see the header).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_fontprefs.h"

#include "tiku_desk_font.h"
#include "tiku_trk_deskbar_prefs.h"

void
tiku_trk_fontprefs_default(tiku_trk_fontprefs_values_t *values)
{
    tiku_trk_deskbar_settings_t settings;

    if (values == NULL) {
        return;
    }
    /* One answer to "what does an undressed Deskbar look like", and it
     * lives where the rest of that answer lives. */
    tiku_trk_deskbar_settings_default(&settings);
    values->family = settings.font_family;
    values->size = settings.font_size;
}

void
tiku_trk_fontprefs_init(tiku_trk_fontprefs_t *prefs)
{
    if (prefs == NULL) {
        return;
    }
    tiku_trk_fontprefs_default(&prefs->value);
    prefs->opened = prefs->value;
}

int
tiku_trk_fontprefs_load(tiku_trk_fontprefs_t *prefs, tiku_trk_store_t *store)
{
    tiku_trk_deskbar_prefs_t record;
    int found;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_fontprefs_init(prefs);
    if (store == NULL) {
        return 0;
    }
    tiku_trk_deskbar_prefs_init(&record);
    found = (tiku_trk_deskbar_prefs_load(&record, store) == 0);
    if (found) {
        prefs->value.family = record.value.font_family;
        prefs->value.size = record.value.font_size;
    }
    prefs->opened = prefs->value;
    return found ? 1 : 0;
}

int
tiku_trk_fontprefs_save(const tiku_trk_fontprefs_t *prefs,
                        tiku_trk_store_t *store)
{
    tiku_trk_deskbar_prefs_t record;

    if (prefs == NULL || store == NULL) {
        return 0;
    }
    /* Read what is there, change the two fields this window owns, write
     * it back: everything else in the record is somebody else's. */
    tiku_trk_deskbar_prefs_init(&record);
    (void)tiku_trk_deskbar_prefs_load(&record, store);
    record.value.font_family = prefs->value.family;
    record.value.font_size = prefs->value.size;
    tiku_trk_deskbar_settings_normalize(&record.value);
    return tiku_trk_deskbar_prefs_save(&record, store) == 0;
}

void
tiku_trk_fontprefs_mark_opened(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->opened = prefs->value;
    }
}

/** @brief Whether two sets of values say the same thing. */
static int
same(const tiku_trk_fontprefs_values_t *a,
     const tiku_trk_fontprefs_values_t *b)
{
    return a->family == b->family && a->size == b->size;
}

int
tiku_trk_fontprefs_defaultable(const tiku_trk_fontprefs_t *prefs)
{
    tiku_trk_fontprefs_values_t fresh;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_fontprefs_default(&fresh);
    return same(&prefs->value, &fresh) ? 0 : 1;
}

int
tiku_trk_fontprefs_revertable(const tiku_trk_fontprefs_t *prefs)
{
    if (prefs == NULL) {
        return 0;
    }
    return same(&prefs->value, &prefs->opened) ? 0 : 1;
}

void
tiku_trk_fontprefs_defaults(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        tiku_trk_fontprefs_default(&prefs->value);
    }
}

void
tiku_trk_fontprefs_revert(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->value = prefs->opened;
    }
}

int
tiku_trk_fontprefs_set_family(tiku_trk_fontprefs_t *prefs, int family)
{
    if (prefs == NULL || family < 0 ||
        family >= tiku_desk_font_family_count() ||
        family == prefs->value.family) {
        return 0;
    }
    prefs->value.family = family;
    return 1;
}

int
tiku_trk_fontprefs_step_size(tiku_trk_fontprefs_t *prefs, int up)
{
    int next;

    if (prefs == NULL) {
        return 0;
    }
    next = prefs->value.size + (up ? TIKU_TRK_FONT_STEP : -TIKU_TRK_FONT_STEP);
    if (next < TIKU_TRK_FONT_MIN || next > TIKU_TRK_FONT_MAX) {
        return 0;               /* the ends stop rather than wrap */
    }
    prefs->value.size = next;
    return 1;
}

int
tiku_trk_fontprefs_family_count(void)
{
    return tiku_desk_font_family_count();
}

const char *
tiku_trk_fontprefs_family_name(int family)
{
    return tiku_desk_font_family_name(family);
}
