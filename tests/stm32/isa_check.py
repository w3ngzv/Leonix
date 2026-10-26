# SPDX-License-Identifier: GPL-2.0-only
"""Expected results for isa.c.

Values follow the instruction descriptions in PM0056 chapter 3, the
faults follow table 20 there (the Cortex-M3 instruction list) and the CFSR
layout of PM0056 4.4.10.  The cycle counts are printed, not checked: the
instruction timing of the Cortex-M3 TRM has not been compared here.

isa.c 的期望结果. 数值依 PM0056 第 3 章的指令说明, 故障依该手册表 20 (Cortex-M3
指令列表) 与 4.4.10 的 CFSR 布局. 周期数只打印, 不检查: 此处未与 Cortex-M3 TRM
的指令时序核对.
"""

import struct

M32 = 0xFFFFFFFF

# op codes in the order of the enums in isa.c; 与 isa.c 中枚举顺序相同的操作码
OPS = """UDIV SDIV CLZ RBIT REV REV16 REVSH BFI_8_12 BFC_4_8 UBFX_5_9 SBFX_5_9
UBFX_0_32 SSAT_8 SSAT_1 SSAT_32 USAT_8 USAT_0 USAT_31 UMULL SMULL UMLAL SMLAL
MLA MLS LSLS LSRS ASRS RORS IT_LT IT_GT4 IT_HI TBB TBH EXCL BITBAND UNALIGNED
CYC_UDIV CYC_UMULL""".split()
OP = {name: i + 1 for i, name in enumerate(OPS)}
FAULTS = """UDF SMLAD QADD UADD8 SEL VADD BX_EVEN XN_FETCH UDIV0_TRAP SDIV0_NOTRAP
LDRD_UNALIGNED LDM_UNALIGNED LDR_UNALIGN_TRP LDR_ALIGNED_TRP""".split()
FAULT = {0x100 + i: name for i, name in enumerate(FAULTS)}

CFSR_BITS = {0: "IACCVIOL", 1: "DACCVIOL", 7: "MMARVALID", 8: "IBUSERR",
             9: "PRECISERR", 10: "IMPRECISERR", 15: "BFARVALID",
             16: "UNDEFINSTR", 17: "INVSTATE", 18: "INVPC", 19: "NOCP",
             24: "UNALIGNED", 25: "DIVBYZERO"}
APSR_N, APSR_Z, APSR_C, APSR_Q = 1 << 31, 1 << 30, 1 << 29, 1 << 27


def s32(x):
    return x - (1 << 32) if x & 0x80000000 else x


def bit(x, n):
    return (x >> n) & 1


def udiv(a, b):
    return 0 if b == 0 else a // b


def sdiv(a, b):
    a, b = s32(a), s32(b)
    if b == 0:
        return 0
    q = abs(a) // abs(b)
    return (q if (a < 0) == (b < 0) else -q) & M32


def clz(a):
    return 32 - a.bit_length()


def rbit(a):
    return int(f"{a:032b}"[::-1], 2)


def rev(a):
    return struct.unpack("<I", struct.pack(">I", a))[0]


def rev16(a):
    return ((a & 0x00FF00FF) << 8 | (a >> 8) & 0x00FF00FF) & M32


def revsh(a):
    h = ((a & 0xFF) << 8) | ((a >> 8) & 0xFF)
    return (h - 0x10000 if h & 0x8000 else h) & M32


def bfi(dst, src, lsb, width):
    mask = ((1 << width) - 1) << lsb
    return (dst & ~mask | (src << lsb) & mask) & M32


def ubfx(a, lsb, width):
    return (a >> lsb) & ((1 << width) - 1)


def sbfx(a, lsb, width):
    v = ubfx(a, lsb, width)
    return (v - (1 << width) if v >> (width - 1) else v) & M32


def ssat(a, n):
    v, hi, lo = s32(a), (1 << (n - 1)) - 1, -(1 << (n - 1))
    r = min(max(v, lo), hi)
    return r & M32, r != v


def usat(a, n):
    v = s32(a)
    r = min(max(v, 0), (1 << n) - 1)
    return r & M32, r != v


