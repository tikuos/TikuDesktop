#!/usr/bin/env python3
"""
Split one C file into a family of files, one functionality each.

A file that has grown section banners is a directory waiting to happen:
each banner names a file.  This tool makes the cut without editing a
function -- functions are moved whole, in the order the file had them,
and everything a move needs beyond that is generated: a private header
carrying the includes, macros and types the family shares, the
prototypes of every function one file defines and another uses, and
the forward declarations each file needs on its own.

  report FILE [--names]       the sections, their sizes, the functions,
                              and which sections call which
  bootstrap FILE --dir DIR    move FILE into DIR, cut its head into
      [--inline f,g]          DIR/<stem>_private.h (small accessors
      [--public H ...]        become static inline there), drop the
      [--mk MK:VAR] [--tag T] banners stay as the map; H are headers whose
                              declarations keep a function non-static;
                              MK:VAR is the make variable of the family;
                              T prefixes every static the split promotes
  move --dir DIR NAME "what"  move functions f.. into <stem>_NAME.c;
      [--section T] f..       --section moves every function under the
      [--lines A-B]           banner titled T; --lines moves raw lines
                              (a typedef, a table).  A banner left with
                              nothing under it goes.
  cut --dir DIR --lines A-B   lift lines A-B of a function in <stem>.c
      [--file F]              (or in family file F)
      --into SUFFIX --sig S   into a new function whose signature is S,
      --call C [--case]       written to <stem>_SUFFIX.c, and put the
      [--brief B] [--pre P]   call C where the lines were.  --case takes
      [--post Q]              a whole `case X: {...}` block and turns its
      [--case-return E]       case-level breaks into `return E;`
      [--subst RE=>REP]..     (bare returns without it).  --subst
                              rewrites the lifted code outside strings
                              and comments (a.x becomes app->x); --pre
                              and --post are lines placed first and last.
  hoist --dir DIR --lines A-B move lines A-B of <stem>.c (a struct, a
      | --from "text"          table of types) into the private header;
                              --from names the block by its first line and
                              takes it through the `};` that closes it
  sync --dir DIR              recompute linkage and the prototype block
                              (run after any hand edit to the family)

Three things the generated code never does.  A static becomes shared
only when another family file names it, and is then renamed with the
family's tag (pose_index becomes pv_pose_index), so two families that
had the same private name do not meet in the link.  Nothing that was non-static is
made static: what the family does not reference may be linked from
outside it, which nothing here can see.  A file-scope static VARIABLE
never goes into the header -- it stays in the file that had it, and is
exported through an extern only when another file uses it.

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""
import io
import os
import re
import subprocess
import sys
import textwrap

GUARD_A = '/* Shared across the family: defined in one file, used in another. */'
GUARD_B = '/* End of the shared declarations. */'
FWD_A = '/* Used before they are defined. */'
FWD_B = '/* End of the forward declarations. */'
CFG = '/* split: public %s */'
TAG = '/* split: tag %s */'

ONELINE = re.compile(r'^(static\s+)?((?:inline\s+)?[A-Za-z_][\w \t*]*?[ *])([a-z_][a-z0-9_]*)\([^;{]*\)\s*$')
TYPE_LINE = re.compile(r'^(static\s+)?(inline\s+)?[A-Za-z_][A-Za-z0-9_ \t*]*\**$')
BANNER = re.compile(r'^/\*[-=]{20,}\*/$')
STATIC_VAR = re.compile(r'^static\s+(?!inline\b)[A-Za-z_][\w\s\*]*?\b([a-z_]\w*)\s*(\[[^;=]*\])?\s*(=|;)')
VAR_LINE = re.compile(r'^(static\s+)?(?!inline\b|extern\b|typedef\b|return\b)[A-Za-z_][\w\s\*]*?\b([a-z_]\w*)\s*(\[[^;=]*\])?\s*(=|;)')


def variables(lines, idx):
    """(line, name, is_static) of every file-scope variable definition."""
    inside = set()
    for f in idx:
        inside.update(range(f['start'], f['end'] + 1))
    out = []
    for k, l in enumerate(lines):
        if k in inside or l[:1] in ('', ' ', '\t', '#', '/', '*', '}'):
            continue
        m = VAR_LINE.match(l)
        if m and '(' not in l.split('=')[0]:
            out.append((k, m.group(2), bool(m.group(1))))
    return out


def rd(p):
    return io.open(p, encoding='utf-8').read()


def wr(p, s):
    io.open(p, 'w', encoding='utf-8').write(s)


# ---------------------------------------------------------------- reading

def index(lines):
    """Function definitions: name at column 0, the type on the line above,
    the doc comment above that."""
    out = []
    i = 0
    n = len(lines)
    while i < n:
        m = re.match(r'^([a-z_][a-z0-9_]*)\(', lines[i])
        if (m and i > 0 and TYPE_LINE.match(lines[i - 1].rstrip())
                and lines[i - 1].strip()):
            j = i
            while j < n and lines[j] != '}':
                j += 1
            s = i - 1
            if s - 1 >= 0 and lines[s - 1].rstrip().endswith('*/'):
                k = s - 1
                while k >= 0 and not lines[k].lstrip().startswith('/*'):
                    k -= 1
                s = k
            e = i
            while e < j and not lines[e].rstrip().endswith(')'):
                e += 1
            out.append(dict(name=m.group(1), sig=i, start=s, end=j, sigend=e,
                            static=lines[i - 1].startswith('static'),
                            ret=re.sub(r'^static\s+', '',
                                       lines[i - 1].strip()),
                            oneline=False))
            i = j
            i += 1
            continue
        m = ONELINE.match(lines[i])
        if m and i + 1 < n and lines[i + 1] == '{':
            j = i + 1
            while j < n and lines[j] != '}':
                j += 1
            s = i
            if s - 1 >= 0 and lines[s - 1].rstrip().endswith('*/'):
                k = s - 1
                while k >= 0 and not lines[k].lstrip().startswith('/*'):
                    k -= 1
                s = k
            out.append(dict(name=m.group(3), sig=i, start=s, end=j, sigend=i,
                            static=lines[i].startswith('static'),
                            ret='', oneline=True))
            i = j
        i += 1
    return out


def sections(lines):
    """(first line, title) of every banner; the head before the first
    banner is a section of its own."""
    out = []
    i = 0
    while i < len(lines):
        if BANNER.match(lines[i]) and i + 2 < len(lines) and BANNER.match(lines[i + 2]):
            title = lines[i + 1].strip(' /*').strip()
            out.append((i, title))
            i += 3
            continue
        i += 1
    if not out or out[0][0] > 0:
        out.insert(0, (0, '(head)'))
    return out


def prototype(lines, f, prefix=''):
    sig = ' '.join(l.strip() for l in lines[f['sig']:f['sigend'] + 1])
    sig = re.sub(r'\s+', ' ', sig)
    ret = f['ret']
    if f.get('oneline'):
        text = re.sub(r'^static\s+', '', sig) + ';'
    else:
        text = ('%s %s;' % (ret, sig)) if not ret.endswith('*') else ('%s%s;' % (ret, sig))
    text = prefix + text
    if len(text) <= 80:
        return text
    head, params = text.split('(', 1)
    parts = params[:-2].split(', ')
    out = head + '('
    col = len(out)
    lines_out = []
    cur = ''
    for k, p in enumerate(parts):
        piece = p + (', ' if k < len(parts) - 1 else ');')
        if cur and col + len(cur) + len(piece.rstrip()) > 80:
            lines_out.append(cur.rstrip())
            cur = ''
        cur += piece
    lines_out.append(cur.rstrip())
    indent = ' ' * col
    return out + lines_out[0] + ''.join('\n' + indent + l for l in lines_out[1:])


def subst_code(text, old, new):
    """Replace whole-word old with new outside comments and strings.  Given
    a list of (regex, replacement) pairs as old, apply them all."""
    pairs = old if isinstance(old, list) else [(r'\b%s\b' % re.escape(old), new)]
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith('/*', i):
            j = text.find('*/', i) + 2
            out.append(text[i:j])
            i = j
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(text[i:j])
            i = j
            continue
        c = text[i]
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        j = i
        while (j < n and text[j] not in '"\'' and not text.startswith('/*', j)
               and not text.startswith('//', j)):
            j += 1
        seg = text[i:j]
        for pat, rep in pairs:
            seg = re.sub(pat, rep, seg)
        out.append(seg)
        i = j
    return ''.join(out)


def word_in(name, text):
    return re.search(r'\b%s\b' % re.escape(name), text) is not None


# ----------------------------------------------------------------- report

def report(path, names=False):
    lines = rd(path).split('\n')
    secs = sections(lines)
    bounds = [(secs[k][0], secs[k + 1][0] if k + 1 < len(secs) else len(lines),
               secs[k][1]) for k in range(len(secs))]
    idx = index(lines)
    owner = {}
    for f in idx:
        for a, b, t in bounds:
            if a <= f['sig'] < b:
                owner[f['name']] = t
    statics = [l for l in lines if STATIC_VAR.match(l) and '(' not in l.split('=')[0]]
    print('%s: %d lines, %d sections, %d functions, %d file-scope statics'
          % (path, len(lines), len(bounds), len(idx), len(statics)))
    print()
    print('  lines  funcs  section')
    for a, b, t in bounds:
        here = [f for f in idx if owner.get(f['name']) == t]
        body = '\n'.join(lines[a:b])
        uses = {}
        for f in idx:
            o = owner.get(f['name'])
            if o != t and re.search(r'\b%s\s*\(' % re.escape(f['name']), body):
                uses[o] = uses.get(o, 0) + 1
        print('  %5d  %5d  %s' % (b - a, len(here), t))
        if names:
            for f in here:
                print('                  %s' % f['name'])
        for o, n in sorted(uses.items(), key=lambda kv: -kv[1]):
            print('                  calls %2d of: %s' % (n, o))
    print()
    big = sorted(idx, key=lambda f: -(f['end'] - f['sig']))[:8]
    print('  longest functions:')
    for f in big:
        print('  %5d  %s' % (f['end'] - f['sig'], f['name']))
    if statics:
        print()
        print('  file-scope statics (stay in the file that had them):')
        for l in statics:
            print('    ' + l.strip())


# ------------------------------------------------------------------ family

class Family:
    def __init__(self, d):
        self.dir = d
        hdrs = [f for f in os.listdir(d) if f.endswith('_private.h')]
        if len(hdrs) != 1:
            sys.exit('%s: expected one *_private.h, found %r' % (d, hdrs))
        self.hdr = hdrs[0]
        self.stem = self.hdr[:-len('_private.h')]
        self.main = self.stem + '.c'
        self.guard = 'TIKU_' + self.stem.upper().replace('TIKU_', '', 1) + '_PRIVATE_H_'
        h = rd(self.path(self.hdr))
        # a header written before the markers settled carries the old
        # ones; they are moved to the current text once, on first sight
        old_a = "/* Shared across the desktop's files: defined in one, used in another. */"
        old_b = '/* End of the shared functions. */'
        if GUARD_A not in h and old_a in h and old_b in h:
            h = h.replace(old_a, GUARD_A).replace(old_b, GUARD_B)
            wr(self.path(self.hdr), h)
        m = re.search(r'/\* split: public (.*?) \*/', h)
        self.public = m.group(1).split() if m else []
        m = re.search(r'/\* split: tag (\w+) \*/', h)
        self.tag = m.group(1) if m else None

    def path(self, f):
        return os.path.join(self.dir, f)

    def files(self):
        return sorted(f for f in os.listdir(self.dir) if f.endswith('.c'))

    def public_names(self):
        names = set()
        for h in self.public:
            for l in rd(h).split('\n'):
                m = re.match(r'^[A-Za-z_][\w\s\*]*?\b([a-z_]\w*)\s*\(', l)
                if m and not l.startswith(('static', '#', ' ', '/', '*')):
                    names.add(m.group(1))
                m2 = re.match(r'^extern\s+.*\b([a-z_]\w*)\s*[\[;]', l)
                if m2:
                    names.add(m2.group(1))
        return names


def refs_outside(fname, name, files):
    for g, text in files.items():
        if g != fname and word_in(name, text):
            return True
    return False


def sync(fam, verbose=True):
    files = {f: rd(fam.path(f)) for f in fam.files()}
    public = fam.public_names()
    protos = {}
    externs = {}
    renames = []
    for f, text in files.items():
        lines = text.split('\n')
        idx = index(lines)
        for fn in idx:
            shared = refs_outside(f, fn['name'], files)
            exported = shared or fn['name'] in public
            tline = fn['sig'] if fn.get('oneline') else fn['sig'] - 1
            tl = lines[tline]
            # promotion only: what was non-static is linked from outside
            # the family, which nothing here can see
            if exported and tl.startswith('static '):
                lines[tline] = tl[len('static '):]
                if fam.tag and fn['name'] not in public:
                    renames.append((fn['name'], fam.tag + '_' + fn['name']))
            if shared and fn['name'] not in public:
                fn2 = dict(fn)
                fn2['ret'] = re.sub(r'^static\s+', '', lines[fn['sig'] - 1].strip())
                protos.setdefault(f, []).append(prototype(lines, fn2))
        # File-scope variables: static while one file uses them, extern'ed
        # from the header once another does.
        for k, name, is_static in variables(lines, idx):
            l = lines[k]
            if refs_outside(f, name, files):
                decl = re.sub(r'^static\s+', '', l.split('=')[0].strip().rstrip(';')).strip()
                if is_static:
                    lines[k] = l[len('static '):]
                    if fam.tag:
                        renames.append((name, fam.tag + '_' + name))
                        decl = re.sub(r'\b%s\b' % re.escape(name), fam.tag + '_' + name, decl)
                externs.setdefault(f, []).append('extern %s;' % decl)
        all_names = {fn['name'] for t in files.values() for fn in index(t.split('\n'))}
        # A forward declaration of something the header now declares is a
        # second declaration with the wrong linkage: every old one goes,
        # and the block below is regenerated from what is actually used
        # before it is defined.
        kept = []
        k = 0
        while k < len(lines):
            l = lines[k]
            m = re.match(r'^static [^=(]*\b([a-z_][a-z0-9_]*)\(', l)
            # a declaration, not a one-line definition with its brace below
            if (m and not l.rstrip().endswith('{') and m.group(1) in all_names
                    and not (k + 1 < len(lines) and lines[k + 1] == '{')):
                j = k
                while not lines[j].rstrip().endswith(';'):
                    j += 1
                k = j + 1
                continue
            kept.append(l)
            k += 1
        lines = kept
        text2 = '\n'.join(lines)
        a = text2.find(FWD_A)
        b = text2.find(FWD_B)
        if a >= 0 and b >= 0:
            text2 = text2[:a] + text2[b + len(FWD_B):].lstrip('\n')
            lines = text2.split('\n')
        idx2 = index(lines)
        fwd = []
        for fn in idx2:
            tline = fn['sig'] if fn.get('oneline') else fn['sig'] - 1
            static_here = lines[tline].startswith('static ')
            # one the header already declares needs nothing here
            if not static_here and (refs_outside(f, fn['name'], files) or fn['name'] in public):
                continue
            before = '\n'.join(lines[:fn['start']])
            if word_in(fn['name'], before):
                fwd.append(prototype(lines, fn, 'static ' if static_here else ''))
        inc = '#include "%s"' % fam.hdr
        if fwd:
            block = FWD_A + '\n' + '\n'.join(fwd) + '\n' + FWD_B
            text2 = '\n'.join(lines).replace(inc, inc + '\n\n' + block, 1)
        else:
            text2 = '\n'.join(lines)
        wr(fam.path(f), text2)
        files[f] = text2
    if renames:
        for g in list(files) + [fam.hdr]:
            t = rd(fam.path(g))
            for old, new in renames:
                t = subst_code(t, old, new)
            wr(fam.path(g), t)
            if g in files:
                files[g] = t
        print('renamed at promotion:', ' '.join(n for o, n in renames))
        return sync(fam, verbose)
    h = rd(fam.path(fam.hdr))
    all_defs = {fn['name'] for t in files.values() for fn in index(t.split('\n'))}
    hl = h.split('\n')
    keep = []
    i = 0
    while i < len(hl):
        m = re.match(r'^static [^=(]*\b([a-z_][a-z0-9_]*)\(', hl[i])
        if m and m.group(1) in all_defs and not hl[i].rstrip().endswith('{'):
            while not hl[i].rstrip().endswith(';'):
                i += 1
            i += 1
            continue
        keep.append(hl[i])
        i += 1
    h = '\n'.join(keep)
    a = h.index(GUARD_A)
    b = h.index(GUARD_B) + len(GUARD_B)
    block = [GUARD_A, '']
    for f in sorted(set(protos) | set(externs)):
        block.append('/* %s */' % f)
        block.extend(sorted(externs.get(f, [])))
        block.extend(sorted(protos.get(f, [])))
        block.append('')
    block.append(GUARD_B)
    wr(fam.path(fam.hdr), h[:a] + '\n'.join(block) + h[b:])
    if verbose:
        print('sync:', {f: len(v) for f, v in protos.items()},
              'externs:', {f: len(v) for f, v in externs.items()})


def forward_decls(p):
    """Keep one block of forward declarations for the statics a loose file
    uses before it defines them, after its last #include."""
    text = rd(p)
    a = text.find(FWD_A)
    b = text.find(FWD_B)
    if a >= 0 and b >= 0:
        text = text[:a] + text[b + len(FWD_B):].lstrip('\n')
    lines = text.split('\n')
    idx = index(lines)
    fwd = []
    for fn in idx:
        tline = fn['sig'] if fn.get('oneline') else fn['sig'] - 1
        static_here = lines[tline].startswith('static ')
        before = '\n'.join(lines[:fn['start']])
        if word_in(fn['name'], before):
            fwd.append(prototype(lines, fn, 'static ' if static_here else ''))
    if fwd:
        last = max(k for k, l in enumerate(lines) if l.startswith('#include'))
        lines[last + 1:last + 1] = ['', FWD_A] + fwd + [FWD_B]
    wr(p, '\n'.join(lines))


