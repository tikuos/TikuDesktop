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
static void report_open(bench_t *st)
{
    if (!st->services->help_html) { snprintf(st->note, sizeof st->note, "This desktop does not support report pages."); return; }
    char *html = calloc(1, 15001); if (!html) return;
    char device[512];
    report_escape(device, sizeof device,
        st->njob && !queue_needs_device(st) ? "This computer (host-only tests)" : st->target_name);
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
