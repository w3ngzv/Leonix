# SPDX-License-Identifier: GPL-2.0-only
TARGET   := leonix

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
export srctree := .
export KCONFIG_CONFIG ?= .config

# The architecture, given as ARCH= on the command line or in the
# environment, as in Linux.  Linux defaults to the host's architecture;
# Leonix runs on no host, so the default is avr, the Leonardo.
# arch/$(SRCARCH)/config.mk names the toolchain, the flags, the image and
# KBUILD_DEFCONFIG, which Linux keeps in arch/$(SRCARCH)/Makefile.
# Kconfig reads SRCARCH from the environment, see arch/Kconfig.
#
# 架构与 Linux 相同, 由命令行或环境变量中的 ARCH= 给出. Linux 默认取主机的架构;
# Leonix 不在任何主机上运行, 因此默认为 avr, 即 Leonardo.
# arch/$(SRCARCH)/config.mk 给出工具链、编译选项、映像与 KBUILD_DEFCONFIG, 后者
# 在 Linux 中位于 arch/$(SRCARCH)/Makefile. Kconfig 从环境变量读取 SRCARCH, 见
# arch/Kconfig.
ARCH ?= avr
SRCARCH := $(ARCH)
export ARCH SRCARCH

ifeq ($(wildcard arch/$(SRCARCH)/config.mk),)
$(error ARCH=$(ARCH): no arch/$(SRCARCH)/config.mk)
endif
KCONF := scripts/kconfig/conf
scripts_kconfig := $(MAKE) -f scripts/kconfig/Makefile

config-targets := config menuconfig nconfig oldconfig olddefconfig \
	defconfig %_defconfig savedefconfig allyesconfig allnoconfig \
	alldefconfig listnewconfig syncconfig
no-dot-config-targets := $(config-targets) clean mrproper check \
	check-qemu check-stm32 stm32-tests htmldocs cleandocs

need-config := 1
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out $(no-dot-config-targets),$(MAKECMDGOALS)),)
need-config :=
endif
endif

ifdef need-config
# Linux lets syncconfig carry a .config made for one architecture over
# to another without a word, dropping the options the other lacks.
# Leonix stops instead, since a forgotten ARCH=arm would otherwise turn
# the STM32 configuration into a Leonardo one.
#
# Linux 允许 syncconfig 把为一个架构生成的 .config 不加提示地转到另一个架构,
# 并丢弃后者没有的选项. Leonix 在此停止, 否则漏写 ARCH=arm 就会把 STM32 的配置
# 改成 Leonardo 的配置.
arch-symbol := CONFIG_$(shell echo '$(SRCARCH)' | tr a-z A-Z)=y
ifneq ($(wildcard $(KCONFIG_CONFIG)),)
ifeq ($(shell grep -x '$(arch-symbol)' $(KCONFIG_CONFIG)),)
$(error $(KCONFIG_CONFIG) was not made for ARCH=$(ARCH))
endif
endif
include include/config/auto.conf
-include include/config/auto.conf.cmd
endif

# Each directory lists its objects in its own Makefile as Linux's obj-y,
# read here in place of Kbuild's descent into the directory.
#
# 每个目录在自己的 Makefile 中以 Linux 的 obj-y 形式列出目标文件, 由此处读取, 代
# 替 Kbuild 对各目录的递归.
kbuild-dirs := arch/$(SRCARCH) kernel fs drivers lib init

define kbuild-dir
obj-y :=
include $(1)/Makefile
OBJS += $$(addprefix $(1)/,$$(obj-y))
endef

OBJS :=
$(foreach d,$(kbuild-dirs),$(eval $(call kbuild-dir,$(d))))


CPPFLAGS := -Iinclude -Iarch/$(SRCARCH)/include \
	    -include include/leonix/kconfig.h
CFLAGS   := -std=c11 -Os -g -Wall -Wextra -Werror
ASFLAGS  := -g -Wall -Werror

