#!/usr/bin/env python3
"""Regenerate the configured development Merge.bin; never downloads it."""
from check_dualcore_artifacts import FW, fail, read_hex


def main():
    image = {}
    for core, low, high in (("V3F", 0, 0x2E000), ("V5F", 0x30000, 0x50000)):
        data = read_hex(FW / core / "obj" / f"T384-RAW16-BENCH_{core}.hex")
        if min(data) != low or max(data) >= high or image.keys() & data.keys():
            fail(f"invalid/overlapping {core} HEX range")
        image.update(data)
    binary = bytearray(b"\xff" * (max(image) + 1))
    for address, byte in image.items():
        binary[address] = byte
    target = FW / "V5F/obj/Merge.bin"
    temporary = target.with_suffix(".bin.tmp")
    temporary.write_bytes(binary)
    temporary.replace(target)
    print(f"Generated {target}: {len(binary)} B, both HEX offsets retained")
    print("FF gaps include calibration slots; this tool does not preserve stored Flash data.")


if __name__ == "__main__":
    main()
