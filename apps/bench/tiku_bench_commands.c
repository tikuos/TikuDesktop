/* tiku_bench_commands.c -- commands.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

void tiku_bench_publish(bench_t *st)
{
    tiku_menuset_t m = {0}; m.nmenu = 1;
    snprintf(m.menu[0].title, sizeof m.menu[0].title, "Bench");
    int ids[] = {RUN, STOP, REFRESH, OPEN, TOGGLE, COPY_LOG, FIND_SUITES,
                 FOCUS_LIST, SELECT_ALL, FOLLOW, OPEN_LOG};
    const char *labels[] = {"Run selected...", "Stop", "Refresh catalogue",
                            "Expand / collapse suite", "Toggle selection", "Copy visible log",
                            "Find suites", "Focus suite list", "Select visible",
                            "Follow tail", "Open full log"};
    m.menu[0].nitem = 11;
    for (int i = 0; i < 11; i++) {
        snprintf(m.menu[0].item[i].label, sizeof m.menu[0].item[i].label, "%s", labels[i]);
        m.menu[0].item[i].command = ids[i];
        m.menu[0].item[i].enabled = ids[i] == STOP ? st->busy || st->loading :
            ids[i] == FOLLOW ? 1 : ids[i] == OPEN_LOG ? *st->log_path != 0 :
            tiku_bench_editable(st);
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

void tiku_bench_ask_run(bench_t *st)
{
    char question[512];
    if (!tiku_bench_queue_needs_device(st)) snprintf(question, sizeof question,
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

int tiku_bench_action(bench_t *st, int command)
{
    if (st->confirm.open) return 0;
    if (command == REPORT) { tiku_bench_report_open(st); return 0; }
    if (command >= 100 && command < 107) {
        tiku_tabs_select(&st->tabs, command - 100); st->dirty = 1; return 0;
    }
    if (command == STOP) { tiku_bench_stop_run(st); return 0; }
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
    if (!tiku_bench_editable(st)) return 0;
    int chosen = tiku_list_chosen(&st->list[TAB_SUITES]);
    if (command == FIND_SUITES || command == FOCUS_LIST) {
        tiku_tabs_select(&st->tabs, TAB_SUITES); tiku_bench_controls(st);
        if (command == FIND_SUITES) tiku_field_start(&st->field[0], "");
        tiku_dialog_focus(&st->dialog, command == FIND_SUITES ? F_FILTER : LIST);
        st->dialog.keyboard = 1; tiku_bench_entries_build(st);
    } else if (command == RUN) {
        tiku_bench_target_capture(st);
        command_t c;
        if (!tiku_bench_queue_build(st)) { snprintf(st->note, sizeof st->note, "Select supported suites or sub-tests first."); }
        else if (tiku_bench_queue_first_invalid(st, &c) >= 0) {
            snprintf(st->note, sizeof st->note, "Invalid run options: check baud, port and KEY=VALUE tokens.");
            tiku_dialog_focus(&st->dialog, F_BAUD); st->dialog.keyboard = 1;
        } else {
            /* Consent is per run: a destructive selection is asked about
             * on its own, before the run's question, Cancel first. */
            int destructive = tiku_bench_queue_destructive(st);
            st->allow_destructive = 0;
            if (destructive) {
                char question[512];
                snprintf(question, sizeof question,
                    "%d selected test(s) may erase ALL files in /data on %s. "
                    "Use a scratch board. Allow them for this run?",
                    destructive, st->target_name);
                tiku_alert_open(&st->confirm, TIKU_ALERT_WARN, RUN_DESTRUCTIVE,
                                question, "Cancel|Allow and continue");
                st->confirm.dflt = 0; st->confirm_button = -1;
            } else tiku_bench_ask_run(st);
        }
    } else if (command == REFRESH) tiku_bench_metadata_start(st);
    else if (command == CLEAR) tiku_bench_clear_selection(st);
    else if (command == OPEN || command == TOGGLE) tiku_bench_toggle_entry(st, chosen, command == OPEN);
    else if (command == SELECT_ALL) {
        for (int i = 0; i < st->nentry; i++)
        if (st->entries[i].leaf < 0 && st->entries[i].suite >= 0) {
            suite_t *s = &st->suites[st->entries[i].suite];
            if (tiku_bench_suite_supported(st, s)) {
                s->selected = 1;
                for (int j = s->first; j < s->first + s->count; j++) st->leaves[j].selected = 0;
            }
        }
    } else if (command == CLEAR_LOG) st->nlog = st->log_head = 0;
    else if (command == FLASH || command == MONITOR)
        st->flash = command == FLASH;
    else if (command == PK) st->pk = !st->pk;
    else if (command == EPAPER) st->epaper = !st->epaper;
    else if (command == LARGE || command == USB) {
        const board_t *b = tiku_bench_board_info(st);
        if (b && command == LARGE && b->large) st->large = !st->large;
        if (b && command == USB && b->usb) st->usb = !st->usb;
        tiku_bench_clear_selection(st); tiku_bench_profile_update(st);
    }
    st->dirty = 1; return 0;
}

