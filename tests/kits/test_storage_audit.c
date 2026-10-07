/* Missing backends, empty values and terminal node events. */
#include "tiku_fs.h"
#include "tiku_attr.h"
#include "tiku_notify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
static void check(int ok, const char *id)
{ if (!ok) { printf("FAIL %s\n", id); fails++; } }
static void missing_fs(void)
{
    char out[64];
    const char *paths[] = {"/file"};
    check(tiku_fs_copy(NULL, paths, 1, "/", 0) < 0, "D-143 copy");
    check(tiku_fs_move(NULL, paths, 1, "/", 0) < 0, "D-143 move");
    check(tiku_fs_duplicate(NULL, paths, 1) < 0, "D-143 duplicate");
    check(tiku_fs_trash(NULL, paths, 1) < 0, "D-143 trash");
    check(tiku_fs_delete(NULL, paths, 1) < 0, "D-143 delete");
    check(tiku_fs_link(NULL, paths, 1, "/", 0) < 0, "D-143 link");
    check(tiku_fs_trash_dir(NULL, "/file", out, sizeof out) < 0, "D-143 trash dir");
    check(tiku_fs_rename(NULL, "/file", "new", out, sizeof out) < 0, "D-143 rename");
    check(tiku_fs_new_folder(NULL, "/", out, sizeof out, NULL, 0) < 0, "D-143 folder");
    check(tiku_fs_new_from_template(NULL, "/a", "/", out, sizeof out, NULL, 0) < 0,
          "D-143 template");
}
int main(void)
{
    char out[16];
    missing_fs();
    tiku_attr_format(NULL, NULL, out, sizeof out);
    check(!strcmp(out, "-"), "D-141 unread");
    tiku_attr_format(NULL, "", out, sizeof out);
    check(!strcmp(out, ""), "D-141 empty");
    tiku_evlist_t *in = calloc(1, sizeof *in), *coalesced = calloc(1, sizeof *coalesced);
    in->count = 2;
    strcpy(in->ev[0].path, "/a"); strcpy(in->ev[1].path, "/a");
    in->ev[0].op = TIKU_EV_CREATED; in->ev[1].op = TIKU_EV_REMOVED;
    tiku_evlist_coalesce(in, coalesced);
    check(coalesced->count == 1 && coalesced->ev[0].op == TIKU_EV_REMOVED, "D-144");
    free(in); free(coalesced);
    puts(fails ? "FAIL: storage audit" : "PASS: storage audit");
    return fails != 0;
}
