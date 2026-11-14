/*
 * LZEXP.H - expansion of a file compressed in Microsoft's COMPRESS
 * format (SZDD), as the release disks carry them.
 *
 * The format, as REL/szdd.py writes it and DOS 5's EXPAND reads it: a
 * header of the magic bytes "SZDD" 88 F0 27 33, the method 'A', the last
 * character of the original name's extension and the original length
 * (u32, least significant byte first); then LZSS tokens over a window of
 * LZ_WINSZ bytes, blanks at the start.  Each group of up to eight tokens
 * follows a flag byte whose bits, least significant first, mark a literal
 * byte (1) or a match (0) of two bytes: the low eight bits of a window
 * position, then its high four bits over the length less three.
 *
 * lz_expand streams: the caller reads the compressed file and writes the
 * expanded one through its own functions.  The window doubles as the
 * output buffer, which goes out in writes of at most LZ_WINSZ bytes.
 * lz_copy (LZCOPY.C) runs it between two open files, or copies a plain
 * file through the same window, so INSTALL and SETUP keep their own
 * opening, temporary file and removal, and one buffer serves both kinds
 * of file.
 *
 * The release disks name a compressed file as COMPRESS -r does (lz_name):
 * the last character of its extension becomes '_', so that no compressed
 * program can be started by mistake.
 */
#ifndef LZEXP_H
#define LZEXP_H

#include "COMPAT.H"

#define LZ_OK           0
#define LZ_EFORMAT      (-1)    /* not the format, damaged or cut short */

#define LZ_WINSZ        4096    /* bytes of the window                  */
#define LZ_INSZ         1024    /* bytes of compressed input read ahead */
#define LZ_TOKMAX       3       /* most bytes of a flag and one token   */

/* Read up to max bytes of the compressed file into buf and their number
   into *got, 0 only at its end: 0 or a nonzero code of the caller's
   other than LZ_EFORMAT. */
typedef int (*lz_read_fn)(void *io, u8 *buf, u16 max, u16 *got);

/* Write the n bytes at buf to the expanded file: 0 or a code as for
   lz_read_fn. */
typedef int (*lz_write_fn)(void *io, const u8 *buf, u16 n);

/* The memory of one expansion, given by the caller (on the stack or
   allocated).  The input has LZ_TOKMAX bytes to spare: a token is read
   whole before the end of the input is checked. */
typedef struct lz_work {
    u8 win[LZ_WINSZ];
    u8 in[LZ_INSZ + LZ_TOKMAX];
} lz_work;

/* Expand the compressed file read through rd into the file written
   through wr, each called with io: LZ_OK when the header is right and the
   tokens give exactly the length it states with no byte left over;
   LZ_EFORMAT when not, nothing written past that length; or the first
   code of rd or wr.  On failure part of the file may have been written,
   for the caller to remove. */
int lz_expand(lz_work *w, lz_read_fn rd, lz_write_fn wr, void *io);

/* Copy the file open as in to the file open as out, expanding it when lz
   is not 0, through w's window: LZ_OK, LZ_EFORMAT, or the CF_E code of
   the read or write that failed, part of the file then written. */
int lz_copy(lz_work *w, int in, int out, int lz);

/* Make the 8.3 name, in place, the name COMPRESS -r gives its compressed
   copy: the last character of a three-character extension becomes '_', a
   shorter extension gets '_' added, and a name without one "._".  name
   has room for the two characters more. */
void lz_name(char *name);

#endif
