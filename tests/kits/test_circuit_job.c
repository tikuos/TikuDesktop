/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_job.c - Gerber jobs: the JSON read strictly, entries that
 * cannot be read or decoded kept as unavailable layers, a failure of the
 * whole job named by its entry, every read released once and only once,
 * cancellation and limits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_stack.h"
#include "tiku_circuit_job_private.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVE_MAX 8                  /* reads the resolver keeps open     */
#define MIB      (1024u * 1024u)

/** @brief The test resolver's files, and the copies it has handed out. */
typedef struct {
    char *job;                      /* the root, whatever it is called   */
    char *art;                      /* artwork.gbr                       */
    char *drill;                    /* drills.drl                        */
    size_t big;                     /* bytes big.gbr and more.gbr claim  */
    int reads, releases;            /* calls of each                     */
    void *live[LIVE_MAX];           /* copies read and not yet released  */
} sources_t;

/** @brief The whole of the file at @p path, NUL-terminated. */
static char *
fixture(const char *path)
{
    FILE *f = fopen(path, "rb");

    assert(f && !fseek(f, 0, SEEK_END));
    long n = ftell(f);

    assert(n > 0);
    rewind(f);
    char *s = malloc((size_t)n + 1);

    assert(s && fread(s, 1, (size_t)n, f) == (size_t)n && !fclose(f));
    s[n] = 0;
    return s;
}

/** @brief The resolver's read: a malloc'd copy of the named file, kept
 *         in @c live until released.  A big.gbr or more.gbr claims
 *         @c big bytes but holds a short text.  Fails without touching
 *         @p out. */
static tiku_circuit_err_t
read_source(void *ctx, const char *parent, const char *ref,
            tiku_circuit_source_t *out, tiku_circuit_diagnostic_t *diag)
{
    sources_t *s = ctx;
    int big = parent && (!strcmp(ref, "big.gbr") || !strcmp(ref, "more.gbr"));
    const char *text = !parent ? s->job
                     : big ? "G04 claims more than it holds*"
                     : !strcmp(ref, "artwork.gbr") ? s->art
                     : !strcmp(ref, "drills.drl") ? s->drill : NULL;
    size_t slot = 0;

    (void)diag;
    s->reads++;
    if (!text) {
        return TIKU_CIRCUIT_MISSING;
    }
    while (slot < LIVE_MAX && s->live[slot]) {
        slot++;
    }
    assert(slot < LIVE_MAX);
    s->live[slot] = malloc(strlen(text) + 1);
    assert(s->live[slot]);
    strcpy(s->live[slot], text);
    out->data = s->live[slot];
    out->bytes = big ? s->big : strlen(text);
    snprintf(out->path, sizeof out->path, "%s", ref);
    snprintf(out->key, sizeof out->key, "%s", ref);
    return TIKU_CIRCUIT_OK;
}

/** @brief The resolver's release: frees a copy it handed out, and fails
 *         the test on any other pointer, a copy released twice among
 *         them.  It leaves @p out as it was. */
static void
release_source(void *ctx, tiku_circuit_source_t *out)
{
    sources_t *s = ctx;

    s->releases++;
    if (out->data) {
        size_t slot = 0;

        while (slot < LIVE_MAX && s->live[slot] != out->data) {
            slot++;
        }
        assert(slot < LIVE_MAX);
        free(s->live[slot]);
        s->live[slot] = NULL;
    }
}

/** @brief Whether every copy the resolver handed out was released. */
static int
all_released(const sources_t *s)
{
    for (size_t i = 0; i < LIVE_MAX; i++) {
        if (s->live[i]) {
            return 0;
        }
    }
    return s->reads == s->releases;
}

/** @brief A cancellation callback that fires on its @p ctx'th call. */
static int
cancel(void *ctx)
{
    return --*(int *)ctx <= 0;
}

/** @brief @p text refused by the job parser with @p code. */
static void
bad(const char *text, tiku_circuit_err_t code)
{
    circuit_job_list_t *list = malloc(sizeof *list);
    tiku_circuit_diagnostic_t diag;

    assert(list);
    tiku_circuit_err_t e = circuit_job_parse(text, strlen(text), NULL, list,
                                             &diag);

    if (e != code) {
        fprintf(stderr, "job %d, not %d: %s\n", e, code, text);
    }
    assert(e == code && diag.code == code && diag.message[0]);
    free(list);
}

