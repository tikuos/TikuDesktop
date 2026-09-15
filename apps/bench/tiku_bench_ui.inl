/* The desktop-kit frontend: one window, shared controls, no GTK dependency. */
static const tiku_rect_t TABS = {16, 52, 928, 28};
static const tiku_rect_t TARGET_RECT = {92, 12, 266, 28};
static const tiku_rect_t ALERT_RECT = {200, 196, 560, 220};
static tiku_rect_t field_rect(int i)
{
    if (!i) return (tiku_rect_t){76, 92, 500, 28};
    if (i == 4) return (tiku_rect_t){80, 550, 650, 28};
    return (tiku_rect_t){190, 310 + (i - 1) * 54, 730, 28};
}
static tiku_rect_t list_rect(int tab)
{
    return (tiku_rect_t){16, 132, 928, tab == TAB_SUITES ? 404 : 402};
}
static void text(bench_t *st, tiku_rect_t r, const char *value, int bold)
{
    tiku_text_line_draw(st->surface, r, bold ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN,
                        value, TIKU_C_TEXT);
}

static int selected_count(const bench_t *st)
{
    int n = 0;
    for (int i = 0; i < st->nsuite; i++) n += st->suites[i].selected;
    for (int i = 0; i < st->nleaf; i++) n += st->leaves[i].selected;
    return n;
}
static int editable(const bench_t *st)
{ return !st->busy && !st->loading && !st->confirm.open; }

static void controls(bench_t *st)
{
    int edit = editable(st), tab = st->tabs.current;
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, TIKU_DIALOG_NONE);
    tiku_dialog_add(&st->dialog, TARGET, TARGET_RECT, TIKU_DIALOG_INPUT, edit);
    tiku_dialog_add(&st->dialog, RUN, (tiku_rect_t){664, 10, 154, 32}, TIKU_DIALOG_BUTTON,
        edit && st->metadata_ok && selected_count(st));
    tiku_dialog_add(&st->dialog, STOP, (tiku_rect_t){834, 10, 110, 32},
                    TIKU_DIALOG_BUTTON, (st->busy || st->loading) && !st->reclaiming);
    if (tab != TAB_OPTIONS)
        tiku_dialog_add(&st->dialog, LIST, list_rect(tab), TIKU_DIALOG_INPUT, 1);
    if (tab == TAB_SUITES) {
        tiku_dialog_add(&st->dialog, F_FILTER, field_rect(0), TIKU_DIALOG_INPUT, edit);
        tiku_dialog_add(&st->dialog, SELECT_ALL, (tiku_rect_t){588, 92, 144, 28}, TIKU_DIALOG_BUTTON, edit);
        tiku_dialog_add(&st->dialog, CLEAR, (tiku_rect_t){742, 92, 90, 28}, TIKU_DIALOG_BUTTON, edit);
        tiku_dialog_add(&st->dialog, REFRESH, (tiku_rect_t){842, 92, 102, 28}, TIKU_DIALOG_BUTTON, edit);
    } else if (tab == TAB_OPTIONS) {
        const board_t *b = board_info(st);
        int ids[] = {FLASH, LARGE, USB, PK, EPAPER};
        for (int i = 0; i < 5; i++) {
            int enabled = edit && (i != 1 || (b && b->large)) && (i != 2 || (b && b->usb));
            tiku_dialog_add(&st->dialog, ids[i], (tiku_rect_t){30, 102 + i * 38, 880, 28},
                            TIKU_DIALOG_TOGGLE, enabled);
        }
        for (int i = 1; i <= 3; i++) tiku_dialog_add(&st->dialog, F_FILTER + i,
            field_rect(i), TIKU_DIALOG_INPUT, edit && (i != 1 || !st->usb));
    } else if (tab == TAB_OUTPUT || tab == TAB_CONSOLE) {
        tiku_dialog_add(&st->dialog, COPY_LOG, (tiku_rect_t){16, 92, 150, 28}, TIKU_DIALOG_BUTTON, st->nlog > 0);
        tiku_dialog_add(&st->dialog, CLEAR_LOG, (tiku_rect_t){178, 92, 110, 28}, TIKU_DIALOG_BUTTON, !st->busy);
        tiku_dialog_add(&st->dialog, FOLLOW, (tiku_rect_t){300, 92, 100, 28}, TIKU_DIALOG_BUTTON, 1);
        tiku_dialog_add(&st->dialog, OPEN_LOG, (tiku_rect_t){412, 92, 110, 28}, TIKU_DIALOG_BUTTON, *st->log_path != 0);
        tiku_dialog_add(&st->dialog, F_INPUT, field_rect(4), TIKU_DIALOG_INPUT,
                        st->busy && st->child > 0 && !st->cancelled && st->send_at == st->send_n);
        tiku_dialog_add(&st->dialog, SEND, (tiku_rect_t){746, 550, 100, 28},
                        TIKU_DIALOG_BUTTON, st->busy && st->child > 0 && !st->cancelled && st->send_at == st->send_n);
    }
    tiku_dialog_end(&st->dialog);
}

