/* tiku_bench_layout.c -- layout.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static tiku_rect_t card_in(tiku_rect_t r);
static const char *button_label(int id);
static int toggle_value(const bench_t *st, int id);

/* The desktop-kit frontend: the GTK window's shape at the desktop's size,
 * a column of cards about the board and the run beside the tabbed views. */
const tiku_rect_t TIKU_BENCH_ALERT_RECT = {200, 196, 560, 220};
tiku_rect_t tiku_bench_tabs_rect(const bench_t *st)
{ return (tiku_rect_t){RX, 52, RW, 28}; }
tiku_rect_t tiku_bench_screen_rect(const bench_t *st)
{ return (tiku_rect_t){0, 0, st->w, st->h}; }
/* The cards, top to bottom, and the two buttons under them. */
const tiku_rect_t TIKU_BENCH_CARD_DEVICE = {10, 6, 238, 72};
const tiku_rect_t TIKU_BENCH_CARD_HISTORY = {10, 82, 238, 102};
const tiku_rect_t TIKU_BENCH_CARD_LINK = {10, 188, 238, 70};
const tiku_rect_t TIKU_BENCH_CARD_MODE = {10, 262, 238, 64};
const tiku_rect_t TIKU_BENCH_CARD_OPTIONS = {10, 330, 238, 64};
const tiku_rect_t TIKU_BENCH_CARD_EPAPER = {10, 398, 238, 76};
const tiku_rect_t TIKU_BENCH_CARD_MAKE = {10, 478, 238, 76};
static const tiku_rect_t RUN_RECT = {10, 560, 150, 26};
static const tiku_rect_t STOP_RECT = {168, 560, 80, 26};
/** Inside a card: what tiku_ui_group leaves once it has drawn the frame. */
static tiku_rect_t card_in(tiku_rect_t r)
{
    int top = tiku_font_plain()->height + 4;
    return (tiku_rect_t){r.x + 8, r.y + top, r.w - 16, r.h - top - 8};
}
tiku_rect_t tiku_bench_target_rect(void)
{
    tiku_rect_t in = card_in(TIKU_BENCH_CARD_DEVICE);
    return (tiku_rect_t){in.x, in.y, in.w, 24};
}
tiku_rect_t tiku_bench_preset_rect(int pattern)
{
    tiku_rect_t in = card_in(TIKU_BENCH_CARD_EPAPER);
    return (tiku_rect_t){in.x + 58, in.y + (pattern ? 26 : 0), in.w - 58, 22};
}
/* The views' bottom edge moves with the window; the cards do not. */
tiku_rect_t tiku_bench_field_rect(const bench_t *st, int i)
{
    tiku_rect_t in;
    if (!i) return (tiku_rect_t){RX + 48, 90, 396, 24};
    if (i == 4) return (tiku_rect_t){RX, st->h - 84, 380, 24};
    if (i == F_FWVARS - F_FILTER)
        return (tiku_rect_t){RX + 60, st->h - 198, RW - 60, 24};
    if (i == 1) {
        in = card_in(TIKU_BENCH_CARD_LINK);
        return (tiku_rect_t){in.x + 48, in.y + 20, in.w - 48, 22};
    }
    in = card_in(TIKU_BENCH_CARD_MAKE);
    return (tiku_rect_t){in.x + 58, in.y + (i == 3 ? 26 : 0), in.w - 58, 22};
}
tiku_rect_t tiku_bench_list_rect(const bench_t *st, int tab)
{
    if (tab == TAB_SUITES) return (tiku_rect_t){RX, 122, RW, st->h - 220};
    if (tab == TAB_OUTPUT || tab == TAB_CONSOLE)
        return (tiku_rect_t){RX, 90, RW, st->h - 180};
    if (tab == TAB_HISTORY) return (tiku_rect_t){RX, 118, RW, st->h - 178};
    if (tab == TAB_FIRMWARE) return (tiku_rect_t){RX, 148, RW, st->h - 352};
    return (tiku_rect_t){RX, 90, RW, st->h - 150};
}
void tiku_bench_text(bench_t *st, tiku_rect_t r, const char *value, int bold)
{
    tiku_text_line_draw(st->surface, r, bold ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN,
                        value, TIKU_C_TEXT);
}

