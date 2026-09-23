#!/usr/bin/env python3
"""Read-only release gate for BOTH fresh WCH maps/HEX and their merged BIN."""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware"


def fail(message):
    raise SystemExit(message)


def section(text, name):
    match = re.search(r"^" + re.escape(name) +
                      r"\s+(?:\n\s*)?0x([0-9a-f]+)\s+0x([0-9a-f]+)", text, re.M | re.I)
    if not match:
        fail(f"missing {name} in target map")
    return int(match[1], 16), int(match[2], 16)


def symbol(text, name):
    match = re.search(r"0x([0-9a-f]+)\s+(?:PROVIDE\s*\(\s*)?" +
                      re.escape(name) + r"(?:\s|=|\))", text, re.I)
    if not match:
        fail(f"missing {name} in target map")
    return int(match[1], 16)


def read_hex(path):
    upper, image, eof = 0, {}, False
    for line in path.read_text().splitlines():
        if not line.startswith(":") or eof:
            fail(f"bad HEX record in {path.name}")
        record = bytes.fromhex(line[1:])
        if len(record) != record[0] + 5 or sum(record) & 255:
            fail(f"bad HEX length/checksum in {path.name}")
        count, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:4 + count]
        if kind == 0:
            for i, byte in enumerate(data):
                target = upper + address + i
                # WCH emits the zero-based Flash alias; normalize physical alias too.
                if 0x08000000 <= target < 0x080F0000:
                    target -= 0x08000000
                if target in image:
                    fail(f"overlapping HEX records in {path.name}")
                image[target] = byte
        elif kind == 1:
            eof = True
        elif kind == 4 and count == 2:
            upper = int.from_bytes(data, "big") << 16
        elif kind == 2 and count == 2:
            upper = int.from_bytes(data, "big") << 4
        elif kind not in (3, 5):
            fail(f"unsupported HEX record in {path.name}")
    if not eof or not image:
        fail(f"empty/incomplete HEX in {path.name}")
    return image


