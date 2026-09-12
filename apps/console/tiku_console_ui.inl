/* One native window; shared focus, choices, fields, lists and sensor chart. */
static const tiku_rect_t TARGET_RECT = {88, 12, 280, 28};
static const tiku_rect_t TABS_RECT = {16, 52, 928, 28};
static const tiku_rect_t BODY = {16, 140, 928, 354};

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
static const char *label(const console_t *st, int id)
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

static int enabled(const console_t *st, int id)
{
    if (id == START) return st->state == IDLE && st->targets.selected >= 0;
    if (id == RETURN) return st->state == ACTIVE || st->state == RELEASING;
    if (id == FILES) return st->state == IDLE && st->targets.selected >= 0;
    if (id == SEND || id == BREAK || id == SLIP_ON || id == SLIP_OFF ||
        id == INFO || id == MEMORY) return st->state == ACTIVE;
    if (id == PING) return st->state == ACTIVE && !st->ping_active;
    if (id == PING_STOP) return st->ping_active;
    if (id == WIFI_SCAN || id == WIFI_STATUS)
        return st->state == ACTIVE && contains(st->mcu, "rp2350");
    if (id == LOG_PATH) return *st->log_path != 0;
    return 1;
}

static void add_button(console_t *st, int id, tiku_rect_t r)
{ tiku_dialog_add(&st->dialog, id, r, TIKU_DIALOG_BUTTON, enabled(st, id)); }

static void controls(console_t *st)
{
    st->targets.enabled = st->state == IDLE;
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, TIKU_DIALOG_NONE);
    tiku_dialog_add(&st->dialog, TARGET, TARGET_RECT, TIKU_DIALOG_INPUT, st->state == IDLE);
    add_button(st, START, (tiku_rect_t){382, 10, 154, 32});
    add_button(st, RETURN, (tiku_rect_t){548, 10, 218, 32});
    /* Files may request a handoff while the console is active. */
    tiku_dialog_add(&st->dialog, FILES, (tiku_rect_t){778, 10, 166, 32},
        TIKU_DIALOG_BUTTON, st->state == ACTIVE || enabled(st, FILES));
    if (st->tabs.current == CONSOLE) {
        tiku_dialog_add(&st->dialog, FIELD + 1, field_rect(1), TIKU_DIALOG_INPUT, 1);
        add_button(st, FOLLOW, (tiku_rect_t){448, 94, 128, 28});
        add_button(st, CLEAR, (tiku_rect_t){588, 94, 120, 28});
        add_button(st, RECORD, (tiku_rect_t){720, 94, 224, 28});
        tiku_dialog_add(&st->dialog, LIST, BODY, TIKU_DIALOG_INPUT, 1);
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
            tiku_choice_draw(&st->targets, st->surface, TARGET_RECT,
                st->targets.selected >= 0 ? NULL : "Select connected device", flags);
        } else if (c->id >= FIELD) {
            int k = c->id - FIELD;
            tiku_ui_textfield_scroll(st->surface, c->rect, st->field[k].text,
                st->field[k].caret, st->field[k].sel_a, st->field[k].sel_b, flags, st->input[k].scroll);
        } else tiku_ui_button(st->surface, c->rect, label(st, c->id), flags);
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
    rebuild(st);
    tiku_list_set_row_h(&st->list, tiku_font_mono(0)->height + 3);
    if (st->follow) tiku_list_scroll_to(&st->list, st->visible, BODY);
    tiku_fill(st->surface, BODY, TIKU_C_DOC);
    int rows = tiku_list_visible_rows(&st->list, BODY), cell = tiku_font_mono_cell(0);
    for (int i = st->list.top; i < st->visible && i < st->list.top + rows; i++) {
        tiku_rect_t r = tiku_list_row_rect(&st->list, BODY, i);
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

static void paint(console_t *st)
{
    controls(st);
    tiku_fill(st->surface, (tiku_rect_t){0,0,CW,CH}, TIKU_C_PANEL);
    text(st, (tiku_rect_t){16,12,68,28}, "Device", 1);
    tiku_tabs_draw(&st->tabs, st->surface, TABS_RECT);
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
        TARGET_RECT, (tiku_rect_t){0,0,CW,CH});
    st->services->frame(st->services->ctx, st->id, st->surface->px, CW, CH);
}

static void publish(console_t *st)
{
    tiku_menuset_t menus = {0}; menus.nmenu = 2;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "Console");
    const int ids[] = {START, RETURN, FILES, BREAK, COPY, COPY_LINE, CLEAR, FOLLOW, RECORD, LOG_PATH, DIRECT};
    menus.menu[0].nitem = sizeof ids / sizeof ids[0];
    for (int i = 0; i < menus.menu[0].nitem; i++) {
        snprintf(menus.menu[0].item[i].label, sizeof menus.menu[0].item[i].label, "%s", label(st, ids[i]));
        menus.menu[0].item[i].command = ids[i];
        menus.menu[0].item[i].enabled = ids[i] == FILES && st->state == ACTIVE ? 1 : enabled(st, ids[i]);
    }
    snprintf(menus.menu[1].title, sizeof menus.menu[1].title, "View"); menus.menu[1].nitem = 3;
    for (int i = 0; i < 3; i++) {
        snprintf(menus.menu[1].item[i].label, sizeof menus.menu[1].item[i].label, "%s",
            i == 0 ? "Console" : i == 1 ? "Network" : "Device");
        menus.menu[1].item[i].command = 100 + i; menus.menu[1].item[i].enabled = 1;
    }
    if (st->services->menus) st->services->menus(st->services->ctx, st->id, &menus);
}

