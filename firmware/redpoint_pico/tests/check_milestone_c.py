"""A/B regressions plus Flash/UF2 reservation and XIP-safe code placement."""
from pathlib import Path
import json
import runpy
import struct

HERE = Path(__file__).resolve().parent
b = runpy.run_path(str(HERE / "check_milestone_b.py"))
state = b["state"]
addresses, elf, build = state["symbol_addresses"], state["elf"], state["build"]
start, reserve, end = 0x10000000, 0x10fff000, 0x11000000
assert addresses["__flash_binary_end"] <= reserve
assert addresses["__flash_binary_end"] == start + len((build / "redpoint_reva.bin").read_bytes())
header = state["header"]
for i in range(header[10]):
    kind, offset, virtual, physical, size, memsize, flags, align = struct.unpack_from("<8I", elf, header[5] + i * header[9])
    if kind == 1 and size and start <= physical < end:
        assert physical + size <= reserve, "Load segment overlaps config"
uf2 = (build / "redpoint_reva.uf2").read_bytes()
for offset in range(0, len(uf2), 512):
    addr, size = struct.unpack_from("<II", uf2, offset+12)
    assert start <= addr and addr + size <= reserve, "UF2 touches config sector"
for name in ("flash_range_erase", "flash_range_program"):
    assert 0x20000000 <= addresses[name] < 0x20042000
write = [addr for name, addr in addresses.items() if "writePage" in name]
assert len(write) == 1 and 0x20000000 <= write[0] < 0x20042000
assert not any("multicore_launch_core1" in name for name in addresses)
assert addresses["__StackTop"] - addresses["__StackBottom"] == 4096
layout = (build / "redpoint-layout/pico_flash_region.ld").read_text()
assert "16773120" in layout
defs = (build / "redpoint-layout/flash_layout.h").read_text()
assert "16777216u" in defs and "16773120u" in defs
commands = json.loads((build / "compile_commands.json").read_text())
flash = next(c for c in commands if c["file"].endswith("/flash_backend.cpp"))
assert "PICO_FLASH_SIZE_BYTES=16777216" in flash["command"]
assert "PICO_FLASH_ASSUME_CORE1_SAFE=1" in flash["command"]
print(f"PASS: 16 MiB physical Flash; config 0x{reserve:08X}..0x{end-1:08X}; ELF/UF2 excluded")
print("PASS: Flash callback/erase/program in SRAM; no core1 launch; 4096-byte main stack")
