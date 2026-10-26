# SPDX-License-Identifier: GPL-2.0-only
MCU      := atmega32u4
F_CPU    := 16000000UL
TARGET   := leonix

# Caterina occupies the top 4 KiB of the 32 KiB flash
# Caterina 占用 32 KiB flash 的最高 4 KiB
APP_FLASH_SIZE := 28672

# the baud rate that sends a running Leonix to the bootloader
# 使运行中的 Leonix 进入 bootloader 的波特率
BOOTLOADER_BAUD := 1200

# The included auto.conf.cmd brings rules of its own, which must not
# become the default goal; Linux declares __all first for the same reason.
#
# 被包含的 auto.conf.cmd 自带规则, 这些规则不得成为默认目标; Linux 先声明 __all
# 也是出于同样的原因.
.DEFAULT_GOAL := all

# Kconfig, as in Linux.  .config holds the choices; syncconfig turns it
# into include/config/auto.conf, read below, and
# include/generated/autoconf.h, included into every C file.
#
# 与 Linux 相同的 Kconfig. .config 保存配置选择; syncconfig 将其转换为下文读取的
# include/config/auto.conf, 以及被包含进每个 C 文件的
# include/generated/autoconf.h.
export ARCH    := avr
export SRCARCH := avr
export srctree := .
export KCONFIG_CONFIG ?= .config
KBUILD_DEFCONFIG := leonardo_defconfig
KCONF := scripts/kconfig/conf
scripts_kconfig := $(MAKE) -f scripts/kconfig/Makefile

config-targets := config menuconfig nconfig oldconfig olddefconfig \
	defconfig %_defconfig savedefconfig allyesconfig allnoconfig \
	alldefconfig listnewconfig syncconfig
no-dot-config-targets := $(config-targets) clean mrproper check \
	htmldocs cleandocs

need-config := 1
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out $(no-dot-config-targets),$(MAKECMDGOALS)),)
need-config :=
endif
endif

ifdef need-config
include include/config/auto.conf
-include include/config/auto.conf.cmd
endif

# Each directory lists its objects in its own Makefile as Linux's obj-y,
# read here in place of Kbuild's descent into the directory.
#
# 每个目录在自己的 Makefile 中以 Linux 的 obj-y 形式列出目标文件, 由此处读取, 代
# 替 Kbuild 对各目录的递归.
kbuild-dirs := arch/avr kernel fs drivers lib init

define kbuild-dir
obj-y :=
include $(1)/Makefile
OBJS += $$(addprefix $(1)/,$$(obj-y))
endef

OBJS :=
$(foreach d,$(kbuild-dirs),$(eval $(call kbuild-dir,$(d))))

CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
NM      := avr-nm
SIZE    := avr-size

CPPFLAGS := -DF_CPU=$(F_CPU) -Iinclude -include include/leonix/kconfig.h
CFLAGS   := -mmcu=$(MCU) -std=c11 -Os -g -Wall -Wextra -Werror
ASFLAGS  := -mmcu=$(MCU) -g -Wall -Werror
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

all: $(TARGET).hex

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c -o $@ $<

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@bss_end=$$($(NM) $@ | awk '$$3 == "__bss_end" { print $$1 }'); \
	set -- $$($(OBJDUMP) -h $@ | awk '$$2 == ".noinit" { print $$3, $$4 }'); \
	noinit_end=$$((0x$$1 + 0x$$2)); \
	test $$((0x$$bss_end + $(BOOT_STACK_MIN))) -le $$(($(BOOT_KEY_ADDR))) || { \
		echo ".bss ends at 0x$$bss_end, too close to the boot key"; rm -f $@; exit 1; }; \
	test $$noinit_end -le $$(($(SRAM_END))) || { \
		echo ".noinit ends past the end of SRAM"; rm -f $@; exit 1; }

$(KCONFIG_CONFIG):
	@echo >&2 '***'
	@echo >&2 '*** Configuration file "$@" not found!'
	@echo >&2 '***'
	@echo >&2 '*** Please run some configurator (e.g. "make $(KBUILD_DEFCONFIG)"'
	@echo >&2 '*** or "make menuconfig").'
	@echo >&2 '***'
	@false

include/config/auto.conf include/generated/autoconf.h: $(KCONFIG_CONFIG)
	$(scripts_kconfig) conf
	$(KCONF) --syncconfig Kconfig

config:
	$(scripts_kconfig) conf
	$(KCONF) --oldaskconfig Kconfig

menuconfig nconfig:
	$(scripts_kconfig) $(@:config=conf)
	scripts/kconfig/$(@:config=conf) Kconfig

oldconfig olddefconfig allyesconfig allnoconfig alldefconfig listnewconfig \
syncconfig:
	$(scripts_kconfig) conf
	$(KCONF) --$@ Kconfig

defconfig: $(KBUILD_DEFCONFIG)

%_defconfig:
	$(scripts_kconfig) conf
	$(KCONF) --defconfig=arch/$(SRCARCH)/configs/$@ Kconfig

# the minimal configuration, to copy over arch/$(SRCARCH)/configs/
# 最小配置, 用于复制到 arch/$(SRCARCH)/configs/
savedefconfig:
	$(scripts_kconfig) conf
	$(KCONF) --savedefconfig=defconfig Kconfig

