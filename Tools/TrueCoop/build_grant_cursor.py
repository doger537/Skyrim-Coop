"""Build our separate nonconstant-global receipt plugin. No vanilla overrides."""
import argparse
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sub(tag, value):
    return tag + struct.pack('<H', len(value)) + value


def text(tag, value):
    return sub(tag, value.encode('ascii') + b'\0')


def record(tag, value, form_id=0):
    return struct.pack('<4sIIIIHH', tag, len(value), 0, form_id, 0, 44, 0) + value


def build():
    ids = (ROOT / 'Code/common/QuestGrantCursorIds.h').read_text(encoding='utf-8')
    first = int(re.search(r'kFirstLocalId = (0x[0-9A-Fa-f]+)', ids)[1], 16)
    world = int(re.search(r'kWorldWords = ([0-9]+)', ids)[1])
    cursor = int(re.search(r'kCursorWords = ([0-9]+)', ids)[1])
    if world != 8 or cursor != 4:
        raise ValueError('Unsupported receipt global layout')
    names = [f'TrueCoopGrantWorld{i}' for i in range(world)]
    names += [f'TrueCoopGrantRevision{i}' for i in range(cursor)]
    names += ['TrueCoopGrantVersion']
    header = sub(b'HEDR', struct.pack('<fII', 1.7, len(names), first + len(names)))
    header += text(b'CNAM', 'TrueCoop saved world receipts')
    header += text(b'MAST', 'Skyrim.esm') + sub(b'DATA', bytes(8))
    records = b''
    for i, name in enumerate(names):
        payload = text(b'EDID', name) + sub(b'FNAM', b'f') + sub(b'FLTV', bytes(4))
        records += record(b'GLOB', payload, 0x01000000 | (first + i))
    group = struct.pack('<4sI4sIII', b'GRUP', 24 + len(records), b'GLOB', 0, 0, 0) + records
    return record(b'TES4', header) + group


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'GameFiles/Skyrim/TrueCoopWorldState.esp'
    expected = build()
    if output.exists():
        if output.read_bytes() != expected:
            raise SystemExit('Existing receipt plugin differs; preserve it and review the layout change')
    elif args.check:
        raise SystemExit('Receipt plugin is missing')
    else:
        with output.open('xb') as stream:
            stream.write(expected)
    print(f'Receipt plugin: {output.name}, 13 new globals, 0 overrides, {len(expected)} bytes')
