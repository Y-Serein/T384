#!/usr/bin/env python3
"""Real file/SHA/ROI/state gates and independent experimental validation."""
from __future__ import annotations
import copy
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from calibration_capture_support import roi_stats,read_capture,load_snapshot,load_evidence
from analyze_blackbody_pair import load_capture,analyze_pair

IDENTITY=dict(pn="WN2384",sn="fixture-sn",fw_hex=b"00.00.07.01".hex())


def write_snapshot(directory: Path,gain: int=1,shutter: int=1) -> dict:
    directory.mkdir(parents=True,exist_ok=False)
    data=struct.pack("<4H",gain,7528,1,shutter)
    status=dict(state="ready",id="cal-state",identity_verified=True,error=0,
                length=8,received=8,crc32=zlib.crc32(data),transaction=1,**IDENTITY)
    report=dict(success=True,transport_verified=True,status=status,bytes=8,
                crc32=zlib.crc32(data),sha256=hashlib.sha256(data).hexdigest(),
                final_status=dict(dvp_paused=False,cleanup_failed=False),
                started_unix=1.0,finished_unix=2.0)
    (directory/"cal-state.bin").write_bytes(data)
    (directory/"cal-state.json").write_text(json.dumps(report),encoding="utf-8")
    return load_snapshot(directory)


def write_capture(root: Path,name: str,setpoint: float,mean: float) -> Path:
    directory=root/name; directory.mkdir()
    width,height=384,288; x,y,size=184,136,16
    frames=[]
    for sequence,value in enumerate((int(mean),int(mean)+2),1):
        data=struct.pack(">H",value)*(width*height)
        filename=f"frame-{sequence}.y16be"; (directory/filename).write_bytes(data)
        frames.append(dict(path=filename,sequence=sequence,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                           roi=roi_stats(data,width,height,x,y,size)))
    manifest=dict(format="t384-radiometry-capture-v1",setpoint_c=setpoint,gain="high",
                  distance_m=0.5,emissivity=0.98,complete_frames=2,frames=frames,
                  geometry=dict(width=width,height=height,frame_bytes=width*height*2,pixel_format="Y16BE"),
                  roi=dict(x=x,y=y,size=size),diag_before={"source.pixel_format":"2"},diag_after={"stream.active":"0"},
                  module_states=dict(before=write_snapshot(directory/"module-before"),after=write_snapshot(directory/"module-after")),
                  original_evidence=dict(identity=IDENTITY,files={},algorithm_rules_verified=False))
    (directory/"manifest.json").write_text(json.dumps(manifest),encoding="utf-8")
    return directory


def expect_failure(function):
    try: function()
    except (ValueError,KeyError,OSError): pass
    else: raise AssertionError("invalid capture accepted")


def main() -> int:
    tool=Path(__file__).resolve().parents[1]/"tools/analyze_blackbody_pair.py"
    with tempfile.TemporaryDirectory(prefix="t384-blackbody-pair-") as temp:
        root=Path(temp)
        low=write_capture(root,"low",-20.0,40000)
        high=write_capture(root,"high",50.0,42000)
        check=write_capture(root,"validation",15.0,41000)
        output=root/"pair.json"
        subprocess.run([sys.executable,str(tool),str(low),str(high),"--validation",str(check),
                        "--max-error-c","2","--output",str(output)],check=True,capture_output=True,text=True)
        report=json.loads(output.read_text(encoding="utf-8"))
        assert report["status"]=="engineering-only-not-oem-radiometric"
        assert report["experimental_validation_passed"] and not report["oem_radiometry_ready"]
        assert report["candidate"]["valid_setpoint_range_c"]==[-20.0,50.0]
        assert report["low"]["frame_count"]==2 and report["roi"]["x"]==184
        expect_failure(lambda: load_capture(low,256,192,120,88,16))
        data=(low/"frame-1.y16be").read_bytes()
        (low/"frame-1.y16be").write_bytes(data[:-1])
        expect_failure(lambda: read_capture(low))
        (low/"frame-1.y16be").write_bytes(data)
        original=json.loads((low/"manifest.json").read_text(encoding="utf-8"))
        for mutation in ("cached_roi","identity","count","path","sequence","gain","shutter"):
            bad=copy.deepcopy(original)
            if mutation=="cached_roi": bad["frames"][0]["roi"]["mean_y16"]+=1
            if mutation=="identity": bad["original_evidence"]["identity"]["sn"]="another-module"
            if mutation=="count": bad["complete_frames"]+=1
            if mutation=="path": bad["frames"][0]["path"]="../frame-1.y16be"
            if mutation=="sequence": bad["frames"][1]["sequence"]=bad["frames"][0]["sequence"]
            if mutation=="gain": bad["module_states"]["after"]["snapshot"]["gain"]="low"
            if mutation=="shutter": bad["module_states"]["after"]["snapshot"]["shutter_status"]=0
            expect_failure(lambda: read_capture(low,manifest_data=bad))
        before=(low/"module-before/cal-state.bin").read_bytes()
        (low/"module-before/cal-state.bin").write_bytes(b"\0"*8)
        expect_failure(lambda: read_capture(low))
        (low/"module-before/cal-state.bin").write_bytes(before)
        a,b,c=load_capture(low),load_capture(high),load_capture(check)
        c["roi_min_y16"]-=1000
        assert not analyze_pair(a,b,[c],2)["experimental_validation_passed"]
        b["gain"]="low"; expect_failure(lambda: analyze_pair(a,b,[]))
        b=load_capture(high); b["roi_mean_y16"]=a["roi_mean_y16"]
        expect_failure(lambda: analyze_pair(a,b,[]))
    print("T384 blackbody original-file/384 ROI/state/tamper/negative-temperature/independent-validation smoke passed")
    return 0


if __name__=="__main__": raise SystemExit(main())
