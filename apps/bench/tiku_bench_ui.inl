/* The desktop-kit frontend: the GTK window's shape at the desktop's size,
 * a column of cards about the board and the run beside the tabbed views. */
#define RX 260                      /* the views' column, to the right edge */
#define RW (st->w - RX - 10)
static const tiku_rect_t ALERT_RECT = {200, 196, 560, 220};
static tiku_rect_t tabs_rect(const bench_t *st)
{ return (tiku_rect_t){RX, 52, RW, 28}; }
static tiku_rect_t screen_rect(const bench_t *st)
{ return (tiku_rect_t){0, 0, st->w, st->h}; }
/* The cards, top to bottom, and the two buttons under them. */
static const tiku_rect_t CARD_DEVICE = {10, 8, 238, 72};
static const tiku_rect_t CARD_HISTORY = {10, 86, 238, 108};
static const tiku_rect_t CARD_LINK = {10, 200, 238, 70};
static const tiku_rect_t CARD_MODE = {10, 276, 238, 64};
static const tiku_rect_t CARD_OPTIONS = {10, 346, 238, 64};
static const tiku_rect_t CARD_EPAPER = {10, 416, 238, 76};
static const tiku_rect_t CARD_MAKE = {10, 498, 238, 76};
static const tiku_rect_t RUN_RECT = {10, 580, 150, 28};
static const tiku_rect_t STOP_RECT = {168, 580, 80, 28};
/** Inside a card: what tiku_ui_group leaves once it has drawn the frame. */
static tiku_rect_t card_in(tiku_rect_t r)
{
    int top = tiku_font_plain()->height + 4;
    return (tiku_rect_t){r.x + 8, r.y + top, r.w - 16, r.h - top - 8};
}
static tiku_rect_t target_rect(void)
{
    tiku_rect_t in = card_in(CARD_DEVICE);
    return (tiku_rect_t){in.x, in.y, in.w, 24};
}
static tiku_rect_t preset_rect(int pattern)
{
    tiku_rect_t in = card_in(CARD_EPAPER);
    return (tiku_rect_t){in.x + 58, in.y + (pattern ? 26 : 0), in.w - 58, 22};
}
/* The views' bottom edge moves with the window; the cards do not. */
static tiku_rect_t field_rect(const bench_t *st, int i)
{
    tiku_rect_t in;
    if (!i) return (tiku_rect_t){RX + 48, 90, 396, 24};
    if (i == 4) return (tiku_rect_t){RX, st->h - 84, 380, 24};
    if (i == 1) {
        in = card_in(CARD_LINK);
        return (tiku_rect_t){in.x + 48, in.y + 20, in.w - 48, 22};
    }
    in = card_in(CARD_MAKE);
    return (tiku_rect_t){in.x + 58, in.y + (i == 3 ? 26 : 0), in.w - 58, 22};
}
static tiku_rect_t list_rect(const bench_t *st, int tab)
{
    if (tab == TAB_SUITES) return (tiku_rect_t){RX, 122, RW, st->h - 98};
    if (tab == TAB_OUTPUT || tab == TAB_CONSOLE)
        return (tiku_rect_t){RX, 90, RW, st->h - 180};
    if (tab == TAB_HISTORY) return (tiku_rect_t){RX, 118, RW, st->h - 178};
    return (tiku_rect_t){RX, 90, RW, st->h - 150};
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
    const board_t *b = board_info(st);
    tiku_rect_t in;
    tiku_dialog_begin(&st->dialog, TIKU_DIALOG_NONE, TIKU_DIALOG_NONE);
    tiku_dialog_add(&st->dialog, TARGET, target_rect(), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, LIST, list_rect(st, tab), TIKU_DIALOG_INPUT, 1);
    if (tab == TAB_SUITES) {
        tiku_dialog_add(&st->dialog, F_FILTER, field_rect(st, 0),
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
        tiku_dialog_add(&st->dialog, F_INPUT, field_rect(st, 4),
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
    }
    /* The cards: the connection, the build mode, the options, the ePaper
     * presets and the make fields, always in reach. */
    tiku_dialog_add(&st->dialog, F_BAUD, field_rect(st, 1), TIKU_DIALOG_INPUT,
                    edit && !st->usb);
    in = card_in(CARD_MODE);
    tiku_dialog_add(&st->dialog, FLASH, (tiku_rect_t){in.x, in.y, in.w, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    tiku_dialog_add(&st->dialog, MONITOR,
                    (tiku_rect_t){in.x, in.y + 18, in.w, 18},
                    TIKU_DIALOG_TOGGLE, edit);
    in = card_in(CARD_OPTIONS);
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
    tiku_dialog_add(&st->dialog, PANEL, preset_rect(0), TIKU_DIALOG_INPUT,
                    edit && st->epaper);
    tiku_dialog_add(&st->dialog, PATTERN, preset_rect(1), TIKU_DIALOG_INPUT,
                    edit && st->epaper);
    tiku_dialog_add(&st->dialog, F_MAKE, field_rect(st, 2), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, F_FLAGS, field_rect(st, 3), TIKU_DIALOG_INPUT,
                    edit);
    tiku_dialog_add(&st->dialog, RUN, RUN_RECT, TIKU_DIALOG_BUTTON,
                    edit && st->metadata_ok && selected_count(st));
    tiku_dialog_add(&st->dialog, STOP, STOP_RECT, TIKU_DIALOG_BUTTON,
                    (st->busy || st->loading) && !st->reclaiming);
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
    default: return "";
    }
}
static int toggle_value(const bench_t *st, int id)
{
    return id == FLASH ? st->flash : id == MONITOR ? !st->flash :
           id == LARGE ? st->large : id == USB ? st->usb :
           id == PK ? st->pk : st->epaper;
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
            tiku_choice_draw(&st->target, st->surface, c->rect,
                st->target.selected >= 0 ? NULL : "No device (host tests)",
                flags);
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
    } else if (tab == TAB_NOTRUN) {
        int n = 0;
        for (int i = 0; i < st->nleaf; i++) {
            const leaf_t *l = &st->leaves[i];
            if (leaf_supported(st, l, 0)) continue;
            if (n++ != row) continue;
            char reason[400];
            snprintf(out, sizeof out, "%s | %s | %s", l->id, l->label,
                     leaf_reason(st, l, reason, sizeof reason));
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
    for (int tab = TAB_RESULTS; tab < TAB_COUNT; tab++) {
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
            tiku_list_scroll_to(&st->list[tab], n, list_rect(st, tab));
    }
}

/** Native checkboxes and disclosure controls, with readable disabled rows. */
static void browser_draw(bench_t *st, tiku_rect_t body)
{
    tiku_list_t *l = &st->list[TAB_SUITES];
    int visible = tiku_list_visible_rows(l, body);
    for (int i = l->top; i < l->count && i < l->top + visible; i++) {
        entry_t e = st->entries[i];
        tiku_rect_t row = tiku_list_row_rect(l, body, i);
        if (e.suite < 0) {
            /* A group's heading: the card's title bar, with how many
             * suites it holds and a fold triangle. */
            const group_t *g = &st->groups[e.group];
            char title[96];
            snprintf(title, sizeof title, "%s (%d)", g->name, g->count);
            tiku_fill(st->surface, row, TIKU_C_PANEL);
            tiku_hline(st->surface, row.x, row.y + row.h - 1, row.w,
                       tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
            tiku_ui_arrow(st->surface,
                          (tiku_rect_t){row.x + 4, row.y + 4, 12, row.h - 8},
                          g->folded ? 3 : 1, 1);
            tiku_text_line_draw(st->surface,
                                (tiku_rect_t){row.x + 24, row.y, row.w - 28, row.h},
                                TIKU_MEASURE_BOLD, title, TIKU_C_TEXT);
            continue;
        }
        const suite_t *s = &st->suites[e.suite];
        const leaf_t *leaf = e.leaf >= 0 ? &st->leaves[e.leaf] : NULL;
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

/**
 * The verdict for the active profile, the GTK frontend's rule: the
 * latest full kernel run decides -- none is UNPROVEN, one older than a
 * day is STALE, else Fresh -- and the benchmark records fold into one
 * performance word, a regression outranking a suspect outranking an
 * invalid, pass only when every metric passes.
 */
static const char *history_verdict(const bench_t *st, char *out, size_t cap,
                                   char *perf, size_t perf_cap)
{
    const past_t *full = NULL;
    for (int i = 0; i < st->nhistory; i++) {
        const past_t *h = &st->history[i];
        if (!strcmp(h->suite, "kernel") && !strcmp(h->kind, "full") &&
            !strcmp(h->profile, st->profile)) full = h;
    }
    snprintf(out, cap, "%s", !full ? "UNPROVEN - no full run" :
             full->age > 86400 ? "STALE - full run over a day old" :
             "Fresh full verdict");
    const char *word = "unproven"; int matched = 0, all_pass = 1;
    for (int i = 0; i < st->nmetric; i++) {
        const metric_t *m = &st->metrics[i];
        if (strcmp(m->field[1], st->profile)) continue;
        matched = 1;
        if (strcmp(m->field[6], "pass")) all_pass = 0;
        if (!strcmp(m->field[6], "regression")) word = "REGRESSION";
        else if (strcmp(word, "REGRESSION") && !strcmp(m->field[6], "suspect"))
            word = "suspect - repeat required";
        else if (strcmp(word, "REGRESSION") && strncmp(word, "suspect", 7) &&
                 !strcmp(m->field[6], "invalid")) word = "invalid";
    }
    if (matched && all_pass) word = "pass";
    snprintf(perf, perf_cap, "performance: %s", word);
    return out;
}

/** The Kernel history card: verdict, the three kinds, performance. */
static void history_card(bench_t *st, tiku_rect_t in)
{
    char verdict[80], perf[64], line[200];
    int y = in.y;
    history_verdict(st, verdict, sizeof verdict, perf, sizeof perf);
    text(st, (tiku_rect_t){in.x, y, in.w, 16}, verdict, 1); y += 16;
    const char *kinds[] = {"full", "selected", "diagnostic"};
    for (int k = 0; k < 3; k++) {
        const past_t *hit = NULL;
        for (int i = 0; i < st->nhistory; i++) {
            const past_t *h = &st->history[i];
            if (!strcmp(h->suite, "kernel") && !strcmp(h->kind, kinds[k]) &&
                !strcmp(h->profile, st->profile)) hit = h;
        }
        if (hit) {
            long n = hit->age;
            snprintf(line, sizeof line, "%s: %s - %ld%c - %s",
                     kinds[k], hit->verdict,
                     n < 3600 ? n / 60 : n < 86400 ? n / 3600 : n / 86400,
                     n < 3600 ? 'm' : n < 86400 ? 'h' : 'd', hit->profile);
        } else snprintf(line, sizeof line, "%s: never", kinds[k]);
        text(st, (tiku_rect_t){in.x + 8, y, in.w - 8, 16}, line, 0); y += 16;
    }
    text(st, (tiku_rect_t){in.x, y, in.w, 16}, perf, 0);
}

/** The three counts over the views, coloured once there is a run. */
static void pills(bench_t *st, int *done)
{
    int pass = 0, fail = 0, skip = 0, x = RX;
    char label[3][32];
    *done = 0;
    for (int i = 0; i < st->nresult; i++) {
        const result_t *r = &st->results[i]; *done += r->done;
        fail += !!(r->fail || r->harness); skip += r->skip;
        pass += r->done && !r->fail && !r->harness && !r->skip;
    }
    snprintf(label[0], sizeof label[0], "%d passed", pass);
    snprintf(label[1], sizeof label[1], "%d failed", fail);
    snprintf(label[2], sizeof label[2], "%d didn't run", skip);
    int tone[3] = {pass ? TIKU_TONE_PASS : TIKU_TONE_IDLE,
                   fail ? TIKU_TONE_FAIL : TIKU_TONE_IDLE,
                   skip ? TIKU_TONE_WARN : TIKU_TONE_IDLE};
    for (int i = 0; i < 3; i++) {
        int w = tiku_ui_pill_w(label[i]);
        tiku_ui_pill(st->surface, (tiku_rect_t){x, 10, w, 20}, label[i],
                     tone[i]);
        x += w + 8;
    }
    text(st, (tiku_rect_t){x + 4, 10, RX + RW - x - 4, 20},
         st->busy ? "running" : st->loading ? "reading the catalogue" :
         st->nresult ? "finished" : "idle", 0);
}

/** The cards: the frames and the words in them; the controls come after. */
static void cards(bench_t *st)
{
    char line[512];
    tiku_rect_t in = tiku_ui_group(st->surface, CARD_DEVICE, "Device");
    if (!*st->profile) snprintf(line, sizeof line, "Host tests available");
    else snprintf(line, sizeof line, "%s | %s", st->profile,
                  st->flash ? "Build + flash" : "Monitor only");
    text(st, (tiku_rect_t){in.x, in.y + 26, in.w, 18}, line, 0);
    in = tiku_ui_group(st->surface, CARD_HISTORY, "Kernel history");
    history_card(st, in);
    in = tiku_ui_group(st->surface, CARD_LINK, "Connection");
    snprintf(line, sizeof line, "Port: %s", st->usb ? "the runner finds it" :
             *st->port ? st->port : "none");
    text(st, (tiku_rect_t){in.x, in.y, in.w, 16}, line, 0);
    text(st, (tiku_rect_t){in.x, in.y + 20, 44, 22}, "Baud", 0);
    (void)tiku_ui_group(st->surface, CARD_MODE, "Build mode");
    (void)tiku_ui_group(st->surface, CARD_OPTIONS, "Options");
    in = tiku_ui_group(st->surface, CARD_EPAPER, "ePaper kit");
    text(st, (tiku_rect_t){in.x, in.y, 56, 22}, "Panel", 0);
    text(st, (tiku_rect_t){in.x, in.y + 26, 56, 22}, "Pattern", 0);
    in = tiku_ui_group(st->surface, CARD_MAKE, "Make");
    text(st, (tiku_rect_t){in.x, in.y, 56, 22}, "Vars", 0);
    text(st, (tiku_rect_t){in.x, in.y + 26, 56, 22}, "CFLAGS", 0);
}

static void paint(bench_t *st)
{
    int done;
    controls(st); detail_counts(st);
    tiku_fill(st->surface, screen_rect(st), TIKU_C_PANEL);
    cards(st);
    pills(st, &done);
    tiku_ui_gauge(st->surface, (tiku_rect_t){RX, 38, RW, 8},
                  st->nresult ? (float)done / st->nresult : 0);
    tiku_tabs_draw(&st->tabs, st->surface, tabs_rect(st));
    int tab = st->tabs.current;
    tiku_rect_t body = list_rect(st, tab);
    char status[512];
    tiku_ui_sunken(st->surface, body, TIKU_C_DOC);
    if (tab == TAB_SUITES) browser_draw(st, body);
    else if (tab == TAB_RESULTS) results_draw(st, body);
    else tiku_list_draw(&st->list[tab], st->surface, body, details_row, st);
    if (tab == TAB_SUITES) {
        text(st, (tiku_rect_t){RX, 90, 46, 24}, "Filter", 0);
        int row = tiku_list_chosen(&st->list[tab]);
        snprintf(status, sizeof status,
                 "%d selected | Space toggles a check; Enter expands a suite.",
                 selected_count(st));
        if (row >= 0 && row < st->nentry) {
            entry_t e = st->entries[row];
            if (e.suite < 0) snprintf(status, sizeof status,
                "%s: %d suite(s). Enter folds the group.",
                st->groups[e.group].name, st->groups[e.group].count);
            else if (e.leaf < 0) snprintf(status, sizeof status, "%.480s",
                                          st->suites[e.suite].description);
            else {
                char reason[400]; const leaf_t *l = &st->leaves[e.leaf];
                snprintf(status, sizeof status, "%s | %s | %s", l->group,
                    l->kind,
                    leaf_supported(st, l, 0) ? (leaf_supported(st, l, 1) ?
                        "Included in full sweep" : "Explicit selection only") :
                    leaf_reason(st, l, reason, sizeof reason));
            }
        }
        tiku_text_block_draw(st->surface,
                             (tiku_rect_t){RX + 4, st->h - 92, RW - 8, 36},
                             TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT, 2);
    } else if (tab == TAB_OUTPUT || tab == TAB_CONSOLE) {
        if (!st->list[tab].count)
            text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 "Latest 256 lines appear here; the whole run is in the log",
                 0);
    } else if (tab == TAB_HISTORY) {
        text(st, (tiku_rect_t){RX, 90, RW, 22},
             "Full, selected and diagnostic runs of the profile stay separate",
             1);
        if (!st->list[tab].count)
            text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 "No records for this profile", 0);
    } else if (tab == TAB_METRICS || tab == TAB_NOTRUN) {
        if (!st->list[tab].count)
            text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 tab == TAB_NOTRUN ? "Every test runs on this profile"
                                   : "No benchmark records for this profile",
                 0);
    }
    controls_draw(st);
    if (*st->prompt)
        text(st, (tiku_rect_t){RX, st->h - 54, RW, 18}, st->prompt, 1);
    text(st, (tiku_rect_t){10, st->h - 28, st->w - 20, 24}, st->note, 0);
    tiku_rect_t screen = screen_rect(st);
    if (st->target.open)
        tiku_choice_draw_popup(&st->target, st->surface, target_rect(), screen);
    if (st->epanel.open)
        tiku_choice_draw_popup(&st->epanel, st->surface, preset_rect(0), screen);
    if (st->epattern.open)
        tiku_choice_draw_popup(&st->epattern, st->surface, preset_rect(1),
                               screen);
    if (st->confirm.open) tiku_alert_draw(&st->confirm, st->surface, ALERT_RECT);
    st->services->frame(st->services->ctx, st->id, st->surface->px,
                        st->w, st->h);
}

static void publish(bench_t *st)
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
            editable(st);
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
            tiku_dialog_focus(&st->dialog, F_BAUD); st->dialog.keyboard = 1;
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
        for (int i = 0; i < st->nentry; i++)
        if (st->entries[i].leaf < 0 && st->entries[i].suite >= 0) {
            suite_t *s = &st->suites[st->entries[i].suite];
            if (suite_supported(st, s)) {
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
        tiku_rect_t body = list_rect(st, TAB_SUITES);
        int row = tiku_list_at(&st->list[TAB_SUITES], body, ev->x, ev->y);
        if (st->tabs.current == TAB_SUITES && row == st->pressed_row &&
            ev->x < body.x + 58)
            toggle_entry(st, row, st->pressed_expand);
        st->pressed_row = -1; st->dirty = 1; return 0;
    }
    tiku_choice_result_t cr = tiku_choice_event(&st->target, ev, target_rect(),
        screen_rect(st), st->dialog.focus == TARGET);
    if (cr == TIKU_CHOICE_IGNORED && st->epaper && editable(st)) {
        /* The presets take the event the same way the device does; a
         * change is a change of argv, nothing else. */
        tiku_choice_result_t pr = tiku_choice_event(&st->epanel, ev,
            preset_rect(0), screen_rect(st),
            st->dialog.focus == PANEL);
        if (pr == TIKU_CHOICE_IGNORED)
            pr = tiku_choice_event(&st->epattern, ev, preset_rect(1),
                screen_rect(st), st->dialog.focus == PATTERN);
        if (pr != TIKU_CHOICE_IGNORED) { st->dirty = 1; return 0; }
    }
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
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, list_rect(st, st->tabs.current));
        st->dirty = 1; return 0;
    }
    tiku_dialog_result_t dr = tiku_dialog_event(&st->dialog, ev);
    if (dr.activated != TIKU_DIALOG_NONE) return action(st, dr.activated);
    for (int i = 0; i < 5; i++) {
        int focus = st->dialog.focus == F_FILTER + i;
        int visible = i == 0 ? st->tabs.current == TAB_SUITES : i == 4 ?
            st->tabs.current == TAB_CONSOLE ||
            st->tabs.current == TAB_OUTPUT : 1;
        if (!visible || (!editable(st) && i != 4) || (i == 1 && st->usb) ||
            (i == 4 && (!st->busy || st->cancelled || st->send_at != st->send_n))) continue;
        tiku_field_result_t fr = tiku_field_event(&st->field[i], &st->input[i], ev, field_rect(st, i), focus);
        if (fr != TIKU_FIELD_IGNORED) {
            if (!i) entries_build(st);
            if (i == 4 && fr == TIKU_FIELD_COMMITTED) action(st, SEND);
            st->dirty = 1; return 0;
        }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        int tab = tiku_tabs_hit(&st->tabs, tabs_rect(st), ev->x, ev->y);
        if (tab >= 0) { tiku_tabs_select(&st->tabs, tab); st->dirty = 1; return 0; }
        tiku_rect_t body = list_rect(st, st->tabs.current);
        int row = tiku_list_at(&st->list[st->tabs.current], body, ev->x, ev->y);
        tiku_list_click(&st->list[st->tabs.current], row, ev->modifiers);
        if (st->tabs.current == TAB_SUITES && row >= 0 && ev->x < body.x + 58)
            { st->pressed_row = row; st->pressed_expand = ev->x < body.x + 22; }
        if (row >= 0) st->dialog.focus = LIST;
    } else if (ev->type == TIKU_EVENT_WHEEL) {
        if (st->tabs.current == TAB_OUTPUT || st->tabs.current == TAB_CONSOLE) st->follow = 0;
        tiku_list_t *l = &st->list[st->tabs.current];
        tiku_list_scroll_to(l, l->top + ev->dy * 3, list_rect(st, st->tabs.current));
    } else if (!dr.taken && ev->type == TIKU_EVENT_KEY_DOWN) {
        if (st->tabs.current == TAB_SUITES && (ev->key == ' ' || ev->key == TIKU_KEY_RETURN))
            action(st, ev->key == ' ' ? TOGGLE : OPEN);
        else tiku_list_key(&st->list[st->tabs.current], ev->key, ev->modifiers, list_rect(st, st->tabs.current));
    }
    st->dirty = 1; return 0;
}