def shift_c(op, a, n, c_in):
    """Register-specified shift: amount is the bottom byte of Rm.

    寄存器指定的移位: 移位量为 Rm 的最低字节.
    """
    n &= 0xFF
    if n == 0:
        return a, c_in
    if op == "LSLS":
        if n < 32:
            return (a << n) & M32, bit(a, 32 - n)
        return 0, bit(a, 0) if n == 32 else 0
    if op == "LSRS":
        if n < 32:
            return a >> n, bit(a, n - 1)
        return 0, bit(a, 31) if n == 32 else 0
    if op == "ASRS":
        if n < 32:
            return (s32(a) >> n) & M32, bit(a, n - 1)
        return (M32 if a >> 31 else 0), bit(a, 31)
    m = n % 32
    r = a if m == 0 else ((a >> m) | (a << (32 - m))) & M32
    return r, bit(r, 31)


def nz(r):
    return (APSR_N if r >> 31 else 0) | (APSR_Z if r == 0 else 0)


def expect(op, a, b, c):
    """Return the expected (lo, hi), or None for records checked elsewhere.

    返回期望的 (lo, hi); 由别处检查的记录返回 None.
    """
    name = OPS[op - 1]
    simple = {"UDIV": lambda: udiv(a, b), "SDIV": lambda: sdiv(a, b),
              "CLZ": lambda: clz(a), "RBIT": lambda: rbit(a),
              "REV": lambda: rev(a), "REV16": lambda: rev16(a),
              "REVSH": lambda: revsh(a),
              "BFI_8_12": lambda: bfi(a, b, 8, 12),
              "BFC_4_8": lambda: bfi(a, 0, 4, 8),
              "UBFX_5_9": lambda: ubfx(a, 5, 9),
              "SBFX_5_9": lambda: sbfx(a, 5, 9),
              "UBFX_0_32": lambda: ubfx(a, 0, 32),
              "MLA": lambda: (c + a * b) & M32,
              "MLS": lambda: (c - a * b) & M32}
    if name in simple:
        return simple[name](), 0
    if name[:4] in ("SSAT", "USAT"):
        r, q = (ssat if name[0] == "S" else usat)(a, int(name[5:]))
        return r, APSR_Q if q else 0
    if name in ("UMULL", "SMULL", "UMLAL", "SMLAL"):
        x, y = (a, b) if name[0] == "U" else (s32(a), s32(b))
        p = x * y
        if name.endswith("LAL"):
            p += c | (c ^ 0x5A5A5A5A) << 32
        p &= (1 << 64) - 1
        return p & M32, p >> 32
    if name in ("LSLS", "LSRS", "ASRS", "RORS"):
        r, carry = shift_c(name, a, b, 0)
        return r, nz(r) | (APSR_C if carry else 0)
    if name == "IT_LT":
        return (1 if s32(a) < s32(b) else 2), 0
    if name == "IT_GT4":
        return (3 if s32(a) > s32(b) else 12), 0
    if name == "IT_HI":
        return (1 if a > b else 2), 0
    if name == "TBB":
        return 10 + 10 * a, 0
    if name == "TBH":
        return 11 + 10 * a, 0
    return None


def check_special(op, a, b, c, lo, hi):
    """Check the records whose fields do not fit expect().

    检查字段不适用 expect() 的记录.

    Returns an error message, or None if the record matches.
    返回错误信息, 记录相符时返回 None.
    """
    name = OPS[op - 1]
    if name == "EXCL":
        # a: LDREX value, b: STREX status, c: STREX after CLREX,
        # lo: STREX with no reservation, hi: final memory word
        # a: LDREX 读出值, b: STREX 状态, c: CLREX 之后的 STREX,
        # lo: 没有保留时的 STREX, hi: 最终的内存字
        want = (0x13579BDF, 0, 1, 1, 0x2468ACE0)
    elif name == "BITBAND" and hi == 0:
        # a: word, b: alias bit 5, c: alias bit 6, lo: alias bit 0
        # a: 该字, b: 别名第 5 位, c: 别名第 6 位, lo: 别名第 0 位
        want = (0x00000021, 1, 0, 1, 0)
    elif name == "BITBAND":
        want = (0x00000001, 0, 0, 1, 1)
    elif name == "UNALIGNED":
        # lo: LDR [+1], hi: LDRH [+3], c: LDRSH [+3]
        want = (1, 3, 0x0504, 0x05040302, 0x0504)
    else:
        return None
    got = (a, b, c, lo, hi)
    return None if got == want else f"got {fmt(got)}, want {fmt(want)}"