static const char *button_label(int id)
{
    switch (id) {
    case RUN: return "Run selected"; case STOP: return "Stop";
    case SELECT_ALL: return "Select visible"; case CLEAR: return "Clear";
    case REFRESH: return "Refresh"; case COPY_LOG: return "Copy visible log";
    case CLEAR_LOG: return "Clear view"; case SEND: return "Send";
    case FOLLOW: return "Follow tail"; case OPEN_LOG: return "Open full log";
    case FLASH: return "Build + flash (off = monitor existing firmware only)";
    case LARGE: return "MSP430 large memory model";
    case USB: return "RP2350 USB console (port and baud selected by runner)";
    case PK: return "CRACEN public-key hardware: TIKU_CRACEN_PK_ENABLE=1";
    case EPAPER: return "ePaper kit: HAS_TIKUKITS=1, TIKU_KIT_EPAPER_ENABLE=1";
    default: return "";
    }
}
static int toggle_value(const bench_t *st, int id)
{
    return id == FLASH ? st->flash : id == LARGE ? st->large :
           id == USB ? st->usb : id == PK ? st->pk : st->epaper;
}

static void controls_draw(bench_t *st)
{
    for (int i = 0; i < st->dialog.count; i++) {
        const tiku_dialog_control_t *c = &st->dialog.control[i];
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_focused(&st->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&st->dialog, c->id)) flags |= TIKU_S_PRESSED;
        if (c->id == LIST) continue;
        if (c->id == TARGET) {
            st->target.enabled = c->enabled;
            tiku_choice_draw(&st->target, st->surface, TARGET_RECT,
                st->target.selected >= 0 ? NULL : "No device (host tests available)", flags);
        } else if (c->id >= F_FILTER) {
            int f = c->id - F_FILTER;
            tiku_ui_textfield_scroll(st->surface, c->rect, st->field[f].text,
                st->field[f].caret, st->field[f].sel_a, st->field[f].sel_b, flags, st->input[f].scroll);
        } else if (c->kind == TIKU_DIALOG_TOGGLE)
            tiku_ui_checkbox(st->surface, c->rect, button_label(c->id),
                             flags | (toggle_value(st, c->id) ? TIKU_S_ON : 0));
        else tiku_ui_button(st->surface, c->rect, button_label(c->id), flags);
    }
}

static const char *details_row(void *ctx, int row)
{
    bench_t *st = ctx; static char out[512]; int tab = st->tabs.current;
    if (tab == TAB_RESULTS) {
        if (row < 0 || row >= st->nresult) return NULL;
        const result_t *r = &st->results[row];
        snprintf(out, sizeof out, "%s | %s", st->jobs[r->job].key, r->text);
    } else if (tab == TAB_CONSOLE || tab == TAB_OUTPUT) {
        int n = 0;
        for (int i = 0; i < st->nlog; i++) {
            const logline_t *l = &st->logs[(st->log_head + 256 - st->nlog + i) % 256];
            if (tab == TAB_OUTPUT && !l->output) continue;
            if (n++ == row) return l->text;
        }
        return NULL;
    } else if (tab == TAB_HISTORY) {
        int n = 0;
        for (int i = 0; i < st->nhistory; i++) {
            const past_t *h = &st->history[i];
            if (strcmp(h->profile, st->profile)) continue;
            if (n++ != row) continue;
            snprintf(out, sizeof out, "%s | %s | %s | %ld hours ago | %s",
                h->profile, h->kind, h->verdict, h->age / 3600,
                strcmp(h->kind, "full") ? "Context only, not a full verdict" :
                h->age > 86400 ? "Full result: stale (>24h)" : "Latest full result");
            return out;
        }
        return NULL;
    } else if (tab == TAB_METRICS) {
        int n = 0;
        for (int i = 0; i < st->nmetric; i++) {
            const metric_t *m = &st->metrics[i];
            if (strcmp(m->field[1], st->profile)) continue;
            if (n++ != row) continue;
            snprintf(out, sizeof out, "%.80s | %.80s %.40s | baseline %.80s | %.80s | n=%.40s",
                m->field[2], m->field[4], m->field[3], m->field[5], m->field[6], m->field[7]);
            return out;
        }
        return NULL;
    } else return NULL;
    return out;
}

