/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_vdir.c - a directory that is several real ones, and the priority
 * rule that makes it more than a concatenation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_vdir.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static void
put(const char *dir, const char *name, const char *text)
{
    char p[600];
    FILE *f;

    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "w");
    if (f != NULL) { fputs(text, f); (void)fclose(f); }
}

static void
gone(const char *dir, const char *name)
{
    char p[600];

    snprintf(p, sizeof p, "%s/%s", dir, name);
    (void)remove(p);
}

int
main(void)
{
    tiku_trk_backend_t *b = tiku_trk_backend_posix_open();
    tiku_trk_vdir_t v, kid;
    tiku_trk_model_t list[32];
    char A[] = "/tmp/trk_vd_a", B[] = "/tmp/trk_vd_b", C[] = "/tmp/trk_vd_c";
    char got[600];
    int n;


    (void)system("rm -rf /tmp/trk_vd_a /tmp/trk_vd_b /tmp/trk_vd_c");
    (void)mkdir(A, 0755); (void)mkdir(B, 0755); (void)mkdir(C, 0755);
    put(A, "only_a.txt", "a");
    put(B, "only_b.txt", "b");
    put(A, "shared.txt", "from A");
    put(B, "shared.txt", "from B");
    put(C, "shared.txt", "from C");

    tiku_trk_vdir_init(&v);
    (void)tiku_trk_vdir_add(&v, A);
    (void)tiku_trk_vdir_add(&v, B);
    (void)tiku_trk_vdir_add(&v, C);

    printf("priority (PVN-061)\n");
    check(tiku_trk_vdir_owner(&v, b, "shared.txt") == 0, "PVN-061",
          "a name is supplied by the FIRST contributor that has it");
    check(tiku_trk_vdir_owner(&v, b, "only_b.txt") == 1, "PVN-061",
          "and one only a later contributor has still appears");
    check(tiku_trk_vdir_owner(&v, b, "nobody.txt") == -1, "PVN-061",
          "a name nobody has is owned by nobody");
    check(tiku_trk_vdir_resolve(&v, b, "shared.txt", got, sizeof got) == 0 &&
          strstr(got, "trk_vd_a") != NULL, "PVN-061",
          "and it resolves to the winner's real path");

    printf("\nthe merged listing\n");
    n = tiku_trk_vdir_list(&v, b, list, 32);
    check(n == 3, "PVN-061",
          "three names from three directories holding five files: a "
          "shadowed copy is not a second row");
    {
        int i, shared = 0;

        for (i = 0; i < n; i++) {
            if (strcmp(list[i].name, "shared.txt") == 0) { shared++; }
        }
        check(shared == 1, "PVN-061", "and the shared name appears once");
    }

    printf("\nwhat a change does (PVN-062, PVN-063)\n");
    /* A creation in a SHADOWED directory is invisible. */
    put(C, "only_a.txt", "shadowed");
    check(!tiku_trk_vdir_after_change(&v, b, "only_a.txt", 2, 1, got,
                                      sizeof got),
          "PVN-061", "a file appearing in a shadowed directory produces no "
          "row -- it cannot be reached by that name");

    /* A creation in a HIGHER-priority directory replaces the shown one. */
    gone(A, "shared.txt");
    check(tiku_trk_vdir_after_change(&v, b, "shared.txt", 0, 0, got,
                                     sizeof got) &&
          strstr(got, "trk_vd_b") != NULL, "PVN-063",
          "removing the winner REVEALS what it shadowed rather than "
          "leaving a hole");
    put(A, "shared.txt", "back");
    check(tiku_trk_vdir_after_change(&v, b, "shared.txt", 0, 1, got,
                                     sizeof got) &&
          strstr(got, "trk_vd_a") != NULL, "PVN-062",
          "and an entry in a higher-priority directory replaces the shown "
          "one in a single step");

    /* Removing a shadowed copy changes nothing visible. */
    gone(C, "shared.txt");
    check(!tiku_trk_vdir_after_change(&v, b, "shared.txt", 2, 0, got,
                                      sizeof got),
          "PVN-063", "while removing a shadowed copy changes nothing");

    /* The last copy going takes the row with it. */
    gone(A, "only_a.txt");
    gone(C, "only_a.txt");
    check(tiku_trk_vdir_after_change(&v, b, "only_a.txt", 0, 0, got,
                                     sizeof got) && got[0] == '\0',
          "PVN-064", "and when the last copy goes, so does the row");

    printf("\nsubdirectories merge too (PVN-066)\n");
    {
        char sa[600], sb[600];

        snprintf(sa, sizeof sa, "%s/sub", A);
        snprintf(sb, sizeof sb, "%s/sub", B);
        (void)mkdir(sa, 0755);
        (void)mkdir(sb, 0755);
        put(sa, "in_a.txt", "1");
        put(sb, "in_b.txt", "2");
        tiku_trk_vdir_child(&v, b, "sub", &kid);
        check(kid.nsrc == 2, "PVN-066",
              "a subdirectory merges the same-named subdirectory of every "
              "contributor that has one");
        n = tiku_trk_vdir_list(&kid, b, list, 32);
        check(n == 2, "PVN-066",
              "so descending into it does not leave the merge behind");
        tiku_trk_vdir_child(&v, b, "nosuch", &kid);
        check(kid.nsrc == 0, "PVN-066",
              "and a name no contributor has as a folder merges nothing");
    }

    printf("\nthe definition file (PVN-067)\n");
    {
        char def[600];
        FILE *f;

        snprintf(def, sizeof def, "/tmp/trk_vd_def");
        f = fopen(def, "w");
        if (f != NULL) {
            fprintf(f, "# a comment must not demote what follows it\n\n%s\n"
                       "   %s   \n", A, B);
            (void)fclose(f);
        }
        check(tiku_trk_vdir_load(&kid, def) == 2, "PVN-067",
              "a definition file is one path per line, comments and blanks "
              "ignored");
        check(strcmp(kid.src[0], A) == 0 && strcmp(kid.src[1], B) == 0,
              "PVN-067",
              "and the order of the lines IS the priority order");
        (void)remove(def);
    }

    printf("\nwatching (PVN-059, PVN-064, PVN-067)\n");
    {
        tiku_trk_vdir_watch_t w;
        char def[600], gonedir[600];
        FILE *f;
        int which = -99;
        unsigned ch;

        snprintf(def, sizeof def, "/tmp/trk_vd_def2");
        f = fopen(def, "w");
        if (f != NULL) { fprintf(f, "%s\n%s\n", A, B); (void)fclose(f); }
        check(tiku_trk_vdir_load(&v, def) == 2, "PVN-067",
              "a definition names its contributors");

        tiku_trk_vdir_watch_init(&w, &v);
        ch = tiku_trk_vdir_poll(&v, &w, &which);
        check(ch == TIKU_TRK_VDIR_CH_NONE, "PVN-059",
              "a poll with nothing changed reports nothing -- the first "
              "one establishes what unchanged means");

        /* A contributor's contents moving. */
        sleep(1);                   /* mtime has one-second resolution */
        put(A, "brand_new.txt", "x");
        ch = tiku_trk_vdir_poll(&v, &w, &which);
        check((ch & TIKU_TRK_VDIR_CH_SRC) != 0u, "PVN-059",
              "a file appearing in a contributor is noticed");
        check(tiku_trk_vdir_poll(&v, &w, &which) == TIKU_TRK_VDIR_CH_NONE,
              "PVN-059", "and reported once, not on every poll after");

        /* The definition file being edited. */
        sleep(1);
        f = fopen(def, "w");
        if (f != NULL) { fprintf(f, "%s\n", A); (void)fclose(f); }
        ch = tiku_trk_vdir_poll(&v, &w, &which);
        check((ch & TIKU_TRK_VDIR_CH_DEF) != 0u, "PVN-067",
              "editing the definition file is noticed, so the merge can be "
              "re-read");

        /* A whole contributor disappearing. */
        snprintf(gonedir, sizeof gonedir, "/tmp/trk_vd_gone");
        (void)mkdir(gonedir, 0755);
        put(gonedir, "from_gone.txt", "g");
        tiku_trk_vdir_init(&v);
        (void)tiku_trk_vdir_add(&v, A);
        (void)tiku_trk_vdir_add(&v, gonedir);
        tiku_trk_vdir_watch_init(&w, &v);
        check(tiku_trk_vdir_supplied_by(&v, b, "from_gone.txt", 1),
              "PVN-064", "a name is known to come from its contributor");
        (void)remove("/tmp/trk_vd_gone/from_gone.txt");
        (void)rmdir(gonedir);
        ch = tiku_trk_vdir_poll(&v, &w, &which);
        check((ch & TIKU_TRK_VDIR_CH_GONE) != 0u && which == 1, "PVN-064",
              "a contributor disappearing is noticed, and says WHICH -- "
              "the caller has to find its rows");
        tiku_trk_vdir_drop(&v, 1);
        check(v.nsrc == 1 && strcmp(v.src[0], A) == 0, "PVN-064",
              "dropping it leaves the others in their original order");
        (void)remove(def);
    }

    printf("\na merged child has an identity of its own (PVN-066)\n");
    {
        tiku_trk_vdir_t two;
        uint64_t a1, a2, b1;

        tiku_trk_vdir_init(&two);
        (void)tiku_trk_vdir_add(&two, B);
        a1 = tiku_trk_vdir_child_id(&v, "sub");
        a2 = tiku_trk_vdir_child_id(&v, "sub");
        b1 = tiku_trk_vdir_child_id(&two, "sub");
        check(a1 == a2 && a1 != 0, "PVN-066",
              "the same merged child gets the same identity every time");
        check(a1 != b1, "PVN-066",
              "and two different merges holding a folder of one name are "
              "different folders -- a window keyed on one must not answer "
              "for the other");
        check(tiku_trk_vdir_child_id(&v, "other") != a1, "PVN-066",
              "as are two children of one merge");
    }
    (void)system("rm -rf /tmp/trk_vd_a /tmp/trk_vd_b /tmp/trk_vd_c");
    tiku_trk_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "the virtual directory holds");
    return fails ? 1 : 0;
}