static void copy_history(console_t *st, int selected)
{
    if (!st->services->clip_set) return;
    rebuild(st);
    char out[TIKU_CLIPBOARD_MAX] = ""; size_t used = 0;
    for (int i = 0; i < st->visible; i++) {
        if (selected && !tiku_list_selected(&st->list, i)) continue;
        char line[COLS * 4 + 1]; line_string(&st->lines[st->index[i]], line, sizeof line);
        size_t n = strlen(line);
        if (used + n + 2 > sizeof out) {
            snprintf(st->note, sizeof st->note, "Too much text for the clipboard. Narrow Find or copy one selected line.");
            return;
        }
        memcpy(out + used, line, n); used += n; out[used++] = '\n'; out[used] = 0;
    }
    if (used) st->services->clip_set(st->services->ctx, out);
}

static int action(console_t *st, int id)
{
    if (id >= 100 && id < 103) {
        tiku_tabs_select(&st->tabs, id - 100);
        if (id == 102 && st->state == IDLE && st->targets.selected >= 0) target_capture(st);
        st->dirty = 1; return 0;
    }
    if (!enabled(st, id) && !(id == FILES && st->state == ACTIVE)) return 0;
    if (id == START) begin(st);
    else if (id == RETURN || id == FILES) {
        st->files_pending = id == FILES;
        if (st->state == ACTIVE) reclaim(st);
        else if (st->state == RELEASING) st->cancelled = 1;
        else if (id == FILES && st->state == IDLE) files_open(st);
    } else if (id == SEND) {
        char command[TIKU_FIELD_MAX + 1]; snprintf(command, sizeof command, "%s\r", st->field[0].text);
        if (queue_bytes(st, command, strlen(command))) {
            if (*st->field[0].text) history_add(st, st->field[0].text);
            tiku_field_start(&st->field[0], ""); tiku_field_input_init(&st->input[0]);
        }
    } else if (id == BREAK) {
        st->out_n = st->out_at = 0; st->ping_active = 0; queue_bytes(st, "\x03", 1);
    } else if (id == CLEAR) {
        memset(st->lines, 0, sizeof st->lines); st->count = 1; st->head = st->column = 0;
        tiku_list_init(&st->list, 1);
    } else if (id == COPY || id == COPY_LINE) copy_history(st, id == COPY_LINE);
    else if (id == FOLLOW) st->follow = 1;
    else if (id == RECORD) record_toggle(st);
    else if (id == LOG_PATH && st->services->clip_set) {
        st->services->clip_set(st->services->ctx, st->log_path);
        snprintf(st->note, sizeof st->note, "Log path copied: %s", st->log_path);
    } else if (id == DIRECT) { st->direct = !st->direct; st->dialog.focus = st->direct ? LIST : FIELD; }
    else if (id == PING) ping_start(st);
    else if (id == PING_STOP) { st->ping_active = 0; ping_summary(st); }
    else {
        const char *cmd = id == SLIP_ON ? "slip on\r" : id == SLIP_OFF ? "slip off\r" :
            id == INFO ? "info\r" : id == MEMORY ? "free\r" :
            id == WIFI_SCAN ? "wifi scan\r" : id == WIFI_STATUS ? "wifi status\r" : NULL;
        if (cmd) { if (id == SLIP_OFF) st->ping_active = 0;
            queue_bytes(st, cmd, strlen(cmd)); tiku_tabs_select(&st->tabs, CONSOLE); }
    }
    st->dirty = 1; return 0;
}

