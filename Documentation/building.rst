.. SPDX-License-Identifier: GPL-2.0-only

==============
构建与烧录
==============

本文说明如何把 Leonix 编译成映像并写入 Arduino Leonardo. 板子开机之后为何
需要这些步骤, 见 booting.rst; 启动代码与链接设置的理由, 见 notes/boot.rst.


工具链
======

构建需要 avr-gcc、AVR 版 binutils、avr-libc 与 GNU make, 烧录需要 avrdude.
构建不需要 Documentation/references 中的数据手册.

开发在 macOS 上进行, 使用 avr-gcc 9.5.0. 2026-10-09 又在下列 Linux 发行版的
容器中, 用各自软件源提供的工具链构建了提交 0eb3ed3 的源码. 容器中只验证
编译, 生成的映像没有烧录到板子上:

  =================  ===========  =========  ============
  平台               avr-gcc      avrdude    映像 (字节)
  =================  ===========  =========  ============
  macOS, Homebrew    9.5.0        8.3        1108
  Ubuntu 22.04       5.4.0        6.3        1032
  Debian 12          5.4.0        7.1        1032
  Ubuntu 24.04       7.3.0        7.1        1024
  Debian 13          14.2.0       7.1        998
  Fedora 44          15.2.0       8.0        1000
  Arch Linux         16.1.0       8.3        1010
  Debian sid         16.2.0       7.1        1010
  =================  ===========  =========  ============

映像大小为 ``make size`` 中 text 与 data 之和. 只有 macOS 一行的映像在板子上
运行过. 不同版本的编译器生成的代码不同, notes/scheduler.rst 中的周期数只对
avr-gcc 9.5.0 成立.

Windows 尚未测试, 下文 Windows 一节依据各工具的文档编写.


macOS
-----

用 Homebrew 安装::

  brew tap osx-cross/avr
  brew install avr-gcc@9 avrdude

avr-binutils 与 avr-libc 作为 avr-gcc@9 的依赖一同安装. 确认版本::

  avr-gcc --version
  avrdude -v

第一条命令的首行应显示 9.5.0.


Linux
-----

各发行版的安装命令:

Debian, Ubuntu::

  sudo apt install gcc-avr binutils-avr avr-libc avrdude make

Fedora::

  sudo dnf install avr-gcc avr-binutils avr-libc avrdude make

Arch Linux::

  sudo pacman -S avr-gcc avr-binutils avr-libc avrdude make

串口设备属于 dialout 组, Arch Linux 上属于 uucp 组, 普通用户需要加入该组
才能烧录. Debian、Ubuntu 与 Fedora 上运行::

  sudo usermod -aG dialout $USER

Arch Linux 上把 dialout 换成 uucp. 重新登录之后生效, 用 ``id`` 确认组已加入.


Windows
-------

推荐使用 MSYS2. Makefile 中的 ``test`` 与 ``rm`` 需要 POSIX shell, MSYS2
提供了这一环境. 下列步骤未经实测.

1. 从 https://www.msys2.org 安装 MSYS2.
2. 打开开始菜单中的 MSYS2 UCRT64 终端.
3. 安装工具链::

     pacman -S make mingw-w64-ucrt-x86_64-avr-gcc mingw-w64-ucrt-x86_64-avr-binutils mingw-w64-ucrt-x86_64-avr-libc mingw-w64-ucrt-x86_64-avrdude

本文写作时 MSYS2 UCRT64 提供的 avr-gcc 为 16.1.0, avrdude 为 8.0. 同版本的
avr-gcc 在 Arch Linux 容器中构建成功.

另一种做法是在 WSL 2 中按 Linux 一节构建. WSL 2 本身不能访问 USB 设备,
Microsoft 的文档要求另装 usbipd-win 才能把设备交给 WSL. 板子每次按复位键
都会重新枚举, 能否在 8 秒内完成转接尚未测试. 在 WSL 中构建、在 Windows
一侧用 MSYS2 的 avrdude 烧录, 可以避开这一问题.


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

``make size`` 的输出示例 (avr-gcc 9.5.0)::

     text    data     bss     dec     hex filename
     1068       2     272    1342     53e leonix.elf

读法如下:

