/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_vdir.h - a directory whose contents are several real ones.
 *
 * The rule that makes it more than a concatenation is PRIORITY: a name is
 * supplied by the first contributing directory that has it, and the same
 * name in a later one is shadowed rather than shown twice.  Everything else
 * in the area follows from that -- what appears when a higher-priority copy
 * arrives, what is revealed when the winner is removed, and what goes when a
 * whole contributor does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_VDIR_H_
#define TIKU_TRK_VDIR_H_

#include "tiku_trk_model.h"

/** @brief How many real directories one virtual directory may merge. */
#define TIKU_TRK_VDIR_MAX_SRC 8

/** @brief One virtual directory: an ordered list of contributing paths. */
typedef struct {
    char src[TIKU_TRK_VDIR_MAX_SRC][TIKU_TRK_PATH_MAX];
    int  nsrc;
    char def[TIKU_TRK_PATH_MAX];    /* the definition file it came from  */
} tiku_trk_vdir_t;

void tiku_trk_vdir_init(tiku_trk_vdir_t *v);

/**
 * @brief Read a definition file: one contributing path per line.
 *
 * Blank lines and '#' comments are ignored, and the order of the remaining
 * lines IS the priority order -- which is why it is a list and not a set.
 *
 * @return number of paths read, or -1.
 */
int tiku_trk_vdir_load(tiku_trk_vdir_t *v, const char *deffile);

/** @brief Add one contributing path at the end (lowest priority). */
int tiku_trk_vdir_add(tiku_trk_vdir_t *v, const char *path);

/**
 * @brief Which contributor supplies @p name, or -1 when none does.
 *
 * The first that has it wins; a copy in a later one is shadowed (PVN-061).
 */
int tiku_trk_vdir_owner(const tiku_trk_vdir_t *v, tiku_trk_backend_t *b,
                        const char *name);

/**
 * @brief The real path @p name resolves to.
 *
 * @return 0 when some contributor has it.
 */
int tiku_trk_vdir_resolve(const tiku_trk_vdir_t *v, tiku_trk_backend_t *b,
                          const char *name, char *out, size_t max);

/**
 * @brief What a name looks like after a change in contributor @p which.
 *
 * Answers the three rows that are all the same question: an entry created
 * in a higher-priority directory replaces the shown one, removing the
 * winner reveals what it shadowed, and a change in a shadowed directory is
 * invisible.
 *
 * @param created Non-zero when the entry appeared, zero when it went.
 * @param out     Receives the path that should now be shown, or "" for none.
 * @return 1 when the visible row changes, 0 when nothing should happen.
 */
int tiku_trk_vdir_after_change(const tiku_trk_vdir_t *v,
                               tiku_trk_backend_t *b, const char *name,
                               int which, int created, char *out,
                               size_t max);

/**
 * @brief List the merged contents into @p out.
 *
 * Shadowed duplicates are dropped, so a name appears once however many
 * contributors hold it.
 *
 * @return how many entries, or -1.
 */
int tiku_trk_vdir_list(const tiku_trk_vdir_t *v, tiku_trk_backend_t *b,
                       tiku_trk_model_t *out, int max);

/**
 * @brief Build the virtual directory a subdirectory of this one is.
 *
 * A subdirectory merges the same-named subdirectory of every contributor
 * that has one, in the same order (PVN-066).
 */
void tiku_trk_vdir_child(const tiku_trk_vdir_t *v, tiku_trk_backend_t *b,
                         const char *name, tiku_trk_vdir_t *out);

#endif /* TIKU_TRK_VDIR_H_ */
