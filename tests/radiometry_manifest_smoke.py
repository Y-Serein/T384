#!/usr/bin/env python3
from __future__ import annotations

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def run_tool(tool: Path, directory: Path, complete: bool) -> dict:
    kt = directory / "kt.bin"
    bt = directory / "bt.bin"
    nuc = directory / "nuc.bin"
    distance = directory / "distance.bin"
    kt.write_bytes(struct.pack("<4H", 16384, 16384, 16384, 16384))
    bt.write_bytes(struct.pack("<4h", 0, -1, 2, 3))
    nuc.write_bytes(struct.pack("<4H", 4800, 4790, 4780, 4770))
    distance.write_bytes(b"distance-table")
    command = [
        sys.executable,
        str(tool),
        "--kt",
        str(kt),
        "--bt",
        str(bt),
        "--nuc-t",
        str(nuc),
        "--version",
        "2",
        "--kt-items",
        "4",
        "--nuc-items",
        "4",
        "--output",
        str(directory / "manifest.json"),
    ]
    if complete:
        command.extend(
            [
                "--distance",
                str(distance),
                "--product",
                "WN2384T",
                "--pn",
                "WN2384T",
                "--sn",
                "SN001",
                "--module-firmware",
                "00.01.00.00",
                "--gain",
                "high",
                "--ktemp",
                "16585",
                "--btemp",
                "-3130",
                "--address-ca",
                "0x91A",
                "--source",
                "vendor-default-data",
                "--calibration-epoch",
                "3",
            ]
        )
    subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads((directory / "manifest.json").read_text(encoding="utf-8"))


def main() -> int:
    tool = Path(__file__).resolve().parents[1] / "tools/validate_radiometry_tables.py"
    with tempfile.TemporaryDirectory(prefix="t384-radiometry-manifest-") as temp:
        directory = Path(temp)
        incomplete = run_tool(tool, directory, complete=False)
        assert incomplete["status"] == "incomplete"
        assert "pn" in incomplete["identity"]["missing_metadata"]
        complete = run_tool(tool, directory, complete=True)
        assert complete["status"] == "ready-for-provisioning"
        assert complete["identity"]["address_ca"] == 0x91A
        assert complete["kt"]["items"] == 4
        assert complete["bt"]["min"] == -1
        assert complete["distance"]["bytes"] == len(b"distance-table")
    print("T384 radiometry manifest smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
