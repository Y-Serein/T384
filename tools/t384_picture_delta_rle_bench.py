#!/usr/bin/env python3
"""Measure lossless delta+RLE suitability for the 640 Packed-UYVY stream.

This is an offline/host-only experiment. It neither changes firmware nor
accepts a compressed stream. Every block has a raw fallback, and every
candidate frame is decoded and compared byte-for-byte before it is counted.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import socket
import time
import urllib.request

from t384_raw16_bench import (
    BenchError,
    FLAG_FRAME_END,
    FLAG_FRAME_START,
    PIXEL_FORMAT_PACKED_UYVY,
    read_chunk,
    verify_headers,
)

WIDTH = 640
HEIGHT = 512
BLOCK_ROWS = 4
BLOCK_PREFIX_BYTES = 32
BLOCK_Y_BYTES = WIDTH * BLOCK_ROWS
BLOCK_BYTES = BLOCK_PREFIX_BYTES + BLOCK_Y_BYTES
FRAME_BYTES = BLOCK_BYTES * (HEIGHT // BLOCK_ROWS)


def encode_y_delta_rle(y: bytes) -> bytes:
    """Encode an exact 8-bit delta stream as first value + (run, delta).

    The caller decides whether this result is smaller than the raw fallback.
    Addition on decode is modulo 256, so all source bytes are preserved.
    """
    if len(y) != BLOCK_Y_BYTES:
        raise ValueError("unexpected Y block length")
    encoded = bytearray((y[0],))
    previous = y[0]
    run = 0
    run_delta = 0
    for value in y[1:]:
        delta = (value - previous) & 0xFF
        previous = value
        if run and (delta != run_delta or run == 255):
            encoded.extend((run, run_delta))
            run = 0
        if not run:
            run_delta = delta
        run += 1
    if run:
        encoded.extend((run, run_delta))
    return bytes(encoded)


def decode_y_delta_rle(encoded: bytes) -> bytes:
    if not encoded:
        raise BenchError("empty delta-RLE block")
    decoded = bytearray((encoded[0],))
    cursor = 1
    while cursor < len(encoded):
        if cursor + 2 > len(encoded):
            raise BenchError("truncated delta-RLE run")
        count, delta = encoded[cursor], encoded[cursor + 1]
        cursor += 2
        if count == 0 or len(decoded) + count > BLOCK_Y_BYTES:
            raise BenchError("invalid delta-RLE run")
        previous = decoded[-1]
        for _ in range(count):
            previous = (previous + delta) & 0xFF
            decoded.append(previous)
    if len(decoded) != BLOCK_Y_BYTES:
        raise BenchError("delta-RLE decoded length mismatch")
    return bytes(decoded)


def _zigzag(value: int) -> int:
    return value * 2 if value >= 0 else (-value * 2 - 1)


def _unzigzag(value: int) -> int:
    return value // 2 if value % 2 == 0 else -(value // 2) - 1


def encode_y_delta_rice(y: bytes, k: int) -> bytes:
    if not 0 <= k <= 7:
        raise ValueError("Rice k must be 0..7")
    # A candidate larger than the raw fallback is useless. Stop before
    # materializing a pathological unary stream (especially k=0 on noise).
    max_bits = (BLOCK_Y_BYTES + 2) * 8
    bits_used = 0
    bits: list[int] = []
    previous = y[0]
    for value in y[1:]:
        delta = (value - previous) & 0xFF
        previous = value
        signed = delta if delta < 128 else delta - 256
        value_u = _zigzag(signed)
        quotient, remainder = value_u >> k, value_u & ((1 << k) - 1)
        needed = quotient + 1 + k
        if bits_used + needed > max_bits:
            return b""
        bits_used += needed
        bits.extend([0] * quotient)
        bits.append(1)
        bits.extend((remainder >> bit) & 1 for bit in range(k - 1, -1, -1))
    output = bytearray((y[0],))
    current = 0
    count = 0
    for bit in bits:
        current = (current << 1) | bit
        count += 1
        if count == 8:
            output.append(current)
            current = count = 0
    if count:
        output.append(current << (8 - count))
    return bytes(output)


def decode_y_delta_rice(encoded: bytes, k: int) -> bytes:
    if not encoded or not 0 <= k <= 7:
        raise BenchError("invalid Rice block")
    decoded = bytearray((encoded[0],))
    bit_offset = 8
    total_bits = len(encoded) * 8
    while len(decoded) < BLOCK_Y_BYTES:
        quotient = 0
        while bit_offset < total_bits:
            bit = (encoded[bit_offset // 8] >> (7 - bit_offset % 8)) & 1
            bit_offset += 1
            if bit:
                break
            quotient += 1
        else:
            raise BenchError("truncated Rice quotient")
        remainder = 0
        for _ in range(k):
            if bit_offset >= total_bits:
                raise BenchError("truncated Rice remainder")
            remainder = (remainder << 1) | ((encoded[bit_offset // 8] >>
                                             (7 - bit_offset % 8)) & 1)
            bit_offset += 1
        unsigned = (quotient << k) | remainder
        signed = _unzigzag(unsigned)
        decoded.append((decoded[-1] + signed) & 0xFF)
    return bytes(decoded)


def encode_block(block: bytes) -> tuple[bytes, bool]:
    if len(block) != BLOCK_BYTES:
        raise ValueError("unexpected Packed-UYVY block length")
    prefix, y = block[:BLOCK_PREFIX_BYTES], block[BLOCK_PREFIX_BYTES:]
    delta_rle = encode_y_delta_rle(y)
    if len(delta_rle) >= len(y):
        # mode 0 = raw Y bytes; mode 1 = lossless delta+RLE Y bytes.
        return prefix + b"\x00" + y, False
    return prefix + b"\x01" + delta_rle, True


def decode_block(encoded: bytes) -> bytes:
    if len(encoded) < BLOCK_PREFIX_BYTES + 1:
        raise BenchError("truncated packed block")
    prefix, mode, body = (encoded[:BLOCK_PREFIX_BYTES],
                          encoded[BLOCK_PREFIX_BYTES],
                          encoded[BLOCK_PREFIX_BYTES + 1:])
    if mode == 0:
        if len(body) != BLOCK_Y_BYTES:
            raise BenchError("raw fallback length mismatch")
        y = body
    elif mode == 1:
        y = decode_y_delta_rle(body)
    else:
        raise BenchError("unknown packed block mode")
    return prefix + y


def measure_frame(frame: bytes) -> dict[str, int]:
    if len(frame) != FRAME_BYTES:
        raise BenchError(f"expected {FRAME_BYTES} B packed frame, got {len(frame)}")
    compressed = 0
    rle_blocks = 0
    raw_blocks = 0
    rice_compressed = 0
    rice_raw_blocks = 0
    rice_k_counts = [0] * 8
    for offset in range(0, len(frame), BLOCK_BYTES):
        original = frame[offset:offset + BLOCK_BYTES]
        encoded, rle = encode_block(original)
        if decode_block(encoded) != original:
            raise BenchError("lossless decode mismatch")
        compressed += len(encoded)
        rle_blocks += int(rle)
        raw_blocks += int(not rle)
        prefix, y = original[:BLOCK_PREFIX_BYTES], original[BLOCK_PREFIX_BYTES:]
        candidates = []
        for k in range(8):
            candidate = encode_y_delta_rice(y, k)
            if candidate:
                candidates.append((len(candidate), k, candidate))
        if not candidates:
            candidates = [(len(y) + 1, 0, b"")]
        size, k, candidate = min(candidates, key=lambda item: item[0])
        if size + 2 >= len(y):
            rice_compressed += BLOCK_PREFIX_BYTES + 1 + len(y)
            rice_raw_blocks += 1
        else:
            if decode_y_delta_rice(candidate, k) != y:
                raise BenchError("Rice decode mismatch")
            rice_compressed += BLOCK_PREFIX_BYTES + 2 + size
            rice_k_counts[k] += 1
    return {"raw_bytes": len(frame), "compressed_bytes": compressed,
            "rle_blocks": rle_blocks, "raw_blocks": raw_blocks,
            "rice_bytes": rice_compressed, "rice_raw_blocks": rice_raw_blocks,
            "rice_k_counts": rice_k_counts}


def collect_frames(base: str, count: int, timeout: float,
                   max_seconds: float) -> list[bytes]:
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    frames: list[bytes] = []
    with opener.open(base.rstrip("/") + "/raw16.stream", timeout=timeout) as response:
        # urllib's connect timeout does not bound a later blocking body read.
        # Apply the same bound to the underlying socket so a stalled device
        # cannot leave this offline tool waiting forever.
        sock = getattr(getattr(getattr(response, "fp", None), "raw", None),
                       "_sock", None)
        if sock is not None:
            sock.settimeout(timeout)
        pixel_format, mode_flag, _ = verify_headers(response, allow_geometry=True)
        width = int(response.headers["X-T384-Frame-Width"])
        height = int(response.headers["X-T384-Frame-Height"])
        frame_bytes = int(response.headers["X-T384-Frame-Bytes"])
        chunk_max = int(response.headers["X-T384-Chunk-Payload-Max"])
        if (width, height, frame_bytes, pixel_format) != (
                WIDTH, HEIGHT, FRAME_BYTES, PIXEL_FORMAT_PACKED_UYVY):
            raise BenchError("device is not the expected 640 Packed-UYVY stream")
        header, payload = bytearray(36), bytearray(chunk_max)
        frame = bytearray(frame_bytes)
        sequence = None
        expected_offset = 0
        deadline = time.monotonic() + max_seconds
        while len(frames) < count and time.monotonic() < deadline:
            try:
                seq, offset, _, chunk, flags = read_chunk(
                    response, header, payload, pixel_format, mode_flag,
                    frame_bytes, width, height, chunk_max)
            except (TimeoutError, socket.timeout, OSError) as error:
                if frames:
                    break
                raise BenchError(f"stream read timed out before first frame: {error}")
            if flags & FLAG_FRAME_START:
                sequence, expected_offset = seq, 0
            if sequence != seq or offset != expected_offset:
                sequence = None
                continue
            frame[offset:offset + len(chunk)] = chunk
            expected_offset += len(chunk)
            if flags & FLAG_FRAME_END:
                if expected_offset == frame_bytes:
                    frames.append(bytes(frame))
                sequence = None
    return frames


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://192.168.17.1")
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--max-seconds", type=float, default=60.0)
    parser.add_argument("--input", type=pathlib.Path, action="append",
                        help="existing 331776-byte Packed-UYVY frame; repeatable")
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("out/stability/640-delta-rle.json"))
    args = parser.parse_args()
    if args.frames <= 0 or args.timeout <= 0 or args.max_seconds <= 0:
        parser.error("frames, timeout and max-seconds must be positive")

    started = time.monotonic()
    frames = [path.read_bytes() for path in args.input] if args.input else \
        collect_frames(args.base, args.frames, args.timeout, args.max_seconds)
    results = [measure_frame(frame) for frame in frames]
    raw = sum(item["raw_bytes"] for item in results)
    compressed = sum(item["compressed_bytes"] for item in results)
    rice = sum(item["rice_bytes"] for item in results)
    rice_k_counts = [sum(item["rice_k_counts"][k] for item in results)
                     for k in range(8)]
    summary = {
        "codec": "delta-rle-and-rice-lossless-bench-v1",
        "frames": len(results),
        "raw_bytes": raw,
        "compressed_bytes": compressed,
        "ratio": compressed / raw,
        "rice_bytes": rice,
        "rice_ratio": rice / raw,
        "rle_blocks": sum(item["rle_blocks"] for item in results),
        "raw_fallback_blocks": sum(item["raw_blocks"] for item in results),
        "rice_raw_fallback_blocks": sum(item["rice_raw_blocks"] for item in results),
        "rice_k_counts": rice_k_counts,
        "elapsed_s": time.monotonic() - started,
        "target_ratio_for_30fps": 0.65,
        "meets_30fps_transport_gate": compressed / raw <= 0.65,
        "rice_meets_30fps_transport_gate": rice / raw <= 0.65,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
