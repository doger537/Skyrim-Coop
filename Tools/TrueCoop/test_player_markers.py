"""Critical bundled-asset checks; no Skyrim process or gameplay is executed."""
import hashlib
import re
import struct
import unittest
from pathlib import Path

import build_player_markers as builder
from build_character_identity import add_identity
from build_checkpoint_globals import add_checkpoint

ROOT = Path(__file__).resolve().parents[2]
OLD = ROOT / 'Code/tests/data/SkyrimTogether-core-legacy.esp'
NEW = ROOT / 'GameFiles/Skyrim/SkyrimTogether.esp'
LEGACY_SHA = '3196266aa29855574bda48335da9ab2f18ad33a80eee116400da7c5c81c93d7e'
MARKER_SHA = '9ef8a8f8bb08dca12f43355e073adf0a3bd69cb4f947964fec10a278a1f7694b'
IDENTITY_SHA = 'ff52f44a06d57bded74a2a306966844845ec254d8b9ae015b30ec493ff224502'
CHECKPOINT_SHA = 'c16976f0eff6490c17bc53d2ef5c7651250b4c9de9dc525f9261d913aa90806e'


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
        marker = builder.add_quest(old, 0x4DF4, 8)
        self.assertEqual(hashlib.sha256(marker).hexdigest(), MARKER_SHA)
        identity = add_identity(marker)
        self.assertEqual(hashlib.sha256(identity).hexdigest(), IDENTITY_SHA)
        self.assertEqual(hashlib.sha256(new).hexdigest(), CHECKPOINT_SHA)
        self.assertEqual(add_checkpoint(identity), new)
        self.assertEqual(add_checkpoint(new), new)
        self.assertEqual(add_identity(new), new)
        self.assertEqual(builder.add_quest(new, 0x4DF4, 8), new)

    def test_all_existing_records_are_byte_identical(self):
        old, new = records(OLD.read_bytes()), records(NEW.read_bytes())
        self.assertEqual(len(new), len(old) + 15)
        for key, value in old.items():
            self.assertEqual(new[key], value, key)
        self.assertEqual(set(new) - set(old), {(b'QUST', 0x02004DF4), (b'STAT', 0x02004DF5)} | {(b'GLOB', 0x02004DF6 + i) for i in range(13)})

    def test_identity_globals_are_nonconstant_exact_float_slots_without_scripts(self):
        header = (ROOT / 'Code/common/CharacterIdentityIds.h').read_text()
        self.assertEqual(int(re.search(r'kFirstPartLocalId = (0x[0-9A-F]+)', header)[1], 16), 0x4DF6)
        self.assertEqual(int(re.search(r'kVersionLocalId = (0x[0-9A-F]+)', header)[1], 16), 0x4DFE)
        current = records(NEW.read_bytes())
        for part in range(13):
            record = current[(b'GLOB', 0x02004DF6 + part)]
            self.assertEqual(struct.unpack_from('<I', record, 8)[0], 0)
            fields = subrecords(record)
            self.assertEqual([tag for tag, _ in fields], [b'EDID', b'FNAM', b'FLTV'])
            expected = f'TrueCoopCharacterPart{part}' if part < 8 else 'TrueCoopCharacterVersion' if part == 8 else f'TrueCoopCheckpointPart{part - 9}'
            self.assertEqual(fields[0][1], expected.encode() + b'\0')
            self.assertEqual(fields[1][1], b'f')
            self.assertEqual(fields[2][1], bytes(4))
        changed = bytearray(NEW.read_bytes())
        changed[changed.index(b'TrueCoopCharacterPart0\0')] = ord('X')
        with self.assertRaises(ValueError):
            add_identity(bytes(changed))

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
