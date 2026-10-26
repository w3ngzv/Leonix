#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Check the coding style: scripts/checkpatch.pl and scripts/kernel-doc,
# both taken from Linux, and scripts/checkwidth.py for lines of Chinese.
# checkpatch reads its options from .checkpatch.conf.
#
#   scripts/checkstyle.sh              every tracked C and assembly file
#   scripts/checkstyle.sh FILE...      the files named
#   scripts/checkstyle.sh -g RANGE     each commit in RANGE, e.g. main..
#
# scripts/kconfig and the macros in include/leonix/kconfig.h are left out
# of the first form: they are Linux's code, kept as Linux has it, and
# checkpatch warns about Linux's own copy of the macros too.  So is
# Documentation/references, which holds other projects' files verbatim.
# Exits 1 if any check reports a problem.  -g runs checkpatch alone.
#
# 检查代码风格: 使用取自 Linux 的 scripts/checkpatch.pl 与 scripts/kernel-doc,
# 以及检查中文行宽的 scripts/checkwidth.py. checkpatch 从 .checkpatch.conf 读取
# 选项.
#
# 第一种用法不检查 scripts/kconfig 与 include/leonix/kconfig.h 中的宏: 这些是
# Linux 的代码, 保持 Linux 的原样, 且 checkpatch 对 Linux 自身的这些宏同样报
# 警告. Documentation/references 保存其他项目的原样文件, 同样不检查.
# 任何一项检查报告问题时以 1 退出. -g 只运行 checkpatch.
cd "$(dirname "$0")/.." || exit 1
PYTHON=${PYTHON:-python3}

if [ "$1" = -g ]; then
	[ -n "$2" ] || { echo "usage: $0 -g RANGE" >&2; exit 2; }
	exec scripts/checkpatch.pl --terse --show-types -g "$2"
fi
if [ $# -eq 0 ]; then
	set -- $(git ls-files -- '*.c' '*.h' '*.S' ':!scripts/kconfig' \
		':!include/leonix/kconfig.h' ':!Documentation/references')
fi

# Each grep succeeds only if it printed something, i.e. found a problem.
# 每个 grep 只在打印了内容, 即发现问题时才成功.
status=0
scripts/checkpatch.pl --terse --show-types --summary-file -f "$@" |
	grep -v ': total: 0 errors, 0 warnings,' && status=1

csrc=
for f; do
	case $f in *.c|*.h) csrc="$csrc $f" ;; esac
done
if [ -n "$csrc" ]; then
	scripts/kernel-doc -none -Wall $csrc 2>&1 | grep . && status=1
fi

$PYTHON scripts/checkwidth.py "$@" || status=1
exit $status
