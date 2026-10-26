#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run a tests/stm32 program and check its records.

Usage:
    stm32test.py --qemu ELF          run under qemu-system-arm stm32vldiscovery
    stm32test.py --stlink ELF        flash and run on a board through ST-LINK

The suite is chosen by the ELF name: isa.elf is checked by isa_check.py,
periph.elf by periph_check.py.  signal.elf is not run here, see
signal_check.py.

The target writes records to SRAM and the host reads them through a
debugger: the GDB stub of QEMU, or the Tcl port of OpenOCD.

运行 tests/stm32 中的一个程序并检查其记录.

用法见上. 测试组按 ELF 文件名选择: isa.elf 由 isa_check.py 检查, periph.elf 由
periph_check.py 检查. signal.elf 不在此处运行, 见 signal_check.py.

目标把记录写入 SRAM, 主机经调试器读取: QEMU 的 GDB 桩, 或 OpenOCD 的 Tcl 端口.
"""

import argparse
import importlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# header and states, see runtime.h; 头部与状态, 见 runtime.h
HDR_ADDR = 0x20000100
REC_ADDR = HDR_ADDR + 16
REC_WORDS = 6
MAGIC, DONE = 0x15A7E500, 0x600DC0DE
PAGE_FULL, PAGE_ACK = 0xF011F011, 0xACACACAC

POLL_S = 0.05
CONNECT_TRIES = 100
QEMU_PORT = 1234
TCL_PORT = 6666
TIMEOUT_S = 60

OPENOCD = ["openocd", "-f", "interface/stlink.cfg", "-c", "set CPUTAPID 0",
           "-f", "target/stm32f1x.cfg"]


def connect(port):
    """Open a TCP connection to a local debugger that is starting up.

    连接一个正在启动的本地调试器.
    """
    for _ in range(CONNECT_TRIES):
        try:
            return socket.create_connection(("127.0.0.1", port))
        except ConnectionRefusedError:
            time.sleep(POLL_S)
    sys.exit(f"no debugger on port {port}")


class Qemu:
    """qemu-system-arm with its GDB stub, spoken as GDB remote protocol.

    The stub reads memory only while the CPU is stopped, so every access
    stops the CPU and run() starts it again.

    带 GDB 桩的 qemu-system-arm, 以 GDB 远程协议通信. 该桩只在 CPU 停止时读取
    内存, 因此每次访问都停止 CPU, 由 run() 重新启动.
    """

    def __init__(self, elf, qemu="qemu-system-arm"):
        self.proc = subprocess.Popen(
            [qemu, "-machine", "stm32vldiscovery", "-kernel", str(elf),
             "-nographic", "-monitor", "none", "-serial", "null",
             "-S", "-gdb", f"tcp::{QEMU_PORT}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.sock = connect(QEMU_PORT)
        self.running = False
        self.voltage = None

    def _send(self, data):
        cs = sum(data.encode()) & 0xFF
        self.sock.sendall(f"${data}#{cs:02x}".encode())
        if self.sock.recv(1) != b"+":
            raise ConnectionError("gdb stub did not acknowledge")

    def _recv(self):
        buf = b""
        while not re.search(rb"\$[^#]*#..", buf):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("gdb stub closed")
            buf += chunk
        self.sock.sendall(b"+")
        m = re.search(rb"\$([^#]*)#", buf)
        assert m
        return m.group(1).decode()

    def _stop(self):
        if self.running:
            self.sock.sendall(b"\x03")
            self._recv()
            self.running = False

    def run(self):
        if not self.running:
            self.sock.sendall(b"$c#63")
            if self.sock.recv(1) != b"+":
                raise ConnectionError("gdb stub did not acknowledge")
            self.running = True

    def words(self, addr, n):
        self._stop()
        data = b""
        chunk = 1024
        while len(data) < 4 * n:
            size = min(chunk, 4 * n - len(data))
            self._send(f"m{addr + len(data):x},{size:x}")
            reply = self._recv()
            if reply.startswith("E"):
                raise RuntimeError(f"read {addr:#x}: {reply}")
            data += bytes.fromhex(reply)
        return list(struct.unpack(f"<{n}I", data))

    def write(self, addr, value):
        self._stop()
        self._send(f"M{addr:x},4:{struct.pack('<I', value).hex()}")
        if self._recv() != "OK":
            raise RuntimeError(f"write {addr:#x} failed")

    def close(self):
        self.proc.kill()
        self.proc.wait()


class OpenOCD:
    """A board behind ST-LINK, driven through the Tcl port of OpenOCD.

    OpenOCD reads memory while the core runs, so run() only resumes once.

    经 OpenOCD 的 Tcl 端口操作 ST-LINK 所连的板子. OpenOCD 可在内核运行时读取
    内存, 因此 run() 只需恢复运行一次.
    """

    def __init__(self, elf):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log")
        # stderr goes to a file: an unread pipe fills and stalls OpenOCD
        # stderr 写入文件: 无人读取的管道写满后会使 OpenOCD 停住
        self.proc = subprocess.Popen(
            OPENOCD + ["-c", f"tcl_port {TCL_PORT}",
                       "-c", "telnet_port disabled", "-c", "gdb_port disabled"],
            stdout=subprocess.DEVNULL, stderr=self.log)
        self.sock = connect(TCL_PORT)
        self.tcl("init")
        self.tcl("halt")
        self.tcl(f"program {elf} verify")
        self.log.seek(0)
        text = self.log.read()
        if "Verified OK" not in text:
            sys.exit("openocd: program or verify failed\n" + text[-2000:])
        m = re.search(r"Target voltage: ([0-9.]+)", text)
        self.voltage = float(m.group(1)) if m else None
        self.started = False

    def tcl(self, cmd):
        self.sock.sendall(cmd.encode() + b"\x1a")
        buf = b""
        while not buf.endswith(b"\x1a"):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("openocd closed the Tcl port")
            buf += chunk
        return buf[:-1].decode()

    def run(self):
        if not self.started:
            self.tcl("reset run")
            self.started = True

    def words(self, addr, n):
        return [int(w, 16) for w in self.tcl(
            f"read_memory {addr:#x} 32 {n}").split()]

    def write(self, addr, value):
        self.tcl(f"mww {addr:#x} {value:#x}")

    def close(self):
        self.proc.terminate()
        self.proc.wait(timeout=5)
        self.log.close()


def collect(target):
    """Run the program to DONE and return all of its records.

    运行程序直到 DONE, 返回其全部记录.

    Returns:
        (records, faults): a list of six-word tuples and the number of
        faults the target took. 六字元组的列表, 以及目标触发的故障数.
    """
    records = []
    deadline = time.monotonic() + TIMEOUT_S
    state = None
    while time.monotonic() < deadline:
        target.run()
        time.sleep(POLL_S)
        magic, state, count, faults = target.words(HDR_ADDR, 4)
        if magic != MAGIC or state not in (PAGE_FULL, DONE):
            continue
        if count:
            page = target.words(REC_ADDR, count * REC_WORDS)
            records += [tuple(page[i:i + REC_WORDS])
                        for i in range(0, len(page), REC_WORDS)]
        if state == DONE:
            return records, faults
        target.write(HDR_ADDR + 4, PAGE_ACK)
    sys.exit(f"target did not finish in {TIMEOUT_S} s, state {state}")


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--qemu", action="store_true")
    where.add_argument("--stlink", action="store_true")
    ap.add_argument("--qemu-bin", default="qemu-system-arm")
    ap.add_argument("elf", type=Path)
    args = ap.parse_args()

    suite = args.elf.stem
    if suite == "signal":
        sys.exit("signal.elf needs a frequency counter, run signal_check.py")
    try:
        checker = importlib.import_module(f"{suite}_check")
    except ModuleNotFoundError:
        sys.exit(f"no checker for {args.elf.name}")
    if args.qemu and not getattr(checker, "QEMU", True):
        sys.exit(f"{args.elf.name} needs a board: QEMU does not model "
                 "the peripherals it tests")

    target = (Qemu(args.elf, args.qemu_bin) if args.qemu
              else OpenOCD(args.elf.resolve()))
    try:
        records, faults = collect(target)
    finally:
        target.close()
    where = "qemu stm32vldiscovery" if args.qemu else "st-link"
    print(f"{args.elf.name} on {where}: {len(records)} records, "
          f"{faults} faults taken")
    bad = checker.check(records, voltage=target.voltage, qemu=args.qemu)
    print(f"{bad} failures" if bad else "all passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
