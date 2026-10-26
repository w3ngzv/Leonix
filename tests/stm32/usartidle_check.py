# SPDX-License-Identifier: GPL-2.0-only
"""Expected results for usartidle.c.

RM0008 Rev 15 27.3 does not say what level a TX pin set to alternate
function push-pull has while its USART is disabled, so the pin levels
and the echoes are reported, not failed.  Only the internal pull is
checked for push-pull: a pin that does not follow its pull-up and
pull-down is wired to something, and the case tells nothing.  For open
drain the TX pin is expected to be wired to RX, whose pull-up then holds
the line high.

usartidle.c 的期望结果. RM0008 Rev 15 27.3 没有规定 USART 未使能时设为复用
推挽的 TX 引脚是什么电平, 因此引脚电平与回显结果只报告, 不计为失败. 只在推挽
时检查内部上下拉: 引脚不随上拉与下拉变化时, 说明它连接了其他东西, 该情形不能
说明任何问题. 开漏时 TX 引脚应以导线接到 RX, 由 RX 的上拉保持该线为高电平.
"""

QEMU = False

U_PIN, U_ECHO = 0x500, 0x501
TIMED_OUT = 0xFFFFFFFF
PINS = {1: "USART1 PA9", 2: "USART2 PA2", 3: "USART3 PB10"}
PULLS = {0: "pull-down", 1: "pull-up"}
MODES = {0: "AF push-pull", 1: "AF open-drain"}


def check(records, voltage=None, qemu=False):
    """Print the records of one run of usartidle.c.

    打印 usartidle.c 一次运行的全部记录.

    Args:
        records: (op, a, b, c, lo, hi) tuples. (op, a, b, c, lo, hi) 元组.
        voltage: unused. 未使用.
        qemu: unused, the suite refuses QEMU. 未使用, 该测试组不在 QEMU 中运行.

    Returns:
        The number of failed checks. 未通过的检查数.
    """
    bad = 0
    summary = {}
    for op, case, b, c, lo, hi in records:
        od, usart = case >> 16, case >> 12 & 0xF
        pull, rep = case >> 8 & 0xF, case & 0xFF
        mode = MODES.get(od, od)
        name = (f"{PINS.get(usart, usart)} {mode} {PULLS.get(pull, pull)} "
                f"#{rep}")
        key = (od, usart, pull)
        if op == U_PIN:
            if not od and b != pull:
                print(f"  FAIL  {name}: pin read {b} on its {PULLS[pull]}, "
                      "it is connected to something")
                bad += 1
            if od and not c:
                print(f"  info  {name}: idle line is low, connect TX to RX "
                      "for the pull-up")
            early = "nothing" if hi == TIMED_OUT else f"{hi:02x}"
            print(f"  info  {name}: with USART off reads {c}, "
                  f"before the first byte SR {lo:03x}, received {early}")
            summary.setdefault(key, [set(), 0, 0])[0].add(c)
        elif op == U_ECHO:
            good = lo == b and c == 0
            got = "none" if lo == TIMED_OUT else f"{lo:02x}"
            print(f"  info  {name}: sent {b:02x} got {got}, errors {c:x}")
            entry = summary.setdefault(key, [set(), 0, 0])
            entry[1] += good
            entry[2] += 1
        else:
            print(f"  FAIL  unknown op {op:#x}")
            bad += 1
    for (od, usart, pull), (levels, k, total) in sorted(summary.items()):
        seen = "/".join(str(v) for v in sorted(levels))
        print(f"  {PINS.get(usart, usart)} {MODES.get(od, od)} "
              f"{PULLS.get(pull, pull)}: "
              f"pin reads {seen} with USART off, {k} of {total} echoed")
    return bad
