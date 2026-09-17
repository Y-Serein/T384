#!/usr/bin/env python3
"""Host-side format negotiation and chunk self-description smoke test."""

from __future__ import annotations

import binascii
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch


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
        self.headers = {
            "Content-Type": "application/x-t384-frame-chunks",
            "X-T384-Format": "T384-FRAME-CHUNK-V1",
            "X-T384-Wire-Version": str(bench.WIRE_VERSION),
            "X-T384-Chunk-Header-Bytes": str(bench.WIRE_HEADER_BYTES),
            "X-T384-Chunk-Payload-Max": str(width * 2 * 8),
            "X-T384-Frame-Width": str(width),
            "X-T384-Frame-Height": str(height),
            "X-T384-Frame-Bytes": str(width * height * 2),
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
    bench.HEADER_STRUCT.pack_into(
        header, 0, bench.WIRE_MAGIC, bench.WIRE_VERSION,
        bench.WIRE_HEADER_BYTES, 7, offset, width * height * 2, 1234,
        width * 2 * 8, width, height, flags,
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

    for width, height in ((256, 192), (384, 288)):
        for mode, name, pixel_format, mode_flag in cases:
            response = Response(bench, mode, name, pixel_format, width, height)
            assert bench.verify_headers(response, allow_geometry=True) == (
                pixel_format, mode_flag, mode
            )
            frame_bytes = width * height * 2
            chunk_bytes = width * 2 * 8
            assembler = bench.FrameAssembler(
                None, bench.SourceContract("real", mode_flag), frame_bytes
            )
            for offset in range(0, frame_bytes, chunk_bytes):
                flags = mode_flag
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

    # Exercise the blackbody tool through its real negotiation, parser and ROI
    # path. A 384 source has no WN2256 temperature model before calibration.
    sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve().parent))
    import capture_radiometry_calibration as capture_tool
    stream = io.BytesIO()
    width, height = 384, 288
    frame_bytes, chunk_bytes = width * height * 2, width * 2 * 8
    for offset in range(0, frame_bytes, chunk_bytes):
        flags = bench.FLAG_TPD_Y16
        if offset == 0:
            flags |= bench.FLAG_FRAME_START
        if offset + chunk_bytes == frame_bytes:
            flags |= bench.FLAG_FRAME_END
        stream.write(chunk_header(bench, flags, bench.PIXEL_FORMAT_Y16_BE,
                                  width, height, offset))
        stream.write(b"\x27\x10" * (chunk_bytes // 2))
    stream.seek(0)
    stream.status = 200
    stream.headers = Response(bench, "tpd", "Y16BE", bench.PIXEL_FORMAT_Y16_BE,
                              width, height).headers
    with tempfile.TemporaryDirectory(prefix="t384-capture-384-") as temp:
        output = pathlib.Path(temp)
        args = SimpleNamespace(output=output, diag_url="http://192.168.18.1/diag",
                               url="http://192.168.18.1/raw16.stream", frames=1,
                               timeout=1, setpoint_c=0, gain="high", distance_m=0.01,
                               emissivity=0.98, humidity=None, ta_c=None, tu_c=None)
        with patch.object(capture_tool, "fetch_diag", return_value={"source.pixel_format": "2"}), \
                patch.object(capture_tool.urllib.request, "build_opener") as opener:
            opener.return_value.open.return_value = stream
            assert capture_tool.capture(args) == 0
        manifest = json.loads((output / "manifest.json").read_text())
        frame = manifest["frames"][0]
        assert frame["bytes"] == frame_bytes
        assert (output / frame["path"]).stat().st_size == frame_bytes
        assert frame["roi"]["x"] == 184 and frame["roi"]["y"] == 136
        assert frame["roi"]["mean_y16"] == 10000

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

    print("T384 host 256/384 Y16/Picture negotiation and complete-frame smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
