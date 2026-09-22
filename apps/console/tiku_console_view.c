/* tiku_console_view.c -- view.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_console_private.h"

static tiku_rect_t field_rect(int i);
static void text(console_t *st, tiku_rect_t r, const char *s, int bold);
static void add_button(console_t *st, int id, tiku_rect_t r);
static void controls_draw(console_t *st);
static tiku_rgb_t glyph_ink(const glyph_t *g);
static void console_draw(console_t *st);
static void device_draw(console_t *st);

/* One native window; shared focus, choices, fields, lists and sensor chart. */
const tiku_rect_t TIKU_CONSOLE_TARGET_RECT = {88, 12, 280, 28};
const tiku_rect_t TIKU_CONSOLE_TABS_RECT = {16, 52, 928, 28};
const tiku_rect_t TIKU_CONSOLE_BODY = {16, 140, 928, 354};

static tiku_rect_t field_rect(int i)
{
    const tiku_rect_t r[] = {{110, 546, 688, 30}, {72, 94, 362, 28},
        {100, 114, 170, 28}, {126, 102, 230, 28}, {448, 102, 70, 28}};
    return r[i];
}
static void text(console_t *st, tiku_rect_t r, const char *s, int bold)
{
    tiku_text_line_draw(st->surface, r,
        bold ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN, s, TIKU_C_TEXT);
}
const char *tiku_console_label(const console_t *st, int id)
{
    switch (id) {
    case START: return "Open console"; case RETURN: return "Return to desktop";
    case FILES: return "Files..."; case SEND: return "Send";
    case BREAK: return "Interrupt (^C)"; case CLEAR: return "Clear view";
    case COPY: return "Copy filtered history"; case COPY_LINE: return "Copy selected line";
    case FOLLOW: return "Follow tail"; case RECORD: return st->log ? "Stop recording" : "Record...";
    case LOG_PATH: return "Copy log path"; case PING: return "Ping";
    case PING_STOP: return "Stop ping"; case SLIP_ON: return "SLIP on";
    case SLIP_OFF: return "SLIP off"; case INFO: return "Board info";
    case MEMORY: return "Memory"; case WIFI_SCAN: return "WiFi scan";
    case WIFI_STATUS: return "WiFi status";
    case DIRECT: return st->direct ? "Input: Direct" : "Input: Command";
    default: return "";
    }
}

int tiku_console_enabled(const console_t *st, int id)
{
    if (id == START) return st->state == IDLE && st->targets.selected >= 0;
    if (id == RETURN) return st->state == ACTIVE || st->state == RELEASING;
    if (id == FILES) return st->state == IDLE && st->targets.selected >= 0;
    if (id == SEND || id == BREAK || id == SLIP_ON || id == SLIP_OFF ||
        id == INFO || id == MEMORY) return st->state == ACTIVE;
    if (id == PING) return st->state == ACTIVE && !st->ping_active;
    if (id == PING_STOP) return st->ping_active;
    if (id == WIFI_SCAN || id == WIFI_STATUS)
        return st->state == ACTIVE && tiku_console_contains(st->mcu, "rp2350");
    if (id == LOG_PATH) return *st->log_path != 0;
    return 1;
}

static void add_button(console_t *st, int id, tiku_rect_t r)
{ tiku_dialog_add(&st->dialog, id, r, TIKU_DIALOG_BUTTON, tiku_console_enabled(st, id)); }

