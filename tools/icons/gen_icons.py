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
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from source_layout import source_path

HERE = os.path.dirname(os.path.abspath(__file__))
SP = os.path.join(HERE, os.pardir, os.pardir, "assets", "icons", "hvif")
# Both assets and kits are relative to the repository root.
OUT = str(source_path(Path(HERE).parents[1], "kits/translation/tiku_icons.c"))

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

def main():
    source = open(OUT, encoding="utf-8").read()
    region = re.search(r"/\* folder --.*?static const tiku_icon_t "
                       r"tiku_icon_table\[TIKU_ICON_COUNT\] = \{.*?\n\};\n",
                       source, re.S)
    if region is None:
        raise ValueError("Cannot locate the generated icon data")
    out = [source[:region.start()]]
    total = 0
    for name, note in NAMES:
        path = os.path.join(SP, name + ".hex")
        raw = bytes.fromhex(open(path).read().replace(" ", "").replace("\n", ""))
        assert raw[:4] == b"ncif", name
        total += len(raw)
        out.append("/* %s -- %s */\n" % (name, note))
        out.append("static const uint8_t tiku_icon_%s[%d] = {\n" %
                   (name, len(raw)))
        for i in range(0, len(raw), 12):
            chunk = raw[i:i + 12]
            out.append("    " + " ".join("0x%02X," % b for b in chunk) + "\n")
        out.append("};\n\n")

    out.append("static const tiku_icon_t "
               "tiku_icon_table[TIKU_ICON_COUNT] = {\n")
    for name, _ in NAMES:
        out.append('    { "%s",%s tiku_icon_%s,\n'
                   '      sizeof tiku_icon_%s },\n'
                   % (name, " " * max(1, 18 - len(name)), name, name))
    out.append("};\n")
    out.append(source[region.end():])

    open(OUT, "w").write("".join(out))
    print("icons: %d, bytes: %d" % (len(NAMES), total))


if __name__ == "__main__":
    main()
