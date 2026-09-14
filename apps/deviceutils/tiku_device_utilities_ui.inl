/* Classic grouped panels with one geometry for pointer and keyboard controls. */

/** Translate a content-relative rectangle to the shell window's coordinates. */
static tiku_rect_t utility_rect(struct device_utility *p, int x, int y, int w, int h)
{
    tiku_rect_t c = tiku_window_content(p->window);
    return (tiku_rect_t){c.x + x, c.y + y, w, h};
}
#include "tiku_device_utilities_registry.inl"

/** Register controls before both drawing and dispatching an event. */
static void utility_controls(struct device_utility *p)
{
    int held = p->dialog.focus, ring = p->dialog.keyboard;
    tiku_dialog_begin(&p->dialog, TIKU_DIALOG_NONE, U_CLOSE);
    tiku_dialog_add(&p->dialog, U_CLOSE, utility_rect(p, 728, 42, 112, 30), TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, U_DEFAULT, utility_rect(p, 16, 42, 136, 30), TIKU_DIALOG_BUTTON, !p->busy);
    tiku_dialog_add(&p->dialog, U_NEXT, utility_rect(p, 164, 42, 136, 30), TIKU_DIALOG_BUTTON, !p->busy);
    tiku_dialog_add(&p->dialog, U_REFRESH, utility_rect(p, 312, 42, 112, 30), TIKU_DIALOG_BUTTON, !p->busy);
    if (!p->mode) {
        tiku_dialog_add(&p->dialog, U_PAUSE, utility_rect(p, 436, 42, 136, 30), TIKU_DIALOG_BUTTON, 1);
    } else {
        tiku_dialog_add(&p->dialog, U_COPY, utility_rect(p, 436, 42, 136, 30), TIKU_DIALOG_BUTTON, !p->busy && p->value[0]);
        tiku_dialog_add(&p->dialog, U_UP, utility_rect(p, 16, 86, 64, 30), TIKU_DIALOG_BUTTON, !p->busy && strcmp(p->path, "/"));
        tiku_dialog_add(&p->dialog, U_PATH, utility_rect(p, 92, 86, 562, 30), TIKU_DIALOG_INPUT, !p->busy && !p->armed);
        tiku_dialog_add(&p->dialog, U_OPEN, utility_rect(p, 666, 86, 174, 30), TIKU_DIALOG_BUTTON, !p->busy);
        tiku_dialog_add(&p->dialog, U_TREE, registry_tree_rect(p), TIKU_DIALOG_INPUT, !p->busy && p->tree_count);
        tiku_dialog_add(&p->dialog, U_LIST, registry_values_rect(p), TIKU_DIALOG_INPUT, !p->busy && p->count);
        int edit = !p->busy && p->writable && !p->directory &&
            !strcmp(p->address.text, p->path) && utility_editable(p->path);
        tiku_dialog_add(&p->dialog, U_VALUE, utility_rect(p, 280, 452, 560, 30), TIKU_DIALOG_INPUT, edit && !p->armed);
        tiku_dialog_add(&p->dialog, U_REVIEW, utility_rect(p, 280, 494, 174, 30), TIKU_DIALOG_BUTTON,
                        edit && !p->armed && strcmp(p->edit.text, p->value));
        tiku_dialog_add(&p->dialog, U_SAVE, utility_rect(p, 466, 494, 180, 30), TIKU_DIALOG_BUTTON, edit && p->armed);
        tiku_dialog_add(&p->dialog, U_CANCEL, utility_rect(p, 658, 494, 182, 30), TIKU_DIALOG_BUTTON, p->armed && !p->busy);
    }
    tiku_dialog_end(&p->dialog);
    /* A read greys every control for as long as it takes, and Done is
     * the one left enabled: the ring stays where the hand put it, or a
     * five-second sample moves it under the next Space. */
    if (p->busy && held != TIKU_DIALOG_NONE) {
        p->dialog.focus = held; p->dialog.keyboard = ring;
    }
}

/** One clipped line for labels and scalars. */
static void utility_line(struct device_utility *p, tiku_surface_t *s,
                         int x, int y, int width, const char *text)
{
    tiku_text_line_draw(s, utility_rect(p, x, y, width, 26),
                       TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT);
}