def main():
    maps = {core: FW / f"{core}/obj/T384-RAW16-BENCH_{core}.map"
            for core in ("V3F", "V5F")}
    missing = [str(p.relative_to(ROOT)) for p in maps.values() if not p.is_file()]
    if missing:
        fail("dual-core target build required; missing " + ", ".join(missing))
    inputs = [p for base in (FW / "Common", FW / "Vendor", FW / "ThirdParty",
                             FW / "V3F/User", FW / "V5F/User")
              for p in base.rglob("*") if p.is_file() and
              p.suffix in (".c", ".h", ".S", ".ld", ".inc")]
    inputs += [FW / "T384-RAW16-BENCH.wvsln"]
    for core in maps:
        inputs += [FW / f"{core}/T384-RAW16-BENCH_{core}.wvproj",
                   FW / core / ".cproject", FW / core / ".kernel"]
    latest = max(inputs, key=lambda p: p.stat().st_mtime_ns)
    ipc_size, images = None, {}
    profile = re.search(r"#define\s+T384_RAW16_PROFILE\s+(\d+)u",
                        (FW / "Common/Raw16/t384_raw16.h").read_text())
    profile_id = int(profile[1])
    v5f_project = (FW / "V5F/T384-RAW16-BENCH_V5F.wvproj").read_text()
    network_on_v5f = "T384_NETWORK_ON_V5F=1" in v5f_project
    frame_bytes = {640: 655360, 384: 221184, 256: 98304}[profile_id]
    streaming = profile_id in (384, 640)
    capture_bytes = (165888 if network_on_v5f and profile_id == 640
                     else {640: 220320, 384: 147456, 256: 98304}[profile_id])
    for core, path in maps.items():
        if path.stat().st_mtime_ns < latest.stat().st_mtime_ns:
            fail(f"stale {core} map; newer input {latest.relative_to(ROOT)}; rebuild both cores")
        text = path.read_text(errors="replace")
        if symbol(text, "_start") != (0 if core == "V3F" else 0x30000):
            fail(f"unexpected {core} reset entry")
        addr, size = section(text, ".t384_ipc")
        if addr != 0x2017F000 or size > 1024 or size == 0:
            fail(f"bad {core} shared IPC reservation")
        if ipc_size is not None and ipc_size != size:
            fail("cross-core IPC layout sizes differ")
        ipc_size = size
        code_addr, code_size = section(text, ".highcode")
        expected_code = ((0x20100000, 150 * 1024) if core == "V3F" else
                         (0x200A0000, 128 * 1024 if network_on_v5f else 32 * 1024))
        if code_addr != expected_code[0] or code_size > expected_code[1]:
            fail(f"{core} runtime code exceeds its RAM partition")
        if section(text, ".stack") != (
                0x2017F800 if core == "V3F" else 0x200FF800, 2048):
            fail(f"{core} stack placement/size mismatch")
        if core == "V3F":
            if symbol(text, "t384_dualcore_frame") != 0x200C0300:
                fail("V3F DTCM frame alias mismatch")
            expected_heap_end = 0x20143FE0
            if symbol(text, "_heap_end") != expected_heap_end:
                fail("V3F heap can overlap fixed DMA/IPC/shared-frame regions")
            margin = expected_heap_end - symbol(text, "_ebss")
            if margin < 32768:
                fail(f"V3F heap margin {margin} B < 32 KiB")
            if re.search(r"\.bss\.(?:slot_data|source_stats)\s+0x", text):
                fail("V3F still owns capture payload/state")
            if profile_id == 640 and network_on_v5f:
                if section(text, ".t384_frame_shared") != (0x20143FE0, 0x29020):
                    fail("V3F shared packed-frame reservation mismatch")
        else:
            if section(text, ".t384_frame") != (0x200C0300, capture_bytes):
                fail("V5F full-frame payload placement/size mismatch")
            dma_bytes = {640: 10240, 384: 12288, 256: 1024}[profile_id]
            if section(text, ".t384_dma") != (0x2017C000, dma_bytes):
                fail("V5F shared DMA staging mismatch")
            if symbol(text, "_heap_end") != 0x200FB000:
                fail("V5F heap overlaps secondary DTCM payload")
            margin = 0x200FB000 - symbol(text, "_ebss")
            if network_on_v5f and profile_id in (384, 640):
                ro_addr, ro_size = section(text, ".t384_http_rodata")
                if ro_addr < 0x30000 or ro_addr + ro_size >= 0x50000:
                    fail("V5F compressed HTTP response is outside V5F Flash")
                meta = section(text, ".t384_frame1_itcm")
                expected_meta = (0x200FB000,
                                 32 if profile_id == 640 else 384)
                if meta != expected_meta:
                    fail("bad V5F network metadata placement")
                if profile_id == 640:
                    if section(text, ".t384_frame_shared_meta") != (0x20143FE0, 2080):
                        fail("bad V5F packed metadata extension placement")
                    if section(text, ".t384_frame_shared") != (0x20144800, 165888):
                        fail("bad V5F packed payload extension placement")
                for name, region_start, region_end in (
                        (".t384_net_http", 0x200FB000, 0x200FF800),
                        (".t384_net_ncm", 0x20125800, 0x20130000),
                        (".t384_net_heap", 0x2016D000, 0x2017C000)):
                    addr, size = section(text, name)
                    if addr < region_start or addr + size > region_end:
                        fail(f"{name} exceeds V5F network SRAM bank")
                secondary_sizes = None
            else:
                secondary_sizes = {640: (2048, 18144, 41472, 56960),
                                   384: (384, 32, 32, 32),
                                   256: (98304, 18432, 43008, 61440)}[profile_id]
            if secondary_sizes is not None:
                for name, addr, size in zip(
                        (".t384_frame1_itcm", ".t384_frame1_dtcm", ".t384_frame1_code", ".t384_frame1_data"),
                        (0x200A8000, 0x200FB000, 0x20125800, 0x2016D000), secondary_sizes):
                    if section(text, name) != (addr, size):
                        fail(f"bad secondary frame region {name}")
            if margin < 8192:
                fail(f"V5F heap margin {margin} B < 8 KiB")
            if re.search(r"\.bss\.(?:ncm_epbuf|ram_heap)\s+0x", text):
                fail("V5F unexpectedly contains network buffers")
        print(f"{core} map: IPC={ipc_size} B, heap margin={margin} B")
        hex_path = path.with_suffix(".hex")
        if not hex_path.is_file() or hex_path.stat().st_mtime_ns < latest.stat().st_mtime_ns:
            fail(f"missing/stale {core} HEX")
        image = read_hex(hex_path)
        low, high = (0, 0x2E000) if core == "V3F" else (0x30000, 0x50000)
        if min(image) < low or max(image) >= high or low not in image:
            fail(f"{core} HEX exceeds its program Flash partition")
        images.update(image)
    merged = FW / "V5F/obj/Merge.bin"
    if not merged.is_file() or merged.stat().st_mtime_ns < latest.stat().st_mtime_ns:
        fail("missing/stale V5F/obj/Merge.bin")
    binary = merged.read_bytes()
    if len(binary) != max(images) + 1:
        fail("Merge.bin size does not match both HEX address ranges")
    for address, byte in enumerate(binary):
        if byte != images.get(address, 0xFF):
            fail(f"Merge.bin differs from HEX/fill at 0x{address:X}")
    print("Fresh dual-core maps/HEX/Merge.bin passed; physical boot/DMA/frame output still requires board validation")


if __name__ == "__main__":
    main()
