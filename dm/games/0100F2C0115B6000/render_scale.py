#!/usr/bin/env python3
"""Prepare an isolated TOTK HeapDef overlay for DM_RENDER_SCALE > 1 on Mac.

The game's file, not this tool, supplies all base sizes. Graphics includes the
dynamic texture pool, so increase it by the child's delta, not its whole size.
The original RomFS is read-only input; set DM_ROMFS_OVERLAY to the output folder
and DM_RENDER_SCALE to the same factor when testing. No game data is committed.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile

HERE = Path(__file__).resolve().parent
DM = HERE.parents[1]
sys.path.insert(0, str(DM / 'tools'))
from byml_patch import Dictionary

HEAPDEF = Path('System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs')
DYNAMIC = ('Heaps', 'DynamicTextureAllocatorMem', 'Size')
GRAPHICS = ('Heaps', 'Graphics', 'Size')


def adapt(data, scale):
    if isinstance(scale, bool) or not math.isfinite(scale) or not 0.5 <= scale <= 3:
        raise ValueError('render scale must be finite and between 0.5 and 3.0')
    doc = Dictionary(data)
    child, parent = doc.read(DYNAMIC), doc.read(GRAPHICS)
    if type(child) is not int or type(parent) is not int or not 0 < child <= parent:
        raise ValueError('invalid Graphics / DynamicTextureAllocatorMem budgets')
    if doc.read(('Heaps', 'DynamicTextureAllocatorMem', 'Parent')) != 'Graphics':
        raise ValueError('dynamic texture pool no longer belongs to Graphics')
    # Match the float accepted by the loader; retain the old budgets at <= 1.
    effective = struct.unpack('<f', struct.pack('<f', scale))[0]
    grown = math.ceil(child * effective * effective / 64) * 64 if effective > 1 else child
    values = {DYNAMIC: grown, GRAPHICS: parent + grown - child}
    updated = doc.replace_ints(values) if grown != child else bytes(data)
    return updated, {'scale': scale, 'effective_scale': effective,
                     'dynamic_KiB': [child, grown], 'graphics_KiB': [parent, parent + grown - child]}


def mac_profile(source, extra_kib):
    # nninitStartup subtracts the file's VirtualAddressMemory reserve from the
    # reported application pool before SetHeapSize. Preserve its original spare
    # space as Graphics grows, and leave alias/stack sizes and their mapping
    # budget unchanged. SetHeapSize uses 2 MiB units. This is a derived Mac-only
    # profile, never a change to the phone-tested profile in the repository.
    if type(extra_kib) is not int or extra_kib < 0:
        raise ValueError('Mac memory growth must be a nonnegative integer in KiB')
    extra = (extra_kib * 1024 + 0x1fffff) & ~0x1fffff
    seen, rows, changes = set(), [], {}
    for line in source.splitlines(keepends=True):
        fields = line.rstrip('\r\n').split('\t')
        if fields[0] in ('mem_reserve', 'app_pool'):
            if len(fields) != 2 or fields[0] in seen:
                raise ValueError('invalid or duplicate Mac memory-profile row')
            seen.add(fields[0]); old = int(fields[1], 0)
            changes[fields[0]] = [old, old + extra]
            line = f'{fields[0]}\t{old + extra:#x}\n'
        rows.append(line)
    if seen != {'mem_reserve', 'app_pool'}:
        raise ValueError('Mac memory profile needs mem_reserve and app_pool')
    return ''.join(rows), changes


def destination(root, relative):
    target = root / relative
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError('output symlink escapes the overlay/profile directory')
    return target


def atomic_write(target, data):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as f:
            temporary = Path(f.name); f.write(data)
        temporary.replace(target)
    finally:
        if temporary is not None: temporary.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--romfs', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--scale', required=True, type=float)
    a = ap.parse_args()
    source, out = a.romfs.resolve(), a.out.resolve()
    profile_root = out.with_name(out.name + '-profile').resolve()
    if any(p.is_relative_to(source) or p.is_relative_to(DM.parent) for p in (out, profile_root)):
        ap.error('the overlay must be outside the original RomFS and repository')
    os.environ['DM_GAME'] = HERE.name
    sys.path.insert(0, str(DM / 'gfx/tools'))
    from shader_split import sarc_members, zstd_mod
    zstd = zstd_mod()
    compressed = (source / HEAPDEF).read_bytes()
    if len(compressed) < 6 or compressed[:4] != bytes.fromhex('28b52ffd'):
        ap.error('HeapDef is not a zstd frame')
    # Read the dictionary from the game's own pack, selected by the frame ID.
    fhd = compressed[4]
    size, offset = (0, 1, 2, 4)[fhd & 3], 6 - (fhd >> 5 & 1)
    did = int.from_bytes(compressed[offset:offset + size], 'little')
    options = {}
    if did:
        pack = zstd.decompress((source / 'Pack/ZsDic.pack.zs').read_bytes())
        dictionaries = {int.from_bytes(d[4:8], 'little'): d for _, d in sarc_members(pack) if d[:4] == bytes.fromhex('37a430ec')}
        if did not in dictionaries:
            ap.error(f'game dictionary {did} is missing')
        options['zstd_dict'] = zstd.ZstdDict(dictionaries[did])
    original = zstd.decompress(compressed, **options)
    try:
        updated, plan = adapt(original, a.scale)
    except ValueError as e:
        ap.error(str(e))
    encoded = zstd.compress(updated, **options) if updated != original else compressed
    assert zstd.decompress(encoded, **options) == updated
    try:
        target = destination(out, HEAPDEF)
        plan_file = destination(out, 'render_scale.json')
        profile = destination(profile_root, HERE.name).resolve()
        tables = [(destination(profile, table.name), table.read_bytes()) for table in HERE.glob('*.tsv') if table.name != 'game.tsv']
        profile_file = destination(profile, 'game.tsv')
        text, memory = mac_profile((HERE / 'game.tsv').read_text(), plan['dynamic_KiB'][1] - plan['dynamic_KiB'][0])
    except ValueError as e:
        ap.error(str(e))
    atomic_write(target, encoded)
    # Derived profiles share tables, but not patches.py: duplicating registered
    # build-ID patch claims in DM_GAMES_DIR would make the rewriter reject them.
    for path, content in tables: atomic_write(path, content)
    atomic_write(profile_file, text.encode())
    plan.update(mac_profile=str(profile), memory=memory)
    plan.update(source_sha256=hashlib.sha256(compressed).hexdigest(), overlay_sha256=hashlib.sha256(encoded).hexdigest())
    atomic_write(plan_file, (json.dumps(plan, indent=2) + '\n').encode())
    print(f'Overlay: {out}')
    print(f'Mac profile: {profile}')
    for name in ('dynamic_KiB', 'graphics_KiB'):
        print(f'{name}: {plan[name][0]} -> {plan[name][1]}')


if __name__ == '__main__':
    main()
