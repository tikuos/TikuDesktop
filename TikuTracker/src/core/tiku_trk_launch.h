/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch.h - which application opens a document, and running it.
 *
 * Tracker asks the MIME database three questions: which applications claim
 * this type, which one is preferred, and how well each matches.  There is no
 * MIME database here, so the same three questions are asked of a registry
 * the port owns: a list of applications, each naming the types it reads.
 * The ANSWERS are what the rows are about -- the ranking, the union across a
 * multiple selection, and what happens when nothing claims the type.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_LAUNCH_H_
#define TIKU_TRK_LAUNCH_H_

#include <sys/types.h>
#include "tiku_model.h"

struct tiku_store;

/** @brief How many applications the registry holds, and types each claims. */
#define TIKU_TRK_APP_MAX        16
#define TIKU_TRK_APP_TYPES_MAX   8
#define TIKU_TRK_APP_NAME_MAX   48
#define TIKU_TRK_APP_CMD_MAX   192

/**
 * @brief WHY an application matched, which is also how well (MA-017).
 *
 * The order is the ranking: an exact type beats a supertype match, which
 * beats an application that takes anything at all.  A preferred handler is
 * not a fourth kind of match -- it is one of these that the user has
 * chosen -- so it is carried beside the relation rather than inside it.
 */
typedef enum {
    TIKU_TRK_REL_NONE = 0,
    TIKU_TRK_REL_SUPERHANDLER,   /* claims everything                     */
    TIKU_TRK_REL_SUPERTYPE,      /* claims "text/", document is "text/c"  */
    TIKU_TRK_REL_EXACT
} tiku_trk_relation_t;

/** @brief One application the registry knows about. */
typedef struct tiku_trk_app {
    char name[TIKU_TRK_APP_NAME_MAX];    /* as the Open With menu says it */
    char cmd[TIKU_TRK_APP_CMD_MAX];      /* argv[0], run with the paths   */
    char type[TIKU_TRK_APP_TYPES_MAX][TIKU_TYPE_MAX]; /* the types claimed */
    int  ntype;                          /* how many of those            */
    int  takes_anything;                 /* a superhandler                */
    /* Where the application itself lives, when it is a file in the
     * namespace.  Its own path is dropped from the documents handed to it,
     * so an application never opens itself (AW-038). */
    char path[TIKU_PATH_MAX];
    int  is_tracker;                     /* this Tracker, not a launch    */
} tiku_trk_app_t;

/** @brief One candidate, with what made it one. */
typedef struct {
    const tiku_trk_app_t *app;
    tiku_trk_relation_t   rel;
    int                   preferred;     /* the user's choice for the type */
} tiku_trk_cand_t;

typedef struct tiku_trk_apps {
    tiku_trk_app_t app[TIKU_TRK_APP_MAX];    /* every application registered */
    int            count;                    /* how many slots are used      */
} tiku_trk_apps_t;

/** @brief Forget every application. */
void tiku_trk_launch_clear(tiku_trk_apps_t *r);

/**
 * @brief Add one application.  @return its index, or -1.
 *
 * @param types Comma-separated; "*" means it takes anything.
 */
int tiku_trk_launch_add(tiku_trk_apps_t *r, const char *name,
                        const char *cmd, const char *types);

/**
 * @brief Read a registry file: one application per line.
 *
 * `name<TAB>command<TAB>type,type,...`, '#' comments, blank lines ignored.
 * A file rather than a database because that is what this port can carry:
 * the rules that matter are about the ANSWERS, not about where the list is
 * kept (AW-013).
 *
 * @return how many were added, or -1 when the file cannot be read.
 */
int tiku_trk_launch_load(tiku_trk_apps_t *r, const char *path);

/**
 * @brief How @p app relates to @p type (MA-017).
 *
 * Wildcard-aware: a claim ending in '/' is a supertype claim, and "*"
 * claims everything.
 */
tiku_trk_relation_t tiku_trk_launch_relation(const tiku_trk_app_t *app,
                                             const char *type);

