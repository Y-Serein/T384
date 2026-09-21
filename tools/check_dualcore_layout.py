#!/usr/bin/env python3
"""Check portable dual-core metadata and exercise both scripts with host ld.

Host linking checks section placement/NOLOAD/assertions, not RISC-V startup,
instruction support, WCH libraries or real DMA/TCM access.
"""
import json
import pathlib
import re
import subprocess
import tempfile
from xml.etree import ElementTree

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware"


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


for core, opposite in (("V3F", "V5F"), ("V5F", "V3F")):
    folder = FW / core
    project = json.loads((folder / f"T384-RAW16-BENCH_{core}.wvproj").read_text())
    cfg = project["buildConfig"]["configurations"][0]
    kernel = project["basic"]["kernel"]
    assert kernel == json.loads((folder / ".kernel").read_text()), core
    assert kernel["kernelName"] == core
    assert kernel["isMaster"] == (core == "V3F")
    assert kernel["mate"] == f"../{opposite}"
    assert kernel["debugOptions"]["dualCoreDebug"]
    assert cfg["riscvTargetProcessor"]["atomic_extension"] is True
    assert cfg["optimization"]["other_optimization_flags"] == "-O2"
    assert set(cfg["ccompiler"]["preprocessor"]["defined_symbols"]) == {
        f"Core_{core}", "T384_DUALCORE=1", "Run_Core=2"}
    assert project["flashConfig"]["erase"] is False
    assert project["flashConfig"]["clearcodeflash"] is False
    assert cfg["clinker"]["general"]["scriptFiles"] == [
        f"${{project}}/Common/Ld/{core}/Link_{core.lower()}.ld"]
    for link in project["basic"]["linkedFolders"]:
        assert link["location"].startswith("../")
        assert (folder / link["location"]).exists(), link
    xml = ElementTree.parse(folder / ".project")
    assert xml.findtext("name") == f"T384-RAW16-BENCH_{core}"
    for link in xml.findall("./linkedResources/link"):
        assert link.findtext("location").startswith("PARENT-1-PROJECT_LOC/")
    xml = ElementTree.parse(folder / ".cproject")
    defines = {e.get("value") for option in xml.iter("option")
               if option.get("valueType") == "definedSymbols"
               for e in option.findall("listOptionValue")}
    assert defines == set(cfg["ccompiler"]["preprocessor"]["defined_symbols"])
    excludes = next(e for e in xml.iter("entry") if e.get("kind") == "sourcePath")
    assert set(excludes.get("excluding").split("|")) == {
        x.removeprefix("${project}/") for x in cfg["excludeResources"]}
    if core == "V5F":
        assert kernel["mergedOptions"]["enabled"]
        assert kernel["mergedOptions"]["downloadMerged"]
        assert kernel["mergedOptions"]["retainOffsetData"]
        assert kernel["mergedOptions"]["mergedList"]["files"] == [
            {"filePath": "../V3F/obj/T384-RAW16-BENCH_V3F.hex"}]
        assert project["flashConfig"]["target_path"] == "obj/Merge.bin"
    else:
        assert project["flashConfig"]["program"] is False

solution = (FW / "T384-RAW16-BENCH.wvsln").read_text()
assert "..\\V3F" in solution and "..\\V5F" in solution

with tempfile.TemporaryDirectory(prefix="t384-dual-link-") as temp:
    temp = pathlib.Path(temp)
    source = temp / "layout.c"
    source.write_text('''
#include "t384_dualcore.h"
t384_dualcore_shared_t t384_dualcore_shared
    __attribute__((section(".t384_ipc"), aligned(32)));
#ifdef Core_V5F
uint8_t t384_dualcore_frame[T384_CAPTURE_BUFFER_BYTES]
    __attribute__((section(".t384_frame"), aligned(32)));
uint8_t staging[12288] __attribute__((section(".t384_dma"), aligned(32)));
uint8_t t384_frame1_itcm[T384_FRAME1_ITCM_BYTES] __attribute__((section(".t384_frame1_itcm"), aligned(32)));
uint8_t t384_frame1_dtcm[T384_FRAME1_DTCM_BYTES] __attribute__((section(".t384_frame1_dtcm"), aligned(32)));
uint8_t t384_frame1_code[T384_FRAME1_CODE_BYTES] __attribute__((section(".t384_frame1_code"), aligned(32)));
uint8_t t384_frame1_data[T384_FRAME1_DATA_BYTES] __attribute__((section(".t384_frame1_data"), aligned(32)));
#endif
uint8_t *frame_reference = t384_dualcore_frame;
uint8_t *secondary_references[] = {t384_frame1_itcm, t384_frame1_dtcm, t384_frame1_code, t384_frame1_data};
uint8_t data[4] = {1, 2, 3, 4};
uint8_t bss[128];
void _start(void) __attribute__((section(".init")));
void _start(void) { }
''')
    for core in ("V3F", "V5F"):
        obj, elf = temp / f"{core}.o", temp / f"{core}.elf"
        script = FW / f"Common/Ld/{core}/Link_{core.lower()}.ld"
        run("gcc", "-c", "-fno-pie", "-fno-asynchronous-unwind-tables",
            "-DT384_DUALCORE=1", "-DT384_RAW16_PROFILE=384u", f"-DCore_{core}",
            "-I" + str(FW / "Common/Raw16"), str(source), "-o", str(obj))
        run("ld", "-T", str(script), str(obj), "-o", str(elf))
        sections = run("readelf", "-SW", str(elf))
        for name, addr in ((".t384_ipc", 0x2017F000),):
            match = re.search(r"\]\s+" + re.escape(name) +
                              r"\s+(\w+)\s+([0-9a-f]+)", sections)
            assert match and match[1] == "NOBITS" and int(match[2], 16) == addr
        symbols = run("nm", "-n", str(elf))
        assert re.search(r"200c0300\s+\w\s+t384_dualcore_frame\b", symbols)
        if core == "V5F":
            assert re.search(r"00030000\s+\w\s+_start\b", symbols)
            for name, addr, size in ((".t384_frame", 0x200C0300, 147456),
                                     (".t384_frame1_itcm", 0x200A8000, 384),
                                     (".t384_frame1_dtcm", 0x200FB000, 32),
                                     (".t384_frame1_code", 0x20125800, 32),
                                     (".t384_frame1_data", 0x2016D000, 32),
                                     (".t384_dma", 0x2017C000, 12288)):
                match = re.search(r"\]\s+" + re.escape(name) +
                                  r"\s+(\w+)\s+([0-9a-f]+)\s+\w+\s+([0-9a-f]+)", sections)
                assert match and match[1] == "NOBITS"
                assert int(match[2], 16) == addr and int(match[3], 16) == size
        # The ordinary heap cannot grow into fixed DMA/IPC/frame regions.
        heap = re.search(r"([0-9a-f]+)\s+\w\s+_heap_end\b", symbols)
        if core == "V3F":
            # PROVIDE emits the symbol only when referenced; inspect script too.
            assert "PROVIDE( _heap_end = ORIGIN(RAM) + LENGTH(RAM) );" in script.read_text()
        else:
            assert heap and int(heap[1], 16) == 0x200FB000
        for name, addr in (("t384_frame1_itcm", 0x200A8000),
                           ("t384_frame1_dtcm", 0x200FB000),
                           ("t384_frame1_code", 0x20125800),
                           ("t384_frame1_data", 0x2016D000)):
            assert re.search(f"{addr:08x}\\s+\\w\\s+{name}\\b", symbols)

print("Dual-core metadata, wake/link address, NOLOAD frame/DMA/IPC and heap bounds passed (host ld)")