int tiku_bench_selected_count(const bench_t *st)
{
    int n = 0;
    for (int i = 0; i < st->nsuite; i++) n += st->suites[i].selected;
    for (int i = 0; i < st->nleaf; i++) n += st->leaves[i].selected;
    return n;
}
int tiku_bench_editable(const bench_t *st)
{ return !st->busy && !st->loading && !st->confirm.open; }

void tiku_bench_controls(bench_t *st)
{
    int edit = tiku_bench_editable(st), tab = st->tabs.current;
    const board_t *b = tiku_bench_board_info(st);
    tiku_rect_t in;
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, TIKU_DIALOG_NONE);
    tiku_dialog_add(&st->dialog, TARGET, tiku_bench_target_rect(), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, LIST, tiku_bench_list_rect(st, tab), TIKU_DIALOG_INPUT, 1);
    if (tab == TAB_SUITES) {
        tiku_dialog_add(&st->dialog, F_FILTER, tiku_bench_field_rect(st, 0),
                        TIKU_DIALOG_INPUT, edit);
        tiku_dialog_add(&st->dialog, SELECT_ALL,
                        (tiku_rect_t){RX + 452, 90, 70, 24},
                        TIKU_DIALOG_BUTTON, edit);
        tiku_dialog_add(&st->dialog, CLEAR,
                        (tiku_rect_t){RX + 530, 90, 70, 24},
                        TIKU_DIALOG_BUTTON, edit);
        tiku_dialog_add(&st->dialog, REFRESH,
                        (tiku_rect_t){RX + 608, 90, 82, 24},
                        TIKU_DIALOG_BUTTON, edit);
    } else if (tab == TAB_OUTPUT || tab == TAB_CONSOLE) {
        int live = st->busy && st->child > 0 && !st->cancelled &&
                   st->send_at == st->send_n;
        tiku_dialog_add(&st->dialog, F_INPUT, tiku_bench_field_rect(st, 4),
                        TIKU_DIALOG_INPUT, live);
        tiku_dialog_add(&st->dialog, SEND,
                        (tiku_rect_t){RX + 388, st->h - 84, 60, 24},
                        TIKU_DIALOG_BUTTON, live);
        tiku_dialog_add(&st->dialog, COPY_LOG,
                        (tiku_rect_t){RX + 456, st->h - 84, 110, 24},
                        TIKU_DIALOG_BUTTON, st->nlog > 0);
        tiku_dialog_add(&st->dialog, CLEAR_LOG,
                        (tiku_rect_t){RX + 574, st->h - 84, 116, 24},
                        TIKU_DIALOG_BUTTON, !st->busy);
    } else if (tab == TAB_FIRMWARE) {
        const int ready = tiku_bench_fw_ready(st);
        tiku_dialog_add(&st->dialog, FW_PRESET, tiku_bench_fw_preset_rect(st),
                        TIKU_DIALOG_INPUT, edit && st->fw.npreset > 0);
        tiku_dialog_add(&st->dialog, F_FWVARS,
                        tiku_bench_field_rect(st, F_FWVARS - F_FILTER),
                        TIKU_DIALOG_INPUT, edit);
        tiku_dialog_add(&st->dialog, FW_PREVIEW,
                        (tiku_rect_t){RX, st->h - 84, 100, 24},
                        TIKU_DIALOG_BUTTON, ready);
        tiku_dialog_add(&st->dialog, FW_INSTALL,
                        (tiku_rect_t){RX + 108, st->h - 84, 110, 24},
                        TIKU_DIALOG_BUTTON, ready);
    }
    /* The cards: the connection, the build mode, the options, the ePaper
     * presets and the make fields, always in reach. */
    tiku_dialog_add(&st->dialog, F_BAUD, tiku_bench_field_rect(st, 1), TIKU_DIALOG_INPUT,
                    edit && !st->usb);
    in = card_in(TIKU_BENCH_CARD_MODE);
    tiku_dialog_add(&st->dialog, FLASH, (tiku_rect_t){in.x, in.y, in.w, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    tiku_dialog_add(&st->dialog, MONITOR,
                    (tiku_rect_t){in.x, in.y + 18, in.w, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    in = card_in(TIKU_BENCH_CARD_OPTIONS);
    tiku_dialog_add(&st->dialog, LARGE, (tiku_rect_t){in.x, in.y, 108, 18},
                    TIKU_DIALOG_TOGGLE, edit && b && b->large);
    tiku_dialog_add(&st->dialog, USB,
                    (tiku_rect_t){in.x + 112, in.y, in.w - 112, 18},
                    TIKU_DIALOG_TOGGLE, edit && b && b->usb);
    tiku_dialog_add(&st->dialog, PK, (tiku_rect_t){in.x, in.y + 18, 108, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    tiku_dialog_add(&st->dialog, EPAPER,
                    (tiku_rect_t){in.x + 112, in.y + 18, in.w - 112, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    tiku_dialog_add(&st->dialog, PANEL, tiku_bench_preset_rect(0), TIKU_DIALOG_INPUT,
                    edit && st->epaper);
    tiku_dialog_add(&st->dialog, PATTERN, tiku_bench_preset_rect(1), TIKU_DIALOG_INPUT,
                    edit && st->epaper);
    tiku_dialog_add(&st->dialog, F_MAKE, tiku_bench_field_rect(st, 2), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, F_FLAGS, tiku_bench_field_rect(st, 3), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, RUN, RUN_RECT, TIKU_DIALOG_BUTTON,
                    edit && !st->child && st->metadata_ok &&
                    tiku_bench_selected_count(st));
    /* Not while the part is being written: a flash cut short leaves it
     * half-programmed, so that step always runs to its end. */
    tiku_dialog_add(&st->dialog, STOP, STOP_RECT, TIKU_DIALOG_BUTTON,
                    (st->busy || st->loading) && !st->reclaiming &&
                    !(st->fw.step == FWS_FLASH &&
                      !strcmp(st->fw.stage[FW_FLASH], "start")));
    tiku_dialog_end(&st->dialog);
}

static const char *button_label(int id)
{
    switch (id) {
    case RUN: return "Run selected"; case STOP: return "Stop";
    case SELECT_ALL: return "All"; case CLEAR: return "None";
    case REFRESH: return "Refresh"; case COPY_LOG: return "Copy log";
    case CLEAR_LOG: return "Clear view"; case SEND: return "Send";
    case FLASH: return "Build + flash, then monitor";
    case MONITOR: return "Monitor existing firmware";
    case LARGE: return "MSP430 large"; case USB: return "RP2350 USB";
    case PK: return "CRACEN PK"; case EPAPER: return "ePaper kit";
    case FW_PREVIEW: return "Preview"; case FW_INSTALL: return "Install...";
    default: return "";
    }
}
static int toggle_value(const bench_t *st, int id)
{
    return id == FLASH ? st->flash : id == MONITOR ? !st->flash :
           id == LARGE ? st->large : id == USB ? st->usb :
           id == PK ? st->pk : st->epaper;
}

void tiku_bench_controls_draw(bench_t *st)
{
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_focused(&st->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&st->dialog, c->id)) flags |= TIKU_S_PRESSED;
        if (c->id == LIST) continue;
        if (c->id == TARGET) {
            st->target.enabled = c->enabled;
            tiku_choice_draw(&st->target, st->surface, c->rect,
                st->target.selected >= 0 ? NULL : "No device (host tests)",
                flags);
        } else if (c->id == FW_PRESET) {
            st->fw.choice.enabled = c->enabled;
            tiku_choice_draw(&st->fw.choice, st->surface, c->rect,
                st->fw.choice.selected >= 0 ? NULL : "No presets", flags);
        } else if (c->id == PANEL || c->id == PATTERN) {
            tiku_choice_t *ch = c->id == PANEL ? &st->epanel : &st->epattern;
            ch->enabled = c->enabled;
            tiku_choice_draw(ch, st->surface, c->rect, NULL, flags);
        } else if (c->id >= F_FILTER) {
            int f = c->id - F_FILTER;
            tiku_ui_textfield_scroll(st->surface, c->rect, st->field[f].text,
                st->field[f].caret, st->field[f].sel_a, st->field[f].sel_b, flags, st->input[f].scroll);
        } else if (c->id == FLASH || c->id == MONITOR)
            tiku_ui_radio(st->surface, c->rect, button_label(c->id),
                          flags | (toggle_value(st, c->id) ? TIKU_S_ON : 0));
        else if (c->kind == TIKU_DIALOG_TOGGLE)
            tiku_ui_checkbox(st->surface, c->rect, button_label(c->id),
                             flags | (toggle_value(st, c->id) ? TIKU_S_ON : 0));
        else tiku_ui_button(st->surface, c->rect, button_label(c->id), flags);
    }
}
