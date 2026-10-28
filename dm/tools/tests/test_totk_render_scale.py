"""Synthetic BYML: the real texture-pool / parent-budget mismatch, no game files."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from byml_patch import Dictionary

spec = importlib.util.spec_from_file_location('totk_render_scale', Path(__file__).resolve().parents[2] / 'games/0100F2C0115B6000/render_scale.py')
scale = importlib.util.module_from_spec(spec); spec.loader.exec_module(scale)


def fixture(endian='<', child=113664, parent=201728):
    order = 'little' if endian == '<' else 'big'
    keys = ['Heaps', 'Graphics', 'DynamicTextureAllocatorMem', 'Size', 'Parent', 'Other']
    data = bytearray((b'YB' if endian == '<' else b'BY') + struct.pack(endian + 'HIII', 7, 0, 0, 0))
    def strings(values):
        offset = len(data)
        data.extend(b'\xc2' + len(values).to_bytes(3, order) + bytes(4 * (len(values) + 1)))
        for i, value in enumerate(values):
            struct.pack_into(endian + 'I', data, offset + 4 + i * 4, len(data)-offset)
            data.extend(value.encode() + b'\0')
        struct.pack_into(endian + 'I', data, offset + 4 + len(values) * 4, len(data)-offset)
        data.extend(bytes((-len(data)) % 4))
        return offset
    def node(entries):
        offset = len(data); data.extend(b'\xc1' + len(entries).to_bytes(3, order))
        for key, kind, value in entries:
            data.extend(keys.index(key).to_bytes(3, order) + bytes([kind]) + struct.pack(endian + 'I', value))
        return offset
    kt, vt = strings(keys), strings(['Graphics'])
    a = node([('Size', 0xd1, child), ('Parent', 0xa0, 0)])
    b = node([('Size', 0xd1, parent)])
    h = node([('DynamicTextureAllocatorMem', 0xc1, a), ('Graphics', 0xc1, b), ('Other', 0xd1, 12345)])
    root = node([('Heaps', 0xc1, h)])
    struct.pack_into(endian + 'III', data, 4, kt, vt, root)
    return bytes(data)


class RenderScale(unittest.TestCase):
    def test_pool_and_parent_grow_together_for_1440p_and_4k(self):
        for endian in ('<', '>'):
            for factor, expected in ((1.6, 291008), (2.4, 654720)):
                with self.subTest(endian=endian, factor=factor):
                    original = fixture(endian)
                    updated, plan = scale.adapt(original, factor)
                    old, new = Dictionary(original), Dictionary(updated)
                    self.assertEqual(new.read(scale.DYNAMIC), expected)
                    self.assertEqual(new.read(scale.GRAPHICS), 201728 + expected - 113664)
                    self.assertEqual(new.read(scale.GRAPHICS)-new.read(scale.DYNAMIC), 201728-113664)
                    self.assertEqual(new.read(('Heaps', 'Other')), 12345)
                    self.assertEqual(len(original), len(updated))
                    allowed = {o+i for p in (scale.DYNAMIC, scale.GRAPHICS) for o in [old.field(p)[1]] for i in range(4)}
                    self.assertTrue(all(i in allowed for i, (a,b) in enumerate(zip(original,updated)) if a != b))
                    self.assertEqual(plan['dynamic_KiB'][0], 113664)

    def test_native_and_lower_scales_preserve_every_byte(self):
        original = fixture(child=113665)
        for factor in (0.5, 0.75, 1):
            self.assertEqual(scale.adapt(original, factor)[0], original)

    def test_bad_scale_hierarchy_or_overflow_is_rejected(self):
        for factor in (True, float('nan'), float('inf'), 0.49, 3.01):
            with self.subTest(factor=factor), self.assertRaises(ValueError): scale.adapt(fixture(), factor)
        with self.assertRaises(ValueError): scale.adapt(fixture(child=201729), 1.6)
        with self.assertRaises(ValueError): scale.adapt(fixture(child=1<<30, parent=1<<30), 3)

    def test_invalid_dictionary_paths_types_and_offsets_are_rejected(self):
        original = fixture(); doc = Dictionary(original)
        for path in ((), ('Missing',), ('Heaps', 'Graphics', 'Missing')):
            with self.subTest(path=path), self.assertRaises(ValueError): doc.field(path)
        with self.assertRaises(ValueError): doc.replace_ints({('Heaps', 'DynamicTextureAllocatorMem', 'Parent'): 3})
        for value in (True, 1.5, 1<<31):
            with self.subTest(value=value), self.assertRaises(ValueError): doc.replace_ints({scale.DYNAMIC: value})
        for raw in (b'', original[:24], original[:-2]):
            with self.subTest(length=len(raw)), self.assertRaises(ValueError): Dictionary(raw).read(scale.DYNAMIC)

    def test_mac_profile_preserves_heap_and_mapping_headroom(self):
        source = 'mem_reserve\t0x140000000\nmem_alias\t0x98000000\napp_pool\t0xCD500000\n'
        for extra_kib in (0, 291008-113664, 654720-113664):
            result, changes = scale.mac_profile(source, extra_kib)
            delta = changes['app_pool'][1] - changes['app_pool'][0]
            self.assertGreaterEqual(delta, extra_kib*1024)
            self.assertLess(delta-extra_kib*1024, 0x200000)
            self.assertEqual(delta % 0x200000, 0)
            self.assertEqual(changes['mem_reserve'][1]-changes['mem_reserve'][0], delta)
            self.assertIn('mem_alias\t0x98000000\n', result)
        for invalid in ('app_pool\t0x100\n', source+'app_pool\t0x123\n'):
            with self.assertRaises(ValueError): scale.mac_profile(invalid, 1024)
        for invalid in (-1, 0.5, True):
            with self.assertRaises(ValueError): scale.mac_profile(source, invalid)

    def test_output_cannot_follow_a_symlink_into_original_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); original = root/'original'; out = root/'overlay'
            original.mkdir(); out.mkdir(); (out/'System').symlink_to(original, target_is_directory=True)
            with self.assertRaises(ValueError): scale.destination(out, Path('System/HeapDef.zs'))
            target = scale.destination(out, 'plan.json')
            scale.atomic_write(target, b'one'); scale.atomic_write(target, b'two')
            self.assertEqual(target.read_bytes(), b'two')
            self.assertEqual(list(original.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
