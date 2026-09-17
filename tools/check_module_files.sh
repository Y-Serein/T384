#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
result_dir="$(mktemp -d /tmp/t384-module-files.XXXXXX)"
gcc -std=gnu99 -Wall -Wextra -Werror -DT384_HOST_SYNTAX_CHECK \
  -I"$project_root/firmware/Common/Raw16" -I"$project_root/firmware/Common/App" \
  "$project_root/tests/module_files_smoke.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files_http.c" \
  "$project_root/firmware/Common/Raw16/t384_mini2_protocol.c" \
  "$project_root/firmware/Common/Raw16/t384_frame_pipeline.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16_wire.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16.c" \
  -o "$result_dir/smoke"
for scenario in {0..15}; do "$result_dir/smoke" "$scenario"; done
gcc -shared -fPIC -std=gnu99 -Wall -Wextra -Werror \
  "$project_root/firmware/Common/Raw16/t384_mini2_protocol.c" \
  -o "$result_dir/protocol.so"
python3 "$project_root/tests/mini2_file_sdk_vectors.py" "$result_dir/protocol.so"
python3 "$project_root/tests/mini2_video_sdk_vectors.py" "$result_dir/protocol.so"
gcc -std=gnu99 -Wall -Wextra -Werror -Wno-comment -DT384_HOST_SYNTAX_CHECK -DCore_V3F \
  -ffunction-sections -fdata-sections \
  -I"$project_root/firmware/V3F/User" \
  -I"$project_root/firmware/Common/Debug" \
  -I"$project_root/firmware/Common/Raw16" -I"$project_root/firmware/Common/App" \
  -I"$project_root/firmware/ThirdParty/lwIP/src/include" \
  -I"$project_root/firmware/ThirdParty/networking" \
  -I"$project_root/firmware/Vendor/WCH/CH32H417/SRC/Core" \
  -I"$project_root/firmware/Vendor/WCH/CH32H417/SRC/Peripheral/inc" \
  "$project_root/tests/module_files_http_smoke.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files_http.c" \
  "$project_root/firmware/Common/Raw16/t384_mini2_protocol.c" \
  "$project_root/firmware/Common/Raw16/t384_frame_pipeline.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16_wire.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16.c" \
  -Wl,--gc-sections -o "$result_dir/http-smoke"
for scenario in {0..6}; do "$result_dir/http-smoke" "$scenario"; done
gcc -std=gnu99 -Wall -Wextra -Werror -Wno-comment -DT384_HOST_SYNTAX_CHECK -DCore_V3F \
  -ffunction-sections -fdata-sections \
  -I"$project_root/firmware/V3F/User" \
  -I"$project_root/firmware/Common/Debug" \
  -I"$project_root/firmware/Common/Raw16" -I"$project_root/firmware/Common/App" \
  -I"$project_root/firmware/ThirdParty/lwIP/src/include" \
  -I"$project_root/firmware/ThirdParty/networking" \
  -I"$project_root/firmware/Vendor/WCH/CH32H417/SRC/Core" \
  -I"$project_root/firmware/Vendor/WCH/CH32H417/SRC/Peripheral/inc" \
  "$project_root/tests/calibration_http_smoke.c" \
  "$project_root/firmware/Common/Raw16/t384_calibration_storage.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files.c" \
  "$project_root/firmware/Common/Raw16/t384_module_files_http.c" \
  "$project_root/firmware/Common/Raw16/t384_mini2_protocol.c" \
  "$project_root/firmware/Common/Raw16/t384_frame_pipeline.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16_wire.c" \
  "$project_root/firmware/Common/Raw16/t384_raw16.c" \
  -Wl,--gc-sections -o "$result_dir/calibration-http-smoke"
"$result_dir/calibration-http-smoke"
python3 "$project_root/tests/module_files_host_smoke.py"
python3 "$project_root/tools/read_mini2_module_files.py" --help >/dev/null
echo "MINI2 read-only file host checks passed; no hardware was accessed"
