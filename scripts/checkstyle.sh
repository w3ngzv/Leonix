#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Check the coding style with scripts/checkpatch.pl, taken from Linux,
# which reads its options from .checkpatch.conf.
#
#   scripts/checkstyle.sh              every tracked C and assembly file
#   scripts/checkstyle.sh FILE...      the files named
#   scripts/checkstyle.sh -g RANGE     each commit in RANGE, e.g. main..
#
# scripts/kconfig is left out of the first form: it is Linux's code, kept
# as Linux has it.  Exits 1 if checkpatch reports an error or a warning.
cd "$(dirname "$0")/.." || exit 1

if [ "$1" = -g ]; then
	[ -n "$2" ] || { echo "usage: $0 -g RANGE" >&2; exit 2; }
	exec scripts/checkpatch.pl --terse --show-types -g "$2"
fi
if [ $# -gt 0 ]; then
	exec scripts/checkpatch.pl --terse --show-types -f "$@"
fi
git ls-files -z -- '*.c' '*.h' '*.S' ':!scripts/kconfig' |
	xargs -0 scripts/checkpatch.pl --terse --show-types --summary-file -f |
	grep -v ': total: 0 errors, 0 warnings,'
# grep's status says whether anything was printed; checkpatch's is lost
# in the pipe, so the result is taken from what remains.
[ $? -ne 0 ]
