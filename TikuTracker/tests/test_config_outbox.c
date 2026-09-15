/* Real filesystem + deterministic device/IO failures. No board required.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIKU_CONFIG_OUTBOX_TEST_HOOKS
#include "../../kits/storage/tiku_config_outbox.c"
#include <assert.h>
#include <sys/wait.h>

static const char *fail_step;
int tiku_config_outbox_fail(const char *step)
{ return fail_step && !strcmp(fail_step, step); }
static char directory[128], uid[33] = "0102030405060708";
static char epoch[33] = "0102030405060708090a0b0c0d0e0f10";
static char history[768], sent[160];
/* The device session trims a reply's trailing line ends. */
static int trim_reply;
static unsigned revision;
static int writes, reads, enrolled, offline, lost_ack, reject_write, empty_ack, readonly;
static char desired[33] = "Board";
static int stat_node(tiku_backend_t *b, const char *p, tiku_model_t *m)
{
    (void)b; (void)p;
    memset(m, 0, sizeof *m);
    m->facts.perm = TIKU_P_READ | (readonly ? 0 : TIKU_P_WRITE);
    return offline ? -1 : 0;
}
static int read_node(tiku_backend_t *b, const char *p, void *out, size_t max)
{
    char hex[65];
    (void)b; reads++;
    if (offline) return -1;
    if (!strcmp(p, "/sys/device/uid")) return snprintf(out, max, "%s\n", uid);
    if (!strcmp(p, "/sys/config/status") || !strcmp(p, "/sys/config/provision"))
        return enrolled ? snprintf(out, max, "v1:ready:%s\n", epoch) :
                          snprintf(out, max, "v1:unenrolled:-\n");
    if (!strcmp(p, "/sys/config/history")) {
        int n = snprintf(out, max, "%s", history);
        char *text = out;
        while (trim_reply && n > 0 &&
               (text[n-1] == '\n' || text[n-1] == '\r')) text[--n] = 0;
        return n;
    }
    for (size_t i = 0; i < strlen(desired); i++)
        snprintf(hex+2*i, 3, "%02x", (unsigned char)desired[i]);
    return snprintf(out, max, "v1:%s:%08x:%s:%s\n", epoch, revision,
                    revision ? "applied" : "unset", revision ? hex : "");
}
static int write_node(tiku_backend_t *b, const char *p, const void *input, size_t n,
                      char *error, size_t max)
{
    char token[33], value[33];
    tiku_config_snapshot_t request;
    tiku_config_pending_t disk;
    int fd = ob_directory(directory, 0);
    (void)b;
    assert(fd >= 0 && ob_load(fd, uid, &disk) == 1); close(fd);
    assert(strlen(disk.request) == n && !memcmp(input, disk.request, n));
    writes++;
    snprintf(sent, sizeof sent, "%.*s", (int)n, (const char *)input);
    if (reject_write) { snprintf(error, max, "offline"); return -1; }
    if (empty_ack) return 0;
    if (!strcmp(p, "/sys/config/provision")) {
        assert(!enrolled && n == 32);
        snprintf(epoch, sizeof epoch, "%.32s", sent); enrolled = 1;
    } else {
        assert(!tiku_config_decode_request(sent, &request, token, value));
        assert(!strcmp(request.incarnation, epoch));
        if (request.revision != revision) { snprintf(error, max, "ECONFLICT"); return -1; }
        revision++;
        snprintf(desired, sizeof desired, "%s", value);
        snprintf(history, sizeof history, "%c:%08x:%s:applied:%.*s\n",
            strstr(p, "frequency") ? '2' : '1', revision, token,
            (int)(strlen(sent)-84), sent+75);
    }
    if (lost_ack) {
        if (lost_ack == 2) offline = 1;
        snprintf(error, max, "ack lost"); return -1;
    }
    return 0;
}
static const tiku_backend_ops_t ops = {.stat=stat_node, .read=read_node, .write=write_node};
static tiku_backend_t backend = {.ops=&ops};
static tiku_config_snapshot_t snapshot(void)
{
    tiku_config_snapshot_t s;
    assert(tiku_config_read(&backend, "/sys/device/name", &s) == 1);
    return s;
}
static void setup(void)
{
    snprintf(directory, sizeof directory, "/tmp/tiku-config-outbox-XXXXXX");
    assert(mkdtemp(directory));
    assert(chmod(directory, 0700) == 0);
    snprintf(uid, sizeof uid, "0102030405060708");
    snprintf(epoch, sizeof epoch, "0102030405060708090a0b0c0d0e0f10");
    snprintf(desired, sizeof desired, "Board");
    history[0] = sent[0] = backend.devid[0] = 0;
    writes = reads = offline = lost_ack = reject_write = empty_ack = readonly = 0;
    enrolled = 1; revision = 7; fail_step = NULL; trim_reply = 0;
}
static void cleanup(void)
{
    char path[192];
    /* Only this test's exact board slot under its unique temporary directory. */
    snprintf(path, sizeof path, "%s/0102030405060708.pending", directory);
    assert(unlink(path) == 0 || errno == ENOENT);
    assert(rmdir(directory) == 0);
}
static tiku_config_pending_t inspect(int state)
{
    tiku_config_pending_t p;
    int before = writes;
    int rc = tiku_config_outbox_inspect(directory, &backend, &p);
    assert(rc == (state == TIKU_CONFIG_PENDING_ERROR ? -1 : 0));
    if (p.state != state) fprintf(stderr, "expected state %d, got %d: %s\n", state, p.state, p.summary);
    assert(p.state == state && writes == before);
    return p;
}
static int save(char *error)
{
    tiku_config_snapshot_t s = snapshot();
    return tiku_config_outbox_save(directory, &backend, "/sys/device/name",
                                  &s, "Lab", 3, error, 256);
}
int main(void)
{
    char error[256], original[160], path[192];
    tiku_config_pending_t p, old;
    setup();
    snprintf(path, sizeof path, "%s/absent", directory);
    assert(!tiku_config_outbox_inspect(path, &backend, &p));
    assert(!p.state && !reads && !writes && access(path, F_OK) == -1);
    inspect(TIKU_CONFIG_PENDING_NONE);
    assert(!save(error) && writes == 1 && revision == 8);
    inspect(TIKU_CONFIG_PENDING_NONE);
    cleanup();

    setup();
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        fail_step = "before-send";
        _exit(save(error) == -1 && !writes ? 0 : 1);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    p = inspect(TIKU_CONFIG_PENDING_RETRY); /* Writer process is gone. */
    snprintf(original, sizeof original, "%s", p.request);
    assert(!tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error));
    assert(writes == 1 && !strcmp(original, sent));
    cleanup();

    const char *failures[] = {"file-sync", "publish", "directory-sync", "before-send"};
    for (size_t i = 0; i < sizeof failures / sizeof *failures; i++) {
        setup(); fail_step = failures[i];
        assert(save(error) == -1 && !writes);
        fail_step = NULL;
        if (i >= 2) {
            p = inspect(TIKU_CONFIG_PENDING_RETRY);
            snprintf(original, sizeof original, "%s", p.request);
            assert(save(error) == -1 && !writes); /* Never replace a pending slot. */
            fail_step = "retry-sync";
            assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
            fail_step = NULL;
            assert(!tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error));
            assert(writes == 1 && !strcmp(original, sent) && revision == 8);
        } else inspect(TIKU_CONFIG_PENDING_NONE);
        cleanup();
    }
    setup(); lost_ack = 1;
    assert(!save(error) && writes == 1 && revision == 8);
    inspect(TIKU_CONFIG_PENDING_NONE); cleanup();
    setup(); lost_ack = 2;
    assert(save(error) == -1 && writes == 1 && revision == 8);
    inspect(TIKU_CONFIG_PENDING_ERROR);
    offline = lost_ack = 0; /* Reconnect to the same board, no automatic write. */
    p = inspect(TIKU_CONFIG_PENDING_CONFIRMED);
    assert(!tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error));
    assert(writes == 1); cleanup();

    setup(); empty_ack = 1;
    assert(save(error) == -1 && revision == 7);
    p = inspect(TIKU_CONFIG_PENDING_RETRY); /* Empty success is not a receipt. */
    empty_ack = 0;
    assert(!tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error));
    assert(writes == 2 && revision == 8); cleanup();

    setup(); fail_step = "remove";
    assert(save(error) == -1 && writes == 1 && revision == 8);
    p = inspect(TIKU_CONFIG_PENDING_CONFIRMED);
    revision = 11; /* A newer setting doesn't erase proof of prior acceptance. */
    inspect(TIKU_CONFIG_PENDING_CONFIRMED);
    fail_step = NULL;
    assert(!tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error));
    assert(writes == 1); cleanup();

    setup(); fail_step = "before-send"; assert(save(error) == -1);
    p = inspect(TIKU_CONFIG_PENDING_RETRY); old = p;
    fail_step = NULL; revision++;
    inspect(TIKU_CONFIG_PENDING_UNKNOWN); /* Evicted/absent receipt is not proof. */
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
    snprintf(history, sizeof history, "1:00000008:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:applied:4c6162\n");
    inspect(TIKU_CONFIG_PENDING_CONFLICT);
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
    snprintf(history, sizeof history, "invalid\n");
    inspect(TIKU_CONFIG_PENDING_ERROR);
    history[0] = 0;
    epoch[0] = 'f';
    inspect(TIKU_CONFIG_PENDING_OBSOLETE);
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
    snprintf(uid, sizeof uid, "aabbccddeeff1122");
    inspect(TIKU_CONFIG_PENDING_NONE);
    assert(tiku_config_outbox_forget(directory, &backend, &p, error, sizeof error) == -1);
    snprintf(uid, sizeof uid, "0102030405060708");
    assert(!tiku_config_outbox_forget(directory, &backend, &p, error, sizeof error));
    fail_step = "before-send"; assert(save(error) == -1); fail_step = NULL;
    assert(tiku_config_outbox_forget(directory, &backend, &old, error, sizeof error) == -1);
    p = inspect(TIKU_CONFIG_PENDING_RETRY);
    snprintf(backend.devid, sizeof backend.devid, "uid-ffffffffffffffff");
    inspect(TIKU_CONFIG_PENDING_ERROR);
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1);
    backend.devid[0] = 0;
    assert(!tiku_config_outbox_forget(directory, &backend, &p, error, sizeof error));
    cleanup();

    setup(); enrolled = 0; revision = 0;
    readonly = 1;
    assert(tiku_config_outbox_enroll(directory, &backend, error, sizeof error) == -1 && !writes);
    inspect(TIKU_CONFIG_PENDING_NONE); readonly = 0;
    assert(!tiku_config_outbox_enroll(directory, &backend, error, sizeof error));
    assert(writes == 1 && enrolled && revision == 0 && !strcmp(desired, "Board"));
    inspect(TIKU_CONFIG_PENDING_NONE); cleanup();
    setup(); enrolled = 0; fail_step = "before-send";
    assert(tiku_config_outbox_enroll(directory, &backend, error, sizeof error) == -1 && !writes);
    fail_step = NULL; p = inspect(TIKU_CONFIG_PENDING_UNKNOWN);
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
    /* Even on virgin storage an old enrollment token is never replayed. */
    assert(!tiku_config_outbox_forget(directory, &backend, &p, error, sizeof error));
    assert(!tiku_config_outbox_enroll(directory, &backend, error, sizeof error));
    assert(strcmp(epoch, p.request)); cleanup();

    setup(); fail_step = "before-send"; assert(save(error) == -1); fail_step = NULL;
    p = inspect(TIKU_CONFIG_PENDING_RETRY);
    int fd = ob_directory(directory, 1);
    assert(fd >= 0);
    assert(tiku_config_outbox_retry(directory, &backend, &p, error, sizeof error) == -1 && !writes);
    close(fd);
    snprintf(path, sizeof path, "%s/%s.pending", directory, uid);
    fd = open(path, O_WRONLY); assert(fd >= 0 && write(fd, "X", 1) == 1); close(fd);
    inspect(TIKU_CONFIG_PENDING_ERROR);
    assert(save(error) == -1 && !writes);
    assert(tiku_config_outbox_forget(directory, &backend, &p, error, sizeof error) == -1);
    assert(!unlink(path) && !symlink("/dev/null", path));
    inspect(TIKU_CONFIG_PENDING_ERROR);
    assert(save(error) == -1 && !writes);
    cleanup();
    setup();
    assert(chmod(directory, 0755) == 0);
    inspect(TIKU_CONFIG_PENDING_ERROR);
    assert(save(error) == -1 && !writes);
    assert(!chmod(directory, 0700));
    tiku_config_snapshot_t s = snapshot();
    assert(tiku_config_outbox_save(directory, &backend, "/sys/cpu/freq_target",
        &s, "064000000", 9, error, sizeof error) == -1 && !writes);
    cleanup();

    /* A real board's reply arrives with its last line end eaten. */
    setup();
    trim_reply = 1;
    tiku_config_snapshot_t trimmed = snapshot();
    assert(tiku_config_outbox_save(directory, &backend, "/sys/device/name",
        &trimmed, "Trimmed", 7, error, sizeof error) == 0 && writes == 1);
    inspect(0);
    cleanup();

    /* A directory named with a trailing slash is one that opens. */
    setup();
    {
        char slashed[192];
        snprintf(slashed, sizeof slashed, "%s/", directory);
        assert(!setenv("TIKU_CONFIG_OUTBOX", slashed, 1));
        char chosen[192];
        assert(tiku_config_outbox_dir(chosen, sizeof chosen) == 0);
        assert(ob_directory(chosen, 0) >= 0);
        assert(!unsetenv("TIKU_CONFIG_OUTBOX"));
    }
    cleanup();

    puts("config outbox: disk failure, restart, receipt, UID, conflict, enrollment, locking and corruption PASS");
    return 0;
}
