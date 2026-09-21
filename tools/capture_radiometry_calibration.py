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
import math
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
from calibration_capture_support import (
    TABLE_EVIDENCE, PARAMETER_EVIDENCE, load_evidence, query_snapshot,
    validate_boundary_states, roi_stats, read_capture,
)
from read_mini2_module_files import Device, NoRedirect


def fetch_diag(url: str) -> dict[str, str]:
    request = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
    with opener.open(request, timeout=5) as response:
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
    with path.open("xb") as output: output.write(frame)
    return {
        "sequence": sequence,
        "path": path.name,
        "bytes": len(frame),
        "sha256": hashlib.sha256(frame).hexdigest(),
        "host_completed_unix": time.time(),
    }


def frame_roi_stats(frame: bytearray, width: int, height: int) -> dict[str, float | int]:
    return roi_stats(frame,width,height,width//2-8,height//2-8,16)


def capture(args: argparse.Namespace) -> int:
    output = args.output
    # Never mix new frames into an earlier point or overwrite its evidence.
    output.mkdir(parents=True, exist_ok=False)
    diag_url = args.diag_url or args.url.rsplit("/", 1)[0] + "/diag"
    evidence=load_evidence(args.table_evidence,args.parameter_evidence)
    before = fetch_diag(diag_url)
    if before.get("source.pixel_format") != "2":
        raise BenchError("source is not Y16/TPD; calibration capture requires pixel_format=2")
    if before.get("stream.active")!="0":
        raise BenchError("active stream must close before calibration state query")
    device=Device(args.url.rsplit("/",1)[0])
    print("读取采集前机芯状态（尚未收帧）…",flush=True)
    module_before=query_snapshot(device,output/"module-before")
    requested_gain=None if args.gain=="current" else args.gain
    actual_gain, gain_verified_before = validate_boundary_states(module_before,module_before,evidence,requested_gain)

    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
    request = urllib.request.Request(args.url, headers={"User-Agent": "t384-radiometry-capture/1"})
    header = bytearray(WIRE_HEADER_BYTES)
    payload = bytearray(CHUNK_PAYLOAD_MAX)
    frames: list[dict[str, object]] = []
    frame = bytearray()
    sequence: int | None = None
    expected_offset = 0
    started = time.time()
    previous_sequence=None
    sequence_gaps=0
    print(f"开始收取 {args.frames} 完整黑体帧…",flush=True)

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
        roi_size=args.roi_size
        roi_x=args.roi_x if args.roi_x is not None else width//2-roi_size//2
        roi_y=args.roi_y if args.roi_y is not None else height//2-roi_size//2
        if roi_size<=0 or roi_x<0 or roi_y<0 or roi_x+roi_size>width or roi_y+roi_size>height:
            raise BenchError("ROI outside source image")
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
                    if previous_sequence is not None:
                        delta=(seq-previous_sequence)&0xFFFFFFFF
                        if not 0<delta<0x80000000: raise BenchError("duplicate/backward complete frame sequence")
                        sequence_gaps+=delta-1
                    frames.append(write_frame(output, seq, frame))
                    previous_sequence=seq
                sequence = None
                expected_offset = 0

    # Wait for the raw TCP close before starting a paused UART transaction.
    deadline=time.monotonic()+5
    while True:
        after=fetch_diag(diag_url)
        if after.get("stream.active")=="0": break
        if time.monotonic()>=deadline: raise BenchError("RAW16 stream did not close; state query not started")
        time.sleep(0.1)
    if len(frames) != args.frames:
        raise BenchError(
            f"capture ended with {len(frames)} complete frames, expected {args.frames}"
        )
    print("完整帧已收齐，读取采集后机芯状态…",flush=True)
    module_after=query_snapshot(device,output/"module-after")
    _, gain_verified_after = validate_boundary_states(module_before,module_after,evidence,actual_gain)
    for item in frames:
        frame_path = output / str(item["path"])
        item["roi"] = roi_stats(frame_path.read_bytes(),width,height,roi_x,roi_y,roi_size)
    manifest = {
        "format": "t384-radiometry-capture-v1",
        "captured_unix": started,
        "setpoint_c": args.setpoint_c,
        "gain": actual_gain,
        "gain_requested":args.gain,
        "gain_verified_at_boundaries": bool(gain_verified_before and gain_verified_after),
        "gain_verified_per_frame":False,
        "geometry":dict(width=width,height=height,frame_bytes=frame_size,pixel_format="Y16BE"),
        "roi":dict(x=roi_x,y=roi_y,size=roi_size),
        "complete_frames":len(frames),
        "sequence_gaps":sequence_gaps,
        "module_states":dict(before=module_before,after=module_after),
        "original_evidence":evidence,
        "oem_radiometry_ready":False,
        "status":"raw-blackbody-acquisition-not-oem-calibrated",
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
    read_capture(output,manifest_data=manifest)
    manifest_path=output/"manifest.json"
    temporary_path=output/"manifest.tmp"
    temporary_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    temporary_path.rename(manifest_path)
    print(f"captured {len(frames)} complete Y16BE frames in {output}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://192.168.17.1/raw16.stream")
    parser.add_argument("--diag-url")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--setpoint-c", type=float, required=True)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--gain", choices=("high", "low", "current"), default="current")
    parser.add_argument("--distance-m", type=float, required=True)
    parser.add_argument("--emissivity", type=float, required=True)
    parser.add_argument("--humidity", type=float)
    parser.add_argument("--ta-c", type=float)
    parser.add_argument("--tu-c", type=float)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--table-evidence",type=Path,default=TABLE_EVIDENCE)
    parser.add_argument("--parameter-evidence",type=Path,default=PARAMETER_EVIDENCE)
    parser.add_argument("--roi-x",type=int)
    parser.add_argument("--roi-y",type=int)
    parser.add_argument("--roi-size",type=int,default=16)
    args = parser.parse_args()
    if args.frames <= 0 or args.frames > 120:
        parser.error("--frames must be between 1 and 120")
    if not all(math.isfinite(v) for v in (args.setpoint_c,args.distance_m,args.emissivity,args.timeout)):
        parser.error("capture parameters must be finite")
    if args.distance_m<=0 or not 0<args.emissivity<=1 or args.timeout<=0:
        parser.error("distance/timeout must be positive and emissivity must be in (0,1]")
    if any(v is not None and not math.isfinite(v) for v in (args.ta_c,args.tu_c,args.humidity)):
        parser.error("optional environmental values must be finite")
    return capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
