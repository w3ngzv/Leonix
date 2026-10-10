// SPDX-License-Identifier: GPL-2.0-only
/*
 * The table behind /bin.  It is an explicit list, kept in alphabetical
 * order because ls /bin prints it as it stands.
 */
#include <avr/pgmspace.h>

#include <leonix/binfs.h>

#include "bin.h"

#define PROGRAM(name)	{ name##_name, name##_main }
#define NAME(name)	static const char name##_name[] PROGMEM = #name

NAME(cat);
NAME(df);
NAME(dmesg);
NAME(echo);
NAME(help);
NAME(ls);
NAME(ps);
NAME(rm);
NAME(uptime);

const struct program programs[] PROGMEM = {
	PROGRAM(cat),
	PROGRAM(df),
	PROGRAM(dmesg),
	PROGRAM(echo),
	PROGRAM(help),
	PROGRAM(ls),
	PROGRAM(ps),
	PROGRAM(rm),
	PROGRAM(uptime),
};

const uint8_t nr_programs = sizeof(programs) / sizeof(programs[0]);
