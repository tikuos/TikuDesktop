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

static void
run_to_end(tiku_trk_fs_t *fs)
{
    int guard = 0;
    while (tiku_trk_fs_busy(fs) && guard++ < 1000) {
        if (tiku_trk_fs_step(fs, 4) == 0) {
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

    tiku_trk_fs_free(fs);
    tiku_trk_backend_close(b);
    (void)system("rm -rf /tmp/trk_fs_a /tmp/trk_fs_b");

    printf("\n%s\n", fails ? "FAILURES" : "all file-operation rules match");
    return fails ? 1 : 0;
}
