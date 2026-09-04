/*
 * TikuTracker -- the file manager for TikuOS.
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

#include "tiku_trk_deskbar_private.h"

/*
 * The strip's own rows, from the face rather than from memory.
 *
 * Every number above was measured against a 12 px face, back when that
 * was the only face there was.  The face is a preference now and the
 * Deskbar applies it to ITSELF -- so a row left at 26 pixels is one whose
 * word gets drawn at 16 px and centred into a strip too short to hold it.
 *
 * Each answers exactly the number it replaces at 12 px, and never less,
 * so the bar has not moved for anyone who left the face alone.
 */
static int
at_least(int want, int least)
{
    return (want > least) ? want : least;
}

int db_leaf_row(void)
{
    return at_least(tiku_font_plain()->height + 19, LEAF_MIN);
}

int db_clock_row(void)
{
    return at_least(tiku_font_plain()->height + 5, CLOCK_MIN);
}

int db_team_row(void)
{
    return at_least(tiku_font_plain()->height + 5, TEAM_MIN);
}

int db_win_row(void)
{
    return at_least(tiku_font_plain()->height + 4, WIN_MIN);
}
