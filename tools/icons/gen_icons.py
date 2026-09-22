#!/usr/bin/env python3
"""Emit kits/translation/tiku_icons.c from assets/icons/hvif/.

Each .hex there is one #'VICN' resource body, concatenated and stripped of
its $"..." wrapping, out of Haiku's src/kits/tracker/TrackerIcons.rdef or
src/data/mime_db/<super>/<subtype>.  Two of them are carried but not
compiled in: broken_link is byte-identical to symlink, and person is the
one blob in the Haiku tree that does not parse (its stroke transformers
carry a fourth payload byte from before the `shorten` field was dropped, so
a strict reader runs off the end -- Haiku cannot import it either).

Run from the repository root: python3 tools/icons/gen_icons.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SP = os.path.join(HERE, os.pardir, os.pardir, "assets", "icons", "hvif")
# Both assets and kits are relative to the repository root.
OUT = os.path.join(HERE, os.pardir, os.pardir, "kits", "translation",
                   "tiku_icons.c")

# Inventory order: the general kinds first, then the volumes and places,
# then the specialised folders and the odds and ends.
NAMES = [
    ("folder",            "R_BeosFolderIcon-shaped MIME default for a directory"),
    ("folder_open",       "the Open Target Folder add-on's icon: folder plus arrow"),
    ("file",              "R_FileIcon"),
    ("volume",            "MIME application/x-vnd.Be-volume, == R_HardDiskIcon"),
    ("symlink",           "MIME be-symlink; R_BrokenLinkIcon is the same bytes"),
    ("trash_empty",       "R_TrashIcon"),
    ("trash_full",        "R_TrashFullIcon"),
    ("query",             "MIME Be-query, == R_QueryIcon"),
    ("application",       "R_AppIcon, registered for B_APP_MIME_TYPE"),
    ("executable",        "MIME be-elfexecutable"),
    ("query_template",    "MIME be-querytemplate, == R_QueryTemplateIcon"),
    ("text_file",         "MIME supertype text"),
    ("generic_app_file",  "MIME supertype application"),
    ("package",           "MIME haiku-package"),
    ("boot_volume",       "R_BootVolumeIcon, the nightly-build variant"),
    ("root",              "R_RootIcon"),
    ("desktop",           "R_DeskIcon"),
    ("home",              "R_HomeDirIcon"),
    ("floppy",            "R_FloppyIcon"),
    ("cd",                "R_CDIcon"),
    ("folder_system",     "R_BeosFolderIcon"),
    ("folder_apps",       "R_AppsDirIcon"),
    ("folder_prefs",      "R_PrefsDirIcon"),
    ("folder_config",     "R_ConfigDirIcon"),
    ("folder_downloads",  "R_DownloadDirIcon"),
    ("folder_mail",       "R_MailDirIcon"),
    ("folder_people",     "R_PersonDirIcon"),
    ("folder_queries",    "R_QueryDirIcon"),
    ("folder_develop",    "R_DevelopDirIcon"),
    ("folder_fonts",      "R_FontDirIcon"),
    ("folder_virtual",    "MIME haiku-virtual-directory"),
    ("bookmark",          "R_BookmarkIcon"),
    ("printer",           "R_PrinterIcon, == R_GenericPrinterIcon"),
    ("share",             "R_ShareIcon"),
]

HEAD = '''/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_icons.c - the embedded HVIF icon set, and a render cache.
 *
 * GENERATED from the blobs in Haiku's src/kits/tracker/TrackerIcons.rdef and
 * src/data/mime_db/.  Edit the generator, not this file.
 *
 * Artwork from Haiku (haiku-os.org), MIT licensed; TrackerIcons.rdef
 * additionally carries the Open Tracker License (Be Incorporated,
 * 1991-2000), reproduced in that file's TrackerIcons.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include "tiku_hvif.h"
#include "tiku_icons.h"

'''

TAIL = '''
int
tiku_icons_count(void)
{
    return (int)(sizeof tiku_icon_table / sizeof tiku_icon_table[0]);
}

const tiku_icon_t *
tiku_icons_at(int i)
{
    if (i < 0 || i >= tiku_icons_count()) {
        return NULL;
    }
    return &tiku_icon_table[i];
}

const tiku_icon_t *
tiku_icons_find(const char *name)
{
    int i, n = tiku_icons_count();

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(tiku_icon_table[i].name, name) == 0) {
            return &tiku_icon_table[i];
        }
    }
    return NULL;
}

/* One rendered bitmap per icon per size.  A pose view asks for the same two
 * or three sizes for the whole window, so the slot count is generous; the
 * oldest slot goes when a fourth size shows up. */
#define TIKU_ICONS_SLOTS 4

typedef struct {
    int       size;
    uint32_t *bmp;
} tiku_icon_slot_t;

static tiku_icon_slot_t
tiku_icon_cache[TIKU_ICON_COUNT][TIKU_ICONS_SLOTS];

const uint32_t *
tiku_icons_bitmap(const char *name, int size)
{
    const tiku_icon_t *ic = tiku_icons_find(name);
    tiku_icon_slot_t *row;
    tiku_hvif_t *parsed;
    uint32_t *bmp;
    int idx, i;

    if (ic == NULL || size <= 0 || size > 1024) {
        return NULL;
    }
    idx = (int)(ic - tiku_icon_table);
    row = tiku_icon_cache[idx];
    for (i = 0; i < TIKU_ICONS_SLOTS; i++) {
        if (row[i].bmp != NULL && row[i].size == size) {
            return row[i].bmp;
        }
    }
    parsed = tiku_hvif_parse(ic->data, ic->len, NULL, 0);
    if (parsed == NULL) {
        return NULL;
    }
    bmp = malloc((size_t)size * (size_t)size * sizeof *bmp);
    if (bmp == NULL || tiku_hvif_render(parsed, bmp, size, size) != 0) {
        free(bmp);
        tiku_hvif_free(parsed);
        return NULL;
    }
    tiku_hvif_free(parsed);
    for (i = 0; i < TIKU_ICONS_SLOTS; i++) {
        if (row[i].bmp == NULL) {
            break;
        }
    }
    if (i == TIKU_ICONS_SLOTS) {
        i = 0;
        free(row[0].bmp);
        memmove(&row[0], &row[1],
                (TIKU_ICONS_SLOTS - 1) * sizeof row[0]);
        i = TIKU_ICONS_SLOTS - 1;
    }
    row[i].size = size;
    row[i].bmp = bmp;
    return bmp;
}

void
tiku_icons_flush(void)
{
    int i, k;

    for (i = 0; i < TIKU_ICON_COUNT; i++) {
        for (k = 0; k < TIKU_ICONS_SLOTS; k++) {
            free(tiku_icon_cache[i][k].bmp);
            tiku_icon_cache[i][k].bmp = NULL;
            tiku_icon_cache[i][k].size = 0;
        }
    }
}
'''


def main():
    out = [HEAD]
    total = 0
    for name, note in NAMES:
        path = os.path.join(SP, name + ".hex")
        raw = bytes.fromhex(open(path).read().replace(" ", "").replace("\n", ""))
        assert raw[:4] == b"ncif", name
        total += len(raw)
        out.append("/* %s -- %s */\n" % (name, note))
        out.append("static const uint8_t tiku_trk_icon_%s[%d] = {\n" %
                   (name, len(raw)))
        for i in range(0, len(raw), 12):
            chunk = raw[i:i + 12]
            out.append("    " + " ".join("0x%02X," % b for b in chunk) + "\n")
        out.append("};\n\n")

    out.append("static const tiku_icon_t "
               "tiku_icon_table[TIKU_ICON_COUNT] = {\n")
    for name, _ in NAMES:
        out.append('    { "%s",%s tiku_trk_icon_%s,\n'
                   '      sizeof tiku_trk_icon_%s },\n'
                   % (name, " " * max(1, 18 - len(name)), name, name))
    out.append("};\n")
    out.append(TAIL)

    open(OUT, "w").write("".join(out))
    print("icons: %d, bytes: %d" % (len(NAMES), total))


main()
