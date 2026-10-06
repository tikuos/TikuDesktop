/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_sys_frames.c - Linux crash-frame names: the report's text and the
 * executable's path reach addr2line as arguments, never through a shell,
 * and a malformed frame or a missing helper yields the frame unnamed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _POSIX_C_SOURCE 200809L
#include "tiku_sys.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
static int failures;

/** @brief Record a frame-resolution result. */
static void check(int condition, const char *message)
{
    printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) {
        failures++;
    }
}

/** @brief Check that the helper gets the executable path as one argument. */
static void resolve_valid(void)
{
    char image[1024], result[256];
    if (tiku_sys_self_path(image, sizeof image) ||
        setenv("TIKU_FRAME_IMAGE", image, 1)) {
        failures++;
        return;
    }
    tiku_sys_frame_name("TikuDesktop(+0x123) [0x123]", result,
                        sizeof result);
    check(!strcmp(result, "fixture_symbol"),
          "symbol lookup preserves executable and offset arguments");
    tiku_sys_frame_name("TikuDesktop(+0x456)", result, sizeof result);
    check(!strcmp(result, "an earlier build's +0x456"),
          "unknown addresses retain the offset");
}

/** @brief Copy this executable to a path containing spaces. */
static int copy_executable(const char *destination)
{
    char image[1024], block[8192];
    size_t bytes;
    if (tiku_sys_self_path(image, sizeof image)) {
        return 0;
    }
    FILE *source = fopen(image, "rb"), *target = fopen(destination, "wb");
    if (!source || !target) {
        if (source) {
            fclose(source);
        }
        if (target) {
            fclose(target);
        }
        return 0;
    }
    int ok = 1;
    while ((bytes = fread(block, 1, sizeof block, source)) != 0) {
        if (fwrite(block, 1, bytes, target) != bytes) { ok = 0; break; }
    }
    if (ferror(source)) {
        ok = 0;
    }
    fclose(source);
    if (fclose(target)) {
        ok = 0;
    }
    return ok && chmod(destination, 0700) == 0;
}
#endif

/** @brief Exercise hostile frames, helper failures and paths with spaces. */
int main(int argc, char **argv)
{
#if defined(__linux__)
    if (argc == 2 && !strcmp(argv[1], "--child")) {
        resolve_valid();
        return failures ? 1 : 0;
    }
    char directory[] = "/tmp/tiku-frame-test-XXXXXX";
    char marker[256], helper[256], executable[256], result[256];
    const char *path = getenv("PATH");
    char *saved_path = path ? strdup(path) : NULL;
    if (path && !saved_path) {
        return 1;
    }
    if (!mkdtemp(directory)) {
        return 1;
    }
    int cwd = open(".", O_RDONLY);
    if (cwd < 0 || chdir(directory)) {
        return 1;
    }
    snprintf(marker, sizeof marker, "%s/injected", directory);
    tiku_sys_frame_name("TikuDesktop(+0;touch${IFS}injected)", result,
                        sizeof result);
    int injected = access(marker, F_OK) == 0;
    check(!injected, "frame text cannot execute a shell command");
    if (injected) {
        unlink(marker);
    }

    snprintf(helper, sizeof helper, "%s/addr2line", directory);
    FILE *script = fopen(helper, "w");
    if (!script) {
        return 1;
    }
    fputs("#!/bin/sh\n"
          ": > called\n"
          "[ \"$#\" = 6 ] && [ \"$1\" = -e ] && "
          "[ \"$2\" = \"$TIKU_FRAME_IMAGE\" ] || exit 2\n"
          "[ \"$3\" = -f ] && [ \"$4\" = -C ] && "
          "[ \"$5\" = -p ] || exit 2\n"
          "case \"$6\" in\n"
          "  +0x123) printf 'fixture_symbol at fixture.c:1\\n';;\n"
          "  +0x456) printf '?? ??:0\\n';;\n"
          "  *) exit 3;;\n"
          "esac\n", script);
    if (fclose(script) || chmod(helper, 0700) ||
        setenv("PATH", directory, 1)) return 1;
    resolve_valid();
    unlink("called");
    const char *bad[] = {"TikuDesktop(+0;echo invalid)",
        "TikuDesktop(+)", "TikuDesktop(+0x)", "TikuDesktop(+0xzz)",
        "TikuDesktop(+0x123", "TikuDesktop(+0x12345678901234567890)"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        tiku_sys_frame_name(bad[i], result, sizeof result);
        check(access("called", F_OK) != 0,
              "malformed offsets do not launch the helper");
        unlink("called");
    }
    tiku_sys_frame_name("/lib/example.so(named_symbol+0x10)", result,
                        sizeof result);
    check(!strcmp(result, "example.so: named_symbol"),
          "library symbols do not need the helper");
    tiku_sys_frame_name("TikuDesktop(+0x789)", result, sizeof result);
    check(!strcmp(result, "TikuDesktop +0x789"),
          "a failing helper leaves a readable frame");

    snprintf(executable, sizeof executable, "%s/program with spaces",
             directory);
    if (!copy_executable(executable)) {
        return 1;
    }
    pid_t child = fork();
    if (child == 0) {
        execl(executable, executable, "--child", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    check(child > 0 && waitpid(child, &status, 0) == child &&
          WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "symbol lookup works from an executable path with spaces");
    unlink(executable);
    unlink(helper);
    unlink("called");
    tiku_sys_frame_name("TikuDesktop(+0x123)", result, sizeof result);
    check(!strcmp(result, "TikuDesktop +0x123"),
          "an unavailable helper leaves a readable frame");

    if (saved_path) {
        setenv("PATH", saved_path, 1);
    } else {
        unsetenv("PATH");
    }
    unsetenv("TIKU_FRAME_IMAGE");
    free(saved_path);
    if (fchdir(cwd)) {
        return 1;
    }
    close(cwd);
    rmdir(directory);
    return failures ? 1 : 0;
#else
    (void)argc;
    (void)argv;
    puts("SKIP: crash-frame shell regression targets the Linux backend");
    return 0;
#endif
}
