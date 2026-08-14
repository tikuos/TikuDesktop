/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_addon.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_addons_init(tiku_trk_addons_t *a)
{ if (a != NULL) memset(a, 0, sizeof *a); }

int
tiku_trk_addons_register(tiku_trk_addons_t *a, const char *name,
                         const char *command, const char *type, char key)
{
    tiku_trk_addon_t *x;
    if (a == NULL || name == NULL || name[0] == '\0' ||
        a->count >= TIKU_TRK_ADDON_MAX) return -1;
    x = &a->item[a->count++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", name);
    snprintf(x->command, sizeof x->command, "%s", command != NULL ? command : "");
    snprintf(x->type, sizeof x->type, "%s", type != NULL ? type : "*");
    x->generic = strcmp(x->type, "*") == 0;
    x->key = key;
    x->mods = 0x3u; /* Command+Option; the command key is always forced. */
    a->generation++;
    return a->count - 1;
}

int
tiku_trk_addons_rebind(tiku_trk_addons_t *a, const char *settings)
{
    const char *p;
    int changed = 0;
    if (a == NULL || settings == NULL) return 0;
    for (p = settings; *p != '\0'; ) {
        const char *eq = strchr(p, '=');
        const char *nl = strchr(p, '\n');
        size_t n = eq != NULL ? (size_t)(eq - p) : 0;
        if (nl == NULL) nl = p + strlen(p);
        if (eq != NULL && eq < nl && n > 0 && eq + 1 < nl) {
            int i;
            for (i = 0; i < a->count; i++) {
                if (strlen(a->item[i].name) == n &&
                    strncmp(a->item[i].name, p, n) == 0) {
                    a->item[i].key = eq[1];
                    changed++;
                    break;
                }
            }
        }
        p = (*nl != '\0') ? nl + 1 : nl;
    }
    if (changed) a->generation++;
    return changed;
}

int
tiku_trk_addons_accepts(const tiku_trk_addon_t *a, const char *const *types,
                        int ntypes)
{
    int i;
    if (a == NULL) return 0;
    if (a->generic) return 1;
    for (i = 0; i < ntypes; i++) {
        if (types[i] != NULL && strcmp(a->type, types[i]) == 0) return 1;
    }
    return 0;
}

static int
before(const tiku_trk_addon_t *a, const tiku_trk_addon_t *b,
       const char *const *types, int ntypes)
{
    int aa = tiku_trk_addons_accepts(a, types, ntypes);
    int ab = tiku_trk_addons_accepts(b, types, ntypes);
    if (aa && ab && a->generic != b->generic) return !a->generic;
    if (aa != ab) return aa > ab;
    return strcmp(a->name, b->name) < 0;
}

int
tiku_trk_addons_build_menu(const tiku_trk_addons_t *a, const char *const *types,
                           int ntypes, tiku_trk_menu_t *menu, int base)
{
    int order[TIKU_TRK_ADDON_MAX], i, j;
    if (a == NULL || menu == NULL) return 0;
    tiku_trk_menu_clear(menu);
    for (i = 0; i < a->count; i++) order[i] = i;
    for (i = 1; i < a->count; i++) {
        int hold = order[i];
        for (j = i; j > 0 && before(&a->item[hold], &a->item[order[j-1]], types, ntypes); j--)
            order[j] = order[j-1];
        order[j] = hold;
    }
    for (i = 0; i < a->count && menu->count < TIKU_TRK_MENU_ITEMS_MAX; i++) {
        const tiku_trk_addon_t *x = &a->item[order[i]];
        (void)tiku_trk_menu_add(menu, x->name, base + order[i], x->key,
                                x->mods, 1);
    }
    return menu->count;
}

int
tiku_trk_addons_invoke(const tiku_trk_addons_t *a, int command, int base,
                       tiku_trk_addon_invoke_fn fn, void *context)
{
    int index = command - base;

    if (a == NULL || fn == NULL || index < 0 || index >= a->count) {
        return 0;
    }
    return fn(&a->item[index], context) != 0;
}
