# SPDX-License-Identifier: GPL-2.0-only
MCU      := atmega32u4
F_CPU    := 16000000UL
TARGET   := leonix

# Caterina occupies the top 4 KiB of the 32 KiB flash.
APP_FLASH_SIZE := 28672

# The baud rate that sends a running Leonix to the bootloader.
BOOTLOADER_BAUD := 1200

OBJS := arch/avr/start.o arch/avr/switch.o arch/avr/time.o arch/avr/i2c.o \
	arch/avr/usb.o arch/avr/panic.o kernel/sched.o kernel/printk.o drivers/lcd.o \
	lib/vsprintf.o init/main.o

CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
NM      := avr-nm
SIZE    := avr-size

CPPFLAGS := -DF_CPU=$(F_CPU) -Iinclude
CFLAGS   := -mmcu=$(MCU) -std=c11 -Os -g -Wall -Wextra -Werror
ASFLAGS  := -mmcu=$(MCU) -g -Wall -Werror
LDFLAGS  := -mmcu=$(MCU) -nostartfiles \
	    -Wl,--defsym=__TEXT_REGION_LENGTH__=$(APP_FLASH_SIZE) \
	    -Wl,-Map=$(TARGET).map

all: $(TARGET).hex

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c -o $@ $<

# Caterina's boot key is at SRAM 0x0800, see arch/avr/usb.c.
BOOT_KEY_ADDR := 0x800800

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@bss_end=$$($(NM) $@ | awk '$$3 == "__bss_end" { print $$1 }'); \
	test $$((0x$$bss_end)) -le $$(($(BOOT_KEY_ADDR))) || { \
		echo ".bss ends at 0x$$bss_end, past the boot key"; rm -f $@; exit 1; }

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

disasm: $(TARGET).elf
	$(OBJDUMP) -d $<

size: $(TARGET).elf
	$(SIZE) $<

# make flash PORT=/dev/cu.usbmodemXXXX
#
# Opening the port at 1200 baud and closing it restarts a running Leonix
# into Caterina.  The port then leaves and comes back as the bootloader's,
# under the same name on macOS and usually on Linux.  If it never leaves,
# the board is taken to be in the bootloader already, for instance after
# a press of RESET, and avrdude runs straight away.  Waits are in tenths
# of a second.
FLASH_DETACH_WAIT := 10
FLASH_ATTACH_WAIT := 80

flash: $(TARGET).hex
	@test -n "$(PORT)" || { echo "usage: make flash PORT=/dev/cu.usbmodemXXXX"; exit 1; }
	@test -e "$(PORT)" || { echo "$(PORT) not found"; exit 1; }
	@stty -f $(PORT) $(BOOTLOADER_BAUD) 2>/dev/null || stty -F $(PORT) $(BOOTLOADER_BAUD)
	@n=0; while [ -e "$(PORT)" ] && [ $$n -lt $(FLASH_DETACH_WAIT) ]; do \
		sleep 0.1; n=$$((n + 1)); done; \
	n=0; while [ ! -e "$(PORT)" ] && [ $$n -lt $(FLASH_ATTACH_WAIT) ]; do \
		sleep 0.1; n=$$((n + 1)); done; \
	test -e "$(PORT)" || { echo "$(PORT) did not come back"; exit 1; }
	avrdude -p m32u4 -c avr109 -P $(PORT) -b 57600 -U flash:w:$<:i

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET).elf $(TARGET).hex $(TARGET).map $(TEST_BIN)

# Host tests of the code in lib/, which has no AVR dependency.
HOSTCC ?= cc
TEST_BIN := tests/test_vsprintf

check: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_vsprintf.c lib/vsprintf.c include/leonix/kernel.h
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude -o $@ \
		tests/test_vsprintf.c lib/vsprintf.c

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

.PHONY: all disasm size flash clean check htmldocs cleandocs
