/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku_trk_drop_protocol.h"
#include <string.h>

int
tiku_trk_foreign_offer_parse(tiku_trk_foreign_offer_t *o, const char *actions,
                             const char *types, int promise)
{
    if (o == NULL) return -1;
    memset(o, 0, sizeof *o);
    o->promise = promise != 0;
    if (actions == NULL || actions[0] == '\0') o->actions = TIKU_TRK_DROP_ACTION_COPY;
    else {
        if (strstr(actions, "copy") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_COPY;
        if (strstr(actions, "move") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_MOVE;
        if (strstr(actions, "link") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_LINK;
        if (strstr(actions, "trash") != NULL) o->actions |= TIKU_TRK_DROP_ACTION_TRASH;
    }
    if (types != NULL) {
        o->has_refs = strstr(types, "application/x-vnd.Be-directory-ref") != NULL ||
                      strstr(types, "text/uri-list") != NULL;
        o->has_text = strstr(types, "text/plain") != NULL;
        o->has_bitmap = strstr(types, "image/") != NULL;
    }
    return 0;
}

tiku_trk_drop_result_t
tiku_trk_foreign_drop(const tiku_trk_foreign_offer_t *o, int directory,
                      int trash, int root, unsigned requested)
{
    tiku_trk_drop_result_t r = { TIKU_TRK_DROP_REPLY_REJECT, 0, 0, 0 };
    unsigned action;
    if (o == NULL || root || (!directory && !trash)) return r;
    if (trash && (o->actions & TIKU_TRK_DROP_ACTION_TRASH)) {
        r.reply = TIKU_TRK_DROP_REPLY_TRASH_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_TRASH;
        r.accepted = 1;
        return r;
    }
    if (!directory) return r;
    action = requested & o->actions;
    if (action == 0) action = (o->actions & TIKU_TRK_DROP_ACTION_COPY) ?
                               TIKU_TRK_DROP_ACTION_COPY : o->actions;
    if (action & TIKU_TRK_DROP_ACTION_MOVE) {
        r.reply = TIKU_TRK_DROP_REPLY_MOVE_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_MOVE;
    } else if (action & TIKU_TRK_DROP_ACTION_COPY) {
        r.reply = TIKU_TRK_DROP_REPLY_COPY_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_COPY;
    } else if (action & TIKU_TRK_DROP_ACTION_LINK) {
        r.reply = TIKU_TRK_DROP_REPLY_COPY_TARGET;
        r.chosen_action = TIKU_TRK_DROP_ACTION_LINK;
    } else return r;
    r.create_clipping = o->promise || o->has_text || o->has_bitmap;
    r.accepted = o->has_refs || o->has_text || o->has_bitmap || o->promise;
    if (!r.accepted) r.reply = TIKU_TRK_DROP_REPLY_REJECT;
    return r;
}