/** @brief @p text accepted by the job parser, with its first Path in
 *         @p path. */
static void
good(const char *text, const char *path)
{
    circuit_job_list_t *list = malloc(sizeof *list);

    assert(list);
    assert(!circuit_job_parse(text, strlen(text), NULL, list, NULL));
    assert(list->count && !strcmp(list->entries[0].path, path));
    free(list);
}

/** @brief Each defect in an otherwise valid job, refused with its code;
 *         the valid job itself is read. */
static void
strict_json(void)
{
    static const struct {
        const char *text;
        tiku_circuit_err_t code;
    } cases[] = {
        {"{}", TIKU_CIRCUIT_BAD},
        {"[]", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":{}}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{}]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":1}]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\",\"Path\":\"y\"}]}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],"
         "\"FilesAttributes\":[{\"Path\":\"y\"}]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\\u0000y\"}]}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\\ud800\"}]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\\ny\"}]}", TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":01}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":1.}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":1e}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":NaN}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":true,}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":[1,]}",
         TIKU_CIRCUIT_BAD},
        {"{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":\"\xc0\x80\"}",
         TIKU_CIRCUIT_BAD},
    };

    good("{\"FilesAttributes\":[{\"Path\":\"x\"}],\"a\":[1.5e3,true]}", "x");
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        bad(cases[i].text, cases[i].code);
    }
}

/** @brief A member name may run to the 4,095 bytes any string may, and
 *         no further. */
static void
key_limit(void)
{
    size_t cap = 2 * 4096 + 64;
    char *text = malloc(cap), *key = malloc(4097);

    assert(text && key);
    memset(key, 'k', 4096);
    key[4095] = 0;
    snprintf(text, cap, "{\"%s\":1,\"FilesAttributes\":[{\"Path\":\"x\"}]}",
             key);
    good(text, "x");
    key[4095] = 'k';
    key[4096] = 0;
    snprintf(text, cap, "{\"%s\":1,\"FilesAttributes\":[{\"Path\":\"x\"}]}",
             key);
    bad(text, TIKU_CIRCUIT_LIMIT);
    free(key);
    free(text);
}

/** @brief A file over its own limits is an unavailable layer; the job's
 *         read budget fails the job, naming the entry that spent it. */
static void
entry_limits(sources_t *s, const tiku_circuit_resolver_t *resolver)
{
    char *original = s->job;
    tiku_circuit_diagnostic_t diag;
    tiku_circuit_t *d = NULL;

    s->job = "{\"FilesAttributes\":[{\"Path\":\"big.gbr\"},"
             "{\"Path\":\"artwork.gbr\"}]}";
    s->big = 9 * MIB;
    assert(!tiku_circuit_job_open("job", NULL, resolver, &d, &diag));
    assert(tiku_circuit_layer_count(d) == 2);
    assert(tiku_circuit_stack_incomplete(d));
    assert(!tiku_circuit_layer(d, 0)->scene);
    assert(tiku_circuit_layer(d, 0)->diagnostic.code == TIKU_CIRCUIT_LIMIT);
    assert(tiku_circuit_layer(d, 1)->scene);
    tiku_circuit_release(d);
    assert(all_released(s));

    s->job = "{\"FilesAttributes\":[{\"Path\":\"big.gbr\"},"
             "{\"Path\":\"more.gbr\"}]}";
    s->big = 40 * MIB;
    d = (void *)s;
    assert(tiku_circuit_job_open("job", NULL, resolver, &d, &diag)
           == TIKU_CIRCUIT_LIMIT);
    assert(d == (void *)s && diag.code == TIKU_CIRCUIT_LIMIT);
    assert(!strcmp(diag.message,
                   "more.gbr: Job files exceed the 64 MiB read budget"));
    assert(all_released(s));
    s->job = original;
}

