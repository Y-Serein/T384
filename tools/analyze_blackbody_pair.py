#!/usr/bin/env python3
"""Analyze two Windows RAW16 blackbody captures.

This produces an engineering two-point response report only.  It never labels
the result as OEM radiometric calibration and never writes firmware or tables.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from calibration_capture_support import read_capture


def load_capture(directory: Path, width: int | None=None, height: int | None=None,
                 x: int | None=None, y: int | None=None, size: int | None=None) -> dict:
    manifest,means=read_capture(directory,width,height,x,y,size)
    if not math.isfinite(manifest["setpoint_c"]): raise ValueError("invalid blackbody setpoint")
    return {
        "directory":str(directory),"setpoint_c":manifest["setpoint_c"],
        "gain":manifest["gain"],"distance_m":manifest["distance_m"],
        "emissivity":manifest["emissivity"],"ta_c":manifest.get("ta_c"),
        "tu_c":manifest.get("tu_c"),"humidity":manifest.get("humidity"),
        "geometry":manifest["geometry"],"roi":manifest["roi"],
        "original_evidence":manifest["original_evidence"],
        "module_states":manifest["module_states"],"frame_count":len(means),
        "roi_mean_y16":statistics.fmean(means),
        "roi_stddev_between_frames":statistics.pstdev(means),
        "roi_min_y16":min(means),"roi_max_y16":max(means),
    }


def matching_conditions(a: dict,b: dict) -> None:
    for key in ("gain","distance_m","emissivity","geometry","roi","original_evidence","ta_c","tu_c","humidity"):
        if a[key]!=b[key]: raise ValueError(f"captures differ in {key}")


def analyze_pair(low: dict,high: dict,validation: list[dict],max_error_c: float | None=None) -> dict:
    matching_conditions(low,high)
    if high["setpoint_c"]<=low["setpoint_c"]: raise ValueError("high setpoint must exceed low setpoint")
    dx=high["roi_mean_y16"]-low["roi_mean_y16"]
    if dx<=0: raise ValueError("Y16 response is flat or decreases with temperature")
    if dx<=6*max(low["roi_stddev_between_frames"],high["roi_stddev_between_frames"]):
        raise ValueError("two-point response is too small relative to frame variation; stabilize and recapture")
    if max_error_c is not None and (not math.isfinite(max_error_c) or max_error_c<=0):
        raise ValueError("max error must be finite and positive")
    slope=(high["setpoint_c"]-low["setpoint_c"])/dx
    intercept=low["setpoint_c"]-slope*low["roi_mean_y16"]
    checks=[]
    for point in validation:
        matching_conditions(low,point)
        if point["setpoint_c"] in (low["setpoint_c"],high["setpoint_c"]):
            raise ValueError("validation point must differ from both fitting setpoints")
        estimated=slope*point["roi_mean_y16"]+intercept
        max_error=max(abs(slope*point[key]+intercept-point["setpoint_c"])
                      for key in ("roi_min_y16","roi_max_y16"))
        checks.append(dict(capture=point,estimated_roi_c=estimated,
                           mean_error_c=estimated-point["setpoint_c"],
                           max_abs_frame_roi_error_c=max_error,
                           roi_stddev_c=slope*point["roi_stddev_between_frames"],
                           extrapolated=not low["setpoint_c"]<point["setpoint_c"]<high["setpoint_c"],
                           passed=None if max_error_c is None else max_error<=max_error_c))
    validated=bool(checks) and max_error_c is not None and all(p["passed"] for p in checks)
    return {
        "format":"t384-engineering-blackbody-pair-v1",
        "status":"engineering-only-not-oem-radiometric",
        "low":low,"high":high,"roi":low["roi"],"validation":checks,
        "experimental_validation_passed":validated,"max_error_c":max_error_c,
        "oem_radiometry_ready":False,"applied":False,
        "linear_model":{"temperature_c = slope * y16 + intercept":{"slope":slope,"intercept":intercept}},
        "candidate":dict(model="experimental-blackbody-2point-v1",slope_c_per_y16=slope,
                         intercept_c=intercept,valid_setpoint_range_c=[low["setpoint_c"],high["setpoint_c"]],
                         gain=low["gain"],geometry=low["geometry"],original_evidence=low["original_evidence"]),
        "notes":["All frame files, lengths, SHA and ROI were recomputed and verified.",
                 "State queries are boundary snapshots, not per-frame gain/FFC/Vtemp/epoch.",
                 "Experimental ROI validation does not prove OEM or full-image accuracy.",
                 "Candidate is not automatically applied or written into MCU/MINI2."],
    }


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("low_capture",type=Path)
    parser.add_argument("high_capture",type=Path)
    parser.add_argument("--validation",type=Path,action="append",default=[])
    parser.add_argument("--max-error-c",type=float)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--width",type=int)
    parser.add_argument("--height",type=int)
    parser.add_argument("--roi-x",type=int)
    parser.add_argument("--roi-y",type=int)
    parser.add_argument("--roi-size",type=int)
    args=parser.parse_args()
    def load(path): return load_capture(path,args.width,args.height,args.roi_x,args.roi_y,args.roi_size)
    report=analyze_pair(load(args.low_capture),load(args.high_capture),[load(p) for p in args.validation],args.max_error_c)
    with args.output.open("x",encoding="utf-8") as output:
        output.write(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ("status","experimental_validation_passed","oem_radiometry_ready")},ensure_ascii=False))
    return 0 if not args.validation or args.max_error_c is None or report["experimental_validation_passed"] else 1


if __name__=="__main__":
    try: raise SystemExit(main())
    except (OSError,ValueError,KeyError) as error:
        raise SystemExit(f"Blackbody analysis failed: {error}") from None