def banner_free(body):
    out = []
    i = 0
    while i < len(body):
        if BANNER.match(body[i]) and i + 2 < len(body) and BANNER.match(body[i + 2]):
            i += 3
            while i < len(body) and body[i].strip() == '':
                i += 1
            continue
        out.append(body[i])
        i += 1
    return out


def file_comment(fname, what):
    hdr = ['/*',
           ' * TikuTracker -- the file manager for TikuOS.',
           ' *',
           ' * Authors: Ambuj Varshney <ambuj@tiku-os.org>',
           ' *']
    hdr += [' * ' + l if l else ' *' for l in textwrap.wrap('%s - %s' % (fname, what), 74)]
    hdr += [' *', ' * SPDX-License-Identifier: Apache-2.0', ' */']
    return hdr


def bootstrap(src, d, inline_names, public, mk, tag=None):
    os.makedirs(d, exist_ok=True)
    base = os.path.basename(src)
    stem = base[:-2]
    hdr = stem + '_private.h'
    dst = os.path.join(d, base)
    if os.path.abspath(src) != os.path.abspath(dst):
        subprocess.run(['git', 'mv', src, dst], check=True)
    lines = rd(dst).split('\n')
    idx = index(lines)
    first = min(f['start'] for f in idx)
    inc0 = next(i for i, l in enumerate(lines) if l.startswith('#include'))
    head_region = lines[inc0:first]
    body = lines[first:]
    # a banner just above the first function titles the body, not the head
    k = len(head_region)
    while k > 0 and head_region[k - 1].strip() == '':
        k -= 1
    if k >= 3 and BANNER.match(head_region[k - 1]) and BANNER.match(head_region[k - 3]):
        body = head_region[k - 3:] + body
        head_region = head_region[:k - 3]
    # file-scope statics stay with the code; the header only shares
    keep_in_file = []
    hdr_region = []
    for l in head_region:
        if STATIC_VAR.match(l) and '(' not in l.split('=')[0]:
            keep_in_file.append(l)
        else:
            hdr_region.append(l)
    inl = []
    for name in inline_names:
        bidx = index(body)
        f = [x for x in bidx if x['name'] == name]
        if not f:
            sys.exit('no function %s to inline' % name)
        f = f[0]
        chunk = body[f['start']:f['end'] + 1]
        tl = f['sig'] - f['start'] - 1
        chunk[tl] = 'static inline ' + re.sub(r'^static\s+', '', chunk[tl])
        inl.append('\n'.join(chunk))
        e = f['end'] + 1
        while e < len(body) and body[e].strip() == '':
            e += 1
        del body[f['start']:e]
    top = '\n'.join(lines[:inc0]).rstrip('\n') + '\n\n#include "%s"\n\n' % hdr
    if keep_in_file:
        top += '\n'.join(keep_in_file) + '\n\n'
    wr(dst, top + '\n'.join(body).lstrip('\n'))
    guard = 'TIKU_' + stem.upper().replace('TIKU_', '', 1) + '_PRIVATE_H_'
    h = file_comment(hdr, 'what the %s files share: the includes, the types, the '
                     'small accessors that read them, and the functions one file '
                     'defines for another.  Nothing outside this directory '
                     'includes it.' % stem)
    h += ['#ifndef %s' % guard, '#define %s' % guard, '']
    if public:
        h += [CFG % ' '.join(public), '']
    if tag:
        h += [TAG % tag, '']
    h += hdr_region
    if inl:
        h += ['', '/* Read where they are used. */', '', '\n\n'.join(inl)]
    h += ['', GUARD_A, '', GUARD_B, '', '#endif /* %s */' % guard, '']
    wr(os.path.join(d, hdr), '\n'.join(h))
    if mk:
        mkfile, var = mk.split(':')
        m = rd(mkfile)
        old = '$(T)/' + os.path.relpath(src, 'TikuTracker') if src.startswith('TikuTracker/') else src
        new = '$(T)/' + os.path.relpath(dst, 'TikuTracker') if dst.startswith('TikuTracker/') else dst
        n = m.count(old)
        if n == 0:
            sys.exit('%s not named in %s' % (old, mkfile))
        m = m.replace(old, '$(%s)' % var)
        first_use = m.index('$(%s)' % var)
        line_start = m.rfind('\n', 0, first_use) + 1
        # the definition goes above the first rule or list that uses it
        while line_start > 0 and m[line_start - 1] != '\n':
            line_start -= 1
        k = line_start
        while k > 0 and m[k - 1:k] != '\n':
            k -= 1
        # back up over a continued list to its first line
        j = m.rfind('\n\n', 0, first_use) + 2
        m = m[:j] + ('# %s, one composition over the files of one directory.\n'
                     '%s := %s\n\n' % (stem, var, new)) + m[j:]
        wr(mkfile, m)
        print('%s: %d uses of %s now $(%s)' % (mkfile, n, old, var))
    fam = Family(d)
    sync(fam)
    sync(fam)


