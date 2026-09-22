/* tiku_bench_view.c -- view.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static void detail_counts(bench_t *st);
static void browser_draw(bench_t *st, tiku_rect_t body);
static void results_draw(bench_t *st, tiku_rect_t body);
static void history_card(bench_t *st, tiku_rect_t in);
static void pills(bench_t *st, int *done);
static void cards(bench_t *st);

const char *tiku_bench_details_row(void *ctx, int row)
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
            if (tiku_bench_leaf_supported(st, l, 0)) continue;
            if (n++ != row) continue;
            char reason[400];
            snprintf(out, sizeof out, "%s | %s | %s", l->id, l->label,
                     tiku_bench_leaf_reason(st, l, reason, sizeof reason));
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
            tiku_list_scroll_to(&st->list[tab], n, tiku_bench_list_rect(st, tab));
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
        int ok = tiku_bench_suite_supported(st, s) && (!leaf || tiku_bench_leaf_supported(st, leaf, 0));
        int checked = leaf ? leaf->selected || (s->selected && tiku_bench_leaf_supported(st, leaf, 1)) : s->selected;
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
                            TIKU_MEASURE_PLAIN, tiku_bench_details_row(st, i), ink);
    }
}

/**
 * The verdict for the active profile, the GTK frontend's rule: the
 * latest full kernel run decides -- none is UNPROVEN, one older than a
 * day is STALE, else Fresh -- and the benchmark records fold into one
 * performance word, a regression outranking a suspect outranking an
 * invalid, pass only when every metric passes.
 */
const char *tiku_bench_history_verdict(const bench_t *st, char *out, size_t cap,
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
    tiku_bench_history_verdict(st, verdict, sizeof verdict, perf, sizeof perf);
    tiku_bench_text(st, (tiku_rect_t){in.x, y, in.w, 15}, verdict, 1); y += 15;
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
        tiku_bench_text(st, (tiku_rect_t){in.x + 8, y, in.w - 8, 15}, line, 0); y += 15;
    }
    tiku_bench_text(st, (tiku_rect_t){in.x, y, in.w, 15}, perf, 0);
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
    tiku_bench_text(st, (tiku_rect_t){x + 4, 10, RX + RW - x - 4, 20},
         st->busy ? "running" : st->loading ? "reading the catalogue" :
         st->nresult ? "finished" : "idle", 0);
}

/** The cards: the frames and the words in them; the controls come after. */
static void cards(bench_t *st)
{
    char line[512];
    tiku_rect_t in = tiku_ui_group(st->surface, TIKU_BENCH_CARD_DEVICE, "Device");
    if (!*st->profile) snprintf(line, sizeof line, "Host tests available");
    else snprintf(line, sizeof line, "%s | %s", st->profile,
                  st->flash ? "Build + flash" : "Monitor only");
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y + 26, in.w, 18}, line, 0);
    in = tiku_ui_group(st->surface, TIKU_BENCH_CARD_HISTORY, "Kernel history");
    history_card(st, in);
    in = tiku_ui_group(st->surface, TIKU_BENCH_CARD_LINK, "Connection");
    snprintf(line, sizeof line, "Port: %s", st->usb ? "the runner finds it" :
             *st->port ? st->port : "none");
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y, in.w, 16}, line, 0);
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y + 20, 44, 22}, "Baud", 0);
    (void)tiku_ui_group(st->surface, TIKU_BENCH_CARD_MODE, "Build mode");
    (void)tiku_ui_group(st->surface, TIKU_BENCH_CARD_OPTIONS, "Options");
    in = tiku_ui_group(st->surface, TIKU_BENCH_CARD_EPAPER, "ePaper kit");
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y, 56, 22}, "Panel", 0);
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y + 26, 56, 22}, "Pattern", 0);
    in = tiku_ui_group(st->surface, TIKU_BENCH_CARD_MAKE, "Make");
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y, 56, 22}, "Vars", 0);
    tiku_bench_text(st, (tiku_rect_t){in.x, in.y + 26, 56, 22}, "CFLAGS", 0);
}

