# SPDX-License-Identifier: GPL-2.0-only
#
# Toolchain, flags, image and flashing for the Arduino Leonardo, read by
# the top Makefile once the architecture is known.
#
# Arduino Leonardo 的工具链、编译选项、映像与烧录, 在架构确定之后由顶层
# Makefile 读取.
#
MCU      := atmega32u4
F_CPU    := 16000000UL

# Caterina occupies the top 4 KiB of the 32 KiB flash
# Caterina 占用 32 KiB flash 的最高 4 KiB
APP_FLASH_SIZE := 28672

# the baud rate that sends a running Leonix to the bootloader
# 使运行中的 Leonix 进入 bootloader 的波特率
BOOTLOADER_BAUD := 1200

CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
NM      := avr-nm
SIZE    := avr-size

CPPFLAGS += -DF_CPU=$(F_CPU)
CFLAGS   += -mmcu=$(MCU)
ASFLAGS  += -mmcu=$(MCU)

# SRAM layout, see include/leonix/memlayout.h; data addresses carry the
# linker's 0x800000 offset.  The boot stack between .bss and the boot key
# must hold main() and panic(), about 90 bytes each by -fstack-usage.
#
# SRAM 布局见 include/leonix/memlayout.h; 数据地址带有链接器的 0x800000 偏移.
# .bss 与启动键之间的启动栈必须容纳 main() 与 panic(), 按 -fstack-usage 计算两者
# 各约 90 字节.
BOOT_KEY_ADDR     := 0x800800
TASK_STACKS_START := 0x800802
SRAM_END          := 0x800b00
BOOT_STACK_MIN    := 128

LDSCRIPT := arch/avr/leonix.lds
LDFLAGS  := -mmcu=$(MCU) -nostartfiles -T $(LDSCRIPT) \
	    -Wl,--defsym=__TEXT_REGION_LENGTH__=$(APP_FLASH_SIZE) \
	    -Wl,--section-start=.noinit=$(TASK_STACKS_START) \
	    -Wl,-Map=$(TARGET).map

KBUILD_IMAGE := $(TARGET).hex

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@bss_end=$$($(NM) $@ | awk '$$3 == "__bss_end" { print $$1 }'); \
	set -- $$($(OBJDUMP) -h $@ | awk '$$2 == ".noinit" { print $$3, $$4 }'); \
	noinit_end=$$((0x$$1 + 0x$$2)); \
	test $$((0x$$bss_end + $(BOOT_STACK_MIN))) -le $$(($(BOOT_KEY_ADDR))) || { \
		echo ".bss ends at 0x$$bss_end, too close to the boot key"; rm -f $@; exit 1; }; \
	test $$noinit_end -le $$(($(SRAM_END))) || { \
		echo ".noinit ends past the end of SRAM"; rm -f $@; exit 1; }

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# make flash PORT=/dev/cu.usbmodemXXXX [HEX=file.hex]
#
# Opening the port at 1200 baud and closing it restarts a running Leonix
# into Caterina.  The port then leaves and comes back as the bootloader's,
# under the same name on macOS and usually on Linux.  If it never leaves,
# the board is taken to be in the bootloader already, for instance after
# a press of RESET, and avrdude runs straight away.  Waits are in tenths
# of a second.
#
# -D skips avrdude's chip erase.  Caterina erases each page before it
# writes it (Caterina.c, ReadWriteMemoryBlock()), so the erase would only
# wear the pages the image does not reach and erase the image pages a
# second time.
#
# 以 1200 波特率打开并关闭串口, 会使运行中的 Leonix 重启进入 Caterina. 随后该串
# 口消失, 再作为 bootloader 的串口出现, 在 macOS 上名称不变, 在 Linux 上通常也不
# 变. 若串口始终未消失, 则认为板子已处于 bootloader 中 (例如按过 RESET 之后),
# avrdude 立即运行. 等待时间以十分之一秒为单位.
#
# -D 跳过 avrdude 的整片擦除. Caterina 在写入每一页之前先擦除该页 (Caterina.c,
# ReadWriteMemoryBlock()), 因此整片擦除只会磨损映像未覆盖的页, 并把映像所在的页
# 再擦除一次.
FLASH_DETACH_WAIT := 10
FLASH_ATTACH_WAIT := 80
AVRDUDE := avrdude -p m32u4 -c avr109 -P $(PORT) -b 57600

define enter_bootloader
	@test -n "$(PORT)" || { echo "usage: make $@ PORT=/dev/cu.usbmodemXXXX"; exit 1; }
	@test -e "$(PORT)" || { echo "$(PORT) not found"; exit 1; }
	@stty -f $(PORT) $(BOOTLOADER_BAUD) 2>/dev/null || stty -F $(PORT) $(BOOTLOADER_BAUD)
	@n=0; while [ -e "$(PORT)" ] && [ $$n -lt $(FLASH_DETACH_WAIT) ]; do \
		sleep 0.1; n=$$((n + 1)); done; \
	n=0; while [ ! -e "$(PORT)" ] && [ $$n -lt $(FLASH_ATTACH_WAIT) ]; do \
		sleep 0.1; n=$$((n + 1)); done; \
	test -e "$(PORT)" || { echo "$(PORT) did not come back"; exit 1; }
endef

# HEX names another image to write, such as a diagnostic build
# HEX 指定另一个要写入的映像, 例如诊断用的构建
HEX ?= $(TARGET).hex

flash: $(HEX)
	$(enter_bootloader)
	$(AVRDUDE) -D -U flash:w:$(HEX):i

# read the fuses and lock bits back through Caterina, in hex
# 经 Caterina 以十六进制读回熔丝位与锁定位
fuses:
	$(enter_bootloader)
	$(AVRDUDE) -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h

.PHONY: flash fuses
