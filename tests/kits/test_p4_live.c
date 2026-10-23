/*
 * TikuDesktop -- explicit-port P4 storage-kit qualification.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Read and compare a complete scalar through the production backend. */
static void expect(tiku_backend_t *b, const char *path, const char *value)
{
    char data[256] = {0};
    int n = b->ops->read(b, path, data, sizeof data - 1);
    assert(n > 0 && n < (int)sizeof data);
    data[strcspn(data, "\r\n")] = 0;
    assert(!strcmp(data, value));
    printf("PASS: desktop reads %s = %s\n", path, data);
}

/** @brief Run read-only checks only with an explicitly supplied P4 port. */
int main(void)
{
    const char *port = getenv("TRK_P4_PORT");
    if (!port || !*port) {
        puts("SKIP: P4 hardware checks require TRK_P4_PORT");
        return 0;
    }
    tiku_backend_t *b = tiku_backend_tiku_open(port, 115200);
    assert(b);
    const tiku_identity_t *id = tiku_backend_tiku_identity(b);
    assert(id && !strcmp(id->mcu, "ESP32-P4 rev 1.3"));
    expect(b, "/sys/psram/state", "up");
    expect(b, "/sys/psram/size", "33554432");
    expect(b, "/sys/cpu/freq_change_mode", "reboot");
    tiku_model_t model;
    assert(b->ops->stat(b, "/dev/gpio/7/6", &model) == 0);
    assert(b->ops->stat(b, "/dev/gpio/7/7", &model) < 0);
    puts("PASS: desktop manifest reaches the last valid GPIO pin");
    b->ops->close(b);
    return 0;
}