/**
 * @brief The applications that can open @p types, best first (AW-040).
 *
 * The UNION over the selection: an application appears when it can open
 * ANY of the types, which is what makes a mixed selection openable at all.
 * Sorted by relation and then by name, so the order is an explanation
 * rather than an accident (AW-042).  Applications in the Trash and
 * applications that are not really applications never reach here (AW-041)
 * -- the registry holds no such entries by construction.
 *
 * @param preferred_of Consulted per type for the user's choice; may be NULL.
 * @return how many candidates were written.
 */
int tiku_trk_launch_candidates(const tiku_trk_apps_t *r,
                               const char *const *types, int ntypes,
                               struct tiku_store *store,
                               tiku_trk_cand_t *out, int max);

/**
 * @brief Whether every one of @p types agrees on one preferred handler.
 *
 * The Open With list preselects a handler only when the selection agrees:
 * with two documents wanting different applications there is no answer to
 * preselect (AW-043).
 *
 * @return the application, or NULL.
 */
const tiku_trk_app_t *tiku_trk_launch_agreed(const tiku_trk_apps_t *r,
                                             const char *const *types,
                                             int ntypes,
                                             struct tiku_store *store);

/** @brief Remember @p app as the handler for @p type (AW-044). */
int tiku_trk_launch_set_preferred(struct tiku_store *store,
                                  const char *type, const char *app_name);

/** @brief The remembered handler for @p type, or "" . */
const char *tiku_trk_launch_preferred(struct tiku_store *store,
                                      const char *type, char *buf,
                                      size_t max);

/** @brief The word a Relation column shows for @p rel (AW-042, MA-081). */
const char *tiku_trk_launch_relation_label(tiku_trk_relation_t rel);

/** @brief What went wrong, so the diagnosis can be specific (AW-039). */
typedef enum {
    TIKU_TRK_LAUNCH_OK = 0,
    TIKU_TRK_LAUNCH_NO_APP,      /* nothing claims the type               */
    TIKU_TRK_LAUNCH_NOTHING,     /* no documents to open                  */
    TIKU_TRK_LAUNCH_IS_TRACKER,  /* handled here instead (AW-035)         */
    TIKU_TRK_LAUNCH_FAILED       /* the command could not be run          */
} tiku_trk_launch_err_t;

/**
 * @brief Make @p m into the application it already is.
 *
 * A file that can be executed does not need something else to open it --
 * it IS the thing that opens.  Asking the registry which application
 * handles its type answers nothing for one, and "no application" for
 * the one file in a folder guaranteed to be runnable is the wrong
 * refusal.
 *
 * @return 1 when @p m is a runnable file and @p out was filled.
 */
int tiku_trk_launch_self(const tiku_model_t *m, tiku_trk_app_t *out);

/**
 * @brief Run @p app over @p paths (AW-002, AW-038).
 *
 * The application's own file is dropped from what it is handed, so it never
 * opens itself.  The child starts in the user's home directory, which is
 * what the original gives everything it launches.  The parent does not wait:
 * a launch that blocked the window it was started from would be a launch
 * the user watches.
 *
 * @return TIKU_TRK_LAUNCH_OK, or why not.
 */
tiku_trk_launch_err_t tiku_trk_launch_run(const tiku_trk_app_t *app,
                                          const char *const *paths, int n);

/** @brief The sentence a failed launch should say (AW-039). */
const char *tiku_trk_launch_reason(tiku_trk_launch_err_t err);

/**
 * @brief Remember that @p pid is @p name, until it is collected.
 *
 * A program that ends is reaped by pid, so nothing else's child is
 * taken, and named when it goes -- a bare pid says nothing to the
 * person reading what happened.
 */
void tiku_trk_launch_note(pid_t pid, const char *name);

/**
 * @brief Collect any launched program that has ended.
 * @return how many, each written into the trail with how it went.
 */
int tiku_trk_launch_reap(void);

#endif /* TIKU_TRK_LAUNCH_H_ */
