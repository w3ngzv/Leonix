# SPDX-License-Identifier: GPL-2.0-only
MCU      := atmega32u4
F_CPU    := 16000000UL
TARGET   := leonix

# Caterina occupies the top 4 KiB of the 32 KiB flash.
APP_FLASH_SIZE := 28672

OBJS := arch/avr/start.o arch/avr/switch.o arch/avr/time.o arch/avr/i2c.o \
	arch/avr/panic.o kernel/sched.o drivers/lcd.o init/main.o

CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
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

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

disasm: $(TARGET).elf
	$(OBJDUMP) -d $<

size: $(TARGET).elf
	$(SIZE) $<

# Press RESET on the board, then run within the 8 s bootloader window:
#   make flash PORT=/dev/cu.usbmodemXXXX
flash: $(TARGET).hex
	@test -n "$(PORT)" || { echo "usage: make flash PORT=/dev/cu.usbmodemXXXX"; exit 1; }
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
