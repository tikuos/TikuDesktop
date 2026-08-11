/* PROPOSED by the N1 reconciler — reference, not yet adopted.
   Decision 2 in full. One type-string namespace with two registries behind one resolver: a string starting "tiku-" is answered by the compiled node-type table, everything else by the MIME database. Carries the six reserved supertypes, the manifest-facts struct, the classifier, the open-action vocabulary, the icon-source rungs, the per-type attribute record and the Open With handler table. Included by tiku_trk_model.h, so every Model carries exactly one type field. */

/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_type.h - one type-string namespace, two registries, one resolver.
 *
 * Tracker's type system is a string with a slash in it, and forty-two
 * behaviours are written against that shape: supertype widening, icon
 * fallback, Open With ranking, per-type columns, query constraints.  So a
 * device node gets a real type string too -- "tiku-sensor/temperature" --
 * and every one of those behaviours ports instead of being rewritten.
 *
 * A string beginning "tiku-" is answered by the compiled node-type table;
 * anything else by the platform MIME database.  That strncmp is the ONLY
 * place in the codebase that knows two type systems exist, which is why no
 * caller branches on backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_TYPE_H_
#define TIKU_TRK_TYPE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TIKU_TRK_TYPE_LEN  64
#define TIKU_TRK_TYPE_PFX  "tiku-"   /**< the whole disjointness rule */

/* The reserved device types.  Six supertypes, split on the axis the
 * descriptor already encodes -- writable, live-read, cheap-read -- so the
 * supertype rung stays meaningful instead of collapsing to one bucket. */
#define TIKU_TRK_T_DIR_ROOT     "tiku-dir/root"
#define TIKU_TRK_T_DIR_DEVICE   "tiku-dir/device"
#define TIKU_TRK_T_DIR_FOLDER   "tiku-dir/folder"
#define TIKU_TRK_T_DIR_SYS      "tiku-dir/sys"
#define TIKU_TRK_T_DIR_DEV      "tiku-dir/dev"
#define TIKU_TRK_T_DIR_PROC     "tiku-dir/proc"
#define TIKU_TRK_T_DIR_STORE    "tiku-dir/store"   /**< a dynops dir (/data) */
#define TIKU_TRK_T_NODE_OPAQUE  "tiku-node/opaque" /**< == octet-stream      */
#define TIKU_TRK_T_NODE_UNKNOWN "tiku-node/unknown"
/* Subtypes, by supertype:
 *   tiku-sensor   temperature voltage current power energy light raw count
 *   tiku-control  switch level enum text command
 *   tiku-meter    counter uptime bytes frequency ratio ident state value
 *   tiku-file     data basic text csv json log                            */

typedef enum {
    TIKU_TRK_SRC_NONE = 0,
    TIKU_TRK_SRC_MIME,
    TIKU_TRK_SRC_NODE
} tiku_trk_typesrc_t;

/** @brief The seam.  Every type-driven operation dispatches on this. */
static inline tiku_trk_typesrc_t
tiku_trk_type_source(const char *type)
{
    if (type == NULL || *type == '\0') {
        return TIKU_TRK_SRC_NONE;
    }
    if (strncmp(type, TIKU_TRK_TYPE_PFX, 5) == 0) {
        return TIKU_TRK_SRC_NODE;
    }
    return TIKU_TRK_SRC_MIME;
}

/** @brief Copy the supertype of @p type into @p out.  0 on success. */
int tiku_trk_type_supertype(const char *type, char *out, size_t max);

/**
 * @brief Tracker's supertype widening test, evaluated locally.
 *
 * @p sel may be a bare supertype ("tiku-sensor") or a full type; this is
 * what the Find panel's type constraint becomes for both registries.
 */
int tiku_trk_type_matches(const char *node_type, const char *sel);

/* ---------------------------------------------------------------------- *
 * The facts a device node presents -- one manifest line, parsed
 * ---------------------------------------------------------------------- */

/** @brief Descriptor vocabulary, mirroring the kernel's enums by value. */
typedef enum { TIKU_TRK_FRESH_STATIC = 0, TIKU_TRK_FRESH_CACHED,
               TIKU_TRK_FRESH_LIVE } tiku_trk_fresh_t;

typedef enum { TIKU_TRK_E_FREE = 0, TIKU_TRK_E_CHEAP, TIKU_TRK_E_PERIPH,
               TIKU_TRK_E_BUS } tiku_trk_ecost_t;

typedef struct {
    unsigned is_dir    : 1;
    unsigned readable  : 1;
    unsigned writable  : 1;
    unsigned dyn_dir   : 1;  /**< a directory with dynops -- a store       */
    unsigned dyn_child : 1;  /**< discovered under a dynops dir            */
    unsigned has_desc  : 1;
    unsigned has_range : 1;
    unsigned hex       : 1;  /**< DF_HEX                                   */
    unsigned secret    : 1;  /**< DF_SECRET -- never render the value      */
    uint8_t  vtype;
    uint8_t  unit;
    uint8_t  fresh;          /**< tiku_trk_fresh_t: the liveness policy    */
    uint8_t  ecost;          /**< tiku_trk_ecost_t: the poll interval      */
    uint8_t  req_cap;
    int16_t  scale;          /**< decimal exponent for FIXED               */
    int32_t  vmin, vmax;
} tiku_trk_facts_t;

/**
 * @brief Parse the manifest's packed meta field.
 *
 * "-" or "vtype,unit,fresh,cost[,lo..hi][,scale=N][,f=hex|secret]".  The
 * trailing tokens are NAMED so firmware predating them parses cleanly:
 * absent scale is 0, absent flags none.  @return 0, or -1 on a malformed
 * field.
 */
