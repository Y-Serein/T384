#!/usr/bin/env python3
"""Back up or explicitly restore the device's quarantined v1 calibration slot.

This never fits a model, writes MINI2, or enables OEM temperature output.
Only --restore performs device Flash writes. Export refuses to overwrite files.
"""
from __future__ import annotations

import argparse
import json
import struct
import urllib.error
import urllib.request
import zlib
from pathlib import Path

HEADER = struct.Struct("<7I32s16s32sB3s")
MAGIC = 0x54334331
MODEL = "t384-empirical-2point-v1"
BASE = "/api/v1/calibration/v1/"


def crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def text_field(data: bytes) -> str:
    if b"\0" not in data:
        raise ValueError("unterminated calibration header field")
    return data.split(b"\0", 1)[0].decode("ascii")


def decode_packet(packet: bytes) -> tuple[dict, bytes]:
    if len(packet) < HEADER.size:
        raise ValueError("incomplete calibration header")
    magic, schema, generation, length, payload_crc, header_crc, cal_id, model, profile, identity, gain, _ = HEADER.unpack_from(packet)
    payload = packet[HEADER.size:]
    if magic != MAGIC or schema != 1 or not 0 < length <= 2048 or length != len(payload):
        raise ValueError("invalid calibration packet format/length")
    if text_field(model) != MODEL or text_field(profile) not in ("256x192", "384x288", "640x512"):
        raise ValueError("unsupported calibration model/profile")
    if not cal_id or gain not in (1, 2, 0xFF) or not any(x not in (0, 255) for x in identity):
        raise ValueError("invalid calibration identity/gain/id")
    header = bytearray(packet[:HEADER.size])
    struct.pack_into("<I", header, 20, 0)
    if crc(header) != header_crc or crc(payload) != payload_crc:
        raise ValueError("calibration header/payload CRC mismatch")
    return dict(schema=schema, generation=generation, payload_len=length,
                payload_crc32=payload_crc, header_crc32=header_crc,
                calibration_id=cal_id, model=text_field(model),
                profile=text_field(profile), identity=identity.hex(), gain=gain), payload


def encode_packet(manifest: dict, payload: bytes) -> bytes:
    # Required fields make an old firmware response fail rather than silently
    # dropping the saved module binding when exporting a backup.
    identity = bytes.fromhex(manifest["identity"])
    if len(identity) != 32:
        raise ValueError("invalid identity length")
    header = HEADER.pack(MAGIC, manifest["schema"], manifest["generation"],
                         manifest["payload_len"], manifest["payload_crc32"], 0,
                         manifest["calibration_id"], manifest["model"].encode("ascii"),
                         manifest["profile"].encode("ascii"), identity, manifest["gain"], b"\0"*3)
    header = bytearray(header)
    struct.pack_into("<I", header, 20, crc(header))
    packet = bytes(header) + payload
    decode_packet(packet)
    return packet


def exchange(url: str, method: str = "GET", body: bytes | None = None) -> bytes:
    request = urllib.request.Request(url, data=body, method=method,
                                     headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            data = response.read(4097)
            if len(data) > 4096:
                raise ValueError("device response exceeds calibration API limit")
            return data
    except urllib.error.HTTPError as error:
        detail = error.read(1024).decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {error.code} from {url}: {detail}") from error


def readback(base: str) -> tuple[dict, bytes]:
    before = json.loads(exchange(base + "manifest"))
    payload = exchange(base + "data")
    after = json.loads(exchange(base + "manifest"))
    if before != after:
        raise ValueError("calibration changed during readback; retry export")
    packet = encode_packet(before, payload)
    return decode_packet(packet)[0], packet


def restore(base: str, packet: bytes) -> dict:
    expected, payload = decode_packet(packet)  # no network writes before validation
    staged = json.loads(exchange(base + "data", "PUT", packet))
    if staged.get("staged") is not True:
        raise ValueError("device did not stage calibration")
    committed = json.loads(exchange(base + "commit", "POST", b""))
    if committed.get("committed") is not True:
        raise ValueError("device did not commit calibration")
    actual, saved = readback(base)
    actual_payload = saved[HEADER.size:]
    for key in ("schema", "payload_len", "payload_crc32", "calibration_id", "model", "profile", "identity", "gain"):
        if actual[key] != expected[key]:
            raise ValueError(f"committed calibration {key} differs from input")
    if actual_payload != payload:
        raise ValueError("committed payload differs from input")
    return actual


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.17.1")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--export", type=Path, help="read-only backup; refuses to overwrite")
    action.add_argument("--restore", type=Path, help="explicitly stage/commit a backup into MCU Flash; stop stream first")
    action.add_argument("--check", type=Path, help="validate a local packet without accessing hardware")
    args = parser.parse_args()
    base = args.url.rstrip("/") + BASE
    if args.check:
        manifest, _ = decode_packet(args.check.read_bytes())
    elif args.export:
        if args.export.exists():
            raise FileExistsError("backup exists; choose a new output path")
        manifest, packet = readback(base)
        args.export.parent.mkdir(parents=True, exist_ok=True)
        with args.export.open("xb") as output:
            output.write(packet)
    else:
        manifest = restore(base, args.restore.read_bytes())
    print(json.dumps({"stored_manifest": manifest, "applied": False,
                      "oem_radiometry_ready": False}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, struct.error) as error:
        raise SystemExit(f"Calibration storage failed: {error}") from None
