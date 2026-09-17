#!/usr/bin/env python3
"""Capture bounded blackbody samples from the existing RAW16 stream.

This tool records raw complete frames and a diagnostic snapshot before/after a
single blackbody point.  It never writes calibration data to the module and
does not calculate or invent KT/BT/NUC-T values.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import time
import urllib.request
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from t384_raw16_bench import (
    BenchError,
    CHUNK_PAYLOAD_MAX,
    PIXEL_FORMAT_Y16_BE,
    SourceContract,
    WIRE_HEADER_BYTES,
    read_chunk,
    verify_headers,
)


def fetch_diag(url: str) -> dict[str, str]:
    request = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(request, timeout=5) as response:
        if response.status != 200:
            raise BenchError(f"diagnostic HTTP status is {response.status}")
        values: dict[str, str] = {}
        for line in response.read().decode("utf-8", "replace").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
        return values


def write_frame(directory: Path, sequence: int, frame: bytearray) -> dict[str, object]:
    path = directory / f"frame-{sequence:010d}.y16be"
    path.write_bytes(frame)
    return {
        "sequence": sequence,
        "path": path.name,
        "bytes": len(frame),
        "sha256": hashlib.sha256(frame).hexdigest(),
    }


def frame_roi_stats(frame: bytearray, width: int, height: int) -> dict[str, float | int]:
    x0 = max(0, width // 2 - 8)
    y0 = max(0, height // 2 - 8)
    values = [
        int.from_bytes(frame[(row * width + col) * 2:(row * width + col) * 2 + 2], "big")
        for row in range(y0, min(height, y0 + 16))
        for col in range(x0, min(width, x0 + 16))
    ]
    if not values:
        raise BenchError("empty ROI")
    mean = sum(values) / len(values)
    variance = sum((value - mean) ** 2 for value in values) / len(values)
    return {
        "x": x0,
        "y": y0,
        "width": min(16, width - x0),
        "height": min(16, height - y0),
        "sample_count": len(values),
        "mean_y16": mean,
        "stddev_y16": variance ** 0.5,
        "minimum_y16": min(values),
        "maximum_y16": max(values),
    }


def capture(args: argparse.Namespace) -> int:
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    diag_url = args.diag_url or args.url.rsplit("/", 1)[0] + "/diag"
    before = fetch_diag(diag_url)
    if before.get("source.pixel_format") not in (None, "2"):
        raise BenchError("source is not Y16/TPD; calibration capture requires pixel_format=2")

    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    request = urllib.request.Request(args.url, headers={"User-Agent": "t384-radiometry-capture/1"})
    header = bytearray(WIRE_HEADER_BYTES)
    payload = bytearray(CHUNK_PAYLOAD_MAX)
    frames: list[dict[str, object]] = []
    frame = bytearray()
    sequence: int | None = None
    expected_offset = 0
    started = time.time()

    with opener.open(request, timeout=args.timeout) as response:
        pixel_format, mode_flag, frame_mode = verify_headers(response, allow_geometry=True)
        if pixel_format != PIXEL_FORMAT_Y16_BE or frame_mode != "tpd":
            raise BenchError("stream is not TPD/Y16BE; refusing calibration capture")
        frame_size = int(response.headers["X-T384-Frame-Bytes"])
        chunk_payload_max = int(response.headers["X-T384-Chunk-Payload-Max"])
        if chunk_payload_max > len(payload):
            payload = bytearray(chunk_payload_max)
        width = int(response.headers["X-T384-Frame-Width"])
        height = int(response.headers["X-T384-Frame-Height"])
        source = SourceContract("real", mode_flag)
        while len(frames) < args.frames and time.time() - started < args.timeout * 4:
            seq, offset, _, chunk, flags = read_chunk(
                response, header, payload, pixel_format, mode_flag,
                frame_size, width, height, chunk_payload_max
            )
            source.validate(flags)
            if flags & 0x0001:
                sequence, expected_offset = seq, 0
                frame = bytearray(frame_size)
            if sequence != seq or offset != expected_offset:
                sequence = None
                expected_offset = 0
                continue
            frame[offset : offset + len(chunk)] = chunk
            expected_offset += len(chunk)
            if flags & 0x0002:
                if sequence == seq and expected_offset == frame_size:
                    frames.append(write_frame(output, seq, frame))
                sequence = None
                expected_offset = 0

    after = fetch_diag(diag_url)
    if len(frames) != args.frames:
        raise BenchError(
            f"capture ended with {len(frames)} complete frames, expected {args.frames}"
        )
    for item in frames:
        frame_path = output / str(item["path"])
        item["roi"] = frame_roi_stats(frame_path.read_bytes(), width, height)
    manifest = {
        "format": "t384-radiometry-capture-v1",
        "captured_unix": started,
        "setpoint_c": args.setpoint_c,
        "gain": args.gain,
        "distance_m": args.distance_m,
        "emissivity": args.emissivity,
        "humidity": args.humidity,
        "ta_c": args.ta_c,
        "tu_c": args.tu_c,
        "source_url": args.url,
        "diag_url": diag_url,
        "diag_before": before,
        "diag_after": after,
        "frames": frames,
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"captured {len(frames)} complete Y16BE frames in {output}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://192.168.17.1/raw16.stream")
    parser.add_argument("--diag-url")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--setpoint-c", type=float, required=True)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--gain", choices=("high", "low"), required=True)
    parser.add_argument("--distance-m", type=float, required=True)
    parser.add_argument("--emissivity", type=float, required=True)
    parser.add_argument("--humidity", type=float)
    parser.add_argument("--ta-c", type=float)
    parser.add_argument("--tu-c", type=float)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()
    if args.frames <= 0 or args.frames > 120:
        parser.error("--frames must be between 1 and 120")
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
