"""Add our script-free marker quest to the bundled GPL SkyrimTogether plugin.

Preserves every existing record, except TES4 HEDR count/next-ID and QUST group
size. Does not read or modify the installed game. Re-running is idempotent.
"""
import argparse
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sub(tag, value):
    if len(tag) != 4 or len(value) > 65535:
        raise ValueError('Invalid subrecord')
    return tag + struct.pack('<H', len(value)) + value


def text(tag, value):
    return sub(tag, value.encode('utf-8') + b'\0')


def marker_payload(slots):
    data = text(b'EDID', 'TrueCoopPlayerMarkers')
    data += text(b'FULL', 'Co-op partners')
    # No stages, fragments, scripts, inventory, factions or AI packages.
    # Not start-game-enabled: the native client starts it after joining a party.
    data += sub(b'DNAM', struct.pack('<HBBfI', 0, 0, 0xFF, 0.0, 6))
    data += sub(b'NEXT', b'')
    for slot in range(slots):
        data += sub(b'QOBJ', struct.pack('<H', slot))
        data += sub(b'FNAM', struct.pack('<I', 0))
        data += text(b'NNAM', 'Co-op partner')
        data += sub(b'QSTA', struct.pack('<II', slot, 0))
    data += sub(b'ANAM', struct.pack('<I', slots))
    for slot in range(slots):
        data += sub(b'ALST', struct.pack('<I', slot))
        data += text(b'ALID', f'Partner{slot}')
        # Optional + allow reuse/dead/disabled/reserved. Never quest-object,
        # essential, protected, reserving, store-name or AI-affecting flags.
        data += sub(b'FNAM', struct.pack('<I', 0x29A))
        data += sub(b'ALFR', struct.pack('<I', 0))
        data += sub(b'VTCK', struct.pack('<I', 0))
        data += sub(b'ALED', b'')
    return data


def entries(blob, start=0, end=None):
    end = len(blob) if end is None else end
    offset = start
    while offset < end:
        if offset + 24 > end:
            raise ValueError('Truncated record header')
        tag, size = struct.unpack_from('<4sI', blob, offset)
        finish = offset + (size if tag == b'GRUP' else 24 + size)
        if finish > end or finish <= offset + 23:
            raise ValueError('Invalid record bounds')
        yield offset, finish, tag
        offset = finish
    if offset != end:
        raise ValueError('Invalid group boundary')


def add_quest(blob, local_id, slots):
    top = list(entries(blob))
    if not top or top[0][2] != b'TES4':
        raise ValueError('Missing TES4 header')
    master_count = 0
    header_end = top[0][1]
    header = bytearray(blob[:header_end])
    offset = 24
    hedr = None
    while offset < header_end:
        tag, size = struct.unpack_from('<4sH', header, offset)
        if offset + 6 + size > header_end:
            raise ValueError('Invalid TES4 subrecord')
        if tag == b'MAST':
            master_count += 1
        if tag == b'HEDR':
            if size != 12:
                raise ValueError('Unsupported HEDR')
            hedr = offset + 6
        offset += 6 + size
    if hedr is None or master_count != 2:
        raise ValueError('Unexpected bundled plugin header/masters')
    record_id = (master_count << 24) | local_id
    payload = marker_payload(slots)
    anchor_id = record_id + 1
    # No model, collision, script or inventory. This local position anchor must
    # never load a mesh/Havok body merely to show a distant player's marker.
    anchor = text(b'EDID', 'TrueCoopPlayerMarkerAnchor') + sub(b'OBND', bytes(12))
    anchor_record = struct.pack('<4sIIIIHH', b'STAT', len(anchor), 0, anchor_id, 0, 44, 0) + anchor
    anchor_found = False
    for start, end, tag in top:
        if tag == b'GRUP' and blob[start + 8:start + 12] == b'STAT':
            for rec, finish, kind in entries(blob, start + 24, end):
                if struct.unpack_from('<I', blob, rec + 12)[0] == anchor_id:
                    if blob[rec:finish] != anchor_record:
                        raise ValueError('Reserved anchor form ID contains different data')
                    anchor_found = True
    group_index = None
    for index, (start, end, tag) in enumerate(top):
        if tag != b'GRUP':
            continue
        if blob[start + 8:start + 12] != b'QUST':
            continue
        if group_index is not None:
            raise ValueError('Multiple top-level QUST groups')
        group_index = index
        for rec, finish, kind in entries(blob, start + 24, end):
            if kind != b'QUST':
                raise ValueError('Unexpected child in QUST group')
            flags, form_id = struct.unpack_from('<II', blob, rec + 8)
            data = blob[rec + 24:finish]
            if flags & 0x40000:
                data = zlib.decompress(data[4:])
            if form_id == record_id:
                if data != payload or not anchor_found:
                    raise ValueError('Reserved marker form ID contains different data')
                return blob
    if group_index is None:
        raise ValueError('No QUST group in bundled plugin')
    count, next_id = struct.unpack_from('<II', header, hedr + 4)
    if next_id != local_id:
        raise ValueError('Reserved marker ID is not the expected next object ID')
    if anchor_found:
        raise ValueError('Partial marker/anchor asset')
    struct.pack_into('<II', header, hedr + 4, count + 2, local_id + 2)
    start, end, _ = top[group_index]
    record = struct.pack('<4sIIIIHH', b'QUST', len(payload), 0, record_id, 0, 44, 0) + payload
    group = bytearray(blob[start:end])
    struct.pack_into('<I', group, 4, len(group) + len(record))
    group += record
    # The original bundled plugin has no STAT group. Refuse an unexpected new
    # upstream layout rather than create two groups with the same label.
    if any(tag == b'GRUP' and blob[a + 8:a + 12] == b'STAT' for a, _, tag in top):
        raise ValueError('Unexpected existing STAT group')
    anchor_group = struct.pack('<4sI4sIII', b'GRUP', 24 + len(anchor_record), b'STAT', 0, 0, 0) + anchor_record
    output = bytes(header) + blob[header_end:start] + anchor_group + bytes(group) + blob[end:]
    # Check preservation of all original quest records, including compressed ones.
    original_records = [blob[a:b] for a, b, _ in entries(blob, start + 24, end)]
    updated_start = start + len(anchor_group)
    updated_records = [output[a:b] for a, b, _ in entries(output, updated_start + 24, end + len(anchor_group) + len(record))]
    if original_records != updated_records[:-1] or add_quest(output, local_id, slots) != output:
        raise ValueError('Plugin preservation/idempotence verification failed')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    ids = (ROOT / 'Code/common/PlayerMarkerIds.h').read_text(encoding='utf-8')
    local_id = int(re.search(r'kQuestLocalId = (0x[0-9A-F]+)', ids)[1], 16)
    slots = int(re.search(r'kSlotCount = ([0-9]+)', ids)[1])
    if int(re.search(r'kAnchorLocalId = (0x[0-9A-F]+)', ids)[1], 16) != local_id + 1:
        raise ValueError('Inconsistent anchor form ID')
    path = ROOT / 'GameFiles/Skyrim/SkyrimTogether.esp'
    before = path.read_bytes()
    after = add_quest(before, local_id, slots)
    from build_character_identity import add_identity
    after = add_identity(after)
    from build_checkpoint_globals import add_checkpoint
    after = add_checkpoint(after)
    if args.check:
        if before != after:
            raise SystemExit('Bundled marker quest needs generating')
    elif before != after:
        path.write_bytes(after)
    print(f'Marker quest verified: {local_id:#x}, {slots} optional aliases/objectives, {len(after)} bytes')


if __name__ == '__main__':
    main()