/** @brief Every group. */
int
main(void)
{
    sources_t s = {.job = fixture("tests/fixtures/circuit/stack.gbrjob"),
                   .art = fixture("tests/fixtures/circuit/artwork.gbr"),
                   .drill = fixture("tests/fixtures/circuit/drills.drl")};
    tiku_circuit_resolver_t resolver = {&s, read_source, release_source};
    tiku_circuit_t *d = NULL;

    /* The third entry is missing: its failed read is released with
     * nothing in it, not the copy read before it. */
    assert(!tiku_circuit_job_open("stack.gbrjob", NULL, &resolver, &d, NULL));
    assert(tiku_circuit_layer_count(d) == 3
           && tiku_circuit_stack_incomplete(d));
    assert(s.reads == 4 && all_released(&s));
    const tiku_circuit_layer_t *first = tiku_circuit_layer(d, 0);

    assert(first->scene && strstr(first->attributes, "NC")
           && !strcmp(first->root, "stack.gbrjob"));
    assert(!tiku_circuit_layer(d, 2)->scene
           && tiku_circuit_layer(d, 2)->diagnostic.code
              == TIKU_CIRCUIT_MISSING);
    tiku_circuit_release(d);
    char *original = s.job;

    s.job = "{\"FilesAttributes\":[{\"Path\":\"artwork.gbr\","
            "\"FileFormat\":\"XNC\"}]}";
    assert(!tiku_circuit_job_open("job", NULL, &resolver, &d, NULL)
           && tiku_circuit_stack_incomplete(d));
    assert(tiku_circuit_layer(d, 0)->diagnostic.code == TIKU_CIRCUIT_FORMAT);
    tiku_circuit_release(d);
    s.job = "{\"FilesAttributes\":[{\"Path\":\"artwork.gbr\","
            "\"FileFormat\":\"IPC356\"}]}";
    int before = s.reads;

    assert(!tiku_circuit_job_open("job", NULL, &resolver, &d, NULL));
    assert(s.reads == before + 1 && tiku_circuit_stack_incomplete(d));
    tiku_circuit_release(d);
    entry_limits(&s, &resolver);
    s.job = original;
    for (size_t n = 1; n < strlen(original) - 1; n++) {
        char *cut = malloc(n + 1);

        assert(cut);
        memcpy(cut, original, n);
        cut[n] = 0;
        s.job = cut;
        d = (void *)&s;
        assert(tiku_circuit_job_open("job", NULL, &resolver, &d, NULL)
               && d == (void *)&s);
        free(cut);
    }
    s.job = original;
    for (int n = 1; n < 30; n++) {
        int left = n;
        tiku_circuit_import_t options = {.cancel = cancel, .ctx = &left};

        d = NULL;
        assert(tiku_circuit_job_open("job", &options, &resolver, &d, NULL)
               == TIKU_CIRCUIT_CANCELLED && !d);
    }
    assert(all_released(&s));
    strict_json();
    key_limit();
    circuit_job_list_t *list = malloc(sizeof *list);

    assert(list);
    const char unicode[] = "{\"FilesAttributes\":[{\"Path\":"
                           "\"\\u677f-\\ud83d\\udd27.gbr\"}],"
                           "\"Extra\":[1.2e-3,true,false,null]}";

    assert(!circuit_job_parse(unicode, strlen(unicode), NULL, list, NULL));
    assert(!strcmp(list->entries[0].path, "板-🔧.gbr"));
    assert(circuit_job_parse("x", 1024u * 1024u + 1, NULL, list, NULL)
           == TIKU_CIRCUIT_LIMIT);
    char too_many[2048] = "{\"FilesAttributes\":[";

    for (int i = 0; i < 33; i++) {
        strcat(too_many, i ? ",{\"Path\":\"x\"}" : "{\"Path\":\"x\"}");
    }
    strcat(too_many, "]}");
    assert(circuit_job_parse(too_many, strlen(too_many), NULL, list, NULL)
           == TIKU_CIRCUIT_LIMIT);
    const char nul[] = "{\0\"FilesAttributes\":[]} ";

    assert(circuit_job_parse(nul, sizeof nul - 1, NULL, list, NULL)
           == TIKU_CIRCUIT_BAD);
    free(list);
    free(original);
    free(s.art);
    free(s.drill);
    puts("PASS: job metadata, UTF-8/escapes, strict JSON, failed-layer "
         "entries, resolver release, cancellation and limits");
    return 0;
}
