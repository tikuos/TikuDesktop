/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tests/test_sys.c - the system seam, against whichever host is running.
 *
 * What POSIX leaves to the machine has one declaration and one file per
 * host behind it, so the only thing worth asserting is the CONTRACT: the
 * answers are about this machine, and they are the same shape wherever
 * they come from.  Nothing here knows which host it is on, which is the
 * point -- a seam that reads the wrong sysctl fails this the same way a
 * seam that reads the wrong file does.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "tiku_sys.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief What the roster pass is looking for while the seam offers. */
struct looking {
    int  offered;
    int  found_me;
    int  every_one_named;
    char my_name[64];
    char my_command[512];
};

/** @brief One process, as tiku_sys_processes() hands it over. */
static void
saw(const tiku_sys_process_t *process, void *ctx)
{
    struct looking *look = ctx;

    look->offered++;
    if (process->name == NULL || process->name[0] == '\0' ||
        process->command == NULL || process->command[0] == '\0' ||
        process->pid <= 0) {
        look->every_one_named = 0;
    }
    if (process->pid == (long)getpid()) {
        look->found_me = 1;
        snprintf(look->my_name, sizeof look->my_name, "%s",
                 process->name ? process->name : "");
        snprintf(look->my_command, sizeof look->my_command, "%s",
                 process->command ? process->command : "");
    }
}

/**
 * @brief A terminal with nothing under it, for asking what a seam says.
 *
 * openpty(3) is in a different header on each host and this file will
 * not name one; the POSIX road to the same pair is in <stdlib.h> on
 * both.
 *
 * @return 0 and a pair, or -1.
 */
static int
a_terminal(int *master, int *slave)
{
    const char *name;

    *master = posix_openpt(O_RDWR | O_NOCTTY);
    if (*master < 0) {
        return -1;
    }
    if (grantpt(*master) != 0 || unlockpt(*master) != 0 ||
        (name = ptsname(*master)) == NULL ||
        (*slave = open(name, O_RDWR | O_NOCTTY)) < 0) {
        close(*master);
        return -1;
    }
    return 0;
}

