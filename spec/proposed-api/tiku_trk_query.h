/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   Decision 6: one bounded predicate AST for all three Find modes (so formula mode is the AST's own syntax), a three-valued cost-ordered evaluator with a pluggable per-entry attribute source, the saved query as a text record that is byte-identical on a local filesystem and in a device store, and a running query whose results ride the same event vocabulary as everything else. Liveness arming is graded by the descriptor: static costs nothing, cached subscribes, live polls at its own cost class. */

/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_query.h - one predicate, two indexes, and a query that is a file.
 *
 * All three Find modes compile to one bounded AST, so formula mode is
 * literally the AST's own syntax and "prefill the formula from the rows" is
 * a print.  Evaluation is host-side against two very different indexes: a
 * device's manifest IS a complete typed index of its whole namespace, which
 * is the one place this system beats the filesystem Tracker was written for;
 * a local tree has no index at all and is walked.  Same predicate, same
 * results -- what degrades is latency and liveness, never semantics.
 *
 * Liveness is graded by the descriptor, because on a device it costs energy:
 * a static node is evaluated once and never again, a cached node is
 * subscribed, and a live node is polled at an interval its own cost class
 * chooses.  Pretending an on-demand sensor has an event to offer would be a
 * lie, and polling everything would be a waste.
 *
 * The query stays an openable, renameable, deletable object on disk, because
 * a dozen behaviours hang off that and would die quietly if it became "a
 * command that prints results".
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_QUERY_H_
#define TIKU_QUERY_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_trk_event.h"
#include "tiku_model.h"
#include "tiku_trk_task.h"

#define TIKU_TRK_PRED_MAX_NODES 64
#define TIKU_TRK_PRED_POOL     512
#define TIKU_TRK_QUERY_SCOPES    8
#define TIKU_TRK_QUERY_ROWS      8

typedef enum {
    TIKU_OP_CONTAINS = 0, TIKU_OP_IS, TIKU_OP_IS_NOT,
    TIKU_TRK_OP_STARTS_WITH, TIKU_OP_ENDS_WITH,
    TIKU_TRK_OP_GT, TIKU_TRK_OP_LT, TIKU_TRK_OP_GE, TIKU_TRK_OP_LE,
    TIKU_TRK_OP_BEFORE, TIKU_TRK_OP_AFTER,
    TIKU_TRK_OP_GLOB, TIKU_TRK_OP_REGEX
} tiku_op_t;

typedef enum {
    TIKU_TRK_PN_TERM = 0, TIKU_TRK_PN_AND, TIKU_TRK_PN_OR, TIKU_TRK_PN_NOT
} tiku_trk_pnkind_t;

/**
 * @brief What answering a term costs.
 *
 * The device values are the descriptor's own cost classes offset by two, so
 * a node's declared cost passes straight through and orders the plan: free
 * terms decide first, and a value read is spent only on what survived them.
 */
typedef enum {
    TIKU_TRK_COST_META = 0, /**< in the mirror already                    */
    TIKU_TRK_COST_STAT,     /**< one local stat                           */
    TIKU_TRK_COST_FREE,
    TIKU_TRK_COST_CHEAP,
    TIKU_TRK_COST_PERIPH,
    TIKU_TRK_COST_BUS
} tiku_trk_cost_t;

typedef struct {
    uint8_t  kind;      /**< tiku_trk_pnkind_t                           */
    uint8_t  op;        /**< tiku_op_t, on terms                     */
    uint8_t  vtype;
    uint8_t  cost;      /**< tiku_trk_cost_t of the attribute            */
    int16_t  lhs, rhs;  /**< child indices; -1 on a term                 */
    uint16_t attr;      /**< offset into pool[]                          */
    uint8_t  icase;
    uint8_t  reserved;
    union { int64_t i; uint64_t u; double f; } val;
    void    *prog;      /**< compiled glob or regex; freed with the AST  */
} tiku_trk_pnode_t;

typedef struct {
    tiku_trk_pnode_t node[TIKU_TRK_PRED_MAX_NODES];
    uint8_t  count;
    int16_t  root;
    char     pool[TIKU_TRK_PRED_POOL];
    uint16_t pool_len;
    uint8_t  max_cost;     /**< the costliest term any evaluation needs  */
    uint8_t  dynamic_date; /**< a relative date term: re-evaluate later  */
} tiku_trk_pred_t;

/** @brief One row of the by-attribute panel. */
typedef struct {
    char    attr[32];
    uint8_t op;
    char    text[128];
    uint8_t combine_or;
} tiku_trk_qrow_t;

int tiku_trk_pred_from_name(tiku_trk_pred_t *p, const char *text,
                            const char *class_sel);
int tiku_trk_pred_from_rows(tiku_trk_pred_t *p, const tiku_trk_qrow_t *rows,
                            int n, const char *class_sel);
int tiku_trk_pred_compile(tiku_trk_pred_t *p, const char *formula,
                          char *err, size_t errmax);
/** @brief Serialize; this is how the formula field is prefilled from the
 *  rows, and the conversion stays one-way exactly as the original's does. */
int tiku_trk_pred_print(const tiku_trk_pred_t *p, char *buf, size_t max);
void tiku_trk_pred_free(tiku_trk_pred_t *p);

/** @brief Three-valued, so a cheap term can decide before a costly one is
 *  paid and an unreadable node is not silently a non-match. */
typedef enum {
    TIKU_TRK_MATCH_NO = 0,
    TIKU_TRK_MATCH_YES,
    TIKU_TRK_MATCH_UNKNOWN
} tiku_trk_match_t;

typedef struct {
    uint8_t     kind;   /**< MISSING / STR / INT / FLOAT / TIME / DEFER  */
    const char *s;
    int64_t     i;
    double      f;
} tiku_trk_aval_t;

/** @brief Where a term's left-hand side comes from for one entry. */
typedef struct {
    int      (*get)(void *ctx, const char *attr, unsigned max_cost,
                    tiku_trk_aval_t *out);  /**< DEFER when over budget  */
    unsigned (*cost_of)(void *ctx, const char *attr);
    void      *ctx;
} tiku_trk_entry_src_t;

tiku_trk_match_t tiku_trk_pred_eval(const tiku_trk_pred_t *p,
                                    const tiku_trk_entry_src_t *e,
                                    unsigned max_cost);

/* ---------------------------------------------------------------------- *
 * The saved query -- a file, on either store
 * ---------------------------------------------------------------------- */

typedef enum {
    TIKU_TRK_SCOPE_EVERYWHERE = 0,
    TIKU_TRK_SCOPE_LOCAL_TREE,
    TIKU_TRK_SCOPE_DEVICE,
    TIKU_TRK_SCOPE_DEVICE_TREE
} tiku_trk_scopekind_t;

typedef struct {
    uint8_t kind;
    char    path[TIKU_PATH_MAX];
    char    identity[64];  /**< the device's die uid; the binding key    */
    char    label[64];     /**< last known name, for display and scoring */
} tiku_trk_scope_t;

/**
 * @brief The on-disk record, parsed.
 *
 * A small text file with a magic first line, never extended attributes: it
 * has to be byte-identical on a local filesystem and inside a device's own
 * store, which has no attributes at all.  It carries both of Tracker's
 * representations -- the executable predicate the results window reads, and
 * the panel fields that let the query be edited again.
 */
typedef struct {
    char            file[TIKU_PATH_MAX];
    uint8_t         is_template;
    uint8_t         temporary;    /**< collected when its window closes  */
    uint8_t         mode;         /**< by name / by attribute / formula  */
    uint8_t         search_trash;
    uint8_t         dynamic_date;
    int64_t         lastchange;
    char            pred[1024];   /**< the executable half               */
    char            initstr[256]; /**< the editable half                 */
    char            class_sel[64];
    tiku_trk_qrow_t rows[TIKU_TRK_QUERY_ROWS];
    uint8_t         nrows;
    tiku_trk_scope_t scope[TIKU_TRK_QUERY_SCOPES];
    uint8_t         nscope;
    char            exclude[TIKU_TRK_QUERY_SCOPES][TIKU_PATH_MAX];
    uint8_t         nexclude;
} tiku_trk_qfile_t;

int tiku_trk_qfile_read(tiku_trk_qfile_t *qf, const char *path);
int tiku_trk_qfile_write(const tiku_trk_qfile_t *qf, const char *path);
int tiku_trk_qfile_default_name(const tiku_trk_qfile_t *qf, char *out,
                                size_t max);

/** @brief The queries directory, created on demand.  Templates and the
 *  recent list are found here plus an index sidecar rebuilt on mismatch --
 *  an explicit registry, not a query that has to find itself. */
const char *tiku_trk_queries_dir(void);

/* ---------------------------------------------------------------------- *
 * A running query
 * ---------------------------------------------------------------------- */

typedef struct tiku_query tiku_query_t;

/**
 * @brief Open a query and start producing results.
 *
 * Results arrive as ordinary events on @p q: a match entering is a CREATED,
 * leaving is a REMOVED, a matched row changing is a QUERY_UPDATE, and a
 * scope disconnecting is an UNMOUNTED.  There is no second vocabulary, so
 * the pose view's single notification door still holds.
 */
tiku_query_t *tiku_trk_query_open(const tiku_trk_qfile_t *qf,
                                      tiku_trk_loop_t *loop,
                                      tiku_trk_evq_t *q);

/** @brief Close it and release every watch slot it holds -- a query window
 *  that leaks slots starves the eight the whole device shares. */
void tiku_trk_query_close(tiku_query_t *qq);

/** @brief Re-evaluate wholesale (a dynamic date rolled over, or the view
 *  lost events and asked for a refresh). */
int tiku_query_refresh(tiku_query_t *qq);

/**
 * @brief The seam: something touched @p ref.
 *
 * Re-runs the predicate for that ONE node and emits enter, leave or update.
 * Fed identically by a local watch, by a device notification and by the
 * poll tier.  A row leaving never closes the window; only the query file
 * being deleted does that.
 */
void tiku_trk_query_on_change(tiku_query_t *qq,
                              const tiku_trk_ref_t *ref);

/** @brief What the count view renders, including how much of the result is
 *  pushed, polled or free, and how many reads have been spent. */
int tiku_trk_query_liveness(const tiku_query_t *qq,
                            tiku_trk_liveness_t *out);

/**
 * @brief Arm liveness for a device scope, graded by descriptor.
 *
 * static: nothing at all.  cached: one subscription.  live: an interval
 * poll chosen from the node's own cost class.  Slots beyond the budget
 * (some are always left for the user's own watches) fall back to polling,
 * loudly, and the count view says how many.
 *
 * @return Slots armed.
 */
int tiku_trk_query_arm(tiku_query_t *qq, tiku_trk_dev_t dev,
                       int slot_budget, unsigned value_budget);

#endif /* TIKU_QUERY_H_ */
