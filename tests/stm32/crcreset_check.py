# SPDX-License-Identifier: GPL-2.0-only
"""Expected results for crcreset.c.

RM0008 Rev 15 4.3: RESET sets DR to 0xFFFFFFFF, each write to DR combines
the previous value with the new word, and DR may be written back to back
or read right after a write.  RM0008 gives no time for RESET to take
effect, so the cases that touch DR right after RESET are reported, not
failed: each line says whether the board matched RM0008, lost the write,
or ran the write on the value from before RESET.  Only the sequence of
HAL_CRC_Calculate() and the preload are checked.

crcreset.c 的期望结果. RM0008 Rev 15 4.3: RESET 把 DR 置为 0xFFFFFFFF, 每次写入
DR 都把原值与新字合并计算, DR 可以连续写入, 也可以写入后立即读取. RM0008 没有
给出 RESET 生效所需的时间, 因此 RESET 之后立即访问 DR 的情形只报告, 不计为失败:
每行说明结果与 RM0008 相符, 写入丢失, 还是写入在 RESET 之前的值上计算. 只有
HAL_CRC_Calculate() 的顺序与预装入的值计入检查.
"""

QEMU = False

M32 = 0xFFFFFFFF
POLY = 0x04C11DB7
PRELOAD = 0x11111111
WORD = 0x12345678
HAL_MULT = 0x9E3779B9

(C_ENABLE_WRITE, C_ENABLE_READBACK_WRITE, C_RESET_WRITE, C_RESET_NOPS_WRITE,
 C_RESET_READ, C_HAL) = range(0x400, 0x406)

CASES = {
    C_ENABLE_WRITE: "clock on, RESET, write",
    C_ENABLE_READBACK_WRITE: "clock on, read RCC, RESET, write",
    C_RESET_WRITE: "RESET, write",
    C_RESET_NOPS_WRITE: "RESET, 8 NOPs, write",
    C_RESET_READ: "RESET, read",
    C_HAL: "HAL_CRC_Calculate order",
}


def crc(words, value=M32):
    """CRC unit of RM0008 4.2 from @value; 从 @value 起按 RM0008 4.2 计算."""
    for w in words:
        value ^= w
        for _ in range(32):
            value = ((value << 1) ^ POLY if value & 0x80000000
                     else value << 1) & M32
    return value


def classify(op, got):
    """Name what @got shows for case @op; 说明 @got 在情形 @op 中的含义."""
    old = crc([PRELOAD])
    if op == C_RESET_READ:
        return {M32: "as RM0008", old: "read the value before RESET"}.get(
            got, "other")
    return {crc([WORD]): "as RM0008", M32: "write lost",
            crc([WORD], old): "RESET not applied",
            old: "DR unchanged"}.get(got, "other")


def check(records, voltage=None, qemu=False):
    """Check the records of one run of crcreset.c.

    检查 crcreset.c 一次运行的全部记录.

    Args:
        records: (op, a, b, c, lo, hi) tuples. (op, a, b, c, lo, hi) 元组.
        voltage: unused. 未使用.
        qemu: unused, the suite refuses QEMU. 未使用, 该测试组不在 QEMU 中运行.

    Returns:
        The number of failed checks. 未通过的检查数.
    """
    bad = 0
    same = {}
    for op, rep, pre, n, got, _ in records:
        name = CASES.get(op, f"op {op:#x}")
        if pre != crc([PRELOAD]):
            print(f"  FAIL  {name}: preload read {pre:08x}")
            bad += 1
            continue
        if op == C_HAL:
            want = crc([(i * HAL_MULT) & M32 for i in range(n)])
            good = got == want
            bad += not good
            print(f"  {'pass' if good else 'FAIL'}  {name:34s} #{rep} "
                  f"{got:08x}, want {want:08x}")
            continue
        what = classify(op, got)
        same.setdefault(name, [0, 0])
        same[name][0] += what == "as RM0008"
        same[name][1] += 1
        print(f"  info  {name:34s} #{rep} {got:08x}  {what}")
    for name, (k, total) in same.items():
        print(f"  {name}: {k} of {total} as RM0008")
    return bad
