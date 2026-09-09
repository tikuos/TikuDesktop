/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watch.c - noticing that a turn has not ended.
 *
 * The watching thread touches one counter and nothing else.  What it
 * does when the count stops moving is ASK the stuck thread to write the
 * report, by a signal: the stack worth having is the stuck one's, and
 * the trail is then read by the thread that writes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_watch.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "tiku_report.h"
#include "tiku_trail.h"

/*---------------------------------------------------------------------------*/
/* THE COUNT, AND WHO IS WATCHING IT                                         */
/*---------------------------------------------------------------------------*/

#define LOOK_EVERY_MS   250
#define ANSWER_WITHIN_MS 1000

/* Volatile because two threads read it and one writes it; a counter is
 * all they share, and a count that is one turn stale is still a count
 * that moved. */
static volatile unsigned long beats;
static volatile int           answered;
static volatile long          quiet_ms;
static volatile int           stalls;

static pthread_t turning;           /* the thread that runs the loop */
static int       after_ms = 5000;
static int       watching_on;

/** @brief On the stuck thread: write what it is in the middle of. */
static void
asked_to_say(int sig)
{
    (void)sig;
    tiku_report_stall(quiet_ms, 1);
    answered = 1;
}

/** @brief Sleep @p ms, whatever else is going on. */
static void
rest(long ms)
{
    struct timespec t;

    t.tv_sec = ms / 1000L;
    t.tv_nsec = (ms % 1000L) * 1000000L;
    (void)nanosleep(&t, NULL);
}

/**
 * @brief The watcher: count, and when the count stops, ask.
 *
 * It never ends anything.  A scan that takes six seconds is a stall to
 * write down, not a program to kill -- and a shell killed by its own
 * watchdog would lose the very thing somebody wanted to read.
 */
static void *
watching(void *ignored)
{
    unsigned long seen = beats;
    long quiet = 0L;
    long say_at = after_ms;

    (void)ignored;
    for (;;) {
        rest(LOOK_EVERY_MS);
        if (beats != seen) {
            seen = beats;
            quiet = 0L;
            say_at = after_ms;
            continue;
        }
        quiet += LOOK_EVERY_MS;
        if (quiet < say_at) {
            continue;
        }
        quiet_ms = quiet;
        answered = 0;
        /* Asking cuts short a sleep the stuck thread was in -- a
         * nanosleep is never resumed after a handler, whatever the
         * handler asked for.  A loop that waits must expect that. */
        (void)pthread_kill(turning, SIGUSR1);
        {
            /* A thread deep in something the kernel will not interrupt
             * cannot answer; after a moment the watcher writes what it
             * has, which is the trail without a stack. */
            long waited = 0L;

            while (!answered && waited < ANSWER_WITHIN_MS) {
                rest(50L);
                waited += 50L;
            }
            if (!answered) {
                tiku_report_stall(quiet, 0);
            }
        }
        stalls++;
        /* Said again as it goes on, so a long stall leaves a record of
         * being long rather than one of being five seconds. */
        say_at = (say_at < 30000L) ? 30000L
               : (say_at < 120000L) ? 120000L : quiet + 120000L;
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/* ARMING                                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_watch_install(int ms)
{
    pthread_t watcher;
    struct sigaction sa;

    if (watching_on) {
        return;
    }
    after_ms = (ms > 0) ? ms : 5000;
    turning = pthread_self();

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = asked_to_say;
    sa.sa_flags = SA_RESTART | SA_ONSTACK;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGUSR1, &sa, NULL);

    if (pthread_create(&watcher, NULL, watching, NULL) != 0) {
        return;
    }
    (void)pthread_detach(watcher);
    watching_on = 1;
    tiku_trail("a turn quiet for %d ms is written down", after_ms);
}

void
tiku_watch_beat(void)
{
    beats++;
}

int
tiku_watch_stalls(void)
{
    return stalls;
}
