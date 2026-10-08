"""Persist and verify the 640 experimental two-point model, never OEM tables.

The existing 384 entry point is deliberately unchanged. Only apply_report
writes MCU Flash; verify_saved_report performs GETs only.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import time

from analyze_blackbody_pair import analyze_pair, load_capture
from calibration_storage_client import (
    BASE, MODEL, crc, decode_packet, encode_packet, readback, restore, storage_status,
)
from capture_radiometry_calibration import fetch_diag
from read_mini2_module_files import Device

REPORT_MODEL = "experimental-blackbody-2point-640-v1"


def reload_report(path: Path) -> dict:
    """Recompute the fit from the saved frames/state, not editable ROI caches."""
    saved = json.loads(path.read_text(encoding="utf-8"))
    if (saved.get("format") != "t384-engineering-blackbody-pair-v1" or
            saved.get("profile") != "640x512" or
            saved.get("candidate", {}).get("model") != REPORT_MODEL):
        raise ValueError("report is not a 640 experimental two-point capture")
    points = [load_capture(Path(saved[name]["directory"]), width=640, height=512)
              for name in ("low", "high")]
    validation = [load_capture(Path(item["capture"]["directory"]), width=640, height=512)
                  for item in saved.get("validation", [])]
    report = analyze_pair(*points, validation, saved.get("max_error_c"), REPORT_MODEL)
    if validation and not report["experimental_validation_passed"]:
        raise ValueError("independent validation did not pass; model was not written")
    report["profile"] = "640x512"
    return report


def model_packet(report: dict) -> bytes:
    low, high = report["low"], report["high"]
    for point in (low, high):
        geometry = point["geometry"]
        if (geometry["width"], geometry["height"], geometry["frame_bytes"],
                geometry["pixel_format"]) != (640, 512, 655360, "Y16BE"):
            raise ValueError("model input is not complete 640x512 Y16BE")
    temperatures = (low["setpoint_c"], high["setpoint_c"])
    if not all(math.isfinite(x) and x == int(x) and 0 <= x <= 65535 for x in temperatures):
        raise ValueError("the existing v1 storage format requires nonnegative integer calibration points (default 0/50 C)")
    low_c, high_c = map(int, temperatures)
    if high_c <= low_c:
        raise ValueError("invalid calibration point order")
    raw_values = (low["roi_mean_y16"], high["roi_mean_y16"])
    if not all(math.isfinite(x) and 0 <= x <= 65535 for x in raw_values):
        raise ValueError("invalid Y16 calibration values")
    low_raw, high_raw = (int(round(x)) for x in raw_values)
    counts_x1000 = int(round((high_raw - low_raw) * 1000.0 / (high_c - low_c)))
    counts_x100 = (counts_x1000 + 5) // 10
    zero_x100 = low_raw * 100 - low_c * counts_x100
    if high_raw <= low_raw or not 0 < counts_x1000 <= 0x7FFFFFFF or counts_x100 <= 0:
        raise ValueError("two-point response cannot be represented by the stored model")
    if not 0 <= zero_x100 <= 6553500:
        raise ValueError("model zero point is outside the firmware v1 range")
    # The displayed model uses the same rounded integer scale as firmware.
    # An explicit independent error limit must also hold after quantization.
    limit = report.get("max_error_c")
    if report.get("validation") and limit is not None:
        for item in report["validation"]:
            point = item["capture"]
            error = max(abs((point[key] * 100 - zero_x100) / counts_x100 -
                            point["setpoint_c"])
                        for key in ("roi_min_y16", "roi_max_y16"))
            if error > limit:
                raise ValueError("stored integer model exceeds the independent error limit")
    identity = low["original_evidence"]["identity"]
    text = f"{identity['pn']}:{identity['sn']}:{identity['fw_hex']}"
    payload = struct.pack("<HHIIi", low_c, high_c, low_raw, high_raw, counts_x1000)
    manifest = dict(
        schema=1, generation=0, payload_len=len(payload), payload_crc32=crc(payload),
        calibration_id=(int(time.time()) & 0xFFFFFFFF) or 1,
        model=MODEL, profile="640x512",
        identity=hashlib.sha256(text.encode("ascii")).hexdigest(),
        gain={"high": 1, "low": 2, "unknown": 0xFF}[low["gain"]],
    )
    return encode_packet(manifest, payload)


def storage_preflight(base_url: str) -> tuple[str, dict]:
    base_url = base_url.rstrip("/")
    Device(base_url)  # Existing local-device URL allowlist; no request here.
    base = base_url + BASE
    status = storage_status(base)
    if (status.get("slot0_addr"), status.get("slot1_addr"), status.get("slot_size")) != (0x50000, 0x52000, 0x1000):
        raise ValueError("firmware does not use the shared calibration A/B layout")
    if "runtime_model_available" not in status:
        raise ValueError("firmware lacks the 640 model-application status; no model was written")
    return base, status


def live_preflight(base_url: str, report: dict) -> tuple[str, dict]:
    base_url = base_url.rstrip("/")
    Device(base_url)
    diag = fetch_diag(base_url + "/diag")
    if (diag.get("dvp.expected_width"), diag.get("dvp.expected_height"),
            diag.get("source.pixel_format"), diag.get("source.stream_ready"),
            diag.get("stream.active")) != ("640", "512", "2", "1", "0"):
        raise ValueError("close http://192.168.17.1/ and other clients; the device must be ready in 640 Y16 mode")
    captured = report["low"]["original_evidence"]["identity"]
    live = dict(pn=diag.get("mini2.pn", ""), sn=diag.get("mini2.sn", ""),
                fw_hex=diag.get("mini2.firmware_version", "").encode("ascii").hex())
    if not all(live.values()) or live != captured:
        raise ValueError("current module identity differs from the capture; no calibration write allowed")
    return storage_preflight(base_url)


def verify_runtime(base: str, expected_packet: bytes) -> dict:
    expected, payload = decode_packet(expected_packet)
    actual, packet = readback(base)
    for key in ("schema", "model", "profile", "identity", "gain", "payload_len", "payload_crc32"):
        if actual[key] != expected[key]:
            raise ValueError(f"stored calibration {key} differs from this report")
    _, actual_payload = decode_packet(packet)
    if actual_payload != payload:
        raise ValueError("stored calibration parameters differ from this report")
    status = storage_status(base)
    if not status.get("has_valid_slot") or status.get("generation") != actual["generation"]:
        raise ValueError("active storage generation does not match readback")
    low_c, _, low_raw, _, counts_x1000 = struct.unpack("<HHIIi", payload)
    counts_x100 = (counts_x1000 + 5) // 10
    zero_x100 = low_raw * 100 - low_c * counts_x100
    if (status.get("runtime_model_available") is not True or
            status.get("runtime_model") != MODEL or
            status.get("runtime_zero_c_x100") != zero_x100 or
            status.get("runtime_counts_per_c_x100") != counts_x100):
        raise ValueError("stored packet is readable, but the runtime did not load the expected model")
    return dict(stored=True, applied=True, oem_radiometry_ready=False,
                calibration_id=actual["calibration_id"], generation=actual["generation"],
                packet_sha256=hashlib.sha256(packet).hexdigest(), storage_status=status,
                cold_boot_verified=False)


def apply_report(base_url: str, report_path: Path) -> dict:
    report_path = Path(report_path)
    report = reload_report(report_path)
    packet = model_packet(report)
    directory = report_path.parent / ("apply-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    directory.mkdir(exist_ok=False)
    packet_path = directory / "model.packet"
    packet_path.write_bytes(packet)
    result_path = directory / "result.json"
    result = dict(stored=False, applied=False, oem_radiometry_ready=False,
                  write_attempted=False, result_path=str(result_path), stage="preflight")
    try:
        base, status = live_preflight(base_url, report)
        if status.get("has_valid_slot"):
            _, backup = readback(base)
            (directory / "before.packet").write_bytes(backup)
        result["stage"] = "write-and-readback"
        result["write_attempted"] = True
        result["stored"] = None  # A lost reply cannot prove whether Flash changed.
        committed = restore(base, packet)
        result["stored"] = True
        result["stage"] = "verify-runtime"
        verified = verify_runtime(base, packet)
        if verified["calibration_id"] != committed["calibration_id"]:
            raise ValueError("calibration changed during runtime verification")
        result.update(verified, stage="complete")
        return result
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        result["error"] = str(error)
        raise RuntimeError(f"640 calibration stopped at {result['stage']}; see {result_path}. Verify saved data before retrying a write.") from error
    finally:
        result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def verify_saved_report(base_url: str, report_path: Path) -> dict:
    report = reload_report(Path(report_path))
    expected = model_packet(report)
    base, _ = live_preflight(base_url, report)
    result = verify_runtime(base, expected)
    result["verification_only"] = True
    result["note"] = "Readback and runtime model match. No write performed; physical power cycling is confirmed by the operator."
    return result