static void detail_counts(bench_t *st)
{
    for (int tab = TAB_RESULTS; tab <= TAB_METRICS; tab++) {
        int n = 0;
        if (tab == TAB_RESULTS) n = st->nresult;
        else if (tab == TAB_CONSOLE) n = st->nlog;
        else if (tab == TAB_OUTPUT) {
            for (int i = 0; i < st->nlog; i++)
                n += st->logs[(st->log_head + 256 - st->nlog + i) % 256].output;
        } else if (tab == TAB_HISTORY) {
            for (int i = 0; i < st->nhistory; i++) n += !strcmp(st->history[i].profile, st->profile);
        } else for (int i = 0; i < st->nmetric; i++) n += !strcmp(st->metrics[i].field[1], st->profile);
        tiku_list_set_count(&st->list[tab], n);
        if (st->follow && (tab == TAB_OUTPUT || tab == TAB_CONSOLE))
            tiku_list_scroll_to(&st->list[tab], n, list_rect(tab));
    }
}

/** Native checkboxes and disclosure controls, with readable disabled rows. */
static void browser_draw(bench_t *st, tiku_rect_t body)
{
    tiku_list_t *l = &st->list[TAB_SUITES];
    int visible = tiku_list_visible_rows(l, body);
    for (int i = l->top; i < l->count && i < l->top + visible; i++) {
        entry_t e = st->entries[i]; const suite_t *s = &st->suites[e.suite];
        const leaf_t *leaf = e.leaf >= 0 ? &st->leaves[e.leaf] : NULL;
        tiku_rect_t row = tiku_list_row_rect(l, body, i);
        int focus = tiku_list_selected(l, i);
        int ok = suite_supported(st, s) && (!leaf || leaf_supported(st, leaf, 0));
        int checked = leaf ? leaf->selected || (s->selected && leaf_supported(st, leaf, 1)) : s->selected;
        tiku_fill(st->surface, row, focus ? TIKU_C_SELECT : TIKU_C_DOC);
        if (!leaf && s->count) tiku_ui_arrow(st->surface,
            (tiku_rect_t){row.x + 4, row.y + 4, 12, row.h - 8}, s->expanded ? 1 : 3, 1);
        tiku_ui_checkbox(st->surface, (tiku_rect_t){row.x + 24, row.y, 22, row.h}, "",
            (checked ? TIKU_S_ON : 0) | (!ok || st->busy ? TIKU_S_DISABLED : 0));
        char value[512];
        if (leaf) snprintf(value, sizeof value, "%s | %s%s%s", leaf->id, leaf->label,
            strcmp(leaf->kind, "functional") ? " | " : "",
            strcmp(leaf->kind, "functional") ? leaf->kind : "");
        else snprintf(value, sizeof value, "%s | %s | %s", s->group, s->key, s->name);
        tiku_rgb_t ink = focus ? TIKU_C_SELTEXT : ok ? TIKU_C_TEXT : tiku_dim(TIKU_C_TEXT, TIKU_C_DOC);
        tiku_text_line_draw(st->surface, (tiku_rect_t){row.x + 52, row.y, row.w - 56, row.h},
                            leaf ? TIKU_MEASURE_PLAIN : TIKU_MEASURE_BOLD, value, ink);
    }
}

static void results_draw(bench_t *st, tiku_rect_t body)
{
    tiku_list_t *l = &st->list[TAB_RESULTS]; int visible = tiku_list_visible_rows(l, body);
    for (int i = l->top; i < l->count && i < l->top + visible; i++) {
        tiku_rect_t row = tiku_list_row_rect(l, body, i);
        const result_t *r = &st->results[i];
        tiku_rgb_t ink = r->fail || r->harness ? TIKU_C_DANGER : TIKU_C_TEXT;
        tiku_fill(st->surface, row, TIKU_C_DOC);
        tiku_text_line_draw(st->surface, (tiku_rect_t){row.x + 5, row.y, row.w - 10, row.h},
                            TIKU_MEASURE_PLAIN, details_row(st, i), ink);
    }
}