/** Keep units and partial memory accounting explicit beside live graphs. */
static void utility_monitor_draw(struct device_utility *p, tiku_surface_t *s)
{
    const char *labels[] = {"Uptime", "CPU clock", "Process RAM", "Stack headroom", "Processes", "Store used"};
    double values[] = {p->metrics.uptime, p->metrics.hz / 1000000,
        p->metrics.ram / 1024, p->metrics.stack / 1024,
        p->metrics.processes, p->metrics.store_used / 1024};
    const char *units[] = {"s", "MHz", "KiB", "KiB", "", "KiB"};
    for (int i = 0; i < 6; i++) {
        char value[96];
        if (p->metrics.valid & (1u << i))
            snprintf(value, sizeof value, "%.*f %s", i == 0 || i == 4 ? 0 : 2,
                     values[i], units[i]);
        else snprintf(value, sizeof value, "Unavailable");
        tiku_ui_group(s, utility_rect(p, 16 + i * 138, 94, 128, 74), labels[i]);
        utility_line(p, s, 24 + i * 138, 126, 112, value);
    }
    int64_t to = p->sampled ? p->sampled : utility_now();
    for (int i = 0; i < 2; i++) {
        tiku_ui_group(s, utility_rect(p, 16 + i * 418, 182, 404, 122),
                       i ? "Store allocation history (KiB)" : "Process RAM history (KiB)");
        tiku_sensor_spark_draw(&p->history[i], s,
            utility_rect(p, 28 + i * 418, 208, 380, 82),
            to - 600000000, to, 15000000, TIKU_RGB(40, 105, 160));
    }
    char capacity[192], ram[48];
    if (p->metrics.valid & 64)
        snprintf(ram, sizeof ram, "SRAM: %.0f KiB", p->metrics.sram / 1024);
    else snprintf(ram, sizeof ram, "SRAM: unavailable");
    if (p->metrics.valid & 32)
        snprintf(capacity, sizeof capacity, "%s | Store: %.1f / %.1f KiB used; %.1f KiB free",
            ram, p->metrics.store_used / 1024, p->metrics.store_size / 1024,
            (p->metrics.store_size - p->metrics.store_used) / 1024);
    else snprintf(capacity, sizeof capacity, "%s | Store occupancy: unavailable.", ram);
    utility_line(p, s, 16, 312, 824, capacity);
    utility_line(p, s, 16, 340, 824, "PID / process | state | SRAM bytes | queued events (up to 8 processes)");
    for (int i = 0; i < p->metrics.nprocess; i++)
        utility_line(p, s, 24, 368 + i * 23, 808, p->metrics.process[i]);
    if (!p->metrics.nprocess) utility_line(p, s, 24, 368, 808, "Process details unavailable.");
}

