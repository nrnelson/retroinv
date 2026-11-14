/*
 * T_ARGST.C - T_ARGS.C built with the Windows programs' switches /TCP:
 * and /MBOX: (ARGS_WINSW), against SRC/CLIENT/ARGSW.C.
 *
 * usage: T_ARGST
 *
 * On the host only: the DOS builds give a source no other .C file to
 * include.  Its checks are those of T_ARGS.C; exit status 0 when every
 * one passes, 1 when one fails.
 */
#define ARGS_WINSW
#include "T_ARGS.C"
