#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
includes=(
  -Ifirmware/V3F/User
  -Ifirmware/Vendor/WCH/CH32H417/SRC/Core
  -Ifirmware/Vendor/WCH/CH32H417/SRC/Peripheral/inc
  -Ifirmware/Common/App -Ifirmware/Common/Raw16 -Ifirmware/Common/Debug
  -Ifirmware/Common/USB -Ifirmware/ThirdParty/TinyUSB/src
  -Ifirmware/ThirdParty/lwIP/src/include -Ifirmware/ThirdParty/networking
)
flags=(-std=gnu99 -DT384_DUALCORE=1 -DT384_HOST_SYNTAX_CHECK
       -Wall -Wextra -Werror -Wno-comment "${includes[@]}")
for profile in 256u 384u; do
  gcc "${flags[@]}" -DT384_RAW16_PROFILE="$profile" -DCore_V3F -fsyntax-only \
    firmware/V3F/User/main.c firmware/Common/App/http_status.c \
    firmware/Common/App/t384_time.c firmware/Common/Raw16/t384_dualcore.c \
    firmware/Common/Raw16/t384_frame_pipeline_full.c \
    firmware/Common/Raw16/t384_frame_source_remote.c
  gcc "${flags[@]}" -DT384_RAW16_PROFILE="$profile" -DCore_V5F -fsyntax-only \
    firmware/V5F/User/main.c firmware/Common/App/t384_time.c \
    firmware/Common/Raw16/t384_dualcore.c \
    firmware/Common/Raw16/t384_frame_pipeline_full.c \
    firmware/Common/Raw16/t384_frame_source_mini2.c \
    firmware/Common/Raw16/t384_module_files.c
  gcc "${flags[@]}" -DT384_RAW16_PROFILE="$profile" -DCore_V5F \
    -ffunction-sections -fdata-sections \
    tests/mini2_dvp_capture_smoke.c \
    firmware/Common/Raw16/t384_frame_pipeline_full.c \
    firmware/Common/Raw16/t384_raw16.c \
    firmware/Common/Raw16/t384_raw16_roi.c \
    firmware/Common/Raw16/t384_raw16_wire.c \
    firmware/Common/Raw16/t384_mini2_protocol.c \
    -Wl,--gc-sections -o /tmp/t384_dualcore_capture_smoke
  /tmp/t384_dualcore_capture_smoke
done
gcc "${flags[@]}" -DCore_V5F -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  tests/mini2_dvp_capture_smoke.c \
  firmware/Common/Raw16/t384_frame_pipeline_full.c \
  firmware/Common/Raw16/t384_raw16.c firmware/Common/Raw16/t384_raw16_roi.c \
  firmware/Common/Raw16/t384_raw16_wire.c \
  firmware/Common/Raw16/t384_mini2_protocol.c \
  -Wl,--gc-sections -o /tmp/t384_dualcore_capture_asan
ASAN_OPTIONS=detect_leaks=0 /tmp/t384_dualcore_capture_asan
gcc "${flags[@]}" -DCore_V5F -c firmware/Common/Raw16/t384_dualcore.c \
  -Dt384_frame_source_task=t384_capture_source_task \
  -Dt384_frame_source_get_stats=t384_capture_source_get_stats \
  -Dt384_module_files_start=t384_capture_files_start \
  -Dt384_module_files_abort=t384_capture_files_abort \
  -Dt384_module_files_status=t384_capture_files_status \
  -Dt384_module_files_download=t384_capture_files_download \
  -Dt384_module_files_download_release=t384_capture_files_download_release \
  -o /tmp/t384_dualcore_rpc_capture.o
gcc "${flags[@]}" -DCore_V3F -no-pie \
  tests/dualcore_rpc_smoke.c firmware/Common/Raw16/t384_frame_source_remote.c \
  /tmp/t384_dualcore_rpc_capture.o -o /tmp/t384_dualcore_rpc_smoke
/tmp/t384_dualcore_rpc_smoke
python3 tools/check_dualcore_layout.py
echo "T384 dual-core host checks passed (not a WCH target build or board test)"
