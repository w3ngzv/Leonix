.. SPDX-License-Identifier: GPL-2.0-only

========
参考文献
========

本目录收录 Leonix 开发中引用的外部文献. 其余文档引用这些文献时, 以下表中
的编号与修订版为准. 同一文献换用新的修订版时, 需要更新本表, 并复核引用它
的章节.

仓库中只保存许可证允许再分发的文献, 目前只有 Leonardo 原理图. 其余文献的
版权声明禁止再分发, 需要按下列地址自行下载, 以表中的文件名放入本目录.
.git/info/exclude 不随仓库分发, 克隆之后需要自行把这些文件名加进去.

处理器与指令集
--------------

ATmega16U4-32U4-datasheet-7766J.pdf
  Atmel, ATmega16U4/ATmega32U4 Datasheet, 文档号 7766J, 2016 年 4 月.
  存储器布局、中断向量、熔丝位与各外设寄存器的依据. 封底的版权页仍写作
  7766I, 2015 年 7 月, 各页页脚为 7766J.

  https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7766-8-bit-AVR-ATmega16U4-32U4_Datasheet.pdf

AVR-InstructionSet-Manual-DS40002198.pdf
  Microchip, AVR Instruction Set Manual, 文档号 DS40002198C, 2024 年 11 月.
  汇编部分的指令编码、周期数与 SREG 行为的依据.

  https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/ReferenceManuals/AVR-InstructionSet-Manual-DS40002198.pdf

  旧地址 ww1.microchip.com/downloads/en/DeviceDoc/ 下的同名文件是 B 版.

开发板
------

Leonardo-A000057-schematics.pdf
  Arduino, Leonardo (SKU A000057) 原理图, 按 CC BY-SA 4.0 发布. 文件中
  没有修订号与日期信息.

  https://docs.arduino.cc/resources/schematics/A000057-schematics.pdf

外设
----

HD44780U-Hitachi.pdf
  Hitachi, HD44780U (LCD-II) Dot Matrix Liquid Crystal Display
  Controller/Driver, ADE-207-272(Z), Rev. 0.0, 1999 年 9 月.

  该器件已停产, 厂商网站不再提供这份文档. 本目录的副本与下列 SparkFun
  镜像逐字节相同:

  https://cdn.sparkfun.com/assets/9/5/f/7/b/HD44780.pdf

PCF8574-TI.pdf
  Texas Instruments, PCF8574 Remote 8-Bit I/O Expander for I2C Bus,
  SCPS068K, 2001 年 7 月初版, 2024 年 9 月修订.

  https://www.ti.com/lit/ds/symlink/pcf8574.pdf

  本目录的副本与 2026-10-07 从该地址重新下载的文件字节不同, 两者首页
  都是 SCPS068K. 核对时以首页的修订号为准.

UM10204-I2C-NXP.pdf
  NXP, I2C-bus specification and user manual, UM10204, Rev. 7.0,
  2021 年 10 月 1 日. 标准模式的时序 (表 11) 与总线停滞时的恢复办法
  (3.1.16 节) 的依据. 版权页声明 All rights reserved. 本目录的副本于
  2026-10-09 下载, SHA-256 为
  dc91f00f65584e06ef36e26c93bf9d91a95fb3c8a1830a9223e53caf678b36af.

  https://www.nxp.com/docs/en/user-guide/UM10204.pdf

USB
---

USB-2.0-spec-20240604.pdf
  USB-IF, Universal Serial Bus Specification, Revision 2.0, 2000 年 4 月 27
  日. 第 9 章的标准请求码 (表 9-4)、描述符类型 (表 9-5) 与请求错误
  (9.2.7 节), 以及控制传输的包长规定 (5.5.3 节) 的依据. 版权页署名为
  Compaq、Hewlett-Packard、Intel 等七家公司, 声明 All rights reserved. 该文件取自 USB-IF 打包的
  usb_20_20240604.zip, 本目录的副本于 2026-10-10 下载, SHA-256 为
  d39698a33486c399124af92bd02e4f978fd9a836b5cf4e52e6e4633eb1d89f61.

  https://www.usb.org/sites/default/files/usb_20_20240604.zip