def fmt(t):
    return " ".join(f"{x:08x}" for x in t)


def cfsr_names(v):
    return ",".join(n for b, n in CFSR_BITS.items() if v >> b & 1) or "-"


# expected CFSR flag, or None for "must not fault", and r0 when no fault
# 期望的 CFSR 标志 (None 表示不得触发故障), 以及不触发故障时的 r0
FAULT_EXPECT = {
    "UDF": ("UNDEFINSTR", None),
    "SMLAD": ("UNDEFINSTR", None),
    "QADD": ("UNDEFINSTR", None),
    "UADD8": ("UNDEFINSTR", None),
    "SEL": ("UNDEFINSTR", None),
    "VADD": ("NOCP", None),
    "BX_EVEN": ("INVSTATE", None),
    "XN_FETCH": ("IACCVIOL", None),
    "UDIV0_TRAP": ("DIVBYZERO", None),
    "SDIV0_NOTRAP": (None, 0),
    "LDRD_UNALIGNED": ("UNALIGNED", None),
    "LDM_UNALIGNED": ("UNALIGNED", None),
    "LDR_UNALIGN_TRP": ("UNALIGNED", None),
    "LDR_ALIGNED_TRP": (None, 0x08070605),
}


# qemu-system-arm 11.1.2 runs SMLAD on its cortex-m3 instead of faulting;
# (2 * 4 + 3 * 5) + 7 = 30.  The board must fault, QEMU is only recorded.
# qemu-system-arm 11.1.2 在其 cortex-m3 上执行 SMLAD 而不触发故障,
# (2 * 4 + 3 * 5) + 7 = 30. 板子上必须触发故障, QEMU 的行为只作记录.
QEMU_DIFFERS = {"SMLAD": (None, 30)}


def check(records, voltage=None, qemu=False):
    """Check the records of one run of isa.c.

    检查 isa.c 一次运行的全部记录.

    Args:
        records: (op, a, b, c, lo, hi) tuples in the order the target wrote
            them. 按目标写入顺序排列的 (op, a, b, c, lo, hi) 元组.
        voltage: unused here. 此处不使用.
        qemu: the run was under QEMU, see QEMU_DIFFERS.
            在 QEMU 下运行, 见 QEMU_DIFFERS.

    Returns:
        The number of failed checks. 未通过的检查数.
    """
    bad = 0
    ok = {}
    timing = []
    for op, a, b, c, lo, hi in records:
        if op in FAULT:
            name = FAULT[op]
            flag, want_r0 = FAULT_EXPECT[name]
            if qemu and name in QEMU_DIFFERS:
                flag, want_r0 = QEMU_DIFFERS[name]
            got = cfsr_names(lo)
            if flag is None:
                good = b == 0 and c == want_r0
            else:
                good = b == 1 and flag in got.split(",")
            note = " (qemu)" if qemu and name in QEMU_DIFFERS else ""
            print(f"  {'pass' if good else 'FAIL'}  {name:16s} faults={b} "
                  f"CFSR={lo:08x} ({got}) HFSR={hi:08x} r0={c:08x}{note}")
            bad += not good
            continue
        name = OPS[op - 1]
        if name.startswith("CYC_"):
            timing.append((name, a, b, lo, hi))
            continue
        err = check_special(op, a, b, c, lo, hi)
        if err is None and name not in ("EXCL", "BITBAND", "UNALIGNED"):
            want = expect(op, a, b, c)
            if (lo, hi) != want:
                err = (f"a={a:08x} b={b:08x} c={c:08x} got {fmt((lo, hi))} "
                       f"want {fmt(want)}")
        if err:
            print(f"  FAIL  {name}: {err}")
            bad += 1
        else:
            ok[name] = ok.get(name, 0) + 1

    print("passed: " + ", ".join(f"{n} {k}" for n, k in ok.items()))
    if any(t or base for _, _, _, t, base in timing):
        print("cycles, overhead subtracted:")
        for name, a, b, t, base in timing:
            print(f"  {name[4:]:6s} {a:08x} {b:08x}  {t - base + 1:3d}")
    else:
        print("cycles: DWT_CYCCNT reads 0, no cycle counts")
    return bad
