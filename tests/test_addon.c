#include <stdio.h>
#include <string.h>
#include "tiku_trk_addon.h"
int main(void) {
    tiku_trk_addons_t a; tiku_trk_menu_t m; const char *types[] = {"text/plain"};
    tiku_trk_addons_init(&a);
    tiku_trk_addons_register(&a, "Generic", "/bin/true", "*", 'g');
    tiku_trk_addons_register(&a, "Writer", "/bin/true", "text/plain", 'w');
    if (tiku_trk_addons_build_menu(&a, types, 1, &m, 1000) != 2 ||
        m.item[0].label[0] != 'W' || m.item[0].mods != 0x3u) return 1;
    if (tiku_trk_addons_rebind(&a, "Writer=x\n") != 1 || m.item[0].shortcut != 'w') return 1;
    tiku_trk_addons_build_menu(&a, types, 1, &m, 1000);
    if (m.item[0].shortcut != 'x') return 1;
    tiku_trk_menu_set_icon(&m, m.item[0].command, "text-file");
    if (strcmp(m.item[0].icon, "text-file") != 0) return 1;
    puts("[PASS] add-on inventory, MIME ordering and live shortcut rebinding"); return 0;
}