USB-CDC-1.2.pdf, USB-CDC-PSTN-1.2.pdf
  USB-IF, Universal Serial Bus Class Definitions for Communications Devices,
  Revision 1.2 (Errata 1), 2010 年 11 月, 以及 Universal Serial Bus
  Communications Class Subclass Specification for PSTN Devices, Revision
  1.2, 2007 年 2 月. 前者给出类代码与功能描述符的通用格式, 后者给出抽象
  控制模型 (ACM) 的请求码 (表 13)、line coding 结构 (表 17) 与 DTR 位
  (表 18). 两份文件的版权页都声明 All rights reserved. 两者取自
  CDC1.2_WMC1.1_012011.zip, 前者在压缩包中名为
  CDC120-20101103-track.pdf, 后者名为 PSTN120.pdf. 本目录的副本于
  2026-10-10 下载, SHA-256 分别为
  38b18b25509dcc906b8dda2b1ac50e1997836f02ed629ad45205d21fe8342f8a 与
  61dff30af2c90d53e53b7de0fc8dc8b6e22f4128442f35abc341a434d49dae19.

  https://www.usb.org/sites/default/files/CDC1.2_WMC1.1_012011.zip

架构的历史资料
--------------

notes/avr.rst 讨论 AVR 为何如此设计, 依据下列资料. 这些资料都没有声明允许
再分发, 处理方式与上面的数据手册相同.

AVR-C-Compiler-CoDesign-Myklebust.pdf
  Gaute Myklebust, The AVR Microcontroller and C Compiler Co-Design,
  ATMEL Development Center, Trondheim. 6 页, 未注明日期. PDF 元数据的
  生成时间为 1997-12-17, 文中引用的最新文献是 1996 年 5 月的 AVR 数据手册.
  记录了指令集定型之前, 应 IAR Systems 的编译器开发者的意见所做的修改.

  Atmel 原地址 www.atmel.com/dyn/resources/prod_documents/compiler.pdf
  已失效. 本目录的副本 2026-10-08 取自 Unicamp 的课程镜像, SHA-256 为
  9780e7b468abcc58b65bcdcb75ef3e22dcf2ad2a24a3973fa9dca70a7e4f86d2:

  https://ic.unicamp.br/~celio/mc404-2004/Atmel_AVR/C_compiler.pdf

Atmel-blog-2014-08-21-Wollan.html, Atmel-blog-2014-08-25-Wollan.html
  Paul Rako, Atmel 官方博客 Bits & Pieces, 2014-08-21 与 2014-08-25 两篇.
  内容是作者对 Vegard Wollan 访谈视频的转述, 页面中没有 Wollan 的直接
  引语, 视频本身未核对. 本目录的副本于 2026-10-08 保存.

  https://atmelcorporation.wordpress.com/2014/08/21/vegard-wollan-on-inventing-the-avr-chip/
  https://atmelcorporation.wordpress.com/2014/08/25/more-avr-history-with-vegard-wollan/

未收录的源码
------------

下列文献以源码形式存在, 本目录只记录其位置, 不保存副本.

Caterina bootloader
  arduino/ArduinoCore-avr 仓库, bootloaders/caterina/Caterina.c 与
  Caterina.h, 以及 boards.txt 的 leonardo.* 条目, 读取时 ArduinoCore-avr 的 master 分支位于
  11b9130371e8. Caterina.c 最后一次被修改是在 1668039101dd (2012-12-07).
  出厂烧录的二进制对应哪一修订版无从得知.

  https://github.com/arduino/ArduinoCore-avr/tree/master/bootloaders/caterina

LUFA-111009
  Caterina 的 Makefile 中 LUFA_PATH 指向的版本. 引用的是
  LUFA/Drivers/USB/Core/AVR8/ 下的 USBController_AVR8.c、
  USBController_AVR8.h 与 USBInterrupt_AVR8.c.

  https://github.com/abcminiuser/lufa/tree/LUFA-111009
