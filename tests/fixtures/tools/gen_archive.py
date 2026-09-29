#!/usr/bin/env python3
"""The archives test_archive reads, made by Python's zipfile, tarfile and
gzip -- never by the kit -- and, where those will not write a form, by
hand here: ZIP64 records, a self-extractor's stub, a v7 tar header, GNU's
base-256 sizes, a gzip header with every field.  And the hostile ones.

    python3 tests/fixtures/tools/gen_archive.py tests/fixtures/archive.inc

Each archive is base64 beside a manifest the test checks it against, a
line an entry: kind (f d l h o), mode, size, CRC-32 of its bytes, flags
(R refused, U unreadable, M metadata, - none), path, and a link's target
after a tab.  Everything is from a fixed seed and fixed times, so a run
writes the same file.
SPDX-License-Identifier: Apache-2.0
"""
import base64, gzip, io, struct, sys, tarfile, time, zipfile, zlib

OUT = sys.argv[1]
T0 = 1714566896                 # 2024-05-01 12:34:56 UTC, every mtime
DOS = (2024, 5, 1, 12, 34, 56)


def lcg_text(seed, n):
    """Text that compresses, from a fixed seed: words, spaces, lines."""
    words = [b"archive", b"tiku", b"stream", b"window", b"deflate",
             b"header", b"entry", b"folder", b"0123456789", b"desk"]
    x, out = seed, bytearray()
    while len(out) < n:
        x = (x * 1103515245 + 12345) & 0x7fffffff
        out += words[(x >> 16) % len(words)]
        out += b"\n" if (x >> 8) % 11 == 0 else b" "
    return bytes(out[:n])


def lcg_bytes(seed, n):
    """Bytes that do not compress, from a fixed seed."""
    x, out = seed, bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7fffffff
        out.append((x >> 16) & 0xff)
    return bytes(out)


class Fixture:
    """An archive, its extension, and the manifest lines it must list."""
    def __init__(self, name, ext, doc):
        self.name, self.ext, self.doc = name, ext, doc
        self.lines, self.data = [], b""

    def entry(self, kind, path, data=b"", mode=None, size=None, flags="-",
              link=None):
        crc = zlib.crc32(data) & 0xffffffff
        size = len(data) if size is None else size
        mode = "-" if mode is None else "%o" % mode
        line = "%s %s %d %08x %s %s" % (kind, mode, size, crc, flags, path)
        self.lines.append(line + ("\t" + link if link else ""))


FIXTURES = []


def fixture(name, ext, doc):
    f = Fixture(name, ext, doc)
    FIXTURES.append(f)
    return f


def zinfo(name, mode=0o644, method=zipfile.ZIP_DEFLATED, kind=0o100000):
    zi = zipfile.ZipInfo(name, DOS)
    zi.compress_type = method
    zi.create_system = 3
    zi.external_attr = (kind | mode) << 16
    return zi


# ------------------------------------------------------------------ ZIP

