/* Query replies do not consume unrelated watch notifications. */
#define _XOPEN_SOURCE 600
#include "../../kits/application/tiku_conduct.c"
#include <assert.h>
int main(void)
{
    int pair[2], fails = 0;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    tiku_conduct_client_t c = {.fd = pair[0]};
    assert(set_nonblock(pair[0]) == 0);
    char notice[TIKU_CONDUCT_ARG + TIKU_CONDUCT_TEXT] = {0};
    strcpy(notice, "status"); strcpy(notice + TIKU_CONDUCT_ARG, "changed");
    send_msg(&pair[1], TIKU_CMSG_TOLD, notice, sizeof notice);
    unsigned char answer[8 + TIKU_CONDUCT_TEXT] = {0};
    int32_t reply[] = {1, 42}; memcpy(answer, reply, sizeof reply);
    send_msg(&pair[1], TIKU_CMSG_ANSWER, answer, sizeof answer);
    int value = 0; char name[64] = {0}, text[64] = {0};
    assert(tiku_conduct_query(&c, 0, 0, 0, NULL, &value, NULL, 0));
    assert(value == 42);
    fails += !tiku_conduct_told(&c, name, sizeof name, text, sizeof text, 10);
    fails += strcmp(name, "status") != 0 || strcmp(text, "changed") != 0;
    tiku_conduct_disconnect(&c); close(pair[1]);
    puts(fails ? "FAIL D-168" : "PASS D-168");
    return !!fails;
}
