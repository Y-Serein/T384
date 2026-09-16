#!/usr/bin/env python3
"""Host-side format negotiation and chunk self-description smoke test."""

from __future__ import annotations

import binascii
import importlib.util
import pathlib
import sys


def load_bench(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location("t384_raw16_bench", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Response:
    status = 200

    def __init__(self, bench, mode: str, pixel_name: str, pixel_format: int):
        self.headers = {
            "Content-Type": "application/x-t384-frame-chunks",
            "X-T384-Format": "T384-FRAME-CHUNK-V1",
            "X-T384-Wire-Version": str(bench.WIRE_VERSION),
            "X-T384-Chunk-Header-Bytes": str(bench.WIRE_HEADER_BYTES),
            "X-T384-Chunk-Payload-Max": str(bench.CHUNK_PAYLOAD_MAX),
            "X-T384-Frame-Width": str(bench.WIDTH),
            "X-T384-Frame-Height": str(bench.HEIGHT),
            "X-T384-Frame-Bytes": str(bench.FRAME_BYTES),
            "X-T384-Frame-Mode": mode,
            "X-T384-Pixel-Format": pixel_name,
            "X-T384-Pixel-Format-Code": str(pixel_format),
            "X-T384-Temperature-Model": (
                "experimental-blackbody-2point-v1" if mode == "tpd" else "unavailable"
            ),
        "X-T384-Y16-Linear-X100": "3865997,11828",
        }


def chunk_header(bench, flags: int, pixel_format: int) -> bytearray:
    header = bytearray(bench.WIRE_HEADER_BYTES)
    bench.HEADER_STRUCT.pack_into(
        header, 0, bench.WIRE_MAGIC, bench.WIRE_VERSION,
        bench.WIRE_HEADER_BYTES, 7, 0, bench.FRAME_BYTES, 1234,
        bench.CHUNK_PAYLOAD_MAX, bench.WIDTH, bench.HEIGHT, flags,
        pixel_format, 0,
    )
    crc = binascii.crc_hqx(memoryview(header)[:34], 0)
    header[34] = crc & 0xFF
    header[35] = crc >> 8
    return header


def main() -> int:
    bench = load_bench(pathlib.Path(sys.argv[1]))
    cases = (
        ("tpd", "Y16BE", bench.PIXEL_FORMAT_Y16_BE, bench.FLAG_TPD_Y16),
        ("picture-fallback", "UYVY", bench.PIXEL_FORMAT_UYVY,
         bench.FLAG_PICTURE_UYVY),
    )
    for mode, name, pixel_format, mode_flag in cases:
        negotiated = bench.verify_headers(Response(bench, mode, name, pixel_format))
        assert negotiated == (pixel_format, mode_flag, mode)
        decoded = bench.decode_header(
            chunk_header(bench, bench.FLAG_FRAME_START | mode_flag,
                         pixel_format), pixel_format, mode_flag
        )
        assert decoded[0] == 7 and decoded[-1] & mode_flag

    try:
        bench.verify_headers(Response(
            bench, "tpd", "UYVY", bench.PIXEL_FORMAT_UYVY
        ))
    except bench.BenchError:
        pass
    else:
        raise AssertionError("inconsistent HTTP format was accepted")

    try:
        bench.decode_header(
            chunk_header(bench, bench.FLAG_FRAME_START |
                         bench.FLAG_PICTURE_UYVY,
                         bench.PIXEL_FORMAT_UYVY),
            bench.PIXEL_FORMAT_Y16_BE, bench.FLAG_TPD_Y16,
        )
    except bench.BenchError:
        pass
    else:
        raise AssertionError("chunk/HTTP format mismatch was accepted")

    print("T384 host Y16/Picture format negotiation smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
