"""Append four exact integer-float words for the paired-save checkpoint token."""
import struct
from build_player_markers import entries, sub, text


def checkpoint_records():
    result = []
    for part in range(4):
        data = text(b'EDID', f'TrueCoopCheckpointPart{part}') + sub(b'FNAM', b'f') + sub(b'FLTV', bytes(4))
        result.append(struct.pack('<4sIIIIHH', b'GLOB', len(data), 0, 0x02004DFF + part, 0, 44, 0) + data)
    return result


def add_checkpoint(blob):
    top = list(entries(blob))
    groups = [(a, b) for a, b, tag in top if tag == b'GRUP' and blob[a + 8:a + 12] == b'GLOB']
    if len(groups) != 1:
        raise ValueError('Expected the character identity GLOB group first')
    start, end = groups[0]
    records = [blob[a:b] for a, b, _ in entries(blob, start + 24, end)]
    added = checkpoint_records()
    if len(records) == 13:
        if records[-4:] != added:
            raise ValueError('Conflicting checkpoint globals')
        return blob
    if len(records) != 9:
        raise ValueError('Unexpected identity global count')
    header = bytearray(blob[:top[0][1]])
    offset, hedr = 24, None
    while offset < len(header):
        tag, size = struct.unpack_from('<4sH', header, offset)
        if tag == b'HEDR' and size == 12:
            hedr = offset + 6
        offset += size + 6
    if hedr is None:
        raise ValueError('Missing HEDR')
    count, next_id = struct.unpack_from('<II', header, hedr + 4)
    if next_id != 0x4DFF:
        raise ValueError('Checkpoint IDs are already occupied')
    struct.pack_into('<II', header, hedr + 4, count + 4, 0x4E03)
    group = bytearray(blob[start:end])
    group += b''.join(added)
    struct.pack_into('<I', group, 4, len(group))
    result = bytes(header) + blob[len(header):start] + bytes(group) + blob[end:]
    if add_checkpoint(result) != result:
        raise ValueError('Non-idempotent checkpoint generation')
    return result