/** Registry-style hierarchy, value table and a separate change editor. */
static void utility_inspector_draw(struct device_utility *p, tiku_surface_t *s)
{
    utility_line(p, s, 16, 130, 248, "Device namespace");
    registry_tree_draw(p, s); registry_values_draw(p, s);
    const char *leaf = strrchr(p->path, '/');
    char detail[192];
    snprintf(detail, sizeof detail, "%.100s | %.50s%s", p->directory ?
        "Select a value" : leaf ? leaf + 1 : p->path, p->writable ?
        utility_editable(p->path) ? "Editable setting" : "Writable; edits restricted" :
        "Read-only / unavailable",
        /* Said wherever the rows came from: a cached listing that was cut
         * at 128 looks exactly like a directory that holds 128. */
        p->truncated ? " | first 128 entries shown" : "");
    utility_line(p, s, 280, 308, 560, detail);
    tiku_rect_t paper = utility_rect(p, 280, 340, 560, 54);
    tiku_fill(s, paper, TIKU_C_DOC);
    tiku_frame(s, paper, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_text_block_draw(s, utility_rect(p, 288, 344, 544, 46),
        TIKU_MEASURE_PLAIN, p->directory ? "Click a value to read it. No values are read automatically." :
        p->value[0] ? p->value : "No readable value. Copy value copies the full accepted text.",
        TIKU_C_TEXT, 2);
    tiku_text_block_draw(s, utility_rect(p, 280, 398, 560, 32),
        TIKU_MEASURE_PLAIN, utility_help(p->path), TIKU_C_TEXT, 2);
    tiku_text_line_draw(s, utility_rect(p, 280, 430, 560, 22),
        TIKU_MEASURE_PLAIN, "Proposed value (not saved)", TIKU_C_TEXT);
    utility_line(p, s, 16, 528, 824, p->armed ?
        "Review target, path, current and proposed values. Confirm save writes once; Cancel makes no change." :
        "Tree: +/- expands or collapses. Values: click to read; arrows select, Enter reads. Edits require review.");
}

/** Shared dialog painting preserves focus, disabled and release-to-activate states. */
static void utility_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t c, void *ctx)
{
    struct device_utility *p = ctx; (void)w;
    utility_controls(p); tiku_fill(s, c, TIKU_C_PANEL);
    char title[192];
    snprintf(title, sizeof title, "Target: %s%s%s", p->target[0] ? p->target : "None selected",
        p->target[0] && !strcmp(p->target, p->app->current_device) ? " (default)" : "",
        p->paused ? "  |  Paused snapshot" : p->busy ? "  |  Reading / working" : "");
    if (!p->mode && p->sampled && !p->paused) {
        size_t n = strlen(title);
        snprintf(title + n, sizeof title - n, "  |  Sample age: %lld s",
                 (long long)((utility_now() - p->sampled) / 1000000));
    }
    utility_line(p, s, 16, 6, 824, title);
    if (p->mode) utility_inspector_draw(p, s); else utility_monitor_draw(p, s);
    const char *labels[] = {"", "Refresh", "Use default", "Next device", p->paused ? "Resume" : "Pause", "Up",
        "Go to path", "Review edit", "Confirm save", "Cancel edit", "Copy value", "Done"};
    for (int i = 0; i < p->dialog.count; i++) {
        const tiku_dialog_control_t *d = &p->dialog.control[i];
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_focused(&p->dialog, d->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&p->dialog, d->id)) flags |= TIKU_S_PRESSED;
        if (d->id == U_PATH || d->id == U_VALUE) {
            tiku_field_t *f = d->id == U_PATH ? &p->address : &p->edit;
            tiku_field_input_t *in = d->id == U_PATH ? &p->address_input : &p->edit_input;
            tiku_ui_textfield_scroll(s, d->rect, f->text, f->caret,
                                    f->sel_a, f->sel_b, flags, in->scroll);
        } else if (d->id <= U_CLOSE) tiku_ui_button(s, d->rect, labels[d->id], flags);
    }
    utility_line(p, s, 16, 555, 824, p->status);
    utility_line(p, s, 16, 582, 824, p->mode ?
        "No port scanning, shell commands or automatic writes. Escape closes this window." :
        "5s refresh after each sample. Process RAM excludes kernel/stack; Store includes metadata and slot slack.");
}

/** Changing target is explicit and drops every old measurement and pending edit. */
static void utility_target(struct device_utility *p, int use_default)
{
    char names[1024], chosen[64] = "", *save; int next = 0;
    if (use_default) snprintf(chosen, sizeof chosen, "%s", p->app->current_device);
    else {
        utility_board_names(p->app, names, sizeof names);
        for (char *s = strtok_r(names, "\n", &save); s; s = strtok_r(NULL, "\n", &save)) {
            if (!chosen[0] || next) snprintf(chosen, sizeof chosen, "%.63s", s);
            if (next) break;
            next = !strcmp(s, p->target);
        }
    }
    snprintf(p->target, sizeof p->target, "%s", chosen);
    utility_tree_reset(p);
    if (p->mode) {
        strcpy(p->path, "/"); tiku_field_start(&p->address, p->path);
    }
    memset(&p->metrics, 0, sizeof p->metrics);
    for (int i = 0; i < 2; i++) tiku_history_init(&p->history[i], p->samples[i], U_HISTORY);
    p->armed = 0; p->writable = 0; p->count = 0; p->value[0] = 0;
    tiku_field_start(&p->edit, ""); utility_request(p, 0);
}

