/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TRK_STATE_PIPELINE_H_
#define TIKU_TRK_STATE_PIPELINE_H_

#include "tiku_trk_state.h"

typedef int (*tiku_trk_state_accept_fn)(const char *attr, void *ctx);
typedef int (*tiku_trk_state_transform_fn)(const char *attr,
                                           const void *in, size_t in_len,
                                           void *out, size_t out_capacity,
                                           size_t *out_len,
                                           void *ctx);

/* Copy selected named state attributes, optionally transforming each value.
 * A failed destination write returns -1; callers can treat the destination
 * as incomplete rather than silently claiming the copy succeeded. */
int tiku_trk_state_pipeline_copy(tiku_trk_store_t *store,
                                 const char *source, const char *dest,
                                 tiku_trk_state_accept_fn accept,
                                 tiku_trk_state_transform_fn transform,
                                 void *ctx);

#endif
