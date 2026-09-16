#!/usr/bin/env python3
"""Analyze two Windows RAW16 blackbody captures.

This produces an engineering two-point response report only.  It never labels
the result as OEM radiometric calibration and never writes firmware or tables.
"""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path


def load_capture(directory: Path, width: int, height: int, x: int, y: int, size: int) -> dict:
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    means = []
    for item in manifest.get("frames", []):
        roi = item.get("roi")
        if not roi or "mean_y16" not in roi:
            raise ValueError(f"{directory}: frame is missing host ROI statistics")
        means.append(float(roi["mean_y16"]))
    if not means:
        raise ValueError(f"{directory}: no complete frames")
    return {
        "directory": str(directory),
        "setpoint_c": manifest["setpoint_c"],
        "gain": manifest["gain"],
        "distance_m": manifest["distance_m"],
        "emissivity": manifest["emissivity"],
        "frame_count": len(means),
        "roi_mean_y16": statistics.fmean(means),
        "roi_stddev_between_frames": statistics.pstdev(means),
        "roi_min_y16": min(means),
        "roi_max_y16": max(means),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("low_capture", type=Path)
    parser.add_argument("high_capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--width", type=int, default=256)
    parser.add_argument("--height", type=int, default=192)
    parser.add_argument("--roi-x", type=int, default=120)
    parser.add_argument("--roi-y", type=int, default=88)
    parser.add_argument("--roi-size", type=int, default=16)
    args = parser.parse_args()
    low = load_capture(args.low_capture, args.width, args.height, args.roi_x, args.roi_y, args.roi_size)
    high = load_capture(args.high_capture, args.width, args.height, args.roi_x, args.roi_y, args.roi_size)
    if low["gain"] != high["gain"]:
        raise SystemExit("captures must use the same gain")
    if low["distance_m"] != high["distance_m"] or low["emissivity"] != high["emissivity"]:
        raise SystemExit("captures must use identical distance and emissivity")
    if low["setpoint_c"] == high["setpoint_c"]:
        raise SystemExit("setpoints must differ")
    dx = high["roi_mean_y16"] - low["roi_mean_y16"]
    if dx == 0:
        raise SystemExit("Y16 response did not change between setpoints")
    slope = (high["setpoint_c"] - low["setpoint_c"]) / dx
    intercept = low["setpoint_c"] - slope * low["roi_mean_y16"]
    report = {
        "format": "t384-engineering-blackbody-pair-v1",
        "status": "engineering-only-not-oem-radiometric",
        "roi": {"x": args.roi_x, "y": args.roi_y, "size": args.roi_size},
        "low": low,
        "high": high,
        "linear_model": {"temperature_c = slope * y16 + intercept": {"slope": slope, "intercept": intercept}},
        "notes": [
            "This two-point fit is not a substitute for KT/BT/NUC-T/SNR calibration.",
            "Do not publish as absolute radiometric temperature without vendor data-domain confirmation.",
        ],
    }
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
