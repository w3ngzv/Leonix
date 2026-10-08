.. SPDX-License-Identifier: GPL-2.0-only

==============
构建与烧录
==============

本文说明如何把 Leonix 编译成映像并写入 Arduino Leonardo. 板子开机之后为何
需要这些步骤, 见 booting.rst; 启动代码与链接设置的理由, 见 notes/boot.rst.


工具链
======

开发使用的版本如下, 均由 macOS 上的 Homebrew 安装:

  ============  =======  ==============================
  软件包        版本     来源
  ============  =======  ==============================
  avr-gcc@9     9.5.0    osx-cross/avr
  avr-binutils  2.46.0   osx-cross/avr, avr-gcc 的依赖
  avrdude       8.3      Homebrew
  ============  =======  ==============================

avr-libc 的头文件随 avr-gcc@9 一同安装. 其他平台与其他版本尚未测试.

安装::

  brew tap osx-cross/avr
  brew install avr-gcc@9 avrdude

确认安装::

  avr-gcc --version
  avrdude -v

第一行应显示 9.5.0. 第二条命令以 Avrdude version 8.3 开头.

构建不需要 Documentation/references 中的数据手册.


构建
====

在仓库根目录运行::

  make

生成三个文件:

leonix.elf
  带调试信息的可执行文件. avr-objdump 与 avr-nm 读取的是这个文件.

leonix.hex
  Intel HEX 格式的映像, avrdude 烧录的是这个文件.

leonix.map
  链接器的映射文件, 记录每个符号的地址, 以及从哪些库中取用了哪些目标文件.

其余目标:

``make size``
  显示各段的大小.

``make disasm``
  输出反汇编.

``make clean``
  删除全部生成的文件.

``make size`` 的输出示例 (提交 de4c9d8)::

     text    data     bss     dec     hex filename
     1106       2     272    1380     564 leonix.elf

读法如下:

- 占用的 Flash 为 text 加 data, 即 1108 字节. ``.data`` 的初值同样存放在
  Flash 中, 由启动代码复制到 SRAM.
- 静态占用的 SRAM 为 data 加 bss, 即 274 字节. SRAM 共 2560 字节, 其余部分
  留给 main() 使用的启动栈.
- Flash 的上限是 28672 字节. 超出时链接失败, 报错信息含
  ``region `text' overflowed``.


烧录
====

Leonix 没有 USB 串口. 板子只在 Caterina bootloader 等待期间以串口出现在
主机上, 等待时长约 8 秒. 烧录时需要先按复位键, 再在这段时间内启动 avrdude.

1. 用 USB 线连接板子与主机.
2. 按一下复位键. 板上 LED 开始呈呼吸状亮灭, 表示 bootloader 正在等待.
3. 在 8 秒内运行::

     make flash PORT=$(ls /dev/cu.usbmodem* | head -n 1)

   avrdude 报告写入与校验的进度. 写入完成之后应用开始运行.

端口名在同一台主机上通常保持不变. 第一次烧录时, 先按复位键, 再运行
``ls /dev/cu.usbmodem*`` 查看端口名, 之后可以直接写入 ``PORT=``.


常见错误
========

``usage: make flash PORT=/dev/cu.usbmodemXXXX``
  没有提供 PORT. 第 3 步中的 ``ls`` 没有找到端口时同样出现这条信息, zsh 在这条信息之前还会输出 ``no matches found``. 原因是复位键没有按下, 或 8 秒的
  等待已经结束. 重新按复位键, 再运行一次.

avrdude 报告无法打开端口
  等待已经结束, 串口随之消失. 重新按复位键, 再运行一次.

``region `text' overflowed``
  映像超过 28672 字节. 运行 ``make size`` 查看各段大小, 再到 leonix.map
  中查找占用最多的符号.

按下复位键之后应用要 8 秒才开始运行
  这是 Caterina 的行为, 与应用无关, 原因见 booting.rst 第 3 节. 拔掉电源
  再接上属于上电复位, 应用立即开始运行.
