#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compare an STM32F103's identification registers with ST's documents.

Usage:
    identify.py --marking STM32F103C8T6 [--revision X]

Reads the identification registers through ST-LINK and OpenOCD without
halting, resetting or writing the target, and prints each one against
the value the documents give for the part named by --marking.  Each line
says "matches", "differs" or "info"; info lines have no documented value
to compare with.  Every line matching does not show that the chip is an
ST part: Bluepill Diagnostics lists compatible chips, CKS32F103C8T6 among
them, that read ST's DEV_ID and REV_ID.

--revision is the revision code from the marking (TN1433 shows where it
is); for medium-density parts it is compared with ES096 table 2.  The
SW-DP DPIDR is not read: OpenOCD's ST-LINK transport does not expose the
debug port.  drivers/swd.c prints it.

以 ST 的文档为准, 对照 STM32F103 的标识寄存器.

经 ST-LINK 与 OpenOCD 读取标识寄存器, 不暂停、不复位、不写入目标, 并把每个
读数与文档对 --marking 所给型号规定的值对照打印. 每行标明 "matches" (相符)、
"differs" (不符) 或 "info" (文档没有可对照的值). 全部相符不能说明芯片是 ST
的产品: Bluepill Diagnostics 列出的兼容芯片, 例如 CKS32F103C8T6, 读出与 ST
相同的 DEV_ID 与 REV_ID.

