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

    def __init__(self, bench, mode: str, pixel_name: str, pixel_format: int,
                 width: int = 256, height: int = 192):
        packed = pixel_format == bench.PIXEL_FORMAT_PACKED_UYVY
        version = 2 if packed else bench.WIRE_VERSION
        self.headers = {
            "Content-Type": "application/x-t384-frame-chunks",
            "X-T384-Format": f"T384-FRAME-CHUNK-V{version}",
            "X-T384-Wire-Version": str(version),
            "X-T384-Chunk-Header-Bytes": str(bench.WIRE_HEADER_BYTES),
            "X-T384-Chunk-Payload-Max": str(width * 4 + 32 if packed else
                                            width * 2 * (4 if width == 640 else 8)),
            "X-T384-Frame-Width": str(width),
            "X-T384-Frame-Height": str(height),
            "X-T384-Frame-Bytes": str((width * 4 + 32) * (height // 4) if packed else
                                      width * height * 2),
            "X-T384-Frame-Mode": mode,
            "X-T384-Pixel-Format": pixel_name,
            "X-T384-Pixel-Format-Code": str(pixel_format),
            "X-T384-Temperature-Model": (
                "experimental-blackbody-2point-v1"
                if mode == "tpd" and width == 256 else "unavailable"
            ),
            "X-T384-Y16-Linear-X100": "3865997,11828" if width == 256 else "0,0",
        }


def chunk_header(bench, flags: int, pixel_format: int,
                 width: int = 256, height: int = 192,
                 offset: int = 0) -> bytearray:
    header = bytearray(bench.WIRE_HEADER_BYTES)
    packed = pixel_format == bench.PIXEL_FORMAT_PACKED_UYVY
    frame_bytes = (width * 4 + 32) * (height // 4) if packed else width * height * 2
    chunk_bytes = width * 4 + 32 if packed else width * 2 * (4 if width == 640 else 8)
    bench.HEADER_STRUCT.pack_into(
        header, 0, bench.WIRE_MAGIC, 2 if packed else bench.WIRE_VERSION,
        bench.WIRE_HEADER_BYTES, 7, offset, frame_bytes, 1234,
        chunk_bytes, width, height, flags,
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

    for width, height in ((256, 192), (384, 288), (640, 512)):
        for mode, name, pixel_format, mode_flag in cases:
            if width == 640 and mode == "picture-fallback":
                name, pixel_format = "PACKED-UYVY", bench.PIXEL_FORMAT_PACKED_UYVY
            response = Response(bench, mode, name, pixel_format, width, height)
            assert bench.verify_headers(response, allow_geometry=True) == (
                pixel_format, mode_flag, mode
            )
            packed = pixel_format == bench.PIXEL_FORMAT_PACKED_UYVY
            frame_bytes = (width * 4 + 32) * (height // 4) if packed else width * height * 2
            chunk_bytes = width * 4 + 32 if packed else width * 2 * (4 if width == 640 else 8)
            assembler = bench.FrameAssembler(
                None, bench.SourceContract("real", mode_flag), frame_bytes
            )
            for offset in range(0, frame_bytes, chunk_bytes):
                flags = mode_flag
                if packed:
                    flags |= bench.FLAG_PICTURE_PACKED
                if offset == 0:
                    flags |= bench.FLAG_FRAME_START
                if offset + chunk_bytes == frame_bytes:
                    flags |= bench.FLAG_FRAME_END
                decoded = bench.decode_header(
                    chunk_header(bench, flags, pixel_format, width, height, offset),
                    pixel_format, mode_flag, frame_bytes, width, height, chunk_bytes
                )
                assembler.consume(decoded[0], decoded[1], bytes(chunk_bytes), decoded[-1])
            assert assembler.complete_frames == 1 and assembler.discarded_frames == 0

    for field, value in (
        ("X-T384-Temperature-Model", "experimental-blackbody-2point-v1"),
        ("X-T384-Chunk-Payload-Max", "4096"),
        ("X-T384-Frame-Bytes", "98304"),
    ):
        response = Response(bench, "tpd", "Y16BE", bench.PIXEL_FORMAT_Y16_BE, 384, 288)
        response.headers[field] = value
        try:
            bench.verify_headers(response, allow_geometry=True)
        except bench.BenchError:
            pass
        else:
            raise AssertionError(f"invalid 384 response accepted: {field}")

    # Capture with archived tables and live boundary state is exercised by
    # calibration_capture_smoke.py in check_module_files.sh. Keep this test
    # focused on 256/384 wire negotiation and complete-frame assembly.

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

    print("T384 host 256/384/640 Y16/Picture negotiation and complete-frame smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
