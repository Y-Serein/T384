#!/usr/bin/env python3
"""Lossless and fallback smoke checks for the 640 delta+RLE host experiment."""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "delta_rle", ROOT / "tools/t384_picture_delta_rle_bench.py")
codec = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(codec)


def frame(y):
    block = bytes(32) + y
    return block * (codec.HEIGHT // codec.BLOCK_ROWS)


smooth = bytes([80]) * codec.BLOCK_Y_BYTES
state = 0x12345678
random_bytes = bytearray()
for _ in range(codec.BLOCK_Y_BYTES):
    state = (1664525 * state + 1013904223) & 0xFFFFFFFF
    random_bytes.append(state >> 24)
noisy = bytes(random_bytes)
for payload, expected_rle in ((smooth, True), (noisy, False)):
    encoded, rle = codec.encode_block(bytes(32) + payload)
    assert rle is expected_rle
    assert codec.decode_block(encoded) == bytes(32) + payload
    for k in range(8):
        rice = codec.encode_y_delta_rice(payload, k)
        if rice:
            assert codec.decode_y_delta_rice(rice, k) == payload

summary = codec.measure_frame(frame(smooth))
assert summary["compressed_bytes"] < summary["raw_bytes"] * 0.65
summary = codec.measure_frame(frame(noisy))
assert summary["raw_blocks"] == codec.HEIGHT // codec.BLOCK_ROWS
print("640 Packed-UYVY delta-RLE lossless/fallback smoke passed")