--revision 为丝印上的修订码 (位置见 TN1433); 中容量器件按 ES096 表 2 对照.
SW-DP 的 DPIDR 不读取: OpenOCD 的 ST-LINK 传输不开放调试端口, drivers/swd.c
可以输出该值.
"""

import argparse
import re
import subprocess
import sys
import tempfile

from stm32test import OPENOCD, TCL_PORT, OpenOCD, connect

CPUID_F1 = 0x411FC231           # PM0056 4.4.2, STM32F1 series
DBGMCU_IDCODE = 0xE0042000      # RM0008 31.6.1
CPUID = 0xE000ED00              # PM0056 4.4.2
F_SIZE = 0x1FFFF7E0             # RM0008 30.1.1
U_ID = 0x1FFFF7E8               # RM0008 30.2
ROM_TABLE = 0xE00FF000          # RM0008 31.6.4
JEP106_ST = (0, 0x20)           # JEP106AB bank 1, 0x20 SGS/Thomson

# flash code: (KiB, density, DEV_ID, source of the size)
# 容量代码: (KiB, 容量等级, DEV_ID, 容量的出处)
FLASH_CODES = {
    "4": (16, "low", 0x412, "DocID15060 Rev 7 section 7"),
    "6": (32, "low", 0x412, "DocID15060 Rev 7 section 7"),
    "8": (64, "medium", 0x410, "DS5319 Rev 20 section 7"),
    "B": (128, "medium", 0x410, "DS5319 Rev 20 section 7"),
}

# RM0008 31.6.1: REV_ID by density; RM0008 31.6.1 按容量等级列出的 REV_ID
REV_IDS = {
    "low": {0x1000: "A"},
    "medium": {0x0000: "A", 0x2000: "B", 0x2001: "Z", 0x2003: "Y"},
}

# ES096 Rev 15 table 2, STM32F103x8/B: marking code to REV_ID
ES096_MARKING = {"A": 0x0000, "B": 0x2000, "Z": 0x2001, "Y": 0x2003,
                 "X": 0x2003, "1": 0x2003, "2": 0x2003, "3": 0x2003}

MARKING_RE = re.compile(r"STM32F103([TCRV])([468B])([HITU])([67])")


class Probe(OpenOCD):
    """OpenOCD on ST-LINK, connected without loading a program.

    经 ST-LINK 连接的 OpenOCD, 不烧录程序.
    """

    def __init__(self):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log")
        self.proc = subprocess.Popen(
            OPENOCD + ["-c", f"tcl_port {TCL_PORT}",
                       "-c", "telnet_port disabled", "-c", "gdb_port disabled"],
            stdout=subprocess.DEVNULL, stderr=self.log)
        self.sock = connect(TCL_PORT)
        self.tcl("init")


def line(state, name, value, note):
    """Print one comparison; 打印一项对照. Returns True if it differs."""
    print(f"  {state:8s} {name:14s} {value:12s} {note}")
    return state == "differs"


def jep106(pidr):
    """Decode PIDR0..4 as Arm IHI 0029F B2.2; 按 IHI 0029F B2.2 解码."""
    p0, p1, p2, _, p4 = pidr
    identity = (p2 & 0x7) << 4 | p1 >> 4
    return p4 & 0xF, identity, (p1 & 0xF) << 8 | p0


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("--marking", required=True,
                    help="the part number on the chip, e.g. STM32F103C8T6")
    ap.add_argument("--revision", help="revision code on the marking")
    args = ap.parse_args()

    m = MARKING_RE.fullmatch(args.marking.upper())
    if not m:
        sys.exit(f"{args.marking}: not an STM32F103 part number this "
                 "script knows (DS5319, DocID15060 section 7)")
    kib, density, dev_id, size_src = FLASH_CODES[m.group(2)]

    probe = Probe()
    try:
        idcode = probe.words(DBGMCU_IDCODE, 1)[0]
        cpuid = probe.words(CPUID, 1)[0]
        fsize = probe.words(F_SIZE, 1)[0] & 0xFFFF
        uid = probe.words(U_ID, 3)
        pidr = [probe.words(ROM_TABLE + off, 1)[0] & 0xFF
                for off in (0xFE0, 0xFE4, 0xFE8, 0xFEC, 0xFD0)]
    finally:
        probe.close()

    print(f"{args.marking.upper()}: {kib} KiB flash, {density} density "
          f"({size_src})")
    bad = 0
    bad += line("matches" if cpuid == CPUID_F1 else "differs", "CPUID",
                f"{cpuid:#010x}", "PM0056 4.4.2: 0x411fc231, Cortex-M3 r1p1")

    dev, rev = idcode & 0xFFF, idcode >> 16
    if idcode == 0:
        line("info", "DBGMCU_IDCODE", "0x00000000",
             "see ES096 2.2.2; DEV_ID and REV_ID not compared")
    else:
        bad += line("matches" if dev == dev_id else "differs", "DEV_ID",
                    f"{dev:#05x}", f"RM0008 31.6.1: {dev_id:#05x} for "
                    f"{density} density")
        known = REV_IDS[density]
        name = known.get(rev)
        bad += line("matches" if name else "differs", "REV_ID",
                    f"{rev:#06x}", f"RM0008 31.6.1: revision {name}"
                    if name else "RM0008 31.6.1: not a listed revision")
        if args.revision and density == "medium":
            want = ES096_MARKING.get(args.revision.upper())
            bad += line("matches" if want == rev else "differs",
                        "revision code", args.revision.upper(),
                        "ES096 table 2: REV_ID "
                        + (f"{want:#06x}" if want is not None else "unknown"))
        elif args.revision:
            line("info", "revision code", args.revision.upper(),
                 "no low-density errata sheet in Documentation/references")

    bad += line("matches" if fsize == kib else "differs", "F_SIZE",
                f"{fsize} KiB", f"{size_src}: {kib} KiB")

    cont, ident, part = jep106(pidr)
    bad += line("matches" if (cont, ident) == JEP106_ST else "differs",
                "ROM table", f"JEP106 {cont}/{ident:#04x}",
                "JEP106AB: bank 1 code 0x20 is SGS/Thomson (ST)")
    line("info", "ROM part", f"{part:#05x}", "no documented value")
    line("info", "U_ID", "", " ".join(f"{w:08x}" for w in uid)
         + ", RM0008 30.2 gives no structure")

    print(f"{bad} of the documented values differ" if bad
          else "all documented values match")
    print("Matching values do not show an ST part: compatible chips can "
          "read the same identifiers.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
