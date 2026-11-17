"""Critical bundled-asset checks; no Skyrim process or gameplay is executed."""
import hashlib
import struct
import unittest
from pathlib import Path

import build_player_markers as builder

ROOT = Path(__file__).resolve().parents[2]
OLD = ROOT / 'Code/tests/data/SkyrimTogether-core-legacy.esp'
NEW = ROOT / 'GameFiles/Skyrim/SkyrimTogether.esp'
LEGACY_SHA = '3196266aa29855574bda48335da9ab2f18ad33a80eee116400da7c5c81c93d7e'
MARKER_SHA = '9ef8a8f8bb08dca12f43355e073adf0a3bd69cb4f947964fec10a278a1f7694b'


def records(blob):
    result = {}
    def scan(start, end):
        for a, b, kind in builder.entries(blob, start, end):
            if kind == b'GRUP':
                scan(a + 24, b)
            elif kind != b'TES4':
                key = (kind, struct.unpack_from('<I', blob, a + 12)[0])
                if key in result:
                    raise ValueError('Duplicate record')
                result[key] = blob[a:b]
    scan(0, len(blob))
    return result


def subrecords(record):
    result = []
    offset = 24
    while offset < len(record):
        kind, size = struct.unpack_from('<4sH', record, offset)
        result.append((kind, record[offset + 6:offset + 6 + size]))
        offset += size + 6
    if offset != len(record):
        raise ValueError('Invalid subrecord boundary')
    return result


class MarkerAssetTests(unittest.TestCase):
    def test_known_hashes_and_reproducible_addition(self):
        old, new = OLD.read_bytes(), NEW.read_bytes()
        self.assertEqual(hashlib.sha256(old).hexdigest(), LEGACY_SHA)
        self.assertEqual(hashlib.sha256(new).hexdigest(), MARKER_SHA)
        self.assertEqual(builder.add_quest(old, 0x4DF4, 8), new)
        self.assertEqual(builder.add_quest(new, 0x4DF4, 8), new)

    def test_all_existing_records_are_byte_identical(self):
        old, new = records(OLD.read_bytes()), records(NEW.read_bytes())
        self.assertEqual(len(new), len(old) + 2)
        for key, value in old.items():
            self.assertEqual(new[key], value, key)
        self.assertEqual(set(new) - set(old), {(b'QUST', 0x02004DF4), (b'STAT', 0x02004DF5)})

    def test_marker_quest_has_no_scripts_stages_or_ai_effects(self):
        quest = subrecords(records(NEW.read_bytes())[(b'QUST', 0x02004DF4)])
        keys = [kind for kind, _ in quest]
        for forbidden in (b'VMAD', b'INDX', b'QSDT', b'ALPC', b'ALSP', b'ALFC', b'COED'):
            self.assertNotIn(forbidden, keys)
        self.assertEqual([struct.unpack('<I', value)[0] for kind, value in quest if kind == b'ALST'], list(range(8)))
        self.assertEqual([struct.unpack('<H', value)[0] for kind, value in quest if kind == b'QOBJ'], list(range(8)))
        self.assertEqual([struct.unpack('<II', value)[0] for kind, value in quest if kind == b'QSTA'], list(range(8)))
        self.assertEqual(sum(kind == b'ALED' for kind, _ in quest), 8)
        alias = False
        for kind, value in quest:
            if kind == b'ALST':
                alias = True
            if kind == b'FNAM' and alias:
                flags, = struct.unpack('<I', value)
                self.assertEqual(flags, 0x29A)
                self.assertEqual(flags & (1 | 4 | 64 | 1024), 0)
        anchor = subrecords(records(NEW.read_bytes())[(b'STAT', 0x02004DF5)])
        self.assertEqual([kind for kind, _ in anchor], [b'EDID', b'OBND'])
        self.assertEqual(anchor[1][1], bytes(12))

    def test_conflicting_marker_identity_is_refused(self):
        new = bytearray(NEW.read_bytes())
        offset = new.index(b'TrueCoopPlayerMarkers\0')
        new[offset] = ord('X')
        with self.assertRaises(ValueError):
            builder.add_quest(bytes(new), 0x4DF4, 8)

    def test_truncated_asset_is_refused(self):
        old = OLD.read_bytes()
        for length in (0, 12, 23, 24, 100, len(old) - 1):
            with self.subTest(length=length), self.assertRaises((ValueError, struct.error)):
                builder.add_quest(old[:length], 0x4DF4, 8)


if __name__ == '__main__':
    unittest.main()
