#!/usr/bin/env python3
"""Check the reference catalogue against the runtime archive requirements, without game files."""
import json
from pathlib import Path
import re


def main():
    root = Path(__file__).resolve().parents[2]
    catalog = json.loads((root / "docs/truecoop/ae/catalog.json").read_text(encoding="utf-8"))
    entries = catalog["creations"]
    assert len(entries) == 74, "The 2021 Anniversary Upgrade has 74 Creations"
    plugins = [entry["plugin"] for entry in entries]
    assert len(set(plugins)) == 74
    header = (root / "Code/encoding/Structs/AnniversaryContent.h").read_text(encoding="utf-8")
    assert re.findall(r'"(cc[^"\n]+\.(?:esl|esm))"', header) == plugins
    free = {entry["plugin"] for entry in entries if entry["distribution"] == "free-SE-update"}
    assert free == {
        "ccbgssse001-fish.esm", "ccbgssse025-advdsgs.esm",
        "ccbgssse037-curios.esl", "ccqdrsse001-survivalmode.esl",
    }
    plan = (root / "TEST_PLAN.md").read_text(encoding="utf-8")
    for entry in entries:
        assert entry["archives"] == [entry["plugin"].rsplit(".", 1)[0] + ".bsa"]
        assert all(test in plan for test in entry["tests"]), entry["plugin"]
        if entry["network_status"] == "game-tested":
            assert entry["evidence"], "Game-tested status requires recorded evidence"
    print("PASS: 74 unique Creations, four free entries, runtime catalogue and test references agree")


if __name__ == "__main__":
    main()
