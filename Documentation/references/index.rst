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