def zip_features():
    f = fixture("zip_features", ".zip",
                "Python's zipfile: folders, stored and deflated files, a link, "
                "UTF-8, CP437 and Info-ZIP Unicode names, an extended time")
    buf = io.BytesIO()
    readme, big = lcg_text(1, 5000), lcg_text(2, 80000) + lcg_bytes(3, 3000)
    with zipfile.ZipFile(buf, "w") as z:
        z.comment = b"made for test_archive"
        zi = zinfo("docs/", 0o755, zipfile.ZIP_STORED, 0o040000)
        z.writestr(zi, b"")
        f.entry("d", "docs", mode=0o755)
        z.writestr(zinfo("docs/readme.txt"), readme)
        f.entry("f", "docs/readme.txt", readme, 0o644)
        z.writestr(zinfo("bin/tool", 0o755, zipfile.ZIP_STORED), b"#!/bin/sh\n")
        f.entry("f", "bin/tool", b"#!/bin/sh\n", 0o755)
        z.writestr(zinfo("data/big.bin"), big)
        f.entry("f", "data/big.bin", big, 0o644)
        z.writestr(zinfo("link", 0o777, zipfile.ZIP_STORED, 0o120000),
                   b"docs/readme.txt")
        f.entry("l", "link", mode=0o777, size=15, link="docs/readme.txt")
        z.writestr(zinfo("na\u00efve/caf\u00e9.txt"), b"utf-8 name\n")
        f.entry("f", "na\u00efve/caf\u00e9.txt", b"utf-8 name\n", 0o644)
        # CP437, no UTF-8 flag: 0x82 is e-acute there.
        zi = zinfo("\u00e9t\u00e9.txt")
        zi.flag_bits &= ~0x800
        z.writestr(zi, b"cp437 name\n")
        f.entry("f", "\u00e9t\u00e9.txt", b"cp437 name\n", 0o644)
        # A stored name with an Info-ZIP Unicode Path field over it.
        stored = b"unicode-field.txt"
        uname = "\u65e5\u672c.txt".encode()
        zi = zinfo("unicode-field.txt")
        zi.extra = struct.pack("<HHBI", 0x7075, 5 + len(uname), 1,
                               zlib.crc32(stored) & 0xffffffff) + uname
        z.writestr(zi, b"unicode path field\n")
        f.entry("f", "\u65e5\u672c.txt", b"unicode path field\n", 0o644)
        # An extended timestamp: its mtime wins over DOS's.
        zi = zinfo("stamped.txt")
        zi.extra = struct.pack("<HHBi", 0x5455, 5, 1, T0 + 3600)
        z.writestr(zi, b"stamped\n")
        f.entry("f", "stamped.txt", b"stamped\n", 0o644)
        # A Mac's companion file: listed, flagged, not extracted.
        z.writestr(zinfo("__MACOSX/._readme.txt"), b"\x00\x05\x16\x07")
        f.entry("f", "__MACOSX/._readme.txt", b"\x00\x05\x16\x07", 0o644,
                flags="M")
    f.data = buf.getvalue()


def zip_windows():
    f = fixture("zip_windows", ".zip",
                "made on DOS: backslashes separate, a drive is dropped, the "
                "folder bit marks folders, no modes")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data, attr in ((r"C:\Projects\a.txt", b"drive\n", 0x20),
                                 ("Projects\\sub\\", b"", 0x10),
                                 (r"Projects\sub\b.txt", b"backslash\n", 0x20)):
            zi = zipfile.ZipInfo(name, DOS)
            zi.create_system = 0
            zi.external_attr = attr
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, data)
    f.entry("f", "Projects/a.txt", b"drive\n")
    f.entry("d", "Projects/sub")
    f.entry("f", "Projects/sub/b.txt", b"backslash\n")
    f.data = buf.getvalue()


class Unseekable(io.RawIOBase):
    """A stream zipfile cannot seek back in: it writes data descriptors."""
    def __init__(self):
        self.buf = bytearray()

    def writable(self):
        return True

    def write(self, b):
        self.buf += b
        return len(b)


def zip_descriptor():
    f = fixture("zip_descriptor", ".zip",
                "written as a stream: sizes and CRCs in data descriptors, "
                "after the data")
    out = Unseekable()
    body = lcg_text(4, 20000)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        with z.open(zinfo("streamed.txt"), "w") as w:
            w.write(body)
    f.entry("f", "streamed.txt", body, 0o644)
    assert out.buf[6] & 8, "zipfile wrote no data descriptor"
    f.data = bytes(out.buf)


def zip64_by_hand():
    """ZIP64 in every place: the central record's sizes and offset in its
    extra field, a ZIP64 end record and locator, 0xffff counts."""
    f = fixture("zip64", ".zip", "ZIP64 records throughout, by hand")
    body = lcg_text(5, 3000)
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = comp.compress(body) + comp.flush()
    crc = zlib.crc32(body) & 0xffffffff
    name = b"sixty-four.txt"
    local_extra = struct.pack("<HHQQ", 1, 16, len(body), len(packed))
    local = struct.pack("<IHHHHHIIIHH", 0x04034b50, 45, 0, 8, 0, 0x58a1, crc,
                        0xffffffff, 0xffffffff, len(name), len(local_extra))
    local += name + local_extra + packed
    cd_extra = struct.pack("<HHQQQ", 1, 24, len(body), len(packed), 0)
    cd = struct.pack("<IHHHHHHIIIHHHHHII", 0x02014b50, 0x033f, 45, 0, 8, 0,
                     0x58a1, crc, 0xffffffff, 0xffffffff, len(name),
                     len(cd_extra), 0, 0, 0, 0o100644 << 16, 0xffffffff)
    cd += name + cd_extra
    at = len(local)
    eocd64 = struct.pack("<IQHHIIQQQQ", 0x06064b50, 44, 45, 45, 0, 0, 1, 1,
                         len(cd), at)
    loc = struct.pack("<IIQI", 0x07064b50, 0, at + len(cd), 1)
    eocd = struct.pack("<IHHHHIIH", 0x06054b50, 0, 0, 0xffff, 0xffff,
                       0xffffffff, 0xffffffff, 0)
    f.data = local + cd + eocd64 + loc + eocd
    f.entry("f", "sixty-four.txt", body, 0o644)


