#!/usr/bin/env python3
"""Validate vendor radiometry tables and emit a provisioning manifest.

The binary tables are supplied by the module vendor or by an approved
calibration run.  This tool authenticates their shape and hashes, but it does
not infer product identity, gain, or the Vtemp-index algorithm from filenames.
Those values must be supplied explicitly in the manifest metadata.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def read_values(path: Path, byte_order: str, signed: bool = False) -> list[int]:
    data = path.read_bytes()
    if len(data) % 2:
        raise ValueError(f"{path}: odd byte length {len(data)}")
    prefix = "<" if byte_order == "little" else ">"
    code = "h" if signed else "H"
    return list(struct.unpack(prefix + "%d%s" % (len(data) // 2, code), data))


def check_table(
    path: Path, expected_bytes: int, kind: str, byte_order: str
) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) != expected_bytes:
        raise ValueError(
            f"{path}: {kind} requires {expected_bytes} bytes, got {len(data)}"
        )
    values = read_values(path, byte_order, signed=kind == "bt")
    result: dict[str, object] = {
        "path": str(path),
        "kind": kind,
        "bytes": len(data),
        "items": len(values),
        "sha256": hashlib.sha256(data).hexdigest(),
        "min": min(values),
        "max": max(values),
    }
    if kind == "nuc_t":
        result["non_increasing"] = all(
            left >= right for left, right in zip(values, values[1:])
        )
    return result


def metadata(args: argparse.Namespace) -> dict[str, object]:
    required = {
        "product": args.product,
        "pn": args.pn,
        "sn": args.sn,
        "module_firmware": args.module_firmware,
        "gain": args.gain,
        "calibration_version": args.version,
        "ktemp": args.ktemp,
        "btemp": args.btemp,
        "address_ca": args.address_ca,
    }
    missing = [name for name, value in required.items() if value is None]
    return {
        **required,
        "missing_metadata": missing,
        "source": args.source,
        "calibration_epoch": args.calibration_epoch,
        "byte_order": args.byte_order,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kt", type=Path, required=True)
    parser.add_argument("--bt", type=Path, required=True)
    parser.add_argument("--nuc-t", type=Path, required=True)
    parser.add_argument("--version", type=int, choices=(2, 3), required=True)
    parser.add_argument("--product")
    parser.add_argument("--pn")
    parser.add_argument("--sn")
    parser.add_argument("--module-firmware")
    parser.add_argument("--gain", choices=("high", "low"))
    parser.add_argument("--ktemp", type=int)
    parser.add_argument("--btemp", type=int)
    parser.add_argument("--address-ca", type=lambda value: int(value, 0))
    parser.add_argument("--source")
    parser.add_argument("--calibration-epoch", type=int)
    parser.add_argument("--byte-order", choices=("little", "big"), default="little")
    parser.add_argument(
        "--distance",
        type=Path,
        help="optional distance-correction table; omitted means manifest is incomplete",
    )
    parser.add_argument(
        "--kt-items",
        type=int,
        help="explicit KT/BT item count for a vendor variant not covered by V2/V3",
    )
    parser.add_argument("--nuc-items", type=int, default=16384)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    kt_len = args.kt_items or (1021 if args.version == 2 else 3601)
    report = {
        "format": "t384-radiometry-manifest-v1",
        "status": "incomplete",
        "identity": metadata(args),
        "calibration_version": args.version,
        "kt": check_table(args.kt, kt_len * 2, "kt", args.byte_order),
        "bt": check_table(args.bt, kt_len * 2, "bt", args.byte_order),
        "nuc_t": check_table(
            args.nuc_t, args.nuc_items * 2, "nuc_t", args.byte_order
        ),
    }
    if args.distance is not None:
        report["distance"] = {
            "path": str(args.distance),
            "bytes": args.distance.stat().st_size,
            "sha256": hashlib.sha256(args.distance.read_bytes()).hexdigest(),
        }
    else:
        report["distance"] = None

    identity = report["identity"]
    if not identity["missing_metadata"] and args.distance is not None:
        report["status"] = "ready-for-provisioning"
    text = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
