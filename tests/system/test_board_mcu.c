/* Board identity copy respects small output buffers. */
#define tiku_mount_slots audit_slots
#define tiku_mount_prefix_at audit_prefix
#define tiku_mount_at audit_mount
#define tiku_backend_tiku_identity audit_identity
#include "../../system/desktop/tiku_trk_desktop_boardrun.c"
#include <assert.h>
int audit_slots(const tiku_backend_t *b) { (void)b; return 1; }
const char *audit_prefix(const tiku_backend_t *b, int i)
{ (void)b; (void)i; return "/devices/board"; }
tiku_backend_t *audit_mount(const tiku_backend_t *b, int i)
{ (void)i; return (tiku_backend_t *)b; }
const tiku_identity_t *audit_identity(const tiku_backend_t *b)
{
    static tiku_identity_t id = {.mcu = "nRF54LM20B"};
    (void)b; return &id;
}
int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    app->backend = (tiku_backend_t *)app;
    for (size_t n = 1; n <= 12; n++) {
        char out[16];
        memset(out, 'X', sizeof out); out[15] = 0;
        int len = desktop_board_mcu(app, "board", out, n);
        if ((size_t)len >= n || out[len] || out[n] != 'X') {
            free(app); puts("FAIL: D-058"); return 1;
        }
    }
    free(app); puts("PASS: D-058"); return 0;
}
