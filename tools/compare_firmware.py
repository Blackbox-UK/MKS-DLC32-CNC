#!/usr/bin/env python3
"""Compare two MKS DLC32 firmware images and write a Markdown report.

Each input may be either a full 4 MB flash image (as Makerbase ships, flashed
at 0x0) or a bare application image (PlatformIO's firmware.bin, flashed at
0x10000).

A byte-for-byte match between a source build and the stock binary is not
expected: the published source targets the TS35 screen and is a newer
revision than the stock TS24 build. This report shows what does and does not
line up, so differences are visible before anything is flashed.

Usage: compare_firmware.py STOCK.bin BUILD.bin [-o report.md]
"""

import argparse
import hashlib
import re
import struct
import sys

FLASH_IMAGE_SIZE = 4 * 1024 * 1024
BOOTLOADER_OFFSET = 0x1000
PARTITION_OFFSET = 0x8000
APP_MAGIC = 0xE9

FLASH_MODES = {0: "QIO", 1: "QOUT", 2: "DIO", 3: "DOUT"}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB"}
FLASH_FREQS = {0x0: "40m", 0x1: "26m", 0x2: "20m", 0xF: "80m"}

# Strings that identify the build and its configuration.
IDENTITY_PATTERNS = {
    "Firmware name (FW_NAME)": rb"V\d\.\d[\d.]*\(8M\.H\d\d\.\d{8}\)",
    "Board name": rb"MKS DLC32 CNC V\d\.\d",
    "Grbl version build": rb"(?<![0-9])20\d{8}(?![0-9])",
    "ESP-IDF version": rb"v\d\.\d\.\d[-\w.]*-g[0-9a-f]{7,}",
    "SD config files": rb"/?\w+cfg\.txt",
}

# Grbl_Esp32 setting names, e.g. "Stepper/IdleTime", "GCode/LaserMode".
SETTING_NAME_RE = re.compile(
    rb"(?<![\w/])(?:Stepper|GCode|Report|Homing|Limits|Laser|Spindle|Coolant|"
    rb"Probe|Hard|Soft|Motor|Axis|Bluetooth|WiFi|Sta|AP|Telnet|HTTP|Notification|"
    rb"Config|Firmware|Hostname|Grbl)/[A-Za-z0-9/]+"
)


# Distinctive init-command bytes for TFT_eSPI panel drivers. The driver in an
# image is the one whose command set appears together in its init code.
DISPLAY_SIGNATURES = {
    "ST7789 (TS24)": {0xB2, 0xB7, 0xBB, 0xC2, 0xC3, 0xC4, 0xC6, 0xD0},
    "ST7796 (TS35)": {0xF0, 0xC3, 0x96, 0xA5, 0xE8, 0xC1, 0xC5},
    "ILI9341": {0xEF, 0xCF, 0xED, 0xE8, 0xCB, 0xF7, 0xEA},
}


def detect_display_driver(app):
    """Best-matching display driver, or None if capstone isn't installed."""
    try:
        import capstone
    except ImportError:
        return None
    if not hasattr(capstone, "CS_ARCH_XTENSA"):
        return None
    md = capstone.Cs(capstone.CS_ARCH_XTENSA, 0)
    imms = []
    pos = 24
    for _ in range(app[1]):
        addr, length = struct.unpack("<II", app[pos:pos + 8])
        code = app[pos + 8:pos + 8 + length]
        pos += 8 + length
        if not 0x400D0000 <= addr < 0x40400000:  # flash-mapped code only
            continue
        off = 0
        while off < len(code):
            end = None
            for ins in md.disasm(code[off:off + 4096], addr + off):
                end = ins.address + ins.size
                if ins.mnemonic in ("movi", "movi.n"):
                    try:
                        imms.append(int(ins.op_str.split(",")[-1], 0))
                    except ValueError:
                        pass
            off = (end - addr) if end else off + 1
    scores = {}
    for name, sig in DISPLAY_SIGNATURES.items():
        scores[name] = max((len(set(imms[k:k + 60]) & sig) / len(sig)
                            for k in range(0, len(imms), 4)), default=0)
    best = max(scores, key=scores.get)
    return best if scores[best] == 1 else "unknown"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def split_image(data):
    """Return (bootloader, partition_table, app) for a full or app-only image."""
    if len(data) == FLASH_IMAGE_SIZE and data[BOOTLOADER_OFFSET] == APP_MAGIC:
        return (
            data[BOOTLOADER_OFFSET:PARTITION_OFFSET],
            data[PARTITION_OFFSET:PARTITION_OFFSET + 0xC00],
            None,  # app located from the partition table below
        )
    if data[0] == APP_MAGIC:
        return None, None, data
    raise ValueError("not an ESP32 flash or app image")


def parse_partitions(table):
    parts = []
    for i in range(0, len(table), 32):
        entry = table[i:i + 32]
        if entry[:2] != b"\xaa\x50":
            break
        ptype, subtype, offset, size = struct.unpack("<BBII", entry[2:12])
        name = entry[12:28].rstrip(b"\0").decode(errors="replace")
        parts.append((name, ptype, subtype, offset, size))
    return parts