static void paint(bench_t *st)
{
    controls(st); detail_counts(st);
    tiku_fill(st->surface, (tiku_rect_t){0, 0, BW, BH}, TIKU_C_PANEL);
    text(st, (tiku_rect_t){16, 12, 72, 28}, "Device", 1);
    char status[512];
    if (!*st->profile) snprintf(status, sizeof status, "Host tests available");
    else snprintf(status, sizeof status, "%s | %s", st->profile,
                  st->flash ? "Build + flash" : "Monitor only");
    text(st, (tiku_rect_t){374, 12, 276, 28}, status, 0);
    tiku_tabs_draw(&st->tabs, st->surface, TABS);
    int tab = st->tabs.current;
    if (tab == TAB_OPTIONS) {
        const char *names[] = {"", "UART baud", "Make variables", "Extra C flags"};
        for (int i = 1; i <= 3; i++) {
            tiku_rect_t r = field_rect(i); r.x = 30; r.w = 150; text(st, r, names[i], 1);
        }
        text(st, (tiku_rect_t){30, 478, 880, 26},
             "Make vars: KEY=VALUE tokens. Memory model uses the checkbox above.", 0);
        text(st, (tiku_rect_t){30, 508, 880, 26},
             "ePaper panel/pattern: use -DEPAPER_KIT_PANEL=... in Extra C flags.", 0);
        text(st, (tiku_rect_t){30, 546, 880, 26},
             "Tests may reset the board or write storage. Review the suite description first.", 1);
    } else {
        tiku_rect_t body = list_rect(tab);
        tiku_ui_sunken(st->surface, body, TIKU_C_DOC);
        if (tab == TAB_SUITES) browser_draw(st, body);
        else if (tab == TAB_RESULTS) results_draw(st, body);
        else tiku_list_draw(&st->list[tab], st->surface, body, details_row, st);
        if (tab == TAB_SUITES) {
            text(st, (tiku_rect_t){16, 92, 56, 28}, "Filter", 0);
            int row = tiku_list_chosen(&st->list[tab]);
            snprintf(status, sizeof status, "%d selected | Space toggles a check; Enter expands a suite.", selected_count(st));
            if (row >= 0 && row < st->nentry) {
                entry_t e = st->entries[row];
                if (e.leaf < 0) snprintf(status, sizeof status, "%.480s", st->suites[e.suite].description);
                else {
                    char reason[400]; const leaf_t *l = &st->leaves[e.leaf];
                    snprintf(status, sizeof status, "%s | %s | %s", l->group, l->kind,
                        leaf_supported(st, l, 0) ? (leaf_supported(st, l, 1) ?
                            "Included in full sweep" : "Explicit selection only") :
                        leaf_reason(st, l, reason, sizeof reason));
                }
            }
            tiku_text_block_draw(st->surface, (tiku_rect_t){20, 544, 920, 50},
                                  TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT, 2);
        } else if (tab == TAB_RESULTS) {
            int pass = 0, fail = 0, skip = 0, done = 0;
            for (int i = 0; i < st->nresult; i++) {
                const result_t *r = &st->results[i]; done += r->done;
                fail += !!(r->fail || r->harness); skip += r->skip;
                pass += r->done && !r->fail && !r->harness && !r->skip;
            }
            snprintf(status, sizeof status, "Groups: %d passed | %d failed | %d skipped | %d/%d complete",
                     pass, fail, skip, done, st->nresult);
            text(st, (tiku_rect_t){20, 92, 920, 26}, status, 1);
            tiku_ui_gauge(st->surface, (tiku_rect_t){20, 550, 920, 20},
                           st->nresult ? (float)done / st->nresult : 0);
        } else if (tab == TAB_OUTPUT || tab == TAB_CONSOLE) {
            text(st, (tiku_rect_t){534, 92, 410, 28}, "Latest 256 lines; full log saved", 0);
            text(st, (tiku_rect_t){16, 550, 60, 28}, "Input", 0);
        } else {
            text(st, (tiku_rect_t){20, 92, 920, 28}, tab == TAB_HISTORY ?
                "Profile-aware history: full, selected and diagnostic runs remain separate" :
                "Benchmark baseline/current comparison (not a functional-test verdict)", 1);
            if (!st->list[tab].count) text(st, (tiku_rect_t){30, 150, 890, 28}, "No records for this profile", 0);
        }
    }
    controls_draw(st);
    if (*st->prompt) text(st, (tiku_rect_t){20, 580, 920, 24}, st->prompt, 1);
    text(st, (tiku_rect_t){16, 606, 928, 26}, st->note, 0);
    if (st->target.open) tiku_choice_draw_popup(&st->target, st->surface, TARGET_RECT,
                                                (tiku_rect_t){0, 0, BW, BH});
    if (st->confirm.open) tiku_alert_draw(&st->confirm, st->surface, ALERT_RECT);
    st->services->frame(st->services->ctx, st->id, st->surface->px, BW, BH);
}

