"""Shared gates for calibration acquisition and offline analysis, no fitting."""
from __future__ import annotations
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path

from read_mini2_module_files import Device, parse_state_snapshot, read_one

ROOT=Path(__file__).resolve().parents[1]
TABLE_EVIDENCE=ROOT/"out/radiometry/mini2-uart/20260918T053852.431973Z"
PARAMETER_EVIDENCE=ROOT/"out/radiometry/mini2-uart/20260918T055634.948076Z"


def identity(status: dict) -> dict:
    result={k:status[k] for k in ("pn","sn","fw_hex")}
    if not result["pn"] or not result["sn"] or len(bytes.fromhex(result["fw_hex"]))!=11:
        raise ValueError("missing/invalid module identity")
    return result


def load_evidence(tables: Path, parameters: Path) -> dict:
    records={}; expected_identity=None
    for kind, sizes in (("kt",(2042,7202)),("bt",(2042,7202)),("nuct",(16384,32768)),("tpd",(6,))):
        for gain in ("high","low"):
            name=f"{kind}-{gain}"
            directory=parameters if kind=="tpd" else tables
            report=json.loads((directory/f"{name}.json").read_text(encoding="utf-8"))
            data=(directory/f"{name}.bin").read_bytes()
            status=report["status"]
            actual_identity=identity(status)
            if expected_identity is None: expected_identity=actual_identity
            if (report.get("success") is not True or report.get("transport_verified") is not True or
                report.get("id")!=name or status.get("id")!=name or status.get("state")!="ready" or
                status.get("identity_verified") is not True or status.get("error")!=0 or
                status.get("cleanup_failed") is not False or actual_identity!=expected_identity or
                len(data) not in sizes or len(data)!=report["bytes"] or len(data)!=status["length"] or
                len(data)!=status["received"] or zlib.crc32(data)!=status["crc32"] or
                zlib.crc32(data)!=report["crc32"] or hashlib.sha256(data).hexdigest()!=report["sha256"]):
                raise ValueError(f"{name}: module evidence failed identity/shape/CRC/SHA gates")
            records[name]=dict(bytes=len(data),sha256=report["sha256"],crc32=report["crc32"])
            if kind=="tpd":
                k,b,a=struct.unpack("<hhH",data)
                records[name]["parameters"]=dict(ktemp=k,btemp=b,address_ca=a)
    for gain in ("high","low"):
        if records[f"kt-{gain}"]["bytes"]!=records[f"bt-{gain}"]["bytes"]:
            raise ValueError("KT/BT lengths differ")
    return dict(identity=expected_identity,files=records,algorithm_rules_verified=False)


def query_snapshot(device: Device, directory: Path) -> dict:
    directory.mkdir(parents=True,exist_ok=False)
    if not read_one(device,"cal-state",directory,False):
        report=json.loads((directory/"cal-state.json").read_text(encoding="utf-8"))
        raise ValueError(f"live calibration state query failed at {report.get('stage','unknown')}: "
                         f"{report.get('error',report.get('cleanup_error'))}; record: {directory/'cal-state.json'}")
    return load_snapshot(directory)


def load_snapshot(directory: Path) -> dict:
    report=json.loads((directory/"cal-state.json").read_text(encoding="utf-8"))
    data=(directory/"cal-state.bin").read_bytes()
    status=report["status"]
    snapshot=parse_state_snapshot(data)
    if (report.get("success") is not True or report.get("transport_verified") is not True or
        status.get("state")!="ready" or status.get("id")!="cal-state" or
        status.get("identity_verified") is not True or status.get("error")!=0 or
        status.get("length")!=8 or status.get("received")!=8 or
        report.get("bytes")!=8 or report.get("crc32")!=zlib.crc32(data) or
        status.get("crc32")!=zlib.crc32(data) or report.get("sha256")!=hashlib.sha256(data).hexdigest()):
        raise ValueError("saved state snapshot failed identity/CRC/SHA gates")
    if (report["final_status"].get("dvp_paused") is not False or
        report["final_status"].get("cleanup_failed") is not False):
        raise ValueError("state query left acquisition paused or cleanup uncertain")
    return dict(identity=identity(report["status"]),snapshot=snapshot,
                started_unix=report["started_unix"],finished_unix=report["finished_unix"],
                transaction=report["status"]["transaction"])


