#!/usr/bin/env python3
"""Offline contract smoke for the 640 experimental-model save boundary.

This uses only temporary Y16 fixtures and fake HTTP/storage functions.  It
never opens a device URL or changes a physical calibration slot.
"""
from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import blackbody_pair_smoke as fixture
import t640_calibration_apply as apply
from analyze_blackbody_pair import analyze_pair, load_capture
from calibration_capture_support import roi_stats
from calibration_storage_client import HEADER, crc, decode_packet


IDENTITY = dict(pn="TIFSC640", sn="fixture-t640", fw_hex=b"01.00.01.03".hex())


def expect_error(callable_):
    try:
        callable_()
    except (ValueError, RuntimeError, KeyError, OSError):
        return
    raise AssertionError("invalid 640 save input was accepted")


def write_capture(root: Path, name: str, setpoint: float, raw: int) -> Path:
    directory = root / name
    directory.mkdir()
    width, height, x, y, size = 640, 512, 312, 248, 16
    frames = []
    for sequence, value in enumerate((raw, raw + 2), 1):
        data = struct.pack(">H", value) * (width * height)
        filename = f"frame-{sequence}.y16be"
        (directory / filename).write_bytes(data)
        frames.append(dict(path=filename, sequence=sequence, bytes=len(data),
                           sha256=hashlib.sha256(data).hexdigest(),
                           roi=roi_stats(data, width, height, x, y, size)))
    manifest = dict(
        format="t384-radiometry-capture-v1", setpoint_c=setpoint, gain="high",
        distance_m=0.01, emissivity=0.98, complete_frames=2, frames=frames,
        geometry=dict(width=width, height=height, frame_bytes=width * height * 2,
                      pixel_format="Y16BE"),
        roi=dict(x=x, y=y, size=size), diag_before={"source.pixel_format": "2"},
        diag_after={"stream.active": "0"},
        module_states=dict(before=fixture.write_snapshot(directory / "module-before"),
                           after=fixture.write_snapshot(directory / "module-after")),
        original_evidence=dict(identity=IDENTITY, files={}, algorithm_rules_verified=False),
    )
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return directory


def write_report(root: Path, validation: bool = False, invalid_validation: bool = False) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    low = write_capture(root, "low", 0.0, 40000)
    high = write_capture(root, "high", 50.0, 45000)
    validation_paths = []
    if validation:
        validation_paths = [write_capture(root, "validation", 25.0,
                                          47000 if invalid_validation else 42500)]
    captures = [load_capture(path, width=640, height=512) for path in validation_paths]
    report = analyze_pair(load_capture(low, width=640, height=512),
                          load_capture(high, width=640, height=512), captures,
                          0.5 if validation else None, apply.REPORT_MODEL)
    report["profile"] = "640x512"
    report_path = root / "report.json"
    report_path.write_text(json.dumps(report), encoding="utf-8")
    return report_path


class FakeStorage:
    def __init__(self, packet: bytes):
        self.packet = packet
        self.restores = 0
        self.events = []
        self.status = dict(slot0_addr=0x50000, slot1_addr=0x52000, slot_size=0x1000,
                           has_valid_slot=True, generation=0,
                           runtime_model_available=True, runtime_model="old-model",
                           runtime_zero_c_x100=0, runtime_counts_per_c_x100=1)

    def update_runtime(self, packet: bytes) -> None:
        manifest, payload = decode_packet(packet)
        low_c, _, low_raw, _, counts_x1000 = struct.unpack("<HHIIi", payload)
        counts_x100 = (counts_x1000 + 5) // 10
        self.status.update(generation=manifest["generation"], runtime_model=manifest["model"],
                           runtime_zero_c_x100=low_raw * 100 - low_c * counts_x100,
                           runtime_counts_per_c_x100=counts_x100)

    def readback(self, _base: str):
        self.events.append("readback")
        return decode_packet(self.packet)[0], self.packet

    def restore(self, _base: str, packet: bytes):
        self.events.append("restore")
        self.restores += 1
        self.packet = packet
        self.update_runtime(packet)
        return decode_packet(packet)[0]


@contextmanager
def fake_device(storage: FakeStorage, diag: dict):
    saved = (apply.Device, apply.fetch_diag, apply.storage_status,
             apply.readback, apply.restore)
    apply.Device = lambda _base: None
    apply.fetch_diag = lambda _url: dict(diag)
    apply.storage_status = lambda _base: dict(storage.status)
    apply.readback = storage.readback
    apply.restore = storage.restore
    try:
        yield
    finally:
        (apply.Device, apply.fetch_diag, apply.storage_status,
         apply.readback, apply.restore) = saved


