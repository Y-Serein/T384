#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -d "$project_root/tools/t384_web/AC020_win&&linux_SDK" ]]; then
  sdk_root="$project_root/tools/t384_web/AC020_win&&linux_SDK"
elif [[ -d "$project_root/iRay/SDK/Win_Linux/AC020_win&&linux_SDK" ]]; then
  sdk_root="$project_root/iRay/SDK/Win_Linux/AC020_win&&linux_SDK"
else
  echo "AC020 SDK directory not found under tools/t384_web or iRay/SDK/Win_Linux" >&2
  exit 1
fi
include_dir="$sdk_root/libir_SDK_release/include"
lib_dir="$sdk_root/libir_SDK_release/linux/x64"
if [[ -f "$project_root/tools/t384_radiometry/iray_tpd_calibration_read.cpp" ]]; then
  source_file="$project_root/tools/t384_radiometry/iray_tpd_calibration_read.cpp"
else
  source_file="$project_root/tools/t384_web/tools/iray_tpd_calibration_read.cpp"
fi
binary="/tmp/t384_iray_tpd_calibration_read"
output_dir="${1:-$project_root/out/radiometry/$(date +%Y%m%d_%H%M%S)}"
control_node="${2:-}"

mkdir -p "$output_dir"
g++ -std=c++17 -Dlinux -Wall -Wextra -Werror \
  -I"$include_dir" "$source_file" \
  -L"$lib_dir" -Wl,-rpath,"$lib_dir" \
  -lirupgrade -lircmd -liruvc -lircam -liruart -lirv4l2 -lusb-1.0 -lpthread -ldl \
  -o "$binary"

echo "只读读取原厂 TPD 标定文件；不会写入 MINI2。"
echo "请连接原厂 iRay USB 命令通道（VID:PID 3474:43c1），不是 CH32 NCM 板。"
echo "WSL USB 透传诊断："
if command -v lsusb >/dev/null 2>&1; then
  if command -v rg >/dev/null 2>&1; then
    lsusb | rg -i '3474|43c1|0020|43d1' || true
  else
    lsusb | grep -Ei '3474|43c1|0020|43d1' || true
  fi
else
  echo "lsusb 未安装"
fi
echo "WSL 视频节点："
ls -l /dev/video* 2>/dev/null || echo "未发现 /dev/video*"
if command -v v4l2-ctl >/dev/null 2>&1; then
  v4l2-ctl --list-devices || true
fi
if [[ -n "$control_node" ]]; then
  LD_LIBRARY_PATH="$lib_dir" "$binary" "$output_dir" "$control_node"
else
  LD_LIBRARY_PATH="$lib_dir" "$binary" "$output_dir"
fi
echo "标定探针输出：$output_dir"
