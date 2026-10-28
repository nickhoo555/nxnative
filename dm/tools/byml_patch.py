"""Read existing BYML dictionary scalars and patch signed integers in place.

This is not a BYML serializer: node offsets, strings, types and unrelated bytes stay
unchanged. It accepts the little/big-endian dictionary layouts used by heap settings.
"""
import struct


class Dictionary:
    def __init__(self, data):
        self.data = bytes(data)
        if len(data) < 16 or data[:2] not in (b'YB', b'BY'):
            raise ValueError('not a BYML dictionary document')
        self.order = 'little' if data[:2] == b'YB' else 'big'
        self.fmt = '<' if self.order == 'little' else '>'
        self.keys = self.strings(self.uint(4))
        self.root = self.uint(12)
        self.block(self.root, 4)
        if self.data[self.root] != 0xc1:
            raise ValueError('BYML root is not a dictionary')

    def block(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError('BYML offset outside the document')
        return self.data[offset:offset + size]

    def uint(self, offset):
        return int.from_bytes(self.block(offset, 4), self.order)

    def count(self, offset):
        return int.from_bytes(self.block(offset + 1, 3), self.order)

    def strings(self, offset):
        if self.block(offset, 1) != b'\xc2':
            raise ValueError('BYML string table missing')
        count = self.count(offset)
        self.block(offset + 4, count * 4)
        result = []
        for i in range(count):
            start = offset + self.uint(offset + 4 + 4 * i)
            self.block(start, 1)
            end = self.data.find(b'\0', start)
            if end < 0:
                raise ValueError('unterminated BYML string')
            result.append(self.data[start:end].decode('utf-8'))
        return result

    def field(self, path):
        if not path:
            raise ValueError('a nonempty dictionary path is required')
        node = self.root
        for i, key in enumerate(path):
            if self.block(node, 1) != b'\xc1':
                raise ValueError(f'not a BYML dictionary at {path[:i]}')
            count = self.count(node)
            self.block(node + 4, count * 8)
            found = []
            for j in range(count):
                entry = node + 4 + j * 8
                index = int.from_bytes(self.block(entry, 3), self.order)
                if index >= len(self.keys):
                    raise ValueError('BYML key index outside the string table')
                if self.keys[index] == key:
                    found.append((self.data[entry + 3], entry + 4))
            if len(found) != 1:
                raise ValueError(f'BYML dictionary path missing or duplicated: {path[:i + 1]}')
            kind, value = found[0]
            if i == len(path) - 1:
                return kind, value
            if kind != 0xc1:
                raise ValueError(f'not a BYML dictionary at {path[:i + 1]}')
            node = self.uint(value)
        raise AssertionError('unreachable')

    def read(self, path):
        kind, value = self.field(path)
        if kind == 0xd1:
            return struct.unpack(self.fmt + 'i', self.block(value, 4))[0]
        if kind == 0xa0:
            strings = self.strings(self.uint(8))
            index = self.uint(value)
            if index >= len(strings):
                raise ValueError('BYML value index outside the string table')
            return strings[index]
        raise ValueError(f'unsupported BYML scalar type {kind:#x} at {path}')

    def replace_ints(self, values):
        edits = []
        for path, value in values.items():
            if type(value) is not int or not -(1 << 31) <= value < 1 << 31:
                raise ValueError('BYML signed integer outside the 32-bit range')
            kind, offset = self.field(path)
            if kind != 0xd1:
                raise ValueError(f'not a signed BYML integer at {path}')
            edits.append((offset, struct.pack(self.fmt + 'i', value)))
        data = bytearray(self.data)
        for offset, value in edits:
            data[offset:offset + 4] = value
        return bytes(data)
