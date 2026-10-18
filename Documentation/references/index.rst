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

STM32F103 与 SWD
----------------

drivers/swd.c 经 SWD 读取 STM32F103 的标识寄存器, 用于鉴定一块印字为
STM32F103C8T6 的核心板 (2026-10-18). 下列文献是该驱动的寄存器地址与鉴定
结论的依据. ST 的文档均声明 All rights reserved. 前三份的副本取自
MediaWiki 仓库的 sources/pdf/, SHA-256 与该仓库的 CHECKSUMS.md 一致.

STM32F103x8B-datasheet-DS5319-rev20.pdf
  STMicroelectronics, STM32F103x8, STM32F103xB 数据手册, DS5319 Rev 20,
  2025 年 7 月. 订货代码 (第 7 节, C8 为 64 KB) 与引脚表 (PA9、PA10、
  PA13、PA14 标为 FT) 的依据. 取自 farnell.com, 未与 st.com 的同号文件
  逐字节比对. SHA-256
  1e54ce18e6c34c5b4a78986f26e245e143748c721009fc9af5953795c30686bc.

  https://www.farnell.com/datasheets/4595259.pdf

STM32F10xxx-RM0008-rev15.pdf
  STMicroelectronics, RM0008 参考手册, DocID13902 Rev 15, 2014 年 6 月.
  Flash 容量寄存器 (30.1.1)、唯一 ID (30.2)、DBGMCU_IDCODE (31.6.1)、
  ROM 表中的 JEDEC-106 代码 (31.6.4) 与 SW-DP 的 ID 0x1BA01477 (31.8.3)
  的依据. 现行版为 Rev 21, 未取得. SHA-256
  12f0c3de375b6c32dfc6aec0f46e61a0ec9393e66ae32c45d9d856ec83dd223e.

  https://www.espruino.com/datasheets/STM32F103xC_ref.pdf

STM32-Cortex-M3-PM0056-rev5.pdf
  STMicroelectronics, PM0056 Cortex-M3 编程手册, DocID15491 Rev 5, 2013 年
  5 月. CPUID 的字段与 STM32F1 的复位值 0x411FC231 的依据. SHA-256
  3917d2c2244da04ca71eaceaaabf6494068bb8f054c8c69dc05e1e9ea892d3f2.

  https://www.eng.auburn.edu/~nelson/courses/elec3040_3050/ARM_Keil%20Documents/Cortex-M3_ProgManual.pdf

STM32F10xx8B-errata-ES096-rev15.pdf
  STMicroelectronics, ES096 STM32F101x8/B, STM32F102x8/B and
  STM32F103x8/B medium-density device errata, Rev 15, 2022 年 3 月. 表 2
  给出 REV_ID 与芯片版本的对应, 2.2.2 节称 DBGMCU_IDCODE 在用户模式下
  读出 0x00, 表 3 中该项对全部版本标为 N. st.com 对脚本请求无响应,
  2026-10-18 取自 Internet Archive 2026-08-25 的快照. SHA-256
  7bcdf58d8b602292d68abb59e181a4f44a038f215d8b01b0167f6d7139285490.

  https://www.st.com/resource/en/errata_sheet/cd00190234-stm32f101x8-b-stm32f102x8-b-and-stm32f103x8-b-medium-density-device-limitations-stmicroelectronics.pdf

STM32F10xxx-flash-PM0075-rev2.pdf
  STMicroelectronics, PM0075 STM32F10xxx Flash memory microcontrollers
  编程手册, Doc ID 17863 Rev 2, 2012 年 8 月. Flash 控制器 (FPEC) 的
  KEYR、SR、CR、AR 寄存器与解锁密钥, 以及主 Flash 只能按半字编程的规定.
  RM0008 关于 Flash 编程的部分指向该文档. st.com 对脚本请求无响应,
  2026-10-18 取自 Internet Archive, 下载时为 gzip 压缩, 解压后 31 页.
  SHA-256 2296c5d528f71ddc7502baa907a0d6a7473ad10f1c67e8b07bb046f291dd4a8a.

  https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf

