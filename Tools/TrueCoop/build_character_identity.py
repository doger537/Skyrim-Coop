"""Add nine script-free, nonconstant character identity globals to our ESP."""
import struct
from build_player_markers import entries, sub, text


def add_identity(blob):
    first, count = 0x4DF6, 9
    wanted = []
    for part in range(count):
        name = f'TrueCoopCharacterPart{part}' if part < 8 else 'TrueCoopCharacterVersion'
        data = text(b'EDID', name) + sub(b'FNAM', b'f') + sub(b'FLTV', struct.pack('<f', 0))
        wanted.append(struct.pack('<4sIIIIHH', b'GLOB', len(data), 0, 0x02000000 | first + part, 0, 44, 0) + data)
    top = list(entries(blob))
    if not top or top[0][2] != b'TES4':
        raise ValueError('Missing TES4 header')
    existing = [blob[a:b] for a, b, tag in top if tag == b'GRUP' and blob[a + 8:a + 12] == b'GLOB']
    group = struct.pack('<4sI4sIII', b'GRUP', 24 + sum(map(len, wanted)), b'GLOB', 0, 0, 0) + b''.join(wanted)
    if existing:
        from build_checkpoint_globals import checkpoint_records
        combined = wanted + checkpoint_records()
        expanded = struct.pack('<4sI4sIII', b'GRUP', 24 + sum(map(len, combined)), b'GLOB', 0, 0, 0) + b''.join(combined)
        if existing not in ([group], [expanded]):
            raise ValueError('Conflicting character identity globals')
        return blob
    # Refuse an unrelated use of any reserved local ID, including other groups.
    def check(start, end):
        for a, b, tag in entries(blob, start, end):
            if tag == b'GRUP':
                check(a + 24, b)
            elif (struct.unpack_from('<I', blob, a + 12)[0] & 0xFFFFFF) in range(first, first + count):
                raise ValueError('Reserved character identity ID is occupied')
    check(0, len(blob))
    end = top[0][1]
    header = bytearray(blob[:end])
    offset, hedr, masters = 24, None, []
    while offset < end:
        tag, size = struct.unpack_from('<4sH', header, offset)
        if offset + 6 + size > end:
            raise ValueError('Truncated TES4 subrecord')
        if tag == b'HEDR' and size == 12:
            hedr = offset + 6
        if tag == b'MAST':
            masters.append(bytes(header[offset + 6:offset + 6 + size]))
        offset += 6 + size
    if hedr is None or masters != [b'Skyrim.esm\0', b'Update.esm\0']:
        raise ValueError('Unexpected core plugin header')
    records, next_id = struct.unpack_from('<II', header, hedr + 4)
    if next_id != first:
        raise ValueError('Unexpected next object ID')
    struct.pack_into('<II', header, hedr + 4, records + count, first + count)
    result = bytes(header) + group + blob[end:]
    if add_identity(result) != result:
        raise ValueError('Identity generation is not idempotent')
    return result
