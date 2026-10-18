# SPDX-License-Identifier: GPL-2.0-only
MCU      := atmega32u4
F_CPU    := 16000000UL
TARGET   := leonix

# Caterina occupies the top 4 KiB of the 32 KiB flash.
APP_FLASH_SIZE := 28672

# The baud rate that sends a running Leonix to the bootloader.
BOOTLOADER_BAUD := 1200

# Each directory lists its objects in its own Makefile as Linux's obj-y,
# read here in place of Kbuild's descent into the directory.
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

CPPFLAGS := -DF_CPU=$(F_CPU) -Iinclude
CFLAGS   := -mmcu=$(MCU) -std=c11 -Os -g -Wall -Wextra -Werror
ASFLAGS  := -mmcu=$(MCU) -g -Wall -Werror
# SRAM layout, see include/leonix/memlayout.h; data addresses carry the
# linker's 0x800000 offset.  The boot stack between .bss and the boot key
# must hold main() and panic(), about 90 bytes each by -fstack-usage.
BOOT_KEY_ADDR     := 0x800800
TASK_STACKS_START := 0x800802
SRAM_END          := 0x800b00
BOOT_STACK_MIN    := 128

LDFLAGS  := -mmcu=$(MCU) -nostartfiles \
	    -Wl,--defsym=__TEXT_REGION_LENGTH__=$(APP_FLASH_SIZE) \
	    -Wl,--section-start=.noinit=$(TASK_STACKS_START) \
	    -Wl,-Map=$(TARGET).map

all: $(TARGET).hex

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c -o $@ $<

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@bss_end=$$($(NM) $@ | awk '$$3 == "__bss_end" { print $$1 }'); \
	set -- $$($(OBJDUMP) -h $@ | awk '$$2 == ".noinit" { print $$3, $$4 }'); \
	noinit_end=$$((0x$$1 + 0x$$2)); \
	test $$((0x$$bss_end + $(BOOT_STACK_MIN))) -le $$(($(BOOT_KEY_ADDR))) || { \
		echo ".bss ends at 0x$$bss_end, too close to the boot key"; rm -f $@; exit 1; }; \
	test $$noinit_end -le $$(($(SRAM_END))) || { \
		echo ".noinit ends past the end of SRAM"; rm -f $@; exit 1; }

# /bin, packed from the programs Leonix_coreutils builds.  The programs
# are rebuilt on every make; their own Makefile decides what is stale.
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

# HEX names another image to write, such as a diagnostic build.
HEX ?= $(TARGET).hex

flash: $(HEX)
	$(enter_bootloader)
	$(AVRDUDE) -D -U flash:w:$(HEX):i

# Read the fuses and lock bits back through Caterina, in hex.
fuses:
	$(enter_bootloader)
	$(AVRDUDE) -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET).elf $(TARGET).hex $(TARGET).map $(TEST_BIN) \
		$(TEST_VM_PROGS) fs/romfs_image.c

# Host tests of the code in lib/, which has no AVR dependency.
HOSTCC ?= cc
# Repositories outside the kernel, see external/README.
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

# HTML documentation in Documentation/output, as "make htmldocs" in Linux.
# Missing Sphinx only skips the docs; the firmware build does not need it.
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

.PHONY: all disasm size flash fuses clean coreutils check htmldocs cleandocs