- 占用的 Flash 为 text 加 data, 即 1070 字节. ``.data`` 的初值同样存放在
  Flash 中, 由启动代码复制到 SRAM.
- 静态占用的 SRAM 为 data 加 bss, 即 274 字节. SRAM 共 2560 字节, 其余部分
  留给 main() 使用的启动栈.
- Flash 的上限是 28672 字节. 超出时链接失败, 报错信息含
  ``region `text' overflowed``.


生成文档
========

文档用 Sphinx 渲染成 HTML, 做法与 Linux 的 ``make htmldocs`` 相同. 所需的
Python 包与版本列在 Documentation/sphinx/requirements.txt. 其中 jieba 负责
中文分词, 不安装 jieba 时, 搜索框只能找到英文与数字.

安装到一个虚拟环境中::

  python3 -m venv ~/.venv/leonix-docs
  ~/.venv/leonix-docs/bin/pip install -r Documentation/sphinx/requirements.txt

在仓库根目录生成::

  PATH=~/.venv/leonix-docs/bin:$PATH make htmldocs

用浏览器打开 Documentation/output/html/index.html 查看. ``make cleandocs`` 删除
生成的文件. 找不到 sphinx-build 时, ``make htmldocs`` 只输出一行提示, 不影响
固件的构建.

推送到 main 且改动了 Documentation/ 时, .github/workflows/docs.yml 用同样的
依赖构建文档, 并发布到 GitHub Pages. 该构建加了 ``-W``, 任何警告都会使发布
失败.

主题默认为 alabaster. 设置 DOCS_THEME 可以换用其他已安装的主题, 例如
``DOCS_THEME=sphinx_rtd_theme``, 与 Linux 的用法一致.


烧录
====

Leonix 没有 USB 串口. 板子只在 Caterina bootloader 等待期间以串口出现在
主机上, 等待时长约 8 秒. 烧录时需要先按复位键, 再在这段时间内启动 avrdude.

1. 用 USB 线连接板子与主机.
2. 按一下复位键. 板上 LED 开始呈呼吸状亮灭, 表示 bootloader 正在等待.
3. 在 8 秒内运行下列命令之一, 视操作系统而定::

     make flash PORT=$(ls /dev/cu.usbmodem* | head -n 1)     macOS
     make flash PORT=$(ls /dev/ttyACM* | head -n 1)          Linux
     make flash PORT=COM5                                    Windows

   avrdude 报告写入与校验的进度. 写入完成之后应用开始运行.

端口名在同一台主机上通常保持不变. 第一次烧录时, 先按复位键, 再查看端口名,
之后可以直接写入 ``PORT=``:

macOS
  ``ls /dev/cu.usbmodem*``.

Linux
  ``ls /dev/ttyACM*``. 机器上没有其他 USB 串口设备时, 端口通常是
  /dev/ttyACM0.

Windows
  打开设备管理器, 展开"端口 (COM 和 LPT)", 按下复位键之后新出现的一项
  即为板子, 括号中的 COM 号就是端口名. 若该设备没有出现在这一类别下,
  而是带有黄色感叹号, 说明系统没有为该设备加载串口驱动. 安装 Arduino IDE
  会一并安装 Leonardo 的驱动. 这一情形尚未在 Windows 上核实.


常见错误
========

``usage: make flash PORT=/dev/cu.usbmodemXXXX``
  没有提供 PORT. 第 3 步中的 ``ls`` 没有找到端口时同样出现这条信息, macOS
  默认的 zsh 在这条信息之前还会输出 ``no matches found``. 原因是复位键没有按下, 或 8 秒的
  等待已经结束. 重新按复位键, 再运行一次.

avrdude 报告无法打开端口
  等待已经结束, 串口随之消失. 重新按复位键, 再运行一次.

Linux 上 avrdude 报告 Permission denied
  当前用户不在串口设备所属的组中, 见上文 Linux 一节.

``region `text' overflowed``
  映像超过 28672 字节. 运行 ``make size`` 查看各段大小, 再到 leonix.map
  中查找占用最多的符号.

按下复位键之后应用要 8 秒才开始运行
  这是 Caterina 的行为, 与应用无关, 原因见 booting.rst 第 3 节. 拔掉电源
  再接上属于上电复位, 应用立即开始运行.
