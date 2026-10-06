/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_textview_alloc.c - a keystroke whose journal entry cannot grow is
 * still journaled, so undo takes back that keystroke and no more.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_call;           /* the allocation to refuse, 1 = next */

/** @brief malloc(), except that the fail_call'th call returns NULL. */
static void *
counted_malloc(size_t bytes)
{
    if (fail_call > 0 && --fail_call == 0) {
        return NULL;
    }
    return malloc(bytes);
}
#define malloc counted_malloc
#include "../../kits/interface/tiku_textview.c"
#undef malloc

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] text    %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Whether @p tv holds the one line @p want. */
static int
holds(const tiku_textview_t *tv, const char *want)
{
    return tiku_textview_lines(tv) == 1 &&
           strcmp(tiku_textview_line(tv, 0), want) == 0;
}

int
main(void)
{
    tiku_textview_t tv;

    printf("a journal entry that cannot grow\n");
    tiku_textview_init(&tv);
    tiku_textview_insert(&tv, 'a');
    tiku_textview_insert(&tv, 'b');
    fail_call = 2;              /* the line's copy, then the entry's growth */
    tiku_textview_insert(&tv, 'c');
    fail_call = 0;
    check(holds(&tv, "abc"), "the third keystroke lands in the text");
    check(tiku_textview_undo(&tv) && holds(&tv, "ab"),
          "one undo takes back that keystroke alone");
    check(tiku_textview_undo(&tv) && holds(&tv, "") &&
          !tiku_textview_can_undo(&tv), "and a second takes back the run");
    tiku_textview_free(&tv);

    tiku_textview_set(&tv, "xyz");
    tiku_textview_place(&tv, 0, 3);
    tiku_textview_backspace(&tv);
    tiku_textview_backspace(&tv);
    fail_call = 2;
    tiku_textview_backspace(&tv);
    fail_call = 0;
    check(holds(&tv, ""), "a third backspace empties the line");
    check(tiku_textview_undo(&tv) && holds(&tv, "x"),
          "one undo restores the character it removed");
    check(tiku_textview_undo(&tv) && holds(&tv, "xyz"),
          "and a second restores the run before it");
    tiku_textview_free(&tv);

    printf("%s\n", fails ? "FAILURES" : "every keystroke is journaled");
    return fails ? 1 : 0;
}
