# SPDX-License-Identifier: GPL-2.0-only
#
# Toolchain, flags, image and flashing for the STM32F103C8, read by the
# top Makefile once the architecture is known.
#
# STM32F103C8 的工具链、编译选项、映像与烧录, 在架构确定之后由顶层 Makefile
# 读取.
#
CROSS_COMPILE ?= arm-none-eabi-

CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
NM      := $(CROSS_COMPILE)nm
SIZE    := $(CROSS_COMPILE)size

ARM_ARCH_FLAGS := -mcpu=cortex-m3 -mthumb

CFLAGS  += $(ARM_ARCH_FLAGS) -ffreestanding -ffunction-sections -fdata-sections
ASFLAGS += $(ARM_ARCH_FLAGS)

# No C library: the string functions come from lib/string.c, through
# include/leonix/string.h.  libgcc stays for whatever helper calls gcc
# emits.
#
# 不链接 C 库: 字符串函数经由 include/leonix/string.h 取自 lib/string.c.
# libgcc 保留, 用于 gcc 生成的辅助函数调用.
LDSCRIPT := arch/arm/leonix.lds
LDFLAGS  := $(ARM_ARCH_FLAGS) -nostdlib -T $(LDSCRIPT) \
	    -Wl,--gc-sections -Wl,-Map=$(TARGET).map

KBUILD_IMAGE := $(TARGET).bin
KBUILD_DEFCONFIG := stm32f103_defconfig

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# make flash, through an ST-LINK/V2 on SWD.  CPUTAPID 0 accepts any
# DPIDR, since clones of the F103 report other values than ST's
# (Documentation/stm32f103.rst).
#
# 经 SWD 上的 ST-LINK/V2 烧录. CPUTAPID 0 接受任何 DPIDR, 因为 F103 的仿制品
# 报告的值与 ST 的不同 (Documentation/stm32f103.rst).
OPENOCD ?= openocd

flash: $(TARGET).elf
	$(OPENOCD) -f interface/stlink.cfg -c "set CPUTAPID 0" \
		-f target/stm32f1x.cfg -c "program $< verify reset exit"

# the same image on qemu-system-arm's stm32vldiscovery, USART1 on stdio;
# Ctrl-A x quits
#
# 在 qemu-system-arm 的 stm32vldiscovery 上运行同一映像, USART1 接到标准输入
# 输出; Ctrl-A x 退出
QEMU_ARM ?= qemu-system-arm

qemu: $(TARGET).elf
	$(QEMU_ARM) -M stm32vldiscovery -kernel $< -nographic -monitor none \
		-serial stdio

# the boot and shell test of tests/qemu_boot.py on the same machine
# 在同一机器上运行 tests/qemu_boot.py 的启动与 shell 测试
check-boot: $(TARGET).elf
	$(PYTHON) tests/qemu_boot.py --qemu $(QEMU_ARM) $<

define archhelp
  echo  '* leonix.bin    - Raw image for the start of the flash'
  echo  '  flash         - Write leonix.elf through an ST-LINK/V2 with OpenOCD'
  echo  '  qemu          - Run leonix.elf on qemu-system-arm stm32vldiscovery'
  echo  '  check-boot    - Boot and shell test on the same machine'
endef

.PHONY: flash qemu check-boot