def owners(lines, idx):
    secs = sections(lines)
    bounds = [(secs[k][0], secs[k + 1][0] if k + 1 < len(secs) else len(lines),
               secs[k][1]) for k in range(len(secs))]
    own = {}
    for f in idx:
        for a, b, t in bounds:
            if a <= f['sig'] < b:
                own[f['name']] = (t, a, b)
    return own, bounds


def add_to_mk(mk, tp):
    mkfile, var = mk.split(':')
    m = rd(mkfile)
    anchor = re.search(r'^%s := .*$' % re.escape(var), m, re.M)
    if anchor is None:
        sys.exit('%s: no %s :=' % (mkfile, var))
    rel = '$(T)/' + os.path.relpath(tp, 'TikuTracker') if tp.startswith('TikuTracker/') else tp
    # the list's last line is the first one not ending in a backslash
    end = anchor.end()
    while m[end - 1] == '\\':
        end = m.index('\n', end + 1)
    m = m[:end] + ' \\\n' + ' ' * (len(var) + 4) + rel + m[end:]
    wr(mkfile, m)


def move(d, suffix, what, names, ranges, mk, section=None):
    fam = Family(d)
    target = '%s_%s.c' % (fam.stem, suffix)
    lines = rd(fam.path(fam.main)).split('\n')
    idx = index(lines)
    by = {f['name']: f for f in idx}
    own, bounds = owners(lines, idx)
    if section is not None:
        hits = [t for a, b, t in bounds if t.lower() == section.lower()]
        if not hits:
            hits = [t for a, b, t in bounds if t.lower().startswith(section.lower())]
        if len(hits) != 1:
            sys.exit('section %r matches %r' % (section, hits))
        names = list(names) + [n for n, (t, a, b) in own.items() if t == hits[0]]
        a, b = [(a, b) for a, b, t in bounds if t == hits[0]][0]
        # whatever in the section is not a function stays: say so
        covered = set()
        for n in names:
            f = by[n]
            covered.update(range(f['start'], f['end'] + 1))
        left = [k for k in range(a + 3, b) if k not in covered and lines[k].strip()]
        # what is not a function -- a static, a table, a typedef -- moves
        # with its section, as the runs of lines it occupies
        run = None
        for k in left:
            if run and k == run[1] + 1:
                run[1] = k
            else:
                if run:
                    ranges.append((run[0] + 1, run[1] + 1))
                run = [k, k]
        if run:
            ranges.append((run[0] + 1, run[1] + 1))
        if left:
            print('%d non-function lines move with the section' % len(left))
    if not names and not ranges:
        sys.exit('nothing to move')
    missing = [n for n in names if n not in by]
    if missing:
        sys.exit('not in %s: %s' % (fam.main, ' '.join(missing)))
    chunks = []
    cut = []
    to_header = []
    for a, b in list(ranges):
        k = a - 1
        while k < b and lines[k].lstrip()[:2] in ('', '/*', '* ', '*/', '//', '*'):
            k += 1
        first = lines[k].lstrip() if k < b else ''
        if first.startswith(('typedef', 'struct ', 'enum ', 'union ', '#define', '#include')):
            to_header.append((a - 1, b))
            ranges.remove((a, b))
    pieces = [(a - 1, b) for a, b in ranges]
    pieces += [(by[n]['start'], by[n]['end'] + 1) for n in names]
    for a, b in sorted(pieces):
        chunks.append('\n'.join(lines[a:b]))
        cut.append((a, b))
    if to_header:
        h = rd(fam.path(fam.hdr))
        block = '\n\n'.join('\n'.join(lines[a:b]) for a, b in sorted(to_header))
        h = h.replace(GUARD_A, block + '\n\n' + GUARD_A, 1)
        wr(fam.path(fam.hdr), h)
        cut += to_header
        print('%d type or macro lines to %s' % (sum(b - a for a, b in to_header), fam.hdr))
    for a, b in sorted(cut, reverse=True):
        e = b
        while e < len(lines) and lines[e].strip() == '':
            e += 1
        del lines[a:e]
    # a banner with nothing under it any more
    idx3 = index(lines)
    own3, bounds3 = owners(lines, idx3)
    empty = [(a, b) for a, b, t in bounds3
             if t != '(head)' and not any(o[0] == t for o in own3.values())
             and not any(l.strip() for l in lines[a + 3:b])]
    for a, b in sorted(empty, reverse=True):
        del lines[a:b]
    wr(fam.path(fam.main), '\n'.join(lines))
    tp = fam.path(target)
    if os.path.exists(tp):
        wr(tp, rd(tp).rstrip('\n') + '\n\n' + '\n\n'.join(chunks) + '\n')
    else:
        hdr = file_comment(target, what) + ['', '#include "%s"' % fam.hdr, '']
        wr(tp, '\n'.join(hdr) + '\n' + '\n\n'.join(chunks) + '\n')
        if mk:
            add_to_mk(mk, tp)
    sync(fam)
    sync(fam)
    print('moved %d functions and %d ranges to %s' % (len(names), len(ranges), target))