include arch/$(SRCARCH)/config.mk

all: $(KBUILD_IMAGE)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c -o $@ $<

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

# auto.conf.cmd makes auto.conf depend on FORCE when an environment
# variable that Kconfig read has changed, as Linux's Makefile provides.
#
# Kconfig 读取的环境变量改变时, auto.conf.cmd 使 auto.conf 依赖 FORCE, 与
# Linux 的 Makefile 相同.
FORCE:

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

# KBUILD_DEFCONFIG comes from arch/$(SRCARCH)/config.mk, read later
# KBUILD_DEFCONFIG 取自稍后读取的 arch/$(SRCARCH)/config.mk
defconfig:
	$(MAKE) $(KBUILD_DEFCONFIG)

# As in Linux, only the configs of ARCH are looked at.  A defconfig of
# another architecture is named in the error.
#
# 与 Linux 相同, 只查找 ARCH 的配置. 若该 defconfig 属于另一个架构, 在错误信息
# 中指出.
%_defconfig:
	@test -f arch/$(SRCARCH)/configs/$@ || { 		echo "$@ not found in arch/$(SRCARCH)/configs"; 		for f in arch/*/configs/$@; do test -f "$$f" && 			echo "it is in $$f: make ARCH=$$(echo $$f | cut -d/ -f2) $@"; done; 		exit 1; }
	$(scripts_kconfig) conf
	$(KCONF) --defconfig=arch/$(SRCARCH)/configs/$@ Kconfig

# the minimal configuration, to copy over arch/$(ARCH)/configs/
# 最小配置, 用于复制到 arch/$(ARCH)/configs/
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

disasm: $(TARGET).elf
	$(OBJDUMP) -d $<

size: $(TARGET).elf
	$(SIZE) $<

# clean keeps .config, as in Linux; mrproper removes it as well
# clean 与 Linux 一样保留 .config, mrproper 也将其删除
mrproper: clean
	rm -rf $(KCONFIG_CONFIG) $(KCONFIG_CONFIG).old include/config \
		include/generated defconfig
	$(scripts_kconfig) clean

# every architecture's directory, since clean reads no .config
# 包括每个架构的目录, 因为 clean 不读取 .config
clean-dirs := $(patsubst %/,%,$(wildcard arch/*/)) $(filter-out arch/%,$(kbuild-dirs))

