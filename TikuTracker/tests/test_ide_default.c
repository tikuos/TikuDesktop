/* TikuBasic follows the desktop default, including changes made
 * while editors are open. */
#include <assert.h>
#define TIKU_APP_EMBED
#include "../../apps/ide/tiku_ide.c"

static const char *chosen = "";
static int project_updates, editor_updates;
static tiku_menuset_t editor_menus;

static int current(void *ctx, char *out, size_t max)
{
    (void)ctx;
    return snprintf(out, max, "%s", chosen);
}

static int menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    (void)ctx;
    assert(set->nmenu == 3);
    assert(strcmp(set->menu[2].title, "Window") == 0);
    if (id == 1) project_updates++;
    else {
        editor_updates++;
        editor_menus = *set;
    }
    return 0;
}

int main(void)
{
    tiku_app_services_t services = { .current_device = current,
                                     .menus = menus };
    ide_t *st = calloc(1, sizeof *st);
    char dest[64];

    assert(st != NULL);
    st->services = &services;
    st->proj_id = 1;
    st->edit[0].id = 2;
    st->edit[0].lang = TIKU_SYNTAX_BASIC;
    st->edit[0].surface = tiku_surface_new(1, 1, 0);
    tiku_textview_init(&st->edit[0].tv);
    snprintf(st->edit[0].name, sizeof st->edit[0].name, "main.bas");
    snprintf(st->proj.board, sizeof st->proj.board, "legacy-board");
    assert(!dest_board(st, dest, sizeof dest));
    chosen = "nrf54l15";
    assert(dest_board(st, dest, sizeof dest));
    assert(strcmp(dest, chosen) == 0);
    snprintf(st->proj.board, sizeof st->proj.board, "(this machine)");
    assert(dest_board(st, dest, sizeof dest));
    ide_board_said(st, TIKU_APP_BOARD_CURRENT, chosen);
    assert(project_updates == 1 && editor_updates == 1);
    assert(strcmp(editor_menus.menu[0].item[1].label, "Run on nrf54l15") == 0);
    st->running = 1;
    snprintf(st->board, sizeof st->board, "nrf54l15");
    chosen = "nrf54lm20b";
    ide_board_said(st, TIKU_APP_BOARD_CURRENT, chosen);
    assert(strcmp(st->board, "nrf54l15") == 0 && st->running);
    assert(strcmp(editor_menus.menu[0].item[1].label,
                  "Run on nrf54lm20b") == 0);
    assert(!editor_menus.menu[0].item[1].enabled);
    chosen = "";
    ide_board_said(st, TIKU_APP_BOARD_CURRENT, chosen);
    assert(!dest_board(st, dest, sizeof dest));
    assert(strcmp(editor_menus.menu[0].item[1].label, "Run main.bas") == 0);
    services.current_device = NULL;
    assert(!dest_board(st, dest, sizeof dest));
    tiku_surface_free(st->edit[0].surface);
    free(st);
    puts("PASS: desktop defaults, legacy projects, menu refresh"
         " and active-run isolation");
    return 0;
}