void tiku_bench_paint(bench_t *st)
{
    int done;
    tiku_bench_controls(st); detail_counts(st);
    tiku_fill(st->surface, tiku_bench_screen_rect(st), TIKU_C_PANEL);
    cards(st);
    pills(st, &done);
    tiku_ui_gauge(st->surface, (tiku_rect_t){RX, 38, RW, 8},
                  st->nresult ? (float)done / st->nresult : 0);
    tiku_tabs_draw(&st->tabs, st->surface, tiku_bench_tabs_rect(st));
    int tab = st->tabs.current;
    tiku_rect_t body = tiku_bench_list_rect(st, tab);
    char status[512];
    tiku_ui_sunken(st->surface, body, TIKU_C_DOC);
    if (tab == TAB_SUITES) browser_draw(st, body);
    else if (tab == TAB_RESULTS) results_draw(st, body);
    else tiku_list_draw(&st->list[tab], st->surface, body, tiku_bench_details_row, st);
    if (tab == TAB_SUITES) {
        tiku_bench_text(st, (tiku_rect_t){RX, 90, 46, 24}, "Filter", 0);
        int row = tiku_list_chosen(&st->list[tab]);
        snprintf(status, sizeof status,
                 "%d selected | Space toggles a check; Enter expands a suite.",
                 tiku_bench_selected_count(st));
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
                    tiku_bench_leaf_supported(st, l, 0) ? (tiku_bench_leaf_supported(st, l, 1) ?
                        "Included in full sweep" : "Explicit selection only") :
                    tiku_bench_leaf_reason(st, l, reason, sizeof reason));
            }
        }
        tiku_text_block_draw(st->surface,
                             (tiku_rect_t){RX + 4, st->h - 92, RW - 8, 36},
                             TIKU_MEASURE_PLAIN, status, TIKU_C_TEXT, 2);
    } else if (tab == TAB_OUTPUT || tab == TAB_CONSOLE) {
        if (!st->list[tab].count)
            tiku_bench_text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 "Latest 256 lines appear here; the whole run is in the log",
                 0);
    } else if (tab == TAB_HISTORY) {
        tiku_bench_text(st, (tiku_rect_t){RX, 90, RW, 22},
             "Full, selected and diagnostic runs of the profile stay separate",
             1);
        if (!st->list[tab].count)
            tiku_bench_text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 "No records for this profile", 0);
    } else if (tab == TAB_METRICS || tab == TAB_NOTRUN) {
        if (!st->list[tab].count)
            tiku_bench_text(st, (tiku_rect_t){RX + 10, body.y + 10, RW - 20, 24},
                 tab == TAB_NOTRUN ? "Every test runs on this profile"
                                   : "No benchmark records for this profile",
                 0);
    }
    tiku_bench_controls_draw(st);
    if (*st->prompt)
        tiku_bench_text(st, (tiku_rect_t){RX, st->h - 54, RW, 18}, st->prompt, 1);
    tiku_bench_text(st, (tiku_rect_t){10, st->h - 28, st->w - 20, 24}, st->note, 0);
    tiku_rect_t screen = tiku_bench_screen_rect(st);
    if (st->target.open)
        tiku_choice_draw_popup(&st->target, st->surface, tiku_bench_target_rect(), screen);
    if (st->epanel.open)
        tiku_choice_draw_popup(&st->epanel, st->surface, tiku_bench_preset_rect(0), screen);
    if (st->epattern.open)
        tiku_choice_draw_popup(&st->epattern, st->surface, tiku_bench_preset_rect(1),
                               screen);
    if (st->confirm.open) tiku_alert_draw(&st->confirm, st->surface, TIKU_BENCH_ALERT_RECT);
    st->services->frame(st->services->ctx, st->id, st->surface->px,
                        st->w, st->h);
}
