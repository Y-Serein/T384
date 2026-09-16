#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def write_capture(root: Path, name: str, setpoint: float, mean: float) -> Path:
    directory = root / name
    directory.mkdir()
    manifest = {
        "format": "t384-radiometry-capture-v1",
        "setpoint_c": setpoint,
        "gain": "high",
        "distance_m": 0.01,
        "emissivity": 0.98,
        "frames": [
            {"path": "frame-1.y16be", "roi": {"mean_y16": mean}},
            {"path": "frame-2.y16be", "roi": {"mean_y16": mean + 2}},
        ],
    }
    (directory / "manifest.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )
    return directory


def main() -> int:
    tool = Path(__file__).resolve().parents[1] / "tools/analyze_blackbody_pair.py"
    with tempfile.TemporaryDirectory(prefix="t384-blackbody-pair-") as temp:
        root = Path(temp)
        low = write_capture(root, "0C-high", 0.0, 40000.0)
        high = write_capture(root, "50C-high", 50.0, 42000.0)
        output = root / "pair.json"
        result = subprocess.run(
            [sys.executable, str(tool), str(low), str(high), "--output", str(output)],
            check=True,
            capture_output=True,
            text=True,
        )
        report = json.loads(output.read_text(encoding="utf-8"))
        assert report["status"] == "engineering-only-not-oem-radiometric"
        assert report["low"]["frame_count"] == 2
        assert report["high"]["frame_count"] == 2
        assert report["linear_model"]["temperature_c = slope * y16 + intercept"]["slope"] > 0
        assert "engineering-only" in result.stdout
    print("T384 blackbody pair smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