/** Address navigation is separate from reading a selected value row. */
static void utility_navigate(struct device_utility *p, int up)
{
    char path[256]; snprintf(path, sizeof path, "%s", p->address.text);
    if (up) {
        snprintf(path, sizeof path, "%s", p->path);
        char *end = strrchr(path, '/'); if (end) *end = 0;
        if (!path[0]) strcpy(path, "/");
    }
    registry_go(p, path);
}

/** Actions are gated again, so keyboard and programmatic picks obey the same policy. */
static void utility_action(struct device_utility *p, int command)
{
    if (command == U_CLOSE) { device_utility_close(p->app, p->window); return; }
    if (command == U_PAUSE) { p->paused = !p->paused; return; }
    if (p->busy) return;
    if (command == U_COPY) {
        int copied = tiku_clipboard_import(p->value, strlen(p->value));
        snprintf(p->status, sizeof p->status, "%s", copied ?
            "Copied complete value; the preview may show only its first lines." :
            "Clipboard rejected the device's text encoding.");
    }
    if (command == U_DEFAULT || command == U_NEXT) utility_target(p, command == U_DEFAULT);
    else if (command == U_REFRESH) {
        p->armed = 0;
        if (!p->mode) p->paused = 0;
        else {
            utility_tree_reset(p);
            p->count = p->writable = 0; p->value[0] = 0;
            tiku_list_set_count(&p->list, 0); tiku_field_start(&p->edit, "");
        }
        utility_request(p, 0);
    }
    else if (command == U_OPEN || command == U_UP) utility_navigate(p, command == U_UP);
    else if (command == U_CANCEL) p->armed = 0;
    else if (p->writable && !strcmp(p->address.text, p->path) && utility_editable(p->path)) {
        if (command == U_REVIEW && strcmp(p->edit.text, p->value)) p->armed = 1;
        else if (command == U_SAVE && p->armed) { p->armed = 0; utility_request(p, 1); }
    }
}

/** Native fields, list navigation and captured pointer release share a single dispatcher. */
static int utility_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct device_utility *p = ctx; (void)w;
    utility_controls(p);
    tiku_dialog_result_t r = tiku_dialog_event(&p->dialog, ev);
    if (r.activated == U_CLOSE) { utility_action(p, U_CLOSE); return 1; }
    if (r.activated != TIKU_DIALOG_NONE) utility_action(p, r.activated);
    int steered = r.taken && ev->type == TIKU_EVENT_KEY_DOWN &&
                  ev->key != TIKU_KEY_RETURN;
    if (!p->busy && p->mode) {
        if (!r.taken || ev->type == TIKU_EVENT_POINTER_DOWN ||
            ev->type == TIKU_EVENT_WHEEL ||
            (ev->type == TIKU_EVENT_KEY_DOWN &&
             ev->key == TIKU_KEY_RETURN &&
             r.activated == TIKU_DIALOG_NONE)) (void)registry_event(p, ev);
        /* A key the panel took is a focus move, not text: Tab carries
         * its own character, which the field it lands in refuses. */
        for (int i = 0; !steered && i < 2; i++) {
            if (!i && p->armed) continue;
            if (i && (!p->writable || p->armed || !utility_editable(p->path))) continue;
            tiku_field_t *f = i ? &p->edit : &p->address;
            tiku_field_result_t fr = tiku_field_event(f,
                i ? &p->edit_input : &p->address_input, ev,
                utility_rect(p, i ? 280 : 92, i ? 452 : 86, i ? 560 : 562, 30),
                p->dialog.focus == (i ? U_VALUE : U_PATH));
            if (fr == TIKU_FIELD_COMMITTED && !i) utility_navigate(p, 0);
            if (fr == TIKU_FIELD_REJECTED)
                snprintf(p->status, sizeof p->status, "Input rejected: invalid text or more than 255 bytes.");
        }
    }
    p->app->dirty = 1; return 1;
}