/** Direct typing is confined to the output pane; editing fields never hits a board. */
static int direct_event(console_t *st, const tiku_event_t *ev)
{
    int tr = tiku_text_input_event(&st->direct_commit, ev);
    if (tr == TIKU_TEXT_COMPLETE) {
        char plain[TIKU_TEXT_INPUT_MAX] = "";
        if (tiku_text_input_append(plain, sizeof plain, st->direct_commit.text))
            queue_bytes(st, plain, strlen(plain));
        else snprintf(st->note, sizeof st->note, "Text rejected: use one line without control characters.");
        return 1;
    }
    if (tr != TIKU_TEXT_IGNORED) return 1;
    if (ev->type != TIKU_EVENT_KEY_DOWN || ev->key == TIKU_KEY_TAB) return 0;
    unsigned k = ev->key;
    if ((ev->modifiers & TIKU_MOD_CMD) && (k == 'v' || k == 'V')) {
        char paste[TIKU_TEXT_INPUT_MAX] = "", plain[TIKU_TEXT_INPUT_MAX] = "";
        int length = st->services->clip_get ?
            st->services->clip_get(st->services->ctx, paste, sizeof paste) : 0;
        if (length < (int)sizeof paste - 1 && tiku_text_input_append(plain, sizeof plain, paste))
            queue_bytes(st, plain, strlen(plain));
        else snprintf(st->note, sizeof st->note, "Paste rejected: use one plain-text line without control characters.");
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && (k == 'c' || k == 'C')) { action(st, BREAK); return 1; }
    if (ev->modifiers & TIKU_MOD_CMD) {
        int lower = tolower((int)k);
        if (lower >= 'a' && lower <= 'z') {
            unsigned char control = (unsigned char)(lower - 'a' + 1);
            queue_bytes(st, &control, 1); return 1;
        }
        return 0;
    }
    const char *special = k == TIKU_KEY_RETURN ? "\r" : k == TIKU_KEY_BACKSPACE ? "\b" :
        k == TIKU_KEY_ESCAPE ? "\x1b" : k == TIKU_KEY_UP ? "\x1b[A" :
        k == TIKU_KEY_DOWN ? "\x1b[B" : k == TIKU_KEY_LEFT ? "\x1b[D" :
        k == TIKU_KEY_RIGHT ? "\x1b[C" : NULL;
    if (special) queue_bytes(st, special, strlen(special));
    else if (k >= 32 && k < 127) { char c = (char)k; queue_bytes(st, &c, 1); }
    else return 0;
    return 1;
}

static int console_event(void *state, uint32_t window, const tiku_event_t *ev)
{
    console_t *st = state; (void)window;
    if (ev->type == TIKU_EVENT_CLOSE) {
        if (st->state == IDLE) return 1;
        st->closing = st->cancelled = 1;
        if (st->state == ACTIVE) reclaim(st);
        return 0;
    }
    controls(st);
    tiku_choice_result_t cr = tiku_choice_event(&st->targets, ev, TARGET_RECT,
        (tiku_rect_t){0,0,CW,CH}, st->dialog.focus == TARGET);
    if (cr != TIKU_CHOICE_IGNORED) {
        if (cr == TIKU_CHOICE_CHANGED && st->state == IDLE && st->targets.selected >= 0) {
            snprintf(st->board, sizeof st->board, "%s", st->targets.item[st->targets.selected].label);
            target_changed(st);
        }
        st->dirty = 1; return 0;
    }
    if (st->tabs.current == CONSOLE && st->dialog.focus == LIST && st->direct && st->state == ACTIVE) {
        if (direct_event(st, ev)) { st->dirty = 1; return 0; }
    } else tiku_text_input_reset(&st->direct_commit);
    if (st->dialog.focus == FIELD && ev->type == TIKU_EVENT_KEY_DOWN &&
        (ev->key == TIKU_KEY_UP || ev->key == TIKU_KEY_DOWN)) {
        history_move(st, ev->key == TIKU_KEY_UP ? -1 : 1); st->dirty = 1; return 0;
    }
    if (st->dialog.focus == LIST && ev->type == TIKU_EVENT_KEY_DOWN && ev->key != TIKU_KEY_TAB) {
        st->follow = 0; tiku_list_key(&st->list, ev->key, ev->modifiers, BODY);
        st->dirty = 1; return 0;
    }
    tiku_dialog_result_t dr = tiku_dialog_event(&st->dialog, ev);
    if (dr.activated != TIKU_DIALOG_NONE) return action(st, dr.activated);
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        if (c->id < FIELD || !c->enabled) continue;
        int f = c->id - FIELD;
        tiku_field_result_t fr = tiku_field_event(&st->field[f], &st->input[f], ev,
            c->rect, st->dialog.focus == c->id);
        if (fr != TIKU_FIELD_IGNORED) {
            if (f == 0 && fr == TIKU_FIELD_COMMITTED) action(st, SEND);
            if (f == 1) { st->follow = 0; st->list.top = 0; tiku_list_select_none(&st->list); }
            st->dirty = 1; return 0;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int tab = tiku_tabs_hit(&st->tabs, TABS_RECT, ev->x, ev->y);
        if (tab >= 0) action(st, 100 + tab);
        else if (st->tabs.current == CONSOLE) {
            int row = tiku_list_at(&st->list, BODY, ev->x, ev->y);
            if (row >= 0) { st->dialog.focus = LIST; st->follow = 0;
                tiku_list_click(&st->list, row, ev->modifiers); }
        }
    } else if (ev->type == TIKU_EVENT_WHEEL && st->tabs.current == CONSOLE) {
        st->follow = 0; tiku_list_scroll_to(&st->list, st->list.top + ev->dy * 3, BODY);
    }
    st->dirty = 1; return 0;
}
