# -*- coding: utf-8 -*-
"""CR-50 consequence 3 — the full Evidence citation sweep.

Every source citation in Evidence, checked against the pinned checkouts. Read from
the **git object store** (`git show <commit>:<path>`), never the working tree, so a
sparse checkout, CRLF conversion or a local edit cannot move a coordinate.

**This tool decides coordinate validity and reports content. It does not decide
claim validity** — that needs the claim, and CR-50 is the demonstration that the two
are separate controls: PF-BF-23's content was right at every stage while all four of
its coordinates were wrong. `content_match` is adjudicated by reading, from the
extract this emits, and is left `?` here.

Emits TSV on stdout. Hard-fails on any citation whose path or tag cannot be resolved:
no "manual follow-up" bucket counts as clean.
"""
import io, os, re, subprocess, sys, collections

BASE = 'C:/Users/guygr/Documents/code/elrs/'
SCRATCH = os.path.dirname(os.path.abspath(__file__))
REPOS = {
    'BF':   (os.path.join(SCRATCH, 'bf'),   '2025.12.5', 'src/main/'),
    'INAV': (os.path.join(SCRATCH, 'inav'), '8.0.1',     'src/main/'),
}

def git(repo, *args):
    p = subprocess.run(['git', '-C', repo] + list(args),
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    return p.returncode, p.stdout, p.stderr

# ---- control 2: pin each repo to an immutable commit, once
COMMITS = {}
for plat, (repo, tag, _) in REPOS.items():
    rc, out, err = git(repo, 'rev-parse', tag + '^{commit}')
    if rc != 0:
        sys.exit('FATAL: cannot resolve tag %s in %s: %s' % (tag, repo, err.strip()))
    COMMITS[plat] = out.strip()
    rc2, out2, _ = git(repo, 'describe', '--tags', '--exact-match', COMMITS[plat])
    if rc2 != 0 or out2.strip() != tag:
        sys.exit('FATAL: %s does not describe as %s (got %r)' % (COMMITS[plat], tag, out2.strip()))

_filecache = {}
def blob(plat, path):
    key = (plat, path)
    if key not in _filecache:
        repo, _, prefix = REPOS[plat]
        rc, out, _ = git(repo, 'show', '%s:%s%s' % (COMMITS[plat], prefix, path))
        _filecache[key] = out.split('\n') if rc == 0 else None
    return _filecache[key]

# ---- parse Evidence into PF blocks, tracking platform and last-path context
ev = io.open(BASE + 'action-camera-bridge-evidence.md', encoding='utf-8').read()
lines = ev.split('\n')
starts = [i for i, l in enumerate(lines) if re.match(r'^### PF-', l)]
starts.append(len(lines))

TOK = re.compile(r'`([a-z_0-9/]+\.[ch]):(\d+)(?:-(\d+))?`|`:(\d+)(?:-(\d+))?`')
PLAT = re.compile(r'\[(BF|INAV|EXT)\]')

rows = []
facts = 0
for k in range(len(starts) - 1):
    i, j = starts[k], starts[k + 1]
    fid = re.match(r'^### (PF-[A-Z]+-\d+)', lines[i]).group(1)
    facts += 1
    plat = 'BF' if '-BF-' in fid else ('INAV' if '-INAV-' in fid else 'EXT')
    last_path = None
    for l in lines[i:j]:
        # A blockquoted version note is HISTORICAL RECORD, not a live citation. It
        # exists to say what a coordinate used to be and why it was wrong, so
        # verifying it against the pinned tree is a category error — the whole point
        # of PF-BF-23's note is that those coordinates do NOT resolve.
        if l.lstrip().startswith('>'):
            continue
        # Markers and tokens are merged into ONE position-ordered stream so the
        # platform context updates as each marker is passed. Resolving per line gets
        # `... ([BF]` / newline / `` `msp/msp.c:2179` `` wrong — the marker ends one
        # line and its citation starts the next — and a line-global scan gets
        # side-by-side `/* [BF] */ /* [INAV] */` fences wrong in the other direction.
        events = ([(m.start(), 'mark', m.group(1)) for m in PLAT.finditer(l)] +
                  [(m.start(), 'tok', m) for m in TOK.finditer(l)])
        for _, kind, payload in sorted(events, key=lambda x: x[0]):
            if kind == 'mark':
                plat = payload
                continue
            m = payload
            plat_tok = plat
            if m.group(1):
                path, s, e = m.group(1), int(m.group(2)), m.group(3)
                last_path = path
            else:
                if last_path is None:
                    rows.append([fid, plat_tok, '?', '?', '?', '?', '?', '?', 'ORPHAN-CONTINUATION',
                                 'bare :line with no preceding path'])
                    continue
                path, s, e = last_path, int(m.group(4)), m.group(5)
            e = int(e) if e else s
            rows.append([fid, plat_tok, path, s, e, None, None, None, None, None])

# ---- resolve every citation against the pinned object
fatal = []
out = []
for r in rows:
    fid, plat, path, s, e = r[0], r[1], r[2], r[3], r[4]
    if plat == 'EXT':
        out.append((fid, plat, 'n/a', path, s, e, 'n/a', 'n/a', '?', 'external-source', ''))
        continue
    src = blob(plat, path)
    if src is None:
        # try without a leading directory, and with the other common prefix
        alt = path.split('/')[-1]
        for cand in (alt, 'msp/' + alt, 'fc/' + alt, 'io/' + alt):
            src = blob(plat, cand)
            if src is not None:
                path = cand
                break
    if src is None:
        fatal.append('%s: %s [%s] does not resolve at %s' % (fid, path, plat, REPOS[plat][1]))
        out.append((fid, plat, COMMITS[plat][:12], path, s, e, 'UNRESOLVED', 'UNRESOLVED',
                    'no', 'FATAL-path', ''))
        continue
    n = len(src)
    inb = 1 <= s <= n and 1 <= e <= n
    extract = ' / '.join(x.strip() for x in src[s - 1:e][:3])[:150] if inb else ''
    out.append((fid, plat, COMMITS[plat][:12], path, s, e,
                'in-bounds' if inb else 'OUT-OF-BOUNDS', '%d lines' % n,
                '?', 'unchanged' if inb else 'FATAL-range', extract))

hdr = ['fact_id', 'platform', 'commit', 'path', 'recorded_start', 'recorded_end',
       'coordinate_status', 'file_lines', 'content_match', 'disposition', 'extract']
print('\t'.join(hdr))
for r in out:
    print('\t'.join(str(x) for x in r))

print('\n# repo identities')
for p, c in COMMITS.items():
    print('# %-5s %s  tag %s' % (p, c, REPOS[p][1]))
print('# facts checked     : %d' % facts)
print('# citations checked : %d' % len(out))
byp = collections.Counter(r[1] for r in out)
print('# by platform       : %s' % dict(byp))
if fatal:
    print('\n# FATAL — unresolved citations (%d):' % len(fatal))
    for f in fatal:
        print('#   ' + f)
    sys.exit(1)
print('# unresolved        : none')
