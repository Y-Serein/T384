#!/usr/bin/env python3
"""Actual wire parser/capture/wizard under bounded fake transport; no device."""
from __future__ import annotations
import argparse
import binascii
import io
import json
import struct
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from blackbody_pair_smoke import IDENTITY,write_snapshot,write_capture
import capture_radiometry_calibration as capture
import calibrate_t384_blackbody as wizard
import t384_raw16_bench as wire
from calibration_capture_support import read_capture


def encoded_stream(synthetic=False,duplicate=False):
    chunks=[]; width,height=384,288; count=width*height*2
    for index in range(2):
        sequence=1 if duplicate else index+1
        data=struct.pack(">H",40000+index*2)*(width*height)
        for offset in range(0,count,6144):
            payload=data[offset:offset+6144]
            flags=wire.FLAG_TPD_Y16 | (wire.FLAG_SYNTHETIC if synthetic else 0)
            if offset==0: flags|=wire.FLAG_FRAME_START
            if offset+len(payload)==count: flags|=wire.FLAG_FRAME_END
            header=bytearray(wire.HEADER_STRUCT.pack(wire.WIRE_MAGIC,wire.WIRE_VERSION,wire.WIRE_HEADER_BYTES,
                    sequence,offset,count,index*34,len(payload),width,height,flags,wire.PIXEL_FORMAT_Y16_BE,0))
            struct.pack_into("<H",header,34,binascii.crc_hqx(header[:34],0))
            chunks.extend((header,payload))
    return b"".join(chunks)


class Response(io.BytesIO):
    status=200
    headers={"Content-Type":"application/x-t384-frame-chunks","X-T384-Format":"T384-FRAME-CHUNK-V1",
             "X-T384-Wire-Version":"1","X-T384-Chunk-Header-Bytes":"36",
             "X-T384-Frame-Width":"384","X-T384-Frame-Height":"288","X-T384-Frame-Bytes":"221184",
             "X-T384-Chunk-Payload-Max":"6144","X-T384-Frame-Mode":"tpd","X-T384-Pixel-Format":"Y16BE",
             "X-T384-Pixel-Format-Code":"2","X-T384-Temperature-Model":"unavailable"}
    def readinto(self,destination): return super().readinto(memoryview(destination)[:127])


class Opener:
    def open(self,request,timeout): return Response(stream)


def args(output):
    return argparse.Namespace(output=output,url="http://192.168.17.1/raw16.stream",diag_url=None,
        table_evidence=Path("unused-fixture"),parameter_evidence=Path("unused-fixture"),gain="current",
        setpoint_c=-20.0,distance_m=0.5,emissivity=0.98,frames=2,timeout=1.0,
        roi_x=None,roi_y=None,roi_size=16,ta_c=None,tu_c=None,humidity=None,
        low_c=-20.0,high_c=50.0,validation_c=15.0,max_error_c=2.0,prepare_only=False)


def snapshot(device,directory): return write_snapshot(directory,gain=0 if bad_gain and directory.name=="module-after" else 1)


def main():
    global stream,bad_gain
    evidence=dict(identity=IDENTITY,files={},algorithm_rules_verified=False)
    capture.load_evidence=lambda *unused:evidence
    capture.query_snapshot=snapshot
    capture.fetch_diag=lambda *unused:{"source.pixel_format":"2","stream.active":"0"}
    capture.urllib.request.build_opener=lambda *unused:Opener()
    with tempfile.TemporaryDirectory(prefix="t384-capture-") as temp:
        root=Path(temp); stream=encoded_stream(); bad_gain=False
        point=root/"valid"; assert capture.capture(args(point))==0
        manifest,means=read_capture(point)
        assert means==[40000,40002] and manifest["gain"]=="high" and manifest["geometry"]["width"]==384
        assert manifest["complete_frames"]==2 and not manifest["gain_verified_per_frame"]
        assert manifest["frames"][0]["bytes"]==221184
        assert manifest["frames"][0]["roi"]["x"]==184 and manifest["frames"][0]["roi"]["y"]==136
        for name in ("truncated","synthetic","duplicate","gain-change"):
            stream=encoded_stream(synthetic=name=="synthetic",duplicate=name=="duplicate")
            if name=="truncated": stream=stream[:-1024]
            bad_gain=name=="gain-change"; directory=root/name
            try: capture.capture(args(directory))
            except (ValueError,wire.BenchError): pass
            else: raise AssertionError("bad acquisition accepted")
            assert not (directory/"manifest.json").exists()
        try: capture.capture(args(point))
        except FileExistsError: pass
        else: raise AssertionError("existing point overwritten")
        wizard.load_evidence=lambda *unused:evidence
        wizard.input=lambda *unused:""
        wizard.fetch_diag=capture.fetch_diag
        def fake_capture(point):
            mean=40000+(point.setpoint_c+20)*2000/70
            write_capture(point.output.parent,point.output.name,point.setpoint_c,mean)
            return 0
        wizard.capture=fake_capture
        session=root/"wizard"; assert wizard.run(args(session))==0
        report=json.loads((session/"report.json").read_text(encoding="utf-8"))
        assert report["experimental_validation_passed"] and not report["applied"]
        prepare=args(root/"prepare"); prepare.prepare_only=True
        assert wizard.run(prepare)==0 and (prepare.output/"plan.json").is_file()
        assert not (prepare.output/"report.json").exists()
        two=args(root/"two-point"); two.low_c=0.0; two.high_c=50.0
        two.validation_c=None; two.max_error_c=None; two.distance_m=0.01
        prompts=[]
        wizard.input=lambda prompt:prompts.append(prompt) or ""
        assert wizard.run(two)==0
        report=json.loads((two.output/"report.json").read_text())
        assert len(prompts)==2 and report["validation"]==[] and not report["experimental_validation_passed"]
        assert not (two.output/"validation").exists()
        def no_http(*unused): raise TimeoutError("fixture device TCP unreachable")
        wizard.fetch_diag=no_http; prompts.clear()
        offline=args(root/"offline")
        try: wizard.run(offline)
        except RuntimeError as error: assert "未开始黑体采集" in str(error)
        else: raise AssertionError("HTTP preflight failure accepted")
        assert prompts==[] and not (offline.output/"low").exists()
        assert not (offline.output/"report.json").exists()
        assert json.loads((offline.output/"preflight.json").read_text())["stage"]=="http-preflight"
    print("384 fragmented capture/no-success-on-failure/no-overwrite/guided-independent-validation smoke passed")


if __name__=="__main__": main()
