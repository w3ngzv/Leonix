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

  - USB 控制器已被初始化, 只做了断开. USB_Detach() 出自 LUFA, 其源码本文
    未核对. 如果它只置位 UDCON 的 DETACH, 那么 USBCON 与 UDIEN 中的 USB
    中断使能位仍然保持置位, 此时在 USB 向量为空的情况下打开中断, CPU 会
    进入 bad-interrupt 处理程序.

  - 为 USB 启动的 PLL 仍在运行.

  - LED_SETUP() 已把 DDRC7、DDRB0、DDRD5 设为输出.

  - CLKPR 已被写为 1 分频. 低字节 0xff 下 CKDIV8 未编程, 上电路径同样运行
    在 16 MHz, 这一项两条路径一致.

启动代码无法区分这两条路径, 因为它运行时 MCUSR 已经被清零. 所以在第一条
``sei`` 之前, 启动代码必须无条件地把 USB 控制器与 PLL 置于已知状态.

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

Micro (PID 0x0037) 的 TX、RX 极性相反. 以上尚未与原理图对照, 在板子上点亮
PC7 是第一项测试.


5. 重新烧录
-----------

应用中没有 CDC 端口, 1200 波特率复位的办法因此失效. 烧录时需要手动按下
复位键, 并在 bootloader 的等待窗口内启动 avrdude::

  avrdude -p m32u4 -c avr109 -P <port> -U flash:w:leonix.hex:i

8 秒这个数值取自源码, 尚未在这块板子上实测.


6. 待办
-------

  - 从板子上读回熔丝位.
  - 确认 L LED 接在 PC7.
  - 实测按下复位键之后 bootloader 的等待时长.
  - 查阅 LUFA 的 USB_Detach(), 确定启动代码中最小的 USB 关闭序列.
