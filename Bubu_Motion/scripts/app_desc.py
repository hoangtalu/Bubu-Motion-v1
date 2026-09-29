#!/usr/bin/env python3
"""Print the esp_app_desc_t of a built app image, or of one read back off a device.

Exists because this project now has several images carrying the SAME version
string (1.7.8 on OTA and 1.7.8 locally are different binaries). The ELF SHA-256
in the descriptor is the only field that actually identifies a build, so this is
how you answer "what is really running on that device".

    python3 scripts/app_desc.py build/bubu.bin
    esptool.py -p PORT read_flash 0x20000 0x100 /tmp/onchip.bin
    python3 scripts/app_desc.py /tmp/onchip.bin
"""
import sys

FIELDS = [("version", 0x30, 32), ("project", 0x50, 32),
          ("time", 0x70, 16), ("date", 0x80, 16), ("idf_ver", 0x90, 32)]


def main(path: str) -> int:
    with open(path, "rb") as f:
        d = f.read(0x100)
    if len(d) < 0xD0:
        print(f"{path}: too short ({len(d)} B); need at least 0xD0")
        return 1
    magic = int.from_bytes(d[0x20:0x24], "little")
    if magic != 0xABCD5432:
        print(f"{path}: no app descriptor (magic {magic:#x})")
        return 1
    print(path)
    for name, off, size in FIELDS:
        print(f"  {name:9} {d[off:off + size].split(chr(0).encode())[0].decode(errors='replace')}")
    print(f"  elf_sha256 {d[0xB0:0xD0].hex()}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(max(main(p) for p in sys.argv[1:]))
