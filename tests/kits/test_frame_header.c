/* Console framing constants are available from a complete standalone
 * header that names no decoder result. */
#include "tiku_frame.h"
#if defined(TIKU_FRAME_TEXT) || defined(TIKU_FRAME_TAKEN) || \
    defined(TIKU_FRAME_WHOLE)
#error "tiku_frame.h names the results of a decoder that does not exist"
#endif
int main(void) { return TIKU_FRAME_END != 0xc0u; }
