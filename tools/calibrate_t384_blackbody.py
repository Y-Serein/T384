#!/usr/bin/env python3
"""Guided 0/50 C blackbody acquisition and experimental two-point fit.

Boundary state and original table evidence are saved. No MCU/MINI2 calibration
writes or automatic temperature activation; this is an experimental host fit.
"""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import urllib.request
import zlib

from calibration_capture_support import ROOT,TABLE_EVIDENCE,PARAMETER_EVIDENCE,load_evidence
from capture_radiometry_calibration import capture,fetch_diag
from analyze_blackbody_pair import load_capture,analyze_pair
from calibration_storage_client import encode_packet,restore,MODEL


def apply_model(base_url: str, report: dict, evidence: dict) -> dict:
    """Build a t384-empirical-2point-v1 packet from this fit and store it.

    Stages and commits into the device's quarantined v1 calibration slot. This
    never enables OEM output; it only persists the engineering model so the
    device web console can display it against the matching module identity.
    """
    low = report["low"]
    high = report["high"]
    delta_c = high["setpoint_c"] - low["setpoint_c"]
    if delta_c <= 0:
        raise ValueError("invalid setpoint range for calibration apply")
    zero_raw = int(round(low["roi_mean_y16"]))
    hot_raw = int(round(high["roi_mean_y16"]))
    counts_per_c_x1000 = int(round((hot_raw - zero_raw) / delta_c * 1000.0))
    payload = struct.pack(
        "<HHIIi",
        int(round(low["setpoint_c"])),
        int(round(high["setpoint_c"])),
        zero_raw,
        hot_raw,
        counts_per_c_x1000,
    )
    ident = evidence["identity"]
    ident_str = f"{ident['pn']}:{ident['sn']}:{ident['fw_hex']}"
    manifest = dict(
        schema=1,
        generation=0,  # firmware assigns the next generation on commit
        payload_len=len(payload),
        payload_crc32=zlib.crc32(payload) & 0xFFFFFFFF,
        calibration_id=int(datetime.now(timezone.utc).timestamp()) & 0xFFFFFFFF,
        model=MODEL,
        profile="384x288",
        identity=hashlib.sha256(ident_str.encode("ascii")).hexdigest(),
        gain=0xFF,  # WN2384 does not expose a readable gain: mark unknown
    )
    base = base_url.rstrip("/") + "/api/v1/calibration/v1/"
    return restore(base, encode_packet(manifest, payload))


