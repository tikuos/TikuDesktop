/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_query.c - predicates, the glob, scope and matching.
 *
 * The worked examples come from the source that specified them, so a change
 * in the matcher shows up as a disagreement with Tracker rather than only as
 * a disagreement with an earlier version of this file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_query.h"
#include "tiku_trk_state.h"
#include "tiku_trk_find.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

static tiku_trk_model_t
node(const char *path, const char *name, tiku_trk_kind_t kind,
     const char *meta, unsigned perm, long size)
{
    tiku_trk_model_t m;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    snprintf(m.name, sizeof m.name, "%s", name);
    snprintf(m.facts.meta, sizeof m.facts.meta, "%s", meta);
    m.kind = kind;
    m.facts.perm = perm;
    m.facts.size = size;
    return m;
}

static int
name_finds(const char *typed, const char *name)
{
    tiku_trk_query_t q;
    tiku_trk_model_t m = node("/x", name, TIKU_TRK_KIND_FILE, "-", 1u, 0);

    tiku_trk_query_init(&q);
    tiku_trk_query_set_name(&q, typed);
    return tiku_trk_query_matches(&q, &m, NULL, "");
}

int
main(void)
{
    printf("the glob, which every operator goes through\n");
    check(tiku_trk_query_glob("*.txt", "a.txt"), "*.txt matches a.txt");
    check(!tiku_trk_query_glob("*.txt", "a.txtx"), "and not a.txtx");
    check(tiku_trk_query_glob("[lL][eE][dD]*", "led0"),
          "a class pattern matches");
    check(tiku_trk_query_glob("[lL][eE][dD]*", "LED9"),
          "and matches the other case");
    check(!tiku_trk_query_glob("[lL][eE][dD]*", "lid0"),
          "but not a near miss");
    check(tiku_trk_query_glob("a*b*c", "axxbyyc"), "two stars backtrack");
    check(!tiku_trk_query_glob("a*b*c", "axxbyy"), "and fail honestly");
    check(tiku_trk_query_glob("*", ""), "a lone star matches nothing at all");
    check(tiku_trk_query_glob("l?d0", "led0"), "? matches one character");
    check(!tiku_trk_query_glob("l?d0", "leed0"), "and only one");
    /* A '[' with nothing closing it is a literal bracket, not a class that
     * happens to swallow whatever follows. */
    check(!tiku_trk_query_glob("[abc", "a"),
          "an unterminated class matches nothing");
    check(tiku_trk_query_glob("[abc]", "a"), "a closed one still matches");
    check(tiku_trk_query_glob("[!abc]", "z"), "and negation works");
    check(!tiku_trk_query_glob("[!abc]", "a"), "both ways");

    printf("\ncase-insensitivity is a rewrite, not a flag\n");
    {
        char pat[128];

        (void)tiku_trk_query_pattern("txt", TIKU_TRK_OP_CONTAINS, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[tT][xX][tT]*") == 0,
              "\"txt\" contains becomes *[tT][xX][tT]*");
        (void)tiku_trk_query_pattern("txt", TIKU_TRK_OP_BEGINS_WITH, pat,
                                     sizeof pat);
        check(strcmp(pat, "[tT][xX][tT]*") == 0,
              "starts-with anchors the front");
        (void)tiku_trk_query_pattern("txt", TIKU_TRK_OP_ENDS_WITH, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[tT][xX][tT]") == 0,
              "ends-with anchors the back");
        (void)tiku_trk_query_pattern("txt", TIKU_TRK_OP_IS, pat, sizeof pat);
        check(strcmp(pat, "[tT][xX][tT]") == 0, "is anchors both");
        /* A space becomes a star, which is why the original finds
         * "foo-bar" when you type "foo bar". */
        (void)tiku_trk_query_pattern("foo bar", TIKU_TRK_OP_CONTAINS, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[fF][oO][oO]*[bB][aA][rR]*") == 0,
              "\"foo bar\" becomes *[fF][oO][oO]*[bB][aA][rR]*");
        /* Digits and punctuation are not letters and get no class. */
        (void)tiku_trk_query_pattern("led0", TIKU_TRK_OP_IS, pat, sizeof pat);
        check(strcmp(pat, "[lL][eE][dD]0") == 0, "a digit stays a digit");
    }

    printf("\nthe star rule: a substring search, unless you type a star\n");
    check(name_finds("led", "led0"), "\"led\" finds led0 as a substring");
    check(name_finds("ED", "led0"), "and does so case-insensitively");
    check(name_finds("d0", "led0"), "a substring can be at the end");
    /* With a star the text stops being a substring and must match outright. */
    check(name_finds("led*", "led0"), "\"led*\" matches led0");
    check(!name_finds("*d", "led0"), "\"*d\" does NOT match led0");
    check(name_finds("*d0", "led0"), "\"*d0\" does");
    check(name_finds("foo bar", "foo-bar"),
          "a typed space spans anything, so \"foo bar\" finds foo-bar");
    check(name_finds("", "anything"),
          "empty text matches everything, as the substring form degenerates");

    printf("\nterms, and how they join\n");
    {
        tiku_trk_query_t q;
        tiku_trk_model_t led = node("/dev/led0", "led0", TIKU_TRK_KIND_DEVICE_NODE,
                                    "bool,bool,cached,free", 3u, 1);
        tiku_trk_model_t ram = node("/sys/mem/free", "free", TIKU_TRK_KIND_DEVICE_NODE,
                                    "u32,bytes,cached,free", 1u, 5);
        tiku_trk_desc_t dled, dram;

        (void)tiku_trk_desc_parse(led.facts.meta, &dled);
        (void)tiku_trk_desc_parse(ram.facts.meta, &dram);

        tiku_trk_query_init(&q);
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_WRITABLE,
                                      TIKU_TRK_OP_IS, "yes",
                                      TIKU_TRK_JOIN_AND);
        check(tiku_trk_query_matches(&q, &led, &dled, "1"),
              "a writable node matches writable=yes");
        check(!tiku_trk_query_matches(&q, &ram, &dram, "1024"),
              "a read-only one does not");

        /* Two terms ANDed: writable AND named led. */
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_NAME,
                                      TIKU_TRK_OP_CONTAINS, "led",
                                      TIKU_TRK_JOIN_AND);
        check(tiku_trk_query_matches(&q, &led, &dled, "1"),
              "and both terms together still match");

        /* Or: writable OR named free -- the read-only one comes back. */
        q.term[1].join = TIKU_TRK_JOIN_OR;
        snprintf(q.term[1].value, sizeof q.term[1].value, "free");
        check(tiku_trk_query_matches(&q, &ram, &dram, "1024"),
              "an OR term readmits what the first term excluded");

        /* A unit term: everything measured in bytes, whatever it is called. */
        tiku_trk_query_init(&q);
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_UNIT, TIKU_TRK_OP_IS,
                                      "bytes", TIKU_TRK_JOIN_AND);
        check(tiku_trk_query_matches(&q, &ram, &dram, "1024") &&
              !tiku_trk_query_matches(&q, &led, &dled, "1"),
              "a unit term selects every node measured in bytes");
    }

    printf("\nordering compares numbers, not their spelling\n");
    {
        tiku_trk_query_t q;
        tiku_trk_model_t n1 = node("/a", "a", TIKU_TRK_KIND_DEVICE_NODE,
                                   "u32,count,cached,free", 1u, 0);
        tiku_trk_desc_t d;

        (void)tiku_trk_desc_parse(n1.facts.meta, &d);
        tiku_trk_query_init(&q);
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_VALUE,
                                      TIKU_TRK_OP_GREATER, "1000",
                                      TIKU_TRK_JOIN_AND);
        check(tiku_trk_query_matches(&q, &n1, &d, "9999"),
              "9999 is greater than 1000");
        check(!tiku_trk_query_matches(&q, &n1, &d, "999"),
              "999 is not (as text it would have been)");
        check(strcmp("999", "1000") > 0,
              "(comparing the text would have got this wrong)");

        /* Most namespace nodes declare no type at all, so the ordering
         * operator has to work on them from the values alone. */
        {
            tiku_trk_model_t plain = node("/sys/boot_count", "boot_count",
                                          TIKU_TRK_KIND_DEVICE_NODE, "-",
                                          1u, 0);
            tiku_trk_desc_t none;

            (void)tiku_trk_desc_parse("-", &none);
            check(!tiku_trk_query_matches(&q, &plain, &none, "30"),
                  "an UNTYPED node reading 30 is not over 1000");
            check(tiku_trk_query_matches(&q, &plain, &none, "9999"),
                  "an untyped node reading 9999 is");
            /* A name is not a number, so it is neither greater nor less. */
            check(!tiku_trk_query_matches(&q, &plain, &none, "EK-RA8P1"),
                  "a node whose value is a name is not over 1000 either");
        }
    }

    printf("\nscope is a path prefix, enforced outside the predicate\n");
    {
        tiku_trk_query_t q;

        tiku_trk_query_init(&q);
        check(tiku_trk_query_in_scope(&q, "/anywhere"),
              "with no scope everything is in scope");
        (void)tiku_trk_query_add_scope(&q, "/dev");
        check(tiku_trk_query_in_scope(&q, "/dev/led0"),
              "/dev admits /dev/led0");
        check(!tiku_trk_query_in_scope(&q, "/sys/uptime"),
              "and excludes /sys/uptime");
        /* The trailing separator is why a sibling cannot sneak in. */
        check(!tiku_trk_query_in_scope(&q, "/devices/x"),
              "/dev does NOT admit /devices/x");
        (void)tiku_trk_query_add_scope(&q, "/sys");
        check(tiku_trk_query_in_scope(&q, "/sys/uptime"),
              "a second scope widens it");
        check(tiku_trk_query_add_scope(&q, "/sys") == 0,
              "adding the same scope twice is a no-op");
    }

    printf("\nthe predicate the formula field would show\n");
    {
        tiku_trk_query_t q;
        char pred[TIKU_TRK_QUERY_PRED_MAX];

        tiku_trk_query_init(&q);
        tiku_trk_query_set_name(&q, "led");
        (void)tiku_trk_query_predicate(&q, pred, sizeof pred);
        check(strcmp(pred, "(name==\"*[lL][eE][dD]*\")") == 0,
              "a by-name search reads (name==\"*[lL][eE][dD]*\")");

        tiku_trk_query_init(&q);
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_UNIT, TIKU_TRK_OP_IS,
                                      "bytes", TIKU_TRK_JOIN_AND);
        (void)tiku_trk_query_add_term(&q, TIKU_TRK_QF_VALUE,
                                      TIKU_TRK_OP_GREATER, "1000",
                                      TIKU_TRK_JOIN_AND);
        (void)tiku_trk_query_predicate(&q, pred, sizeof pred);
        check(strcmp(pred, "(unit==\"[bB][yY][tT][eE][sS]\")&&(value>1000)")
              == 0, "two terms join with &&, and > keeps its number bare");
        if (strcmp(pred, "(unit==\"[bB][yY][tT][eE][sS]\")&&(value>1000)")) {
            printf("         got %s\n", pred);
        }
    }

    printf("\nsaved queries survive a round trip\n");
    {
        tiku_trk_store_t *st = tiku_trk_store_memory_open();
        tiku_trk_query_t a, b;
        char names[8][128];
        int n;

        tiku_trk_query_init(&a);
        (void)tiku_trk_query_add_term(&a, TIKU_TRK_QF_UNIT, TIKU_TRK_OP_IS,
                                      "bytes", TIKU_TRK_JOIN_AND);
        (void)tiku_trk_query_add_term(&a, TIKU_TRK_QF_VALUE,
                                      TIKU_TRK_OP_GREATER, "1000",
                                      TIKU_TRK_JOIN_OR);
        (void)tiku_trk_query_add_scope(&a, "/sys");
        check(tiku_trk_query_save(&a, "big memory", st) == 0,
              "a query saves");
        check(tiku_trk_query_load(&b, "big memory", st) == 0,
              "and loads back");
        check(b.term_count == a.term_count && b.scope_count == a.scope_count,
              "with its terms and scopes intact");
        check(b.term[1].op == TIKU_TRK_OP_GREATER &&
              b.term[1].join == TIKU_TRK_JOIN_OR &&
              strcmp(b.term[1].value, "1000") == 0,
              "including the operator, the join and the value");
        check(tiku_trk_query_load(&b, "no such query", st) == -1,
              "an unsaved name does not load");

        n = tiku_trk_query_list(st, names, 8);
        check(n == 1 && strcmp(names[0], "big memory") == 0,
              "a saved query is listed by name");
        (void)tiku_trk_query_save(&a, "second", st);
        n = tiku_trk_query_list(st, names, 8);
        check(n == 2 && strcmp(names[0], "second") == 0,
              "the newest is listed first");
        (void)tiku_trk_query_forget("second", st);
        n = tiku_trk_query_list(st, names, 8);
        check(n == 1 && strcmp(names[0], "big memory") == 0,
              "forgetting removes it from the list");

        /* What a record has to carry to be re-openable and sweepable. */
        a.kind = TIKU_TRK_QREC_TEMPLATE;
        a.temporary = 1;
        a.saved_at = 1700000000;
        snprintf(a.type, sizeof a.type, "text/plain");
        a.scope_was[0].dev = 4242u;
        snprintf(a.scope_was[0].volume, sizeof a.scope_was[0].volume,
                 "Backup");
        (void)tiku_trk_query_save(&a, "record", st);
        check(tiku_trk_query_load(&b, "record", st) == 0 &&
              b.kind == TIKU_TRK_QREC_TEMPLATE, "Q-028: a record says "
              "whether opening it RUNS it or opens the panel filled in");
        check(b.temporary == 1 && b.saved_at == 1700000000,
              "MA-053: and whether anyone meant to keep it, and when it "
              "was written -- the two things a sweep of stale by-products "
              "needs and neither of which existed");
        check(strcmp(b.type, "text/plain") == 0,
              "Q-004: the type it was narrowed to comes back with it");
        check(b.scope_was[0].dev == 4242u &&
              strcmp(b.scope_was[0].volume, "Backup") == 0,
              "Q-027: and a scope remembers WHICH volume it meant, not "
              "only the path -- mount points are reused, and a query that "
              "kept only the path searches whatever took its place");
        check(strcmp(b.scope[0], a.scope[0]) == 0,
              "with the path still there beside it");
        {   /* An identity line with no scope before it belongs to nothing. */
            tiku_trk_query_t stray;

            (void)tiku_trk_state_write(st, "stray", "_trk/query",
                                       "v2\nmode 0\nd 77 Ghost\n", 22);
            check(tiku_trk_query_load(&stray, "stray", st) == 0 &&
                  stray.scope_count == 0 && stray.dropped == 1 &&
                  stray.scope_was[0].dev == 0u,
                  "Q-027: an identity line that arrives BEFORE any scope is "
                  "dropped rather than attached to the next one -- a scope "
                  "wearing another volume's identity is worse than a scope "
                  "with none");
        }
        {   /* A v1 record still reads: the new fields take defaults. */
            tiku_trk_query_t old;

            (void)tiku_trk_state_write(st, "ancient", "_trk/query",
                                       "v1\nmode 0\nt 0 0 0 hello\n", 25);
            check(tiku_trk_query_load(&old, "ancient", st) == 0 &&
                  old.term_count == 1 &&
                  strcmp(old.term[0].value, "hello") == 0 &&
                  old.kind == TIKU_TRK_QREC_QUERY && old.temporary == 0,
                  "a record written before any of this still loads, with "
                  "the fields it does not carry taking their defaults");
        }
        tiku_trk_store_free(st);
    }

    printf("\nthe Find panel\n");
    {
        tiku_trk_find_t f;
        tiku_trk_query_t q;
        char lbl[64];

        tiku_trk_find_init(&f);
        check(f.row_count == 1, "a panel starts with one row");
        check(!tiku_trk_find_remove_row(&f),
              "and refuses to remove its last one");
        check(tiku_trk_find_add_row(&f) && f.row_count == 2, "rows are added");
        check(tiku_trk_find_remove_row(&f) && f.row_count == 1,
              "and removed once there is more than one");

        /* The join lives on the row ABOVE the one it reaches, and the fold
         * is strictly left to right. */
        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(&f);
        f.row[0].field = TIKU_TRK_QF_UNIT;
        f.row[0].op = TIKU_TRK_OP_IS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "bytes");
        f.row[0].join = TIKU_TRK_JOIN_OR;
        f.row[1].field = TIKU_TRK_QF_NAME;
        f.row[1].op = TIKU_TRK_OP_CONTAINS;
        snprintf(f.row[1].value, sizeof f.row[1].value, "led");
        tiku_trk_find_build(&f, &q);
        check(q.term_count == 2 && q.term[1].join == TIKU_TRK_JOIN_OR,
              "the join recorded on row 0 is the one that reaches row 1");

        /* Switching into formula shows what the controls were asking. */
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_FORMULA);
        check(strstr(f.formula, "[bB][yY][tT][eE][sS]") != NULL &&
              strstr(f.formula, "||") != NULL,
              "formula mode is pre-filled with the composed predicate");
        /* And leaving it does not throw the rows away, unlike the original. */
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        check(f.row_count == 2 && strcmp(f.row[1].value, "led") == 0,
              "leaving formula mode keeps the rows it came from");

        tiku_trk_find_init(&f);
        (void)tiku_trk_find_add_place(&f, "sys");
        (void)tiku_trk_find_add_place(&f, "dev");
        check(tiku_trk_find_add_place(&f, "sys") == 0,
              "a place is offered once");
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "Everywhere") == 0, "no scope reads Everywhere");
        f.everywhere = 0;
        f.place_mask = 1u;
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "/sys") == 0, "one place is named");
        f.place_mask = 3u;
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "2 places") == 0, "several are counted");
        tiku_trk_find_build(&f, &q);
        check(q.scope_count == 2, "and both reach the query");

        {
            char nm[128];

            tiku_trk_find_init(&f);
            snprintf(f.name_text, sizeof f.name_text, "led");
            (void)tiku_trk_find_default_name(&f, 0, nm, sizeof nm);
            check(strstr(nm, "name contains led") != NULL &&
                  strstr(nm, " - ") != NULL,
                  "a saved query is named for its criteria and its time");
            check(strchr(nm, '/') == NULL,
                  "and carries nothing a path cannot");
        }
    }

    printf("\neach field offers only the operators it can answer\n");
    {
        tiku_trk_find_t f;
        char nm[160];

        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        /* Size is measured: it starts on "is" and never offers "contains". */
        f.row[0].field = TIKU_TRK_QF_SIZE;
        f.row[0].op = TIKU_TRK_OP_CONTAINS;
        {
            tiku_desk_rect_t fr = { 0, 0, 560, 400 };
            int picked, hx = -1, hy = -1, sx, sy, row0;

            /* Ask the panel where its own field control is rather than
             * assuming: a test that guesses coordinates tests the guess. */
            for (sy = 0; sy < fr.h && hy < 0; sy++) {
                for (sx = 0; sx < fr.w; sx++) {
                    if (tiku_trk_find_hit(&f, fr, sx, sy, &row0) ==
                        TIKU_TRK_FIND_HIT_FIELD && row0 == 0) {
                        hx = sx;
                        hy = sy;
                        break;
                    }
                }
            }
            check(hx >= 0, "the panel reports where its field control is");
            (void)tiku_trk_find_click(&f, fr, hx + 2, hy + 2);
            picked = -1;
            {
                int k;
                for (k = 0; k < f.popup.count; k++) {
                    if (strcmp(f.popup.item[k].label, "Size") == 0) {
                        picked = k;
                    }
                }
            }
            check(f.popup.count > 0 && picked >= 0,
                  "clicking it opens a menu that offers Size");
            if (picked >= 0) {
                int y = f.popup_frame.y +
                        tiku_trk_menu_item_top(&f.popup, picked) + 2;
                (void)tiku_trk_find_click(&f, fr, f.popup_frame.x + 4, y);
            }
            check(f.row[0].field == TIKU_TRK_QF_SIZE,
                  "picking Size sets the field");
            check(f.row[0].op != TIKU_TRK_OP_CONTAINS,
                  "and drops \"contains\", which a size cannot answer");
        }

        /* A saved name describes every row, not just the first. */
        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(&f);
        f.row[0].field = TIKU_TRK_QF_UNIT;
        f.row[0].op = TIKU_TRK_OP_IS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "bytes");
        f.row[0].join = TIKU_TRK_JOIN_OR;
        f.row[1].field = TIKU_TRK_QF_NAME;
        f.row[1].op = TIKU_TRK_OP_CONTAINS;
        snprintf(f.row[1].value, sizeof f.row[1].value, "led");
        (void)tiku_trk_find_default_name(&f, 0, nm, sizeof nm);
        check(strstr(nm, "bytes") != NULL && strstr(nm, "led") != NULL &&
              strstr(nm, " or ") != NULL,
              "the generated name describes every row and its join");

        /* Entering attribute mode puts the caret on the last row. */
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_FORMULA);
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        check(f.focus_row == f.row_count - 1,
              "the caret lands on the row still being filled in");
    }

    printf("\nre-opening a query in the panel (Q-026)\n");
    {
        tiku_trk_find_t f;
        tiku_trk_query_t q;

        tiku_trk_find_init(&f);
        (void)tiku_trk_find_add_place(&f, "sys");
        (void)tiku_trk_find_add_place(&f, "dev");
        tiku_trk_find_set_mode(&f, TIKU_TRK_QMODE_ATTRIBUTE);
        f.row_count = 2;
        f.row[0].field = TIKU_TRK_QF_NAME;
        f.row[0].op = TIKU_TRK_OP_CONTAINS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "log");
        f.row[0].join = TIKU_TRK_JOIN_OR;
        f.row[1].field = TIKU_TRK_QF_SIZE;
        f.row[1].op = TIKU_TRK_OP_GREATER;
        snprintf(f.row[1].value, sizeof f.row[1].value, "500");
        f.everywhere = 0;
        f.place_mask = 0x2u;            /* the second place */
        tiku_trk_find_build(&f, &q);
        {
            tiku_trk_find_t back;

            tiku_trk_find_init(&back);
            (void)tiku_trk_find_add_place(&back, "sys");
            (void)tiku_trk_find_add_place(&back, "dev");
            check(tiku_trk_find_from_query(&back, &q) == 1,
                  "Q-026: a query the panel can express restores whole");
            check(back.mode == TIKU_TRK_QMODE_ATTRIBUTE &&
                  back.row_count == 2, "with its mode and its rows");
            check(back.row[0].field == TIKU_TRK_QF_NAME &&
                  back.row[1].op == TIKU_TRK_OP_GREATER &&
                  strcmp(back.row[1].value, "500") == 0,
                  "each row's field, operator and value");
            check(back.row[0].join == TIKU_TRK_JOIN_OR,
                  "and the join, which belongs to the row BEFORE the one "
                  "it joins -- the asymmetry the builder writes out");
            check(!back.everywhere && back.place_mask == 0x2u,
                  "the scope ticks the place it named, not the first one");
        }
        {   /* A scope this panel cannot offer. */
            tiku_trk_find_t back;

            tiku_trk_find_init(&back);
            (void)tiku_trk_find_add_place(&back, "sys");
            check(tiku_trk_find_from_query(&back, &q) == 0,
                  "a query naming somewhere the panel does not offer is "
                  "reported rather than silently narrowed");
            check(back.everywhere,
                  "and falls back to everywhere: a search that quietly "
                  "lost its scope searches somewhere else");
        }
        {   /* By name: what was TYPED comes back. */
            tiku_trk_query_t byname;
            tiku_trk_find_t back;

            tiku_trk_query_init(&byname);
            tiku_trk_query_set_name(&byname, "report");
            tiku_trk_find_init(&back);
            check(tiku_trk_find_from_query(&back, &byname) == 1 &&
                  back.mode == TIKU_TRK_QMODE_NAME &&
                  strcmp(back.name_text, "report") == 0, "Q-026: a by-name "
                  "query comes back with the text in the field, not as a "
                  "term the user never typed as one");
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "every query rule holds");
    return fails ? 1 : 0;
}