# by directory, as Linux's clean: objects of options now off go too
# 与 Linux 的 clean 一样按目录删除, 已关闭选项的目标文件也被删除
clean:
	rm -f $(foreach d,$(clean-dirs),$(d)/*.o $(d)/*.d) \
		$(TARGET).elf $(TARGET).hex $(TARGET).bin $(TARGET).map $(TEST_BIN) \
		$(TEST_VM_PROGS) fs/romfs_image.c tests/test_lxfs.avr.elf \
		$(STM32_TESTS)

# host tests of the code that has no AVR dependency; 不依赖 AVR 的代码的主机测试
HOSTCC ?= cc
# repositories outside the kernel, see external/README
# 内核以外的仓库, 见 external/README
TOOLCHAIN ?= $(CURDIR)/external/toolchain
COREUTILS ?= external/coreutils
PYTHON ?= python3
LXASM := $(PYTHON) $(TOOLCHAIN)/lxasm.py
TEST_BIN := tests/test_vsprintf tests/test_lxfs tests/test_vm tests/test_stm32_flash
TEST_VM_PROGS := tests/vm/arith.lx tests/vm/fault.lx

check: $(TEST_BIN) $(TEST_VM_PROGS)
	./tests/test_vsprintf
	./tests/test_lxfs
	./tests/test_vm
	./tests/test_stm32_flash

%.lx: %.s $(TOOLCHAIN)/lxasm.py
	$(LXASM) $< $@

tests/test_vm: tests/test_vm.c kernel/vm.c include/leonix/vm.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Itests/host -Iinclude -o $@ \
		tests/test_vm.c kernel/vm.c

tests/test_vsprintf: tests/test_vsprintf.c lib/vsprintf.c include/leonix/kernel.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Itests/host -Iinclude -o $@ \
		tests/test_vsprintf.c lib/vsprintf.c

tests/test_lxfs: tests/test_lxfs.c fs/lxfs.c include/leonix/lxfs.h include/leonix/fs.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Itests/host -Iinclude -o $@ \
		tests/test_lxfs.c fs/lxfs.c

# The STM32 Flash backend with lxfs on top, its registers from
# tests/fake_stm32.  -O2 because it remounts once for every program and
# erase of the rewrites it interrupts.
#
# STM32 的 Flash 后端及其上的 lxfs, 寄存器取自 tests/fake_stm32. 以 -O2 编译,
# 因为该测试对其所中断的每次重写中的每一次编程与擦除都重新挂载一次.
tests/test_stm32_flash: tests/test_stm32_flash.c drivers/stm32_flash.c fs/lxfs.c \
		tests/fake_stm32/asm/stm32f1.h tests/fake_stm32/asm/sections.h \
		include/leonix/lxfs.h include/leonix/fs.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -Itests/fake_stm32 -Itests/host \
		-Iinclude -o $@ tests/test_stm32_flash.c fs/lxfs.c

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

AVR_CC ?= avr-gcc

tests/test_lxfs.avr.elf: tests/test_lxfs.c tests/qemu_uart.c fs/lxfs.c \
		include/leonix/lxfs.h include/leonix/fs.h
	$(AVR_CC) -mmcu=atmega2560 -Os -std=gnu11 -Wall -Wextra -Werror -Iarch/avr/include \
		-Iinclude -o $@ tests/test_lxfs.c tests/qemu_uart.c fs/lxfs.c

check-qemu: tests/test_lxfs.avr.elf
	sh tests/run-qemu.sh $(QEMU_AVR) $(QEMU_TIMEOUT) $<

# Test programs for an STM32F103 board, see tests/stm32/stm32test.py.
# check-stm32 runs the instruction checks on qemu-system-arm's
# stm32vldiscovery and needs no board; stm32test.py --stlink and
# signal_check.py run them on one.
#
# STM32F103 板的测试程序, 见 tests/stm32/stm32test.py. check-stm32 在
# qemu-system-arm 的 stm32vldiscovery 上运行指令检查, 不需要板子; 在板上运行
# 用 stm32test.py --stlink 与 signal_check.py.
ARM_CC ?= arm-none-eabi-gcc
QEMU_ARM ?= qemu-system-arm
STM32_CFLAGS := -mcpu=cortex-m3 -mthumb -O2 -std=gnu11 -ffreestanding \
	-nostdlib -Wall -Wextra -Werror
STM32_TESTS := tests/stm32/isa.elf tests/stm32/periph.elf tests/stm32/signal.elf \
	tests/stm32/crcreset.elf tests/stm32/usartidle.elf

tests/stm32/%.elf: tests/stm32/%.c tests/stm32/runtime.c tests/stm32/runtime.h \
		tests/stm32/stm32.ld
	$(ARM_CC) $(STM32_CFLAGS) -T tests/stm32/stm32.ld -Wl,-e,reset_handler \
		-o $@ $(filter %.c %.S,$^)

tests/stm32/isa.elf: tests/stm32/isa_leaf.S tests/stm32/isa.h

stm32-tests: $(STM32_TESTS)

check-stm32: tests/stm32/isa.elf
	$(PYTHON) tests/stm32/stm32test.py --qemu --qemu-bin $(QEMU_ARM) $<

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

.PHONY: all disasm size clean mrproper coreutils check check-qemu htmldocs FORCE \
	cleandocs check-stm32 stm32-tests $(config-targets)
