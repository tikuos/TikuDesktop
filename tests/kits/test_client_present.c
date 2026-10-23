/* Pixel presentation uses the same success convention as command lists. */
#include <tiku_client.c>
#include <sys/socket.h>
#include <assert.h>
int main(void)
{
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    client_ctx_t ctx = {0};
    ctx.remote.fd = pair[0];
    uint32_t pixel = 0xff123456;
    int result = svc_present(&ctx, 1, NULL, &pixel, 1, 1);
    close(pair[0]); close(pair[1]);
    puts(result == 1 ? "PASS: D-172" : "FAIL: D-172");
    return result != 1;
}
