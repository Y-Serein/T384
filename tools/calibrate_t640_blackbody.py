#!/usr/bin/env python3
"""Capture and analyze an engineering 640x512 TPD/Y16 blackbody session.

Default operation only captures and fits.  Explicit --apply stores the saved
experimental model through the matching device API; this never writes MINI2.
Picture/UYVY (including packed Picture) is rejected because 8-bit luma is not
an OEM radiometry input.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path

from calibration_capture_support import read_capture
from capture_radiometry_calibration import capture, fetch_diag
from analyze_blackbody_pair import load_capture, analyze_pair


MODEL = "experimental-blackbody-2point-640-v1"


def finite(value: float | None) -> bool:
    return value is None or math.isfinite(value)


def base_url(args: argparse.Namespace) -> str:
    return args.url.rsplit("/", 1)[0]


def print_save_result(action: str, result: dict) -> int:
    result_path = result.get("result_path")
    calibration_id = result.get("calibration_id")
    generation = result.get("generation")
    applied = result.get("applied")
    print(f"640 experimental model {action}: applied={applied}, "
          f"calibration_id={calibration_id}, generation={generation}")
    if result_path:
        print(f"Saved-model result: {result_path}")
    print("This is an experimental model load/save result, not OEM radiometry.")
    return 0 if applied else 1


def run(args: argparse.Namespace) -> int:
    if args.apply_from is not None:
        from t640_calibration_apply import apply_report
        return print_save_result("applied from report", apply_report(base_url(args), args.apply_from))
    if args.verify_saved is not None:
        from t640_calibration_apply import verify_saved_report
        return print_save_result("saved report verified", verify_saved_report(base_url(args), args.verify_saved))

    output = args.output or Path(__file__).resolve().parents[1] / "out" / "radiometry" / "blackbody" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True, exist_ok=False)
    diag_url = args.diag_url or base_url(args) + "/diag"

    try:
        diag = fetch_diag(diag_url)
        if diag.get("stream.active") != "0":
            raise RuntimeError("active RAW16 stream must be closed before 640 calibration")
        if (diag.get("source.pixel_format") != "2" or
                diag.get("dvp.expected_width") != "640" or
                diag.get("dvp.expected_height") != "512"):
            raise RuntimeError(
                "current device is not 640x512 TPD/Y16; Picture/UYVY is rejected "
                "and cannot be used as formal radiometry input"
            )
        if diag.get("source.stream_ready") != "1":
            raise RuntimeError("640 source is not stream-ready")
        if args.apply:
            from t640_calibration_apply import storage_preflight
            storage_preflight(base_url(args))
    except (OSError, ValueError, RuntimeError) as error:
        preflight = dict(success=False, error=str(error), stage="http-preflight", url=diag_url)
        (output / "preflight.json").write_text(
            json.dumps(preflight, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        raise RuntimeError(f"640 calibration preflight failed; no blackbody frames collected: {error}") from error

    points = [("low", args.low_c), ("high", args.high_c)]
    if args.validation_c is not None:
        points.append(("validation", args.validation_c))
    plan = dict(
        format="t384-blackbody-session-v1",
        profile="640x512",
        points=points,
        frames_per_point=args.frames,
        distance_m=args.distance_m,
        emissivity=args.emissivity,
        max_error_c=args.max_error_c,
        original_evidence=dict(
            identity=dict(
                pn=diag.get("mini2.pn", ""),
                sn=diag.get("mini2.sn", ""),
                fw_hex=diag.get("mini2.firmware_version", "").encode("ascii").hex(),
            ),
            files={},
            algorithm_rules_verified=False,
            source="live-identity-only-no-OEM-table-proof",
        ),
        automatic_calibration_writes=args.apply,
        model=MODEL,
        oem_radiometry_ready=False,
    )
    (output / "plan.json").write_text(
        json.dumps(plan, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (output / "preflight.json").write_text(
        json.dumps(dict(success=True, diag=diag), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    gain = args.gain
    for name, temperature in points:
        input(f"将 640 黑体设为 {temperature:g} °C，确认稳定并覆盖中心 ROI 后按回车开始 {name} 采集：")
        point = argparse.Namespace(**vars(args))
        point.output = output / name
        point.setpoint_c = temperature
        point.gain = gain
        point.diag_url = diag_url
        point.live_identity_only = True
        capture(point)
        capture_info = load_capture(point.output, width=640, height=512)
        gain = capture_info["gain"]
        print(f"{name}: {capture_info['frame_count']} complete frames, "
              f"gain={gain}, ROI mean={capture_info['roi_mean_y16']:.3f}")

    validation = [load_capture(output / "validation", width=640, height=512)] \
        if args.validation_c is not None else []
    report = analyze_pair(
        load_capture(output / "low", width=640, height=512),
        load_capture(output / "high", width=640, height=512),
        validation,
        args.max_error_c,
        MODEL,
    )
    report["profile"] = "640x512"
    report["oem_radiometry_ready"] = False
    report["applied"] = False
    report["notes"].append(
        "640 report is capture/fit evidence only; MINI2 is never written. "
        "A saved experimental model is created only by explicit --apply."
    )
    report["notes"].append("OEM KT/BT/NUC-T data domain, gain semantics and per-frame state binding remain unverified.")
    report_path = output / "report.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"640 report: {report_path}")
    validation_failed = (args.validation_c is not None and
                         args.max_error_c is not None and
                         not report["experimental_validation_passed"])
    if validation_failed:
        print("Independent experimental validation failed; saved-model application was not started.")
        return 1
    if not args.apply:
        print("This is engineering-only evidence; it was not applied to the device.")
        return 0

    from t640_calibration_apply import apply_report
    print("Applying the explicitly requested saved experimental model; MINI2 is not written.")
    return print_save_result("applied", apply_report(base_url(args), report_path))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.17.1/raw16.stream")
    parser.add_argument("--diag-url")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--low-c", type=float, default=0.0)
    parser.add_argument("--high-c", type=float, default=50.0)
    parser.add_argument("--validation-c", type=float)
    parser.add_argument("--max-error-c", type=float)
    parser.add_argument("--distance-m", type=float, default=0.01)
    parser.add_argument("--emissivity", type=float, default=0.98)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--gain", choices=("high", "low", "current"), default="current")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--humidity", type=float)
    parser.add_argument("--ta-c", type=float)
    parser.add_argument("--tu-c", type=float)
    parser.add_argument("--roi-x", type=int)
    parser.add_argument("--roi-y", type=int)
    parser.add_argument("--roi-size", type=int, default=16)
    saved_model_action = parser.add_mutually_exclusive_group()
    saved_model_action.add_argument("--apply", action="store_true",
                                    help="save and load this new experimental report after capture")
    saved_model_action.add_argument("--apply-from", type=Path, metavar="REPORT",
                                    help="apply an existing report without recapturing")
    saved_model_action.add_argument("--verify-saved", type=Path, metavar="REPORT",
                                    help="read back an existing saved report without writing")
    args = parser.parse_args()
    numeric = (args.low_c, args.high_c, args.distance_m, args.emissivity,
               args.frames, args.timeout)
    if not all(math.isfinite(value) for value in numeric) or args.low_c >= args.high_c:
        parser.error("invalid 640 calibration parameters")
    if args.validation_c is not None and not args.low_c < args.validation_c < args.high_c:
        parser.error("validation point must be between low and high setpoints")
    if args.distance_m <= 0 or not 0 < args.emissivity <= 1 or args.frames < 1 or args.frames > 120 or args.timeout <= 0:
        parser.error("invalid distance/emissivity/frames/timeout")
    if not all(finite(value) for value in (args.humidity, args.ta_c, args.tu_c, args.max_error_c)):
        parser.error("optional environmental/error values must be finite")
    if args.max_error_c is not None and args.max_error_c <= 0:
        parser.error("max error must be positive")
    try:
        raise SystemExit(run(args))
    except KeyboardInterrupt:
        raise SystemExit(
            "640 calibration interrupted; saved-model state may be unknown. "
            "Verify the report before retrying."
        ) from None
    except (OSError, KeyError, ValueError, RuntimeError) as error:
        suffix = (" Saved-model state may be unknown; verify before retrying."
                  if args.apply or args.apply_from is not None else "")
        raise SystemExit(f"640 calibration stopped: {error}.{suffix}") from None


if __name__ == "__main__":
    main()
