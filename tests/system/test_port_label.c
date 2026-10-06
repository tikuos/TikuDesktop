/* The Devices menu names a port by its leaf without the "cu." or "tty." a
 * Mac puts first, and cuts a long leaf to its letters, an ellipsis and as
 * many of its last characters as tell it from the other ports listed: the
 * two ports of one J-Link probe and the port of a second probe read alike
 * for their first fourteen characters.  SPDX-License-Identifier:
 * Apache-2.0 */
#include "../../system/desktop/tiku_trk_desktop_private.h"

#include <stdio.h>
#include <string.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/** @brief The label of @p where among the @p n leaves in @p all. */
static const char *
label(const char *where, const char *const *all, int n)
{
    static char out[64];

    device_port_label(where, all, n, out, sizeof out);
    return out;
}

int
main(void)
{
    static const char *const rig[] = {
        "cu.usbmodem0010577569361", "cu.usbmodem0010577569363",
        "cu.usbmodem0011600029361", "cu.usbserial-110"
    };

    printf("naming ports in the Devices menu\n");
    check(strcmp(label(rig[1], rig, 4),
                 "usbmodem\xe2\x80\xa6" "9363") == 0,
          "a long leaf keeps its letters and its last four characters");
    check(strcmp(label(rig[0], rig, 4),
                 "usbmodem\xe2\x80\xa6" "69361") == 0 &&
          strcmp(label(rig[2], rig, 4),
                 "usbmodem\xe2\x80\xa6" "29361") == 0,
          "two leaves ending alike keep as many more as tell them apart");
    check(strcmp(label(rig[3], rig, 4), "usbserial-110") == 0,
          "a short leaf is shown whole, without the cu.");
    check(strcmp(label("tty.usbmodem14101", NULL, 0), "usbmodem14101") == 0,
          "tty. goes as cu. does");
    check(strcmp(label("ttyACM0", NULL, 0), "ttyACM0") == 0 &&
          strcmp(label("ttyUSB12", NULL, 0), "ttyUSB12") == 0,
          "a Linux name is left as it is");
    check(strcmp(label(rig[1], rig + 1, 1),
                 "usbmodem\xe2\x80\xa6" "9363") == 0,
          "a port listed alone is told from nothing but itself");
    printf("%s\n", fails ? "FAILURES" : "every port reads apart");
    return fails ? 1 : 0;
}
