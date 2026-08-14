/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_DROP_PROTOCOL_H_
#define TIKU_TRK_DROP_PROTOCOL_H_

#include <stdint.h>

#define TIKU_TRK_DROP_ACTION_COPY  0x1u
#define TIKU_TRK_DROP_ACTION_MOVE  0x2u
#define TIKU_TRK_DROP_ACTION_LINK  0x4u
#define TIKU_TRK_DROP_ACTION_TRASH 0x8u

typedef struct {
    unsigned actions;
    int has_refs;
    int has_text;
    int has_bitmap;
    int promise;
    const char *originator;
} tiku_trk_foreign_offer_t;

typedef enum {
    TIKU_TRK_DROP_REPLY_NONE = 0,
    TIKU_TRK_DROP_REPLY_COPY_TARGET,
    TIKU_TRK_DROP_REPLY_MOVE_TARGET,
    TIKU_TRK_DROP_REPLY_TRASH_TARGET,
    TIKU_TRK_DROP_REPLY_REJECT
} tiku_trk_drop_reply_t;

typedef struct {
    tiku_trk_drop_reply_t reply;
    unsigned chosen_action;
    int create_clipping;
    int accepted;
} tiku_trk_drop_result_t;

int tiku_trk_foreign_offer_parse(tiku_trk_foreign_offer_t *offer,
                                 const char *actions, const char *types,
                                 int promise);
tiku_trk_drop_result_t tiku_trk_foreign_drop(const tiku_trk_foreign_offer_t *o,
                                             int target_directory,
                                             int target_trash, int root_disk,
                                             unsigned requested_action);

#endif