int tiku_bench_event(void *state, uint32_t window, const tiku_event_t *ev)
{
    bench_t *st = state; (void)window;
    if (ev->type == TIKU_EVENT_CLOSE) {
        if (st->busy || st->loading) { st->closing = 1; tiku_bench_stop_run(st); return 0; }
        return 1;
    }
    if (ev->type == TIKU_EVENT_RESIZE) {
        /* The corner moved: the views take the room, never less than
         * the window opened with, and the next frame is at that size. */
        int w = ev->width < BW ? BW : ev->width;
        int h = ev->height < BH ? BH : ev->height;
        if (w != st->w || h != st->h) {
            tiku_surface_t *grown = tiku_surface_new(w, h, TIKU_C_PANEL);
            if (grown) {
                tiku_surface_free(st->surface); st->surface = grown;
                st->w = w; st->h = h;
            }
        }
        st->dirty = 1; return 0;
    }
    if (st->confirm.open) {
        int chosen = -1;
        if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_TAB)
            st->confirm.dflt = (st->confirm.dflt + 1) % st->confirm.nbutton;
        if (ev->type == TIKU_EVENT_KEY_DOWN) chosen = tiku_alert_key(&st->confirm, ev->key);
        if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1)
            st->confirm_button = tiku_alert_button_at(&st->confirm, TIKU_BENCH_ALERT_RECT.w, TIKU_BENCH_ALERT_RECT.h,
                ev->x - TIKU_BENCH_ALERT_RECT.x, ev->y - TIKU_BENCH_ALERT_RECT.y);
        if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1) {
            int at = tiku_alert_button_at(&st->confirm, TIKU_BENCH_ALERT_RECT.w, TIKU_BENCH_ALERT_RECT.h,
                ev->x - TIKU_BENCH_ALERT_RECT.x, ev->y - TIKU_BENCH_ALERT_RECT.y);
            if (at == st->confirm_button) chosen = at;
            st->confirm_button = -1;
        }
        if (chosen >= 0) {
            int tag = st->confirm.tag;
            tiku_alert_reset(&st->confirm);
            if (chosen == 1 && tag == RUN_DESTRUCTIVE) {
                st->allow_destructive = 1;
                tiku_bench_ask_run(st);
            } else if (chosen == 1) tiku_bench_run_confirmed(st);
        }
        st->dirty = 1; return 0;
    }
    tiku_bench_controls(st);
    if (ev->type == TIKU_EVENT_DEACTIVATED) st->pressed_row = -1;
    if (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1 && st->pressed_row >= 0) {
        tiku_rect_t body = tiku_bench_list_rect(st, TAB_SUITES);
        int row = tiku_list_at(&st->list[TAB_SUITES], body, ev->x, ev->y);
        if (st->tabs.current == TAB_SUITES && row == st->pressed_row &&
            ev->x < body.x + 58)
            tiku_bench_toggle_entry(st, row, st->pressed_expand);
        st->pressed_row = -1; st->dirty = 1; return 0;
    }
    tiku_choice_result_t cr = tiku_choice_event(&st->target, ev, tiku_bench_target_rect(),
        tiku_bench_screen_rect(st), st->dialog.focus == TARGET);
    if (cr == TIKU_CHOICE_IGNORED && st->epaper && tiku_bench_editable(st)) {
        /* The presets take the event the same way the device does; a
         * change is a change of argv, nothing else. */
        tiku_choice_result_t pr = tiku_choice_event(&st->epanel, ev,
            tiku_bench_preset_rect(0), tiku_bench_screen_rect(st),
            st->dialog.focus == PANEL);
        if (pr == TIKU_CHOICE_IGNORED)
            pr = tiku_choice_event(&st->epattern, ev, tiku_bench_preset_rect(1),
                tiku_bench_screen_rect(st), st->dialog.focus == PATTERN);
        if (pr != TIKU_CHOICE_IGNORED) { st->dirty = 1; return 0; }
    }
    if (cr != TIKU_CHOICE_IGNORED) {
        if (cr == TIKU_CHOICE_CHANGED && tiku_bench_editable(st)) {
            snprintf(st->target_name, sizeof st->target_name, "%s", st->target.item[st->target.selected].label);
            tiku_bench_clear_selection(st); tiku_bench_target_capture(st);
            const board_t *b = tiku_bench_board_info(st); char baud[32];
            if (b) { snprintf(baud, sizeof baud, "%d", b->baud); tiku_field_start(&st->field[1], baud); }
        }
        st->dirty = 1; return 0;
    }
    if (st->dialog.focus == LIST && ev->type == TIKU_EVENT_KEY_DOWN && ev->key != TIKU_KEY_TAB) {
        if (st->tabs.current == TAB_SUITES && (ev->key == ' ' || ev->key == TIKU_KEY_RETURN))
            tiku_bench_action(st, ev->key == ' ' ? TOGGLE : OPEN);
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, tiku_bench_list_rect(st, st->tabs.current));
        st->dirty = 1; return 0;
    }
    tiku_dialog_result_t dr = tiku_dialog_event(&st->dialog, ev);
    if (dr.activated != TIKU_DIALOG_NONE) return tiku_bench_action(st, dr.activated);
    for (int i = 0; i < 5; i++) {
        int focus = st->dialog.focus == F_FILTER + i;
        int visible = i == 0 ? st->tabs.current == TAB_SUITES : i == 4 ?
            st->tabs.current == TAB_CONSOLE ||
            st->tabs.current == TAB_OUTPUT : 1;
        if (!visible || (!tiku_bench_editable(st) && i != 4) || (i == 1 && st->usb) ||
            (i == 4 && (!st->busy || st->cancelled || st->send_at != st->send_n))) continue;
        tiku_field_result_t fr = tiku_field_event(&st->field[i], &st->input[i], ev, tiku_bench_field_rect(st, i), focus);
        if (fr != TIKU_FIELD_IGNORED) {
            if (!i) tiku_bench_entries_build(st);
            if (i == 4 && fr == TIKU_FIELD_COMMITTED) tiku_bench_action(st, SEND);
            st->dirty = 1; return 0;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int tab = tiku_tabs_hit(&st->tabs, tiku_bench_tabs_rect(st), ev->x, ev->y);
        if (tab >= 0) { tiku_tabs_select(&st->tabs, tab); st->dirty = 1; return 0; }
        tiku_rect_t body = tiku_bench_list_rect(st, st->tabs.current);
        int row = tiku_list_at(&st->list[st->tabs.current], body, ev->x, ev->y);
        tiku_list_click(&st->list[st->tabs.current], row, ev->modifiers);
        if (st->tabs.current == TAB_SUITES && row >= 0 && ev->x < body.x + 58)
            { st->pressed_row = row; st->pressed_expand = ev->x < body.x + 22; }
        if (row >= 0) st->dialog.focus = LIST;
    } else if (ev->type == TIKU_EVENT_WHEEL) {
        if (st->tabs.current == TAB_OUTPUT || st->tabs.current == TAB_CONSOLE) st->follow = 0;
        tiku_list_t *l = &st->list[st->tabs.current];
        tiku_list_scroll_to(l, l->top + ev->dy * 3, tiku_bench_list_rect(st, st->tabs.current));
    } else if (!dr.taken && ev->type == TIKU_EVENT_KEY_DOWN) {
        if (st->tabs.current == TAB_SUITES && (ev->key == ' ' || ev->key == TIKU_KEY_RETURN))
            tiku_bench_action(st, ev->key == ' ' ? TOGGLE : OPEN);
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, tiku_bench_list_rect(st, st->tabs.current));
    }
    st->dirty = 1; return 0;
}
