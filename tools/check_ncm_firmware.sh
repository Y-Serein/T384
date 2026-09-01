#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
firmware_root="$project_root/tests/ch32h417_t384_ncm"
test_output="/tmp/t384_descriptor_smoke"

includes=(
  -I"$firmware_root/V3F/User"
  -I"$firmware_root/Vendor/WCH/CH32H417/SRC/Core"
  -I"$firmware_root/Vendor/WCH/CH32H417/SRC/Peripheral/inc"
  -I"$firmware_root/Common/App"
  -I"$firmware_root/Common/USB"
  -I"$firmware_root/Common/Camera"
  -I"$firmware_root/Common/Debug"
  -I"$firmware_root/ThirdParty/TinyUSB/src"
  -I"$firmware_root/ThirdParty/lwIP/src/include"
  -I"$firmware_root/ThirdParty/networking"
)

common_flags=(
  -std=gnu99
  -DCore_V3F
  -DT384_HOST_SYNTAX_CHECK
  -Wall
  -Wextra
  -Werror
  -Wno-comment
  "${includes[@]}"
)

python3 "$project_root/tools/embed_device_console.py" --check
python3 -m json.tool "$firmware_root/V3F/T384-NCM_V3F.wvproj" >/dev/null
python3 -m json.tool "$firmware_root/V3F/.kernel" >/dev/null
python3 - "$firmware_root/V3F/.project" "$firmware_root/V3F/.cproject" <<'PY'
import sys
from xml.etree import ElementTree

for path in sys.argv[1:]:
    ElementTree.parse(path)
PY

python3 - "$firmware_root" <<'PY'
import json
import pathlib
import re
import sys
from xml.etree import ElementTree

root = pathlib.Path(sys.argv[1])
project_dir = root / "V3F"
wvproj = project_dir / "T384-NCM_V3F.wvproj"
expected_links = {
    "Common": "../Common",
    "Core": "../Vendor/WCH/CH32H417/SRC/Core",
    "ThirdParty": "../ThirdParty",
    "Peripheral": "../Vendor/WCH/CH32H417/SRC/Peripheral",
    "Startup": "../Vendor/WCH/CH32H417/SRC/Startup",
}
expected_project_links = {
    "Common": "PARENT-1-PROJECT_LOC/Common",
    "Core": "PARENT-1-PROJECT_LOC/Vendor/WCH/CH32H417/SRC/Core",
    "ThirdParty": "PARENT-1-PROJECT_LOC/ThirdParty",
    "Peripheral": "PARENT-1-PROJECT_LOC/Vendor/WCH/CH32H417/SRC/Peripheral",
    "Startup": "PARENT-1-PROJECT_LOC/Vendor/WCH/CH32H417/SRC/Startup",
}
drive_path = re.compile(r"(?:^|[\"'\s>])([A-Za-z]:[\\/])", re.MULTILINE)
data = json.loads(wvproj.read_text(encoding="utf-8"))
links = {item["name"]: item["location"] for item in data["basic"]["linkedFolders"]}
if links != expected_links:
    raise SystemExit(f"non-portable NCM linkedFolders: {links}")
xml_root = ElementTree.parse(project_dir / ".project").getroot()
xml_links = {
    link.findtext("name"): link.findtext("location")
    for link in xml_root.findall("./linkedResources/link")
}
if xml_links != expected_project_links:
    raise SystemExit(f"non-portable NCM linkedResources: {xml_links}")
for path in [root / "T384-NCM.wvsln", wvproj, project_dir / ".project",
             project_dir / ".cproject", project_dir / ".template",
             project_dir / "T384-NCM_V3F.launch"]:
    text = path.read_text(encoding="utf-8", errors="replace")
    if drive_path.search(text) or "ch32h417_t384_ncm" in text:
        raise SystemExit(f"machine-local path remains in NCM metadata: {path}")
print("T384 NCM project portability checks passed")
PY

gcc "${common_flags[@]}" -fsyntax-only \
  "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c" \
  "$firmware_root/Common/USB/usb_descriptors.c" \
  "$firmware_root/Common/App/http_status.c" \
  "$firmware_root/Common/App/t384_ncm.c" \
  "$firmware_root/Common/App/t384_time.c" \
  "$firmware_root/Common/Camera/t384_camera.c" \
  "$firmware_root/V3F/User/main.c" \
  "$firmware_root/ThirdParty/TinyUSB/src/tusb.c" \
  "$firmware_root/ThirdParty/TinyUSB/src/common/tusb_fifo.c" \
  "$firmware_root/ThirdParty/TinyUSB/src/device/usbd.c" \
  "$firmware_root/ThirdParty/TinyUSB/src/device/usbd_control.c" \
  "$firmware_root/ThirdParty/TinyUSB/src/class/net/ncm_device.c" \
  "$firmware_root"/ThirdParty/lwIP/src/core/*.c \
  "$firmware_root"/ThirdParty/lwIP/src/core/ipv4/*.c \
  "$firmware_root/ThirdParty/lwIP/src/netif/ethernet.c" \
  "$firmware_root/ThirdParty/networking/dhserver.c"

gcc "${common_flags[@]}" \
  "$project_root/tests/descriptor_smoke.c" \
  "$firmware_root/Common/USB/usb_descriptors.c" \
  -o "$test_output"
"$test_output"

rg -q 'WINNCM' "$firmware_root/Common/USB/usb_descriptors.c"
rg -q '192\.168\.18\.1' "$project_root/web/device_console.html"
rg -q 'indexedDB' "$project_root/web/device_console.html"
rg -q 'let recording = false' "$project_root/web/device_console.html"
rg -q 'queue_static_chunk' "$firmware_root/Common/App/http_status.c"
rg -Fq 'Connection: keep-alive' "$firmware_root/Common/App/http_status.c"
rg -Fq '#define T384_NCM_IPV4_C 18u' "$firmware_root/Common/App/t384_product_config.h"
rg -q 'USBHS_UDIE_TRANSFER' "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c"
rg -q 'UEP_RX_TOG_AUTO' "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c"
rg -Fq 'GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE)' \
  "$firmware_root/V3F/User/main.c"
rg -Fq 'SWPMI_BypassCmd(ENABLE)' "$firmware_root/V3F/User/main.c"
rg -Fq 'NCM/lwIP init failed; USB kept active' "$firmware_root/V3F/User/main.c"
if rg -q '!tusb_init.*\|\|.*!t384_ncm_init|NVIC_SystemReset' \
  "$firmware_root/V3F/User/main.c"; then
  echo "USB attach can still be hidden by a combined reset path" >&2
  exit 1
fi
rg -q '"mcu": "CH32H417WEU"' "$firmware_root/V3F/T384-NCM_V3F.wvproj"
rg -Fq 'MCU=CH32H417WEU' "$firmware_root/V3F/.template"
rg -Fq '"erase": false' "$firmware_root/V3F/T384-NCM_V3F.wvproj"
rg -Fq '"clearcodeflash": false' "$firmware_root/V3F/T384-NCM_V3F.wvproj"
rg -Fq 'Erase All=false' "$firmware_root/V3F/.template"
rg -Fq 'Clear CodeFlash=false' "$firmware_root/V3F/.template"
if rg -q '\.\./V5F/' "$firmware_root/V3F/.kernel" "$firmware_root/V3F/T384-NCM_V3F.wvproj"; then
  echo "stale V5F merge input in V3F project" >&2
  exit 1
fi

echo "T384 NCM static checks passed"
