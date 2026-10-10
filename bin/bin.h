/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _BIN_BIN_H
#define _BIN_BIN_H

/* The programs of /bin, listed in bin/programs.c. */
int cat_main(int argc, char **argv);
int df_main(int argc, char **argv);
int dmesg_main(int argc, char **argv);
int echo_main(int argc, char **argv);
int help_main(int argc, char **argv);
int ls_main(int argc, char **argv);
int ps_main(int argc, char **argv);
int rm_main(int argc, char **argv);
int uptime_main(int argc, char **argv);

#endif /* _BIN_BIN_H */
