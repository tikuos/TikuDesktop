/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_types.c - the type database: what a type is, and what it shows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_types.h"
#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_trk_types_t db;
    const tiku_trk_typerec_t *r;
    char path[128];

    snprintf(path, sizeof path, "/tmp/trk_types_%d", (int)getpid());
    (void)unlink(path);

    printf("what the database knows (AW-013)\n");
    tiku_trk_types_init(&db);
    check(tiku_trk_types_install(&db, path) == 1, "AW-013",
          "the system types are installed on a first run, when there is no "
          "database there");
    check(tiku_trk_types_install(&db, path) == 0, "AW-013",
          "and never again: a database the user has edited is not ours to "
          "overwrite");
    r = tiku_trk_types_find(&db, "text/plain");
    check(r != NULL && strcmp(r->desc, "Text file") == 0, "AW-013",
          "a type nobody named is described by its SUPERTYPE -- a database "
          "that had to name every text there is would answer for none of "
          "them");
    r = tiku_trk_types_find(&db, "text/x-source");
    check(r != NULL && strcmp(r->desc, "Source file") == 0, "AW-013",
          "and naming it beats that");
    check(tiku_trk_types_find(&db, "chemical/x-pdb") == NULL, "AW-013",
          "a type it has never heard of is answered NULL rather than by "
          "something almost right");
    check(tiku_trk_types_super(&db, "text/x-source") != NULL &&
          strcmp(tiku_trk_types_super(&db, "text/x-source")->type,
                 "text/") == 0,
          "Q-007", "a selected subtype can ask for its supertype's "
          "declarations separately");
    check(tiku_trk_types_exact(&db, "text/x-source") != NULL &&
          strcmp(tiku_trk_types_exact(&db, "text/x-source")->desc,
                 "Source file") == 0,
          "Q-007", "and its own declarations without the supertype "
          "hiding them");

    printf("\nthe documents that open for editing whatever their bits\n");
    r = tiku_trk_types_find(&db, "text/x-basic");
    check(r != NULL && r->document, "PVS-011",
          "BASIC source is a document: its handler opens it");
    r = tiku_trk_types_find(&db, "application/x-tiku-project");
    check(r != NULL && r->document, "PVS-011", "so is a Tiku project");
    r = tiku_trk_types_find(&db, "text/x-source");
    check(r != NULL && !r->document, "PVS-011",
          "and a source file that says nothing is not");
    {
        /* Through the file: the word survives a save and a load. */
        tiku_trk_types_t again;

        tiku_trk_types_init(&again);
        check(tiku_trk_types_load(&again, path) >= 0, "PVS-011",
              "the database reads back");
        r = tiku_trk_types_find(&again, "text/x-basic");
        check(r != NULL && r->document, "PVS-011",
              "with the document word kept");
    }

    printf("\nthe columns a type is worth showing (CW-026, AW-015)\n");
    r = tiku_trk_types_find(&db, "tiku-gpio");
    check(r != NULL && r->ncol == 3 &&
          r->col[0] == TIKU_TRK_COL_VALUE && r->col[1] == TIKU_TRK_COL_CAP,
          "CW-026",
          "a device node offers the two columns only it has, in the order "
          "the database gives them");
    r = tiku_trk_types_find(&db, "image/png");
    check(r != NULL && r->ncol == 2 &&
          r->col[0] == TIKU_TRK_COL_SIZE, "CW-026",
          "and an image offers what an image has, which is not the same "
          "set");
    check(tiku_trk_types_column("permissions") == TIKU_TRK_COL_PERM &&
          tiku_trk_types_column("nonesuch") == -1, "AW-015",
          "columns are named in the file rather than numbered, and a name "
          "nothing stands for is refused rather than read as column zero");

    {
        int at = tiku_trk_types_add(&db, "x-test/one", "Test", "file",
                                    "size,nonesuch,modified");

        check(at >= 0 && db.rec[at].ncol == 2 &&
              db.rec[at].col[1] == TIKU_TRK_COL_DATE, "AW-015",
              "and a column name the database does not know is left OUT "
              "rather than stored as column zero, which is the name "
              "column and would be a listing showing its name twice");
    }

    {
        const tiku_trk_typerec_t *sr = tiku_trk_types_find(&db,
                                                           "text/x-source");

        check(sr != NULL && sr->nattr == 1 &&
              strcmp(sr->attr[0], "user.trk.note") == 0, "CW-026",
              "a type can offer an ATTRIBUTE as one of its columns, named "
              "rather than numbered -- what a view will call it depends on "
              "which slot it gets, and that is the session's business");
        check(sr != NULL && sr->ncol == 3, "CW-026",
              "and its fixed columns are unaffected by having one");
    }

    printf("\nwritten and read back (AW-013)\n");
    {
        tiku_trk_types_t again;

        /* Including the one just added, so what is compared is what the
         * database holds now rather than what it was installed with. */
        check(tiku_trk_types_save(&db, path) == 0, "AW-013",
              "the database writes itself out");

        tiku_trk_types_init(&again);
        check(tiku_trk_types_load(&again, path) == db.n, "AW-013",
              "every record round-trips through the file");
        r = tiku_trk_types_find(&again, "text/x-source");
        check(r != NULL && r->nattr == 1 &&
              strcmp(r->attr[0], "user.trk.note") == 0, "CW-026",
              "an attribute column round-trips through the file by name");
        r = tiku_trk_types_find(&again, "tiku-gpio");
        check(r != NULL && r->ncol == 3 &&
              r->col[1] == TIKU_TRK_COL_CAP, "AW-013",
              "including its columns, by name");
        check(tiku_trk_types_load(&again, path) == db.n &&
              again.n == db.n, "AW-013",
              "and reading it twice leaves one record per type, not two "
              "with the lookup free to return either");
    }
    (void)unlink(path);
    printf("\n%s\n", fails ? "FAILURES" : "the type database holds");
    return fails ? 1 : 0;
}