def app_length(app):
    """Length of an app image up to and including its checksum and hash."""
    segments = app[1]
    pos = 24
    for _ in range(segments):
        seg_len = struct.unpack("<I", app[pos + 4:pos + 8])[0]
        pos += 8 + seg_len
    pos += 1
    pos += (16 - pos % 16) % 16  # checksum padded to 16 bytes
    if app[23] == 1:  # hash appended
        pos += 32
    return pos


def analyse(path):
    data = open(path, "rb").read()
    boot, table, app = split_image(data)
    info = {"path": path, "file_size": len(data), "file_sha256": sha256(data)}
    if table is not None:
        parts = parse_partitions(table)
        info["partitions"] = parts
        info["bootloader_sha256"] = sha256(boot.rstrip(b"\xff"))
        app0 = next(p for p in parts if p[0] == "app0")
        app = data[app0[3]:app0[3] + app0[4]]
    else:
        info["partitions"] = None
        info["bootloader_sha256"] = None
    length = app_length(app)
    app = app[:length]
    info["app_size"] = length
    info["app_sha256"] = sha256(app)
    info["flash_mode"] = FLASH_MODES.get(app[2], hex(app[2]))
    info["flash_size"] = FLASH_SIZES.get(app[3] >> 4, hex(app[3] >> 4))
    info["flash_freq"] = FLASH_FREQS.get(app[3] & 0xF, hex(app[3] & 0xF))
    info["segments"] = app[1]
    info["display"] = detect_display_driver(app)
    ident = {}
    for label, pattern in IDENTITY_PATTERNS.items():
        found = sorted({m.decode(errors="replace") for m in re.findall(pattern, app)})
        ident[label] = found
    info["identity"] = ident
    info["settings"] = sorted({m.decode() for m in SETTING_NAME_RE.findall(app)})
    info["strings"] = {
        s.decode(errors="replace")
        for s in re.findall(rb"[\x20-\x7e]{8,}", app)
    }
    return info


def fmt_parts(parts):
    if parts is None:
        return "app image only (no partition table)"
    return ", ".join(f"{n} @0x{o:x} ({s // 1024} KB)" for n, _, _, o, s in parts)


def row(label, a, b):
    mark = "same" if a == b else "**differs**"
    return f"| {label} | {a} | {b} | {mark} |"


def report(stock, build):
    out = []
    out.append("# Firmware comparison\n")
    out.append(f"- Stock: `{stock['path']}`")
    out.append(f"- Build: `{build['path']}`\n")
    out.append("## Image\n")
    out.append("| Item | Stock | Build | Result |")
    out.append("| --- | --- | --- | --- |")
    out.append(row("App SHA-256", stock["app_sha256"][:16], build["app_sha256"][:16]))
    out.append(row("App size (bytes)", stock["app_size"], build["app_size"]))
    out.append(row("Flash mode (app header)", stock["flash_mode"], build["flash_mode"]))
    out.append(row("Flash size", stock["flash_size"], build["flash_size"]))
    out.append(row("Flash freq", stock["flash_freq"], build["flash_freq"]))
    out.append(row("Partition table", fmt_parts(stock["partitions"]), fmt_parts(build["partitions"])))
    if stock["bootloader_sha256"] and build["bootloader_sha256"]:
        out.append(row("Bootloader SHA-256", stock["bootloader_sha256"][:16], build["bootloader_sha256"][:16]))
    if stock["display"] is not None:
        out.append(row("Display driver", stock["display"], build["display"]))
    out.append("")
    out.append("## Identity strings\n")
    out.append("| Item | Stock | Build | Result |")
    out.append("| --- | --- | --- | --- |")
    for label in IDENTITY_PATTERNS:
        a = ", ".join(stock["identity"][label]) or "none"
        b = ", ".join(build["identity"][label]) or "none"
        out.append(row(label, a, b))
    out.append("")
    s_set, b_set = set(stock["settings"]), set(build["settings"])
    out.append("## Grbl setting names\n")
    out.append(f"{len(s_set & b_set)} shared, {len(s_set - b_set)} only in stock, "
               f"{len(b_set - s_set)} only in build.\n")
    if s_set - b_set:
        out.append("Only in stock: " + ", ".join(f"`{x}`" for x in sorted(s_set - b_set)) + "\n")
    if b_set - s_set:
        out.append("Only in build: " + ", ".join(f"`{x}`" for x in sorted(b_set - s_set)) + "\n")
    only_stock = sorted(stock["strings"] - build["strings"])
    only_build = sorted(build["strings"] - stock["strings"])
    shared = len(stock["strings"] & build["strings"])
    out.append("## Printable strings (8+ chars)\n")
    out.append(f"{shared} shared, {len(only_stock)} only in stock, {len(only_build)} only in build.\n")
    for title, items in (("Only in stock", only_stock), ("Only in build", only_build)):
        out.append(f"<details><summary>{title} ({len(items)})</summary>\n\n```")
        out.extend(items[:400])
        if len(items) > 400:
            out.append(f"... {len(items) - 400} more")
        out.append("```\n</details>\n")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stock")
    ap.add_argument("build")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    text = report(analyse(args.stock), analyse(args.build))
    if args.output:
        with open(args.output, "w") as f:
            f.write(text + "\n")
    else:
        sys.stdout.write(text + "\n")


if __name__ == "__main__":
    main()
