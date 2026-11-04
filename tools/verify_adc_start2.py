#!/usr/bin/env python3
"""Verify the production ADC START2 function's ELF location and instruction bytes."""
import struct
import sys
from pathlib import Path

SYMBOL = "wp5_adc_convert5_start2"
# Exact 52-byte function body from R3 Flash-XIP START2 path A.
# Compare the function itself, excluding the NOP before its symbol.
EXPECTED = bytes.fromhex(
    "2d e9 10 4c 4f f0 04 0b 9c 46 03 f1 0a 0a "
    "c1 f8 00 b0 00 bf 00 bf 13 68 db 05 fc d5 "
    "53 68 2c f8 02 3b 13 68 e2 45 c3 f3 40 23 "
    "00 f8 01 3b ee d1 bd e8 10 8c"
)


def read_elf(path):
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("Expected little-endian ELF32")
    e_shoff = struct.unpack_from("<I", data, 32)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    sections = []
    for i in range(e_shnum):
        pos = e_shoff + i * e_shentsize
        sections.append(struct.unpack_from("<IIIIIIIIII", data, pos))
    if not sections or e_shstrndx >= len(sections):
        raise ValueError("Invalid ELF section table")

    def section_data(idx):
        s = sections[idx]
        return data[s[4]:s[4] + s[5]]

    def cstr(buf, i):
        if i >= len(buf):
            return ""
        return buf[i:buf.find(b"\0", i) if b"\0" in buf[i:] else len(buf)].decode("utf-8", "replace")

    symbol = None
    for idx, sec in enumerate(sections):
        if sec[1] != 2:  # SHT_SYMTAB
            continue
        strtab = section_data(sec[6])
        entries = section_data(idx)
        stride = sec[9]
        if not stride:
            raise ValueError("Invalid ELF symbol table")
        for pos in range(0, len(entries), stride):
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                "<IIIBBH", entries, pos
            )
            if cstr(strtab, st_name) == SYMBOL:
                symbol = (st_value, st_size, st_shndx)
                break
        if symbol:
            break

    if symbol is None:
        raise ValueError(f"Symbol {SYMBOL} not found (was it linked?)")
    address, size, shndx = symbol
    address &= ~1  # Thumb function pointers may have bit 0 set.
    if not (0x10000000 <= address < 0x11000000):
        raise ValueError(f"Function is not in Flash XIP: 0x{address:08x}")
    if (address + 14) % 8 != 0:
        raise ValueError(f"ADC START address has wrong phase: 0x{address+14:08x}")
    if size != len(EXPECTED):
        raise ValueError(f"Unexpected function size: {size}, expected {len(EXPECTED)}")
    if shndx >= len(sections):
        raise ValueError("Invalid function section index")
    sec = sections[shndx]
    section_addr, section_offset, section_size = sec[3], sec[4], sec[5]
    start = address - section_addr
    if start < 0 or start + size > section_size:
        raise ValueError("Function outside its ELF section")
    body = data[section_offset + start:section_offset + start + size]
    if body != EXPECTED:
        first_diff = next((i for i, (a, b) in enumerate(zip(body, EXPECTED)) if a != b), None)
        raise ValueError(f"Machine-code mismatch at function byte offset {first_diff}")
    return address


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} path/to/firmware.elf", file=sys.stderr)
        return 2
    try:
        address = read_elf(sys.argv[1])
    except (ValueError, OSError, struct.error) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: {SYMBOL} at Flash 0x{address:08x}")
    print(f"PASS: ADC START at 0x{address + 14:08x} (phase 0 mod 8)")
    print(f"PASS: all {len(EXPECTED)} instruction bytes match R3 START2 path A")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
