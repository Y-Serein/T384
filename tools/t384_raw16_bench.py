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
PIXEL_FORMAT_LE16 = 1
FLAG_FRAME_START = 0x0001
FLAG_FRAME_END = 0x0002
FLAG_SYNTHETIC = 0x0004
HEADER_STRUCT = struct.Struct("<IHHIIIIHHHHHH")
DEFAULT_URL = "http://192.168.18.1/raw16.stream"


class BenchError(RuntimeError):
    pass


def expected_frame() -> bytes:
    frame = bytearray(FRAME_BYTES)
    for word_index in range(WIDTH * HEIGHT):
        struct.pack_into("<H", frame, word_index * 2, (word_index * 257 + 0x0348) & 0xFFFF)
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


def decode_header(header: bytearray) -> tuple[int, int, int, int, int]:
    (
        magic,
        version,
        header_bytes,
        sequence,
        frame_offset,
        frame_bytes,
        capture_ms,
        payload_bytes,
        width,
        height,
        flags,
        pixel_format,
        crc,
    ) = HEADER_STRUCT.unpack(header)
    if magic != WIRE_MAGIC or version != WIRE_VERSION or header_bytes != WIRE_HEADER_BYTES:
        raise BenchError(f"invalid chunk envelope magic/version: 0x{magic:08X}/{version}/{header_bytes}")
    if binascii.crc_hqx(memoryview(header)[:34], 0) != crc:
        raise BenchError(f"frame {sequence} offset {frame_offset}: header CRC mismatch")
    if frame_bytes != FRAME_BYTES or width != WIDTH or height != HEIGHT:
        raise BenchError(f"frame {sequence}: geometry mismatch {width}x{height}/{frame_bytes}")
    if pixel_format != PIXEL_FORMAT_LE16:
        raise BenchError(f"frame {sequence}: pixel format {pixel_format} is not RAW16LE")
    if not 0 < payload_bytes <= CHUNK_PAYLOAD_MAX or frame_offset + payload_bytes > FRAME_BYTES:
        raise BenchError(f"frame {sequence}: invalid chunk {frame_offset}+{payload_bytes}")
    return sequence, frame_offset, capture_ms, payload_bytes, flags


def verify_headers(response) -> None:
    if response.status != 200:
        raise BenchError(f"HTTP status is {response.status}, expected 200")
    required = {
        "Content-Type": "application/x-t384-raw16-chunks",
        "X-T384-Format": "RAW16LE-CHUNK-V1",
        "X-T384-Wire-Version": str(WIRE_VERSION),
        "X-T384-Chunk-Header-Bytes": str(WIRE_HEADER_BYTES),
        "X-T384-Chunk-Payload-Max": str(CHUNK_PAYLOAD_MAX),
        "X-T384-Frame-Width": str(WIDTH),
        "X-T384-Frame-Height": str(HEIGHT),
        "X-T384-Frame-Bytes": str(FRAME_BYTES),
    }
    for name, expected in required.items():
        actual = response.headers.get(name)
        if actual != expected:
            raise BenchError(f"{name} is {actual!r}, expected {expected!r}")


class SourceContract:
    def __init__(self, expected_mode: str) -> None:
        self.expected_mode = expected_mode
        self.synthetic: bool | None = None

    def validate(self, flags: int) -> bool:
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
    def __init__(self, expected: bytes | None, source: SourceContract) -> None:
        self.expected_view = memoryview(expected) if expected is not None else None
        self.source = source
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
            if not self.valid or self.expected_offset != FRAME_BYTES:
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


def read_chunk(stream, header: bytearray, payload: bytearray):
    read_exact(stream, header)
    sequence, offset, capture_ms, payload_bytes, flags = decode_header(header)
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
    expected = None if args.expect_source == "real" else expected_frame()

    print(
        f"连接 {args.url}；正式分块链路={WIDTH}x{HEIGHT} RAW16LE "
        f"({FRAME_BYTES} B/frame)，完整帧门槛={args.min_mb_s:.3f} MB/s"
    )
    with opener.open(request, timeout=args.timeout) as response:
        verify_headers(response)

        source = SourceContract(args.expect_source)
        warmup = FrameAssembler(expected, source)
        warmup_deadline = time.monotonic() + args.warmup
        while time.monotonic() < warmup_deadline:
            sequence, offset, _, chunk, flags = read_chunk(response, header, payload)
            warmup.consume(sequence, offset, chunk, flags)

        measured = FrameAssembler(expected, source)
        while True:
            sequence, offset, _, chunk, flags = read_chunk(response, header, payload)
            if flags & FLAG_FRAME_START:
                break
        started = time.monotonic()
        measured.consume(sequence, offset, chunk, flags)
        deadline = started + args.duration
        while time.monotonic() < deadline:
            sequence, offset, _, chunk, flags = read_chunk(response, header, payload)
            measured.consume(sequence, offset, chunk, flags)
        elapsed = time.monotonic() - started

    complete_payload = measured.complete_frames * FRAME_BYTES
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
    print("PASS：采集适配器、固定队列、分块协议、全像素、帧序号与持续吞吐均通过")
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
