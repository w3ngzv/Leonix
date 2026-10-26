#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# run-qemu.sh QEMU TIMEOUT ELF: run a test on an emulated ATmega2560 and
# pass if it prints "all passed".  A finished test spins in exit(), so
# QEMU is stopped once a result line appears, or after TIMEOUT seconds.
qemu=$1 timeout=$2 elf=$3
out=$(mktemp)
trap 'rm -f "$out"' EXIT

"$qemu" -machine mega2560 -bios "$elf" -nographic -serial file:"$out" \
	-monitor none 2>/dev/null &
pid=$!
n=0
while [ $n -lt "$timeout" ] && ! grep -q -e 'all passed' -e 'failed' "$out"; do
	sleep 1
	n=$((n + 1))
done
kill $pid 2>/dev/null
wait $pid 2>/dev/null
tr -d '\r' < "$out"
grep -q 'all passed' "$out" || { echo "$elf: no pass after ${timeout}s"; exit 1; }
