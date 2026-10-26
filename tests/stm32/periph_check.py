# SPDX-License-Identifier: GPL-2.0-only
"""Expected results for periph.c.

Limits come from DS5319 Rev 20 (datasheet) and RM0008 Rev 15 (reference
manual); each check names its table or section.  QEMU models none of
these peripherals, so the suite runs only on a board.

periph.c 的期望结果. 界限取自 DS5319 Rev 20 (数据手册) 与 RM0008 Rev 15 (参考
手册), 每项检查注明所依据的表或章节. QEMU 不模拟这些外设, 因此该测试组只在
板上运行.
"""

QEMU = False

(P_HSE, P_LSI, P_HSI, P_LSE, P_BKP, P_ADC_CAL, P_ADC_VREF, P_ADC_TEMP,
 P_CRC, P_DMA, P_TIMCHAIN, P_GPIO_OUT, P_GPIO_BSRR_BOTH, P_GPIO_BRR,
 P_GPIO_PULLUP, P_GPIO_PULLDOWN, P_GPIO_LOCK, P_USART) = range(0x200, 0x212)

TIMED_OUT = 0xFFFFFFFF
HSE_HZ = 8_000_000
RTC_HSE_HZ = HSE_HZ / 128
ADC_FULL = 4095
PB_HIGH_PINS = 0xF000
# cycles lost starting and stopping TIM3; 启动与停止 TIM3 损失的周期数
CHAIN_SLACK = 50

# DS5319 table 25: fLSI 30 to 60 kHz; 表 25: fLSI 为 30 至 60 kHz
LSI_HZ = (30_000, 60_000)
# DS5319 table 24: ACCHSI is -1.3 % to +2 % for TA = 0 to 70 C; the room
# temperature is not measured, so the 25 C row (-1.1 % to +1.8 %) is not used
# DS5319 表 24: TA = 0 至 70 C 时 ACCHSI 为 -1.3 % 至 +2 %; 室温未测量, 因此不用
# 25 C 一行 (-1.1 % 至 +1.8 %)
HSI_PCT = (-1.3, 2.0)
# DS5319 table 12: VREFINT 1.16 to 1.24 V for -40 to +85 C, typ 1.20
# DS5319 表 12: -40 至 +85 C 时 VREFINT 为 1.16 至 1.24 V, 典型值 1.20
VREFINT = (1.16, 1.20, 1.24)
# DS5319 table 51: V25 typ 1.43 V, Avg_Slope typ 4.3 mV/C
# DS5319 表 51: V25 典型值 1.43 V, Avg_Slope 典型值 4.3 mV/C
V25, AVG_SLOPE = 1.43, 0.0043
CRC_POLY = 0x04C11DB7


def crc_stm32(words):
    """CRC of 32-bit words as the CRC unit computes it (RM0008 4.2).

    按 CRC 单元的方式计算 32 位字序列的 CRC (RM0008 4.2): 多项式 0x04C11DB7,
    初值 0xFFFFFFFF, 每个字从最高位起逐位处理, 不反射, 不异或输出.
    """
    crc = 0xFFFFFFFF
    for w in words:
        crc ^= w
        for _ in range(32):
            crc = ((crc << 1) ^ CRC_POLY if crc & 0x80000000
                   else crc << 1) & 0xFFFFFFFF
    return crc


class Report:
    def __init__(self):
        self.bad = 0

    def line(self, good, name, text):
        if good is None:
            tag = "info"
        else:
            tag = "pass" if good else "FAIL"
            self.bad += not good
        print(f"  {tag}  {name:14s} {text}")


