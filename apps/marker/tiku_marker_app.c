/* tiku_marker_app.c -- TikuMarker, a read-only Markdown viewer: the
 * reader's document window started under TikuMarker's profile.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_marker.h"

const tiku_reader_profile_t tiku_marker_profile = {
    .name = "TikuMarker", .icon = "marker",
    .bookmark_env = "TIKU_MARKER_BOOKMARKS",
    .bookmark_file = "TikuMarkerBookmarks",
    .help_page = "apps/marker.html",
    .welcome_html = "<h1>TikuMarker</h1>"
        "<p>Read Markdown documents, notes and project guides.</p>"
        "<h2>Open a Markdown file</h2>"
        "<p>Choose File &gt; Open or press Ctrl+O to open a .md or "
        ".markdown file. Headings, lists, tables and code blocks appear "
        "as a formatted document.</p>"
        "<h2>Read and navigate</h2>"
        "<p>Use View &gt; Outline to jump to a heading, Ctrl+F to find "
        "text, or View &gt; Toggle bookmark to save a reading position. "
        "View also offers larger text and a comfortable reading width.</p>"
        "<h2>Your files stay unchanged</h2>"
        "<p>TikuMarker is a viewer, not an editor. To make changes, open "
        "the file with Editor, save it, then press Ctrl+R here to reload. "
        "Local Markdown and HTML links are supported; scripts and web "
        "requests are not run.</p>"
};

/** @brief Start the shared document viewer as TikuMarker. */
static int start(void **state, const tiku_app_services_t *services)
{
    return tiku_reader_app_start(state, services, &tiku_marker_profile);
}

static const tiku_app_descriptor_t descriptor = {
    .id = "marker", .name = "TikuMarker", .start = start,
    .stop = tiku_reader_app_stop, .event_in = tiku_reader_app_event,
    .tick = tiku_reader_app_tick, .pick = tiku_reader_app_pick,
    .picked = tiku_reader_app_picked, .closed = tiku_reader_app_closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
__attribute__((visibility("default"))) const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
