"""Download acceptance/manifest tests without network or hardware."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import struct
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from read_mini2_module_files import validate_download, read_one
import read_mini2_module_files as reader

data = bytes(range(256))*128
status = dict(state="ready", id="nuct-high", transaction=3, length=len(data),
              received=len(data), crc32=zlib.crc32(data), error=0, close_status=0,
              cleanup_failed=False, identity_verified=True, pn="WN2256",
              sn="fixture", fw_hex=b"00.00.08.03".hex(), dvp_paused=True)
headers = {"Content-Length": str(len(data)), "X-T384-CRC32": f"{zlib.crc32(data):08x}",
           "X-T384-Transaction": "3"}
result = validate_download(status, data, headers, "nuct-high", False)
assert result["sha256"] == hashlib.sha256(data).hexdigest()
assert result["oem_radiometry_ready"] is False
assert validate_download({**status, "pn": "WN2384T"}, data, headers,
                         "nuct-high", False)["transport_verified"] is True
cases = [({**status, "pn": ""}, data, headers, False),
         ({**status, "identity_verified": False}, data, headers, False),
         ({**status, "close_status": 7}, data, headers, False),
         (status, data[:-2], headers, False),
         (status, b"", headers, False),
         (status, bytes([1])+data[1:], headers, False),
         (status, data, {**headers, "X-T384-Transaction": "2"}, False),
         (status, data, headers, True)]
for s, d, h, proof in cases:
    try:
        validate_download(s, d, h, "nuct-high", proof)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid download accepted")

parameter_data = struct.pack("<hhH", -2, -3, 0x1234)
parameter_status = {**status, "id": "tpd-high", "pn": "WN2384", "open_status": 255,
                    "close_status": 255, "path": "", "length": 6, "received": 6,
                    "crc32": zlib.crc32(parameter_data)}
parameter_headers = {**headers, "Content-Length": "6",
                     "X-T384-CRC32": f"{zlib.crc32(parameter_data):08x}"}
assert validate_download(parameter_status, parameter_data, parameter_headers,
                         "tpd-high", False)["transport_verified"]
for change in ({"close_status": 0}, {"open_status": 2}, {"path": "guessed"}):
    try:
        validate_download({**parameter_status, **change}, parameter_data,
                          parameter_headers, "tpd-high", False)
    except ValueError:
        pass
    else:
        raise AssertionError("parameter query accepted as file transaction")

snapshot_data=struct.pack("<4H",1,7528,1,1)
snapshot_status={**parameter_status,"id":"cal-state","length":8,"received":8,"crc32":zlib.crc32(snapshot_data)}
snapshot_headers={**headers,"Content-Length":"8","X-T384-CRC32":f"{zlib.crc32(snapshot_data):08x}"}
assert validate_download(snapshot_status,snapshot_data,snapshot_headers,"cal-state",False)["transport_verified"]
assert reader.parse_state_snapshot(snapshot_data)["gain"]=="high"
assert not reader.parse_state_snapshot(snapshot_data)["frame_state_verified"]
try: reader.parse_state_snapshot(struct.pack("<4H",9,7528,1,1))
except ValueError: pass
else: raise AssertionError("unknown gain enum accepted")


class Device:
    def __init__(self, bad_cleanup=False):
        self.downloaded = False
        self.bad_cleanup = bad_cleanup
    def json(self, route, body=None):
        if route == "read":
            assert body == "nuct-high"
            return dict(status)
        assert route == "status"
        if self.bad_cleanup:
            raise RuntimeError("test cleanup failure")
        return {**status, "state": "done", "dvp_paused": False}
    def request(self, route):
        assert route == "data?transaction=3"
        self.downloaded = True
        return data, headers


with tempfile.TemporaryDirectory(prefix="t384-file-host-") as tmp:
    class LostStartDevice:
        def json(self,route,body=None):
            if route=="read": raise TimeoutError("lost start response")
            assert route=="status" # No blind abort/retry when transaction unknown.
            return dict(transaction=99,state="reading",dvp_paused=True)
    lost_output=Path(tmp)/"lost-start"; lost_output.mkdir()
    assert not read_one(LostStartDevice(),"cal-state",lost_output,False)
    lost=json.loads((lost_output/"cal-state.json").read_text())
    assert lost["stage"]=="start-http" and lost["recovery_status"]["transaction"]==99
    assert not (lost_output/"cal-state.bin").exists()
    class SnapshotDevice:
        def json(self,route,body=None):
            if route=="read": assert body=="cal-state"; return snapshot_status
            assert route=="status"
            return {**snapshot_status,"state":"done","dvp_paused":False}
        def request(self,route):
            assert route=="data?transaction=3"
            return snapshot_data,snapshot_headers
    state_output=Path(tmp)/"state"; state_output.mkdir()
    assert read_one(SnapshotDevice(),"cal-state",state_output,False)
    assert json.loads((state_output/"cal-state.json").read_text())["snapshot"]["vtemp_raw"]==7528
    calls=[]
    with patch.object(sys,"argv",["reader","--state","--output",str(Path(tmp)/"state-cli")]), \
        patch.object(reader,"Device"), \
        patch.object(reader,"read_one",side_effect=lambda dev,table,out,proof:calls.append(table) or True):
        assert reader.main()==0
    assert calls==["cal-state"]
    for failed_cleanup in (False, True):
        output = Path(tmp)/str(failed_cleanup)
        output.mkdir()
        assert read_one(Device(failed_cleanup), "nuct-high", output, False) is not failed_cleanup
        report = json.loads((output/"nuct-high.json").read_text())
        assert report["success"] is not failed_cleanup
        assert (output/"nuct-high.bin").read_bytes() == data

    for known_proof in (False, True):
        calls = []
        arguments = ["read_mini2_module_files.py", "--all", "--output", str(Path(tmp)/str(known_proof)/"all")]
        if known_proof:
            arguments.append("--expect-wn2256-nuct")
        with patch.object(sys, "argv", arguments), patch.object(reader, "Device"), \
                patch.object(reader, "read_one", side_effect=lambda dev, table, out, proof: calls.append((table, proof)) or True):
            assert reader.main() == 0
        assert calls == [(table, known_proof and table == "nuct-high") for table in reader.IDS]

    calls = []
    with patch.object(sys, "argv", ["reader", "--all", "--output", str(Path(tmp)/"failed-first")]), \
            patch.object(reader, "Device"), \
            patch.object(reader, "read_one", side_effect=lambda dev, table, out, proof: calls.append(table) or False):
        assert reader.main() == 1
    assert calls == ["nuct-high"]

    class ParameterDevice:
        def json(self, route, body=None):
            if route == "read":
                assert body == "tpd-high"
                return parameter_status
            assert route == "status"
            return {**parameter_status, "state": "done", "dvp_paused": False}
        def request(self, route):
            assert route == "data?transaction=3"
            return parameter_data, parameter_headers

    output = Path(tmp)/"parameters"
    output.mkdir()
    assert read_one(ParameterDevice(), "tpd-high", output, False)
    report = json.loads((output/"tpd-high.json").read_text())
    assert report["parameters"] == dict(ktemp=-2, btemp=-3, address_ca=0x1234)
    assert report["query"]["sdk_gain"] == 1
    assert not report["query"]["actual_video_gain_verified"]
    assert not report["oem_radiometry_ready"]
    calls = []
    with patch.object(sys, "argv", ["reader", "--parameters", "--output", str(Path(tmp)/"both-parameters")]), \
            patch.object(reader, "Device"), \
            patch.object(reader, "read_one", side_effect=lambda dev, table, out, proof: calls.append(table) or True):
        assert reader.main() == 0
    assert calls == ["tpd-high", "tpd-low"]

    calls = []
    with patch.object(sys, "argv", ["reader", "--parameters", "--output", str(Path(tmp)/"locked-parameters")]), \
            patch.object(reader, "Device") as device_class, \
            patch.object(reader, "read_one", side_effect=lambda dev, table, out, proof: calls.append(table) or False):
        device_class.return_value.json.return_value = {"cleanup_failed": True, "dvp_paused": False}
        assert reader.main() == 1
    assert calls == ["tpd-high"]
print("module file host integrity/manifest/cleanup smoke passed")
