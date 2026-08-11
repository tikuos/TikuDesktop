/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_query.c - predicates, matching, and walking the namespace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_query.h"
#include "tiku_trk_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESULTS_MAX 512
#define QUEUE_MAX   256

void
tiku_trk_query_init(tiku_trk_query_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof *q);
        q->max_results = RESULTS_MAX;
    }
}

void
tiku_trk_query_set_name(tiku_trk_query_t *q, const char *text)
{
    if (q == NULL) {
        return;
    }
    q->mode = TIKU_TRK_QMODE_NAME;
    q->term_count = 0;
    /* The star decides the operator, and it is looked for ANYWHERE in the
     * typed text rather than at either end: "*.txt" and "a*b" are both
     * whole-name patterns, "txt" is a substring. */
    (void)tiku_trk_query_add_term(q, TIKU_TRK_QF_NAME,
                                  (text != NULL && strchr(text, '*') != NULL)
                                      ? TIKU_TRK_OP_IS
                                      : TIKU_TRK_OP_CONTAINS,
                                  text, TIKU_TRK_JOIN_AND);
}

int
tiku_trk_query_add_term(tiku_trk_query_t *q, tiku_trk_qfield_t field,
                        tiku_trk_qop_t op, const char *value,
                        tiku_trk_qjoin_t join)
{
    tiku_trk_qterm_t *t;

    if (q == NULL || q->term_count >= TIKU_TRK_QUERY_TERMS_MAX) {
        return -1;
    }
    t = &q->term[q->term_count];
    memset(t, 0, sizeof *t);
    t->field = field;
    t->op = op;
    t->join = join;
    snprintf(t->value, sizeof t->value, "%s", (value != NULL) ? value : "");
    return q->term_count++;
}

int
tiku_trk_query_add_scope(tiku_trk_query_t *q, const char *path)
{
    char norm[TIKU_TRK_PATH_MAX];
    int i;
    size_t n;

    if (q == NULL || path == NULL || path[0] == '\0' ||
        q->scope_count >= TIKU_TRK_QUERY_SCOPES_MAX) {
        return 0;
    }
    /* Normalise BEFORE comparing: a trailing separator is appended so the
     * prefix test cannot match a sibling whose name merely starts the same
     * way (/dev must not admit /devices), and comparing the raw path against
     * an already-normalised one would never find the duplicate. */
    snprintf(norm, sizeof norm, "%s", path);
    n = strlen(norm);
    if (n > 0u && norm[n - 1u] != '/' && n + 1u < sizeof norm) {
        norm[n] = '/';
        norm[n + 1u] = '\0';
    }
    for (i = 0; i < q->scope_count; i++) {
        if (strcmp(q->scope[i], norm) == 0) {
            return 0;
        }
    }
    memcpy(q->scope[q->scope_count], norm, strlen(norm) + 1u);
    q->scope_count++;
    return 1;
}

void
tiku_trk_query_clear_scope(tiku_trk_query_t *q)
{
    if (q != NULL) {
        q->scope_count = 0;
    }
}

