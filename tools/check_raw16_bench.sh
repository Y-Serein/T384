#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
firmware_root="$project_root/firmware"
verified_root="$project_root/tests/ch32h417 t384 raw16 bench"

if [[ -e "$project_root/designs" ]]; then
  echo "root designs/ is forbidden; move design artifacts under docs/design/" >&2
  exit 1
fi
descriptor_output="/tmp/t384_raw16_descriptor_smoke"
pattern_output="/tmp/t384_raw16_pattern_smoke"
product_pattern_output="/tmp/t384_raw16_product_pattern_smoke"
pipeline_output="/tmp/t384_raw16_pipeline_smoke"
product_pipeline_output="/tmp/t384_raw16_product_pipeline_smoke"
source_output="/tmp/t384_raw16_source_sim_smoke"
checksum_copy_output="/tmp/t384_lwip_checksum_copy_smoke"
mini2_protocol_output="/tmp/t384_mini2_uart_protocol_smoke"
console_javascript="/tmp/t384_raw16_console.js"

includes=(
  -I"$firmware_root/V3F/User"
  -I"$firmware_root/Vendor/WCH/CH32H417/SRC/Core"
  -I"$firmware_root/Vendor/WCH/CH32H417/SRC/Peripheral/inc"
  -I"$firmware_root/Common/App"
  -I"$firmware_root/Common/USB"
  -I"$firmware_root/Common/Raw16"
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

python3 "$project_root/tools/embed_device_console.py" \
  --source "$project_root/web/raw16_bench_console.html" \
  --output "$firmware_root/Common/App/device_console_html.inc" \
  --check
python3 - "$project_root/web/raw16_bench_console.html" \
  "$console_javascript" <<'PY'
import pathlib
import re
import sys

html = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
matches = re.findall(r"<script>(.*?)</script>", html, flags=re.DOTALL)
if len(matches) != 1:
    raise SystemExit(f"expected one inline script, found {len(matches)}")
pathlib.Path(sys.argv[2]).write_text(matches[0], encoding="utf-8")
PY
node --check "$console_javascript"

python3 -m json.tool \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj" >/dev/null
python3 - "$firmware_root/V3F/.project" "$firmware_root/V3F/.cproject" <<'PY'
import sys
from xml.etree import ElementTree

for path in sys.argv[1:]:
    ElementTree.parse(path)
PY

python3 - "$firmware_root" "$verified_root" <<'PY'
import json
import pathlib
import re
import sys
from xml.etree import ElementTree

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

for root_text in sys.argv[1:]:
    root = pathlib.Path(root_text)
    solution = root / "T384-RAW16-BENCH.wvsln"
    project_dir = root / "V3F"
    wvproj = project_dir / "T384-RAW16-BENCH_V3F.wvproj"
    project_xml = project_dir / ".project"
    cproject = project_dir / ".cproject"
    for required in (solution, wvproj, project_xml, cproject):
        if not required.is_file():
            raise SystemExit(f"missing portable MRS input: {required}")

    project_data = json.loads(wvproj.read_text(encoding="utf-8"))
    actual_links = {
        item["name"]: item["location"]
        for item in project_data["basic"]["linkedFolders"]
    }
    if actual_links != expected_links:
        raise SystemExit(f"non-portable linkedFolders in {wvproj}: {actual_links}")

    xml_root = ElementTree.parse(project_xml).getroot()
    actual_project_links = {
        link.findtext("name"): link.findtext("location")
        for link in xml_root.findall("./linkedResources/link")
    }
    if actual_project_links != expected_project_links:
        raise SystemExit(
            f"non-portable linkedResources in {project_xml}: {actual_project_links}"
        )

    metadata = [solution, wvproj, project_xml, cproject]
    metadata.extend(project_dir.glob("*.launch"))
    for path in metadata:
        text = path.read_text(encoding="utf-8", errors="replace")
        if drive_path.search(text) or "ch32h417_t384_raw16_bench" in text:
            raise SystemExit(f"machine-local path remains in MRS metadata: {path}")

print("T384 firmware/tests MRS portability checks passed")
PY

for root in "$firmware_root" "$verified_root"; do
  if [[ -d "$root/V3F/obj" ]] &&
     rg -q 'firmware[/\\]ch32h417_t384_raw16_bench' \
       "$root/V3F/obj" -g '*.d'; then
    echo "stale pre-migration MRS dependency cache under ${root#"$project_root/"}/V3F/obj" >&2
    exit 1
  fi
done

python3 - "$project_root/firmware" "$project_root/tests" <<'PY'
import json
import pathlib
import re
import sys
from xml.etree import ElementTree

drive_path = re.compile(r"(?:^|[\"'\s>])([A-Za-z]:[\\/])", re.MULTILINE)
for tree_text in sys.argv[1:]:
    tree = pathlib.Path(tree_text)
    metadata = []
    for pattern in ("*.wvsln", "*.wvproj", ".project", ".cproject",
                    ".template", ".kernel", "*.launch"):
        metadata.extend(tree.rglob(pattern))
    for path in metadata:
        if "obj" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if drive_path.search(text):
            raise SystemExit(f"machine-local path remains in MRS metadata: {path}")

    for wvproj in tree.rglob("*.wvproj"):
        data = json.loads(wvproj.read_text(encoding="utf-8"))
        for linked in data.get("basic", {}).get("linkedFolders", []):
            location = linked["location"].replace("\\", "/")
            if location.startswith("/") or re.match(r"^[A-Za-z]:", location):
                raise SystemExit(f"absolute linkedFolder in {wvproj}: {location}")
            target = (wvproj.parent / location).resolve()
            if not target.exists():
                raise SystemExit(f"missing linkedFolder target in {wvproj}: {target}")

    for project_xml in tree.rglob(".project"):
        xml_root = ElementTree.parse(project_xml).getroot()
        for link in xml_root.findall("./linkedResources/link"):
            location = link.findtext("location") or ""
            if not location.startswith("PARENT-"):
                raise SystemExit(
                    f"linkedResource is not project-relative in {project_xml}: {location}"
                )

print("T384 firmware/tests complete MRS portability checks passed")
PY

gcc "${common_flags[@]}" -fsyntax-only \
  "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c" \
  "$firmware_root/Common/USB/usb_descriptors.c" \
  "$firmware_root/Common/App/http_status.c" \
  "$firmware_root/Common/App/t384_ncm.c" \
  "$firmware_root/Common/App/t384_time.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  "$firmware_root/Common/Raw16/t384_frame_pipeline.c" \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c" \
  "$firmware_root/Common/Raw16/t384_mini2_protocol.c" \
  "$firmware_root/Common/Raw16/t384_frame_source_sim.c" \
  "$firmware_root/Common/Raw16/t384_raw16_wire.c" \
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
  -o "$descriptor_output"
"$descriptor_output"

gcc -std=gnu99 -DT384_RAW16_PROFILE=384u \
  -Wall -Wextra -Werror \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/raw16_pattern_smoke.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  -o "$pattern_output"
"$pattern_output"

gcc -std=gnu99 -Wall -Wextra -Werror \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/raw16_pattern_smoke.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  -o "$product_pattern_output"
"$product_pattern_output"

gcc -std=gnu99 -DT384_HOST_SYNTAX_CHECK \
  -DT384_RAW16_PROFILE=384u \
  -Wall -Wextra -Werror \
  -I"$firmware_root/Common/App" \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/raw16_pipeline_smoke.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  "$firmware_root/Common/Raw16/t384_frame_pipeline.c" \
  "$firmware_root/Common/Raw16/t384_raw16_wire.c" \
  -o "$pipeline_output"
"$pipeline_output"

gcc -std=gnu99 -DT384_HOST_SYNTAX_CHECK -Wall -Wextra -Werror \
  -I"$firmware_root/Common/App" \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/raw16_pipeline_smoke.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  "$firmware_root/Common/Raw16/t384_frame_pipeline.c" \
  "$firmware_root/Common/Raw16/t384_raw16_wire.c" \
  -o "$product_pipeline_output"
"$product_pipeline_output"

gcc -std=gnu99 -DT384_HOST_SYNTAX_CHECK \
  -DT384_RAW16_PROFILE=384u \
  -DT384_FRAME_SOURCE_SIMULATOR=1 -Wall -Wextra -Werror \
  -I"$firmware_root/Common/App" \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/raw16_source_sim_smoke.c" \
  "$firmware_root/Common/Raw16/t384_raw16.c" \
  "$firmware_root/Common/Raw16/t384_frame_pipeline.c" \
  "$firmware_root/Common/Raw16/t384_frame_source_sim.c" \
  -o "$source_output"
"$source_output"

gcc -std=gnu99 -Wall -Wextra -Werror \
  -I"$firmware_root/Common/Raw16" \
  "$project_root/tests/mini2_uart_protocol_smoke.c" \
  "$firmware_root/Common/Raw16/t384_mini2_protocol.c" \
  -o "$mini2_protocol_output"
"$mini2_protocol_output"

gcc "${common_flags[@]}" -ffunction-sections -fdata-sections \
  "$project_root/tests/lwip_checksum_copy_smoke.c" \
  "$firmware_root/ThirdParty/lwIP/src/core/inet_chksum.c" \
  -Wl,--gc-sections -o "$checksum_copy_output"
"$checksum_copy_output"

PYTHONPYCACHEPREFIX=/tmp/t384_raw16_pycache \
  python3 -m py_compile "$project_root/tools/t384_raw16_bench.py"
python3 "$project_root/tools/t384_raw16_bench.py" --help >/dev/null

rg -q '"mcu": "CH32H417WEU"' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
rg -Fq 'MCU=CH32H417WEU' "$firmware_root/V3F/.template"
rg -Fq '"erase": false' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
rg -Fq '"clearcodeflash": false' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
rg -Fq 'Erase All=false' "$firmware_root/V3F/.template"
rg -Fq 'Clear CodeFlash=false' "$firmware_root/V3F/.template"
rg -q '"projectName": "T384-RAW16-BENCH_V3F"' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
rg -Fq '"other_optimization_flags": "-O2"' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
rg -Fq 'value="-O2" valueType="string"' \
  "$firmware_root/V3F/.cproject"
rg -Fq 'T384 RAW16 bench requires effective -O2' \
  "$firmware_root/V3F/User/main.c"
rg -Fq 'Target Path=obj/T384-RAW16-BENCH_V3F.hex' \
  "$firmware_root/V3F/.template"
rg -Fq '"target_path": "obj/T384-RAW16-BENCH_V3F.hex"' \
  "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj"
if rg --hidden -a -q 'obj/T384-NCM_V3F\.(hex|elf)' \
  "$firmware_root/V3F"; then
  echo "stale T384-NCM download target in RAW16 bench metadata" >&2
  exit 1
fi
rg -Fq '#if T384_RAW16_FRAME_BYTES != 98304u && T384_RAW16_FRAME_BYTES != 221184u' \
  "$firmware_root/Common/Raw16/t384_raw16.c"
rg -Fq 'X-T384-Format: RAW16LE-CHUNK-V1' \
  "$firmware_root/Common/App/http_status.c"
rg -Fq 't384_frame_pipeline_peek' \
  "$firmware_root/Common/App/http_status.c"
if rg -q 't384_raw16_fill' "$firmware_root/Common/App/http_status.c"; then
  echo "HTTP path still generates synthetic RAW16 pixels" >&2
  exit 1
fi
rg -Fq '#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  16384' \
  "$firmware_root/Common/App/tusb_config.h"
rg -Fq '#define TCP_SND_BUF (16 * TCP_MSS)' \
  "$firmware_root/Common/App/lwipopts.h"
rg -Fq '#define LWIP_CHECKSUM_ON_COPY 1' \
  "$firmware_root/Common/App/lwipopts.h"
rg -Fq '#define LWIP_CHKSUM_COPY(dst, src, len)' \
  "$firmware_root/Common/App/arch/cc.h"
rg -Fq 't384_lwip_chksum_copy' \
  "$firmware_root/Common/App/arch/cc.h"
rg -Fq 'USBHSD->UEP_TX_BURST |= (uint8_t)(1u << ep);' \
  "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c"
rg -Fq 'void tud_network_xmit_flush(void)' \
  "$firmware_root/ThirdParty/TinyUSB/src/class/net/ncm_device.c"
rg -Fq 'tud_network_xmit_flush();' \
  "$firmware_root/Common/App/t384_ncm.c"
rg -Fq '#define T384_NCM_RX_BUDGET 8u' \
  "$firmware_root/Common/App/t384_ncm.c"
rg -Fq 'queue_static_chunk' "$firmware_root/Common/App/http_status.c"
rg -Fq '#define T384_FRAME_SOURCE_SIMULATOR 0' \
  "$firmware_root/Common/Raw16/t384_frame_source.h"
rg -Fq 'mini2-dvp-raw16-v1' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 't384_frame_source_stream_ready' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 't384_mini2_build_dvp30_command' \
  "$firmware_root/Common/Raw16/t384_mini2_protocol.c"
rg -Fq 'mini2_control_dvp30_status' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 'mini2_uart0_query(0x84u' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 'mini2_uart0_query(0x86u' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 'mini2_uart0_query_info(0x01u' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 'mini2_uart0_query_info(0x02u' \
  "$firmware_root/Common/Raw16/t384_frame_source_mini2.c"
rg -Fq 'DMA_M2M_Enable' \
  "$firmware_root/Common/Raw16/t384_frame_source_sim.c"
rg -Fq 'dma-equivalent-dvp-source-v1' \
  "$firmware_root/Common/Raw16/t384_frame_source_sim.c"
rg -Fq "fetch('/raw16.stream" "$project_root/web/raw16_bench_console.html"
rg -Fq 'RAW16LE-CHUNK-V1' "$project_root/web/raw16_bench_console.html"
rg -Fq 'pixels[pixel] = RAW16_PALETTE' \
  "$project_root/web/raw16_bench_console.html"
rg -Fq 'let recording = false' "$project_root/web/raw16_bench_console.html"
rg -Fq 'indexedDB' "$project_root/web/raw16_bench_console.html"
rg -Fq 'MAX_RECORDED_SAMPLES = 86400' \
  "$project_root/web/raw16_bench_console.html"
rg -Fq 'function streamWatchdog' "$project_root/web/raw16_bench_console.html"
if rg -q '\bMOCK\b|mockData|renderMockFrame' \
    "$project_root/web/raw16_bench_console.html"; then
  echo "product page still contains fake local runtime data" >&2
  exit 1
fi
if rg -q 't384_camera_(init|task)' "$firmware_root/V3F/User/main.c"; then
  echo "RAW16 bench still initializes the OV2640 path" >&2
  exit 1
fi
if find "$firmware_root/V3F/obj" -maxdepth 1 \
    \( -name 'T384-NCM_V3F.*' -o -name 'T384-NCM*' \) \
    -print -quit 2>/dev/null | grep -q .; then
  echo "stale T384-NCM build artifact in RAW16 bench" >&2
  exit 1
fi
if find "$firmware_root/Common/Camera" -type f -print -quit 2>/dev/null | grep -q .; then
  echo "OV2640 source remains in RAW16 bench" >&2
  exit 1
fi
map_file="$firmware_root/V3F/obj/T384-RAW16-BENCH_V3F.map"
if [[ -f "$map_file" ]] &&
   rg -q 'Common/Camera/t384_camera\.o|\.bss\.frame_slots' "$map_file"; then
  echo "stale RAW16 bench map still contains OV2640 buffers; Clean and rebuild V3F" >&2
  exit 1
fi
if [[ -f "$map_file" ]]; then
  for input in \
      "$firmware_root/V3F/T384-RAW16-BENCH_V3F.wvproj" \
      "$firmware_root/V3F/.cproject" \
      "$firmware_root/V3F/.template" \
      "$firmware_root/V3F/User/main.c" \
      "$firmware_root/Common/App/http_status.c" \
      "$firmware_root/Common/App/t384_product_config.h" \
      "$firmware_root/Common/App/lwipopts.h" \
      "$firmware_root/Common/App/arch/cc.h" \
      "$firmware_root/Common/App/tusb_config.h" \
      "$firmware_root/Common/App/device_console_html.inc" \
      "$firmware_root/Common/App/t384_ncm.c" \
      "$firmware_root/Common/USB/dcd_ch32h417_usbhs.c" \
      "$firmware_root/Common/Raw16/t384_raw16.c" \
      "$firmware_root/Common/Raw16/t384_raw16.h" \
      "$firmware_root/Common/Raw16/t384_frame_pipeline.c" \
      "$firmware_root/Common/Raw16/t384_frame_pipeline.h" \
      "$firmware_root/Common/Raw16/t384_frame_source_mini2.c" \
      "$firmware_root/Common/Raw16/t384_mini2_protocol.c" \
      "$firmware_root/Common/Raw16/t384_mini2_protocol.h" \
      "$firmware_root/Common/Raw16/t384_frame_source_sim.c" \
      "$firmware_root/Common/Raw16/t384_raw16_wire.c" \
      "$firmware_root/ThirdParty/TinyUSB/src/class/net/net_device.h" \
      "$firmware_root/ThirdParty/TinyUSB/src/class/net/ncm_device.c"; do
    if [[ "$input" -nt "$map_file" ]]; then
      echo "stale RAW16 bench map is older than ${input#"$project_root/"}; rebuild V3F" >&2
      exit 1
    fi
  done
  python3 - "$map_file" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
expected_slot_data = 0x18000
ncm = re.search(r"\.bss\.ncm_epbuf\s*\n\s*0x[0-9a-f]+\s+0x([0-9a-f]+)", text, re.I)
if not ncm or int(ncm.group(1), 16) != 0xD010:
    actual = ncm.group(1) if ncm else "missing"
    raise SystemExit(f"unexpected ncm_epbuf size 0x{actual}; expected 0xD010 for 16KiB x 3 + 4KiB")
payload = re.search(r"\.bss\.slot_data\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", text, re.I)
if not payload or int(payload.group(1), 16) != expected_slot_data:
    actual = payload.group(1) if payload else "missing"
    raise SystemExit(f"unexpected RAW16 slot_data size 0x{actual}; expected 0x{expected_slot_data:X}")
metadata = re.search(r"\.bss\.slots\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", text, re.I)
if not metadata or int(metadata.group(1), 16) != 0x180:
    actual = metadata.group(1) if metadata else "missing"
    raise SystemExit(f"unexpected RAW16 slot metadata size 0x{actual}; expected 0x180")
ebss = re.search(r"0x([0-9a-f]+)\s+PROVIDE \(_ebss = \.\)", text, re.I)
if not ebss:
    raise SystemExit("missing _ebss in RAW16 bench map")
stack_origin = 0x2017F800
margin = stack_origin - int(ebss.group(1), 16)
if margin < 0x8000:
    raise SystemExit(f"RAW16 pipeline RAM margin before stack is only 0x{margin:X}; need at least 32KiB")
print(f"RAW16 bench map config passed: ncm_epbuf=0xD010, slot_data=0x{expected_slot_data:X}, metadata=0x180, pre-stack margin=0x{margin:X}")
PY
fi

echo "T384 RAW16 bench static checks passed"
