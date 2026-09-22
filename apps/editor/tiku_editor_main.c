/* Standalone Editor entry point; modules and tests use the app descriptor.
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_editor_private.h"

int
main(int argc, char **argv)
{
    if (argc > 1) tiku_editor_startup_path(argv[1]);
    return tiku_client_run(&tiku_edit_app);
}
