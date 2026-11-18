"""Build our separate nonconstant-global receipt plugin. No vanilla overrides."""
import argparse
import re
import struct
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sub(tag, value):
    return tag + struct.pack('<H', len(value)) + value


def text(tag, value):
    return sub(tag, value.encode('ascii') + b'\0')


def record(tag, value, form_id=0):
    return struct.pack('<4sIIIIHH', tag, len(value), 0, form_id, 0, 44, 0) + value


def build(legacy=False):
    ids = (ROOT / 'Code/common/QuestGrantCursorIds.h').read_text(encoding='utf-8')
    first = int(re.search(r'kFirstLocalId = (0x[0-9A-Fa-f]+)', ids)[1], 16)
    world = int(re.search(r'kWorldWords = ([0-9]+)', ids)[1])
    cursor = int(re.search(r'kCursorWords = ([0-9]+)', ids)[1])
    if world != 8 or cursor != 4:
        raise ValueError('Unsupported receipt global layout')
    names = [f'TrueCoopGrantWorld{i}' for i in range(world)]
    names += [f'TrueCoopGrantRevision{i}' for i in range(cursor)]
    names += ['TrueCoopGrantVersion']
    if not legacy:
        registry = (ROOT / 'Code/common/WorldItemRegistryIds.h').read_text(encoding='utf-8')
        number = lambda key: int(re.search(key + r' = (0x[0-9A-Fa-f]+|[0-9]+)', registry)[1], 0)
        slots, words = number('kSlots'), number('kWords')
        if slots != 64 or words != 4:
            raise ValueError('Unsupported world item registry layout')
        names += [f'TrueCoopWorldItem{slot}Word{word}' for slot in range(slots) for word in range(words)]
        names += [f'TrueCoopItemReceiptWord{word}' for word in range(words)] + ['TrueCoopItemReceiptVersion']
    header = sub(b'HEDR', struct.pack('<fII', 1.7, len(names) + (0 if legacy else 1), first + len(names) if legacy else number('kReceiptVersionLocalId') + 1))
    header += text(b'CNAM', 'TrueCoop saved world receipts')
    header += text(b'MAST', 'Skyrim.esm') + sub(b'DATA', bytes(8))
    records = b''
    for i, name in enumerate(names):
        payload = text(b'EDID', name) + sub(b'FNAM', b'f') + sub(b'FLTV', bytes(4))
        local = first + i
        if not legacy and i >= world + cursor + 1:
            index = i - (world + cursor + 1)
            local = number('kFirstIdLocalId') + index if index < slots * words else number('kReceiptFirstLocalId') + index - slots * words
        records += record(b'GLOB', payload, 0x01000000 | local)
    group = struct.pack('<4sI4sIII', b'GRUP', 24 + len(records), b'GLOB', 0, 0, 0) + records
    if legacy:
        return record(b'TES4', header) + group
    quest = text(b'EDID', 'TrueCoopWorldItemRegistry')
    quest += sub(b'DNAM', struct.pack('<HBBfI', 0, 0, 0xFF, 0., 6)) + sub(b'NEXT', b'')
    quest += sub(b'ANAM', struct.pack('<I', slots))
    for slot in range(slots):
        quest += sub(b'ALST', struct.pack('<I', slot)) + text(b'ALID', f'WorldItem{slot}')
        # Same verified optional/non-quest-object alias flags as our markers.
        quest += sub(b'FNAM', struct.pack('<I', 0x29A)) + sub(b'ALFR', bytes(4)) + sub(b'VTCK', bytes(4)) + sub(b'ALED', b'')
    quest = record(b'QUST', quest, 0x01000000 | number('kQuestLocalId'))
    quest_group = struct.pack('<4sI4sIII', b'GRUP', 24 + len(quest), b'QUST', 0, 0, 0) + quest
    return record(b'TES4', header) + group + quest_group


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--extend-registry', action='store_true', help='Extend only our exact unpublished 13-global plugin')
    args = parser.parse_args()
    output = ROOT / 'GameFiles/Skyrim/TrueCoopWorldState.esp'
    expected = build()
    if output.exists():
        if output.read_bytes() != expected:
            old = output.read_bytes()
            if args.check or not args.extend_registry or old != build(legacy=True) or hashlib.sha256(old).hexdigest() != '0845abf539d68f014643f16e43dc3a59ceb411990b2e14c6aeed123741bfaf8c':
                raise SystemExit('Existing receipt plugin differs; preserve it and review the layout change')
            output.write_bytes(expected)
    elif args.check:
        raise SystemExit('Receipt plugin is missing')
    else:
        with output.open('xb') as stream:
            stream.write(expected)
    print(f'World state plugin: {output.name}, saved receipts + 64 optional object aliases, 0 overrides, {len(expected)} bytes')
