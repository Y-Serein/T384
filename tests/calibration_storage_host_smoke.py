#!/usr/bin/env python3
"""Storage backup CRC/binding and restore readback, with no device access."""
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location("client", Path(__file__).resolve().parents[1]/"tools/calibration_storage_client.py")
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
payload = bytes(range(256))*8
manifest = dict(schema=1, generation=7, payload_len=len(payload), payload_crc32=client.crc(payload),
                calibration_id=42, model=client.MODEL, profile="384x288", identity="42"*32, gain=1)
packet = client.encode_packet(manifest,payload)
decoded, data = client.decode_packet(packet)
assert data == payload and decoded["identity"] == manifest["identity"]
for offset in (0,20,32,client.HEADER.size, len(packet)-1):
    bad=bytearray(packet); bad[offset]^=1
    try:
        client.decode_packet(bytes(bad))
    except ValueError:
        pass
    else:
        raise AssertionError("corrupt packet accepted")

calls=[]
def fake_exchange(url,method="GET",body=None):
    calls.append((url,method,body))
    if method=="PUT": return b'{"staged":true}'
    if method=="POST": return b'{"committed":true,"applied":false}'
    if url.endswith("manifest"): return json.dumps(manifest).encode()
    return payload
client.exchange=fake_exchange
assert client.restore("device/",packet)["identity"]==manifest["identity"]
assert [method for _,method,_ in calls]==["PUT","POST","GET","GET","GET"]
calls.clear()
try:
    client.restore("device/",packet[:-1])
except ValueError:
    pass
else:
    raise AssertionError("truncated packet accepted")
assert not calls
manifest["identity"]="43"*32
try:
    client.restore("device/",packet)
except ValueError:
    pass
else:
    raise AssertionError("readback with wrong module binding accepted")
print("calibration backup/CRC/restore-binding smoke passed; no hardware accessed")
