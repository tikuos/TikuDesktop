/* A failed transport write cannot report successful delivery. */
#include "tiku_remote.h"
#include "tiku_dl.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int pair[2], fails = 0;
    signal(SIGPIPE, SIG_IGN);
    tiku_remote_client_t client = {0};
    tiku_msg_t *msg = tiku_msg_new(1);
    assert(msg);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    client.fd = pair[0]; close(pair[1]);
    fails += tiku_remote_say(&client, msg) != 0;
    assert(client.fd == -1);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    client.fd = pair[0]; close(pair[1]);
    tiku_dl_t *dl = tiku_dl_new();
    assert(dl);
    fails += tiku_remote_draw(&client, 1, dl, 10, 10) != 0;
    assert(client.fd == -1);
    tiku_dl_free(dl); tiku_msg_free(msg);
    puts(fails ? "FAIL D-169" : "PASS D-169");
    return !!fails;
}
