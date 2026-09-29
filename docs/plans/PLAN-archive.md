# TikuArchive: opening the archives files arrive in

*A plan for listing, browsing and extracting archives on TikuDesktop:
the ZIPs vendors ship SDKs in, the tarballs toolchains come as, and the
compressed files in between.  2026-09-28.*

## 1. What arrives, and what reading it needs

| Archive | Where it comes from | Needs | Have |
|---|---|---|---|
| `.zip` | vendor SDKs, downloads, mail | ZIP container, stored and DEFLATE, ZIP64, data descriptors, UTF-8 and CP437 names, Unix modes | DEFLATE, whole-buffer and a bit at a time |
| `.tar.gz`, `.tgz` | source releases, toolchains | gzip (RFC 1952) and tar: ustar, pax, GNU long names | no |
| `.tar` | backups, raw releases | tar | no |
| `.gz` | one compressed file, logs | gzip, multi-member | no |
| `.tar.xz`, `.xz` | kernels, toolchains | LZMA2 in the xz container | no |
| `.tar.bz2`, `.bz2` | older source releases | bzip2 (BWT, Huffman) | no |
| `.tar.zst`, `.zst` | newer distributions | zstd (FSE, Huffman) | no |
| `.7z` | Windows downloads | 7z container, LZMA and LZMA2, BCJ filters, AES | no |
| encrypted `.zip` | shared with a password | ZipCrypto; WinZip AES (PBKDF2-SHA1, AES-CTR, HMAC) | AES, SHA-1, HMAC, PBKDF2 in the support kit |
| `.rar` | Windows downloads | an unpublished compression format | not planned |

Zips from Windows and 7-Zip also use Deflate64 (method 9): DEFLATE with a
64 KB window and longer matches, cheap to read beside DEFLATE itself.

## 2. Shape

A kit and an app, as TikuPDF is: `kits/translation/tiku_archive*` opens
an archive, lists what is in it and extracts it, with no window, and is
tested on the host against files; `apps/archive` is TikuArchive, the
window.  Applications are processes of their own, which is where a
reader of files from strangers belongs.

**Streams, not buffers.**  Each layer reads from the one below it as it
needs to -- a file, gzip over it, tar over that -- so a 4 GB tarball is
listed and extracted in the same few hundred kilobytes.  That needs a
DEFLATE decoder that stops when its output is full and resumes: a new,
table-driven one; the bit-at-a-time decoder PNG and PDF use stays as it
is.  Listing a ZIP reads its central directory and nothing else; listing
a compressed tar has to read it through once, on a worker, with progress.

**Extraction is the boundary.**  Names come from strangers, so:

- Everything lands in a new folder named for the archive, made fresh
  (`name 2` beside one already there), never merged into an old one.
- A leading `/` or drive letter is taken off, as tar does; a name with a
  `..`, a control character, or longer than the disk allows is refused
  and reported, not rewritten.
- Files are made by walking down from the new folder with `openat` and
  `O_NOFOLLOW`, so no link -- in the archive or put there meanwhile -- is
  followed on the way down.
- A symbolic link is made only when its target is relative and stays
  inside the folder; a hard link only to a file this extraction made;
  devices and FIFOs never.
- Modes keep `rwx` alone; set-id and sticky bits are dropped.
  Directories get their modes and times after their contents.
- Sizes are checked as bytes arrive: an entry that inflates past the size
  it states stops there, ZIP entries whose data overlaps (the zip-bomb
  construction) are refused, and every CRC-32 is checked.
- A later entry with a name already made replaces it, as tar means; the
  report says so, which also catches names that differ only in case on a
  Mac's disk.

**The window** is a list like Tracker's: Name, Size, Packed, Modified.
Folders open in place under a path bar; toolbar icons for Open, Enclosing
Folder, Extract All and Extract; a find field narrows the folder shown.
Extract All puts the new folder beside the archive, Extract To... asks
where through the Save panel.  Opening a file inside the archive extracts
it to a private temporary folder and opens it in its program, as a
double-click in Tracker would.  Progress and Cancel sit in the status
line; a finished extraction offers Show in Tracker.  Tracker opens
archives in it; its icons are drawn as the set's are; it has a help page.

## 3. Milestones

**1 -- ZIP, tar and gzip.**  Streaming DEFLATE with Deflate64, CRC-32,
ZIP (ZIP64, data descriptors, Info-ZIP Unicode paths, Unix modes and
links, extended times), tar (ustar, pax, GNU long names), gzip (multi-
member, tar found inside by its header).  Safe extraction.  TikuArchive
with its list, path bar, extraction, Open and progress.  Types,
associations, icons and help.  Tests from fixtures Python's `zipfile`
and `tarfile`, Info-ZIP `zip` and bsdtar make, hostile archives made by
hand, a cross-check against `unzip` and bsdtar, mutation fuzzing under
ASan, and a lane through `tiku-run`.

**2 -- bzip2 and xz.**  `.tar.bz2`, `.tbz2`, `.bz2`; `.tar.xz`, `.txz`,
`.xz` (LZMA2, with the xz container's checks); ZIP methods 12 and 95.
zstd if the milestone has room.

**3 -- Locked zips and 7z.**  ZipCrypto and WinZip AES, asked for with the
password field TikuPDF has; 7z with LZMA, LZMA2, BCJ and encrypted
headers.

**4 -- Making archives.**  Compress in Tracker and TikuArchive, to ZIP and
`.tar.gz`: this needs a DEFLATE compressor, which the kits do not have.

Not planned: RAR, whose compression has no published description, and
archives split over several files.