def check(records, voltage=None, qemu=False):
    """Check the records of one run of periph.c.

    检查 periph.c 一次运行的全部记录.

    Args:
        records: (op, a, b, c, lo, hi) tuples. (op, a, b, c, lo, hi) 元组.
        voltage: VDD measured by ST-LINK, or None. ST-LINK 测得的 VDD, 或 None.
        qemu: unused, the suite refuses QEMU. 未使用, 该测试组不在 QEMU 中运行.

    Returns:
        The number of failed checks. 未通过的检查数.
    """
    r = Report()
    vref_raw = None
    for op, a, b, c, lo, hi in records:
        if op == P_HSE:
            r.line(a == 1, "HSE start", f"HSERDY after {b} reads")
        elif op == P_LSI:
            ok = a == 1 and lo != TIMED_OUT
            r.line(ok and LSI_HZ[0] <= lo <= LSI_HZ[1], "LSI",
                   f"{lo} Hz against HSE (DS5319 table 25: 30-60 kHz)")
        elif op == P_HSI:
            if lo == TIMED_OUT or lo == 0:
                r.line(False, "HSI", "RTC on HSE/128 did not start")
                continue
            hz = b * RTC_HSE_HZ / lo
            pct = (hz / 8e6 - 1) * 100
            r.line(HSI_PCT[0] <= pct <= HSI_PCT[1], "HSI",
                   f"{hz / 1e6:.4f} MHz ({pct:+.3f} %) against HSE, "
                   f"DS5319 table 24, 0-70 C: {HSI_PCT[0]} to +{HSI_PCT[1]} %")
        elif op == P_LSE:
            if a != 1:
                r.line(None, "LSE", "no 32.768 kHz crystal started; "
                       "a board item, the chip is not at fault")
            else:
                r.line(abs(lo - 32768) <= 33, "LSE",
                       f"{lo} Hz against HSE after {b} reads")
        elif op == P_BKP:
            r.line(lo == b, f"BKP_DR{a}", f"wrote {b:04x} read {lo:04x}")
        elif op == P_ADC_CAL:
            r.line(a == 1, "ADC calibrate", f"CR2 {lo:08x}")
        elif op == P_ADC_VREF:
            if lo == TIMED_OUT:
                r.line(False, "ADC VREFINT", "no end of conversion")
                continue
            vref_raw = lo / a
            vdda = VREFINT[1] * ADC_FULL / vref_raw
            lo_v = VREFINT[0] * ADC_FULL / vref_raw
            hi_v = VREFINT[2] * ADC_FULL / vref_raw
            text = (f"raw {vref_raw:.1f}, VDDA {vdda:.3f} V "
                    f"({lo_v:.3f} to {hi_v:.3f} V over DS5319 table 12)")
            if voltage:
                text += f", ST-LINK reads {voltage:.3f} V"
                r.line(lo_v <= voltage <= hi_v, "ADC VREFINT", text)
            else:
                r.line(None, "ADC VREFINT", text)
        elif op == P_ADC_TEMP:
            if lo == TIMED_OUT or not vref_raw:
                r.line(False, "ADC temp", "no conversion")
                continue
            vsense = (lo / a) * VREFINT[1] / vref_raw
            temp = (V25 - vsense) / AVG_SLOPE + 25
            r.line(None, "ADC temp", f"VSENSE {vsense:.3f} V, about "
                   f"{temp:.0f} C with the typical V25 and slope; "
                   "V25 alone spreads 1.34-1.52 V, about +-21 C")
        elif op == P_CRC:
            want = crc_stm32([(i * 0x9E3779B9) & 0xFFFFFFFF
                              for i in range(a)])
            r.line(lo == want, "CRC", f"{lo:08x}, want {want:08x}")
        elif op == P_DMA:
            r.line(b == 0 and c == 1 and hi == 0, "DMA mem2mem",
                   f"{b} words wrong of {a}, ISR {lo:x}, CNDTR {hi}")
        elif op == P_TIMCHAIN:
            ticks = a * lo + b
            r.line(abs(ticks - c) <= CHAIN_SLACK, "TIM3 -> TIM2",
                   f"{a} x {lo} + {b} = {ticks}, SysTick {c}")
        elif op == P_GPIO_OUT:
            r.line(lo == a, "GPIO out", f"ODR {a:04x} IDR {lo:04x}")
        elif op == P_GPIO_BSRR_BOTH:
            r.line(lo == 0x1000, "BSRR set+reset",
                   f"ODR {lo:04x}, RM0008 9.2.5: set wins")
        elif op == P_GPIO_BRR:
            r.line(lo == PB_HIGH_PINS & ~0x2000, "BRR", f"ODR {lo:04x}")
        elif op == P_GPIO_PULLUP:
            r.line(lo == PB_HIGH_PINS, "pull-up", f"IDR {lo:04x}")
        elif op == P_GPIO_PULLDOWN:
            r.line(lo == 0, "pull-down", f"IDR {lo:04x}")
        elif op == P_GPIO_LOCK:
            r.line(lo == b and a & (1 << 16), "LCKR",
                   f"LCKR {a:05x}, CRH {b:08x} then {lo:08x}")
        elif op == P_USART:
            ok = lo == b and c == 0
            r.line(ok, f"USART{a} echo",
                   f"sent {b:02x} got {'none' if lo == TIMED_OUT else hex(lo)}"
                   f", errors {c:x} (half-duplex, pin push-pull)")
        else:
            r.line(False, "unknown", f"op {op:#x}")
    return r.bad
