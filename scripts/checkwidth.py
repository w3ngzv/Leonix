#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Report lines wider than 80 columns as a terminal shows them.

Usage: checkwidth.py FILE...

checkpatch.pl counts characters, so a line of Chinese passes it at
twice the width an editor shows.  Here a wide character takes two
columns and a tab runs to the next multiple of 8.  Only lines with a
wide character are checked; checkpatch already measures the rest.
Exits 1 if any line is reported.

按终端显示的宽度报告超过 80 列的行.

checkpatch.pl 按字符计数, 因此一行中文在编辑器中显示为两倍宽度时仍能通过检查.
本脚本中宽字符占两列, 制表符延伸到下一个 8 的倍数. 只检查含宽字符的行, 其余的行
已由 checkpatch 测量. 报告了任何行时以 1 退出.
"""

import sys
import unicodedata

MAX_COLUMNS = 80
TAB_STOP = 8


def columns(line):
    col = 0
    for c in line:
        if c == '\t':
            col = (col // TAB_STOP + 1) * TAB_STOP
        elif unicodedata.east_asian_width(c) in 'WF':
            col += 2
        else:
            col += 1
    return col


def main(paths):
    wide = 0
    for path in paths:
        with open(path, encoding='utf-8') as f:
            for nr, line in enumerate(f, 1):
                line = line.rstrip('\n')
                if line.isascii():
                    continue
                n = columns(line)
                if n > MAX_COLUMNS:
                    print('%s:%d: %d columns' % (path, nr, n))
                    wide += 1
    return 1 if wide else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
