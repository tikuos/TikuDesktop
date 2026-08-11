/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fs.c - file-operation rules, checked against their FEATURES rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_fs.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-8s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Cancel after this many chunk checks; 0 means never. */
static int cancel_after;
static int cancel_seen;

static int
cancel_hook(void *ctx)
{
    (void)ctx;
    if (cancel_after == 0) {
        return 0;
    }
    return (++cancel_seen >= cancel_after);
}

static int
there(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0);
}

static void
put(const char *p, const char *text)
{
    FILE *f = fopen(p, "w");
    if (f != NULL) { fputs(text, f); (void)fclose(f); }
}

/** @brief Whether the file at @p p holds exactly @p text. */
static int
holds(const char *p, const char *text)
{
    char  buf[128];
    FILE *f = fopen(p, "r");
    size_t n;

    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

static void
run_to_end(tiku_trk_fs_t *fs)
{
    int guard = 0;
    while (tiku_trk_fs_busy(fs) && guard++ < 1000) {
        if (tiku_trk_fs_step(fs, 4) == 0) {
            /* An error stops the job with a question up; standing in for
             * the user clicking OK keeps the rest of a suite about the
             * rule it is testing rather than about the alert. */
            if (tiku_trk_fs_progress(fs)->error_ask) {
                tiku_trk_fs_resolve_error(fs, 1);
                continue;
            }
            break;                    /* stopped on a conflict */
        }
    }
}

int
main(void)
{
    const char *A = "/tmp/trk_fs_a", *B = "/tmp/trk_fs_b";
    char pa[256], pb[256], name[128], err[256];
    tiku_trk_backend_t *b = tiku_trk_backend_posix_open();
    tiku_trk_fs_t *fs = tiku_trk_fs_new(b);
    const char *list[2];

    (void)mkdir(A, 0755);
    (void)mkdir(B, 0755);
    snprintf(pa, sizeof pa, "%s/note.txt", A);
    put(pa, "one");

    printf("file operations\n");

    /* --- duplicate naming (FS-006) -------------------------------------- */
    check(tiku_trk_fs_duplicate_name(fs, A, "note.txt", name,
                                     sizeof name) == 0 &&
          strcmp(name, "note.txt copy") == 0, "FS-006",
          "the first duplicate is '<name> copy'");
    put("/tmp/trk_fs_a/note.txt copy", "one");
    check(tiku_trk_fs_duplicate_name(fs, A, "note.txt", name,
                                     sizeof name) == 0 &&
          strcmp(name, "note.txt copy 2") == 0, "FS-006",
          "the next is 'copy 2'");
    check(tiku_trk_fs_duplicate_name(fs, A, "note.txt copy", name,
                                     sizeof name) == 0 &&
          strcmp(name, "note.txt copy 2") == 0, "FS-006",
          "duplicating a 'copy' gives 'copy 2', never 'copy copy'");
    (void)unlink("/tmp/trk_fs_a/note.txt copy");

    /* --- link naming (FS-009) ------------------------------------------- */
    check(tiku_trk_fs_link_name(fs, B, "note.txt", name, sizeof name) == 0 &&
          strcmp(name, "note.txt") == 0, "FS-009",
          "a link into another folder keeps the original name");
    check(tiku_trk_fs_link_name(fs, A, "note.txt", name, sizeof name) == 0 &&
          strcmp(name, "note.txt link") == 0, "FS-009",
          "and only becomes '<name> link' when the name is taken");

    /* --- drop semantics (FS-001, FS-002) -------------------------------- */
    check(tiku_trk_fs_drop_action(fs, pa, B, 0) == TIKU_TRK_OP_MOVE,
          "FS-001", "a drop within one volume moves");
    check(tiku_trk_fs_drop_action(fs, pa, B, 1) == TIKU_TRK_OP_COPY,
          "FS-002", "an explicit copy overrides that");

    /* --- copy, and what it leaves behind -------------------------------- */
    list[0] = pa;
    check(tiku_trk_fs_copy(fs, list, 1, B, TIKU_TRK_CONFLICT_ASK) == 0,
          "FS", "a copy starts");
    run_to_end(fs);
    snprintf(pb, sizeof pb, "%s/note.txt", B);
    check(there(pb) && there(pa), "FS", "the copy exists and the original stays");
    check(tiku_trk_fs_progress(fs)->done == 1, "FS", "progress counted it");

    /* --- a collision stops and asks (FS-conflict) ----------------------- */
    check(tiku_trk_fs_copy(fs, list, 1, B, TIKU_TRK_CONFLICT_ASK) == 0,
          "FS", "a second copy starts");
    run_to_end(fs);
    check(tiku_trk_fs_progress(fs)->conflict == 1, "FS",
          "it STOPS on the name collision instead of guessing");
    tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_RENAME);
    run_to_end(fs);
    check(there("/tmp/trk_fs_b/note.txt copy"), "FS",
          "answering 'rename' invents a fresh name");

    /* --- skip-in-place (FS-005) ----------------------------------------- */
    check(tiku_trk_fs_copy(fs, list, 1, A, TIKU_TRK_CONFLICT_ASK) == 0,
          "FS-005", "copying into its own folder starts");
    run_to_end(fs);
    check(tiku_trk_fs_progress(fs)->skipped == 1, "FS-005",
          "and the item is silently skipped, not copied onto itself");

    /* --- duplicate lands in the item's OWN parent (FS-008) -------------- */
    check(tiku_trk_fs_duplicate(fs, list, 1) == 0, "FS-008",
          "duplicate starts");
    run_to_end(fs);
    check(there("/tmp/trk_fs_a/note.txt copy"), "FS-008",
          "the duplicate is beside the original, not in some destination");

    /* --- move, then undo ------------------------------------------------ */
    {
        const char *mv[1];
        char moved[256];

        mv[0] = "/tmp/trk_fs_a/note.txt copy";
        snprintf(moved, sizeof moved, "%s/note.txt copy", B);
        check(tiku_trk_fs_move(fs, mv, 1, B, TIKU_TRK_CONFLICT_RENAME) == 0,
              "FS", "a move starts");
        run_to_end(fs);
        check(!there(mv[0]), "FS", "the original is gone after a move");
        check(tiku_trk_fs_can_undo(fs, name, sizeof name) &&
              strcmp(name, "Move") == 0, "FS-undo",
              "the move is offered as undoable, by name");
        check(tiku_trk_fs_undo(fs, err, sizeof err) >= 1, "FS-undo",
              "undo reverses it");
        check(there(mv[0]), "FS-undo", "and the item is back where it was");
    }

    /* --- rename --------------------------------------------------------- */
    check(!tiku_trk_fs_name_ok("", err, sizeof err), "MA-rename",
          "an empty name is refused");
    check(!tiku_trk_fs_name_ok("a/b", err, sizeof err), "MA-rename",
          "a name with a slash is refused");
    check(!tiku_trk_fs_name_ok("..", err, sizeof err), "MA-rename",
          "'..' is refused");
    check(tiku_trk_fs_rename(fs, pa, "renamed.txt", err, sizeof err) == 0 &&
          there("/tmp/trk_fs_a/renamed.txt"), "MA-rename",
          "a valid rename works");
    check(tiku_trk_fs_rename(fs, "/tmp/trk_fs_a/renamed.txt",
                             "note.txt copy", err, sizeof err) != 0,
          "MA-rename", "renaming onto an existing name is refused");
    check(tiku_trk_fs_undo(fs, err, sizeof err) >= 1 && there(pa),
          "FS-undo", "a rename undoes too");

    /* --- delete is final, and says so ----------------------------------- */
    {
        const char *del[1];
        char victim[256];

        snprintf(victim, sizeof victim, "%s/doomed.txt", A);
        put(victim, "x");
        del[0] = victim;
        check(tiku_trk_fs_trash(fs, del, 1) == 0, "FS-trash", "trash starts");
        run_to_end(fs);
        check(!there(victim), "FS-trash", "the item is gone");
    }




    /* --- what the collision question OFFERS (FS-017, FS-018, FS-019) ---- */
    {
        char one[600], two[600], adir[600];
        const char *sel[2];

        /* One file-versus-file collision: the short question. */
        snprintf(one, sizeof one, "%s/solo.txt", A);
        put(one, "a");
        snprintf(two, sizeof two, "%s/solo.txt", B);
        put(two, "b");
        sel[0] = one;
        (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_ASK);
        (void)tiku_trk_fs_step(fs, 4);
        check(tiku_trk_fs_progress(fs)->conflict &&
              !tiku_trk_fs_progress(fs)->conflict_many, "FS-018",
              "a lone file-versus-file collision asks the short question");
        tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_SKIP);
        run_to_end(fs);

        /* A FOLDER collision always asks the four-way one, even alone. */
        snprintf(adir, sizeof adir, "%s/samedir", A);
        (void)mkdir(adir, 0755);
        {
            char d2[600];

            snprintf(d2, sizeof d2, "%s/samedir", B);
            (void)mkdir(d2, 0755);
        }
        sel[0] = adir;
        (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_ASK);
        (void)tiku_trk_fs_step(fs, 4);
        check(tiku_trk_fs_progress(fs)->conflict &&
              tiku_trk_fs_progress(fs)->conflict_many &&
              tiku_trk_fs_progress(fs)->conflict_isdir, "FS-019",
              "a folder collision asks the four-way question even when it "
              "is the only one");
        tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_SKIP);
        run_to_end(fs);

        /* More than one collision: the four-way question, and "all" is
         * what stops it asking again (FS-017). */
        {
            char c1[600], c2[600];

            snprintf(c1, sizeof c1, "%s/both1.txt", A);
            put(c1, "1");
            snprintf(c2, sizeof c2, "%s/both2.txt", A);
            put(c2, "2");
            {
                char d1[600], d2[600];

                snprintf(d1, sizeof d1, "%s/both1.txt", B);
                put(d1, "x");
                snprintf(d2, sizeof d2, "%s/both2.txt", B);
                put(d2, "y");
            }
            sel[0] = c1;
            sel[1] = c2;
            (void)tiku_trk_fs_copy(fs, sel, 2, B, TIKU_TRK_CONFLICT_ASK);
            (void)tiku_trk_fs_step(fs, 4);
            check(tiku_trk_fs_progress(fs)->conflict_many, "FS-017",
                  "several collisions ask the four-way question");
            tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_SKIP_ALL);
            run_to_end(fs);
            check(tiku_trk_fs_progress(fs)->skipped == 2 &&
                  !tiku_trk_fs_progress(fs)->conflict, "FS-017",
                  "\"skip all\" answers the rest without asking again");
        }

        /* And a plain "skip" is NOT sticky: the next one asks. */
        {
            char c1[600], c2[600];

            snprintf(c1, sizeof c1, "%s/both1.txt", A);
            snprintf(c2, sizeof c2, "%s/both2.txt", A);
            sel[0] = c1;
            sel[1] = c2;
            (void)tiku_trk_fs_copy(fs, sel, 2, B, TIKU_TRK_CONFLICT_ASK);
            (void)tiku_trk_fs_step(fs, 4);
            tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_SKIP);
            (void)tiku_trk_fs_step(fs, 4);
            check(tiku_trk_fs_progress(fs)->conflict, "FS-019",
                  "a plain \"skip\" answers one collision and the next "
                  "still asks");
            tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_SKIP_ALL);
            run_to_end(fs);
        }

        /* Replace REMOVES the old file first (FS-021). */
        {
            char c1[600], d1[600];

            snprintf(c1, sizeof c1, "%s/both1.txt", A);
            snprintf(d1, sizeof d1, "%s/both1.txt", B);
            put(d1, "old-and-longer-than-the-new-one");
            sel[0] = c1;
            (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_REPLACE);
            run_to_end(fs);
            check(holds(d1, "1"), "FS-021",
                  "replacing leaves the new contents alone, with none of "
                  "the longer file it replaced trailing after them");

            /* A destination that will not go stops the item and says so. */
            {
                char lockdir[600], locked[700];

                snprintf(lockdir, sizeof lockdir, "%s/nodelete", B);
                (void)mkdir(lockdir, 0755);
                snprintf(locked, sizeof locked, "%s/both1.txt", lockdir);
                put(locked, "x");
                (void)chmod(lockdir, 0555);   /* no unlink in here */
                sel[0] = c1;
                (void)tiku_trk_fs_copy(fs, sel, 1, lockdir,
                                       TIKU_TRK_CONFLICT_REPLACE);
                (void)tiku_trk_fs_step(fs, 4);
                check(tiku_trk_fs_progress(fs)->error_ask &&
                      strstr(tiku_trk_fs_progress(fs)->error,
                             "problem trying to replace") != NULL,
                      "FS-021",
                      "and a file that cannot be removed is reported rather "
                      "than written over");
                tiku_trk_fs_resolve_error(fs, 1);
                run_to_end(fs);
                (void)chmod(lockdir, 0755);
                (void)remove(locked);
                (void)rmdir(lockdir);
            }
        }

        /* "Replace" is one-shot the same way "Skip" is. */
        {
            char c1[600], c2[600], d1[600], d2[600];

            snprintf(c1, sizeof c1, "%s/both1.txt", A);
            snprintf(c2, sizeof c2, "%s/both2.txt", A);
            snprintf(d1, sizeof d1, "%s/both1.txt", B);
            snprintf(d2, sizeof d2, "%s/both2.txt", B);
            put(d1, "x");
            put(d2, "y");
            sel[0] = c1;
            sel[1] = c2;
            (void)tiku_trk_fs_copy(fs, sel, 2, B, TIKU_TRK_CONFLICT_ASK);
            (void)tiku_trk_fs_step(fs, 4);
            tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_REPLACE);
            (void)tiku_trk_fs_step(fs, 4);
            check(tiku_trk_fs_progress(fs)->conflict, "FS-019",
                  "a plain \"replace\" overwrites one and the next still "
                  "asks");
            tiku_trk_fs_resolve(fs, TIKU_TRK_CONFLICT_REPLACE_ALL);
            run_to_end(fs);
            check(holds(d1, "1") && holds(d2, "2"), "FS-017",
                  "and both replaced files really hold the source's "
                  "contents -- the one-shot answer and the sticky one");
        }
    }

    /* --- a folder "replaced" is MERGED, never emptied first (FS-020) ---- */
    {
        char sdir[600], ddir[600], keep[700], mine[700], theirs[700];
        const char *sel[1];

        snprintf(sdir, sizeof sdir, "%s/tree", A);
        (void)mkdir(sdir, 0755);
        snprintf(mine, sizeof mine, "%s/tree/shared.txt", A);
        put(mine, "new");
        snprintf(theirs, sizeof theirs, "%s/tree/only-source.txt", A);
        put(theirs, "s");

        snprintf(ddir, sizeof ddir, "%s/tree", B);
        (void)mkdir(ddir, 0755);
        snprintf(keep, sizeof keep, "%s/tree/only-dest.txt", B);
        put(keep, "d");
        {
            char clash[700];

            snprintf(clash, sizeof clash, "%s/tree/shared.txt", B);
            put(clash, "old");
        }

        sel[0] = sdir;
        (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_REPLACE);
        run_to_end(fs);

        check(there(keep), "FS-020",
              "a child of the destination that the source knows nothing "
              "about SURVIVES -- replacing a folder is a merge, not a "
              "delete and recopy");
        {
            char clash[700];
            char got[16];
            FILE *f;

            snprintf(clash, sizeof clash, "%s/tree/shared.txt", B);
            f = fopen(clash, "r");
            got[0] = '\0';
            if (f != NULL) {
                size_t n = fread(got, 1, sizeof got - 1, f);
                got[n] = '\0';
                (void)fclose(f);
            }
            check(strcmp(got, "new") == 0, "FS-020",
                  "a same-named child IS replaced");
        }
        {
            char brought[700];

            snprintf(brought, sizeof brought, "%s/tree/only-source.txt", B);
            check(there(brought), "FS-020",
                  "and one only the source had is brought over");
        }
    }

    /* --- a same-volume move of a folder merges too (FS-029) ------------- */
    {
        char sdir[600], dkeep[700], moved[700];
        const char *sel[1];

        snprintf(sdir, sizeof sdir, "%s/mtree", A);
        (void)mkdir(sdir, 0755);
        snprintf(moved, sizeof moved, "%s/mtree/carried.txt", A);
        put(moved, "c");
        {
            char d[600];

            snprintf(d, sizeof d, "%s/mtree", B);
            (void)mkdir(d, 0755);
        }
        snprintf(dkeep, sizeof dkeep, "%s/mtree/stays.txt", B);
        put(dkeep, "s");

        sel[0] = sdir;
        (void)tiku_trk_fs_move(fs, sel, 1, B, TIKU_TRK_CONFLICT_REPLACE);
        run_to_end(fs);
        {
            char arrived[700];

            snprintf(arrived, sizeof arrived, "%s/mtree/carried.txt", B);
            check(there(arrived) && there(dkeep), "FS-029",
                  "a move onto a folder of the same name merges: what was "
                  "there stays and what moved arrives");
            check(!there(moved) && !there(sdir), "FS-029",
                  "and the emptied source folder goes, which is what makes "
                  "it a move");
        }
    }

    /* --- the refusals: operations that would destroy the unnamed -------- */
    {
        char inner[600], deep[700], afile[600], adir[600], why[220];
        const char *sel[1];

        snprintf(inner, sizeof inner, "%s/holder", A);
        (void)mkdir(inner, 0755);
        snprintf(deep, sizeof deep, "%s/holder/under", A);
        (void)mkdir(deep, 0755);

        check(tiku_trk_fs_check_move(fs, inner, inner, why, sizeof why) ==
              TIKU_TRK_REFUSE_INTO_SELF && strstr(why, "itself") != NULL,
              "FS-023", "a folder cannot be moved into itself");
        check(tiku_trk_fs_check_move(fs, inner, deep, why, sizeof why) ==
              TIKU_TRK_REFUSE_INTO_SELF, "FS-023",
              "nor into one of its own sub-folders");
        check(tiku_trk_fs_check_move(fs, deep, B, why, sizeof why) ==
              TIKU_TRK_REFUSE_NONE, "FS-023",
              "but a sub-folder may be moved somewhere else entirely");
        {
            /* A sibling whose name merely EXTENDS the other's is not a
             * sub-folder: "/a/hold" is not inside "/a/holder", and a
             * containment test done on text alone would refuse this. */
            char sibling[600];

            snprintf(sibling, sizeof sibling, "%s/hold", A);
            (void)mkdir(sibling, 0755);
            check(tiku_trk_fs_check_move(fs, sibling, inner, why,
                                         sizeof why) ==
                  TIKU_TRK_REFUSE_NONE, "FS-023",
                  "and \"hold\" may be moved into \"holder\" -- a name "
                  "that starts the same is not a parent");
            (void)rmdir(sibling);
        }

        /* Replacing a folder with one of its own children (FS-024). */
        check(tiku_trk_fs_check_move(fs, deep, A, why, sizeof why) ==
              TIKU_TRK_REFUSE_NONE, "FS-024",
              "moving a child up to a folder with no name clash is fine");
        {
            char sibling[700];

            snprintf(sibling, sizeof sibling, "%s/holder/holder", A);
            (void)mkdir(sibling, 0755);
            check(tiku_trk_fs_check_move(fs, sibling, A, why, sizeof why) ==
                  TIKU_TRK_REFUSE_BY_CHILD &&
                  strstr(why, "sub-folder") != NULL, "FS-024",
                  "but a folder may not be replaced by its own sub-folder");
            (void)rmdir(sibling);
        }

        /* A kind mismatch is refused outright (FS-025). */
        snprintf(afile, sizeof afile, "%s/thing", A);
        put(afile, "x");
        snprintf(adir, sizeof adir, "%s/thing", B);
        (void)mkdir(adir, 0755);
        check(tiku_trk_fs_check_move(fs, afile, B, why, sizeof why) ==
              TIKU_TRK_REFUSE_KIND &&
              strstr(why, "with a file") != NULL, "FS-025",
              "a file may not replace a folder, and the message says which "
              "way round it is");
        (void)rmdir(adir);
        (void)mkdir(adir, 0755);
        {
            char dirsrc[600], filedst[600];

            snprintf(dirsrc, sizeof dirsrc, "%s/kind", A);
            (void)mkdir(dirsrc, 0755);
            snprintf(filedst, sizeof filedst, "%s/kind", B);
            put(filedst, "y");
            check(tiku_trk_fs_check_move(fs, dirsrc, B, why, sizeof why) ==
                  TIKU_TRK_REFUSE_KIND &&
                  strstr(why, "with a folder") != NULL, "FS-025",
                  "and a folder may not replace a file");
            (void)unlink(filedst);
            (void)rmdir(dirsrc);
        }
        (void)unlink(afile);
        (void)rmdir(adir);

        /* The queue builder enforces it, so no caller can go round it. */
        sel[0] = inner;
        (void)tiku_trk_fs_copy(fs, sel, 1, deep, TIKU_TRK_CONFLICT_ASK);
        run_to_end(fs);
        check(tiku_trk_fs_progress(fs)->skipped == 1 &&
              tiku_trk_fs_progress(fs)->done == 0, "FS-023",
              "and a copy that would do it is refused before it starts, "
              "not part-way through");
        (void)rmdir(deep);
        (void)rmdir(inner);
    }

    /* --- the Trash: a move with a memory (FS-038, FS-039, FS-040) -------- */
    {
        const char *home = getenv("HOME");
        char work[512], victim[600], trash[600], origin[600], err[256];
        const char *sel[1];

        if (home == NULL || home[0] == '\0') {
            printf("  (no HOME: the Trash cases need a writable volume)\n");
        } else {
            snprintf(work, sizeof work, "%s/.tiku_trk_fs_test", home);
            (void)mkdir(work, 0755);
            /* Start from an empty Trash.  A run that crashed half way
             * through leaves items behind, and a count that included them
             * would fail for a reason that has nothing to do with the
             * rule. */
            (void)tiku_trk_fs_empty_trash(fs);
            run_to_end(fs);
            snprintf(victim, sizeof victim, "%s/keepsake.txt", work);
            put(victim, "x");
            sel[0] = victim;

            check(tiku_trk_fs_delete_question(fs, victim, 0) ==
                  TIKU_TRK_DELETE_ASK, "FS-046",
                  "a file on a volume with a Trash gets the three-way "
                  "question");
            check(tiku_trk_fs_delete_question(fs, victim, 1) ==
                  TIKU_TRK_DELETE_STRAIGHT, "FS-046",
                  "and Shift says the user has already decided");

            check(tiku_trk_fs_trash(fs, sel, 1) == 0, "FS-038",
                  "trashing starts");
            run_to_end(fs);
            check(!there(victim), "FS-038", "the item leaves its folder");
            check(tiku_trk_fs_trash_dir(fs, victim, trash, sizeof trash) == 0,
                  "FS-038", "and the Trash exists on its own volume");
            {
                char in_trash[700];

                snprintf(in_trash, sizeof in_trash, "%s/keepsake.txt", trash);
                check(there(in_trash), "FS-038",
                      "the item is IN it -- trashing is a move, not a "
                      "deletion under another name");
                check(tiku_trk_fs_delete_question(fs, in_trash, 0) ==
                      TIKU_TRK_DELETE_STRAIGHT, "FS-047",
                      "and inside the Trash the question is not asked "
                      "again: deleting is all that is left");
                check(tiku_trk_fs_original_path(fs, in_trash, origin,
                                                sizeof origin) &&
                      strcmp(origin, victim) == 0, "FS-039",
                      "it remembers where it came from");

                /* Something in the way is not overwritten (FS-040). */
                put(victim, "y");
                sel[0] = in_trash;
                check(tiku_trk_fs_restore(fs, sel, 1, err, sizeof err) == 0 &&
                      err[0] != '\0', "FS-040",
                      "restoring onto an occupied name is refused, not "
                      "silently destructive");
                (void)unlink(victim);
                check(tiku_trk_fs_restore(fs, sel, 1, err, sizeof err) == 1 &&
                      there(victim), "FS-040",
                      "and with the way clear it goes back where it was");
            }

            /* Two of a name both survive: the second is renamed, as a
             * duplicate is (FS-039). */
            {
                char other[600], in_trash[700];

                snprintf(other, sizeof other, "%s/second", work);
                (void)mkdir(other, 0755);
                snprintf(other, sizeof other, "%s/second/keepsake.txt", work);
                put(other, "z");
                sel[0] = victim;
                (void)tiku_trk_fs_trash(fs, sel, 1);
                run_to_end(fs);
                sel[0] = other;
                (void)tiku_trk_fs_trash(fs, sel, 1);
                run_to_end(fs);
                snprintf(in_trash, sizeof in_trash,
                         "%s/keepsake copy.txt", trash);
                {
                    int n = 0;
                    DIR *d = opendir(trash);
                    struct dirent *e;

                    while (d != NULL && (e = readdir(d)) != NULL) {
                        if (strncmp(e->d_name, "keepsake", 8) == 0) { n++; }
                    }
                    if (d != NULL) { (void)closedir(d); }
                    check(n == 2, "FS-039",
                          "two files of one name are both kept, the second "
                          "renamed rather than overwriting the first");
                }
            }
            check(tiku_trk_fs_empty_trash(fs) >= 2, "FS-048",
                  "emptying the Trash removes what is in it");
            run_to_end(fs);
            (void)rmdir(work);
        }
    }


    /* --- cancelling (FS-033, FS-034) ------------------------------------ */
    printf("\ncancelling a job\n");
    {
        char big[600], made[600];
        const char *sel[1];

        snprintf(big, sizeof big, "%s/large.bin", A);
        put(big, "0123456789");
        sel[0] = big;
        snprintf(made, sizeof made, "%s/large.bin", B);

        cancel_seen = 0;
        /* The second check: the first is the one between items, so this
         * one lands INSIDE the copy, after fopen has created the
         * destination and before a byte of it is right. */
        cancel_after = 2;
        tiku_trk_fs_set_cancel_check(fs, cancel_hook, NULL);
        (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_ASK);
        run_to_end(fs);
        check(!there(made), "FS-033",
              "cancelling during a file copy removes the half-written "
              "destination rather than leaving a truncated file under the "
              "real one's name");
        check(tiku_trk_fs_progress(fs)->cancelled, "FS-033",
              "and the job says it was cancelled");
        check(there(big), "FS-033", "the source is untouched");

        /* A FOLDER copy keeps what it managed. */
        {
            char dir[500], c1[600], c2[600], c3[600], out[500];
            int i;

            snprintf(dir, sizeof dir, "%s/manyfiles", A);
            (void)mkdir(dir, 0755);
            snprintf(c1, sizeof c1, "%s/a.txt", dir);
            snprintf(c2, sizeof c2, "%s/b.txt", dir);
            snprintf(c3, sizeof c3, "%s/c.txt", dir);
            put(c1, "a");
            put(c2, "b");
            put(c3, "c");

            cancel_seen = 0;
            /* One check between items, then two per file (a chunk and
             * the end of it): six lands inside the third child. */
            cancel_after = 6;
            sel[0] = dir;
            (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_ASK);
            run_to_end(fs);
            snprintf(out, sizeof out, "%s/manyfiles", B);
            check(there(out), "FS-034",
                  "cancelling a folder copy leaves the folder it had "
                  "already made -- only the file in hand is cleaned up");
            {
                int kept = 0;
                char probe[700];

                for (i = 0; i < 3; i++) {
                    snprintf(probe, sizeof probe, "%s/%c.txt", out,
                             'a' + i);
                    kept += there(probe);
                }
                check(kept == 2, "FS-034",
                      "with the children it had written still there and "
                      "the rest never started");
            }
        }
        cancel_after = 0;
        tiku_trk_fs_set_cancel_check(fs, NULL, NULL);
    }

    /* --- an error asks whether to continue (FS-035) --------------------- */
    {
        char bad[600], good[600], out[600];
        const char *sel[2];

        /* A source that cannot be read: the copy of it fails, the copy of
         * the other one should still be offered. */
        snprintf(bad, sizeof bad, "%s/unreadable.txt", A);
        put(bad, "secret");
        (void)chmod(bad, 0000);
        snprintf(good, sizeof good, "%s/readable.txt", A);
        put(good, "fine");
        sel[0] = bad;
        sel[1] = good;

        (void)tiku_trk_fs_copy(fs, sel, 2, B, TIKU_TRK_CONFLICT_ASK);
        (void)tiku_trk_fs_step(fs, 4);
        check(tiku_trk_fs_progress(fs)->error_ask, "FS-035",
              "a file that will not copy stops the job and asks");
        check(strstr(tiku_trk_fs_progress(fs)->error, "unreadable.txt") !=
              NULL &&
              strstr(tiku_trk_fs_progress(fs)->error, "Error copying") !=
              NULL,
              "FS-035", "naming the file and what went wrong");
        check(tiku_trk_fs_progress(fs)->error_can_continue, "FS-035",
              "and offers to carry on");

        tiku_trk_fs_resolve_error(fs, 1);
        run_to_end(fs);
        snprintf(out, sizeof out, "%s/readable.txt", B);
        check(there(out) && tiku_trk_fs_progress(fs)->failed == 1,
              "FS-035", "continuing counts the failure and copies the rest");

        /* Answering "cancel" abandons what is left. */
        (void)remove(out);
        (void)tiku_trk_fs_copy(fs, sel, 2, B, TIKU_TRK_CONFLICT_ASK);
        (void)tiku_trk_fs_step(fs, 4);
        tiku_trk_fs_resolve_error(fs, 0);
        run_to_end(fs);
        check(!there(out) && tiku_trk_fs_progress(fs)->cancelled,
              "FS-035", "and cancelling stops the rest of the job dead");

        (void)chmod(bad, 0644);
        (void)remove(bad);
    }

    /* --- the undo stack goes deeper than one (FS-063) ------------------- */
    printf("\nundo, redo and new folders\n");
    {
        char f1[600], f2[600], n1[600], n2[600], what[64];

        snprintf(f1, sizeof f1, "%s/u1.txt", A);
        snprintf(f2, sizeof f2, "%s/u2.txt", A);
        put(f1, "one");
        put(f2, "two");
        snprintf(n1, sizeof n1, "%s/u1-renamed.txt", A);
        snprintf(n2, sizeof n2, "%s/u2-renamed.txt", A);

        (void)tiku_trk_fs_rename(fs, f1, "u1-renamed.txt", NULL, 0);
        (void)tiku_trk_fs_rename(fs, f2, "u2-renamed.txt", NULL, 0);
        check(tiku_trk_fs_undo(fs, NULL, 0) == 1 && there(f2) && !there(f1),
              "FS-063", "undo reverses the LAST operation");
        check(tiku_trk_fs_can_undo(fs, what, sizeof what) &&
              strcmp(what, "Rename") == 0,
              "FS-063", "and the one before it is still there to undo");
        check(tiku_trk_fs_undo(fs, NULL, 0) == 1 && there(f1),
              "FS-063", "which undoes too -- the stack is not one deep");

        /* Redo puts them back, newest first. */
        check(tiku_trk_fs_can_redo(fs, what, sizeof what) &&
              strcmp(what, "Rename") == 0,
              "FS-063", "an undone operation can be redone");
        check(tiku_trk_fs_redo(fs, NULL, 0) == 1 && there(n1),
              "FS-063", "and redo replays it");
        (void)tiku_trk_fs_redo(fs, NULL, 0);
        check(there(n2), "FS-063", "both come back");
        check(!tiku_trk_fs_can_redo(fs, NULL, 0),
              "FS-063", "and then there is nothing left to redo");

        /* Doing something NEW makes the redo history unreachable. */
        (void)tiku_trk_fs_undo(fs, NULL, 0);
        check(tiku_trk_fs_can_redo(fs, NULL, 0),
              "FS-063", "an undo leaves a redo waiting");
        (void)tiku_trk_fs_rename(fs, n1, "u1-again.txt", NULL, 0);
        check(!tiku_trk_fs_can_redo(fs, NULL, 0),
              "FS-063", "and a new operation wipes it");
    }

    /* The stack is bounded at 20 and drops the OLDEST. */
    {
        char p[600], q[600];
        int i, drained = 0;

        for (i = 0; i < 25; i++) {
            snprintf(p, sizeof p, "%s/deep%d.txt", A, i);
            put(p, "x");
            snprintf(q, sizeof q, "deep%d-r.txt", i);
            (void)tiku_trk_fs_rename(fs, p, q, NULL, 0);
        }
        while (tiku_trk_fs_can_undo(fs, NULL, 0)) {
            (void)tiku_trk_fs_undo(fs, NULL, 0);
            drained++;
        }
        check(drained == 20, "FS-063",
              "twenty-five operations leave exactly twenty on the stack");
        snprintf(p, sizeof p, "%s/deep0.txt", A);
        snprintf(q, sizeof q, "%s/deep0-r.txt", A);
        check(there(q) && !there(p), "FS-063",
              "and the five it dropped are the OLDEST -- the first rename "
              "stayed done");
        snprintf(p, sizeof p, "%s/deep24.txt", A);
        check(there(p), "FS-063",
              "while the newest was still there to be undone");
    }

    /* --- undoing a copy removes what the copy made (FS-064) ------------- */
    {
        char src[600], made[600];

        snprintf(src, sizeof src, "%s/tocopy.txt", A);
        put(src, "c");
        {
            const char *sel[1];

            sel[0] = src;
            (void)tiku_trk_fs_copy(fs, sel, 1, B, TIKU_TRK_CONFLICT_ASK);
            run_to_end(fs);
        }
        snprintf(made, sizeof made, "%s/tocopy.txt", B);
        check(there(made), "FS-064", "the copy landed");
        (void)tiku_trk_fs_undo(fs, NULL, 0);
        check(!there(made) && there(src), "FS-064",
              "undoing a copy deletes the copy and leaves the original");
        (void)tiku_trk_fs_redo(fs, NULL, 0);
        check(there(made), "FS-064", "and redo copies it again");
    }

    /* --- undoing a move to the Trash puts it back (FS-065) -------------- */
    {
        const char *home = getenv("HOME");
        char work[512], t[600], what[64];
        const char *sel[1];

        /* Under HOME, not /tmp: the Trash is resolved per filesystem, and
         * on this host /tmp and HOME are different ones. */
        if (home != NULL && home[0] != '\0') {
            snprintf(work, sizeof work, "%s/.tiku_trk_fs_test", home);
            (void)mkdir(work, 0755);
            snprintf(t, sizeof t, "%s/totrash.txt", work);
            put(t, "t");
            sel[0] = t;
            (void)tiku_trk_fs_trash(fs, sel, 1);
            run_to_end(fs);
            check(!there(t) && tiku_trk_fs_progress(fs)->done == 1,
                  "FS-065", "the item went to the Trash");
            check(tiku_trk_fs_can_undo(fs, what, sizeof what) &&
                  strcmp(what, "Move to Trash") == 0,
                  "FS-065", "which is an undoable move, named as one");
            (void)tiku_trk_fs_undo(fs, NULL, 0);
            check(there(t), "FS-065", "and undo brings it back");
            (void)remove(t);
            (void)rmdir(work);
        }
    }

    /* --- New folder, and its naming (FS-066, FS-079) -------------------- */
    {
        char made[600], name[64];
        int i;

        tiku_trk_fs_new_folder_name(fs, B, name, sizeof name);
        check(strcmp(name, "New folder") == 0,
              "FS-079", "the first new folder is called \"New folder\"");
        check(tiku_trk_fs_new_folder(fs, B, made, sizeof made, NULL, 0) == 0 &&
              there(made), "FS-066", "and creating it makes it");
        tiku_trk_fs_new_folder_name(fs, B, name, sizeof name);
        check(strcmp(name, "New folder 2") == 0,
              "FS-079", "the second is \"New folder 2\", with a space");

        for (i = 2; i <= 9; i++) {
            (void)tiku_trk_fs_new_folder(fs, B, NULL, 0, NULL, 0);
        }
        tiku_trk_fs_new_folder_name(fs, B, name, sizeof name);
        check(strcmp(name, "New folder10") == 0,
              "FS-079", "and the tenth loses the space -- \"New folder10\", "
              "the original's own off-by-one, kept so both agree on which "
              "folder is which");

        (void)tiku_trk_fs_new_folder(fs, B, made, sizeof made, NULL, 0);
        check(there(made), "FS-066", "a new folder is there");
        (void)tiku_trk_fs_undo(fs, NULL, 0);
        check(!there(made), "FS-066", "and undoing New folder deletes it");
        (void)tiku_trk_fs_redo(fs, NULL, 0);
        check(there(made), "FS-066", "redo makes it again");
    }

    tiku_trk_fs_free(fs);
    tiku_trk_backend_close(b);
    (void)system("rm -rf /tmp/trk_fs_a /tmp/trk_fs_b");
    printf("\n%s\n", fails ? "FAILURES" : "all file-operation rules match");
    return fails ? 1 : 0;
}
