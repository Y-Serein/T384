"""Download acceptance/manifest tests without network or hardware."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
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
print("module file host integrity/manifest/cleanup smoke passed")
