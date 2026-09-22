/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_menutext.c - menus written as text become a menu set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_remote.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

int
main(void)
{
    tiku_menuset_t set;
    int n = tiku_remote_menus_from_text(
        "menu File\nitem 1 o Open a file…\nsep\nitem 9 q Quit\n"
        "menu Level\nitem 2 - Up\nitem 3 - Down\n\n# a remark\n", &set);

    printf("menus from text\n");
    check(n == 2 && set.nmenu == 2, "two menus");
    check(strcmp(set.menu[0].title, "File") == 0 &&
              strcmp(set.menu[1].title, "Level") == 0, "titled as written");
    check(set.menu[0].nitem == 3 && set.menu[0].item[1].separator &&
              !set.menu[0].item[0].separator,
          "File has an item, a separator and an item");
    check(set.menu[0].item[0].command == 1 && set.menu[0].item[0].sc == 'o' &&
              set.menu[0].item[0].mods == TIKU_MOD_CMD &&
              strcmp(set.menu[0].item[0].label, "Open a file…") == 0 &&
              set.menu[0].item[0].enabled,
          "an item keeps its command, shortcut and label with spaces");
    check(set.menu[1].item[0].sc == 0 && set.menu[1].item[0].mods == 0u &&
              set.menu[1].item[0].command == 2,
          "a dash is no shortcut");
    check(tiku_remote_menus_from_text("item 1 o Lost\n", &set) == 0,
          "an item before any menu belongs to none, and there are none");
    check(tiku_remote_menus_from_text("", &set) == 0, "nothing is nothing");
    printf("%s\n", fails ? "FAILURES" : "a hundred bytes say the menus");
    return fails ? 1 : 0;
}
