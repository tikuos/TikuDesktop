/* Standard rates are applied exactly; unsupported rates are refused. */
#define _XOPEN_SOURCE 600
#include "tiku_conduct.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
int main(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY), fails = 0;
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    int slave = tiku_conduct_open_tty(ptsname(master), 4800);
    struct termios tio;
    assert(slave >= 0 && tcgetattr(slave, &tio) == 0);
    fails += cfgetospeed(&tio) != B4800; close(slave);
#ifndef __APPLE__
    slave = tiku_conduct_open_tty(ptsname(master), 1234567);
    fails += slave >= 0;
    if (slave >= 0) close(slave);
#endif
    close(master);
    puts(fails ? "FAIL D-171" : "PASS D-171"); return !!fails;
}