def loop_marks(lines):
    """For each line, whether it sits inside a loop or switch that the lines
    themselves open; strings and comments skipped."""
    stack = []
    pending = False
    out = []
    in_comment = False
    for l in lines:
        out.append(any(stack))
        i = 0
        while i < len(l):
            if in_comment:
                j = l.find('*/', i)
                if j < 0:
                    break
                in_comment = False
                i = j + 2
                continue
            if l.startswith('/*', i):
                in_comment = True
                i += 2
                continue
            if l.startswith('//', i):
                break
            c = l[i]
            if c in '"\'':
                j = i + 1
                while j < len(l) and l[j] != c:
                    if l[j] == '\\':
                        j += 1
                    j += 1
                i = j + 1
                continue
            m = re.match(r'(for|while|do|switch)\b', l[i:])
            if m and (i == 0 or not (l[i - 1].isalnum() or l[i - 1] == '_')):
                pending = True
                i += len(m.group(1))
                continue
            if c == '{':
                stack.append(pending)
                pending = False
            elif c == '}':
                if stack:
                    stack.pop()
            i += 1
    return out


def cut(d, lo, hi, suffix, sig, call, brief, pre, post, substs, case, mk, src=None,
        case_return=None):
    loose = not [f for f in os.listdir(d) if f.endswith('_private.h')]
    if loose:
        if src is None:
            sys.exit('a directory without a private header needs --file')
        fam = None
        target = src
        path = lambda f: os.path.join(d, f)
    else:
        fam = Family(d)
        target = '%s_%s.c' % (fam.stem, suffix)
        src = src or fam.main
        path = fam.path
    lines = rd(path(src)).split('\n')
    body = lines[lo - 1:hi]
    if substs:
        body = subst_code('\n'.join(body), substs, None).split('\n')
        substs = None
    if case:
        label = body[0].strip()
        if not label.startswith('case ') and not label.startswith('default'):
            sys.exit('--case: line %d is not a case label: %s' % (lo, label))
        body = body[1:]
        if label.endswith('{'):
            close = max(k for k, l in enumerate(body) if l.strip() == '}')
            body = body[:close]
        while body and body[-1].strip() == '':
            body.pop()
        if body and body[-1].strip() == 'break;':
            body.pop()
        # a break at the case's own level leaves the case: the function
        # ends there.  Deeper ones belong to their own loop or switch.
        # A continue at that level meant the pump loop, which the
        # function cannot reach: refused.
        marks = loop_marks(body)
        for k, l in enumerate(body):
            st = l.strip()
            if not marks[k] and st == 'break;':
                body[k] = l.replace('break;', 'return %s;' % case_return if case_return else 'return;')
            elif not marks[k] and st == 'continue;' or st.startswith('goto '):
                sys.exit('line %d: %s at case level cannot be lifted' % (lo + 1 + k, st))
    # dedent by the least indent of a code line
    ind = min((len(l) - len(l.lstrip()) for l in body if l.strip()), default=0)
    body = [l[ind - 4:] if len(l) >= ind - 4 and l.strip() else '' for l in body] if ind > 4 else body
    text = '\n'.join(body)
    fn = []
    if brief:
        first = textwrap.wrap(brief, 70)
        rest = textwrap.wrap(' '.join(first[1:]), 77) if len(first) > 1 else []
        fn.append('/**\n * @brief ' + '\n * '.join([first[0]] + rest) + '\n */')
    if loose and not sig.startswith('static '):
        sig = 'static ' + sig
    ret, _, rest = sig.partition(' ')
    if sig.startswith('static '):
        ret, rest = sig.rsplit(' ', 1)[0], sig.rsplit(' ', 1)[1]
        k = sig.index('(')
        ret = sig[:k].rsplit(' ', 1)[0]
        rest = sig[len(ret) + 1:]
    elif sig.count(' ') and not sig.startswith(('void', 'int', 'const', 'unsigned')):
        ret, rest = sig.split(' ', 1)
    fn.append(ret)
    if len(rest) > 80:
        head, params = rest.split('(', 1)
        parts = params[:-1].split(', ')
        col = len(head) + 1
        out_lines = []
        cur = ''
        for k, part in enumerate(parts):
            piece = part + (', ' if k < len(parts) - 1 else ')')
            if cur and col + len(cur) + len(piece.rstrip()) > 80:
                out_lines.append(cur.rstrip())
                cur = ''
            cur += piece
        out_lines.append(cur.rstrip())
        rest = head + '(' + out_lines[0] + ''.join('\n' + ' ' * col + l for l in out_lines[1:])
    fn.append(rest)
    fn.append('{')
    # a parameter the lifted code never names is silenced, not dropped:
    # the signature is the family's, the body is what it is
    params = re.findall(r'\b([a-z_]\w*)\s*(?:,|\)\s*$)', rest)
    unused = [q for q in params if not re.search(r'\b%s\b' % q, text) and q != 'void']
    if unused:
        fn.append('\n'.join('    (void)%s;' % q for q in unused))
    if pre:
        fn.append('    ' + pre.replace('\\n', '\n    ') + '\n')
    fn.append(text)
    if post:
        fn.append('    ' + post.replace('\\n', '\n    '))
    fn.append('}')
    lines[lo - 1:hi] = call.replace('\\n', '\n').split('\n')
    wr(path(src), '\n'.join(lines))
    tp = path(target)
    if os.path.exists(tp):
        wr(tp, rd(tp).rstrip('\n') + '\n\n' + '\n'.join(fn) + '\n')
    else:
        hdr = file_comment(target, brief or suffix) + ['', '#include "%s"' % fam.hdr, '']
        wr(tp, '\n'.join(hdr) + '\n' + '\n'.join(fn) + '\n')
        if mk:
            add_to_mk(mk, tp)
    if loose:
        forward_decls(tp)
    else:
        sync(fam, False)
        sync(fam, False)
    print('cut %d lines into %s in %s' % (hi - lo + 1, rest.split('(')[0], target))