def good_diag() -> dict:
    return dict(**{"dvp.expected_width": "640", "dvp.expected_height": "512",
                   "source.pixel_format": "2", "source.stream_ready": "1",
                   "stream.active": "0", "mini2.pn": IDENTITY["pn"],
                   "mini2.sn": IDENTITY["sn"],
                   "mini2.firmware_version": "01.00.01.03"})


def main() -> int:
    original_identity = fixture.IDENTITY
    fixture.IDENTITY = IDENTITY
    try:
        with tempfile.TemporaryDirectory(prefix="t384-t640-apply-") as temp:
            root = Path(temp)
            report_path = write_report(root)
            recomputed = apply.reload_report(report_path)
            packet = apply.model_packet(recomputed)
            manifest, payload = decode_packet(packet)
            assert HEADER.size == 112 and len(payload) == struct.calcsize("<HHIIi") == 16
            assert manifest["profile"] == "640x512" and manifest["payload_crc32"] == crc(payload)

            storage = FakeStorage(packet)
            storage.update_runtime(packet)
            with fake_device(storage, good_diag()):
                saved = apply.apply_report("http://fake", report_path)
                assert saved["applied"] and saved["calibration_id"] == decode_packet(storage.packet)[0]["calibration_id"]
                assert Path(saved["result_path"]).is_file() and storage.restores == 1
                assert storage.events.index("readback") < storage.events.index("restore")
                verified = apply.verify_saved_report("http://fake", report_path)
                assert verified["verification_only"] and storage.restores == 1

            for mutation in ("identity", "not_y16", "active_stream", "wrong_slots", "missing_runtime"):
                bad_storage = FakeStorage(packet)
                bad_storage.update_runtime(packet)
                diag = good_diag()
                if mutation == "identity":
                    diag["mini2.pn"] = "other-module"
                elif mutation == "not_y16":
                    diag["source.pixel_format"] = "3"
                elif mutation == "active_stream":
                    diag["stream.active"] = "1"
                elif mutation == "wrong_slots":
                    bad_storage.status["slot1_addr"] = 0x54000
                else:
                    del bad_storage.status["runtime_model_available"]
                with fake_device(bad_storage, diag):
                    expect_error(lambda: apply.apply_report("http://fake", report_path))
                assert bad_storage.restores == 0

            readback_bad = FakeStorage(packet)
            readback_bad.update_runtime(packet)
            original_restore = readback_bad.restore
            def corrupt_restore(base: str, candidate: bytes):
                result = original_restore(base, candidate)
                readback_bad.packet = readback_bad.packet[:-1] + bytes([readback_bad.packet[-1] ^ 1])
                return result
            readback_bad.restore = corrupt_restore
            with fake_device(readback_bad, good_diag()):
                expect_error(lambda: apply.apply_report("http://fake", report_path))
            assert readback_bad.restores == 1

            runtime_bad = FakeStorage(packet)
            runtime_bad.update_runtime(packet)
            original_runtime_restore = runtime_bad.restore
            def stale_runtime_restore(base: str, candidate: bytes):
                result = original_runtime_restore(base, candidate)
                runtime_bad.status["runtime_counts_per_c_x100"] += 1
                return result
            runtime_bad.restore = stale_runtime_restore
            with fake_device(runtime_bad, good_diag()):
                expect_error(lambda: apply.apply_report("http://fake", report_path))
            assert runtime_bad.restores == 1

            failed_validation = write_report(root / "failed-validation", validation=True,
                                             invalid_validation=True)
            validation_storage = FakeStorage(packet)
            with fake_device(validation_storage, good_diag()):
                expect_error(lambda: apply.apply_report("http://fake", failed_validation))
            assert validation_storage.restores == 0

            report_384 = json.loads(report_path.read_text(encoding="utf-8"))
            report_384["profile"] = "384x288"
            rejected_384 = root / "report-384.json"
            rejected_384.write_text(json.dumps(report_384), encoding="utf-8")
            expect_error(lambda: apply.reload_report(rejected_384))

            fake_picture = apply.reload_report(report_path)
            fake_picture["low"]["geometry"]["pixel_format"] = "UYVY"
            expect_error(lambda: apply.model_packet(fake_picture))

            state_file = root / "low" / "module-before" / "cal-state.bin"
            original_state = state_file.read_bytes()
            state_file.write_bytes(b"\0" * len(original_state))
            expect_error(lambda: apply.reload_report(report_path))
            state_file.write_bytes(original_state)
            missing = root / "low" / "frame-1.y16be"
            missing.unlink()
            expect_error(lambda: apply.reload_report(report_path))
    finally:
        fixture.IDENTITY = original_identity
    print("T640 offline apply/verify/recompute/identity/runtime boundary smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
