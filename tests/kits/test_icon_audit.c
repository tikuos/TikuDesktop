/*
 * TikuOS kits -- icon cache and ownership regression tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_icons.h"
#include <stdio.h>

static int fails;

/** @brief Count a failed contract check. */
static void check(int ok, const char *id)
{
    if (!ok) { printf("FAIL %s\n", id); fails++; }
}

/** @brief Flush releases both cached variants, including after repeated use. */
static void flushing(const char *name)
{
    for (int i = 0; i < 3; i++) {
        check(tiku_icons_bitmap_washed(name, 16, 0, 87) != NULL, "D-099 build");
        tiku_icons_flush();
        check(tiku_icons_cached_bytes() == 0 && !tiku_icons_cached(name, 16),
              "D-099 flush");
    }
    check(tiku_icons_bitmap(name, 16) != NULL, "D-099 rebuild");
    tiku_icons_flush();
}

/** @brief A hole refilled in slot zero does not make it the oldest render. */
static void eviction(const tiku_icon_t *a, const tiku_icon_t *b)
{
    for (int size = 64; size <= 67; size++)
        check(tiku_icons_bitmap(a->name, size) != NULL, "D-100 fill");
    check(tiku_icons_bitmap(b->name, 338) != NULL &&
          !tiku_icons_cached(a->name, 64) && tiku_icons_cached(a->name, 65),
          "D-100 budget hole");
    check(tiku_icons_add(b->name, b->data, b->len), "D-101 replace");
    check(tiku_icons_bitmap(a->name, 68) != NULL &&
          tiku_icons_bitmap(a->name, 69) != NULL &&
          tiku_icons_cached(a->name, 68) && !tiku_icons_cached(a->name, 65),
          "D-100 oldest render");
    tiku_icons_flush();
}

/** @brief Replacement releases earlier owned art and names, including baked slots. */
static void replacements(const tiku_icon_t *icon)
{
    for (int i = 0; i < 5; i++)
        check(tiku_icons_add(icon->name, icon->data, icon->len), "D-101");
}

/** @brief Run icon checks; LeakSanitizer checks flushed and replaced storage. */
int main(void)
{
    const tiku_icon_t *a = tiku_icons_at(0), *b = tiku_icons_at(1);

    setvbuf(stdout, NULL, _IOLBF, 0);
    flushing(a->name); eviction(a, b); replacements(b);
    printf("%s: icon audit\n", fails ? "FAIL" : "PASS");
    return !!fails;
}