def validate_boundary_states(before: dict, after: dict, evidence: dict, gain: str | None=None) -> tuple[str, bool]:
    if before["identity"]!=after["identity"] or before["identity"]!=evidence["identity"]:
        raise ValueError("module PN/SN/FW changed or differs from original table evidence")
    a,b=before["snapshot"],after["snapshot"]
    if a["gain"]=="unknown":
        # WN2384's basic_gain_get does not respond with a clean status; the
        # gain was set and ACKed before capture but could not be read back.
        # Do not fail the acquisition on this, but mark the boundary unverified.
        if b["gain"]!="unknown":
            raise ValueError("gain was unknown before capture but reported differently after")
        gain_used="unknown"
        gain_verified=False
    else:
        if a["gain"] not in ("high","low") or a["gain"]!=b["gain"] or (gain and gain!=a["gain"]):
            raise ValueError("gain changed, is unsupported, or differs from requested gain")
        gain_used=a["gain"]
        gain_verified=True
    if a["shutter_status"]!=1 or b["shutter_status"]!=1:
        raise ValueError("shutter is closed or unknown at the acquisition boundary")
    if a["auto_ffc_enabled"]!=b["auto_ffc_enabled"]:
        raise ValueError("auto FFC configuration changed during acquisition")
    return gain_used, gain_verified


def roi_stats(data: bytes,width: int,height: int,x: int,y: int,size: int) -> dict:
    if len(data)!=width*height*2 or size<=0 or x<0 or y<0 or x+size>width or y+size>height:
        raise ValueError("incomplete Y16BE frame or ROI outside image")
    values=[int.from_bytes(data[(row*width+col)*2:(row*width+col)*2+2],"big")
            for row in range(y,y+size) for col in range(x,x+size)]
    mean=sum(values)/len(values)
    return dict(x=x,y=y,width=size,height=size,sample_count=len(values),mean_y16=mean,
                stddev_y16=math.sqrt(sum((v-mean)**2 for v in values)/len(values)),
                minimum_y16=min(values),maximum_y16=max(values))


def read_capture(directory: Path,width: int | None=None,height: int | None=None,
                 x: int | None=None,y: int | None=None,size: int | None=None,
                 manifest_data: dict | None=None) -> tuple[dict,list[float]]:
    manifest=manifest_data if manifest_data is not None else json.loads((directory/"manifest.json").read_text(encoding="utf-8"))
    if manifest.get("format")!="t384-radiometry-capture-v1": raise ValueError("unknown capture format")
    geometry=manifest["geometry"]; w,h=geometry["width"],geometry["height"]
    if (w,h) not in ((256,192),(384,288)) or geometry["frame_bytes"]!=w*h*2:
        raise ValueError("unsupported capture geometry")
    if (width is not None and width!=w) or (height is not None and height!=h):
        raise ValueError("requested geometry differs from capture")
    roi=manifest["roi"]; rx,ry,rs=roi["x"],roi["y"],roi["size"]
    if (x is not None and x!=rx) or (y is not None and y!=ry) or (size is not None and size!=rs):
        raise ValueError("requested ROI differs from capture")
    states=manifest["module_states"]; evidence=manifest["original_evidence"]
    for boundary in ("before","after"):
        if states[boundary]!=load_snapshot(directory/f"module-{boundary}"):
            raise ValueError("manifest state differs from saved query evidence")
    validate_boundary_states(states["before"],states["after"],evidence,manifest["gain"])
    if not manifest.get("diag_before") or not manifest.get("diag_after"):
        raise ValueError("capture is missing diagnostic snapshots")
    means=[]; sequences=set(); paths=set()
    for frame in manifest["frames"]:
        name=frame["path"]
        if Path(name).name!=name or name in paths or frame["sequence"] in sequences:
            raise ValueError("frame path or sequence is invalid/duplicated")
        paths.add(name); sequences.add(frame["sequence"])
        data=(directory/name).read_bytes()
        if len(data)!=w*h*2 or frame["bytes"]!=len(data) or hashlib.sha256(data).hexdigest()!=frame["sha256"]:
            raise ValueError("frame length/SHA mismatch")
        stats=roi_stats(data,w,h,rx,ry,rs)
        if frame["roi"]!=stats: raise ValueError("cached ROI differs from original frame")
        means.append(stats["mean_y16"])
    if not means or len(means)!=manifest["complete_frames"]:
        raise ValueError("manifest count differs from complete frames")
    return manifest,means
