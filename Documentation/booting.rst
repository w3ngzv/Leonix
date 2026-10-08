.. SPDX-License-Identifier: GPL-2.0-only

==================================
在 Arduino Leonardo 上引导 Leonix
==================================

本文记录控制权到达应用区 0x0000 时, 硬件与出厂 bootloader 留下的状态.
arch/avr 下的启动代码依据这些事实编写, 因此每一项都注明出处. 尚未在
实物上核对的项目, 文中逐一注明.

出处见 references/index.rst, 本文用到其中两项:

  - ATmega16U4/32U4 数据手册, 7766J
  - Caterina bootloader 源码与 boards.txt

出厂烧录的 Caterina 二进制由该源码的某一修订版编译而成, 具体是哪一版
无从得知. 下文描述的是现行源码的行为, 与板上的程序未必完全一致.


1. 存储器布局
-------------

Flash 容量为 32 KiB. Arduino 为 Leonardo 设定的熔丝位是::

  low      0xff
  high     0xd8
  extended 0xcb

高字节 0xd8 编程了 BOOTRST, BOOTSZ1..0 为 00, 对应最大的引导区
(数据手册表 27-8):

  ============  ===============  ===============
  区段          字地址           字节地址
  ============  ===============  ===============
  应用区        0x0000 - 0x37ff  0x0000 - 0x6fff
  引导区        0x3800 - 0x3fff  0x7000 - 0x7fff
  ============  ===============  ===============

Leonix 的映像因此不得超过 28672 字节. 每次复位都从 bootloader 开始执行,
不会直接进入 0x0000.

同一字节中 JTAGEN 未编程, PF4 至 PF7 是普通端口引脚, MCUCR 的 JTD 位
无需处理. WDTON 同样未编程, 看门狗可以由软件关闭.

扩展字节 0xcb 中 HWBE 未编程, HWB 引脚不参与 bootloader 的选择.

数据存储空间 (数据手册 5.2 节)::

  0x0000 - 0x001f   寄存器文件
  0x0020 - 0x005f   I/O 寄存器
  0x0060 - 0x00ff   扩展 I/O 寄存器
  0x0100 - 0x0aff   片内 SRAM, 2560 字节

栈指针的初值为 RAMEND, 即 0x0aff.

以上熔丝位是 Arduino 烧录时使用的值, 尚未从这块板子上读回. Caterina 实现了
AVR109 协议的熔丝读取命令, 可以经由 bootloader 用 avrdude 读出.


2. 中断向量
-----------

连同复位在内共有 43 个向量 (数据手册表 9-1). 每项占两个字, 正好容纳一条
``jmp``, 整张表占据字地址 0x0000 至 0x0055, 共 172 字节. 第 6、7、9 项与
第 14 至 16 项为保留项, 由于向量按位置索引, 这些位置同样要填入表项.

只有 MCUCR 的 IVSEL 为 0 时, 向量才取自应用区. Caterina 运行期间把 IVSEL
置 1, 在 StartSketch() 跳转之前清零, 所以应用可以认定进入时 IVSEL 为 0.


3. 离开 bootloader
------------------

Caterina 的 main() 先读取 MCUSR, 随即写 0, 再关闭看门狗. 之后按复位原因
分为三种情形:

  - 上电复位且应用区非空: 立即调用 StartSketch(), bootloader 的硬件初始化
    一项都没有执行.

  - 看门狗复位且没有 boot key: 与上电复位相同.

  - 外部复位 (按下复位键), 或带 boot key 的看门狗复位: bootloader 开始
    运行, 启用 USB, 启动 Timer1, 等待编程器连接. 若 TIMEOUT_PERIOD
    (8000 个 1 ms 节拍) 内没有收到命令, 则断开 USB 并调用 StartSketch().

StartSketch() 关闭中断, 停止 Timer1 并清零 TIMSK1 与 TCNT1, 清零 IVSEL,
熄灭三个 LED, 然后跳转到 0x0000.

两条路径交给应用的机器状态并不相同. 上电复位之后外设处于复位值. 8 秒超时
之后:

  - USB 控制器已被初始化, 只做了断开. Caterina 依据 LUFA-111009 编译,
    该版本的 USB_Detach() 只执行 ``UDCON |= (1 << DETACH)``. USBCON 的
    USBE、OTGPADE、VBUSTE, UHWCON 的 UVREGE, 以及 UDIEN 的 SUSPE 与
    EORSTE 都保持置位. 此时在 USB 向量为空的情况下打开中断, 一次 VBUS
    变化就会使 CPU 进入 bad-interrupt 处理程序.

  - PLLFRQ 已被写为 ``(1 << PLLUSB) | (1 << PDIV3) | (1 << PDIV1)``.
    PLL 本身只在 USB General 中断检测到 VBUS 时才由 LUFA 启动, 所以
    超时时 PLL 是否在运行取决于 USB 线是否连接. 独立供电、不接 USB 线时,
    PLL 未启动.

  - LED_SETUP() 已把 DDRC7、DDRB0、DDRD5 设为输出.

  - CLKPR 已被写为 1 分频. 低字节 0xff 下 CKDIV8 未编程, 上电路径同样运行
    在 16 MHz, 这一项两条路径一致.

