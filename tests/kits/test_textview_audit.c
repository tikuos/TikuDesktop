/* Public edits clamp caller-supplied caret coordinates. */
#include "tiku_textview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int edit(int mode, int coordinate)
{
    tiku_textview_t *tv = calloc(1, sizeof *tv);
    tiku_textview_init(tv);
    tv->cx = coordinate;
    tv->cy = coordinate;
    if (mode == 0) tiku_textview_insert_text(tv, "a");
    if (mode == 1) tiku_textview_insert(tv, 'a');
    if (mode == 2) tiku_textview_newline(tv);
    int ok = mode == 2 ? tv->nline == 2 && tv->cx == 0 && tv->cy == 1 &&
                        tv->line[0].len == 0 && tv->line[1].len == 0
                       : !strcmp(tiku_textview_line(tv, 0), "a") && tv->cx == 1;
    tiku_textview_free(tv); free(tv);
    puts(ok ? "PASS: D-121" : "FAIL: D-121");
    return !ok;
}
int main(int argc, char **argv)
{
    if (argc > 1) return edit(atoi(argv[1]), 50);
    int fails = 0;
    for (int mode = 0; mode < 3; mode++) {
        fails += edit(mode, 50);
        fails += edit(mode, -50);
    }
    return fails != 0;
}