int tiku_trk_meta_parse(const char *meta, tiku_trk_facts_t *out);

/**
 * @brief The device's FinishSettingUpType.
 *
 * Pure -- it never touches the link.  Writes a NUL-terminated "tiku-*"
 * string; never fails and never leaves the type empty, which is the
 * invariant the Kind column depends on.
 */
void tiku_trk_type_classify(const tiku_trk_facts_t *f,
                            char out[TIKU_TRK_TYPE_LEN]);

/* ---------------------------------------------------------------------- *
 * The node-type registry -- the MIME database's opposite number
 * ---------------------------------------------------------------------- */

/** @brief What double-clicking does.  Never nothing. */
typedef enum {
    TIKU_TRK_OPEN_CONTAINER = 0, /**< a window, exactly like a folder     */
    TIKU_TRK_OPEN_INSPECT,       /**< Get Info: value + descriptor        */
    TIKU_TRK_OPEN_EDIT,          /**< in-place value editor -> a write    */
    TIKU_TRK_OPEN_TOGGLE,        /**< bool: write the inverse now         */
    TIKU_TRK_OPEN_PLOT,          /**< subscribe + rolling time series     */
    TIKU_TRK_OPEN_TEXT
} tiku_trk_open_t;

/** @brief Where the icon came from; a device node is always UNKNOWN, so the
 *  node-supplied rungs are declared absent rather than probed. */
typedef enum {
    TIKU_TRK_ICON_UNKNOWN = 0,
    TIKU_TRK_ICON_NODE,
    TIKU_TRK_ICON_TYPE,
    TIKU_TRK_ICON_SUPERTYPE,
    TIKU_TRK_ICON_GENERIC,
    TIKU_TRK_ICON_BUILTIN
} tiku_trk_icon_src_t;

/** @brief One viewable attribute; the same record shape a MIME db carries,
 *  so the data-driven column menu needs no new code. */
typedef struct {
    const char *name;         /**< "tiku:value", "tiku:unit"              */
    const char *public_name;  /**< "Value", "Unit", "Range", "Cost"       */
    uint32_t    attr_type;
    uint16_t    width;
    uint8_t     align;
    unsigned    editable : 1;
    unsigned    viewable : 1;
    const char *display_as;   /**< "checkbox" for BOOL, else NULL         */
} tiku_trk_attr_t;

typedef struct {
    const char            *type;
    const char            *short_desc;  /**< the Kind column              */
    const char            *long_desc;
    const char            *mime_alias;  /**< the bridge to the MIME side  */
    uint16_t               icon_id;
    uint8_t                open_action; /**< tiku_trk_open_t              */
    const tiku_trk_attr_t *attrs;
    uint8_t                attr_count;
} tiku_trk_typeinfo_t;

const tiku_trk_typeinfo_t *tiku_trk_typedb_find(const char *type);
const tiku_trk_typeinfo_t *tiku_trk_typedb_super(const char *type);

/** @brief Enumerate the six supertypes (Find panel, Attributes menu). */
int tiku_trk_typedb_supertypes(const char **out, int max);
int tiku_trk_typedb_subtypes(const char *super, const char **out, int max);

/* Two-registry resolvers.  These are what the ported call sites use; each
 * dispatches on tiku_trk_type_source() and on nothing else. */

/** @brief exact -> supertype -> the raw string -> "Unknown". */
int tiku_trk_type_short_desc(const char *type, char *buf, size_t max);
int tiku_trk_type_long_desc(const char *type, char *buf, size_t max);

/** @brief Icon fallback rungs 3, 5 and 6.  @p src receives the rung that
 *  answered, which is the icon cache's tag. */
int tiku_trk_type_icon(const char *type, int px, tiku_trk_icon_src_t *src,
                       void **icon_out);

int tiku_trk_type_attrs(const char *type, const tiku_trk_attr_t **out,
                        int max);
tiku_trk_open_t tiku_trk_type_open_action(const char *type);

/* ---------------------------------------------------------------------- *
 * Handlers -- the Open With table
 * ---------------------------------------------------------------------- */

struct tiku_trk_model;

typedef struct tiku_trk_handler {
    const char *id;        /**< "plot"                                    */
    const char *name;      /**< "Plot"                                    */
    const char *supports;  /**< supertype, exact type, or the superhandler */
    uint16_t    icon_id;
    unsigned    background : 1;
    int       (*open)(struct tiku_trk_model *const *sel, int n, void *ctx);
} tiku_trk_handler_t;

/** @brief The superhandler test grows one token; the ranking above it is
 *  Tracker's unchanged. */
static inline int
tiku_trk_is_superhandler(const char *sig)
{
    return strcmp(sig, "application/octet-stream") == 0
        || strcmp(sig, TIKU_TRK_T_NODE_OPAQUE) == 0;
}

int tiku_trk_handlers_for(const char *type, const tiku_trk_handler_t **out,
                          int max);

/* The preferred-handler map: per node first, then per type.  Host-side,
 * always -- a device carries no BEOS:PREF_APP and nothing is written to it. */
const char *tiku_trk_pref_for_node(const char *store_uid, const char *path);
const char *tiku_trk_pref_for_type(const char *type);
int tiku_trk_pref_set_for_node(const char *store_uid, const char *path,
                               const char *handler_id);
int tiku_trk_pref_set_for_type(const char *type, const char *handler_id);

#endif /* TIKU_TRK_TYPE_H_ */