def run(args: argparse.Namespace) -> int:
    evidence=load_evidence(args.table_evidence,args.parameter_evidence)
    if getattr(args, "apply_from", None):
        report=json.loads(args.apply_from.read_text(encoding="utf-8"))
        applied=apply_model(args.url.rsplit("/",1)[0], report, report["low"]["original_evidence"])
        print(f"标定包已从报告写入设备存储并校验通过：calibration_id="
              f"{applied.get('calibration_id')} gain="
              + ("unknown" if applied.get("gain") == 0xFF else str(applied.get("gain"))))
        return 0
    print("原表与两档参数已核验。默认采集0°C、50°C，距离0.01 m，发射率0.98；不写机芯。")
    numeric=(args.low_c,args.high_c,args.distance_m,args.emissivity,args.timeout)
    if args.validation_c is not None: numeric+= (args.validation_c,)
    if args.max_error_c is not None: numeric+= (args.max_error_c,)
    if not all(math.isfinite(x) for x in numeric): raise ValueError("nonfinite calibration parameters")
    if any(v is not None and not math.isfinite(v) for v in (args.ta_c,args.tu_c,args.humidity)):
        raise ValueError("nonfinite environmental parameter")
    if args.low_c>=args.high_c or (args.validation_c is not None and not args.low_c<args.validation_c<args.high_c):
        raise ValueError("invalid fitting/optional validation temperatures")
    if args.distance_m<=0 or not 0<args.emissivity<=1 or (args.max_error_c is not None and args.max_error_c<=0) or args.timeout<=0:
        raise ValueError("invalid distance/emissivity/error/timeout")
    if not 1<=args.frames<=120 or args.roi_size<=0: raise ValueError("invalid frame count/ROI size")
    output=args.output or ROOT/"out/radiometry/blackbody"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True,exist_ok=False)
    points=[("low",args.low_c),("high",args.high_c)]
    if args.validation_c is not None: points.append(("validation",args.validation_c))
    plan=dict(format="t384-blackbody-session-v1",points=points,frames_per_point=args.frames,
              distance_m=args.distance_m,emissivity=args.emissivity,max_error_c=args.max_error_c,
              original_evidence=evidence,automatic_calibration_writes=False,
              model="experimental-blackbody-2point-v1",oem_radiometry_ready=False)
    (output/"plan.json").write_text(json.dumps(plan,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(f"本次资料目录：{output}")
    if args.prepare_only: return 0
    print("请关闭 http://192.168.17.1/ 成像页及全部抓流客户端。固定距离和ROI，黑体辐射面须覆盖ROI。")
    diag_url=args.diag_url or args.url.rsplit("/",1)[0]+"/diag"
    print("先检查设备HTTP连接；此时尚未采集黑体帧。",flush=True)
    try:
        diag=fetch_diag(diag_url)
        if diag.get("source.pixel_format")!="2": raise RuntimeError("设备当前不是TPD/Y16输出")
        if diag.get("stream.active")!="0": raise RuntimeError("设备仍有活动流，请关闭成像页及抓流程序")
        preflight=dict(success=True,diag=diag)
    except (OSError,RuntimeError,ValueError) as error:
        preflight=dict(success=False,error=str(error),stage="http-preflight",url=diag_url)
        (output/"preflight.json").write_text(json.dumps(preflight,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        raise RuntimeError(f"设备HTTP预检失败，未开始黑体采集：{error}；记录：{output/'preflight.json'}") from error
    (output/"preflight.json").write_text(json.dumps(preflight,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    gain=args.gain
    for name,temp in points:
        input(f"将黑体设为 {temp:g} °C，确认稳定且覆盖ROI后按回车开始 {name} 采集：")
        point=argparse.Namespace(**vars(args))
        point.output=output/name; point.setpoint_c=temp; point.gain=gain
        capture(point)
        info=load_capture(point.output)
        gain=info["gain"]
        print(f"{name}：{info['frame_count']}完整帧，实际{gain}增益，ROI均值{info['roi_mean_y16']:.3f}")
    validation=[load_capture(output/"validation")] if args.validation_c is not None else []
    report=analyze_pair(load_capture(output/"low"),load_capture(output/"high"),validation,args.max_error_c)
    (output/"report.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    if report["validation"]:
        check=report["validation"][0]
        print(f"独立点ROI平均误差 {check['mean_error_c']:.3f} °C；最大帧ROI误差 {check['max_abs_frame_roi_error_c']:.3f} °C")
    print(f"报告：{output/'report.json'}")
    if getattr(args, "apply", False):
        applied = apply_model(args.url.rsplit("/", 1)[0], report, evidence)
        print(f"标定包已写入设备存储并校验通过：calibration_id={applied.get('calibration_id')} "
              f"gain=" + ("unknown" if applied.get("gain") == 0xFF else str(applied.get("gain"))))
    if not validation:
        print("0°C/50°C采集与实验拟合完成；后续由你验证，报告未标记验证通过，未启用正式OEM温度。")
        return 0
    print("实验验证"+("通过" if report["experimental_validation_passed"] else "未通过/未设置误差指标")+"；未启用正式OEM温度。")
    return 0 if args.max_error_c is None or report["experimental_validation_passed"] else 1


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url",default="http://192.168.17.1/raw16.stream")
    parser.add_argument("--diag-url")
    parser.add_argument("--output",type=Path)
    for field,default in (("low-c",0.0),("high-c",50.0),("distance-m",0.01),("emissivity",0.98)):
        parser.add_argument("--"+field,type=float,default=default)
    for field in ("validation-c","max-error-c","ta-c","tu-c","humidity"):
        parser.add_argument("--"+field,type=float)
    parser.add_argument("--frames",type=int,default=30)
    parser.add_argument("--gain",choices=("current","high","low"),default="current")
    parser.add_argument("--timeout",type=float,default=10.0)
    parser.add_argument("--roi-x",type=int)
    parser.add_argument("--roi-y",type=int)
    parser.add_argument("--roi-size",type=int,default=16)
    parser.add_argument("--table-evidence",type=Path,default=TABLE_EVIDENCE)
    parser.add_argument("--parameter-evidence",type=Path,default=PARAMETER_EVIDENCE)
    parser.add_argument("--prepare-only",action="store_true",help="validate evidence and save a plan; no hardware access")
    parser.add_argument("--apply",action="store_true",
                        help="after fitting, stage+commit the model into the device calibration slot")
    parser.add_argument("--apply-from",type=Path,
                        help="apply an existing report.json to the device storage without collecting")
    return run(parser.parse_args())


if __name__=="__main__":
    try: raise SystemExit(main())
    except KeyboardInterrupt: raise SystemExit("已取消；未写机芯，已完成点的资料保留。") from None
    except (OSError,RuntimeError,ValueError,KeyError) as error:
        raise SystemExit(f"标定流程停止：{error}") from None
