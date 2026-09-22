/* Runner metadata and launch envelopes, without loading an application. */
#include <assert.h>
#include <fcntl.h>
#define main runner_entry
#include "../../system/runner/tiku_run.c"
#undef main

int main(void)
{
    assert(!unsetenv("PATH"));
    assert(bundle_environment("build/runtime/apps/image/TikuImage.so"));
    const char *bundle = getenv("TIKU_APP_BUNDLE");
    assert(bundle && bundle[0] == '/' && strstr(bundle, "/apps/image"));
    assert(strstr(getenv("PATH"), ":/usr/bin:/bin"));
    assert(!strcmp(getenv("TIKU_APP_HELP"), "apps/viewer.html"));

    tiku_msg_t *message = tiku_msg_new(42); assert(message);
    const char *html = "<p>literal $(text); not a command</p>";
    assert(tiku_msg_add_string(message, "html", html));
    size_t size = tiku_msg_flat_size(message);
    void *bytes = malloc(size); assert(bytes);
    assert(tiku_msg_flatten(message, bytes, size, NULL));
    FILE *f = tmpfile(); assert(f);
    assert(fwrite(bytes, 1, size, f) == size && !fflush(f));
    rewind(f);
    int fd = dup(fileno(f)); assert(fd >= 3);
    char number[32]; snprintf(number, sizeof number, "%d", fd);
    tiku_msg_t *copy = read_message(number);
    assert(copy && tiku_msg_what(copy) == 42);
    assert(!strcmp(tiku_msg_find_string(copy, "html", 0), html));
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    tiku_msg_free(copy); tiku_msg_free(message); free(bytes); fclose(f);
    assert(!read_message("2") && !read_message("bad") &&
           !read_message("999999999999999999999999"));
    int pipefd[2]; assert(!pipe(pipefd));
    snprintf(number, sizeof number, "%d", pipefd[0]);
    assert(!read_message(number)); /* Must not wait for bytes from a pipe. */
    close(pipefd[0]); close(pipefd[1]);
    puts("PASS: bundle-local helpers/help, absent PATH, bounded literal launch data");
}
