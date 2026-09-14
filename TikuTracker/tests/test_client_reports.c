/* Application runtime diagnostics include events but never typed text.
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tiku_client.h"
#include "tiku_remote.h"

static int mode;

/** One small real window; mode three fails before any window opens. */
static int start(void **state, const tiku_app_services_t *services)
{
    *state = NULL;
    if (mode == 3) return 1;
    return services->open(services->ctx, "Diagnostics", 16, 16) ? 0 : 1;
}
static void stop(void *state) { (void)state; }

/** A controlled resize fault exercises the same path as an application bug. */
static int event(void *state, const tiku_event_t *e)
{
    (void)state;
    if (mode == 2 && e->type == TIKU_EVENT_RESIZE) raise(SIGABRT);
    return mode == 4;
}
static const tiku_app_descriptor_t app = {
    .id = "test.diagnostics", .name = "Diagnostics",
    .start = start, .stop = stop, .event = event
};

/** Each scenario has a private report directory and a fresh child runtime. */
static void scenario(int which)
{
    char dir[] = "/tmp/tiku-client-reports-XXXXXX", path[512], text[65536];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/latest-crash.txt", dir);
    int pair[2], status = 0; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    mode = which;
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
        close(pair[0]); setenv("TIKU_TRK_REPORTS", dir, 1);
        _exit(tiku_client_run_fd(&app, pair[1]));
    }
    close(pair[1]);
    tiku_remote_listener_t desk = {0}; desk.fd = -1;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) desk.session[i].fd = -1;
    assert(!tiku_remote_adopt(&desk, pair[0]));
    for (int i = 0; i < 1000 && desk.session[0].used &&
         !desk.session[0].win[0].opened; i++) {
        tiku_remote_poll(&desk); usleep(1000);
    }
    if (which != 3) assert(desk.session[0].win[0].opened);
    tiku_window_t fake = {0}; desk.session[0].win[0].window = &fake;
    if (which == 0) tiku_remote_shutdown(&desk);
    if (which == 1) assert(!shutdown(desk.session[0].fd, SHUT_RDWR));
    if (which == 2 || which == 4) {
        tiku_event_t e = {.type = TIKU_EVENT_RESIZE, .width = 1440, .height = 900};
        tiku_remote_event(&desk, &fake, &e);
    }
    int ended = 0;
    for (int i = 0; i < 2000; i++) {
        tiku_remote_poll(&desk);
        if (waitpid(child, &status, WNOHANG) == child) { ended = 1; break; }
        usleep(1000);
    }
    if (!ended) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    assert(ended);
    tiku_remote_shutdown(&desk);
    FILE *f = fopen(path, "r");
    if (which == 0 || which == 4) {
        assert(!f && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    } else {
        assert(f); size_t n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f);
        assert(strstr(text, "Diagnostics") && strstr(text, "application start:"));
        if (which == 1) assert(strstr(text, "unexpected desktop connection loss"));
        if (which == 2) {
            assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
            assert(strstr(text, "resize window 1 to 1440x900"));
            assert(strstr(text, "signal code:") && strstr(text, "instruction:"));
        } else assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
        if (which == 3) assert(strstr(text, "application initialization failed"));
    }
    printf("PASS: application diagnostics scenario %d\n", which);
}

int main(void)
{
    assert(setenv("TIKU_APP_HELP", "apps/editor.html", 1) == 0);
    for (int i = 0; i < 5; i++) scenario(i);
    return 0;
}
