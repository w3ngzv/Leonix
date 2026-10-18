# SPDX-License-Identifier: GPL-2.0-only
#
# The host programs of Linux's scripts/kconfig, built without Kbuild.
# The sources are Linux v6.8's, unchanged; this file stands in for
# Linux's scripts/kconfig/Makefile and scripts/Makefile.host.
# Run from the top directory: make -f scripts/kconfig/Makefile conf.

obj := scripts/kconfig

HOSTCC         ?= cc
HOSTCFLAGS     ?= -O2
HOSTPKG_CONFIG ?= pkg-config
YACC           = bison
LEX            = flex

export HOSTPKG_CONFIG

common-objs := confdata.o expr.o lexer.lex.o menu.o parser.tab.o \
	       preprocess.o symbol.o util.o
lxdialog    := lxdialog/checklist.o lxdialog/inputbox.o \
	       lxdialog/menubox.o lxdialog/textbox.o lxdialog/util.o \
	       lxdialog/yesno.o

conf-objs   := $(addprefix $(obj)/,conf.o $(common-objs))
mconf-objs  := $(addprefix $(obj)/,mconf.o mnconf-common.o $(lxdialog) \
		$(common-objs))
nconf-objs  := $(addprefix $(obj)/,nconf.o nconf.gui.o mnconf-common.o \
		$(common-objs))

conf: $(obj)/conf
mconf: $(obj)/mconf
nconf: $(obj)/nconf

# As in Linux's Makefile.host: -t for yydebug, -l without #line.
$(obj)/parser.tab.c $(obj)/parser.tab.h: $(obj)/parser.y
	$(YACC) -o $(obj)/parser.tab.c --defines=$(obj)/parser.tab.h -t -l $<

$(obj)/lexer.lex.c: $(obj)/lexer.l
	$(LEX) -o $@ -L $<

$(obj)/lexer.lex.o: $(obj)/parser.tab.h

# mconf-cfg.sh and nconf-cfg.sh find ncurses and write its flags out.
$(obj)/%conf-cflags $(obj)/%conf-libs: $(obj)/%conf-cfg.sh
	sh $< $(obj)/$*conf-cflags $(obj)/$*conf-libs

$(obj)/mconf.o $(obj)/mnconf-common.o $(addprefix $(obj)/,$(lxdialog)): \
	$(obj)/mconf-cflags
$(obj)/nconf.o $(obj)/nconf.gui.o: $(obj)/nconf-cflags

ncurses-cflags = $(if $(filter $(obj)/mconf.o $(obj)/mnconf-common.o \
	$(obj)/lxdialog/%,$@),$(shell cat $(obj)/mconf-cflags)) \
	$(if $(filter $(obj)/nconf%,$@),$(shell cat $(obj)/nconf-cflags))

$(obj)/%.o: $(obj)/%.c
	$(HOSTCC) $(HOSTCFLAGS) -I$(obj) $(ncurses-cflags) -c -o $@ $<

$(obj)/conf: $(conf-objs)
	$(HOSTCC) -o $@ $^

$(obj)/mconf: $(mconf-objs) $(obj)/mconf-libs
	$(HOSTCC) -o $@ $(mconf-objs) $(shell cat $(obj)/mconf-libs)

$(obj)/nconf: $(nconf-objs) $(obj)/nconf-libs
	$(HOSTCC) -o $@ $(nconf-objs) $(shell cat $(obj)/nconf-libs)

clean:
	rm -f $(obj)/conf $(obj)/mconf $(obj)/nconf $(obj)/*.o \
		$(obj)/lxdialog/*.o $(obj)/parser.tab.[ch] $(obj)/lexer.lex.c \
		$(obj)/[mn]conf-cflags $(obj)/[mn]conf-libs

.PHONY: conf mconf nconf clean
