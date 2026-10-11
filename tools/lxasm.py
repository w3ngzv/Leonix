#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Assembler for the Leonix virtual machine.

Usage: lxasm.py input.s output

Source syntax, one statement per line, ';' to the end of a line is a
comment:

    label:              a code address
    .equ NAME value     a constant
    .data NAME size     NAME is the address of size bytes of data memory
    .str  "text"        bytes in the code, no terminator added; \\n \\0 \\\\ \\"
    .byte n, ...        bytes in the code
    .word n, ...        16-bit words in the code
    mnemonic [operand]

An operand is a number (decimal, 0x hex, 'c'), a label, a .equ or a .data
name, optionally +/- a number.  "lit" assembles to LIT8 for a number that
fits a signed byte and to LIT16 otherwise, labels always to LIT16 so that
the first pass knows every size.  "sys" takes a system call name.

The opcodes, header layout and system call numbers must match
include/leonix/vm.h and include/leonix/syscall.h.
"""

import re
import struct
import sys

MAGIC = b"LX"
VERSION = 1
DATA_BASE = 0x8000
DATA_MAX = 160

OPS = {
    "nop": (0x00, 0), "lit8": (0x01, 1), "lit16": (0x02, 2),
    "dup": (0x03, 0), "drop": (0x04, 0), "swap": (0x05, 0),
    "over": (0x06, 0), "rot": (0x07, 0), "pick": (0x08, 1),
    "add": (0x10, 0), "sub": (0x11, 0), "mul": (0x12, 0),
    "divu": (0x13, 0), "modu": (0x14, 0), "and": (0x15, 0),
    "or": (0x16, 0), "xor": (0x17, 0), "shl": (0x18, 0),
    "shr": (0x19, 0), "not": (0x1A, 0), "neg": (0x1B, 0),
    "eq": (0x20, 0), "lt": (0x21, 0), "ult": (0x22, 0), "eqz": (0x23, 0),
    "ldb": (0x30, 0), "stb": (0x31, 0), "ldw": (0x32, 0), "stw": (0x33, 0),
    "jmp": (0x40, 2), "jz": (0x41, 2), "jnz": (0x42, 2), "call": (0x43, 2),
    "ret": (0x44, 0), ">r": (0x45, 0), "r>": (0x46, 0), "r@": (0x47, 0),
    "sys": (0x50, 1),
}

SYSCALLS = {
    "exit": 0, "read": 1, "write": 2, "open": 3, "close": 4,
    "readdir": 5, "unlink": 6, "exec": 7, "dup2": 8, "statfs": 9,
}


class AsmError(Exception):
    pass


def unescape(text):
    out = bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\":
            i += 1
            esc = text[i]
            out += {"n": b"\n", "0": b"\0", "\\": b"\\", '"': b'"',
                    "t": b"\t"}.get(esc) or esc.encode()
        else:
            out += c.encode()
        i += 1
    return bytes(out)


def split_operands(text):
    return [part.strip() for part in text.split(",") if part.strip()]


def is_number(token):
    return re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)|'(?:[^\\]|\\.)'", token) is not None


def number(token):
    if token.startswith("'"):
        return unescape(token[1:-1])[0]
    return int(token, 0)


class Assembler:
    def __init__(self):
        self.symbols = {}
        self.data_size = 0

    def value(self, expr, lineno):
        match = re.fullmatch(r"\s*([^\s+-][^\s+]*?)\s*(?:([+-])\s*(\S+))?\s*",
                             expr)
        if not match:
            raise AsmError(f"line {lineno}: bad operand {expr!r}")
        base, sign, offset = match.groups()
        if is_number(base):
            val = number(base)
        elif base in self.symbols:
            val = self.symbols[base]
        else:
            raise AsmError(f"line {lineno}: unknown name {base!r}")
        if sign:
            val += number(offset) if sign == "+" else -number(offset)
        return val

    def statements(self, source):
        for lineno, line in enumerate(source.splitlines(), 1):
            line = re.sub(r";(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$).*", "", line)
            line = line.strip()
            while True:
                match = re.match(r"([A-Za-z_][\w.]*):\s*(.*)", line)
                if not match:
                    break
                yield lineno, "label", match.group(1), ""
                line = match.group(2)
            if line:
                word, _, rest = line.partition(" ")
                yield lineno, word.lower(), rest.strip(), line

    def size(self, word, rest, lineno):
        if word in (".str",):
            return len(unescape(self.string(rest, lineno)))
        if word == ".byte":
            return len(split_operands(rest))
        if word == ".word":
            return 2 * len(split_operands(rest))
        if word == "lit":
            return 2 if is_number(rest) and -128 <= number(rest) < 128 else 3
        if word in OPS:
            return 1 + OPS[word][1]
        raise AsmError(f"line {lineno}: unknown instruction {word!r}")

    def string(self, rest, lineno):
        match = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', rest)
        if not match:
            raise AsmError(f"line {lineno}: bad string {rest!r}")
        return match.group(1)

    def assemble(self, source):
        pc = 0
        for lineno, word, rest, _ in self.statements(source):
            if word == "label":
                if rest in self.symbols:
                    raise AsmError(f"line {lineno}: {rest} defined twice")
                self.symbols[rest] = pc
            elif word == ".equ":
                name, val = rest.split(None, 1)
                self.symbols[name] = self.value(val, lineno)
            elif word == ".data":
                name, size = rest.split(None, 1)
                self.symbols[name] = DATA_BASE + self.data_size
                self.data_size += self.value(size, lineno)
            else:
                pc += self.size(word, rest, lineno)
        if self.data_size > DATA_MAX:
            raise AsmError(f"{self.data_size} bytes of data, at most {DATA_MAX}")

        code = bytearray()
        for lineno, word, rest, _ in self.statements(source):
            if word in ("label", ".equ", ".data"):
                continue
            if word == ".str":
                code += unescape(self.string(rest, lineno))
            elif word == ".byte":
                code += bytes(self.value(v, lineno) & 0xFF
                              for v in split_operands(rest))
            elif word == ".word":
                for v in split_operands(rest):
                    code += struct.pack("<H", self.value(v, lineno) & 0xFFFF)
            elif word == "lit":
                if is_number(rest) and -128 <= number(rest) < 128:
                    code += bytes([OPS["lit8"][0], number(rest) & 0xFF])
                else:
                    code += bytes([OPS["lit16"][0]])
                    code += struct.pack("<H", self.value(rest, lineno) & 0xFFFF)
            elif word == "sys":
                if rest not in SYSCALLS:
                    raise AsmError(f"line {lineno}: unknown system call {rest!r}")
                code += bytes([OPS["sys"][0], SYSCALLS[rest]])
            else:
                opcode, nbytes = OPS[word]
                code.append(opcode)
                if nbytes == 1:
                    code += bytes([self.value(rest, lineno) & 0xFF])
                elif nbytes == 2:
                    code += struct.pack("<H", self.value(rest, lineno) & 0xFFFF)
                elif rest:
                    raise AsmError(f"line {lineno}: {word} takes no operand")
        header = MAGIC + bytes([VERSION, 0]) + struct.pack("<H", self.data_size)
        return header + bytes(code)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__ or "")
        return 2
    with open(argv[1], encoding="utf-8") as src:
        source = src.read()
    try:
        image = Assembler().assemble(source)
    except AsmError as err:
        sys.stderr.write(f"{argv[1]}: {err}\n")
        return 1
    with open(argv[2], "wb") as out:
        out.write(image)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
