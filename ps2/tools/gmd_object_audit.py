#!/usr/bin/env python3
"""Audit a .gmd level against the objects this engine can draw/run.

Usage: python ps2/tools/gmd_object_audit.py LEVEL.gmd [...]

Reads the object table in source/objects.c (ID = row index) and the object
IDs used by romfs/main_levels/*.gmd, then prints, per level: object count,
how many objects have an ID the engine has no art for (ID >= GAME_OBJECT_COUNT
or an empty row), which IDs no official level uses, group usage, and the
object keys (properties) no official level uses.
"""
import base64, collections, glob, os, re, sys, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
# Rows that exist in the table but are logic-only objects the engine handles
# elsewhere (fade/BG/ground/color triggers); they are not "missing".
LOGIC_IDS = {22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 56, 57, 58, 59, 105, 744,
             899, 915, 1329}


def objects_of(path):
    text = open(path, encoding='utf-8', errors='ignore').read()
    m = re.search(r'<k>k4</k><s>([^<]*)</s>', text)
    raw = zlib.decompress(base64.urlsafe_b64decode(m.group(1) + '=='), 47)
    out = []
    for part in raw.decode('latin1').split(';')[1:]:
        f = part.split(',')
        if len(f) >= 2:
            out.append(dict(zip(f[0::2], f[1::2])))
    return out


def engine_known_ids():
    src = open(os.path.join(ROOT, 'source', 'objects.c'), encoding='utf-8').read()
    body = src[src.index('game_objects[GAME_OBJECT_COUNT] = {'):]
    rows = re.findall(r'^\s*\{\s*(-?\d+),', body, re.M)
    return {i for i, r in enumerate(rows) if int(r) != -1}


def main(paths):
    known = engine_known_ids()
    base_ids, base_keys = collections.Counter(), collections.defaultdict(set)
    for f in glob.glob(os.path.join(ROOT, 'romfs', 'main_levels', '*.gmd')):
        for o in objects_of(f):
            i = int(o['1'])
            base_ids[i] += 1
            base_keys[i] |= set(o) - {'1'}
    for path in paths:
        objs = objects_of(path)
        ids = collections.Counter(int(o['1']) for o in objs)
        no_art = {i: c for i, c in ids.items()
                  if i not in known and i not in LOGIC_IDS}
        new = {i: c for i, c in ids.items() if not base_ids[i]}
        print('==', os.path.basename(path), '-', len(objs), 'objects,', len(ids), 'distinct IDs')
        print('  no engine art/handler: %d IDs, %d objects (%d%%)' % (
            len(no_art), sum(no_art.values()), 100 * sum(no_art.values()) // max(1, len(objs))))
        print('  IDs no official level uses:', len(new))
        print('  missing IDs (id:count):', ', '.join('%d:%d' % kv for kv in sorted(no_art.items())))
        groups = collections.Counter()
        for o in objs:
            for g in o.get('57', '').split('.'):
                if g:
                    groups[int(g)] += 1
        if groups:
            print('  groups: %d objects, %d distinct groups, max id %d, largest group %d' % (
                sum(1 for o in objs if '57' in o), len(groups), max(groups), max(groups.values())))
        newkeys = collections.Counter()
        for o in objs:
            if not base_ids[int(o['1'])]:
                continue
            for k in o:
                if k != '1' and k not in base_keys[int(o['1'])]:
                    newkeys[k] += 1
        print('  keys unseen for IDs the official levels DO use (key:objects):',
              ', '.join('%s:%d' % kv for kv in sorted(newkeys.items(), key=lambda kv: int(kv[0]))))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
