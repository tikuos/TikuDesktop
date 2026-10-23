/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_shell_product.h - product identity, defaults and optional shell services.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_PRODUCT_H_
#define TIKU_SHELL_PRODUCT_H_
/** @brief Enable board namespace, channel and device services. */
#define TIKU_SHELL_BOARD_SERVICES 1
/** @brief Enable remote text input, accessibility, links and printing. */
#define TIKU_SHELL_REMOTE_SERVICES 0
/** @brief Enable note placement with automatic title-tab hiding. */
#define TIKU_SHELL_NOTE_TABS 0
/** @brief Maximum advertised content types per application handler. */
#define TIKU_SHELL_APP_TYPES_MAX 8
/** @brief Font size for the About product name. */
#define TIKU_SHELL_ABOUT_FONT_SIZE 28
/** @brief Product display name. */
#define TIKU_SHELL_PRODUCT_NAME "TikuOS"
/** @brief File manager executable and display name. */
#define TIKU_SHELL_TRACKER_NAME "TikuTracker"
/** @brief Desktop executable and display name. */
#define TIKU_SHELL_DESKTOP_NAME "TikuDesktop"
/** @brief Installed help-directory format relative to an executable root. */
#define TIKU_SHELL_INSTALLED_HELP_PATH "%s/share/tiku/help"
/** @brief User command-file path format relative to the home directory. */
#define TIKU_SHELL_COMMAND_PATH "%s/.config/tracker/command"
/** @brief Product menu label. */
#define TIKU_SHELL_MENU_NAME "Tiku"
/** @brief Version output format with build date and time arguments. */
#define TIKU_SHELL_VERSION_FORMAT "TikuDesktop, built %s %s\n"
/** @brief Settings-file path format relative to the home directory. */
#define TIKU_SHELL_SETTINGS_PATH "%s/.config/TrackerSettings"
/** @brief Device state-directory format relative to the home directory. */
#define TIKU_SHELL_DEVICE_STATE_PATH "%s/.local/share/tracker/devices"
/** @brief Native desktop host-window title. */
#define TIKU_SHELL_DESKTOP_TITLE "TikuOS Desktop"
/** @brief System-wide Deskbar entry directory. */
#define TIKU_SHELL_DESKBAR_PATH "/usr/share/tiku-tracker/deskbar"
/** @brief User file-type configuration path format. */
#define TIKU_SHELL_TYPE_SETTINGS_PATH "%s/.config/tracker/types"
/** @brief User application registry path format. */
#define TIKU_SHELL_APP_SETTINGS_PATH "%s/.config/tracker/apps"
/** @brief System-wide application registry directory. */
#define TIKU_SHELL_INSTALLED_APPS_PATH "/usr/share/tiku-tracker/apps"
/** @brief User add-on directory path format. */
#define TIKU_SHELL_ADDONS_PATH "%s/.config/tracker/add-ons"
/** @brief User add-on shortcut-file path format. */
#define TIKU_SHELL_ADDON_SHORTCUTS_PATH "%s/.config/tracker/addon-shortcuts"
/** @brief Notice displayed after the desktop restarts. */
#define TIKU_SHELL_RESTART_MESSAGE "TikuDesktop stopped and was started again.  "
/** @brief User icon-directory path format. */
#define TIKU_SHELL_USER_ICONS_PATH "%s/.config/tracker/icons"
/** @brief Mount diagnostic line format for the selected services. */
#define TIKU_SHELL_MOUNT_REPORT_FORMAT "  %s%s%s\n"
/** @brief Settings notice for changes applied at the next desktop start. */
#define TIKU_SHELL_RESTART_SETTINGS_MESSAGE "Changes take effect the next time you start TikuDesktop."
/** @brief Background-image configuration suffix relative to the home directory. */
#define TIKU_SHELL_BACKGROUND_PATH "/.config/tracker/desktop-background"
/** @brief Production diagnostic naming the development script interpreter. */
#define TIKU_SHELL_SCRIPT_REFUSAL_MESSAGE "refused).\ntracker: scripts run under build/dev/TikuDesktop, "
/** @brief Version label displayed in About. */
#define TIKU_SHELL_ABOUT_VERSION "0.1"
/** @brief Desktop name followed by the space preceding its version. */
#define TIKU_SHELL_DESKTOP_NAME_SPACE "TikuDesktop "
/** @brief About copyright and project-address text. */
#define TIKU_SHELL_ABOUT_COPYRIGHT "(c) 2026 the TikuOS project   http://tiku-os.org"
/** @brief Desktop executable path format relative to its installation root. */
#define TIKU_SHELL_DESKTOP_EXECUTABLE_PATH "%s/TikuDesktop"
/** @brief Crash report identity and timestamp format. */
#define TIKU_SHELL_CRASH_REPORT_FORMAT "TikuDesktop\nended: %04d-%02d-%02d %02d:%02d:%02d\n"
/** @brief Document-opening status format. */
#define TIKU_SHELL_OPENING_FORMAT "Opening %s on TikuDesktop"
/** @brief Document-opening failure diagnostic format. */
#define TIKU_SHELL_OPEN_FAILED_FORMAT "Could not open %s. Start TikuDesktop and check the app is installed."
/** @brief Production binary path used by system regression tests. */
#define TIKU_SHELL_TEST_RUNTIME_BINARY "build/runtime/system/TikuDesktop"
/** @brief Crash-report fixture command used by system regression tests. */
#define TIKU_SHELL_TEST_CRASH_COMMAND "echo TikuDesktop > %s/crash-20260101-100000-TikuDesktop-$$.txt;"
/** @brief Desktop identity fragment in crash-report filenames. */
#define TIKU_SHELL_CRASH_NAME_FRAGMENT "-TikuDesktop-"
/** @brief Application-description fixture text for SDK boundary tests. */
#define TIKU_SHELL_TEST_APPLICATION_DESCRIPTION "a TikuOS application wears the application "
/** @brief Development binary path used by system regression tests. */
#define TIKU_SHELL_TEST_DESKTOP_BINARY "build/dev/TikuDesktop"
/** @brief Initial content-type and display-label pairs for File Associations. */
#define TIKU_SHELL_ASSOCIATION_LABELS { \
        {"text/plain", "Plain text (.txt)"}, \
        {"text/markdown", "Markdown document (.md, .markdown)"}, \
        {"text/html", "HTML document (.html, .htm)"}, \
        {"text/x-basic", "BASIC source (.bas)"}, \
        {"application/x-tiku-project", "Tiku project (.tiku)"}, \
        {"application/x-tiku-richdoc", "TikuWrite document (.tkrd)"}, \
        {"application/x-kicad-schematic", "KiCad schematic (.kicad_sch)"}, \
        {"application/x-excellon", "Drill drawing (.drl, .xnc, .xln)"}, \
        {"application/x-gerber", "Gerber artwork (.gbr and layer files)"}, \
        {"application/x-gerber-job", "Gerber job (.gbrjob)"}, \
        {"application/x-schematic", \
         "Schematic (.sch; content checked on open)"}, \
        {"image/png", "PNG image (.png)"}, \
        {"image/jpeg", "JPEG image (.jpg, .jpeg)"}, \
        {"image/bmp", "Bitmap image (.bmp)"}, \
        {"image/x-ms-bmp", "Windows bitmap"}, \
        {"application/pdf", "PDF document (.pdf)"}, \
        {"application/zip", "ZIP archive (.zip)"}, \
        {"application/x-tar", "Tar archive (.tar)"}, \
        {"application/x-compressed-tar", "Compressed tar (.tar.gz, .tgz)"}, \
        {"application/gzip", "Gzip-compressed file (.gz)"}, \
        {"application/x-tikusdr", "TikuSDR recording (.sdrlog)"} \
    }
