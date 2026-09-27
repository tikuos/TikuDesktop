/* tiku_bench_results.c -- results.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static void result_end(bench_t *st, const char *id, int pass, int fail, int harness);
static void report_escape(char *out, size_t cap, const char *in);

void tiku_bench_log_line(bench_t *st, const char *text, int output)
{
    logline_t *l = &st->logs[st->log_head];
    snprintf(l->text, sizeof l->text, "%s", text); l->output = output;
    st->log_head = (st->log_head + 1) % 256;
    if (st->nlog < 256) st->nlog++;
    if (st->log) { fprintf(st->log, "%s\n", text); fflush(st->log); }
    st->dirty = 1;
}

result_t *tiku_bench_result_add(bench_t *st, int job, const char *id)
{
    if (st->nresult == NR) { st->batch_failed = 1; return NULL; }
    result_t *r = &st->results[st->nresult++]; memset(r, 0, sizeof *r);
    r->job = job; snprintf(r->id, sizeof r->id, "%s", id);
    snprintf(r->text, sizeof r->text, "Pending: %.150s", id);
    return r;
}

void tiku_bench_plan_build(bench_t *st)
{
    st->nresult = 0;
    for (int k = 0; k < st->njob; k++) {
        const job_t *job = &st->jobs[k]; const suite_t *s = &st->suites[job->suite];
        int before = st->nresult;
        for (int i = s->first; i < s->first + s->count; i++) {
            const leaf_t *l = &st->leaves[i];
            if (*job->only ? tiku_bench_csv_has(job->only, l->id) : tiku_bench_leaf_supported(st, l, 1))
                tiku_bench_result_add(st, k, l->id);
        }
        if (st->nresult == before) tiku_bench_result_add(st, k, s->key);
    }
}

/** Host validation may revise a firmware-green row, rather than add a second. */
static void result_end(bench_t *st, const char *id, int pass, int fail, int harness)
{
    result_t *r = NULL;
    for (int i = 0; i < st->nresult; i++)
        if (st->results[i].job == st->job && !strcmp(st->results[i].id, id)) r = &st->results[i];
    if (!r) r = tiku_bench_result_add(st, st->job, id);
    if (!r) return;
    r->done = 1; r->pass = pass; r->fail = fail; r->harness = harness;
    r->skip = pass == 0 && fail == 0 && !harness;
    snprintf(r->text, sizeof r->text, "%s %.150s | pass %d, fail %d%s",
        fail || harness ? "FAIL" : r->skip ? "SKIP" : "PASS", id, pass, fail,
        harness ? " | harness validation failed" : "");
    st->dirty = 1;
}

