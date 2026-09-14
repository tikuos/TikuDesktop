/* Presentation categories retain exact paths and launch behaviour. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/deskbar/bar/tiku_trk_deskbar_private.h"
#include "../src/shell/desktop/tiku_trk_desktop_appgroups.inl"

int main(void)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL); assert(db);
    /* Monitor and Inspector are ordinary bundle entries too. */
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_DEVICE_TOOLS,
        "Device Monitor", "/opt/apps/monitor/DeviceMonitor.so", "application"));
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_DEVICE_TOOLS,
        "VFS Inspector", "/opt/apps/vfs/VFSInspector.so", "application"));
    const char *names[] = {"editor.so", "terminal.so", "Bench.so", "TikuConsole.so",
        "hello.so", "clock.so", "counter.so", "sketch.so", "twowin.so", "sensors.so",
        "DeveloperCalculator.so"};
    const char *categories[] = {"applications", "applications", "device-tools",
        "device-tools", "demos", "demos", "demos", "demos", "demos", "demos",
        "applications"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        int group = installed_group(categories[i]);
        assert(group == (i < 2 || i == 10 ? TIKU_TRK_DB_GROUP_APPS :
            i < 4 ? TIKU_TRK_DB_GROUP_DEVICE_TOOLS : TIKU_TRK_DB_GROUP_DEMOS));
        char path[128];
        /* Metadata chooses the group; the exact module path is retained. */
        snprintf(path, sizeof path, "/opt/apps/%s", names[i]);
        assert(tiku_trk_deskbar_add_app_in(db, group, names[i], path,
                                           "application"));
    }
    assert(installed_group("custom.so") == TIKU_TRK_DB_GROUP_APPS);
    assert(installed_group("device-tools") == TIKU_TRK_DB_GROUP_DEVICE_TOOLS);
    assert(installed_group("programming") == TIKU_TRK_DB_GROUP_PROGRAMMING);
    assert(installed_group("TikuHex.so") == TIKU_TRK_DB_GROUP_APPS);
    /* Programming is read last, so a table one entry short loses it
     * whole rather than losing the app that overflowed. */
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_PROGRAMMING,
        "TikuBasic", "/opt/apps/basic/TikuBasic.so", "application"));
    assert(tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_PROGRAMMING,
        "TikuC", "/opt/apps/c/TikuC.so", "application"));
    db_build_leaf(db);
    int found[4] = {0};
    for (int i = 0; i < db->leaf_count; i++) {
        if (db->leaf[i].kind != TIKU_TRK_DB_ITEM_APPS &&
            db->leaf[i].kind != TIKU_TRK_DB_ITEM_PROGRAMMING) continue;
        int group = db->leaf[i].which;
        found[group]++;
        db_open_sub(db, i);
        assert(db->sub_count == (group == TIKU_TRK_DB_GROUP_DEMOS ? 6 :
            group == TIKU_TRK_DB_GROUP_PROGRAMMING ? 2 :
            group == TIKU_TRK_DB_GROUP_DEVICE_TOOLS ? 4 : 3));
        for (int j = 0; j < db->sub_count; j++) {
            assert(db->sub[j].kind == TIKU_TRK_DB_ITEM_APP);
            const char *leaf = strrchr(db->sub[j].path, '/');
            assert(leaf && !strncmp(db->sub[j].path, "/opt/apps/", 10));
            for (size_t k = 0; k < sizeof names / sizeof names[0]; k++)
                if (!strcmp(leaf + 1, names[k]))
                    assert(installed_group(categories[k]) == group);
        }
    }
    for (int i = 0; i < 4; i++) assert(found[i] == 1);
    tiku_trk_deskbar_free(db);
    db = tiku_trk_deskbar_new(NULL); db_build_leaf(db);
    for (int i = 0; i < db->leaf_count; i++) assert(db->leaf[i].kind != TIKU_TRK_DB_ITEM_APPS);
    tiku_trk_deskbar_free(db);
    /* Four group rows and a full Deskbar folder: Shut down is added
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
    assert(db->leaf[db->leaf_count - 1].kind == TIKU_TRK_DB_ITEM_QUIT);
    tiku_trk_deskbar_free(db);
    for (int i = 0; i < 30; i++) {
        char path[64];
        snprintf(path, sizeof path, "/tmp/trk_leafmax/item%02d", i);
        (void)remove(path);
    }
    (void)remove("/tmp/trk_leafmax");
    puts("PASS: Applications, Programming, Device Tools and Demos are separate, exact-path launch menus");
    return 0;
}