/** @brief Initial type, description, icon and column tuples in registration order. */
#define TIKU_SHELL_TYPE_DEFAULTS { \
    {"text/", "Text file", "text_file", "size,modified,permissions"}, \
    {"text/plain", "Text file", "text_file", "size,modified,permissions,document"}, \
    {"text/x-basic", "BASIC source", "text_file", "size,modified,permissions,document"}, \
    {"application/x-tiku-project", "Tiku project", "file", "size,modified,document"}, \
    {"text/x-source", "Source file", "text_file", "size,modified,permissions"}, \
    {"image/", "Image", "file", "size,modified"}, \
    {"image/png", "PNG image", "file", "size,modified"}, \
    {"audio/", "Audio", "file", "size,modified"}, \
    {"audio/mpeg", "MPEG audio", "file", "size,modified"}, \
    {"video/", "Video", "file", "size,modified"}, \
    {"video/mp4", "MPEG-4 video", "file", "size,modified"}, \
    {"application/pdf", "PDF document", "file", "size,modified"}, \
    {"application/x-tiku-richdoc", "TikuWrite document", "doc_file", "size,modified,document"}, \
    {"application/zip", "ZIP archive", "archive_file", "size,modified"}, \
    {"application/x-tar", "Tar archive", "archive_file", "size,modified"}, \
    {"application/x-compressed-tar", "Compressed tar archive", "archive_file", "size,modified"}, \
    {"application/gzip", "Gzip-compressed file", "archive_file", "size,modified"}, \
    {"application/x-docx", "Word document", "doc_file", "size,modified"}, \
    {"application/vnd.oasis.opendocument.text", "OpenDocument text", "doc_file", "size,modified"}, \
    {"application/x-pptx", "PowerPoint presentation", "slides_file", "size,modified"}, \
    {"application/x-tikusdr", "TikuSDR recording", "sdr_file", "size,modified"}, \
    {"tiku-", "Device node", "device", "value,cap,modified"}, \
    {"application/x-vnd.Be-bookmark", "Bookmark", "bookmark", "attr:META:url"}, \
    {"application/x-person", "Person", "file", "attr:META:name,attr:META:email"}, \
    {"text/x-email", "E-mail", "file", "attr:MAIL:subject,attr:MAIL:from"}, \
    {"text/x-source", "Source file", "text_file", "size,modified,permissions,attr:user.trk.note"}, \
}
#endif