static void publish(bench_t *st)
{
    tiku_menuset_t m = {0}; m.nmenu = 1;
    snprintf(m.menu[0].title, sizeof m.menu[0].title, "Bench");
    int ids[] = {RUN, STOP, REFRESH, OPEN, TOGGLE, COPY_LOG, FIND_SUITES, FOCUS_LIST, SELECT_ALL};
    const char *labels[] = {"Run selected...", "Stop", "Refresh catalogue",
                            "Expand / collapse suite", "Toggle selection", "Copy visible log",
                            "Find suites", "Focus suite list", "Select visible"};
    m.menu[0].nitem = 9;
    for (int i = 0; i < 9; i++) {
        snprintf(m.menu[0].item[i].label, sizeof m.menu[0].item[i].label, "%s", labels[i]);
        m.menu[0].item[i].command = ids[i];
        m.menu[0].item[i].enabled = ids[i] == STOP ? st->busy || st->loading : editable(st);
    }
    m.nmenu = 2; snprintf(m.menu[1].title, sizeof m.menu[1].title, "View");
    m.menu[1].nitem = st->tabs.count;
    for (int i = 0; i < st->tabs.count; i++) {
        snprintf(m.menu[1].item[i].label, sizeof m.menu[1].item[i].label, "%s", st->tabs.label[i]);
        m.menu[1].item[i].command = 100 + i; m.menu[1].item[i].enabled = 1;
    }
    int report = m.menu[1].nitem++;
    snprintf(m.menu[1].item[report].label, sizeof m.menu[1].item[report].label, "Results report...");
    m.menu[1].item[report].command = REPORT;
    m.menu[1].item[report].enabled = st->services->help_html && st->nresult > 0;
    st->services->menus(st->services->ctx, st->id, &m);
}

