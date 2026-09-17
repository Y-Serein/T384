#!/usr/bin/env python3
"""Read OEM tables through T384 NCM/UART; never writes MINI2 or host calibration."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time
import urllib.error
import urllib.request
import zlib

from validate_radiometry_tables import check_table

IDS = ("nuct-high", "kt-high", "bt-high", "distance-high",
       "nuct-low", "kt-low", "bt-low", "distance-low")
KNOWN_SHA = "718d61a69cced663015d509973e75af07920453461b582cbbd367ca98210e4de"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RuntimeError("Device API redirect rejected")


class Device:
    def __init__(self, base: str):
        if base.rstrip("/") not in ("http://192.168.17.1", "http://192.168.18.1"):
            raise ValueError("This bring-up tool only targets the local USB device at 192.168.17.1 or legacy 192.168.18.1")
        self.base = base.rstrip("/") + "/api/v1/module-files/"
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def request(self, route: str, body: str | None = None):
        request = urllib.request.Request(
            self.base + route,
            data=None if body is None else body.encode("ascii"),
            headers={} if body is None else {"Content-Type": "application/octet-stream"},
            method="GET" if body is None else "POST",
        )
        try:
            with self.opener.open(request, timeout=20) as response:
                data = response.read(32769)
                if len(data) > 32768:
                    raise ValueError("response exceeds maximum table size")
                return data, response.headers
        except urllib.error.HTTPError as error:
            detail = error.read(1024).decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {error.code}: {detail}") from error

    def json(self, route: str, body: str | None = None) -> dict:
        return json.loads(self.request(route, body)[0])


def validate_download(status: dict, data: bytes, headers, expected_id: str,
                      known_wn2256: bool) -> dict:
    if (status.get("state") != "ready" or status.get("id") != expected_id or
            status.get("identity_verified") is not True or status.get("error") != 0 or
            status.get("close_status") != 0 or status.get("cleanup_failed") is not False or
            not status.get("pn") or not status.get("sn") or len(status.get("fw_hex", "")) != 22):
        raise ValueError("device identity or transaction is not verified")
    if not data or len(data) % 2 or len(data) > 32768:
        raise ValueError("empty, odd or oversized table")
    crc = zlib.crc32(data)
    if (len(data) != status["length"] or len(data) != status["received"] or
            crc != status["crc32"] or int(headers["X-T384-Transaction"]) != status["transaction"] or
            int(headers["X-T384-CRC32"], 16) != crc or int(headers["Content-Length"]) != len(data)):
        raise ValueError("length/transaction/CRC32 mismatch; entire download rejected")
    sha = hashlib.sha256(data).hexdigest()
    if known_wn2256:
        if (expected_id != "nuct-high" or status["pn"] != "WN2256" or
                status["fw_hex"] != b"00.00.08.03".hex() or len(data) != 32768 or sha != KNOWN_SHA):
            raise ValueError("WN2256 NUC-T first proof failed: PN/FW/32768 B/known SHA mismatch")
    return {"bytes": len(data), "items": len(data)//2, "sha256": sha,
            "crc32": crc, "transport_verified": True, "oem_radiometry_ready": False}


def read_one(device: Device, table_id: str, output: Path, proof: bool) -> bool:
    report = {"id": table_id, "gain": table_id.rsplit("-", 1)[1], "success": False}
    transaction = None
    try:
        status = device.json("read", table_id)
        transaction = status["transaction"]
        report["start"] = status
        deadline = time.monotonic()+40
        while status["state"] == "reading":
            if time.monotonic() >= deadline:
                raise TimeoutError("file transaction exceeded host deadline")
            time.sleep(0.1)
            status = device.json("status")
            if status["transaction"] != transaction:
                raise RuntimeError("transaction changed while polling")
        report["status"] = status
        if status["state"] != "ready":
            raise RuntimeError(f"module read failed: {json.dumps(status, ensure_ascii=False)}")
        data, headers = device.request(f"data?transaction={transaction}")
        report.update(validate_download(status, data, headers, table_id, proof))
        path = output / f"{table_id}.bin"
        with path.open("xb") as stream:
            stream.write(data)
        kind = table_id.rsplit("-", 1)[0]
        report["table"] = check_table(path, len(data), "nuc_t" if kind == "nuct" else kind, "little")
        known_sizes = {"kt": (2042, 7202), "bt": (2042, 7202), "nuct": (16384, 32768)}
        report["known_shape"] = len(data) in known_sizes.get(kind, ())
        report["shape_note"] = "Observed words only; table version/data domain/parameters remain unconfirmed"
        report["success"] = True
        print(f"{table_id}: {len(data)} B, {len(data)//2} items, SHA-256={report['sha256']}")
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        report["error"] = str(error)
        print(f"{table_id}: FAILED: {error}")
    finally:
        if transaction is not None:
            try:
                final = device.json("status")
                if final["transaction"] == transaction and final.get("dvp_paused"):
                    device.json("abort", str(transaction))
                    deadline = time.monotonic()+8
                    while time.monotonic() < deadline:
                        final = device.json("status")
                        if final["transaction"] != transaction or not final.get("dvp_paused"):
                            break
                        time.sleep(0.1)
                report["final_status"] = final
                if final["transaction"] == transaction and final.get("dvp_paused"):
                    report["cleanup_error"] = "device remains paused; inspect status"
            except (OSError, ValueError, RuntimeError, KeyError) as error:
                report["cleanup_error"] = str(error)
        if "cleanup_error" in report:
            report["success"] = False
            print(f"{table_id}: CLEANUP FAILED: {report['cleanup_error']}")
        with (output/f"{table_id}.json").open("x", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
    return report["success"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--table", choices=IDS, default="nuct-high")
    parser.add_argument("--all", action="store_true", help="read eight IDs; first verify this module's NUC-T transaction")
    parser.add_argument("--expect-wn2256-nuct", action="store_true")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1]/"out/radiometry/mini2-uart")
    args = parser.parse_args()
    if args.expect_wn2256_nuct and args.table != "nuct-high" and not args.all:
        parser.error("known proof is only valid for nuct-high")
    output = args.output / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True, exist_ok=False)
    print(f"Output: {output}")
    device = Device("http://192.168.17.1")
    success = True
    for table_id in IDS if args.all else (args.table,):
        proof = args.expect_wn2256_nuct and table_id == "nuct-high"
        ok = read_one(device, table_id, output, proof)
        success = success and ok
        if not ok and (proof or (args.all and table_id == "nuct-high")):
            print("Stopped: first NUC-T verification failed; no further table requests sent")
            break
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
