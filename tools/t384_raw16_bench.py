#!/usr/bin/env python3
"""Validate the source->ring->NCM->HTTP RAW16 pipeline.

The only simulated component is the capture adapter.  The tool validates every
synthetic RAW16 byte plus the versioned envelope, frame assembly and sustained
complete-frame payload.  With a real source it keeps the same protocol/frame
checks without assuming image pixels.  It writes no capture history and never
stores data on the device.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
import urllib.error
import urllib.request

WIDTH = 256
HEIGHT = 192
FRAME_BYTES = WIDTH * HEIGHT * 2
WIRE_MAGIC = 0x31523354  # bytes: T3R1
WIRE_VERSION = 1
WIRE_HEADER_BYTES = 36
CHUNK_PAYLOAD_MAX = 4096
PIXEL_FORMAT_Y16_BE = 2
PIXEL_FORMAT_UYVY = 3
PIXEL_FORMAT_PACKED_UYVY = 4
FLAG_FRAME_START = 0x0001
FLAG_FRAME_END = 0x0002
FLAG_SYNTHETIC = 0x0004
FLAG_TPD_Y16 = 0x0010
FLAG_PICTURE_UYVY = 0x0020
FLAG_PICTURE_PACKED = 0x0040
FLAG_DATA_MODE_MASK = FLAG_TPD_Y16 | FLAG_PICTURE_UYVY
HEADER_STRUCT = struct.Struct("<IHHIIIIHHHHHH")
DEFAULT_URL = "http://192.168.17.1/raw16.stream"


class BenchError(RuntimeError):
    pass


def expected_frame(width: int = WIDTH, height: int = HEIGHT) -> bytes:
    frame = bytearray(width * height * 2)
    for word_index in range(width * height):
        struct.pack_into(">H", frame, word_index * 2, (word_index * 257 + 0x0348) & 0xFFFF)
    return bytes(frame)


def read_exact(stream, destination) -> None:
    view = memoryview(destination)
    offset = 0
    while offset < len(view):
        count = stream.readinto(view[offset:])
        if count is None:
            chunk = stream.read(len(view) - offset)
            count = len(chunk)
            view[offset : offset + count] = chunk
        if count == 0:
            raise BenchError(f"stream ended: {offset}/{len(view)} bytes")
        offset += count


def decode_header(header: bytearray, expected_pixel_format: int,
                  expected_mode_flag: int, frame_bytes: int = FRAME_BYTES,
                  width: int = WIDTH, height: int = HEIGHT,
                  chunk_payload_max: int = CHUNK_PAYLOAD_MAX) -> tuple[int, int, int, int, int]:
    (
        magic,
        version,
        header_bytes,
        sequence,
        frame_offset,
        chunk_frame_bytes,
        capture_ms,
        payload_bytes,
        chunk_width,
        chunk_height,
        flags,
        pixel_format,
        crc,
    ) = HEADER_STRUCT.unpack(header)
    expected_version = 2 if expected_pixel_format == PIXEL_FORMAT_PACKED_UYVY else WIRE_VERSION
    if magic != WIRE_MAGIC or version != expected_version or header_bytes != WIRE_HEADER_BYTES:
        raise BenchError(f"invalid chunk envelope magic/version: 0x{magic:08X}/{version}/{header_bytes}")
    if binascii.crc_hqx(memoryview(header)[:34], 0) != crc:
        raise BenchError(f"frame {sequence} offset {frame_offset}: header CRC mismatch")
    if chunk_frame_bytes != frame_bytes or chunk_width != width or chunk_height != height:
        raise BenchError(
            f"frame {sequence}: geometry mismatch "
            f"{chunk_width}x{chunk_height}/{chunk_frame_bytes}"
        )
    if pixel_format != expected_pixel_format:
        raise BenchError(
            f"frame {sequence}: pixel format changed {pixel_format}/{expected_pixel_format}"
        )
    if flags & FLAG_DATA_MODE_MASK != expected_mode_flag:
        raise BenchError(f"frame {sequence}: data mode flags 0x{flags:04X} mismatch")
    if bool(flags & FLAG_PICTURE_PACKED) != (expected_pixel_format == PIXEL_FORMAT_PACKED_UYVY):
        raise BenchError(f"frame {sequence}: packed flag mismatch")
    if not 0 < payload_bytes <= chunk_payload_max or frame_offset + payload_bytes > frame_bytes:
        raise BenchError(f"frame {sequence}: invalid chunk {frame_offset}+{payload_bytes}")
    return sequence, frame_offset, capture_ms, payload_bytes, flags


def verify_headers(response, allow_geometry: bool = False) -> tuple[int, int, str]:
    if response.status != 200:
        raise BenchError(f"HTTP status is {response.status}, expected 200")
    required = {
        "Content-Type": "application/x-t384-frame-chunks",
        "X-T384-Chunk-Header-Bytes": str(WIRE_HEADER_BYTES),
    }
    for name, expected in required.items():
        actual = response.headers.get(name)
        if actual != expected:
            raise BenchError(f"{name} is {actual!r}, expected {expected!r}")
    try:
        stream_width = int(response.headers.get("X-T384-Frame-Width", ""))
        stream_height = int(response.headers.get("X-T384-Frame-Height", ""))
        stream_frame_bytes = int(response.headers.get("X-T384-Frame-Bytes", ""))
        stream_chunk_max = int(response.headers.get("X-T384-Chunk-Payload-Max", ""))
    except ValueError as error:
        raise BenchError("invalid stream geometry headers") from error
    if allow_geometry:
        if (stream_width, stream_height) not in ((256, 192), (384, 288), (640, 512)):
            raise BenchError(f"unsupported stream geometry {stream_width}x{stream_height}")
        # The pixel format below determines whether 640 uses the packed v2 geometry.
    elif {
        "X-T384-Chunk-Payload-Max": str(CHUNK_PAYLOAD_MAX),
        "X-T384-Frame-Width": str(WIDTH),
        "X-T384-Frame-Height": str(HEIGHT),
        "X-T384-Frame-Bytes": str(FRAME_BYTES),
    } != {
        name: response.headers.get(name)
        for name in (
            "X-T384-Chunk-Payload-Max",
            "X-T384-Frame-Width",
            "X-T384-Frame-Height",
            "X-T384-Frame-Bytes",
        )
    }:
        raise BenchError("stream geometry does not match 256x192 bench")
    frame_mode = response.headers.get("X-T384-Frame-Mode")
    pixel_name = response.headers.get("X-T384-Pixel-Format")
    try:
        pixel_format = int(response.headers.get("X-T384-Pixel-Format-Code", ""))
    except ValueError as error:
        raise BenchError("invalid X-T384-Pixel-Format-Code") from error
    formats = {
        "tpd": (PIXEL_FORMAT_Y16_BE, "Y16BE", FLAG_TPD_Y16),
        "picture-fallback": (PIXEL_FORMAT_UYVY, "UYVY", FLAG_PICTURE_UYVY),
    }
    if (stream_width, stream_height) == (640, 512) and pixel_format == PIXEL_FORMAT_PACKED_UYVY:
        formats["picture-fallback"] = (
            PIXEL_FORMAT_PACKED_UYVY, "PACKED-UYVY", FLAG_PICTURE_UYVY)
    if frame_mode not in formats:
        raise BenchError(f"unsupported X-T384-Frame-Mode {frame_mode!r}")
    expected_format, expected_name, mode_flag = formats[frame_mode]
    if (pixel_format, pixel_name) != (expected_format, expected_name):
        raise BenchError(
            f"inconsistent pixel format {pixel_format}/{pixel_name!r} for {frame_mode}"
        )
    packed_picture = pixel_format == PIXEL_FORMAT_PACKED_UYVY
    expected_version = 2 if packed_picture else WIRE_VERSION
    if (response.headers.get("X-T384-Format") != f"T384-FRAME-CHUNK-V{expected_version}"
            or response.headers.get("X-T384-Wire-Version") != str(expected_version)):
        raise BenchError("stream wire version does not match pixel format")
    if allow_geometry:
        expected_frame_bytes = ((stream_width * 4 + 32) * (stream_height // 4)
                                if packed_picture else stream_width * stream_height * 2)
        expected_chunk_max = (stream_width * 4 + 32 if packed_picture else
                              stream_width * 2 * (4 if stream_width == 640 else 8))
        if (stream_frame_bytes, stream_chunk_max) != (expected_frame_bytes, expected_chunk_max):
            raise BenchError("stream geometry does not match pixel format")
    temperature_model = response.headers.get("X-T384-Temperature-Model")
    expected_model = (
        "experimental-blackbody-2point-v1"
        if frame_mode == "tpd" and (stream_width, stream_height) == (256, 192)
        else "unavailable"
    )
    if temperature_model != expected_model:
        raise BenchError(
            f"temperature model {temperature_model!r}, expected {expected_model!r}"
        )
    if expected_model == "experimental-blackbody-2point-v1" and response.headers.get(
        "X-T384-Y16-Linear-X100"
    ) != "3865997,11828":
        raise BenchError("missing or invalid Y16 experimental mapping")
    return pixel_format, mode_flag, frame_mode


class SourceContract:
    def __init__(self, expected_mode: str, data_mode_flag: int) -> None:
        self.expected_mode = expected_mode
        self.data_mode_flag = data_mode_flag
        self.synthetic: bool | None = None

    def validate(self, flags: int) -> bool:
        if flags & FLAG_DATA_MODE_MASK != self.data_mode_flag:
            raise BenchError("frame data mode changed inside one stream")
        synthetic = bool(flags & FLAG_SYNTHETIC)
        if self.synthetic is None:
            self.synthetic = synthetic
        elif self.synthetic != synthetic:
            raise BenchError("source type changed inside one stream")
        if self.expected_mode == "synthetic" and not synthetic:
            raise BenchError("expected synthetic source, received real source")
        if self.expected_mode == "real" and synthetic:
            raise BenchError("expected real source, received synthetic source")
        return synthetic

    @property
    def label(self) -> str:
        if self.synthetic is None:
            return "unknown"
        return "synthetic/full-pixel-check" if self.synthetic else "real/frame-integrity-check"


class FrameAssembler:
    def __init__(self, expected: bytes | None, source: SourceContract,
                 frame_bytes: int = FRAME_BYTES) -> None:
        self.expected_view = memoryview(expected) if expected is not None else None
        self.source = source
        self.frame_bytes = frame_bytes
        self.sequence: int | None = None
        self.expected_offset = 0
        self.valid = False
        self.previous_complete: int | None = None
        self.complete_frames = 0
        self.discarded_frames = 0
        self.sequence_gaps = 0
        self.sequence_errors = 0
        self.raw_payload_bytes = 0

    def consume(self, sequence: int, offset: int, payload, flags: int) -> None:
        self.raw_payload_bytes += len(payload)
        if self.source.validate(flags):
            if self.expected_view is None:
                raise BenchError("synthetic source has no expected frame")
            expected_slice = self.expected_view[offset : offset + len(payload)]
            if payload != expected_slice:
                raise BenchError(f"frame {sequence} offset {offset}: RAW16 payload mismatch")

        if flags & FLAG_FRAME_START:
            if self.sequence is not None:
                self.discarded_frames += 1
            self.sequence = sequence
            self.expected_offset = 0
            self.valid = True
        if self.sequence != sequence or offset != self.expected_offset:
            self.valid = False
        else:
            self.expected_offset += len(payload)

        if flags & FLAG_FRAME_END:
            if not self.valid or self.expected_offset != self.frame_bytes:
                self.discarded_frames += 1
            else:
                if self.previous_complete is not None:
                    delta = (sequence - self.previous_complete) & 0xFFFFFFFF
                    if 1 < delta < 0x80000000:
                        self.sequence_gaps += delta - 1
                    elif delta != 1:
                        self.sequence_errors += 1
                self.previous_complete = sequence
                self.complete_frames += 1
            self.sequence = None
            self.expected_offset = 0
            self.valid = False


def read_chunk(stream, header: bytearray, payload: bytearray,
               pixel_format: int, mode_flag: int, frame_bytes: int = FRAME_BYTES,
               width: int = WIDTH, height: int = HEIGHT,
               chunk_payload_max: int = CHUNK_PAYLOAD_MAX):
    read_exact(stream, header)
    sequence, offset, capture_ms, payload_bytes, flags = decode_header(
        header, pixel_format, mode_flag, frame_bytes, width, height,
        chunk_payload_max
    )
    chunk = memoryview(payload)[:payload_bytes]
    read_exact(stream, chunk)
    return sequence, offset, capture_ms, chunk, flags


def run(args: argparse.Namespace) -> int:
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    request = urllib.request.Request(
        args.url,
        headers={"User-Agent": "t384-raw16-pipeline/1", "Accept": "*/*"},
    )
    header = bytearray(WIRE_HEADER_BYTES)
    payload = bytearray(CHUNK_PAYLOAD_MAX)
    with opener.open(request, timeout=args.timeout) as response:
        pixel_format, mode_flag, frame_mode = verify_headers(response, allow_geometry=True)
        width = int(response.headers["X-T384-Frame-Width"])
        height = int(response.headers["X-T384-Frame-Height"])
        frame_bytes = int(response.headers["X-T384-Frame-Bytes"])
        chunk_payload_max = int(response.headers["X-T384-Chunk-Payload-Max"])
        payload = bytearray(chunk_payload_max)
        expected = (expected_frame(width, height) if args.expect_source != "real" and
                    pixel_format == PIXEL_FORMAT_Y16_BE else None)
        print(
            f"连接 {args.url}；正式分块链路={width}x{height} {frame_mode} "
            f"({frame_bytes} B/frame)，完整帧门槛={args.min_mb_s:.3f} MB/s"
        )

        source = SourceContract(args.expect_source, mode_flag)
        warmup = FrameAssembler(expected, source, frame_bytes)
        warmup_deadline = time.monotonic() + args.warmup
        while time.monotonic() < warmup_deadline:
            sequence, offset, _, chunk, flags = read_chunk(
                response, header, payload, pixel_format, mode_flag,
                frame_bytes, width, height, chunk_payload_max
            )
            warmup.consume(sequence, offset, chunk, flags)

        measured = FrameAssembler(expected, source, frame_bytes)
        while True:
            sequence, offset, _, chunk, flags = read_chunk(
                response, header, payload, pixel_format, mode_flag,
                frame_bytes, width, height, chunk_payload_max
            )
            if flags & FLAG_FRAME_START:
                break
        started = time.monotonic()
        measured.consume(sequence, offset, chunk, flags)
        deadline = started + args.duration
        while time.monotonic() < deadline:
            sequence, offset, _, chunk, flags = read_chunk(
                response, header, payload, pixel_format, mode_flag,
                frame_bytes, width, height, chunk_payload_max
            )
            measured.consume(sequence, offset, chunk, flags)
        elapsed = time.monotonic() - started

    complete_payload = measured.complete_frames * frame_bytes
    complete_mb_s = complete_payload / elapsed / 1_000_000
    wire_payload_mb_s = measured.raw_payload_bytes / elapsed / 1_000_000
    fps = measured.complete_frames / elapsed
    print(
        f"结果：完整 {measured.complete_frames} 帧，{fps:.3f} FPS，"
        f"完整帧有效负载 {complete_mb_s:.3f} MB/s，线上RAW负载 {wire_payload_mb_s:.3f} MB/s；"
        f"序号缺口 {measured.sequence_gaps}，序列异常 {measured.sequence_errors}，"
        f"丢弃不完整帧 {measured.discarded_frames}；源模式 {source.label}；"
        f"预热完整 {warmup.complete_frames} 帧"
    )
    failures: list[str] = []
    if complete_mb_s < args.min_mb_s:
        failures.append(f"完整帧有效负载 {complete_mb_s:.3f} < {args.min_mb_s:.3f} MB/s")
    if measured.sequence_gaps:
        failures.append(f"完整帧序号缺口 {measured.sequence_gaps}")
    if measured.sequence_errors:
        failures.append(f"重复/逆序帧 {measured.sequence_errors}")
    if measured.discarded_frames:
        failures.append(f"不完整帧 {measured.discarded_frames}")
    if failures:
        print("FAIL：" + "；".join(failures), file=sys.stderr)
        return 1
    print("PASS：完整帧、分块校验、帧序号与设置的吞吐门槛通过；不代表测温精度或长期稳定性认证")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="T384 RAW16 正式流水线完整性和吞吐验证")
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--warmup", type=float, default=3.0)
    parser.add_argument("--min-mb-s", type=float, default=4.5)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--expect-source",
        choices=("auto", "synthetic", "real"),
        default="auto",
        help="auto detects the source flag; synthetic additionally validates every pixel",
    )
    args = parser.parse_args()
    if args.duration <= 0 or args.warmup < 0 or args.min_mb_s <= 0:
        parser.error("duration/min-mb-s must be positive and warmup non-negative")
    return args


def main() -> int:
    try:
        return run(parse_args())
    except (BenchError, OSError, urllib.error.URLError) as error:
        print(f"FAIL：{error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("中断", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
