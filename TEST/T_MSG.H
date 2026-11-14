/*
 * T_MSG.H - what the two source files of the T_MSG test share: the
 * scratch directory, the stamp line and text buffer of the files it
 * writes, and the watched load of T_MSG2.C that T_MSG.C uses.
 *
 * The test is split in two compilation units, T_MSG.C (the texts, the
 * pages, the reader, main) and T_MSG2.C (the far blocks, reads and
 * closes of a load, msg_date, msg_dec), because Watcom C 6.5 runs out
 * of compiler memory on it as one file.
 */
#ifndef T_MSG_H
#define T_MSG_H

#include "COMPAT.H"
#include "MSG.H"

#define PATHMAX         96
#define STAMPLEN        13      /* "=STAMP hhhh" CR LF                  */

extern char g_tdir[PATHMAX];            /* the scratch directory prefix */
extern char g_stamp[STAMPLEN + 1];      /* the program's stamp line     */
/* The file text of the page and Ctrl-Z cases: nine lines of MSG_MAXRAW
   bytes. */
extern char g_text[9 * (MSG_MAXRAW + 1)];

/* Write a, then b (either may be NULL), as the file name of the scratch
   directory; 0 or a CF_E code. */
int write_file(const char *name, const char *a, const char *b);

/* "?n", the text of a number without one, into want (8 bytes). */
const char *unknown(char *want, u16 n);

/* The sizes of the far blocks a load_watched left, oldest first. */
#define AL_MAX          4
extern u16 g_live[AL_MAX];

/* Write text as INV.MSG and load it with the blocks, reads and closes
   watched: allocation refuse refused, read failread failing, the file
   holding swap from its second read on (0, 0 and NULL for none).  The
   load's result; whatever it is, the file was closed once, no block was
   freed while a newer one lived, and only the block of a load that
   succeeded is left. */
int load_watched(const char *text, u16 refuse, u16 failread,
                 const char *swap);

/* The phases of T_MSG2.C; mdir is the directory of the built INV.MSG. */
void test_blocks(const char *mdir);
void test_load_fails(void);
void test_swapped(void);
void test_dates(void);
void test_decimals(void);

#endif
