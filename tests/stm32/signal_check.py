#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Time the square waves of signal.c with a Leonardo running Leonix.

Usage: signal_check.py --leonix /dev/cu.usbmodemNNNN --pin PA8 signal.elf

The STM32 is flashed and released through ST-LINK; the wave is timed by
"echo f > /dev/dap" on the Leonardo, which counts FREQ_PERIODS periods on
D13 with Timer3 at 16 MHz (drivers/swd.c).  Wire the named pin to D13 and
GND to GND, and leave D8 and D9 unconnected while ST-LINK drives SWD.

以运行 Leonix 的 Leonardo 测量 signal.c 输出的方波.

STM32 经 ST-LINK 烧录与释放; 方波由 Leonardo 上的 "echo f > /dev/dap" 计时,
该命令以 16 MHz 的 Timer3 计 D13 上 FREQ_PERIODS 个周期 (drivers/swd.c). 把所
指定的引脚接到 D13, GND 接 GND; ST-LINK 驱动 SWD 期间 D8 与 D9 不得连接.
"""

import argparse
import os
import select
import sys
import termios
import time
from pathlib import Path

from stm32test import DONE, HDR_ADDR, MAGIC, REC_ADDR, REC_WORDS, OpenOCD

SIG_MODE = 0x20001C00
S_READY = 0x300
LEONARDO_HZ = 16_000_000
FREQ_PERIODS = 64           # drivers/swd.c
PROMPT = b"# "
SHELL_TIMEOUT_S = 10.0
SETTLE_S = 0.3

# mode, pin, what it shows, allowed deviation in percent and its source
# 模式, 引脚, 所检查的内容, 允许的偏差 (百分比) 及其出处
MODES = [
    (1, "PA8", "HSI", (-1.3, 2.0), "DS5319 table 24, 0-70 C"),
    (2, "PA8", "HSE crystal", (-0.1, 0.1), "two crystals, no table"),
    (3, "PB6", "PLL x9, TIM4 on APB1 / 2", (-0.1, 0.1),
     "RM0008 7.2: timer clock x2 when APB1 is divided"),
    (4, "PA9", "USART1 at 1200 Bd on HSI", (-1.3, 2.0),
     "follows HSI, DS5319 table 24"),
]


class Leonix:
    """The Leonix shell on the Leonardo's USB serial port.

    Leonardo USB 串口上的 Leonix shell.
    """

    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attr = termios.tcgetattr(self.fd)
        attr[0] = attr[1] = attr[3] = 0
        attr[2] |= termios.CREAD | termios.CLOCAL
        termios.tcsetattr(self.fd, termios.TCSANOW, attr)
        self.sh("")

    def sh(self, line):
        """Run one shell line and return its output lines.

        运行一行 shell 命令, 返回输出的各行.
        """
        os.write(self.fd, line.encode() + b"\r")
        out = b""
        end = time.monotonic() + SHELL_TIMEOUT_S
        while not out.endswith(PROMPT):
            if time.monotonic() > end:
                raise TimeoutError(repr(out))
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    out += os.read(self.fd, 512)
                except BlockingIOError:
                    pass
        text = out[:-len(PROMPT)].decode(errors="replace").replace("\r", "")
        # drop the echo and kernel log lines; 去掉回显与内核日志行
        return [ln for ln in text.split("\n")[1:]
                if ln and not ln.startswith("[")]

    def freq_sum(self):
        self.sh("echo f > /dev/dap")
        out = self.sh("cat /dev/dap")
        if not out or not out[0].startswith("F "):
            raise RuntimeError(f"/dev/dap: {out}")
        return int(out[0].split()[1])


def start_mode(board, mode):
    """Reset the STM32 into @mode and return its S_READY record.

    以 @mode 复位 STM32, 返回其 S_READY 记录.
    """
    board.tcl("reset halt")
    board.write(SIG_MODE, mode)
    board.write(HDR_ADDR + 4, 0)
    board.tcl("resume")
    deadline = time.monotonic() + SHELL_TIMEOUT_S
    while time.monotonic() < deadline:
        magic, state, count, _ = board.words(HDR_ADDR, 4)
        if magic == MAGIC and state == DONE and count == 1:
            rec = board.words(REC_ADDR, REC_WORDS)
            if rec[0] == S_READY:
                return rec
        time.sleep(0.05)
    raise RuntimeError(f"mode {mode} did not start")


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("--leonix", required=True, help="Leonardo serial port")
    ap.add_argument("--pin", required=True, choices=["PA8", "PB6", "PA9"])
    ap.add_argument("elf", type=Path)
    args = ap.parse_args()

    leonix = Leonix(args.leonix)
    board = OpenOCD(args.elf.resolve())
    bad = 0
    try:
        for mode, pin, what, (lo_pct, hi_pct), source in MODES:
            if pin != args.pin:
                continue
            _, _, ok, clk, div, _ = start_mode(board, mode)
            if not ok:
                print(f"  FAIL  mode {mode} {what}: clock did not start")
                bad += 1
                continue
            time.sleep(SETTLE_S)
            total = leonix.freq_sum()
            hz = FREQ_PERIODS * LEONARDO_HZ / total
            want = clk / div
            pct = (hz / want - 1) * 100
            good = lo_pct <= pct <= hi_pct
            bad += not good
            print(f"  {'pass' if good else 'FAIL'}  mode {mode} {pin} "
                  f"{what}: {hz:.3f} Hz, nominal {want:.3f} Hz, "
                  f"{pct:+.4f} % (limit {lo_pct} to +{hi_pct} %, {source})")
    finally:
        board.close()
    print(f"{bad} failures" if bad else "all passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
