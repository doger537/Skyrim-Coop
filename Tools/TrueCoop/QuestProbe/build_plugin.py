"""Build an isolated, new-record-only Papyrus diagnostic quest. No game files are read.

VMAD/QUST fields checked against TES5Edit/Core/wbDefinitionsTES5.pas, dev-4.1.6:
https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.6/Core/wbDefinitionsTES5.pas
No fragments or aliases: the optional VMAD fragment/alias suffix is omitted.
"""
import argparse
import struct
from pathlib import Path


def sub(tag, data):
    return tag.encode("ascii") + struct.pack("<H", len(data)) + data


def text(tag, value):
    return sub(tag, value.encode("ascii") + b"\0")


def record(tag, data, form_id=0):
    return struct.pack("<4sIIIIHH", tag.encode("ascii"), len(data), 0, form_id, 0, 44, 0) + data


def build():
    header = sub("HEDR", struct.pack("<fII", 1.7, 1, 0x801))
    header += text("CNAM", "TrueCoop local diagnostic")
    header += text("MAST", "Skyrim.esm") + sub("DATA", bytes(8))
    script = b"TrueCoopQuestGrantProbe"
    vmad = struct.pack("<HHHH", 5, 2, 1, len(script)) + script + struct.pack("<BH", 0, 0)
    quest = text("EDID", "TrueCoopRewardProbe") + sub("VMAD", vmad)
    quest += text("FULL", "TrueCoop local reward diagnostic")
    # No start-game flag, quest stages, aliases, conditions or vanilla overrides.
    # Start explicitly with startquest; invoke with cqf, then discard the character.
    quest += sub("DNAM", struct.pack("<HBBfI", 0, 0, 0xFF, 0.0, 0))
    quest += sub("NEXT", b"") + sub("ANAM", struct.pack("<I", 0))
    quest = record("QUST", quest, 0x01000800)
    group = struct.pack("<4sI4sIII", b"GRUP", 24 + len(quest), b"QUST", 0, 0, 0) + quest
    return record("TES4", header) + group


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as stream:
        stream.write(build())
    print(f"Diagnostic-only plugin: {args.output}; 1 new QUST, 0 overrides")