def zip_sfx():
    f = fixture("zip_sfx", ".zip",
                "a self-extractor: code before the archive, offsets counted "
                "from the archive's own start")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr(zinfo("inside.txt"), b"found past the stub\n")
    f.data = b"#!/bin/sh\necho self-extracting\nexit 0\n" + bytes(300) + \
        buf.getvalue()
    f.entry("f", "inside.txt", b"found past the stub\n", 0o644)


# ------------------------------------------------------------------ tar

def tinfo(name, kind=tarfile.REGTYPE, mode=0o644, size=0, link=""):
    ti = tarfile.TarInfo(name)
    ti.type, ti.mode, ti.size = kind, mode, size
    ti.linkname, ti.mtime = link, T0
    ti.uname = ti.gname = "tiku"
    return ti


def tar_add(t, f, name, data=b"", kind=tarfile.REGTYPE, mode=0o644,
            link="", listed=None, flags="-"):
    t.addfile(tinfo(name, kind, mode, len(data), link),
              io.BytesIO(data) if data else None)
    k = {tarfile.REGTYPE: "f", tarfile.DIRTYPE: "d", tarfile.SYMTYPE: "l",
         tarfile.LNKTYPE: "h"}.get(kind, "o")
    f.entry(k, listed or name.rstrip("/"), data if k == "f" else b"",
            mode & 0o777, None if k == "f" else 0, flags, link or None)


def tar_pax():
    f = fixture("tar_pax", ".tar",
                "pax: a global header, a long path and link in extended "
                "headers, a UTF-8 name, modes, both links")
    buf = io.BytesIO()
    long = "deep/" + "d" * 120 + "/" + "a-file-with-a-long-name-" * 5 + ".txt"
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT,
                      pax_headers={"comment": "a global header"}) as t:
        tar_add(t, f, "top/", kind=tarfile.DIRTYPE, mode=0o750)
        tar_add(t, f, "top/script.sh", b"#!/bin/sh\necho hi\n", mode=0o755)
        tar_add(t, f, "top/data.txt", lcg_text(6, 6000))
        tar_add(t, f, long, b"long\n")
        tar_add(t, f, "top/\u00fcber.txt", b"utf-8\n")
        tar_add(t, f, "top/sym", kind=tarfile.SYMTYPE, link="data.txt",
                mode=0o777)
        tar_add(t, f, "top/hard", kind=tarfile.LNKTYPE, link="top/data.txt")
        tar_add(t, f, "top/far", kind=tarfile.SYMTYPE, mode=0o777,
                link="../" + "x" * 110)
    f.data = buf.getvalue()


def tar_gnu():
    f = fixture("tar_gnu", ".tar",
                "GNU: long names and links in L and K records, and a size in "
                "base-256")
    buf = io.BytesIO()
    long = "gnu-" + "n" * 140 + ".txt"
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.GNU_FORMAT) as t:
        tar_add(t, f, long, b"gnu long name\n")
        tar_add(t, f, "shortlink", kind=tarfile.SYMTYPE, mode=0o777,
                link="target/" + "t" * 120)
        tar_add(t, f, "plain.txt", b"plain\n")
    data = bytearray(buf.getvalue())
    # plain.txt's size in base-256, as GNU writes past 8 GB.
    at = data.find(b"plain.txt")
    data[at + 124:at + 136] = b"\x80" + bytes(10) + b"\x06"
    data[at + 148:at + 156] = b" " * 8
    data[at + 148:at + 155] = b"%06o\x00" % sum(data[at:at + 512])
    f.data = bytes(data)


