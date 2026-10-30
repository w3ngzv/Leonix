.. SPDX-License-Identifier: GPL-2.0-only

==============
代码规范
==============

Leonix 的 C 与汇编代码遵循 Linux 126ad898a6e9 (v7.3-16337, 7.4 合并窗口
期间的主线) 的 Documentation/process/coding-style.rst, 本文只列出在其之上
增加的规则. 增加的规则多数来自 Leonix 运行的环境: avr-gcc 的 int 为 16 位,
中断与任务共享 2.5 KiB SRAM, 文件存放在可能随时断电的 EEPROM 上. Linux 的规则
在 Linux 内核中有缺省成立的前提, 这些前提在 ATmega32U4 上并不成立.

规则的写法以工具能够检查为先. 只能由人检查的规则在各节中注明.


检查
====

运行::

  scripts/checkstyle.sh              # 全部源文件
  scripts/checkstyle.sh fs/lxfs.c    # 指定的文件
  scripts/checkstyle.sh -g main..    # 当前分支上的每个提交
  make check                         # 主机测试
  make check-qemu                    # 以 avr-gcc 编译 lxfs 测试, 在 QEMU 中运行

提交之前, 上述命令都应无错误. checkstyle.sh 调用同一提交的
scripts/checkpatch.pl, 选项在 .checkpatch.conf 中. 该文件关闭了五类检查,
每一类要求的设施 Leonix 都没有: compiler_attributes.h、strscpy() 以及
READ_ONCE(). 其余检查与 Linux 相同, 警告与错误同样需要处理.
scripts/kconfig 与 include/leonix/kconfig.h 中的宏是原样导入的 Linux 代码,
不在检查之列; checkpatch.pl 对 Linux 自身的这些宏同样报告警告.
include/leonix/kern_levels.h 与 include/leonix/printk.h 只免于 checkpatch.pl,
``KERN_<LEVEL>`` 与 ``pr_<level>()`` 的写法与 Linux 的同名文件相同,
checkpatch.pl 对 Linux 的这两个文件报告同样的问题.

checkstyle.sh 另外运行两项检查. scripts/kernel-doc 是 Linux 在 683e8cbaba7f
删除 Perl 版本之前的最后一版, 即 de258fa8ca8d 中的 scripts/kernel-doc.pl, 以
``-none -Wall`` 检查 kernel-doc 注释与函数声明是否一致. scripts/checkwidth.py
检查含中文的行: checkpatch.pl 按字符计数, 一个汉字在终端中却占两列, 该脚本按
显示宽度计算, 上限为 80 列.


文件开头
========

每个 .c、.h 与 .S 文件的第一行是 SPDX 许可证标识, 随后的注释说明该文件做什么,
由谁调用, 以及对应 Linux 中的哪一部分 (若有). 读者打开任何一个文件, 应当
不必查阅其他文件就能知道它在系统中的位置. fs/lxfs.c 与 fs/vfs.c 的开头可作
示例. 该规则由人检查.


注释
====

注释的用途是让读者看懂代码中不显然的部分: 名字与参数说明不了的函数,
看起来多余或反常的写法, 以及代码依赖的约束. 代码本身已经说明的内容不写.
注释的写法取 Linux 2010 至 2018 年间的形态:

- 函数的作用、调用上下文或返回值无法从名字与参数看出时, 在定义处写
  kernel-doc 注释 (Linux 的 Documentation/doc-guide/kernel-doc.rst), 只写
  不显然的那几项. 例如 msleep_interruptible() 的返回值与 Linux 的同名函数
  不同, 需要说明; signal_pending() 的名字已说明其作用, 不需要注释.
- 函数内部与行尾的短注释写成片语, 不加句号, 例如 ``/* zero .bss */``.
  写成完整句子的注释以句号结尾.
- 注释尽量短. 一句话能说明的约束不写成一段.
- 第一人称可用 "I" 与 "我", 不必写 "we". Leonix 由一人维护, "we" 没有所指.

注释用中文或英文中的一种写成, 不必同时写两种. 中文使用半角标点, 汉字之间
不加空格. 已有的双语注释不必专门修改, 修改其所在代码时删去其中一种.

依据外部资料的数值与做法, 在注释中写出出处, 写到读者能够查到的粒度:
数据手册的表号或节号、提交号、反汇编中的指令. 例如 drivers/eeprom.c 中的
"datasheet table 5-3" 与 arch/avr/time.c 中 _Static_assert 旁的说明. 出处的
文献放在 Documentation/references 中登记.

