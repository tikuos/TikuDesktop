/* Registry hierarchy, preserved siblings, conservative reads and native input. */
#include <assert.h>
#include "../../apps/deviceutils/tiku_device_utilities.c"

int desktop_board_names(utility_app_t *app, char *out, size_t n)
{ (void)app; return snprintf(out, n, "test-board\n"); }

static int find_branch(struct device_utility *p, const char *path)
{
    for (int i = 0; i < U_ROWS; i++) if (!strcmp(p->tree[i].path, path)) return i;
    return -1;
}

static void listing(struct device_utility *p, const char *path,
                    const char *const *names, int count)
{
    utility_job_t *j = calloc(1, sizeof *j); assert(j);
    j->owner = p; j->ok = j->mode = j->directory = 1; j->count = count;
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(p->path, sizeof p->path, "%s", path);
    tiku_field_start(&p->address, path);
    for (int i = 0; i < count; i++) {
        const char *name = names[i];
        j->entries[i].directory = name[0] == '/';
        snprintf(j->entries[i].name, sizeof j->entries[i].name, "%s", name + (name[0] == '/'));
        j->entries[i].readable = 1;
        j->entries[i].writable = !strcmp(name, "freq_target");
    }
    utility_done(j); free(j);
}

static int preload_reads, preload_lists, preload_fail, preload_wide;
static utility_job_t *cancel_during_read;
static int preload_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    (void)b; preload_lists++;
    if (preload_fail && !strcmp(path, "/sys/cpu")) return -1;
    const char *root[] = {"/sys", "/dev", "/data"};
    const char *sys[] = {"/cpu", "uptime", "time", "reboot", "unknown"};
    const char *cpu[] = {"freq", "freq_available", "freq_target"};
    const char *other[] = {"value"};
    const char **names = !strcmp(path, "/") ? root : !strcmp(path, "/sys") ? sys :
        !strcmp(path, "/sys/cpu") ? cpu : other;
    int count = names == root || names == cpu ? 3 : names == sys ? 5 : 1;
    if (preload_wide && !strcmp(path, "/")) count = U_ROWS + 1;
    for (int i = 0; i < count; i++) {
        tiku_model_t m = {0};
        if (preload_wide && !strcmp(path, "/")) {
            snprintf(m.name, sizeof m.name, "branch%d", i); m.kind = TIKU_KIND_DIRECTORY;
        } else {
            m.kind = names[i][0] == '/' ? TIKU_KIND_DIRECTORY : TIKU_KIND_FILE;
            snprintf(m.name, sizeof m.name, "%s", names[i] + (names[i][0] == '/'));
        }
        m.facts.perm = !strcmp(m.name, "time") || !strcmp(m.name, "reboot") ? TIKU_P_WRITE : TIKU_P_READ;
        if (fn(&m, ctx)) break;
    }
    return count;
}

static int preload_read(tiku_backend_t *b, const char *path, void *out, size_t cap)
{
    (void)b; preload_reads++;
    assert(utility_autoread(path) && strcmp(path, "/sys/time"));
    if (cancel_during_read) atomic_store(&cancel_during_read->cancelled, 1);
    if (!strcmp(path, "/sys/cpu/freq_available")) return -1;
    return snprintf(out, cap, "64000000\n");
}

static void test_preload(struct device_utility *p)
{
    const tiku_backend_ops_t ops = {.list=preload_list, .read=preload_read};
    tiku_backend_t b = {.ops=&ops};
    utility_job_t *j = calloc(1, sizeof *j); assert(j);
    j->backend = &b; j->mode = j->preload = 1; j->owner = p;
    utility_tree_reset(p); strcpy(p->path, "/");
    utility_run(j);
    assert(preload_lists == 5 && preload_reads == 4 && !j->limited);
    utility_done(j);
    assert(!j->cache && p->cache && !strcmp(p->path, "/sys"));
    assert(find_branch(p, "/sys/cpu") >= 0 && find_branch(p, "/data") >= 0);
    assert(p->entries[0].sampled == 1 && !p->entries[1].sampled);
    assert(utility_cache_show(p, "/sys/cpu"));
    assert(p->count == 3 && p->entries[0].sampled == 1 && p->entries[1].sampled == -1);
    assert(!p->writable && !p->value[0] && preload_reads == 4 && preload_lists == 5);
    assert(utility_cache_show(p, "/dev") && !p->entries[0].sampled);
    assert(!utility_autoread("/sys/vfs/events") && !utility_autoread("/sys/reboot"));
    assert(utility_autoread("/proc/12/events") && !utility_autoread("/proc/12/kill"));
    utility_action(p, U_REFRESH);
    assert(!p->cache && p->preload && !p->count && !p->writable);
    preload_fail = 1; utility_run(j); utility_done(j);
    assert(strstr(p->status, "Partial") && !utility_cache_show(p, "/sys/cpu"));
    preload_fail = 0; utility_tree_reset(p);
    cancel_during_read = j; int before = preload_reads;
    utility_run(j); assert(preload_reads == before + 1);
    utility_done(j); assert(!p->cache);
    cancel_during_read = NULL; utility_cache_free(j->cache); j->cache = NULL;
    atomic_store(&j->cancelled, 0); preload_wide = 1;
    utility_run(j); assert(j->limited && j->cache->truncated);
    int count = 0; for (utility_snapshot_t *s = j->cache; s; s = s->next) count++;
    assert(count == U_ROWS);
    utility_cache_free(j->cache); free(j); preload_wide = 0;
}

