"""Cross-check firmware commands against local OEM SDK callbacks, no device I/O."""
import ctypes as C
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
sdk_path = root / "tools/t384_web/AC020_win&&linux_SDK/libir_SDK_release/linux/x64/libircmd.so"
sdk = C.CDLL(str(sdk_path))
firmware = C.CDLL(sys.argv[1])
pointer = C.POINTER(C.c_uint8)
callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, pointer, C.c_uint32)
records = []


@callback_type
def transfer(device, params, data, length):
    record = bytes(data[:length])
    records.append(record)
    if record[2] == 0x87:
        C.memmove(C.addressof(data.contents)+18, b"\x02\x00\x02\x00\x00", 5)
    elif record[2] == 0x86:
        C.memset(C.addressof(data.contents)+18, 0, length-18)
    return 0


# IrControlHandle_t from the local libircam.h: twelve pointer-sized fields.
control = (C.c_void_p * 12)()
control[0] = 1  # sentinel, callbacks never dereference it
control[3] = control[4] = C.cast(transfer, C.c_void_p).value
sdk.ircmd_log_register(2, None, None)
sdk.ircmd_create_handle.argtypes = [C.c_void_p]
sdk.ircmd_create_handle.restype = C.c_void_p
handle = sdk.ircmd_create_handle(control)
assert handle
sdk.adv_cfg_file_open.argtypes = [C.c_void_p, C.c_char_p, C.c_uint32,
                                 C.POINTER(C.c_int), C.POINTER(C.c_uint32)]
sdk.adv_cfg_file_read.argtypes = [C.c_void_p, C.c_int, pointer]
sdk.adv_cfg_file_close.argtypes = [C.c_void_p, C.c_int]
sdk.ircmd_delete_handle.argtypes = [C.c_void_p]
path = b"default_data/high_gain/tpd_nuc_t.bin"
file_id, length = C.c_int(), C.c_uint32()
try:
    assert sdk.adv_cfg_file_open(handle, path, 0, C.byref(file_id), C.byref(length)) == 0
    assert length.value == 512
    block = (C.c_uint8 * 512)()
    assert sdk.adv_cfg_file_read(handle, file_id, block) == 0
    assert sdk.adv_cfg_file_close(handle, file_id) == 0
finally:
    sdk.ircmd_delete_handle(handle)

open_command = (C.c_uint8 * 279)()
assert firmware.t384_mini2_build_file_open(open_command, file_id, path)
assert bytes(open_command)[5:] == records[0]
assert bytes(open_command)[:5] == b"UCI\x12\x01"
info = (C.c_uint8 * 23)()
firmware.t384_mini2_build_file_info(info, file_id)
assert bytes(info)[5:] == records[1][:18]
read = (C.c_uint8 * 23)()
assert firmware.t384_mini2_build_file_read(read, file_id, 0, 512)
assert bytes(read)[5:] == records[2][:18]
close = (C.c_uint8 * 23)()
firmware.t384_mini2_build_file_close(close, file_id)
assert bytes(close)[5:] == records[3]
import binascii
for record in records:
    assert int.from_bytes(record[16:18], "little") == binascii.crc_hqx(record[:16], 0)
print("MINI2 local OEM SDK open/info/read/close vectors passed (callbacks only)")

# Query parameters with a distinct callback: no device handles or hardware.
class TpdParameters(C.Structure):
    _fields_ = [("ktemp", C.c_int16), ("btemp", C.c_int16), ("addr_ca", C.c_uint16)]

@callback_type
def parameter_transfer(device, params, data, length):
    records.append(bytes(data[:18]))
    assert length == 24
    C.memmove(C.addressof(data.contents)+18, b"\xfe\xff\xfd\xff\x34\x12", 6)
    return 0

for index in (3, 4, 5):
    control[index] = C.cast(parameter_transfer, C.c_void_p).value
handle = sdk.ircmd_create_handle(control)
assert handle
sdk.adv_tpd_parameters_get.argtypes = [C.c_void_p, C.c_int, C.POINTER(TpdParameters)]
firmware.t384_mini2_build_tpd_parameters_query_command.argtypes = [pointer, C.c_uint8]
firmware.t384_mini2_build_tpd_parameters_query_command.restype = C.c_bool
try:
    for gain in (0, 1):
        records.clear()
        parameters = TpdParameters()
        assert sdk.adv_tpd_parameters_get(handle, gain, C.byref(parameters)) == 0
        command = (C.c_uint8 * 23)()
        assert firmware.t384_mini2_build_tpd_parameters_query_command(command, gain)
        assert records == [bytes(command)[5:]]
        assert (parameters.ktemp, parameters.btemp, parameters.addr_ca) == (-2, -3, 0x1234)
        assert binascii.crc_hqx(records[0][:16], 0) == int.from_bytes(records[0][16:18], "little")
    assert not firmware.t384_mini2_build_tpd_parameters_query_command(command, 2)
    assert not firmware.t384_mini2_build_tpd_parameters_query_command(None, 1)
finally:
    sdk.ircmd_delete_handle(handle)
print("MINI2 OEM SDK TPD parameter query vectors and signed LE parsing passed (callbacks only)")

@callback_type
def state_transfer(device,params,data,length):
    records.append(bytes(data[:18]))
    assert length in (19,20)
    C.memset(C.addressof(data.contents)+18,0,length-18)
    data[18]=1
    return 0

for index in (3,4,5): control[index]=C.cast(state_transfer,C.c_void_p).value
handle=sdk.ircmd_create_handle(control)
firmware.t384_mini2_build_calibration_state_query_command.argtypes=[pointer,C.c_uint8]
firmware.t384_mini2_build_calibration_state_query_command.restype=C.c_bool
try:
    for field,name in ((0,"basic_gain_get"),(2,"basic_auto_ffc_status_get"),(3,"adv_shutter_status_get")):
        fn=getattr(sdk,name); fn.argtypes=[C.c_void_p,C.POINTER(C.c_int)]
        records.clear(); value=C.c_int()
        assert fn(handle,C.byref(value))==0 and value.value==1
        assert firmware.t384_mini2_build_calibration_state_query_command(command,field)
        assert records==[bytes(command)[5:]]
    records.clear(); value=C.c_uint32()
    sdk.adv_device_realtime_status_get.argtypes=[C.c_void_p,C.c_int,C.POINTER(C.c_uint32)]
    assert sdk.adv_device_realtime_status_get(handle,0,C.byref(value))==0 and value.value==1
    assert firmware.t384_mini2_build_calibration_state_query_command(command,1)
    assert records==[bytes(command)[5:]]
    assert not firmware.t384_mini2_build_calibration_state_query_command(command,4)
    assert not firmware.t384_mini2_build_calibration_state_query_command(None,0)
finally:
    sdk.ircmd_delete_handle(handle)
print("MINI2 OEM SDK calibration gain/raw-Vtemp/auto-FFC/shutter query vectors passed (callbacks only)")