尚未完成的工作在需要修改的代码处写成 ``TODO:`` 注释, 说明缺少什么, 理由较长时
指向 Documentation/notes 中的对应文档. 中文段落以 ``待办:`` 开头. 完成该项的
提交同时删除这条注释. Linux 的 coding-style.rst 对此没有规定, 这一写法取自其
代码中的实际用法: 在 126ad898a6e9 的 .c、.h 与 .S 文件中, ``TODO:`` 出现 4074
次, 多于 ``FIXME:`` 的 2636 次与 ``XXX:`` 的 1038 次. Linux 的 staging 驱动
另在目录中放一个 TODO 文件 (Documentation/process/2.Process.rst), Leonix 不采用
这种做法. ``git grep -n 'TODO:'`` 列出全部待办.

注释中不写代码无法保持正确的数字, 例如某函数当前的周期数或栈用量. 这类
测量值写在提交说明与 Documentation/notes 中, 并注明日期与提交号.


命名
====

Linux 的命名规则 (第 4 节) 全部适用. 另加两条:

- 在多个文件中可见的名字用完整的词, 并以子系统为前缀: ``lxfs_open``、
  ``vm_run``、``sched_start``. 只在一个函数内使用的局部变量可以简短.
- 表示物理量的名字带单位: ``_ms``、``_us``、``_bytes``、``_ticks``. 同一个
  ``timeout`` 可以是毫秒也可以是节拍, 单位写进名字, 调用处的错误才看得见.

该节由人检查.


整数宽度
========

avr-gcc 的 ``int`` 为 16 位, 主机编译器的 ``int`` 为 32 位. 同一段代码在
主机测试中通过, 在板上仍可能溢出.

- 存储格式、寄存器与跨模块接口使用 <stdint.h> 的定宽类型.
- 结果可能超过 16 位的运算, 先把一个操作数转换为 ``uint32_t``. 位移量可能
  达到 16 的常量写作 ``(uint32_t)1 << n`` 或 ``1UL << n``.
- 有符号与无符号的比较在两种宽度下结果可能不同, 编译器以 ``-Wextra`` 报告.
  确认两侧的取值范围之后再加转换, 范围不明显时在注释中写出.

能在主机上测试的模块, 同时以 avr-gcc 编译并在 qemu-system-avr 中运行同一
测试 (``make check-qemu``, 目前覆盖 fs/lxfs.c). 该做法在 2026-10-26 查出过
tests/test_lxfs.c 中一处只在 16 位 int 下出现的有符号比较.


常量
====

数值常量以 ``#define`` 命名. 常量之间存在的关系用 ``_Static_assert`` 在编译
时检查, 并以一句话说明断言失败时的含义, 例如 arch/avr/time.c 中定时器分频
必须整除时钟频率. 关系写成断言之后, 修改一个常量时编译器会指出其余常量.


与中断共享的数据
================

中断处理程序与任务共同读写的变量声明为 ``volatile``. 多于一个字节的变量,
在任务中的读写放在 ``local_irq_save()`` 与 ``local_irq_restore()`` 之间
(include/leonix/irqflags.h), 因为 AVR 一条指令只读写一个字节, 中断可能发生
在两个字节之间. kernel/printk.c 的 ``log_newest()`` 是示例.

kernel/、fs/、lib/、init/ 与 include/ 中的代码不包含 avr-libc 的头文件:
开关中断经由 include/leonix/irqflags.h, flash 中的常量经由
include/leonix/pgmspace.h. 驱动与内核代码也不包含 C 库的 <string.h>, 改为
包含 include/leonix/string.h, 做法与 Linux 的 include/linux/string.h 相同:
<asm/string.h> 以 ``__HAVE_ARCH_<NAME>`` 声明架构自己提供的函数, 其余由
lib/string.c 提供. AVR 上全部由 avr-libc 提供, ARM 上全部取自
lib/string.c. arch/avr 中的代码可以直接使用 avr-libc, 例如
arch/avr/time.c 的 ``get_jiffies()`` 使用 ``ATOMIC_BLOCK``. 该节由人检查.


断电与 EEPROM
=============

EEPROM 上的数据结构必须在任何一次复位之后保持一致. 每种修改操作安排一个
最后写入的单字节作为生效点; 在它写入之前复位, 数据保持操作之前的样子, 在它
写入之后复位, 数据是操作之后的样子. 多字节的值需要整体生效时, 先写入日志.
lxfs 的做法见 notes/vfs.rst "复位时的一致性".

不使用 ``eeprom_update_block()`` 写入有先后要求的字节, 该函数从末尾向前写入.
新增写入路径须在 tests/test_lxfs.c 的复位测试中覆盖, 该测试在每个可能的
字节处模拟复位.


错误与返回值
============

可能失败的函数返回 0 或负的 errno, 编号取自 Linux 的
include/uapi/asm-generic/errno-base.h, 定义在 include/leonix/errno.h.
返回值的含义写在函数前的注释中. 这与 Linux 第 16 节一致, 在此重述是因为
avr-libc 自身的函数不遵循这一约定.


现状
====

2026-10-26 在 feature/comment-style 上, checkstyle.sh 的三项检查均无报告.