# /bin, packed from the programs Leonix_coreutils builds.  The programs
# are rebuilt on every make; their own Makefile decides what is stale.
#
# /bin, 由 Leonix_coreutils 构建的程序打包而成. 每次 make 都重新构建这些程序, 哪
# 些需要重建由其自身的 Makefile 判断.
ROMFS_PROGRAMS = $(notdir $(wildcard $(COREUTILS)/build/*))

coreutils:
	@test -f $(COREUTILS)/Makefile || { \
		echo "$(COREUTILS) is missing, see external/README"; exit 1; }
	$(MAKE) -C $(COREUTILS) TOOLCHAIN=$(TOOLCHAIN) PYTHON=$(PYTHON)

fs/romfs_image.c: coreutils tools/mkromfs.py
	$(PYTHON) tools/mkromfs.py $@ \
		$(foreach p,$(ROMFS_PROGRAMS),$(p)=$(COREUTILS)/build/$(p))

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

disasm: $(TARGET).elf
	$(OBJDUMP) -d $<

size: $(TARGET).elf
	$(SIZE) $<

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

# clean keeps .config, as in Linux; mrproper removes it as well
# clean 与 Linux 一样保留 .config, mrproper 也将其删除
mrproper: clean
	rm -rf $(KCONFIG_CONFIG) $(KCONFIG_CONFIG).old include/config \
		include/generated defconfig
	$(scripts_kconfig) clean

# by directory, as Linux's clean: objects of options now off go too
# 与 Linux 的 clean 一样按目录删除, 已关闭选项的目标文件也被删除
clean:
	rm -f $(foreach d,$(kbuild-dirs),$(d)/*.o $(d)/*.d) $(TARGET).elf $(TARGET).hex $(TARGET).map $(TEST_BIN) \
		$(TEST_VM_PROGS) fs/romfs_image.c tests/test_lxfs.avr.elf

# host tests of the code that has no AVR dependency; 不依赖 AVR 的代码的主机测试
HOSTCC ?= cc
# repositories outside the kernel, see external/README
# 内核以外的仓库, 见 external/README
TOOLCHAIN ?= $(CURDIR)/external/toolchain
COREUTILS ?= external/coreutils
PYTHON ?= python3
LXASM := $(PYTHON) $(TOOLCHAIN)/lxasm.py
TEST_BIN := tests/test_vsprintf tests/test_lxfs tests/test_vm
TEST_VM_PROGS := tests/vm/arith.lx tests/vm/fault.lx

check: $(TEST_BIN) $(TEST_VM_PROGS)
	./tests/test_vsprintf
	./tests/test_lxfs
	./tests/test_vm

%.lx: %.s $(TOOLCHAIN)/lxasm.py
	$(LXASM) $< $@

tests/test_vm: tests/test_vm.c kernel/vm.c include/leonix/vm.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude -o $@ \
		tests/test_vm.c kernel/vm.c

tests/test_vsprintf: tests/test_vsprintf.c lib/vsprintf.c include/leonix/kernel.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude -o $@ \
		tests/test_vsprintf.c lib/vsprintf.c

tests/test_lxfs: tests/test_lxfs.c fs/lxfs.c include/leonix/lxfs.h include/leonix/fs.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude -o $@ \
		tests/test_lxfs.c fs/lxfs.c

# The lxfs test again, built by avr-gcc and run under qemu-system-avr,
# which has no ATmega32U4: an ATmega2560 runs it, the EEPROM is an array
# as on the host, and stdout is USART0.  It checks the code avr-gcc makes
# of fs/lxfs.c, with its 16-bit int, which the host test cannot.
#
# 再次运行 lxfs 测试, 由 avr-gcc 构建并在 qemu-system-avr 下运行.
# qemu-system-avr 没有 ATmega32U4, 因此由 ATmega2560 运行, EEPROM 与主机测试一样
# 是一个数组, stdout 为 USART0. 该测试检查 avr-gcc 为 fs/lxfs.c 生成的代码 (int
# 为 16 位), 这是主机测试无法做到的.
QEMU_AVR ?= qemu-system-avr
QEMU_TIMEOUT ?= 300

tests/test_lxfs.avr.elf: tests/test_lxfs.c tests/qemu_uart.c fs/lxfs.c \
		include/leonix/lxfs.h include/leonix/fs.h
	$(CC) -mmcu=atmega2560 -Os -std=gnu11 -Wall -Wextra -Werror -Iinclude \
		-o $@ tests/test_lxfs.c tests/qemu_uart.c fs/lxfs.c

check-qemu: tests/test_lxfs.avr.elf
	sh tests/run-qemu.sh $(QEMU_AVR) $(QEMU_TIMEOUT) $<

# HTML documentation in Documentation/output, as "make htmldocs" in Linux.
# Missing Sphinx only skips the docs; the firmware build does not need it.
#
# HTML 文档生成在 Documentation/output, 与 Linux 的 "make htmldocs" 相同. 缺少
# Sphinx 时只跳过文档, 固件构建不需要 Sphinx.
SPHINXBUILD ?= sphinx-build
DOCS_OUTPUT := Documentation/output

htmldocs:
	@command -v $(SPHINXBUILD) >/dev/null 2>&1 || { \
		echo "$(SPHINXBUILD) not found, see Documentation/sphinx/requirements.txt"; \
		exit 0; }; \
	$(SPHINXBUILD) -b html -q -d $(DOCS_OUTPUT)/.doctrees Documentation $(DOCS_OUTPUT)/html

cleandocs:
	rm -rf $(DOCS_OUTPUT)

-include $(OBJS:.o=.d)

.PHONY: all disasm size flash fuses clean mrproper coreutils check check-qemu htmldocs \
	cleandocs $(config-targets)
