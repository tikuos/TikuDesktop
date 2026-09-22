/* tiku_bench_catalogue.c -- catalogue.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_bench_private.h"

static void meta_copy(bench_t *st, char *out, size_t cap, const char *value);
static int contains(const char *text, const char *needle);
static int group_of(bench_t *st, const char *name);

/* Registry/profile model shared by the live UI and display-free regressions. */
int tiku_bench_csv_has(const char *csv, const char *word)
{
    if (!strcmp(csv, "*") || !strcmp(csv, "any")) return 1;
    if (!*word) return 0;
    size_t n = strlen(word);
    for (const char *p = csv; *p;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !memcmp(p, word, n)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/** Prefer the explicit checkout, otherwise resolve beside applications/build. */
void tiku_bench_locate_bench(bench_t *st)
{
    const char *env = getenv("TIKU_BENCH");
    if (env && *env) { snprintf(st->directory, sizeof st->directory, "%s", env); return; }
    char exe[1024];
    if (tiku_sys_self_path(exe, sizeof exe) == 0) {
        char *at = strstr(exe, "/applications/");
        if (at) {
            *at = '\0';
            snprintf(st->directory, sizeof st->directory, "%.480s/TikuBench", exe);
            return;
        }
    }
    snprintf(st->directory, sizeof st->directory, "%s/tikuOS/TikuBench",
             getenv("HOME") ? getenv("HOME") : ".");
}

/** Oversized metadata is an error, not a silently incomplete test catalogue. */
static void meta_copy(bench_t *st, char *out, size_t cap, const char *value)
{
    if (snprintf(out, cap, "%s", value) >= (int)cap) st->metadata_bad = 1;
}
#define MC(dst, src) meta_copy(st, dst, sizeof(dst), src)
void tiku_bench_metadata_line(bench_t *st, char *line)
{
    char *f[10]; int n = 0;
    if (!line[0] || line[1] != '\t') return;
    char *p = line;
    while (n < 10) {
        f[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0'; p = tab + 1;
    }
    if (*f[0] == 'B' && n >= 7) {
        if (st->nboard == 32) { st->metadata_bad = 1; return; }
        board_t *b = &st->boards[st->nboard++];
        MC(b->key, f[1]); b->baud = atoi(f[4]); b->large = atoi(f[5]); b->usb = atoi(f[6]);
    } else if (*f[0] == 'S' && n >= 7) {
        if (st->nsuite == NS) { st->metadata_bad = 1; return; }
        suite_t *s = &st->suites[st->nsuite++]; memset(s, 0, sizeof *s);
        MC(s->key, f[1]); MC(s->group, f[2]); MC(s->name, f[3]);
        MC(s->boards, f[4]); MC(s->description, f[5]);
        s->large = atoi(f[6]); s->first = st->nleaf;
        s->host_only = n >= 8 && !strcmp(f[7], "1");
    } else if (*f[0] == 'C' && n >= 9) {
        if (!st->nsuite || st->nleaf == NL ||
            strcmp(st->suites[st->nsuite - 1].key, f[1])) { st->metadata_bad = 1; return; }
        leaf_t *l = &st->leaves[st->nleaf++]; memset(l, 0, sizeof *l);
        MC(l->id, f[2]); MC(l->label, f[3]); MC(l->profiles, f[4]);
        MC(l->group, f[5]); MC(l->kind, f[6]); MC(l->reasons, f[7]); MC(l->full, f[8]);
        st->suites[st->nsuite - 1].count++;
    } else if (*f[0] == 'H' && n >= 7 && st->nhistory < NH) {
        past_t *h = &st->history[st->nhistory++];
        MC(h->board, f[1]); MC(h->suite, f[2]); MC(h->kind, f[3]);
        MC(h->verdict, f[4]); h->age = atol(f[5]); MC(h->profile, f[6]);
    } else if (*f[0] == 'M' && n >= 8 && st->nmetric < NH) {
        metric_t *m = &st->metrics[st->nmetric++];
        for (int i = 1; i < 8; i++) MC(m->field[i], f[i]);
    } else if (*f[0] == 'B' || *f[0] == 'S' || *f[0] == 'C') st->metadata_bad = 1;
}
#undef MC

void tiku_bench_clear_selection(bench_t *st)
{
    for (int i = 0; i < st->nsuite; i++) st->suites[i].selected = 0;
    for (int i = 0; i < st->nleaf; i++) st->leaves[i].selected = 0;
}



int tiku_bench_suite_supported(const bench_t *st, const suite_t *s)
{
    return s->host_only || (st->supported && tiku_bench_csv_has(s->boards, st->mcu) &&
        !(s->large && !strncmp(st->mcu, "msp430", 6) && !st->large));
}
int tiku_bench_leaf_supported(const bench_t *st, const leaf_t *l, int full)
{ return tiku_bench_csv_has(full ? l->full : l->profiles, st->profile); }

/** Unsupported leaves stay visible and explain their canonical exclusion. */
const char *tiku_bench_leaf_reason(const bench_t *st, const leaf_t *l, char *out, size_t cap)
{
    size_t n = strlen(st->profile);
    for (const char *p = l->reasons; *p;) {
        const char *end = strstr(p, ";;");
        if (!strncmp(p, st->profile, n) && p[n] == '=') {
            size_t len = end ? (size_t)(end - p - n - 1) : strlen(p + n + 1);
            snprintf(out, cap, "%.*s", (int)len, p + n + 1); return out;
        }
        if (!end) break;
        p = end + 2;
    }
    return "Not available in this profile";
}

static int contains(const char *text, const char *needle)
{
    if (!*needle) return 1;
    for (; *text; text++) {
        size_t i = 0;
        while (needle[i] && text[i] &&
               tolower((unsigned char)needle[i]) == tolower((unsigned char)text[i])) i++;
        if (!needle[i]) return 1;
    }
    return 0;
}

/** The group a suite belongs to, added to the table on first sight. */
static int group_of(bench_t *st, const char *name)
{
    for (int i = 0; i < st->ngroup; i++)
        if (!strcmp(st->groups[i].name, name)) return i;
    if (st->ngroup == NG) return NG - 1;
    group_t *g = &st->groups[st->ngroup]; memset(g, 0, sizeof *g);
    snprintf(g->name, sizeof g->name, "%s", name);
    return st->ngroup++;
}

/**
 * The suite list as rows: a heading for each group in the catalogue's
 * own order, then the group's suites and, under an expanded suite, its
 * leaves.  A filter shows every group with a match and opens the suites
 * in it; a folded group keeps only its heading.  Headings are marked on
 * the list so the cursor and the checks pass over them.
 */
void tiku_bench_entries_build(bench_t *st)
{
    st->nentry = 0;
    const char *filter = st->field[0].text;
    for (int i = 0; i < st->nsuite; i++) {
        (void)group_of(st, st->suites[i].group);
    }
    for (int g = 0; g < st->ngroup; g++) {
        group_t *grp = &st->groups[g];
        int heading = st->nentry, shown = 0;
        grp->count = 0;
        st->entries[st->nentry++] = (entry_t){-1, -1, g};
        for (int i = 0; i < st->nsuite; i++) {
            suite_t *s = &st->suites[i];
            if (strcmp(s->group, grp->name)) continue;
            int match = contains(s->key, filter) || contains(s->name, filter) || contains(s->group, filter);
            int childmatch = 0;
            for (int j = s->first; j < s->first + s->count; j++)
                childmatch |= contains(st->leaves[j].id, filter) || contains(st->leaves[j].label, filter);
            if (!match && !childmatch) continue;
            grp->count++;
            if (grp->folded && !*filter) continue;
            shown++;
            st->entries[st->nentry++] = (entry_t){i, -1, g};
            if (!s->expanded && !*filter) continue;
            for (int j = s->first; j < s->first + s->count; j++)
                if (match || contains(st->leaves[j].id, filter) || contains(st->leaves[j].label, filter))
                    st->entries[st->nentry++] = (entry_t){i, j, g};
        }
        (void)shown;
        /* Nothing under it: no heading either. */
        if (grp->count == 0) st->nentry = heading;
    }
    tiku_list_set_count(&st->list[TAB_SUITES], st->nentry);
    for (int i = 0; i < st->nentry; i++) {
        tiku_list_set_heading(&st->list[TAB_SUITES], i,
                              st->entries[i].suite < 0);
    }
}

/** The rows of the Did not run tab: every leaf the active profile excludes. */
void tiku_bench_notrun_build(bench_t *st)
{
    int n = 0;
    for (int i = 0; i < st->nleaf; i++)
        if (!tiku_bench_leaf_supported(st, &st->leaves[i], 0)) n++;
    tiku_list_set_count(&st->list[TAB_NOTRUN], n);
}

void tiku_bench_toggle_entry(bench_t *st, int row, int expand)
{
    if (st->busy || st->loading || row < 0 || row >= st->nentry) return;
    entry_t e = st->entries[row];
    if (e.suite < 0) {              /* a heading: fold or open its group */
        st->groups[e.group].folded = !st->groups[e.group].folded;
        tiku_bench_entries_build(st); return;
    }
    suite_t *s = &st->suites[e.suite];
    if (expand && e.leaf < 0) { s->expanded = !s->expanded; tiku_bench_entries_build(st); return; }
    if (!tiku_bench_suite_supported(st, s)) return;
    if (e.leaf < 0) {
        s->selected = !s->selected;
        for (int i = s->first; i < s->first + s->count; i++) st->leaves[i].selected = 0;
    } else if (tiku_bench_leaf_supported(st, &st->leaves[e.leaf], 0)) {
        s->selected = 0; st->leaves[e.leaf].selected = !st->leaves[e.leaf].selected;
    }
}

/** Build an immutable queue. Full-suite selection never expands into --only. */
int tiku_bench_queue_build(bench_t *st)
{
    st->njob = st->job = 0;
    if (!st->metadata_ok) return 0;
    for (int i = 0; i < st->nsuite; i++) {
        suite_t *s = &st->suites[i];
        if (!tiku_bench_suite_supported(st, s)) continue;
        job_t *job = &st->jobs[st->njob]; job->suite = i; job->only[0] = '\0';
        snprintf(job->key, sizeof job->key, "%s", s->key);
        size_t used = 0;
        if (!s->selected) for (int j = s->first; j < s->first + s->count; j++) {
            leaf_t *l = &st->leaves[j];
            if (!l->selected || !tiku_bench_leaf_supported(st, l, 0)) continue;
            size_t n = strlen(l->id);
            if (used + n + 2 > sizeof job->only) {
                snprintf(st->note, sizeof st->note, "Selection exceeds command capacity; split the run.");
                st->njob = 0; return 0;
            }
            if (used) job->only[used++] = ',';
            memcpy(job->only + used, l->id, n + 1); used += n;
        }
        if (s->selected || used) st->njob++;
    }
    return st->njob > 0;
}

/** A host-only queue must not disconnect a board, even if one is selected. */
int tiku_bench_queue_needs_device(const bench_t *st)
{
    for (int i = 0; i < st->njob; i++)
        if (!st->suites[st->jobs[i].suite].host_only) return 1;
    return 0;
}
