#!/usr/bin/env python3
"""Read-only full-frame stability and reconnect checks for a single RAW16 stream.

Whole input frames skipped by the dual-core single-frame buffer are reported,
not treated as broken frames. This does not certify zero-drop/full-rate output.
"""
import argparse
import json
import pathlib
import threading
import time
import urllib.request

from t384_raw16_bench import (BenchError, FLAG_FRAME_END, FLAG_FRAME_START,
                             FrameAssembler, SourceContract, read_chunk,
                             verify_headers)


def opener():
    return urllib.request.build_opener(urllib.request.ProxyHandler({}))


def diag(base, timeout):
    with opener().open(base + "/diag", timeout=timeout) as response:
        text = response.read().decode("utf-8")
    prefixes = ("source.", "dvp.", "pipeline.", "ncm.", "http.", "stream.", "dualcore.")
    return {key: value for line in text.splitlines() if "=" in line
            for key, value in [line.split("=", 1)] if key.startswith(prefixes)}


def window(base, duration, timeout, max_gap, observe):
    began = time.monotonic()
    first_latency = None
    last_frame = began
    longest = 0.0
    with opener().open(base + "/raw16.stream", timeout=timeout) as response:
        pixel_format, mode, _ = verify_headers(response, allow_geometry=True)
        width = int(response.headers["X-T384-Frame-Width"])
        height = int(response.headers["X-T384-Frame-Height"])
        frame_bytes = int(response.headers["X-T384-Frame-Bytes"])
        chunk_max = int(response.headers["X-T384-Chunk-Payload-Max"])
        assembler = FrameAssembler(None, SourceContract("real", mode), frame_bytes)
        header, payload = bytearray(36), bytearray(chunk_max)
        deadline = began + duration
        synced = False
        # Finish the frame crossing the deadline; never hide a partial frame.
        while True:
            sequence, offset, _, chunk, flags = read_chunk(
                response, header, payload, pixel_format, mode,
                frame_bytes, width, height, chunk_max)
            now = time.monotonic()
            if now - last_frame > max_gap:
                raise BenchError(f"no complete frame for {now - last_frame:.3f}s")
            if not synced:
                if not flags & FLAG_FRAME_START:
                    raise BenchError("stream begins without FRAME_START")
                synced = True
            before = assembler.complete_frames
            assembler.consume(sequence, offset, chunk, flags)
            if assembler.discarded_frames or assembler.sequence_errors:
                raise BenchError("partial frame or duplicate/reversed sequence")
            if assembler.complete_frames != before:
                longest = max(longest, now - last_frame)
                if first_latency is None:
                    first_latency = now - began
                last_frame = now
                if now >= deadline and flags & FLAG_FRAME_END:
                    break
            observe(assembler, now - began)
    elapsed = time.monotonic() - began
    return {"elapsed_s": elapsed, "complete_frames": assembler.complete_frames,
            "fps": assembler.complete_frames / elapsed,
            "payload_bps": assembler.complete_frames * frame_bytes / elapsed,
            "sequence_gaps": assembler.sequence_gaps,
            "partial_frames": assembler.discarded_frames,
            "sequence_errors": assembler.sequence_errors,
            "first_frame_s": first_latency, "max_frame_gap_s": longest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://192.168.17.1")
    parser.add_argument("--duration", type=float, default=600)
    parser.add_argument("--reconnects", type=int, default=10)
    parser.add_argument("--max-gap", type=float, default=2)
    parser.add_argument("--min-fps", type=float, default=25,
                        help="minimum complete-frame FPS in the continuous window")
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / "out/stability/latest.json")
    args = parser.parse_args()
    if args.duration <= 0 or args.reconnects < 0 or args.max_gap <= 0 or args.min_fps <= 0:
        parser.error("duration/max-gap must be positive; reconnects nonnegative")
    base = args.base.rstrip("/")
    stop = threading.Event()
    result = {"base": base, "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "minimum_complete_fps": args.min_fps,
              "windows": [], "diagnostics": [], "errors": [],
              "policy": "whole-frame sequence gaps reported; partial/CRC/order errors fail"}

    def collect():
        while not stop.is_set():
            try:
                sample = {"time": time.time(), "values": diag(base, args.max_gap)}
            except Exception as error:
                sample = {"time": time.time(), "error": str(error)}
                result["errors"].append("diagnostic: " + str(error))
            result["diagnostics"].append(sample)
            stop.wait(2)

    worker = threading.Thread(target=collect, daemon=True)
    worker.start()
    last_report = 0

    def observe(assembler, elapsed):
        nonlocal last_report
        if elapsed >= last_report + 10:
            last_report = elapsed
            print(f"{elapsed:.0f}s: complete={assembler.complete_frames}, "
                  f"fps={assembler.complete_frames / elapsed:.2f}, "
                  f"skipped={assembler.sequence_gaps}", flush=True)

    try:
        result["windows"].append(window(base, args.duration, args.max_gap,
                                          args.max_gap, observe))
        print("Continuous window:", json.dumps(result["windows"][-1]), flush=True)
        if result["windows"][-1]["fps"] < args.min_fps:
            raise BenchError(f"complete-frame FPS {result['windows'][-1]['fps']:.3f} "
                             f"< {args.min_fps:.3f}")
        for index in range(args.reconnects):
            # Real HTTP recovery check after closing the preceding TCP stream.
            started = time.monotonic()
            with opener().open(base + "/", timeout=args.max_gap) as response:
                response.read()
            diag(base, args.max_gap)
            recovery = time.monotonic() - started
            if recovery > args.max_gap:
                raise BenchError(f"HTTP recovery {recovery:.3f}s exceeds limit")
            measured = window(base, 3, args.max_gap, args.max_gap, lambda *_: None)
            measured.update(reconnect=index + 1, http_recovery_s=recovery)
            result["windows"].append(measured)
            print("Reconnect:", json.dumps(measured), flush=True)
    except Exception as error:
        result["errors"].append(str(error))
        print("FAIL:", str(error), flush=True)
    finally:
        stop.set()
        worker.join(args.max_gap + 1)
        result["stable"] = not result["errors"] and len(result["windows"]) == args.reconnects + 1
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("Results:", args.output, flush=True)
    return 0 if result["stable"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
