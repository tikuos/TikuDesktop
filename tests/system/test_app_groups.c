/* Presentation categories retain exact paths and launch behaviour. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "tiku_manifest.h"
#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"
#include "../../system/desktop/tiku_trk_desktop_appgroups.inl"

int main(void)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL); assert(db);
    const struct { const char *file, *category; } shipped[] = {
        {"editor/editor", "documents"}, {"notes/notes", "documents"},
        {"reader/reader", "documents"}, {"marker/marker", "documents"},
        {"doc/doc", "documents"}, {"pdf/pdf", "documents"}, {"slides/slides", "documents"},
        {"image/image", "graphics"}, {"grab/grab", "graphics"}, {"circuit/circuit", "graphics"},
        {"calculator/calc", "utilities"}, {"terminal/terminal", "utilities"}, {"archive/archive", "utilities"},
        {"ide/basic", "development"}, {"ide/c", "development"},
        {"developer-calculator/calculator", "development"}, {"hex/hex", "development"}, {"diff/diff", "development"},
        {"bench/bench", "device-tools"}, {"console/console", "device-tools"},
        {"deviceutils/monitor", "device-tools"}, {"deviceutils/vfs", "device-tools"}, {"sdr/sdr", "device-tools"}
    };
    for (size_t i = 0; i < sizeof shipped / sizeof shipped[0]; i++) {
        char path[256]; tiku_manifest_t m;
        snprintf(path, sizeof path, "apps/%s.app", shipped[i].file);
        assert(!tiku_manifest_read(path, &m, NULL, 0));
        assert(!strcmp(m.category, shipped[i].category));
        snprintf(path, sizeof path, "/opt/apps/%s/%s", m.id, m.module);
        assert(tiku_trk_deskbar_add_app_in(db, installed_group(m.category), m.name, path, m.icon));
    }
    assert(installed_group("custom.so") == TIKU_TRK_DB_GROUP_APPS);
    assert(installed_group("device-tools") == TIKU_TRK_DB_GROUP_DEVICE_TOOLS);
    assert(installed_group("programming") == TIKU_TRK_DB_GROUP_PROGRAMMING);
    assert(installed_group("TikuHex.so") == TIKU_TRK_DB_GROUP_APPS);
    assert(installed_group("applications") == installed_group("utilities"));
    assert(installed_group("programming") == installed_group("development"));
    assert(!tiku_trk_deskbar_add_app_in(db, -1, "Bad", "/bad", NULL));
    assert(!tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_COUNT, "Bad", "/bad", NULL));
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_DEMOS, "zebra", "/demo/z.so", "clock"));
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_DEMOS, "Alpha", "/demo/a.so", "file"));
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_DEMOS, "alpha", "/demo/b.so", "folder"));
    db_build_leaf(db);
    const int order[] = {TIKU_TRK_DB_GROUP_DOCUMENTS, TIKU_TRK_DB_GROUP_GRAPHICS,
        TIKU_TRK_DB_GROUP_UTILITIES, TIKU_TRK_DB_GROUP_DEVELOPMENT,
        TIKU_TRK_DB_GROUP_DEVICE_TOOLS, TIKU_TRK_DB_GROUP_DEMOS};
    const int counts[] = {7, 3, 3, 5, 5, 3};
    int found[TIKU_TRK_DB_GROUP_COUNT] = {0}, next = 0;
    for (int i = 0; i < db->leaf_count; i++) {
        if (db->leaf[i].kind != TIKU_TRK_DB_ITEM_EXPAND ||
            db->leaf[i].expand != TIKU_TRK_DB_EXPAND_APPS) continue;
        int group = db->leaf[i].which;
        assert(group == order[next]);
        found[group]++;
        db_open_sub(db, i);
        assert(db->sub_count == counts[next++]);
        for (int j = 0; j < db->sub_count; j++) {
            assert(db->sub[j].command == TIKU_TRK_DB_LAUNCH_APP);
            if (j) assert(strcasecmp(db->sub[j-1].label, db->sub[j].label) <= 0);
            int match = 0;
            for (int k = 0; k < db->app_count; k++)
                if (!strcmp(db->app[k].command, db->sub[j].path)) {
                    assert(db->app[k].group == group);
                    assert(!strcmp(db->app[k].label, db->sub[j].label));
                    assert(!strcmp(db->app[k].icon, db->sub[j].icon)); match++;
                }
            assert(match == 1);
        }
        if (group == TIKU_TRK_DB_GROUP_DEMOS)
            assert(!strcmp(db->sub[0].path, "/demo/a.so") && !strcmp(db->sub[1].path, "/demo/b.so"));
    }
    for (int i = 0; i < TIKU_TRK_DB_GROUP_COUNT; i++) assert(found[i] == 1);
    tiku_trk_deskbar_free(db);
    db = tiku_trk_deskbar_new(NULL); db_build_leaf(db);
    for (int i = 0; i < db->leaf_count; i++)
        assert(db->leaf[i].kind != TIKU_TRK_DB_ITEM_EXPAND ||
               db->leaf[i].expand != TIKU_TRK_DB_EXPAND_APPS);
    tiku_trk_deskbar_free(db);
    /* All group rows and a full Deskbar folder: Shut down is added
     * last, so a menu without room for it loses it first. */
    (void)mkdir("/tmp/trk_leafmax", 0700);
    for (int i = 0; i < 30; i++) {
        char path[64]; FILE *f;
        snprintf(path, sizeof path, "/tmp/trk_leafmax/item%02d", i);
        f = fopen(path, "w"); assert(f); fputs("x\n", f); fclose(f);
    }
    tiku_backend_t *posix = tiku_backend_posix_open(); assert(posix);
    db = tiku_trk_deskbar_new(posix); assert(db);
    for (int g = 0; g < TIKU_TRK_DB_GROUP_COUNT; g++)
        assert(tiku_trk_deskbar_add_app_in(db, g, "row", "/opt/apps/row.so",
                                           "application"));
    tiku_trk_deskbar_set_menu_dir(db, "/tmp/trk_leafmax", "/tmp/trk_leafmax");
    db_build_leaf(db);
    assert(db->leaf_count > 4);
    assert(db->leaf[db->leaf_count - 1].command == TIKU_TRK_DB_QUIT);
    tiku_trk_deskbar_free(db);
    for (int i = 0; i < 30; i++) {
        char path[64];
        snprintf(path, sizeof path, "/tmp/trk_leafmax/item%02d", i);
        (void)remove(path);
    }
    (void)remove("/tmp/trk_leafmax");
    puts("PASS: shipped app categories, legacy aliases, stable label sorting, exact paths/icons, empty groups and full menus");
    return 0;
}