static int action(bench_t *st, int command)
{
    if (st->confirm.open) return 0;
    if (command == REPORT) { report_open(st); return 0; }
    if (command >= 100 && command < 107) {
        tiku_tabs_select(&st->tabs, command - 100); st->dirty = 1; return 0;
    }
    if (command == STOP) { stop_run(st); return 0; }
    if (command == FOLLOW) { st->follow = 1; st->dirty = 1; return 0; }
    if (command == OPEN_LOG) {
        if (!st->services->open_path || !st->services->open_path(st->services->ctx, st->log_path)) {
            if (st->services->clip_set) st->services->clip_set(st->services->ctx, st->log_path);
            snprintf(st->note, sizeof st->note, "Full log path copied: %.450s", st->log_path);
        }
        st->dirty = 1; return 0;
    }
    if (command == COPY_LOG) {
        size_t cap = (size_t)st->nlog * LINE_MAXIMUM + 1;
        char *all = malloc(cap); size_t at = 0;
        if (!all) return 0;
        for (int i = 0; i < st->nlog; i++) {
            const logline_t *l = &st->logs[(st->log_head + 256 - st->nlog + i) % 256];
            if (st->tabs.current == TAB_OUTPUT && !l->output) continue;
            at += (size_t)snprintf(all + at, cap - at, "%s\n", l->text);
        }
        all[at] = '\0';
        if (st->services->clip_set) st->services->clip_set(st->services->ctx, all);
        free(all); return 0;
    }
    if (command == SEND && st->busy && st->input_fd >= 0 && !st->cancelled && st->send_at == st->send_n) {
        st->send_n = (size_t)snprintf(st->sendbuf, sizeof st->sendbuf, "%s\n", st->field[4].text);
        st->send_at = 0;
        tiku_field_start(&st->field[4], "");
        st->dirty = 1; return 0;
    }
    if (!editable(st)) return 0;
    int chosen = tiku_list_chosen(&st->list[TAB_SUITES]);
    if (command == FIND_SUITES || command == FOCUS_LIST) {
        tiku_tabs_select(&st->tabs, TAB_SUITES); controls(st);
        if (command == FIND_SUITES) tiku_field_start(&st->field[0], "");
        tiku_dialog_focus(&st->dialog, command == FIND_SUITES ? F_FILTER : LIST);
        st->dialog.keyboard = 1; entries_build(st);
    } else if (command == RUN) {
        target_capture(st);
        command_t c;
        if (!queue_build(st)) { snprintf(st->note, sizeof st->note, "Select supported suites or sub-tests first."); }
        else if (queue_first_invalid(st, &c) >= 0) {
            snprintf(st->note, sizeof st->note, "Invalid run options: check baud, port and KEY=VALUE tokens.");
            tiku_tabs_select(&st->tabs, TAB_OPTIONS);
        } else {
            char question[512];
            if (!queue_needs_device(st)) snprintf(question, sizeof question,
                "Run %d host test suite(s) on this computer? No device is needed or released. "
                "Firmware, serial and board options are ignored. Local test binaries will be built.",
                st->njob);
            else snprintf(question, sizeof question,
                "%d suite(s) on %s (%s), %s. Tests may reset the device or write storage. "
                "The desktop will release its connection until the queue finishes. Continue?",
                st->njob, st->target_name, st->profile,
                st->flash ? "BUILD + FLASH replaces firmware" : "monitor existing firmware");
            tiku_alert_open(&st->confirm, TIKU_ALERT_WARN, RUN, question, "Cancel|Run tests");
            st->confirm.dflt = 0; st->confirm_button = -1;
        }
    } else if (command == REFRESH) metadata_start(st);
    else if (command == CLEAR) clear_selection(st);
    else if (command == OPEN || command == TOGGLE) toggle_entry(st, chosen, command == OPEN);
    else if (command == SELECT_ALL) {
        for (int i = 0; i < st->nentry; i++) if (st->entries[i].leaf < 0) {
            suite_t *s = &st->suites[st->entries[i].suite];
            if (suite_supported(st, s)) {
                s->selected = 1;
                for (int j = s->first; j < s->first + s->count; j++) st->leaves[j].selected = 0;
            }
        }
    } else if (command == CLEAR_LOG) st->nlog = st->log_head = 0;
    else if (command == FLASH) st->flash = !st->flash;
    else if (command == PK) st->pk = !st->pk;
    else if (command == EPAPER) st->epaper = !st->epaper;
    else if (command == LARGE || command == USB) {
        const board_t *b = board_info(st);
        if (b && command == LARGE && b->large) st->large = !st->large;
        if (b && command == USB && b->usb) st->usb = !st->usb;
        clear_selection(st); profile_update(st);
    }
    st->dirty = 1; return 0;
}