启动代码无法区分这两条路径, 因为它运行时 MCUSR 已经被清零. 所以在第一条
``sei`` 之前, 启动代码必须无条件地把 USB 控制器与 PLL 置于已知状态.

USB 关闭序列由以下写入组成, 每一项写入的都是数据手册给出的复位值, 在上电
路径上重复执行不产生影响::

  USBCON = 1 << FRZCLK;   /* 0x20 */
  USBINT = 0;
  UHWCON = 0;
  PLLCSR = 0;
  PLLFRQ = 1 << PDIV2;    /* 0x04 */

数据手册 21.6.2 节与 22.2 节说明, 清除 USBE 等同于对 USB 控制器的一次
硬件复位, UDCON、UDIEN、UDINT 与各端点寄存器随之恢复复位值, DETACH 重新
置位, 因此这些寄存器不必逐个写入. USBCON 的 OTGPADE 与 VBUSTE、USBINT、
UHWCON 属于 USB 通用寄存器, PLLCSR 与 PLLFRQ 属于时钟系统, 都不在这次
复位的范围内, 需要显式写入. 写入期间中断已由 StartSketch() 关闭.

LUFA 的 USB_Disable() 执行的是同一组操作, 只是顺序为先关中断使能、再清
USBE, 并且不置位 FRZCLK, 也不复原 PLLFRQ. 上面的序列尚未在板子上验证.

这组写入并不要求替换 avr-libc 的 gcrt1.S. avr-libc 为启动过程预留了
.init0 至 .init9 段, 把上述写入放进 .init3 段, 同样会在中断打开之前执行.
Leonix 改用 arch/avr/start.S, 是为了让复位向量到 main() 之间的每一条指令
都出自本项目.

8 秒超时还带来一个后果. 按下复位键之后, 只要应用区非空, Caterina 就会等满
8 秒才进入应用, 是否连接 USB 线都一样. 独立运行时, 每次手动复位都有这
8 秒的延迟. 不经过这段等待的只有上电复位与不带 boot key 的看门狗复位.

Caterina 还把 SRAM 地址 0x0800 处的一个字用作 boot key. 该处写入 0x7777
之后再发生看门狗复位, bootloader 会保持运行. Arduino core 在主机以 1200
波特率打开 CDC 端口时写入该值. Leonix 没有 CDC 端口, 不会写入 boot key,
.data 与 .bss 可以覆盖这个地址.


4. LED
------

依据 Caterina.h 中对应 Leonardo 产品 ID 的定义:

  ======  ====  ===========
  LED     引脚  点亮电平
  ======  ====  ===========
  L       PC7   高
  TX      PD5   低
  RX      PB0   低
  ======  ====  ===========

Micro (PID 0x0037) 的 TX、RX 极性相反. 2026-10-08 在板子上用 init/main.c
翻转 PC7, L LED 随之闪烁, L 一行由此确认. TX 与 RX 两行尚未实测.


5. 重新烧录
-----------

应用中没有 CDC 端口, 1200 波特率复位的办法因此失效. 烧录时需要手动按下
复位键, 并在 bootloader 的等待窗口内启动 avrdude::

  avrdude -p m32u4 -c avr109 -P <port> -U flash:w:leonix.hex:i

2026-10-08 在板子上按下复位键, 呼吸灯持续约 8 秒后进入应用, 与源码中的
TIMEOUT_PERIOD 一致. 该时长为目测, 未用仪器计时.


6. 待办
-------

  - 从板子上读回熔丝位.
  - 用外接电源检验 USB 关闭序列中的 VBUSTE 一项. 2026-10-08 插着 USB 线
    按下复位键, 超时之后 L LED 恢复 0.5 秒的翻转, 说明超时路径上打开中断
    之后没有 USB 中断进入 bad-interrupt 处理程序. 但测试期间 VBUS 一直
    存在, 没有发生 VBUS 变化, VBUSTE 是否已被清除因此尚未检验. 检验方法是
    由圆口插座或 VIN 供电, 按下复位键等待 Leonix 开始闪烁, 再拔下 USB 线,
    L LED 应当继续翻转.
