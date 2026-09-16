#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 5 || $# -gt 8 ]]; then
  echo "usage: $0 OUTPUT_DIR SETPOINT_C GAIN DISTANCE_M EMISSIVITY [VIDEO_NODE] [FRAME_COUNT] [MODE_TOOL]" >&2
  exit 2
fi

output_dir="$1"
setpoint_c="$2"
gain="$3"
distance_m="$4"
emissivity="$5"
video_node="${6:-/dev/video0}"
frame_count="${7:-30}"
mode_tool="${8:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../review" && pwd)/iray_uvc_mode}"
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
read_tool="$project_root/tools/read_wn2256_calibration.sh"

if [[ ! -x "$mode_tool" ]]; then
  echo "mode tool is not executable: $mode_tool" >&2
  exit 1
fi
if [[ ! -f "$read_tool" ]]; then
  echo "calibration reader is missing: $read_tool" >&2
  exit 1
fi
if [[ ! -e "$video_node" ]]; then
  echo "video node does not exist: $video_node" >&2
  exit 1
fi
if [[ "$gain" != high && "$gain" != low ]]; then
  echo "GAIN must be high or low" >&2
  exit 2
fi

mkdir -p "$output_dir"
meta="$output_dir/point.json"
frames="$output_dir/frames.y16le"
diag="$output_dir/command_snapshot.txt"
restore_required=0

restore_device() {
  if [[ "$restore_required" -ne 1 ]]; then
    return 0
  fi
  "$mode_tool" mid-yuv >/dev/null 2>&1 || true
  "$mode_tool" usb-on-50 >/dev/null 2>&1 || true
  restore_required=0
}
trap restore_device EXIT INT TERM

printf '{\n  "format": "t384-wn2256-blackbody-point-v1",\n  "setpoint_c": %s,\n  "gain": "%s",\n  "distance_m": %s,\n  "emissivity": %s,\n  "video_node": "%s",\n  "frame_count": %s\n}\n' \
  "$setpoint_c" "$gain" "$distance_m" "$emissivity" "$video_node" "$frame_count" > "$meta"

restore_required=1
"$mode_tool" usb-on-50 >/dev/null
"$mode_tool" mid-tpd >/dev/null
"$read_tool" "$output_dir/calibration" > "$diag" 2>&1 || true

v4l2-ctl --device="$video_node" \
  --set-fmt-video=width=256,height=192,pixelformat=YUYV \
  --set-parm=50 --stream-mmap=3 --stream-skip=100 \
  --stream-count="$frame_count" --stream-to="$frames"

expected_bytes=$((frame_count * 256 * 192 * 2))
actual_bytes=$(wc -c < "$frames")
if [[ "$actual_bytes" -ne "$expected_bytes" ]]; then
  echo "incomplete capture: expected $expected_bytes bytes, got $actual_bytes" >&2
  exit 1
fi
sha256sum "$frames" > "$output_dir/frames.sha256"
echo "captured $frame_count frame(s) to $output_dir"
