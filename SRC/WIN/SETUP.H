/*
 * SETUP.H - the resources of SETUP, the installer of RETROINV's Windows
 * client (SETUP.C, SETUP.RC), which its test TEST/WSETUP.C reads too.
 */
#ifndef SETUP_H
#define SETUP_H

/* The dialog and its controls. */
#define SU_R_MAIN       1
#define SU_C_DIR        101     /* the directory                        */
#define SU_C_OPTS       102     /* the switches of the item             */
#define SU_C_NOTE       103     /* the note under them, then progress   */

/* The texts of the string table, in the Windows character set. */
#define SU_S_TITLE      1       /* the dialog's and boxes' caption      */
#define SU_S_USAGE      2
#define SU_S_NOLIST     3       /* the list's path                      */
#define SU_S_BADDIR     4
#define SU_S_BADOPTS    5
#define SU_S_COPYING    6       /* a file's name                        */
#define SU_S_NODIR      7       /* the directory                        */
#define SU_S_NOCOPY     8       /* a file, the directory, a reason      */
#define SU_S_DONE       9       /* the directory                        */
#define SU_S_NOPM       10
#define SU_S_PMFAIL     11
#define SU_S_BYHAND     12      /* NOPM or PMFAIL, the dir, the command */
#define SU_S_ASK        13      /* the directory                        */
#define SU_S_FLOPPY     14
#define SU_S_GONE       15      /* the directory, NOGROUP or ""         */
#define SU_S_NODEL      16      /* a file, a reason, NOGROUP or ""      */
#define SU_S_NOGROUP    17
#define SU_S_NOTE       18
/* The reasons of NOCOPY and NODEL. */
#define SU_S_RNOENT     19
#define SU_S_RFULL      20
#define SU_S_RUSED      21
#define SU_S_RDOS       22      /* a DOS error's number                 */
#define SU_S_RBAD       23
#define SU_S_PACKED     24      /* the directory                        */

#endif
