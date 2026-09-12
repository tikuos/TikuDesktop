/* Device utilities use fake backends: no ports, hardware writes or discovery. */
#include <assert.h>
#include "../src/shell/desktop/tiku_trk_desktop_utilities.c"

int desktop_board_names(desktop_t *app, char *out, size_t n)
{ (void)app; return snprintf(out, n, "alpha\nbeta\n"); }
static char name[64] = "alpha";
static int writes, readonly, failure, mismatch, fixed;
static int stat_mock(tiku_backend_t *b, const char *path, tiku_model_t *m)
{
    (void)b; memset(m, 0, sizeof *m);
    if (failure) return -1;
    m->kind = !strcmp(path, "/sys") || !strcmp(path, "/proc") ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    m->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE); return 0;
}
static int read_mock(tiku_backend_t *b, const char *path, void *out, size_t n)
{
    (void)b; if (failure) return -1;
    const char *s = "42";
    if (!strcmp(path, "/sys/device/name")) s = name;
    if (!strcmp(path, "/sys/cpu/freq_change_mode")) s = fixed ? "fixed" : "live";
    if (!strcmp(path, "/sys/cpu/freq_available")) s = "64000000 128000000";
    if (!strcmp(path, "/sys/cpu/freq_target")) s = "128000000";
    if (!strcmp(path, "/sys/mem/nvm_map")) s = "kind\tRRAM\t2097152\nstore\t65536\t1024\n";
    return snprintf(out, n, "%s\n", s);
}
static int write_mock(tiku_backend_t *b, const char *path, const void *in, size_t n, char *err, size_t cap)
{
    (void)b; (void)err; (void)cap; writes++;
    if (!mismatch && !strcmp(path, "/sys/device/name")) snprintf(name, sizeof name, "%.*s", (int)n, (const char *)in);
    return 0;
}
static int list_mock(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    (void)b; tiku_model_t m = {0};
    m.kind = !strcmp(path, "/proc") ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
    m.facts.perm = TIKU_P_READ;
    snprintf(m.name, sizeof m.name, "%s", !strcmp(path, "/proc") ? "0" : "uptime");
    fn(&m, ctx); return 1;
}
static atomic_int blocked;
/** A read the board never answers, released by the test that posted it. */
static void block_run(void *ctx)
{ (void)ctx; while (atomic_load(&blocked)) usleep(10000); }
static void block_free(void *ctx)
{
    utility_job_t *j = ctx; struct desktop_utility *p = j->owner;
    if (j->posted) utility_jobs--;
    if (--p->refs == 0) free(p);
    free(j);
}
static utility_job_t *job(tiku_backend_t *b, const char *path, int write)
{
    utility_job_t *j = calloc(1, sizeof *j); assert(j);
    j->backend = b; j->mode = 1; j->write = write;
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->before, sizeof j->before, "alpha");
    snprintf(j->input, sizeof j->input, "beta"); return j;
}
int main(void)
{
    atomic_store(&blocked, 1);
    const tiku_backend_ops_t ops = {.stat=stat_mock, .read=read_mock, .write=write_mock, .list=list_mock};
    tiku_backend_t b = {.ops=&ops}; double n;
    assert(utility_number("0\n", &n) && n == 0);
    assert(!utility_number("1oops", &n) && !utility_number("-1", &n));
    assert(!utility_number("18446744073709551616", &n));
    assert(utility_path("/sys/cpu/freq") && utility_path("/"));
    assert(!utility_path("/sys/../data") && !utility_path("/sys//cpu") && !utility_path("/sys/./cpu"));
    utility_metrics_t m = {0}; utility_store("store\t100\n", &m); assert(!m.valid);
    utility_store("store\t100\t0\n", &m); assert(m.valid == 32 && m.store_used == 0);
    m.valid = 0; utility_store("store\t100\t101\n", &m); assert(!m.valid);
    utility_job_t *j = job(&b, "/sys", 0); utility_run(j);
    assert(j->ok && j->count == 1 && writes == 0); free(j);
    j = job(&b, "/sys/device/name", 1); utility_run(j);
    assert(writes == 1 && j->ok && !strcmp(j->value, "beta")); free(j);
    j = job(&b, "/sys/device/name", 1); utility_run(j);
    assert(writes == 1 && strstr(j->status, "changed since review")); free(j);
    snprintf(name, sizeof name, "alpha"); readonly = 1;
    j = job(&b, "/sys/device/name", 1); utility_run(j); assert(writes == 1); free(j); readonly = 0;
    j = job(&b, "/sys/reboot", 1); utility_run(j); assert(writes == 1); free(j);
    j = job(&b, "/sys/device/name", 1); strcpy(j->input, "bad\nname"); utility_run(j); assert(writes == 1); free(j);
    j = job(&b, "/sys/cpu/freq_target", 1); strcpy(j->input, "1000000"); utility_run(j); assert(writes == 1);
    strcpy(j->input, "64000000"); fixed = 1; utility_run(j); assert(writes == 1); fixed = 0; free(j);
    mismatch = 1; j = job(&b, "/sys/device/name", 1); utility_run(j);
    assert(writes == 2 && strstr(j->status, "different value")); free(j); mismatch = 0;
    j = job(&b, "/sys", 0); j->mode = 0; utility_run(j);
    assert(j->metrics.valid == 127 && j->metrics.store_used == 1024 && j->metrics.nprocess == 1);
    utility_metrics_t snapshot = j->metrics; free(j);
    failure = 1; j = job(&b, "/sys", 0); j->mode = 0; utility_run(j);
    assert(!j->metrics.valid); free(j); failure = 0;
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    tiku_surface_t *s = tiku_surface_new(1000, 780, TIKU_RGB(51,102,152));
    app->desk = tiku_workspace_new(s);
    desktop_utility_open(app, 0); assert(app->utility[0]);
    app->utility[0]->metrics = snapshot;
    snprintf(app->utility[0]->target, 64, "fake-board");
    tiku_workspace_draw(app->desk); assert(!tiku_surface_png(s, "/tmp/tiku-monitor.png"));
    desktop_utility_open(app, 1); assert(app->utility[1]);
    struct desktop_utility *p = app->utility[1]; p->writable = 1;
    strcpy(p->path, "/sys/device/name"); strcpy(p->value, "alpha");
    tiku_field_start(&p->address, p->path); tiku_field_start(&p->edit, "beta");
    utility_action(p, U_REVIEW); assert(p->armed && writes == 2);
    tiku_workspace_draw(app->desk); assert(!tiku_surface_png(s, "/tmp/tiku-vfs.png"));
    utility_action(p, U_CANCEL); assert(!p->armed && writes == 2);
    tiku_field_start(&p->address, "/sys/reboot");
    utility_action(p, U_REVIEW); assert(!p->armed && writes == 2);
    tiku_field_start(&p->address, p->path);
    tiku_font_set_size(16); tiku_workspace_draw(app->desk);
    assert(!tiku_surface_png(s, "/tmp/tiku-vfs-large.png"));
    tiku_font_set_size(12);
    struct desktop_utility *monitor = app->utility[0];
    monitor->paused = 1; monitor->sampled = 123;
    j = job(&b, "/sys", 0); j->mode = 0; j->owner = monitor;
    j->metrics.ram = 999; utility_done(j);
    assert(monitor->metrics.ram != 999 && monitor->sampled == 123); free(j);
    j = job(&b, "/sys", 0); j->owner = p; p->refs++; p->busy = 1;
    j->cache = calloc(1, sizeof *j->cache); assert(j->cache);
    p->job = j;
    desktop_utility_close(app, p->window); assert(!app->utility[1]);
    assert(atomic_load(&j->cancelled));
    utility_done(j); utility_free(j);
    j = job(&b, "/sys/device/name", 1);
    j->owner = monitor; monitor->refs++; monitor->job = j;
    desktop_utility_close(app, monitor->window);
    assert(atomic_load(&j->cancelled));
    tiku_work_job_t work = {.run=utility_run, .done=utility_done, .free=utility_free, .ctx=j};
    j->posted = 1; utility_jobs++;
    assert(tiku_work_post(&work) == 0);
    desktop_utilities_destroy(app);
    assert(utility_jobs == 0 && writes == 2);
    /* A board that stopped answering mid-read must not hold the way out:
     * the wait gives up after five seconds and the loop goes on. */
    utility_job_t *held = calloc(1, sizeof *held); assert(held);
    held->owner = monitor; monitor->refs++;
    tiku_work_job_t block = {.run=block_run, .done=NULL, .free=block_free,
                             .ctx=held};
    held->posted = 1; utility_jobs++;
    assert(tiku_work_post(&block) == 0);
    int64_t began = tiku_trk_now();
    desktop_utilities_destroy(app);
    int64_t took = (tiku_trk_now() - began) / 1000;
    assert(took >= 4500 && took < 9000 && utility_jobs == 1);
    atomic_store(&blocked, 0);
    while (utility_jobs) { (void)tiku_work_collect(); usleep(10000); }
    tiku_workspace_free(app->desk); tiku_surface_free(s); free(app);
    puts("PASS: telemetry, missing data, VFS paths, permissions, edit confirmation, readback, cancellation and close lifetime");
    return 0;
}