int main(void)
{
    utility_app_t *app = calloc(1, sizeof *app); assert(app);
    app->surf = tiku_surface_new(1000, 740, TIKU_RGB(51,102,152));
    app->desk = tiku_workspace_new(app->surf);
    strcpy(app->current_device, "test-board");
    device_utility_open(app, U_INSPECTOR);
    struct device_utility *p = app->utility[U_INSPECTOR]; assert(p);
    assert(!strcmp(p->target, "test-board") && p->preload);
    strcpy(p->target, "test-board"); strcpy(app->current_device, "test-board");
    const char *root[] = {"/sys", "/proc", "/data"};
    listing(p, "/", root, 3);
    assert(p->tree_count == 4 && p->count == 0);
    assert(!strcmp(p->tree[p->tree_rows[1]].path, "/data"));
    const char *sys[] = {"/cpu", "/mem", "/device", "uptime", "time"};
    listing(p, "/sys", sys, 5);
    assert(p->tree_count == 7 && p->count == 2);
    assert(!strcmp(p->entries[0].name, "uptime"));
    const char *cpu[] = {"freq", "freq_available", "freq_target", "freq_change_mode"};
    listing(p, "/sys/cpu", cpu, 4);
    assert(p->count == 4 && !strcmp(p->folder, "/sys/cpu"));
    utility_job_t *j = calloc(1, sizeof *j); assert(j);
    j->owner = p; j->ok = j->mode = j->readable = j->writable = j->found = 1;
    strcpy(j->path, "/sys/cpu/freq_target"); strcpy(j->value, "128000000");
    strcpy(p->path, j->path); tiku_field_start(&p->address, j->path);
    utility_done(j); free(j);
    assert(p->count == 4 && p->entries[2].sampled == 1);
    assert(!strcmp(p->entries[2].data, "128000000") && !p->entries[0].sampled);
    assert(p->tree_count == 7 && p->writable);
    tiku_field_start(&p->edit, "64000000");
    strcpy(p->status, "Test fixture: selected value read; siblings remain unqueried.");
    tiku_workspace_draw(app->desk);
    assert(!tiku_surface_png(app->surf, "/tmp/tiku-registry.png"));
    tiku_font_set_size(16); tiku_workspace_draw(app->desk);
    assert(!tiku_surface_png(app->surf, "/tmp/tiku-registry-large.png"));
    tiku_font_set_size(12);
    /* A write that never reached the device leaves the row it was
     * offered from exactly as the read left it. */
    j = calloc(1, sizeof *j); assert(j);
    j->owner = p; j->mode = j->write = j->writable = 1;
    strcpy(j->path, "/sys/cpu/freq_target");
    snprintf(j->status, sizeof j->status, "Not saved: value changed since review.");
    utility_done(j); free(j);
    assert(p->entries[2].sampled == 1 && !p->entries[0].sampled);
    assert(!strcmp(p->entries[2].data, "128000000"));
    assert(!p->writable && !p->armed && strstr(p->status, "Not saved"));
    /* A listing cut at 128 must say so wherever its rows came from: a
     * cached one looks exactly like a directory that holds 128. */
    char said[4096];
    p->truncated = 0; tiku_workspace_draw(app->desk);
    (void)tiku_workspace_narrate(app->desk, p->window, said, sizeof said);
    assert(!strstr(said, "first 128 entries"));
    p->truncated = 1; tiku_workspace_draw(app->desk);
    (void)tiku_workspace_narrate(app->desk, p->window, said, sizeof said);
    assert(strstr(said, "first 128 entries"));
    p->truncated = 0;
    int cpu_node = find_branch(p, "/sys/cpu");
    int sys_node = find_branch(p, "/sys");
    p->tree_selected = cpu_node; utility_tree_rows(p);
    tiku_rect_t tree = registry_tree_rect(p);
    int row = p->tree_list.cursor;
    tiku_rect_t r = tiku_list_row_rect(&p->tree_list, tree, row);
    tiku_event_t ev = {.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=r.x+32, .y=r.y+r.h/2};
    utility_event(p->window, &ev, p);
    assert(!p->tree[cpu_node].expanded && p->tree_count == 7);
    assert(find_branch(p, "/sys/cpu") == cpu_node);
    ev.type = TIKU_EVENT_POINTER_UP; utility_event(p->window, &ev, p);
    p->tree[sys_node].expanded = 0; utility_tree_rows(p);
    assert(p->tree_count == 4);
    p->tree[sys_node].expanded = 1; utility_tree_rows(p);
    assert(p->tree_count == 7);
    utility_controls(p); tiku_dialog_focus(&p->dialog, U_LIST);
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_DOWN};
    utility_event(p->window, &ev, p);
    assert(!p->writable && !p->armed && !p->value[0]);
    assert(!strcmp(p->path, "/sys/cpu/freq_change_mode"));
    const char *changed[] = {"/device", "uptime"};
    listing(p, "/sys", changed, 2);
    assert(find_branch(p, "/sys/cpu") == -1 && find_branch(p, "/sys/mem") == -1);
    /* The snapshot goes with the branch: a directory the device has just
     * said is gone must not be reachable again from the cache. */
    assert(!utility_cache_find(p->cache, "/sys/cpu"));
    assert(!utility_cache_show(p, "/sys/cpu"));
    assert(find_branch(p, "/proc") >= 0 && p->count == 1);
    assert(utility_tree_ensure(p, "/new/deep/") >= 0);
    assert(find_branch(p, "/new/deep") >= 0 && find_branch(p, "/new/deep/") < 0);
    assert(utility_tree_ensure(p, "/sys/../bad") < 0);
    /* Enter reads the row the arrows moved to rather than being
     * swallowed by the panel that took the key. */
    utility_controls(p); assert(tiku_dialog_focus(&p->dialog, U_LIST));
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_RETURN};
    utility_event(p->window, &ev, p);
    assert(!strcmp(p->path, "/sys/uptime"));
    assert(!p->writable && strstr(p->status, "No connected target"));
    assert(p->tree_count == 1 && !p->count);
    /* Tab carries a tab character; the field it lands in refuses one. */
    strcpy(p->status, "fixture");
    utility_controls(p); assert(tiku_dialog_focus(&p->dialog, U_UP));
    ev = (tiku_event_t){.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_TAB};
    ev.text[0] = '\t';
    utility_event(p->window, &ev, p);
    assert(p->dialog.focus == U_PATH && !strcmp(p->status, "fixture"));
    /* A read greys every control; the ring waits where the hand is. */
    p->busy = 1; utility_controls(p);
    assert(p->dialog.focus == U_PATH);
    p->busy = 0; utility_controls(p);
    assert(p->dialog.focus == U_PATH);
    /* A path the device does not have leaves no branch behind: a failed
     * stat says nothing about what directories exist. */
    utility_tree_reset(p);
    j = calloc(1, sizeof *j); assert(j);
    j->owner = p; j->mode = 1;
    strcpy(j->path, "/nope/deep");
    snprintf(j->status, sizeof j->status, "Path unavailable or device disconnected.");
    utility_done(j); free(j);
    assert(find_branch(p, "/nope") == -1 && p->tree_count == 1);
    /* A partial listing must not prune branches omitted by its limit. */
    listing(p, "/", root, 3);
    j = calloc(1, sizeof *j); assert(j);
    strcpy(j->path, "/"); j->directory = j->ok = j->truncated = 1;
    utility_registry_result(p, j);
    assert(find_branch(p, "/sys") >= 0 && find_branch(p, "/proc") >= 0);
    free(j);
    utility_tree_reset(p);
    for (int i = 0; i < U_ROWS - 1; i++) {
        char path[64]; snprintf(path, sizeof path, "/branch%03d", i);
        assert(utility_tree_ensure(p, path) >= 0);
    }
    assert(utility_tree_ensure(p, "/overflow") < 0 && p->tree_full);
    utility_tree_rows(p); assert(p->tree_count == U_ROWS);
    /* A full tree is what one path met, not a verdict the next value
     * read inherits and reports in place of its own. */
    j = calloc(1, sizeof *j); assert(j);
    j->owner = p; j->ok = j->mode = j->readable = j->found = 1;
    strcpy(j->path, "/branch000"); strcpy(j->value, "7");
    snprintf(j->status, sizeof j->status, "Value read from device.");
    utility_done(j); free(j);
    assert(!strstr(p->status, "Tree cache full") && !p->tree_full);
    /* A complete refresh frees missing branches, making room again. */
    listing(p, "/", root, 3);
    assert(p->tree_count == 4 && find_branch(p, "/sys") >= 0 && !p->tree_full);
    test_preload(p);
    device_utilities_destroy(app);
    tiku_workspace_free(app->desk); tiku_surface_free(app->surf); free(app);
    puts("PASS: automatic safe snapshots, limits, cancellation, cached navigation, registry tree and disconnect");
    return 0;
}