def tar_v7():
    f = fixture("tar_v7", ".tar", "a v7 header: no magic, a folder by its /")
    blocks = b""
    for name, body, typ in ((b"old/", b"", b"\0"),
                            (b"old/v7.txt", b"v7\n", b"0")):
        h = bytearray(512)
        h[0:len(name)] = name
        h[100:108] = b"000644 \0"
        h[108:116] = h[116:124] = b"000000 \0"
        h[124:136] = b"%011o " % len(body)
        h[136:148] = b"%011o " % T0
        h[148:156] = b" " * 8
        h[156:157] = typ
        h[148:155] = b"%06o\0" % sum(h)
        blocks += bytes(h) + body + bytes((512 - len(body) % 512) % 512)
    f.data = blocks + bytes(1024)
    f.entry("d", "old", mode=0o644)
    f.entry("f", "old/v7.txt", b"v7\n", 0o644)


def tgz():
    f = fixture("tgz", ".tar.gz",
                "tar in gzip, as tarfile writes it: the tar found by its "
                "first header")
    buf = io.BytesIO()
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as t:
        tar_add(t, f, "pkg/", kind=tarfile.DIRTYPE, mode=0o755)
        tar_add(t, f, "pkg/src.c", lcg_text(7, 50000))
        tar_add(t, f, "pkg/blob", lcg_bytes(8, 3000))
    with gzip.GzipFile(filename="pkg.tar", mode="wb", fileobj=buf,
                       mtime=T0) as g:
        g.write(raw.getvalue())
    f.data = buf.getvalue()


# ------------------------------------------------------------------ gzip

def gz_fields():
    f = fixture("gz_fields", ".gz",
                "one file, its header carrying every field: extra, name, "
                "comment and header CRC")
    body = lcg_text(9, 12000)
    head = bytearray(b"\x1f\x8b\x08\x1e" + struct.pack("<I", T0) + b"\x00\x03")
    head += struct.pack("<H", 4) + b"XY\x00\x00"
    head += b"notes.txt\0" + b"a comment\0"
    head += struct.pack("<H", zlib.crc32(head) & 0xffff)
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    f.data = bytes(head) + comp.compress(body) + comp.flush() + \
        struct.pack("<II", zlib.crc32(body) & 0xffffffff, len(body))
    f.entry("f", "notes.txt", body, size=-1)


def gz_members():
    f = fixture("gz_members", ".gz",
                "two members one after the other, then zeros, as tapes pad")
    one, two = lcg_text(10, 30000), b"the second member\n"
    f.data = gzip.compress(one, mtime=T0) + gzip.compress(two, mtime=T0) + \
        bytes(64)
    f.entry("f", "gz_members", one + two, size=-1)


# ------------------------------------------------------------------ hostile

def hostile_zip():
    f = fixture("hostile_zip", ".zip",
                "names that climb out, a control character, an absolute path "
                "kept inside")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, flags, listed in (("../climb.txt", "R", "../climb.txt"),
                                    ("a/../../climb2.txt", "R",
                                     "a/../../climb2.txt"),
                                    ("/abs/inside.txt", "-", "abs/inside.txt"),
                                    ("ctl\x01.txt", "R", "ctl?.txt"),
                                    ("fine.txt", "-", "fine.txt")):
            z.writestr(zinfo(name), b"hostile\n")
            f.entry("f", listed, b"hostile\n", 0o644, flags=flags)
    f.data = buf.getvalue()


def hostile_tar():
    f = fixture("hostile_tar", ".tar",
                "links out and through, a hard link out, a FIFO, a device, "
                "one name twice, a set-uid file")
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as t:
        tar_add(t, f, "out", kind=tarfile.SYMTYPE, mode=0o777,
                link="../../outside")
        tar_add(t, f, "out/through.txt", b"through?\n")
        tar_add(t, f, "abs", kind=tarfile.SYMTYPE, mode=0o777,
                link="/etc/passwd")
        tar_add(t, f, "dir/", kind=tarfile.DIRTYPE, mode=0o755)
        tar_add(t, f, "in", kind=tarfile.SYMTYPE, mode=0o777, link="dir")
        tar_add(t, f, "in/via-link.txt", b"via?\n")
        tar_add(t, f, "ok.txt", b"ok\n")
        tar_add(t, f, "hard-out", kind=tarfile.LNKTYPE, link="/etc/passwd")
        tar_add(t, f, "hard-in", kind=tarfile.LNKTYPE, link="ok.txt")
        tar_add(t, f, "fifo", kind=tarfile.FIFOTYPE, flags="U")
        tar_add(t, f, "dev", kind=tarfile.CHRTYPE, flags="U")
        tar_add(t, f, "twice.txt", b"first\n")
        tar_add(t, f, "twice.txt", b"second\n")
        tar_add(t, f, "setuid", b"#!/bin/sh\n", mode=0o4755)
    f.data = buf.getvalue()


