/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_types.h - what the system knows about a type.
 *
 * The compiled-in table can say what a type is CALLED and which icon it
 * wears; it cannot be added to, and it holds nothing about what a type is
 * worth showing columns FOR.  This is the database the rows are about: a
 * description, an icon, and the extra columns a type offers -- written
 * once on first run and read from a file afterwards, so a type learned
 * about later is not a rebuild away (AW-013, AW-015, CW-026).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TYPES_H_
#define TIKU_TRK_TYPES_H_

#include "tiku_trk_model.h"
#include "tiku_trk_poseview.h"

#define TIKU_TRK_TYPES_MAX       24
#define TIKU_TRK_TYPE_COLS_MAX    6
#define TIKU_TRK_TYPE_DESC_MAX   48

/** @brief One type, as the database describes it. */
typedef struct {
    char type[TIKU_TRK_TYPE_MAX];
    char desc[TIKU_TRK_TYPE_DESC_MAX];   /* what a Kind column says      */
    char icon[TIKU_TRK_NAME_MAX];        /* the art it wears             */
    /* The columns this type is worth showing, by TIKU_TRK_COL_*.  A type
     * that offers none is not a type without columns -- it is one whose
     * columns are the generic ones every listing has. */
    int  col[TIKU_TRK_TYPE_COLS_MAX];
    int  ncol;
    /* Columns that are an ATTRIBUTE rather than a fixed fact, written in
     * the file as "attr:<name>".  Kept apart because they cannot be a
     * column id until a view has given them one. */
    char attr[TIKU_TRK_TYPE_COLS_MAX][TIKU_TRK_ATTR_NAME_MAX];
    int  nattr;
} tiku_trk_typerec_t;

typedef struct tiku_trk_types {
    tiku_trk_typerec_t rec[TIKU_TRK_TYPES_MAX];
    int                n;
} tiku_trk_types_t;

/** @brief Start empty. */
void tiku_trk_types_init(tiku_trk_types_t *db);

/**
 * @brief Add or REPLACE one type's record.
 *
 * @param cols Comma-separated column names ("size", "modified", ...), or
 *             NULL for none.
 * @return its index, or -1.
 */
int tiku_trk_types_add(tiku_trk_types_t *db, const char *type,
                       const char *desc, const char *icon, const char *cols);

/**
 * @brief What the database says about @p type, or NULL.
 *
 * Exact first, then the SUPERTYPE: a database that had to name every
 * "text/..." there is would answer for none of them (MA-017's ranking, in
 * the place a description is looked up).
 */
const tiku_trk_typerec_t *tiku_trk_types_find(const tiku_trk_types_t *db,
                                              const char *type);

/** @brief Read a database file.  @return how many, or -1. */
int tiku_trk_types_load(tiku_trk_types_t *db, const char *path);

/** @brief Write it.  @return 0 when written. */
int tiku_trk_types_save(const tiku_trk_types_t *db, const char *path);

/**
 * @brief Install the system types, once (AW-013).
 *
 * Only when the file is absent: a database the user has edited is not
 * ours to overwrite, and re-installing on every start would undo every
 * type they added.
 *
 * @return 1 when it installed, 0 when one was already there.
 */
int tiku_trk_types_install(tiku_trk_types_t *db, const char *path);

/** @brief The column id a name stands for, or -1. */
int tiku_trk_types_column(const char *name);

#endif /* TIKU_TRK_TYPES_H_ */