下列网页与源码文件于 2026-10-18 保存, 用于判断芯片真伪. 网页属二手资料,
其中的结论在可行处已对照上面的 ST 文档复核.

Bluepill-Diagnostics-v1.640.html
  Terry Porter, Bluepill Diagnostics V1.6 User Guide, 页面注明 2026-09-13
  更新. 四项原厂判据, 其中包括 Flash 容量寄存器为 64 KB 与厂商代码为
  STMicroelectronics. SHA-256
  a79175eaba2ba1e53868b3dfb3c97fd165d7b6e6209f54a8a6e4b4446bdb02fb.

  https://mecrisp-stellaris-folkdoc.sourceforge.io/bluepill-diags-v1.640.html

eevblog-2020-stm32f1-dbgmcu-idcode.html
  EEVblog 论坛帖, STM32F1xx reading 0xE0042000, the 'MCU device ID code',
  2020 年 2 月. 引述 ES096 2.2.2 节, 并记录原厂 STM32F103C8 的用户程序
  在未连接调试器时读出 0. SHA-256
  b6744146694647839c2df6c3d51fe5b78f13712eb6fd6f8479f36121bbcdde39.

  https://www.eevblog.com/forum/microcontrollers/stm32f1xx-reading-0xe0042000-the-mcu-device-id-code/msg2899594/

CNX-2020-how-to-detect-stm32-fakes.html
  CNX Software, How to Detect STM32 Fakes, 2020-03-22. 转引 ST 对丝印
  991KA 93 / MYS 807 为假片的答复, 原始出处是下一项. SHA-256
  eb297d93f80af28e5184c633f1e28be9b7c43017ce1741e791817ee6c0a80e51.

  https://www.cnx-software.com/2020/03/22/how-to-detect-stm32-fakes/

Greaseweazle-wiki-STM32-Fakes.html
  keirf/greaseweazle 仓库的 wiki 页 STM32 Fakes. SHA-256
  cd154c4e406cdbaabde18b12e12a84857bf0c0aca96219dee4a67658d58349e4.

  https://github.com/keirf/greaseweazle/wiki/STM32-Fakes

ST-community-f103c8-128kb-flash.html
  ST 社区帖, STM32F103C8T6 with 128kb of flash instead of 64? Fake or
  genuine?. SHA-256
  da3e421ebedcf14a78710f748292394016c78f3e20612955339c0119c30c749c.

  https://community.st.com/stm32-mcus-products-25/stm32f103c8t6-with-128kb-of-flash-instead-of-64-fake-or-genuine-51319

deividAlfa-stm32-programming.md
  deividAlfa/stm32_soldering_iron_controller 仓库的
  Readme_files/Programming.md, 读取时 master 位于 b13cdf4b90f0. 列出原厂
  STM32F101/102/103 Cx/Rx 可能出现的 DBGMCU_IDCODE 与 Flash 容量寄存器
  取值. SHA-256
  9104d5b7372222eea3cda9b4a3a6e473ac7f521a8ea7b05726b3b9a19ab1475e.

  https://github.com/deividAlfa/stm32_soldering_iron_controller/blob/master/Readme_files/Programming.md

OpenOCD-jep106.inc, OpenOCD-arm_coresight.h
  OpenOCD 源码 src/helper/jep106.inc 与 src/target/arm_coresight.h, 读取
  时 master 位于 bb5a52da599c. 前者把 JEP106 代码映射到厂商名 (第 0 组
  0x20 为 STMicroelectronics), 后者给出由 PIDR 求零件号与设计厂商的位域.
  按 GPL-2.0-or-later 发布, 可以再分发, 为与本节其余文件一致仍不纳入
  仓库. SHA-256 分别为
  92386f15139654b67e7756dcadd25881264bb7d8e8ea88386b96095855f5db5f 与
  c99cdcfda2dbb34096ddd023f852484e48cf1dfcf136efacbd49cbfba004e36c.

  https://github.com/openocd-org/openocd/tree/master/src

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