void tiku_console_controls(console_t *st)
{
    st->targets.enabled = st->state == IDLE;
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, TIKU_DIALOG_NONE);
    tiku_dialog_add(&st->dialog, TARGET, TIKU_CONSOLE_TARGET_RECT, TIKU_DIALOG_INPUT, st->state == IDLE);
    add_button(st, START, (tiku_rect_t){382, 10, 154, 32});
    add_button(st, RETURN, (tiku_rect_t){548, 10, 218, 32});
    /* Files may request a handoff while the console is active. */
    tiku_dialog_add(&st->dialog, FILES, (tiku_rect_t){778, 10, 166, 32},
        TIKU_DIALOG_BUTTON, st->state == ACTIVE || tiku_console_enabled(st, FILES));
    if (st->tabs.current == CONSOLE) {
        tiku_dialog_add(&st->dialog, FIELD + 1, field_rect(1), TIKU_DIALOG_INPUT, 1);
        add_button(st, FOLLOW, (tiku_rect_t){448, 94, 128, 28});
        add_button(st, CLEAR, (tiku_rect_t){588, 94, 120, 28});
        add_button(st, RECORD, (tiku_rect_t){720, 94, 224, 28});
        tiku_dialog_add(&st->dialog, LIST, TIKU_CONSOLE_BODY, TIKU_DIALOG_INPUT, 1);
        add_button(st, DIRECT, (tiku_rect_t){16, 506, 180, 28});
        add_button(st, BREAK, (tiku_rect_t){210, 506, 180, 28});
        add_button(st, COPY_LINE, (tiku_rect_t){404, 506, 226, 28});
        add_button(st, LOG_PATH, (tiku_rect_t){644, 506, 180, 28});
        tiku_dialog_add(&st->dialog, FIELD, field_rect(0), TIKU_DIALOG_INPUT, st->state == ACTIVE);
        add_button(st, SEND, (tiku_rect_t){814, 546, 130, 30});
    } else if (st->tabs.current == NETWORK) {
        for (int i = 3; i < 5; i++) tiku_dialog_add(&st->dialog, FIELD + i,
            field_rect(i), TIKU_DIALOG_INPUT, !st->ping_active);
        add_button(st, PING, (tiku_rect_t){536, 102, 112, 28});
        add_button(st, PING_STOP, (tiku_rect_t){660, 102, 130, 28});
        int ids[] = {SLIP_ON, SLIP_OFF, WIFI_SCAN, WIFI_STATUS};
        for (int i = 0; i < 4; i++) add_button(st, ids[i], (tiku_rect_t){16 + i * 186, 484, 174, 30});
    } else {
        tiku_dialog_add(&st->dialog, FIELD + 2, field_rect(2), TIKU_DIALOG_INPUT, st->state == IDLE);
        add_button(st, INFO, (tiku_rect_t){290, 114, 160, 28});
        add_button(st, MEMORY, (tiku_rect_t){466, 114, 160, 28});
    }
    tiku_dialog_end(&st->dialog);
}

static void controls_draw(console_t *st)
{
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_focused(&st->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&st->dialog, c->id)) flags |= TIKU_S_PRESSED;
        if (c->id == LIST) continue;
        if (c->id == TARGET) {
            st->targets.enabled = c->enabled;
            tiku_choice_draw(&st->targets, st->surface, TIKU_CONSOLE_TARGET_RECT,
                st->targets.selected >= 0 ? NULL : "Select connected device", flags);
        } else if (c->id >= FIELD) {
            int k = c->id - FIELD;
            tiku_ui_textfield_scroll(st->surface, c->rect, st->field[k].text,
                st->field[k].caret, st->field[k].sel_a, st->field[k].sel_b, flags, st->input[k].scroll);
        } else tiku_ui_button(st->surface, c->rect, tiku_console_label(st, c->id), flags);
    }
}

static tiku_rgb_t glyph_ink(const glyph_t *g)
{
    const tiku_rgb_t colours[] = {TIKU_RGB(60,60,60), TIKU_RGB(178,35,35),
        TIKU_RGB(20,115,40), TIKU_RGB(135,100,0), TIKU_RGB(35,80,180),
        TIKU_RGB(140,45,145), TIKU_RGB(0,120,140), TIKU_RGB(100,100,100)};
    return g->ink < 16 ? colours[g->ink % 8] : TIKU_C_TEXT;
}

static void console_draw(console_t *st)
{
    tiku_console_rebuild(st);
    tiku_list_set_row_h(&st->list, tiku_font_mono(0)->height + 3);
    if (st->follow) tiku_list_scroll_to(&st->list, st->visible, TIKU_CONSOLE_BODY);
    tiku_fill(st->surface, TIKU_CONSOLE_BODY, TIKU_C_DOC);
    int rows = tiku_list_visible_rows(&st->list, TIKU_CONSOLE_BODY), cell = tiku_font_mono_cell(0);
    for (int i = st->list.top; i < st->visible && i < st->list.top + rows; i++) {
        tiku_rect_t r = tiku_list_row_rect(&st->list, TIKU_CONSOLE_BODY, i);
        const line_t *l = &st->lines[st->index[i]];
        int selected = tiku_list_selected(&st->list, i);
        if (selected) tiku_fill(st->surface, r, TIKU_C_SELECT);
        for (int j = 0; j < l->length; j++) {
            const glyph_t *g = &l->cell[j];
            tiku_text(st->surface, tiku_font_mono(g->bold), r.x + 6 + j * cell,
                r.y + tiku_font_mono(0)->ascent + 1, g->glyph,
                selected ? TIKU_C_SELTEXT : glyph_ink(g));
        }
    }
    text(st, (tiku_rect_t){16, 578, 928, 22}, st->direct ?
        "Direct mode: click output to type; Tab returns to controls. Paste only accepts one line." :
        "Command mode: Enter sends one line; Up/Down recalls history. Scroll to pause following.", 0);
}