def broken():
    ok = io.BytesIO()
    with zipfile.ZipFile(ok, "w") as z:
        z.writestr(zinfo("a.txt"), b"a" * 1000)
        z.writestr(zinfo("b.txt"), b"b" * 1000)
    d = bytearray(ok.getvalue())
    second = d.find(b"PK\x01\x02", d.find(b"PK\x01\x02") + 4)
    struct.pack_into("<I", d, second + 42, 0)      # b.txt's data is a.txt's
    f = fixture("bomb_zip", ".zip", "two entries over the same data")
    f.data = bytes(d)

    f = fixture("liar_zip", ".zip", "an entry that inflates past its size")
    one = io.BytesIO()
    with zipfile.ZipFile(one, "w") as z:
        z.writestr(zinfo("liar.txt"), b"A" * 50000)
    d = bytearray(one.getvalue())
    struct.pack_into("<I", d, d.find(b"PK\x01\x02") + 24, 10)
    f.data = bytes(d)
    f.entry("f", "liar.txt", mode=0o644, size=10)

    f = fixture("badcrc_zip", ".zip", "an entry whose CRC is wrong")
    one = io.BytesIO()
    with zipfile.ZipFile(one, "w") as z:
        z.writestr(zinfo("badcrc.txt"), b"checksum me\n" * 50)
    d = bytearray(one.getvalue())
    struct.pack_into("<I", d, d.find(b"PK\x01\x02") + 16, 0x12345678)
    f.data = bytes(d)
    f.entry("f", "badcrc.txt", b"checksum me\n" * 50, 0o644)

    f = fixture("badgz", ".gz", "a gzip whose trailer's CRC is wrong")
    g = bytearray(gzip.compress(b"trailer\n" * 100, mtime=T0))
    g[-8] ^= 0xff
    f.data = bytes(g)
    f.entry("f", "badgz", b"trailer\n" * 100, size=-1)

    f = fixture("cut_tgz", ".tgz", "a tar.gz cut in half: listed as far as "
                "it goes, then damaged")
    whole = next(x for x in FIXTURES if x.name == "tgz").data
    f.data = whole[:len(whole) // 2]


for make in (zip_features, zip_windows, zip_descriptor, zip64_by_hand,
             zip_sfx, tar_pax, tar_gnu, tar_v7, tgz, gz_fields, gz_members,
             hostile_zip, hostile_tar, broken):
    make()

def c_string(text):
    """@p text as a C string literal's inside: UTF-8, octal past ASCII."""
    out = []
    for b in text.encode("utf-8"):
        c = chr(b)
        if c == "\n":
            out.append("\\n")
        elif c == "\t":
            out.append("\\t")
        elif c in "\"\\":
            out.append("\\" + c)
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append("\\%03o" % b)
    return "".join(out)


out = ["/* test_archive's archives, by tests/fixtures/tools/gen_archive.py:",
       " * base64, each with the manifest of what it must list.",
       " * SPDX-License-Identifier: Apache-2.0 */"]
for f in FIXTURES:
    b64 = base64.b64encode(f.data).decode()
    out.append("/* %s */" % f.doc)
    out.append("static const char fx_%s[] = /* %d bytes */"
               % (f.name, len(f.data)))
    out += ['    "%s"' % b64[i:i + 72] for i in range(0, len(b64), 72)]
    out[-1] += ";"
    out.append("static const char mf_%s[] =" % f.name)
    out += ['    "%s"' % c_string(l + "\n") for l in f.lines] or ['    ""']
    out[-1] += ";"
out.append("static const struct { const char *name, *ext, *data, *manifest; }"
           " fixtures[] = {")
for f in FIXTURES:
    out.append('    { "%s", "%s", fx_%s, mf_%s },' % (f.name, f.ext, f.name,
                                                        f.name))
out.append("};")
open(OUT, "w").write("\n".join(out) + "\n")
print("%d fixtures, %d bytes of archives" % (len(FIXTURES),
      sum(len(f.data) for f in FIXTURES)))
