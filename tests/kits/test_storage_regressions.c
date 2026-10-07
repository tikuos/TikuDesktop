/*
 * TikuOS kits -- storage regression tests.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_storage_regressions.c - query cleanup, scope identity and parent paths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_query.h"
#include "tiku_state.h"

static int fails;

/** @brief Record a failed contract check. */
static void
check(int ok, const char *id, const char *what)
{
    if (!ok) {
        printf("FAIL %s: %s\n", id, what);
        fails++;
    }
}

/** @brief A directory whose mounted identity is supplied by the test. */
static int
directory_stat(tiku_backend_t *b, const char *path, tiku_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->facts.dev = *(const unsigned *)b->impl;
    m->kind = TIKU_KIND_DIRECTORY;
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->name, sizeof m->name, "volume");
    return 0;
}

/** @brief Every directory contains one matching file. */
static int
directory_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    tiku_model_t m;

    memset(&m, 0, sizeof m);
    m.facts.dev = *(const unsigned *)b->impl;
    m.kind = TIKU_KIND_FILE;
    snprintf(m.path, sizeof m.path, "%s/file", path);
    snprintf(m.name, sizeof m.name, "file");
    return fn(&m, ctx);
}

/** @brief Cleared scopes bind anew; stopping an unfinished refresh frees it. */
static void
query_lifecycle(void)
{
    static const tiku_backend_ops_t ops = {
        .stat = directory_stat, .list = directory_list
    };
    unsigned identity = 41;
    tiku_backend_t backend = {&ops, "", &identity};
    tiku_query_t q;
    int round;

    tiku_query_init(&q);
    (void)tiku_query_add_scope(&q, "/old");
    tiku_query_bind_scopes(&q, &backend);
    check(q.scope_was[0].dev == 41, "D-145", "initial scope is bound");
    tiku_query_clear_scope(&q);
    identity = 99;
    (void)tiku_query_add_scope(&q, "/new");
    tiku_query_bind_scopes(&q, &backend);
    check(q.scope_was[0].dev == 99 &&
          !strcmp(q.scope_was[0].path, "/new/"), "D-145",
          "clearing a scope removes the old volume identity");
    tiku_query_init(&q);
    for (round = 0; round < 3; round++) {
        tiku_qrun_t *run = tiku_query_start(&backend, &q);

        while (run && !tiku_query_done(run))
            (void)tiku_query_step(run, 4);
        check(run && tiku_query_count(run) == 1, "D-140",
              "query starts with a nonempty result snapshot");
        check(tiku_query_refresh_begin(run) == 0, "D-140", "refresh starts");
        if (round > 0)
            (void)tiku_query_step(run, 1);
        if (round == 2)
            check(tiku_query_refresh_end(run, NULL) == 0, "D-140",
                  "a completed refresh can also be stopped");
        tiku_query_stop(run);
    }
}

/** @brief Parent creation distinguishes directories from failures and files. */
static void
parent_paths(void)
{
    char root[] = "/tmp/tiku-parents.XXXXXX";
    char path[1200], child[1200], longpath[1200];
    struct stat st;
    FILE *f;

    if (!mkdtemp(root))
        exit(2);
    snprintf(path, sizeof path, "%s/one/two/file", root);
    check(tiku_state_mkparents(path) == 0 && tiku_state_mkparents(path) == 0,
          "D-146", "missing parents are created and existing ones accepted");
    snprintf(child, sizeof child, "%s/one/two", root);
    check(stat(child, &st) == 0 && S_ISDIR(st.st_mode), "D-146",
          "successful creation produces a directory");
    rmdir(child);
    snprintf(child, sizeof child, "%s/one", root);
    rmdir(child);
    snprintf(child, sizeof child, "%s/block", root);
    f = fopen(child, "wb");
    if (!f)
        exit(2);
    fclose(f);
    snprintf(path, sizeof path, "%s/block/file", root);
    check(tiku_state_mkparents(path) == -1, "D-146",
          "a file at the final parent is not a directory");
    snprintf(path, sizeof path, "%s/block/next/file", root);
    check(tiku_state_mkparents(path) == -1, "D-146",
          "a file at an intermediate component is an error");
    memset(longpath, 'x', sizeof longpath - 1);
    longpath[sizeof longpath - 1] = '\0';
    check(tiku_state_mkparents(longpath) == -1, "D-146",
          "overlong paths are refused without truncation");
    check(tiku_state_mkparents(NULL) == -1 &&
          tiku_state_mkparents("") == -1 &&
          tiku_state_mkparents("file") == 0, "D-146", "argument boundaries");
    unlink(child);
    rmdir(root);
}

/** @brief Run storage checks; LeakSanitizer also checks abandoned snapshots. */
int
main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    query_lifecycle();
    parent_paths();
    printf("%s: storage regressions\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
