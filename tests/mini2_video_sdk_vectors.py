"""Compare video control constructors to actual local OEM SDK, no device I/O."""
import ctypes as C
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
sdk = C.CDLL(str(root / "tools/t384_web/AC020_win&&linux_SDK/libir_SDK_release/linux/x64/libircmd.so"))
firmware = C.CDLL(sys.argv[1])
pointer = C.POINTER(C.c_uint8)
records = []
callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, pointer, C.c_uint32)
channel_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int))


@callback_type
def transfer(device, params, data, length):
    records.append(bytes(data[:min(length, 18)]))
    if length > 18:
        C.memset(C.addressof(data.contents) + 18, 0, length - 18)
    return 0


@channel_type
def channel(device, result):
    result[0] = 3  # UART_COMMAND_CHANNEL from libircam.h
    return 0


class OutputInfo(C.Structure):
    _fields_ = [(name, C.c_int) for name in ("status", "format", "fps", "num")]


control = (C.c_void_p * 12)()
control[0] = 1
for index in (3, 4, 5):
    control[index] = C.cast(transfer, C.c_void_p).value
control[10] = C.cast(channel, C.c_void_p).value
sdk.ircmd_log_register(2, None, None)
sdk.ircmd_create_handle.argtypes = [C.c_void_p]
sdk.ircmd_create_handle.restype = C.c_void_p
sdk.ircmd_delete_handle.argtypes = [C.c_void_p]
sdk.adv_output_frame_rate_set.argtypes = [C.c_void_p, C.c_int]
sdk.adv_digital_video_output_set.argtypes = [C.c_void_p, OutputInfo]
sdk.adv_digital_video_output_get.argtypes = [C.c_void_p, C.POINTER(OutputInfo)]
sdk.adv_stream_mid_mode_set.argtypes = [C.c_void_p, C.c_int]
sdk.adv_stream_mid_mode_get.argtypes = [C.c_void_p, C.POINTER(C.c_int)]
firmware.t384_mini2_build_video_command.argtypes = [pointer] + [C.c_uint8] * 4
firmware.t384_mini2_build_digital_query_command.argtypes = [pointer]
firmware.t384_mini2_build_query_command.argtypes = [pointer, C.c_uint8, C.c_uint8]
handle = sdk.ircmd_create_handle(control)
assert handle
command = (C.c_uint8 * 23)()


def compare(name, args, constructor, constructor_args):
    records.clear()
    assert getattr(sdk, name)(handle, *args) == 0, name
    constructor(command, *constructor_args)
    assert records == [bytes(command)[5:]], (name, records, bytes(command).hex())


try:
    # SDK lazily selects transport on first call; callbacks only, no USB/UART.
    assert sdk.adv_output_frame_rate_set(handle, 60) == 0
    for rate in (30, 60):
        compare("adv_output_frame_rate_set", (rate,),
                firmware.t384_mini2_build_video_command, (0x44, rate, 0, 0))
        compare("adv_digital_video_output_set", (OutputInfo(1, 1, rate, 0),),
                firmware.t384_mini2_build_video_command, (0x46, 1, 1, rate))
    compare("adv_digital_video_output_get", (C.byref(OutputInfo()),),
            firmware.t384_mini2_build_digital_query_command, ())
    assert command[17] == 3, "0x86 returns three fields, not four"
    compare("adv_stream_mid_mode_set", (1,),
            firmware.t384_mini2_build_video_command, (0x45, 1, 0, 0))
    compare("adv_stream_mid_mode_get", (C.byref(C.c_int()),),
            firmware.t384_mini2_build_query_command, (0x85, 1))
finally:
    sdk.ircmd_delete_handle(handle)
print("MINI2 OEM SDK video setters/getters including 0x86 three-byte query passed (callbacks only)")