static void device_draw(console_t *st)
{
    text(st, (tiku_rect_t){16, 90, 928, 22}, "Serial settings and device snapshot", 1);
    text(st, (tiku_rect_t){16, 116, 80, 24}, "Baud", 0);
    char line[512]; snprintf(line, sizeof line, "%s | %s", *st->mcu ? st->mcu : "No device", st->port);
    text(st, (tiku_rect_t){16, 164, 928, 26}, line, 1);
    char stats[4096]; memcpy(stats, st->stats, sizeof stats);
    char *save = NULL; int y = 200;
    if (!*stats) {
        /* The snapshot comes from a service only the in-process host
         * offers: say the tab is empty for that reason rather than
         * leaving a blank page that reads as a board with nothing. */
        text(st, (tiku_rect_t){32, y, 896, 24},
             st->services->device_stats
                 ? "No snapshot yet; the desk is still asking the board."
                 : "This host does not offer a device snapshot.", 0);
        y += 26;
    }
    for (char *p = strtok_r(stats, "\n", &save); p && y < 492; p = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(p, '\t');
        if (tab) { *tab++ = 0; text(st, (tiku_rect_t){32, y, 260, 24}, p, 1);
            text(st, (tiku_rect_t){300, y, 628, 24}, tab, 0); }
        else text(st, (tiku_rect_t){32, y, 896, 24}, p, 0);
        y += 26;
    }
    text(st, (tiku_rect_t){16, 516, 928, 24}, "Snapshot captured before opening the console; Board info / Memory print live output.", 0);
    text(st, (tiku_rect_t){16, 548, 928, 24}, "Firmware build profiles remain in standalone TikuConsole. Bench runs test firmware.", 0);
    text(st, (tiku_rect_t){16, 576, 928, 24}, "Baud affects this console session only; it does not reconfigure device firmware.", 0);
}

void tiku_console_paint(console_t *st)
{
    tiku_console_controls(st);
    tiku_fill(st->surface, (tiku_rect_t){0,0,CW,CH}, TIKU_C_PANEL);
    text(st, (tiku_rect_t){16,12,68,28}, "Device", 1);
    tiku_tabs_draw(&st->tabs, st->surface, TIKU_CONSOLE_TABS_RECT);
    if (st->tabs.current == CONSOLE) {
        text(st, (tiku_rect_t){16,94,56,28}, "Find", 0);
        text(st, (tiku_rect_t){16,546,92,30}, "Command", 0);
        console_draw(st);
    } else if (st->tabs.current == NETWORK) {
        text(st, (tiku_rect_t){16,102,110,28}, "Board IPv4", 0);
        text(st, (tiku_rect_t){370,102,76,28}, "Probes", 0);
        text(st, (tiku_rect_t){16,144,928,28}, st->net_note, 0);
        if (st->ping_active) st->plot.to_us = st->now + 100000;
        if (st->plot.to_us <= st->plot.from_us) st->plot.to_us = st->plot.from_us + 1000000;
        tiku_sensor_plot_draw(&st->plot, st->surface, (tiku_rect_t){16,184,928,278});
        text(st, (tiku_rect_t){16,526,928,24}, "Host SLIP address: 172.16.7.1. Ping enables SLIP; replies appear in Console.", 0);
        text(st, (tiku_rect_t){16,552,928,24}, "No TUN / NAT or Internet forwarding. Use standalone TikuConsole for that bridge.", 0);
        text(st, (tiku_rect_t){16,578,928,24}, "WiFi controls require an RP2350 WiFi-enabled firmware build.", 0);
    } else device_draw(st);
    controls_draw(st);
    char status[640]; snprintf(status, sizeof status, "%s | RX %llu / TX %llu B",
        st->note, st->received, st->sent);
    text(st, (tiku_rect_t){16,608,928,24}, status, 0);
    if (st->targets.open) tiku_choice_draw_popup(&st->targets, st->surface,
        TIKU_CONSOLE_TARGET_RECT, (tiku_rect_t){0,0,CW,CH});
    st->services->frame(st->services->ctx, st->id, st->surface->px, CW, CH);
}
