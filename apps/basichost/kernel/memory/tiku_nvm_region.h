/* tiku_nvm_region.h -- the host's stand-in for
 * kernel/memory/tiku_nvm_region.h, for the BASIC interpreter compiled on
 * the host, which has no carved NVM region.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HOST_NVM_REGION_STUB_H_
#define TIKU_HOST_NVM_REGION_STUB_H_
/* No region selects the byte-writable persist buffers, which
 * tiku_basic_config.h places in plain .bss on the host: nothing is kept
 * between runs. */
#define TIKU_NVM_HAS_REGION 0
#endif