def hoist(d, lo, hi, start=None):
    fam = Family(d)
    lines = rd(fam.path(fam.main)).split('\n')
    if start is not None:
        lo = next((k + 1 for k, l in enumerate(lines) if l.startswith(start)), None)
        if lo is None:
            sys.exit('no line starts with %r' % start)
        hi = next(k + 1 for k in range(lo, len(lines)) if lines[k].startswith('}'))
        # a doc comment above the block goes with it
        if lo >= 2 and lines[lo - 2].rstrip().endswith('*/'):
            k = lo - 2
            while k >= 0 and not lines[k].lstrip().startswith('/*'):
                k -= 1
            lo = k + 1
    block = '\n'.join(lines[lo - 1:hi])
    e = hi
    while e < len(lines) and lines[e].strip() == '':
        e += 1
    del lines[lo - 1:e]
    wr(fam.path(fam.main), '\n'.join(lines))
    h = rd(fam.path(fam.hdr))
    h = h.replace(GUARD_A, block + '\n\n' + GUARD_A, 1)
    wr(fam.path(fam.hdr), h)
    sync(fam, False)
    sync(fam, False)
    print('hoisted %d lines into %s' % (hi - lo + 1, fam.hdr))


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    cmd = argv[0]
    args = argv[1:]
    if cmd == 'report':
        names = '--names' in args
        for p in [a for a in args if a != '--names']:
            report(p, names)
        return 0
    opts = dict(dir=None, inline=[], public=[], mk=None, lines=[], section=None, tag=None,
                into=None, sig=None, call=None, brief=None, pre=None, post=None, **{'from': None, 'file': None, 'case-return': None},
                subst=[], case=False)
    rest = []
    while args:
        a = args.pop(0)
        if a == '--dir':
            opts['dir'] = args.pop(0)
        elif a == '--inline':
            opts['inline'] = args.pop(0).split(',')
        elif a == '--public':
            opts['public'].append(args.pop(0))
        elif a == '--mk':
            opts['mk'] = args.pop(0)
        elif a == '--tag':
            opts['tag'] = args.pop(0)
        elif a in ('--into', '--sig', '--call', '--brief', '--pre', '--post', '--from', '--file', '--case-return'):
            opts[a[2:]] = args.pop(0)
        elif a == '--subst':
            pat, _, rep = args.pop(0).partition('=>')
            opts['subst'].append((pat, rep))
        elif a == '--case':
            opts['case'] = True
        elif a == '--section':
            opts['section'] = args.pop(0)
        elif a == '--lines':
            lo, hi = args.pop(0).split('-')
            opts['lines'].append((int(lo), int(hi)))
        else:
            rest.append(a)
    if cmd == 'bootstrap':
        bootstrap(rest[0], opts['dir'], opts['inline'], opts['public'], opts['mk'], opts['tag'])
    elif cmd == 'move':
        move(opts['dir'], rest[0], rest[1], rest[2:], opts['lines'], opts['mk'],
             opts['section'])
    elif cmd == 'cut':
        (lo, hi), = opts['lines']
        cut(opts['dir'], lo, hi, opts['into'], opts['sig'], opts['call'], opts['brief'],
            opts['pre'], opts['post'], opts['subst'], opts['case'], opts['mk'], opts['file'],
            opts['case-return'])
    elif cmd == 'hoist':
        if opts['lines']:
            (lo, hi), = opts['lines']
            hoist(opts['dir'], lo, hi)
        else:
            hoist(opts['dir'], 0, 0, opts['from'])
    elif cmd == 'sync':
        fam = Family(opts['dir'])
        sync(fam)
        sync(fam)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