int
tiku_trk_query_in_scope(const tiku_trk_query_t *q, const char *path)
{
    int i;

    if (q == NULL || q->scope_count == 0) {
        return 1;                     /* no filter: the whole namespace   */
    }
    for (i = 0; i < q->scope_count; i++) {
        size_t n = strlen(q->scope[i]);

        if (strncmp(q->scope[i], path, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* matching                                                                  */
/*---------------------------------------------------------------------------*/

static int
is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int
lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int
tiku_trk_query_pattern(const char *value, tiku_trk_qop_t op, char *out,
                       size_t max)
{
    size_t o = 0;
    const char *p = value;

    if (out == NULL || max == 0u) {
        return 0;
    }
    /* Substring and suffix searches are anchored by a leading star; the
     * value itself never carries one. */
    if ((op == TIKU_TRK_OP_CONTAINS || op == TIKU_TRK_OP_ENDS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    for (; p != NULL && *p != '\0'; p++) {
        if (*p == ' ') {
            /* A space matches any run, which is why "foo bar" also finds
             * "foo-bar" and "foo.bar". */
            if (o + 1u < max) { out[o++] = '*'; }
            continue;
        }
        if (is_alpha((unsigned char)*p)) {
            /* Case-insensitivity is spelled as a class per letter rather
             * than as a flag on the comparison. */
            if (o + 4u < max) {
                out[o++] = '[';
                out[o++] = (char)lower((unsigned char)*p);
                out[o++] = (char)(lower((unsigned char)*p) - 32);
                out[o++] = ']';
            }
            continue;
        }
        if (o + 1u < max) { out[o++] = *p; }
    }
    if ((op == TIKU_TRK_OP_CONTAINS || op == TIKU_TRK_OP_BEGINS_WITH) &&
        o + 1u < max) {
        out[o++] = '*';
    }
    out[o] = '\0';
    return (int)o;
}

int
tiku_trk_query_glob(const char *pattern, const char *name)
{
    const char *star = NULL, *retry = NULL;

    if (pattern == NULL || name == NULL) {
        return 0;
    }
    while (*name != '\0') {
        if (*pattern == '[' && strchr(pattern, ']') != NULL) {
            /* A '[' with no ']' after it is not a class -- it is a literal
             * bracket in a name.  Treating it as an unterminated class let
             * "[abc" match "a", because the scan ran to the terminator and
             * the pattern was then considered spent. */
            const char *cl = pattern + 1;
            int neg = 0, hit = 0;

            if (*cl == '!' || *cl == '^') { neg = 1; cl++; }
            for (; *cl != '\0' && *cl != ']'; cl++) {
                if (cl[1] == '-' && cl[2] != '\0' && cl[2] != ']') {
                    if ((unsigned char)*name >= (unsigned char)cl[0] &&
                        (unsigned char)*name <= (unsigned char)cl[2]) {
                        hit = 1;
                    }
                    cl += 2;
                } else if (*cl == *name) {
                    hit = 1;
                }
            }
            if (hit != neg) {
                pattern = (*cl == ']') ? cl + 1 : cl;
                name++;
                continue;
            }
        } else if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
            continue;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
            continue;
        }
        if (star != NULL) {
            /* Backtrack: the star swallows one more character. */
            pattern = star + 1;
            name = ++retry;
            continue;
        }
        return 0;
    }
    while (*pattern == '*') {
        pattern++;
    }
    return (*pattern == '\0');
}

/** @brief The text a term tests against. */
static const char *
field_text(tiku_trk_qfield_t f, const tiku_trk_model_t *m,
           const tiku_trk_desc_t *d, const char *value, char *buf,
           size_t max)
{
    switch (f) {
    case TIKU_TRK_QF_PATH:  return m->path;
    case TIKU_TRK_QF_KIND:  return tiku_trk_model_kind_string(m);
    case TIKU_TRK_QF_VALUE: return (value != NULL) ? value : "";
    case TIKU_TRK_QF_CAP:   return m->facts.req_cap;
    case TIKU_TRK_QF_UNIT:  return (d != NULL) ? d->unit_name : "";
    case TIKU_TRK_QF_SIZE:
        snprintf(buf, max, "%lld", (long long)m->facts.size);
        return buf;
    case TIKU_TRK_QF_WRITABLE:
        return tiku_trk_model_is_writable(m) ? "yes" : "no";
    default:
        return m->name;
    }
}

/** @brief Whether @p s is entirely a number, and its value. */
static int
looks_numeric(const char *s, long *v)
{
    char *end;

    *v = 0;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    *v = strtol(s, &end, 0);
    if (end == s) {
        return 0;
    }
    while (*end == ' ' || *end == '\r' || *end == '\n') {
        end++;
    }
    return (*end == '\0');
}

/** @brief Whether a field is compared as a number rather than as text. */
static int
numeric_field(tiku_trk_qfield_t f, const tiku_trk_desc_t *d)
{
    if (f == TIKU_TRK_QF_SIZE) {
        return 1;
    }
    return (f == TIKU_TRK_QF_VALUE && d != NULL &&
            d->vtype != TIKU_TRK_VT_NONE && d->vtype != TIKU_TRK_VT_STR);
}

static int
term_matches(const tiku_trk_qterm_t *t, const tiku_trk_model_t *m,
             const tiku_trk_desc_t *d, const char *value)
{
    char buf[64], pat[TIKU_TRK_QUERY_VALUE_MAX * 4 + 8];
    const char *text = field_text(t->field, m, d, value, buf, sizeof buf);

    if (t->op == TIKU_TRK_OP_GREATER || t->op == TIKU_TRK_OP_LESS) {
        long a, b;
        int text_num = looks_numeric(text, &a);
        int want_num = looks_numeric(t->value, &b);

        /* Tracker takes the type from the attribute registry and simply does
         * not offer an ordering operator on anything untyped.  Most nodes
         * here ARE untyped, so refusing would make the operator useless;
         * instead the values decide.  Both numeric compares as numbers --
         * without which "30" > "1000" is true, because '3' > '1'. */
        if (want_num && text_num) {
            return (t->op == TIKU_TRK_OP_GREATER) ? (a > b) : (a < b);
        }
        /* Asked for a number and given a name, the answer is no: a device
         * name is not greater than 1000, nor less than it. */
        if (want_num != text_num) {
            return 0;
        }
        if (numeric_field(t->field, d)) {
            return (t->op == TIKU_TRK_OP_GREATER) ? (a > b) : (a < b);
        }
        return (t->op == TIKU_TRK_OP_GREATER)
                   ? (strcmp(text, t->value) > 0)
                   : (strcmp(text, t->value) < 0);
    }
    (void)tiku_trk_query_pattern(t->value, t->op, pat, sizeof pat);
    {
        int hit = tiku_trk_query_glob(pat, (text != NULL) ? text : "");

        return (t->op == TIKU_TRK_OP_IS_NOT) ? !hit : hit;
    }
}

int
tiku_trk_query_matches(const tiku_trk_query_t *q, const tiku_trk_model_t *m,
                       const tiku_trk_desc_t *d, const char *value)
{
    int i, acc = 0;

    if (q == NULL || m == NULL || q->term_count == 0) {
        return 1;
    }
    /* Terms fold left, each joining the accumulated result the way its own
     * join says -- the same shape as the panel, where the and/or control
     * sits on the row ABOVE the one it governs. */
    acc = term_matches(&q->term[0], m, d, value);
    for (i = 1; i < q->term_count; i++) {
        int r = term_matches(&q->term[i], m, d, value);

        acc = (q->term[i].join == TIKU_TRK_JOIN_OR) ? (acc || r) : (acc && r);
    }
    return acc;
}

/*---------------------------------------------------------------------------*/
/* rendering                                                                 */
/*---------------------------------------------------------------------------*/

static const char *
field_name(tiku_trk_qfield_t f)
{
    switch (f) {
    case TIKU_TRK_QF_PATH:     return "path";
    case TIKU_TRK_QF_KIND:     return "kind";
    case TIKU_TRK_QF_VALUE:    return "value";
    case TIKU_TRK_QF_SIZE:     return "size";
    case TIKU_TRK_QF_CAP:      return "cap";
    case TIKU_TRK_QF_UNIT:     return "unit";
    case TIKU_TRK_QF_WRITABLE: return "writable";
    default:                   return "name";
    }
}

int
tiku_trk_query_predicate(const tiku_trk_query_t *q, char *out, size_t max)
{
    int i;
    size_t o = 0;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; q != NULL && i < q->term_count; i++) {
        const tiku_trk_qterm_t *t = &q->term[i];
        char pat[TIKU_TRK_QUERY_VALUE_MAX * 4 + 8];
        const char *op = "==";
        char frag[TIKU_TRK_QUERY_VALUE_MAX * 4 + 64];

        if (i > 0) {
            o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s",
                                  (t->join == TIKU_TRK_JOIN_OR) ? "||" : "&&");
        }
        switch (t->op) {
        case TIKU_TRK_OP_IS_NOT:  op = "!="; break;
        case TIKU_TRK_OP_GREATER: op = ">";  break;
        case TIKU_TRK_OP_LESS:    op = "<";  break;
        default:                  op = "=="; break;
        }
        if (t->op == TIKU_TRK_OP_GREATER || t->op == TIKU_TRK_OP_LESS) {
            snprintf(frag, sizeof frag, "(%s%s%s)", field_name(t->field), op,
                     t->value);
        } else {
            /* The pattern is what is really compared, so it is what the
             * formula shows: an editable predicate that lies about what it
             * will do is worse than none. */
            (void)tiku_trk_query_pattern(t->value, t->op, pat, sizeof pat);
            snprintf(frag, sizeof frag, "(%s%s\"%s\")", field_name(t->field),
                     op, pat);
        }
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "%s", frag);
        if (o >= max) {
            break;
        }
    }
    return (int)strlen(out);
}

/*---------------------------------------------------------------------------*/
/* running                                                                   */
/*---------------------------------------------------------------------------*/

struct tiku_trk_qrun {
    tiku_trk_backend_t *backend;
    tiku_trk_query_t    q;
    char                queue[QUEUE_MAX][TIKU_TRK_PATH_MAX];
    int                 head, tail;
    tiku_trk_model_t   *results;
    int                 count;
    int                 visited;
    int                 done;
    /* Live: the snapshot taken at the start of a refresh, and how many of
     * the current results are new since it. */
    unsigned char      *fresh;
    int                 refreshes;
};

static void
enqueue(tiku_trk_qrun_t *r, const char *path)
{
    int next = (r->tail + 1) % QUEUE_MAX;

    if (next == r->head) {
        return;                /* the queue is full; this subtree is cut  */
    }
    snprintf(r->queue[r->tail], TIKU_TRK_PATH_MAX, "%s", path);
    r->tail = next;
}

/** @brief Receives one entry during the walk. */
static int
visit(const tiku_trk_model_t *m, void *ctx)
{
    tiku_trk_qrun_t *r = ctx;
    tiku_trk_desc_t d;
    char value[128];

    if (tiku_trk_model_is_container(m)) {
        /* Descend regardless of whether the directory itself matches: what
         * is being searched for is usually inside it. */
        enqueue(r, m->path);
    }
    if (r->count >= r->q.max_results) {
        return 1;
    }
    (void)tiku_trk_desc_parse(m->facts.meta, &d);
    value[0] = '\0';
    /* A term on the value has to read it, which costs a round trip -- so it
     * is only paid when a term actually asks for one. */
    {
        int i, wants = 0;

        for (i = 0; i < r->q.term_count; i++) {
            if (r->q.term[i].field == TIKU_TRK_QF_VALUE) {
                wants = 1;
            }
        }
        if (wants && !tiku_trk_model_is_container(m) &&
            r->backend->ops->read != NULL) {
            int n = r->backend->ops->read(r->backend, m->path, value,
                                          sizeof value - 1u);
            if (n > 0) {
                value[n] = '\0';
                while (n > 0 && (value[n - 1] == '\n' ||
                                 value[n - 1] == '\r')) {
                    value[--n] = '\0';
                }
            }
        }
    }
    if (tiku_trk_query_in_scope(&r->q, m->path) &&
        tiku_trk_query_matches(&r->q, m, &d, value)) {
        r->results[r->count++] = *m;
    }
    return 0;
}

tiku_trk_qrun_t *
tiku_trk_query_start(tiku_trk_backend_t *b, const tiku_trk_query_t *q)
{
    tiku_trk_qrun_t *r;
    int i;

    if (b == NULL || q == NULL) {
        return NULL;
    }
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    r->results = calloc(RESULTS_MAX, sizeof *r->results);
    if (r->results == NULL) {
        free(r);
        return NULL;
    }
    r->backend = b;
    r->q = *q;
    if (r->q.max_results <= 0 || r->q.max_results > RESULTS_MAX) {
        r->q.max_results = RESULTS_MAX;
    }
    /* Walking starts at the scopes when there are any, so a scoped search
     * does not read the whole namespace and then throw most of it away. */
    if (q->scope_count > 0) {
        for (i = 0; i < q->scope_count; i++) {
            char start[TIKU_TRK_PATH_MAX];
            size_t n;

            snprintf(start, sizeof start, "%s", q->scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            enqueue(r, start);
        }
    } else {
        enqueue(r, "/");
    }
    return r;
}

void
tiku_trk_query_stop(tiku_trk_qrun_t *r)
{
    if (r != NULL) {
        free(r->fresh);
        free(r->results);
        free(r);
    }
}

int
tiku_trk_query_step(tiku_trk_qrun_t *r, int budget)
{
    int before;

    if (r == NULL || r->done) {
        return -1;
    }
    before = r->count;
    while (budget-- > 0 && r->head != r->tail) {
        char dir[TIKU_TRK_PATH_MAX];

        snprintf(dir, sizeof dir, "%s", r->queue[r->head]);
        r->head = (r->head + 1) % QUEUE_MAX;
        r->visited++;
        (void)r->backend->ops->list(r->backend, dir, visit, r);
        if (r->count >= r->q.max_results) {
            break;
        }
    }
    if (r->head == r->tail || r->count >= r->q.max_results) {
        r->done = 1;
    }
    return r->count - before;
}

int
tiku_trk_query_count(const tiku_trk_qrun_t *r)
{
    return (r != NULL) ? r->count : 0;
}

const tiku_trk_model_t *
tiku_trk_query_at(const tiku_trk_qrun_t *r, int i)
{
    if (r == NULL || i < 0 || i >= r->count) {
        return NULL;
    }
    return &r->results[i];
}

int
tiku_trk_query_is_new(const tiku_trk_qrun_t *r, int i)
{
    if (r == NULL || r->fresh == NULL || i < 0 || i >= r->count) {
        return 0;
    }
    return r->fresh[i];
}

int
tiku_trk_query_refresh(tiku_trk_qrun_t *r, tiku_trk_qdiff_t *out)
{
    tiku_trk_model_t *old;
    unsigned char *seen;
    int old_count, i, j;

    if (r == NULL) {
        return -1;
    }
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    /* Snapshot, then re-walk.  The view is NOT cleared: a result that is
     * still there keeps its row, so nothing flickers and no selection is
     * lost merely because a value moved. */
    old = calloc((size_t)(r->count > 0 ? r->count : 1), sizeof *old);
    seen = calloc((size_t)(r->count > 0 ? r->count : 1), 1);
    if (old == NULL || seen == NULL) {
        free(old);
        free(seen);
        return -1;
    }
    old_count = r->count;
    for (i = 0; i < old_count; i++) {
        old[i] = r->results[i];
    }

    r->count = 0;
    r->visited = 0;
    r->head = r->tail = 0;
    r->done = 0;
    if (r->q.scope_count > 0) {
        for (i = 0; i < r->q.scope_count; i++) {
            char start[TIKU_TRK_PATH_MAX];
            size_t n;

            snprintf(start, sizeof start, "%s", r->q.scope[i]);
            n = strlen(start);
            if (n > 1u && start[n - 1u] == '/') {
                start[n - 1u] = '\0';
            }
            enqueue(r, start);
        }
    } else {
        enqueue(r, "/");
    }
    while (!r->done) {
        if (tiku_trk_query_step(r, 16) < 0) {
            break;
        }
    }

    if (r->fresh != NULL) {
        free(r->fresh);
    }
    r->fresh = calloc((size_t)(r->count > 0 ? r->count : 1), 1);

    /* Mark survivors in the snapshot as they are re-found.  Leaving them
     * marked unseen is the hazard the original carries: everything still in
     * the snapshot at the end is treated as gone, so a result that never
     * left would be deleted and re-added on every refresh. */
    for (i = 0; i < r->count; i++) {
        int survived = 0;

        for (j = 0; j < old_count; j++) {
            if (!seen[j] && strcmp(old[j].path, r->results[i].path) == 0) {
                seen[j] = 1;
                survived = 1;
                break;
            }
        }
        if (!survived) {
            if (r->fresh != NULL) {
                r->fresh[i] = 1;
            }
            if (out != NULL) {
                out->added++;
            }
        }
    }
    for (j = 0; j < old_count; j++) {
        tiku_trk_model_t probe;

        if (seen[j]) {
            continue;
        }
        if (out != NULL) {
            out->removed++;
            /* Why it left is only knowable now: ask whether the node is
             * still there at all.  Afterwards the two cases are
             * indistinguishable. */
            if (r->backend->ops->stat == NULL ||
                r->backend->ops->stat(r->backend, old[j].path, &probe) != 0) {
                out->vanished++;
            }
        }
    }
    r->refreshes++;
    free(old);
    free(seen);
    return 0;
}

int
tiku_trk_query_visited(const tiku_trk_qrun_t *r)
{
    return (r != NULL) ? r->visited : 0;
}

int
tiku_trk_query_done(const tiku_trk_qrun_t *r)
{
    return (r != NULL) ? r->done : 1;
}

/*---------------------------------------------------------------------------*/
/* Saved queries                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serialise a query to one line per field.
 *
 * Text rather than a struct dump: a saved query outlives the build that
 * wrote it, and a layout change must not silently reinterpret an old one.
 */
static int
serialise(const tiku_trk_query_t *q, char *out, size_t max)
{
    size_t o = 0;
    int i;

    o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                          "v1\nmode %d\n", (int)q->mode);
    for (i = 0; i < q->term_count; i++) {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u,
                              "t %d %d %d %s\n", (int)q->term[i].field,
                              (int)q->term[i].op, (int)q->term[i].join,
                              q->term[i].value);
    }
    for (i = 0; i < q->scope_count; i++) {
        o += (size_t)snprintf(out + o, (o < max) ? max - o : 0u, "s %s\n",
                              q->scope[i]);
    }
    return (o < max) ? (int)o : -1;
}

static int
deserialise(tiku_trk_query_t *q, const char *in)
{
    const char *p = in;

    tiku_trk_query_init(q);
    if (in == NULL || strncmp(in, "v1\n", 3) != 0) {
        return -1;              /* an unknown version is not guessed at   */
    }
    p += 3;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        char line[TIKU_TRK_PATH_MAX + 64];
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (n >= sizeof line) {
            n = sizeof line - 1u;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        if (strncmp(line, "mode ", 5) == 0) {
            q->mode = (tiku_trk_qmode_t)atoi(line + 5);
        } else if (line[0] == 't' && line[1] == ' ') {
            int f = 0, op = 0, j = 0, used = 0;

            if (sscanf(line + 2, "%d %d %d %n", &f, &op, &j, &used) >= 3) {
                (void)tiku_trk_query_add_term(q, (tiku_trk_qfield_t)f,
                                              (tiku_trk_qop_t)op,
                                              line + 2 + used,
                                              (tiku_trk_qjoin_t)j);
            }
        } else if (line[0] == 's' && line[1] == ' ') {
            (void)tiku_trk_query_add_scope(q, line + 2);
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

int
tiku_trk_query_save(const tiku_trk_query_t *q, const char *name,
                    struct tiku_trk_store *store)
{
    char buf[TIKU_TRK_QUERY_PRED_MAX * 2];
    char index[2048];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    if (serialise(q, buf, sizeof buf) < 0) {
        return -1;
    }
    if (tiku_trk_state_write(store, name, TIKU_TRK_QUERY_ATTR, buf,
                             strlen(buf)) != 0) {
        return -1;
    }
    /* The index is what makes saved queries findable; without it a name has
     * to be remembered to be re-opened. */
    n = tiku_trk_state_read(store, TIKU_TRK_QUERY_STORE_NODE,
                            TIKU_TRK_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        n = 0;
    }
    index[n] = '\0';
    if (strstr(index, name) == NULL) {
        /* Newest first, so the list reads as a history. */
        char merged[2048];

        /* A name is bounded so the oldest entries fall off the end of the
         * index rather than the newest failing to be written. */
        snprintf(merged, sizeof merged, "%.127s\n%.1900s", name, index);
        (void)tiku_trk_state_write(store, TIKU_TRK_QUERY_STORE_NODE,
                                   TIKU_TRK_QUERY_INDEX_ATTR, merged,
                                   strlen(merged));
    }
    return 0;
}

int
tiku_trk_query_load(tiku_trk_query_t *q, const char *name,
                    struct tiku_trk_store *store)
{
    char buf[TIKU_TRK_QUERY_PRED_MAX * 2];
    int n;

    if (q == NULL || name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_trk_state_read(store, name, TIKU_TRK_QUERY_ATTR, buf,
                            sizeof buf - 1u);
    if (n < 0) {
        return -1;
    }
    buf[n] = '\0';
    return deserialise(q, buf);
}

int
tiku_trk_query_list(struct tiku_trk_store *store, char names[][128], int max)
{
    char index[2048];
    const char *p;
    int n, count = 0;

    if (store == NULL || names == NULL) {
        return 0;
    }
    n = tiku_trk_state_read(store, TIKU_TRK_QUERY_STORE_NODE,
                            TIKU_TRK_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return 0;
    }
    index[n] = '\0';
    for (p = index; *p != '\0' && count < max; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u) {
            if (len > 127u) {
                len = 127u;
            }
            memcpy(names[count], p, len);
            names[count][len] = '\0';
            count++;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return count;
}

int
tiku_trk_query_forget(const char *name, struct tiku_trk_store *store)
{
    char index[2048], merged[2048];
    const char *p;
    size_t o = 0;
    int n;

    if (name == NULL || store == NULL) {
        return -1;
    }
    n = tiku_trk_state_read(store, TIKU_TRK_QUERY_STORE_NODE,
                            TIKU_TRK_QUERY_INDEX_ATTR, index,
                            sizeof index - 1u);
    if (n < 0) {
        return -1;
    }
    index[n] = '\0';
    merged[0] = '\0';
    for (p = index; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        if (len > 0u && (len != strlen(name) ||
                         strncmp(p, name, len) != 0)) {
            o += (size_t)snprintf(merged + o,
                                  (o < sizeof merged) ? sizeof merged - o : 0u,
                                  "%.*s\n", (int)len, p);
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return tiku_trk_state_write(store, TIKU_TRK_QUERY_STORE_NODE,
                                TIKU_TRK_QUERY_INDEX_ATTR, merged,
                                strlen(merged));
}