void tiku_bench_consume_line(bench_t *st, char *line)
{
    if (st->loading) { tiku_bench_metadata_line(st, line); return; }
    if (st->fw.step != FWS_IDLE) { tiku_bench_fw_line(st, line); return; }
    /* Strip ANSI escape sequences; keep readable text in logs and UI. */
    char clean[LINE_MAXIMUM]; size_t at = 0;
    for (size_t i = 0; line[i] && at + 1 < sizeof clean; i++) {
        if ((unsigned char)line[i] == 27 && line[i + 1] == '[') {
            i += 2; while (line[i] && !(line[i] >= '@' && line[i] <= '~')) i++;
            if (!line[i]) break;
        } else if ((unsigned char)line[i] >= 32 || line[i] == '\t') clean[at++] = line[i];
    }
    clean[at] = '\0';
    if (strstr(clean, "[[HITL:UNPLUG]]")) snprintf(st->prompt, sizeof st->prompt, "Action required: unplug the test board");
    else if (strstr(clean, "[[HITL:REPLUG]]")) snprintf(st->prompt, sizeof st->prompt, "Action required: reconnect the test board");
    else if (strstr(clean, "[[HITL:DONE]]")) st->prompt[0] = '\0';
    if (strstr(clean, "[BUILD]") || strstr(clean, "[FLASH]")) st->in_build = 1;
    if (strstr(clean, "[MONITOR]") || strstr(clean, "[TS:BEGIN]")) st->in_build = 0;
    tiku_bench_log_line(st, clean, !st->in_build && !strstr(clean, "[TC:"));
    if (strstr(clean, "Build + flash succeeded") || strstr(clean, "Flash OK") ||
        strstr(clean, "Build failed") || strstr(clean, "Flash failed")) st->in_build = 0;
    const char *begin = strstr(clean, "[TC:RUN] ");
    char suite[96], category[160];
    if (begin) snprintf(st->active, sizeof st->active, "%s", begin + 9);
    else if (sscanf(clean, "== %95[^:]: %159s --", suite, category) == 2)
        snprintf(st->active, sizeof st->active, "%s", category);
    if (begin || strstr(clean, "[BUILD]") || strstr(clean, "[FLASH]") || strstr(clean, "[MONITOR]"))
        snprintf(st->note, sizeof st->note, "%.500s", clean);
    char *mark = strstr(clean, "[TC:END] ");
    if (mark) {
        char *counts = strstr(mark + 9, " pass=");
        int pass = 0, fail = 0, harness = 0;
        if (counts && sscanf(counts, " pass=%d fail=%d", &pass, &fail) == 2 &&
            pass >= 0 && fail >= 0 && pass <= 100000000 && fail <= 100000000) {
            char *hf = strstr(counts, " harness_fail=");
            if (hf) harness = atoi(hf + 14) != 0;
            *counts = '\0'; result_end(st, mark + 9, pass, fail, harness);
        } else { st->batch_failed = 1; tiku_bench_log_line(st, "Invalid completion marker", 1); }
    } else if (strstr(clean, "[TS:END]")) {
        char *counts = strstr(clean, "total="); int total, pass, fail;
        if (counts && sscanf(counts, "total=%d pass=%d fail=%d", &total, &pass, &fail) == 3 &&
            total >= 0 && pass >= 0 && fail >= 0 && pass <= 100000000 && fail <= 100000000) {
            const char *id = *st->active ? st->active : st->jobs[st->job].key;
            result_end(st, id, pass, fail, 0);
            if (!strcmp(st->jobs[st->job].key, "kernel")) {
                for (int i = 0; i < st->nresult; i++) if (st->results[i].job == st->job &&
                    !strcmp(st->results[i].id, id)) {
                    st->results[i].done = 0;
                    snprintf(st->results[i].text, sizeof st->results[i].text,
                        "Firmware: %.150s | %d/%d | awaiting host validation", id, pass, total);
                }
            }
        }
    } else if (strstr(clean, "[FAIL]") || strstr(clean, "[T:F:") ||
               strstr(clean, "Build failed") || strstr(clean, "Flash failed")) {
        result_t *r = tiku_bench_result_add(st, st->job, clean);
        if (r) { r->done = r->fail = 1; snprintf(r->text, sizeof r->text, "FAIL %.240s", clean); }
    } else if (strstr(clean, "[WARN]")) {
        result_t *r = tiku_bench_result_add(st, st->job, clean);
        if (r) { r->done = r->skip = 1; snprintf(r->text, sizeof r->text, "WARN %.240s", clean); }
    }
    /* Suites without enumerated leaves still need evidence of actual tests. */
    const suite_t *s = &st->suites[st->jobs[st->job].suite];
    if (!s->count && (strstr(clean, "[PASS]") || strstr(clean, "[T:P:"))) {
        for (int i = 0; i < st->nresult; i++) if (st->results[i].job == st->job &&
            !strcmp(st->results[i].id, s->key)) {
            int pass = st->results[i].pass + 1;
            result_end(st, s->key, pass, st->results[i].fail, st->results[i].harness); break;
        }
    }
}

/* A user-requested snapshot, never another test run or hardware operation. */

/** @brief Device text is data, not executable document markup. */
static void report_escape(char *out, size_t cap, const char *in)
{
    size_t n = 0;
    for (const char *s = in; *s && n + 7 < cap; s++) {
        const char *escaped = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;
        if (escaped) { size_t len = strlen(escaped); memcpy(out+n, escaped, len); n += len; }
        else if ((unsigned char)*s >= 32 || *s == '\n' || *s == '\t') out[n++] = *s;
    }
    out[n] = 0;
}

/** @brief The Help reader owns the report copy after this call returns. */
void tiku_bench_report_open(bench_t *st)
{
    if (!st->services->help_html) { snprintf(st->note, sizeof st->note, "This desktop does not support report pages."); return; }
    char *html = calloc(1, 15001); if (!html) return;
    char device[512];
    report_escape(device, sizeof device,
        st->njob && !tiku_bench_queue_needs_device(st) ? "This computer (host-only tests)" : st->target_name);
    int used = snprintf(html, 15001, "<p>Target: <b>%s</b>. %s</p><table><tr><th>Test</th><th>Result</th></tr>", device,
        st->busy ? "Run still in progress; this is a snapshot." : "Completed results snapshot.");
    int shown = 0;
    for (; shown < st->nresult && used < 12000; shown++) {
        result_t *r = &st->results[shown]; char id[1024], detail[1600];
        report_escape(id, sizeof id, r->id); report_escape(detail, sizeof detail, r->text);
        used += snprintf(html+used, 15001-(size_t)used,
            "<tr><td>%s</td><td>Pass %d; fail %d; harness %d; skip %d. %s</td></tr>",
            id, r->pass, r->fail, r->harness, r->skip, detail);
    }
    snprintf(html+used, 15001-(size_t)used, "</table><p>%d of %d result rows shown. The complete results and log remain in TikuBench.</p>", shown, st->nresult);
    if (!st->services->help_html(st->services->ctx, "TikuBench results", html))
        snprintf(st->note, sizeof st->note, "Could not open report; results remain available here.");
    free(html); st->dirty = 1;
}