int
main(void)
{
    printf("the system seam, whichever host is underneath\n");

    {
        char path[] = "/tmp/tiku-sys-sync-XXXXXX";
        int fd = mkstemp(path);
        check(fd >= 0, "a private regular file can be created");
        if (fd >= 0) {
            check(write(fd, "sync", 4) == 4 && tiku_sys_sync_file(fd) == 0,
                  "the host can flush a regular file through its durability barrier");
            close(fd);
            check(unlink(path) == 0, "the temporary file is removed");
        }
        check(tiku_sys_sync_file(-1) == -1, "a failed flush is reported, not hidden");
    }

    {
        struct looking look;
        int got;

        memset(&look, 0, sizeof look);
        look.every_one_named = 1;
        got = tiku_sys_processes(saw, &look);
        check(got > 0 && got == look.offered,
              "the roster offers what it says it offered");
        check(look.every_one_named,
              "and every one of them has a pid, a name and an image: a "
              "row with nothing to show is a row a person cannot use");
        /* The one process this test can be certain about is itself. */
        check(look.found_me, "this very process is among them");
        check(strstr(look.my_name, "test_sys") != NULL,
              "under the name it was built as");
        check(look.my_command[0] == '/',
              "and with the image it was started from");
    }

    {
        long total = -1, avail = -1;

        tiku_sys_memory(&total, &avail);
        check(total > 0, "the machine says how much memory it has");
        check(avail > 0 && avail <= total,
              "and how much of it is still to be had, which is some of it "
              "and not more than all of it");
    }

    check(tiku_sys_uptime() > 0,
          "and how long it has been up, which is not none");

    {
        char self[512];
        const char *leaf;

        check(tiku_sys_self_path(self, sizeof self) == 0 && self[0] == '/',
              "this program knows its own image, by an absolute path");
        leaf = strrchr(self, '/');
        leaf = (leaf != NULL) ? leaf + 1 : self;
        check(strcmp(leaf, "test_sys") == 0,
              "and it is THIS program's, not the shell's that started it");
        check(access(self, X_OK) == 0,
              "and something is there to exec, which is what a restart "
              "does with it");
    }

    {
        char out[200];

        /* A line no host writes: what comes back is the line itself,
         * because a report is worth more unreadable than empty. */
        out[0] = '\0';
        tiku_sys_frame_name("nothing any backtrace ever wrote", out,
                            sizeof out);
        check(strstr(out, "nothing any backtrace") != NULL,
              "a frame it cannot read comes back as it went in");
        out[0] = '\0';
        tiku_sys_frame_name("", out, sizeof out);
        check(out[0] == '\0' || strlen(out) < sizeof out,
              "and an empty one is answered rather than overrun");
    }

    check(tiku_sys_fault_pc(NULL) == NULL,
          "no context is no instruction, rather than a wild pointer");

    {
        /*
         * A rate the ordinary call could not set.  What this can assert
         * without a cable is the CONTRACT, which is the same on both
         * hosts: asked about something that is not a serial line it
         * says it cannot, rather than reporting a line that is now
         * running at a rate nothing is running at.  A pty is exactly
         * that -- a terminal with no driver under it -- so it stands in
         * for every descriptor the answer must be "no" for.
         */
        int master, slave;

        check(tiku_sys_serial_speed(-1, 115200) == -1,
              "a rate cannot be set on a descriptor that is not open");
        if (a_terminal(&master, &slave) == 0) {
            check(tiku_sys_serial_speed(slave, 0) == -1,
                  "nor can no rate at all be set on a real one");
            check(tiku_sys_serial_speed(slave, 921600) == -1,
                  "and a terminal with no serial driver under it says "
                  "so, rather than claiming a line it does not have");
            close(slave);
            close(master);
        }
        /*
         * And the rates the tree offers are still set the ordinary way
         * wherever that works, which is what keeps the second way off
         * the path a board is usually opened on.
         */
        if (a_terminal(&master, &slave) == 0) {
            struct termios t;
            int exact = 1, i;
            const int RATES[] = { 9600, 115200, 230400, 460800, 921600 };

            for (i = 0; i < (int)(sizeof RATES / sizeof RATES[0]); i++) {
                speed_t want = (speed_t)RATES[i];

                if (tcgetattr(slave, &t) != 0) {
                    exact = 0;
                    break;
                }
                cfmakeraw(&t);
                (void)cfsetispeed(&t, want);
                (void)cfsetospeed(&t, want);
                if (tcsetattr(slave, TCSANOW, &t) != 0 ||
                    tcgetattr(slave, &t) != 0 || cfgetospeed(&t) != want) {
                    exact = 0;
                    break;
                }
            }
            check(exact,
                  "every rate the tree offers is set by name and reads "
                  "back as itself, so the driver is only ever asked "
                  "when the ordinary way did not take");
            close(slave);
            close(master);
        }
    }

    {
        /*
         * The ports a board could be behind.  There may be none -- no
         * machine is required to have a board plugged into it -- so
         * what is asserted is the shape of the answer and the two
         * promises a caller leans on: every path it gives back is
         * openable as a path, and nothing is opened to find that out.
         *
         * The second promise is why a caller can list on the turn and
         * decide off it: a port whose device has not answered its host
         * blocks the open for half a minute on some machines, and a
         * desktop that stopped for half a minute while drawing a menu
         * is a desktop that has stopped.
         */
        char ports[8][256];
        int n = tiku_sys_serial_ports(ports, 8);
        int shaped = 1, quick = 1, i;
        long long spent;

        check(n >= -1 && n <= 8,
              "the list of serial ports is as long as it says, or says "
              "this host keeps no such list");
        for (i = 0; i < (n > 0 ? n : 0); i++) {
            if (ports[i][0] != '/' || strlen(ports[i]) < 2u) {
                shaped = 0;
            }
        }
        check(shaped, "and each one is an absolute path, which is the "
                      "only thing a caller can do anything with");
        {
            struct timespec t0, t1;

            clock_gettime(CLOCK_MONOTONIC, &t0);
            (void)tiku_sys_serial_ports(ports, 8);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            spent = (t1.tv_sec - t0.tv_sec) * 1000LL +
                    (t1.tv_nsec - t0.tv_nsec) / 1000000LL;
            quick = (spent < 500);
        }
        check(quick, "and listing them costs nothing near what opening "
                     "one can, because listing opens none of them");
    }

    printf("\n%s\n", fails ? "FAILURES"
                           : "the seam answers about this machine");
    return fails ? 1 : 0;
}