static int bench_event(void *state, uint32_t window, const tiku_event_t *ev)
{
    bench_t *st = state; (void)window;
    if (ev->type == TIKU_EVENT_CLOSE) {
        if (st->busy || st->loading) { st->closing = 1; stop_run(st); return 0; }
        return 1;
    }
    if (st->confirm.open) {
        int chosen = -1;
        if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_TAB)
            st->confirm.dflt = (st->confirm.dflt + 1) % st->confirm.nbutton;
        if (ev->type == TIKU_EVENT_KEY_DOWN) chosen = tiku_alert_key(&st->confirm, ev->key);
        if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1)
            st->confirm_button = tiku_alert_button_at(&st->confirm, ALERT_RECT.w, ALERT_RECT.h,
                ev->x - ALERT_RECT.x, ev->y - ALERT_RECT.y);
        if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1) {
            int at = tiku_alert_button_at(&st->confirm, ALERT_RECT.w, ALERT_RECT.h,
                ev->x - ALERT_RECT.x, ev->y - ALERT_RECT.y);
            if (at == st->confirm_button) chosen = at;
            st->confirm_button = -1;
        }
        if (chosen >= 0) { tiku_alert_reset(&st->confirm); if (chosen == 1) run_confirmed(st); }
        st->dirty = 1; return 0;
    }
    controls(st);
    if (ev->type == TIKU_EVENT_DEACTIVATED) st->pressed_row = -1;
    if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1 && st->pressed_row >= 0) {
        int row = tiku_list_at(&st->list[TAB_SUITES], list_rect(TAB_SUITES), ev->x, ev->y);
        if (st->tabs.current == TAB_SUITES && row == st->pressed_row && ev->x < 74)
            toggle_entry(st, row, st->pressed_expand);
        st->pressed_row = -1; st->dirty = 1; return 0;
    }
    tiku_choice_result_t cr = tiku_choice_event(&st->target, ev, TARGET_RECT,
        (tiku_rect_t){0, 0, BW, BH}, st->dialog.focus == TARGET);
    if (cr != TIKU_CHOICE_IGNORED) {
        if (cr == TIKU_CHOICE_CHANGED && editable(st)) {
            snprintf(st->target_name, sizeof st->target_name, "%s", st->target.item[st->target.selected].label);
            clear_selection(st); target_capture(st);
            const board_t *b = board_info(st); char baud[32];
            if (b) { snprintf(baud, sizeof baud, "%d", b->baud); tiku_field_start(&st->field[1], baud); }
        }
        st->dirty = 1; return 0;
    }
    if (st->dialog.focus == LIST && ev->type == TIKU_EVENT_KEY_DOWN && ev->key != TIKU_KEY_TAB) {
        if (st->tabs.current == TAB_SUITES && (ev->key == ' ' || ev->key == TIKU_KEY_RETURN))
            action(st, ev->key == ' ' ? TOGGLE : OPEN);
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, list_rect(st->tabs.current));
        st->dirty = 1; return 0;
    }
    tiku_dialog_result_t dr = tiku_dialog_event(&st->dialog, ev);
    if (dr.activated != TIKU_DIALOG_NONE) return action(st, dr.activated);
    for (int i = 0; i < 5; i++) {
        int focus = st->dialog.focus == F_FILTER + i;
        int visible = i == 0 ? st->tabs.current == TAB_SUITES : i == 4 ?
            st->tabs.current == TAB_CONSOLE || st->tabs.current == TAB_OUTPUT : st->tabs.current == TAB_OPTIONS;
        if (!visible || (!editable(st) && i != 4) || (i == 1 && st->usb) ||
            (i == 4 && (!st->busy || st->cancelled || st->send_at != st->send_n))) continue;
        tiku_field_result_t fr = tiku_field_event(&st->field[i], &st->input[i], ev, field_rect(i), focus);
        if (fr != TIKU_FIELD_IGNORED) {
            if (!i) entries_build(st);
            if (i == 4 && fr == TIKU_FIELD_COMMITTED) action(st, SEND);
            st->dirty = 1; return 0;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int tab = tiku_tabs_hit(&st->tabs, TABS, ev->x, ev->y);
        if (tab >= 0) { tiku_tabs_select(&st->tabs, tab); st->dirty = 1; return 0; }
        if (st->tabs.current != TAB_OPTIONS) {
            int row = tiku_list_at(&st->list[st->tabs.current], list_rect(st->tabs.current), ev->x, ev->y);
            tiku_list_click(&st->list[st->tabs.current], row, ev->modifiers);
            if (st->tabs.current == TAB_SUITES && row >= 0 && ev->x < 74)
                { st->pressed_row = row; st->pressed_expand = ev->x < 38; }
            if (row >= 0) st->dialog.focus = LIST;
        }
    } else if (ev->type == TIKU_EVENT_WHEEL && st->tabs.current != TAB_OPTIONS) {
        if (st->tabs.current == TAB_OUTPUT || st->tabs.current == TAB_CONSOLE) st->follow = 0;
        tiku_list_t *l = &st->list[st->tabs.current];
        tiku_list_scroll_to(l, l->top + ev->dy * 3, list_rect(st->tabs.current));
    } else if (!dr.taken && ev->type == TIKU_EVENT_KEY_DOWN && st->tabs.current != TAB_OPTIONS) {
        if (st->tabs.current == TAB_SUITES && (ev->key == ' ' || ev->key == TIKU_KEY_RETURN))
            action(st, ev->key == ' ' ? TOGGLE : OPEN);
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, list_rect(st->tabs.current));
    }
    st->dirty = 1; return 0;
}
