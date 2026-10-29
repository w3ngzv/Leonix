#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Boot the STM32 build of Leonix on qemu-system-arm and drive its shell.

qemu_boot.py [--qemu QEMU] ELF

The machine is stm32vldiscovery, whose STM32F100RB has the same core,
USART1 and SysTick as the F103 but 8 KiB of SRAM; arch/arm/leonix.lds
keeps the kernel within that.  QEMU ignores writes to Flash, so lxfs runs
from RAM and the driver logs "lxfs in RAM".  Each command's output is
checked, then ps reports the free bytes of each stack.  Ctrl-C is then
sent to a program blocked reading the terminal, to one that never reads,
and at the prompt.

在 qemu-system-arm 上启动 Leonix 的 STM32 构建, 并驱动其 shell.

所用机器为 stm32vldiscovery, 其 STM32F100RB 的内核、USART1 与 SysTick 与 F103
相同, 但只有 8 KiB SRAM; arch/arm/leonix.lds 使内核保持在该范围之内. QEMU 忽略
对 Flash 的写入, 因此 lxfs 在 RAM 中运行, 驱动记录 "lxfs in RAM". 逐条检查各
命令的输出, 最后由 ps 报告每个栈的空闲字节数. 随后向阻塞在终端读取中的程序、
从不读取的程序以及提示符处发送 Ctrl-C.
"""
import argparse
import os
import select
import subprocess
import sys
import time

PROMPT = b"# "
BOOT_TIMEOUT_S = 10
COMMAND_TIMEOUT_S = 5

# command, text its output must contain
# 命令, 以及其输出必须包含的文本
SESSION = [
    ("echo hello > /f", ""),
    ("cat /f", "hello"),
    ("echo again >> /f", ""),
    ("cat /f", "hello\nagain"),
    ("ls /", "f"),
    ("ls /bin", "sh"),
    ("rm /f", ""),
    ("cat /f", ""),
    ("df", ""),
    ("uptime", ""),
    ("dmesg", "Leonix booted"),
    ("ps", "ID S FREE"),
]

CTRL_C = b"\x03"
# time for a program to start before Ctrl-C is sent; 发送 Ctrl-C 之前留给程序启动的时间
START_DELAY_S = 0.5
# header "LX", version 1, no flags, no data, then jmp 0
# 文件头 "LX"、版本 1、无标志、无数据, 然后是 jmp 0
LOOP_HEX = "4c5801000000400000"

# what is typed after the command line, before Ctrl-C
# 命令行之后、Ctrl-C 之前键入的内容
INTERRUPTS = [
    ("unhex", b""),
    ("/loop", b""),
    ("/loop", b"xyz"),
]


class Console:
    """USART1 of the emulated board, on QEMU's stdin and stdout."""

    def __init__(self, qemu, elf):
        self.proc = subprocess.Popen(
            [qemu, "-M", "stm32vldiscovery", "-kernel", elf, "-nographic",
             "-monitor", "none", "-serial", "stdio"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        assert self.proc.stdin and self.proc.stdout
        self.stdin = self.proc.stdin
        self.fd = self.proc.stdout.fileno()

    def read_until(self, mark, timeout_s):
        """Bytes up to and including the next @mark, or None on timeout."""
        buf = b""
        deadline = time.monotonic() + timeout_s
        while not buf.endswith(mark):
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([self.fd], [], [], left)[0]:
                return None
            chunk = os.read(self.fd, 1)
            if not chunk:
                return None
            buf += chunk
        return buf

    def run(self, line):
        """Type @line and return what the shell printed before its prompt."""
        self.stdin.write(line.encode() + b"\r")
        self.stdin.flush()
        out = self.read_until(PROMPT, COMMAND_TIMEOUT_S)
        if out is None:
            return None
        text = out[:-len(PROMPT)].decode(errors="replace").replace("\r", "")
        # drop the echoed command line; 去掉回显的命令行
        return text.split("\n", 1)[1] if "\n" in text else ""

    def interrupt(self, line, typed):
        """Run @line, type @typed and Ctrl-C; return the output up to the prompt."""
        self.stdin.write(line.encode() + b"\r")
        self.stdin.flush()
        time.sleep(START_DELAY_S)
        self.stdin.write(typed + CTRL_C)
        self.stdin.flush()
        out = self.read_until(PROMPT, COMMAND_TIMEOUT_S)
        if out is None:
            return None
        return out[:-len(PROMPT)].decode(errors="replace").replace("\r", "")

    def close(self):
        self.proc.kill()
        self.proc.wait()


def check_interrupts(con):
    """Ctrl-C stops each program and the shell goes on; returns the failures."""
    fails = 0
    # unhex prints no prompt between lines; unhex 在各行之间不输出提示符
    con.stdin.write(f"unhex > /loop\r{LOOP_HEX}\r.\r".encode())
    con.stdin.flush()
    if con.read_until(PROMPT, COMMAND_TIMEOUT_S) is None:
        print("FAIL unhex > /loop: no prompt")
        return 1
    for line, typed in INTERRUPTS:
        out = con.interrupt(line, typed)
        print(f"# {line} {typed!r} ^C\n{out}", end="")
        if out is None or not out.endswith("^C\n"):
            print(f"FAIL {line!r}: no ^C before the prompt")
            fails += 1
            continue
        # the shell still runs, and nothing typed before Ctrl-C reached it
        # shell 仍在运行, Ctrl-C 之前键入的内容没有交给 shell
        out = con.run("echo ok")
        print(f"# echo ok\n{out}", end="")
        if out != "ok\n":
            print(f"FAIL after {line!r}: {out!r}")
            fails += 1
    con.stdin.write(b"abc" + CTRL_C)
    con.stdin.flush()
    out = con.read_until(PROMPT, COMMAND_TIMEOUT_S)
    if out is None or b"abc^C" not in out:
        print(f"FAIL abc ^C at the prompt: {out!r}")
        fails += 1
    return fails


def main():
    ap = argparse.ArgumentParser(description="Boot Leonix on QEMU's stm32vldiscovery")
    ap.add_argument("--qemu", default="qemu-system-arm")
    ap.add_argument("elf")
    args = ap.parse_args()

    con = Console(args.qemu, args.elf)
    fails = 0
    try:
        boot = con.read_until(PROMPT, BOOT_TIMEOUT_S)
        if boot is None:
            sys.exit(f"{args.elf}: no shell prompt after {BOOT_TIMEOUT_S} s")
        print(boot.decode(errors="replace").replace("\r", ""), end="")
        print()
        for line, want in SESSION:
            out = con.run(line)
            if out is None:
                print(f"FAIL {line!r}: no prompt after {COMMAND_TIMEOUT_S} s")
                fails += 1
                break
            print(f"# {line}\n{out}", end="")
            if want not in out:
                print(f"FAIL {line!r}: {want!r} not in output")
                fails += 1
            if "panic" in out:
                print(f"FAIL {line!r}: panic")
                fails += 1
        if not fails:
            fails += check_interrupts(con)
    finally:
        con.close()
    if fails:
        sys.exit(f"qemu_boot: {fails} failed")
    print("qemu_boot: all passed")


if __name__ == "__main__":
    main()
