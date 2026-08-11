/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fs.c - file-operation rules, checked against their FEATURES rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

    tiku_trk_fs_free(fs);
    tiku_trk_backend_close(b);
    (void)system("rm -rf /tmp/trk_fs_a /tmp/trk_fs_b");
    printf("\n%s\n", fails ? "FAILURES" : "all file-operation rules match");
    return fails ? 1 : 0;
}
