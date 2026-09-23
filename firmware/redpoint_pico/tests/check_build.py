"""Inspect descriptors in the linked ARM ELF, plus dependency and UF2 provenance.

Usage: python firmware/redpoint_pico/tests/check_build.py [build-directory]
Only Python's standard library is required. No USB access.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
elf = (build / "redpoint_reva.elf").read_bytes()
assert elf[:6] == b"\x7fELF\x01\x01", "Expected little-endian ELF32"
header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
sections = [struct.unpack_from("<IIIIIIIIII", elf, header[6] + i * header[11])
            for i in range(header[12])]
symbols = {}
symbol_addresses = {}
for section in sections:
    if section[1] != 2:  # SHT_SYMTAB
        continue
    strings_section = sections[section[6]]
    strings = elf[strings_section[4]:strings_section[4] + strings_section[5]]
    for pos in range(section[4], section[4] + section[5], section[9]):
        name, addr, size, _, _, index = struct.unpack_from("<IIIBBH", elf, pos)
        name = strings[name:strings.find(b"\0", name)].decode()
        if name and index:
            symbol_addresses[name] = addr
        if not size or not 0 < index < len(sections):
            continue
        target = sections[index]
        offset = target[4] + addr - target[3]
        symbols[name] = elf[offset:offset + size]

def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]

device = symbols["desc_device"]
assert len(device) == 18 and u16(device, 2) == 0x0201
assert device[4:8] == bytes([0xef, 2, 1, 64])
assert (u16(device, 8), u16(device, 10), device[17]) == (0xcafe, 0x4019, 1)
config = symbols["desc_configuration"]
assert len(config) == u16(config, 2) == 210
assert config[4:9] == bytes([6, 1, 0, 0x80, 50])
expected = {
    (0, 0): [(0x81, 3, 64)],
    (1, 0): [], (1, 1): [(0x02, 2, 64), (0x82, 2, 64)],
    (2, 0): [(0x83, 3, 8)],
    (3, 0): [(0x04, 2, 64), (0x84, 2, 64)],
    (4, 0): [(0x85, 3, 8)], (5, 0): [(0x86, 3, 8)],
}
actual, counts, classes, hids, iads, owners, unions = {}, {}, {}, {}, [], {}, []
offset = 0
while offset < len(config):
    length, kind = config[offset:offset+2]
    assert length >= 2 and offset + length <= len(config)
    d = config[offset:offset+length]
    if kind == 4:
        current = (d[2], d[3])
        assert current not in actual
        actual[current], counts[current], classes[current] = [], d[4], tuple(d[5:8])
    elif kind == 5:
        ep = d[2]
        assert 0 < (ep & 15) < 16 and not ep & 0x70
        assert ep not in owners or owners[ep] == current[0], "Endpoint address collision"
        owners[ep] = current[0]
        actual[current].append((ep, d[3] & 3, u16(d, 4)))
        if (d[3] & 3) == 3:
            assert d[6] == {0x81: 50, 0x83: 1, 0x85: 10, 0x86: 10}[ep]
    elif kind == 0x0b:
        iads.append(tuple(d[2:7]))
    elif kind == 0x21:
        assert d[5:7] == bytes([1, 0x22])
        hids[current[0]] = u16(d, 7)
    elif kind == 0x24 and d[2] == 6:
        unions.append(tuple(d[3:5]))
    offset += length
assert {k: sorted(v) for k, v in actual.items()} == {k: sorted(v) for k, v in expected.items()}, actual
assert all(counts[k] == len(v) for k, v in actual.items())
assert iads == [(0, 2, 2, 13, 0), (2, 2, 2, 2, 0)]
assert unions == [(0, 1), (2, 3)]
assert classes[(0, 0)] == (2, 13, 0) and classes[(2, 0)] == (2, 2, 0)
assert classes[(4, 0)] == (3, 1, 2) and classes[(5, 0)] == (3, 1, 1)

def report_bits(report):
    """Walk HID short items and count input/output bits; no report IDs here."""
    offset = count = size = 0
    bits = {8: 0, 9: 0}  # Input, Output main tags
    while offset < len(report):
        prefix = report[offset]
        assert prefix != 0xfe
        length = (0, 1, 2, 4)[prefix & 3]
        value = int.from_bytes(report[offset+1:offset+1+length], "little")
        kind, tag = (prefix >> 2) & 3, prefix >> 4
        if kind == 1:
            assert tag != 8, "Unexpected Report ID"
            if tag == 7: size = value
            if tag == 9: count = value
        elif kind == 0 and tag in bits:
            bits[tag] += size * count
        offset += length + 1
    return bits

mouse = symbols["desc_mouse_report"]
keyboard = symbols["desc_keyboard_report"]
assert hids == {4: len(mouse), 5: len(keyboard)}
assert report_bits(mouse)[8] == 40
assert report_bits(keyboard) == {8: 64, 9: 8}
bos, ms = symbols["desc_bos"], symbols["desc_ms_os_20"]
assert len(bos) == u16(bos, 2) == 33 and bos[4] == 1
assert u16(bos, 29) == 178 and bos[31] == 1
assert len(ms) == u16(ms, 8) == 178
assert ms[14] == 0 and u16(ms, 16) == 168  # configuration index
assert ms[22] == 0 and u16(ms, 24) == 160  # NCM interface only
assert ms[30:38] == b"WINNCM\0\0"
assert all(name in symbols for name in ["netd_init", "cdcd_init", "hidd_init"])
print("PASS: ELF configuration 210 B, six interfaces, eight unique endpoint addresses")
print("PASS: boot mouse/keyboard, 5 B/8 B input reports, 1 B keyboard output")
print("PASS: BOS and Windows WINNCM function scope = interface 0")

commands = json.loads((build / "compile_commands.json").read_text())
ncm = [Path(c["file"]).resolve() for c in commands if Path(c["file"]).name == "ncm_device.c"]
reference = json.loads((ROOT / "reference.json").read_text())
cache = (build / "CMakeCache.txt").read_text()
tinyusb = next(line.split("=", 1)[1] for line in cache.splitlines()
               if line.startswith("REDPOINT_TINYUSB_PATH:PATH="))
assert ncm == [(Path(tinyusb) / "src/class/net/ncm_device.c").resolve()]
for name, digest in reference["sha256"].items():
    assert hashlib.sha256((Path(tinyusb) / name).read_bytes()).hexdigest() == digest, name
print("PASS: external current NCM source selected; recorded reference hashes unchanged")

uf2 = (build / "redpoint_reva.uf2").read_bytes()
binary = (build / "redpoint_reva.bin").read_bytes()
assert len(uf2) % 512 == 0
payload = bytearray()
for index in range(len(uf2) // 512):
    block = uf2[index*512:(index+1)*512]
    a, b, flags, addr, size, num, total, family = struct.unpack_from("<8I", block)
    assert (a, b) == (0x0a324655, 0x9e5d5157)
    assert flags & 0x2000 and family == 0xe48bff56  # RP2040
    assert num == index and total == len(uf2)//512 and size == 256
    assert addr == 0x10000000 + index*256
    assert struct.unpack_from("<I", block, 508)[0] == 0x0ab16f30
    payload.extend(block[32:32+size])
assert payload[:len(binary)] == binary
print(f"PASS: RP2040 UF2 payload matches BIN ({len(binary)} B); UF2 {len(uf2)} B")
