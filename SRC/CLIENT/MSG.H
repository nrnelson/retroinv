/*
 * MSG.H - the client's message file, INV.MSG, and the server's,
 * MSG_SRVFILE, with the text-file reader and the small text helpers the
 * screens share (MSG.C).
 *
 * Every text the full-screen client shows that does not come from the
 * server (menu labels, key hints, list headers, form labels, dialog
 * titles, its own messages, the key names of the test scripts) lives in
 * INV.MSG, not in the program, so that it takes no room in the near
 * data segment.  A text is fetched by number: those of the modules every
 * screen uses stay in far memory for the run, and each screen's own are
 * read from the file as a page when it first needs one (msg_load).
 *
 * File format: text lines ended by CR LF (LF alone is accepted), each
 * at most MSG_MAXRAW bytes before the line end; a Ctrl-Z (1Ah) ends
 * the file.
 *
 *   =STAMP hhhh                the first line: the stamp, four hex
 *                              digits
 *   # comment                  ignored, as are blank lines
 *   nnn text                   message nnn (1..MSG_MAXNUM - 1, decimal,
 *                              no leading zero), one space, then the
 *                              text up to the end of the line; "nnn"
 *                              alone has an empty text
 *
 * The lines of each range of numbers (MSG_R_ below) stand together,
 * with nothing but comments and blank lines among them.
 *
 * Escapes in the text: \n a line break, \\ a backslash, \xHH any byte
 * from 03h (01h and 02h mark the ^1 and ^2 places in the loaded text;
 * written so for every byte 80h..FFh: the source files are ASCII); ^1
 * and ^2 are the places where msg_sub puts its arguments, and ^^ is a
 * literal '^'.  Any other '\' or '^' sequence makes the line malformed.
 * A text holds at most MSG_MAXLEN bytes after unescaping.
 *
 * The file is made by the build from one source file per client module
 * that shows text (SRC/CLIENT/<module>.MSG, holding that module's range
 * of numbers), as the server's MSG_SRVFILE is from
 * SRC/SERVER/INVSERV.MSG.  A source line is "nnn NAME text", NAME being
 * the message's constant, which the build leaves out of the file.
 * TEST/msgchk.py checks the sources: every message constant a module
 * defines (#define M<x>_<NAME> nnn, the prefix fixed per module, in its
 * .C file, or in its header for the messages other modules use) has
 * exactly one line with the same name and number in the module's .MSG
 * file and in the module's range; no number or name occurs twice in all
 * the sources together; no line is unused, too long, malformed or
 * non-ASCII; and every range read as a page fits MSG_PAGE bytes.
 * msgchk also computes the stamp, a 16-bit checksum of the file format
 * and of every message's number, name and count of placeholders, over
 * every message file it builds in one run.  It writes the stamp as the
 * =STAMP line of each built file and, as the constant MSG_STAMP, into a
 * generated header, MSGSTMP.H in the build's object tree, which only
 * MSG.C includes (the MAKEFILE copies it into the Turbo C and Watcom C
 * 6.5 build trees with the other headers).  Nobody keeps the value by
 * hand, and a program refuses an INV.MSG made for another set of
 * messages or in another format, while a change of wording alone keeps
 * the stamp.
 */
#ifndef MSG_H
#define MSG_H

#include "COMPAT.H"

/* ---------------------------------------------------------------------
 * The message file.
 * --------------------------------------------------------------------- */
#define MSG_FILE        "INV.MSG"
#define MSG_SRVFILE     "INVSERV.MSG"   /* the server's texts           */
#define MSG_MAXNUM      1200    /* numbers are 1..MSG_MAXNUM - 1        */
#define MSG_MAXLEN      160     /* bytes of one text, unescaped         */
#define MSG_MAXRAW      255     /* bytes of one file line               */
#define MSG_NBUF        3       /* rotating buffers of msg and msg_sub  */
#define MSG_PAGE        1024    /* bytes of a page slot                 */
#define MSG_NSLOT       2       /* page slots                           */

/* The text INV writes to standard error after its name when msg_load
   fails, followed by msg_why. */
#define MSG_NOFILE      "cannot read " MSG_FILE

/* Load the message file found by msg_fopen as dir + file (MSG_FILE for
   INV, MSG_SRVFILE for the server).  The file is read whole into a far
   block with one read, every line is checked, and the block is freed.
   The texts of the numbers below MSG_PAGEMIN, those of the modules
   every screen uses, and from MSG_PAGEEND on, the server's, then stay
   in a far block for the run.  It takes the first block's place
   (cmem_replace), so that nothing freed lies under it, and is filled
   from a second read, into a block freed at once, of the file's start
   up to the last line of those texts; the load so needs, for a moment,
   the larger of the file's bytes and of those two blocks together.
   Each range of numbers (MSG_R_) between them is a page: the first time
   one of its texts is wanted, the range is read from the file msg_load
   opened with one read into one of MSG_NSLOT far slots of MSG_PAGE
   bytes, the one used least recently, through a far area kept for the
   largest such range, so that a page never needs memory; the slots and
   the area are there only when the file has such a range.  The
   directory and file are kept, not copied, for those reads, and must
   stay unchanged until msg_free.  Returns 0; a CF_E code when neither
   file can be read, memory runs out, or the second read finds lines
   that do not fit the pages the first sized (CF_EINVAL); or -1 when the
   file passes CMEM_MAXBLK bytes, the stamp differs from the program's
   (MSGSTMP.H), a line is malformed (bad number, duplicate, bad escape,
   too long), a range's lines are split by another's, a page needs more
   than MSG_PAGE bytes (two per number of its range up to its highest,
   plus its texts, unescaped and NUL-terminated), or the far block of
   the run (the index, the slots, the read area and the loaded pages)
   more than CMEM_MAXBLK; msg_why then says what and, for a line, which.
   Loading again replaces what was loaded. */
int msg_load(const char *dir, const char *file);

/* The reason for the last failed msg_load, "" after a success. */
const char *msg_why(void);

/* Free the far block; every text is then unknown. */
void msg_free(void);

/* Text n, written into the next of MSG_NBUF rotating near buffers of
   MSG_MAXLEN + 1 bytes.  An unknown number gives "?nnn", as does a
   number whose page cannot be read (the file gone or changed).

   A result stays valid through MSG_NBUF - 1 further calls of msg and
   msg_sub, so passing up to MSG_NBUF results straight as the arguments
   of one call is always safe.  A result must never be kept across any
   call into TUI, RQERR, LISTV, FORM, MEMO or a screen, which fetch
   texts of their own; every function there that takes a text copies it
   before it fetches one. */
const char *msg(u16 n);

/* Text n with ^1 and ^2 replaced by a1 and a2 (NULL for "") and ^^ by
   '^', in one pass: the arguments are never scanned for placeholders.
   The result is built in a buffer on the stack, cut at MSG_MAXLEN, and
   only then copied into the next rotating buffer, so a1 and a2 may
   themselves be msg results.  Valid as long as a msg result. */
const char *msg_sub(u16 n, const char *a1, const char *a2);

/* Text n into buf of size bytes (cut to fit); returns its length. */
u16 msg_get(u16 n, char *buf, u16 size);

/* ---------------------------------------------------------------------
 * Reading text files.
 *
 * A file the client reads whole, or a section of one, comes into a far
 * block with a single read (msg_fread), so that DOS moves whole sectors
 * at a time instead of one per call.
 *
 * A buffered line reader, over an open handle or over such a block, is
 * shared by every client module that reads a text file line by line.  A
 * line ends at LF; a CR right before the line end is dropped (CR LF or
 * LF alone), and a Ctrl-Z (1Ah) ends the file, as DOS editors may leave
 * one at the end.  The caller gives the buffer, so the reader takes no
 * near data of its own.
 * --------------------------------------------------------------------- */
/* Open the file name for reading, denying others write access: as *dir
   + name (*dir "" or ending in a separator) or, when that fails or *dir
   is "", in the current directory, *dir then becoming "".  0 or the
   CF_E code of the last open. */
int msg_fopen(const char **dir, const char *name, int *h);

/* n bytes (1..CMEM_MAXBLK) of the open file h from pos into a new far
   block *out of n bytes, with one read.  Returns 0; CF_ENOMEM; the CF_E
   code of a failed seek or read; or CF_EINVAL when the file ends before
   pos + n.  *out is NULL after a failure. */
int msg_fread(int h, u32 pos, u16 n, cmem_blk **out);

#define MSG_IDXLINE     32      /* bytes of a section index line        */

/* Section key of the file name, opened as msg_fopen does in dir, into a
   new far block *out of *n bytes, with one read.  The file starts with
   an index of its sections, one line "=KEY hhhh hhhh" each of at most
   MSG_IDXLINE bytes before its end: the key (in any case), then the
   section's offset and its length in bytes, four hex digits each; the
   index ends at the first line not starting with '='.  Returns 0; -1
   when the key is not in the index or its section is empty; or as
   msg_fopen and msg_fread do.  *out is NULL after a failure. */
int msg_section(const char *dir, const char *name, const char *key,
                cmem_blk **out, u16 *n);

typedef struct msg_rd {
    char *buf;                  /* the caller's buffer                  */
    cmem_blk *blk;              /* the block read, or NULL: the handle  */
    int  h;
    u16  size;                  /* bytes of buf                         */
    u16  pos;                   /* next byte of buf to give             */
    u16  got;                   /* bytes in buf                         */
    u16  at;                    /* next byte of blk to copy into buf    */
    u16  end;                   /* end of the bytes of blk read         */
    u8   eof;                   /* end of file or Ctrl-Z seen           */
    u8   cr;                    /* a CR read and not yet given          */
} msg_rd;

/* Start reading the open handle h through buf (bufsz bytes, 1 to
   32767: the reader takes differences of pointers into it), at the
   handle's position. */
void msg_rd_init(msg_rd *r, int h, char *buf, u16 bufsz);

/* Start reading bytes from..to - 1 of block b through buf, of bufsz
   bytes as for msg_rd_init, as a file. */
void msg_rd_blk(msg_rd *r, cmem_blk *b, u16 from, u16 to, char *buf,
                u16 bufsz);

/* The next piece of a line into p: at most max bytes (max at least 1),
   no NUL, *n bytes given and *eol 1 when the line ended after them
   (also at the end of the file); a line longer than max comes in
   several pieces.  Returns 1 for a piece, 0 at the end of the file, or
   the negated cf_ code of a failed read. */
int msg_rd_piece(msg_rd *r, char *p, u16 max, u16 *n, int *eol);

/* The next whole line into line (max + 1 bytes), NUL-terminated, its
   length in *len.  A line longer than max is cut to its first max
   bytes, its rest skipped, and *len is then max + 1.  Returns as
   msg_rd_piece does. */
int msg_rd_line(msg_rd *r, char *line, u16 max, u16 *len);

/* ---------------------------------------------------------------------
 * Text helpers of the screens.
 * --------------------------------------------------------------------- */
#define MSG_DATEW       10      /* a date as shown: YYYY-MM-DD          */
#define MSG_CUTMARK     '~'     /* the last cell of a name cut short    */
#define MSG_DATELEN     8       /* a date on the wire: YYYYMMDD         */

/* A wire date (MSG_DATELEN digits, YYYYMMDD) as YYYY-MM-DD into out
   (MSG_DATEW + 1 bytes); 1, or 0 with out "" when wire is not such a
   date. */
int msg_date(char *out, const char *wire);

/* Only a file that defines MSG_PATHS before it includes this header sees
   msg_path, as the largest tests would have no Watcom C 6.5 compiler
   memory left otherwise. */
#ifdef MSG_PATHS
/* A location's path (names from the top down joined by MSG_PATHSEP, as
   the server's PATH= gives it) into out (w + 1 bytes), cut to w
   characters (w at least MSG_PATHCUTW + 1) from the left: the leading
   names give way to MSG_PATHCUT, so the nearest names show; a last name
   too long for that stands alone, cut at its end with MSG_CUTMARK when
   it is longer than w. */
#define MSG_PATHSEP     " > "
#define MSG_PATHSEPW    3       /* characters of MSG_PATHSEP            */
#define MSG_PATHCUT     "... > "        /* the dots, then MSG_PATHSEP   */
#define MSG_PATHCUTW    6       /* characters of MSG_PATHCUT            */
void msg_path(char *out, const char *path, u16 w);
#endif

/* Text first + i, i the place of v's first character in the string
   letters, as msg; NULL when v is empty or its first character none of
   letters.  Only a file that defines MSG_LETTER before it includes this
   header sees it, for the compiler memory of the largest tests. */
#ifdef MSG_LETTER
const char *msg_letter(u16 first, const char *letters, const char *v);
#endif

#define MSG_DECCAP      ((0xFFFFU - 9) / 10) /* msg_dec's largest cap   */

/* The value of the decimal digits at *s, *s advanced past the digits
   read.  Reading stops at the first non-digit or as soon as the value
   is cap or more, so a caller that needs a value below cap checks the
   result and what *s is left on.  cap is at most MSG_DECCAP, so the
   value always fits. */
u16 msg_dec(const char **s, u16 cap);

/* ---------------------------------------------------------------------
 * Message numbers.
 * --------------------------------------------------------------------- */
/* Message number ranges, one per module; a module defines its own
   constants inside its range, with its prefix. */
#define MSG_R_TUI       1       /* MT_  TUI.C, shared ones in TUI.H     */
#define MSG_R_INV       100     /* MN_  INVMENU.C: the menu bar         */
#define MSG_R_LISTV     200     /* ML_                                  */
#define MSG_R_FORM      250     /* MF_                                  */
#define MSG_R_MEMO      300     /* MM_                                  */
#define MSG_R_KEYSCR    350     /* MK_  key names of the test scripts   */
#define MSG_R_LOGIN     400     /* MG_  SCRLOGIN.C                      */
#define MSG_R_ITEM      450     /* MI_  SCRITEM.C                       */
#define MSG_R_LOC       520     /* MO_  SCRLOC.C                        */
#define MSG_R_ADMIN     580     /* MZ_  SCRADMIN.C                      */
#define MSG_R_SRCH      620     /* MS_  SCRSRCH.C                       */
#define MSG_R_CODE      656     /* ME_  SCRCODE.C                       */
#define MSG_R_SET       680     /* MC_  SCRSET.C                        */
#define MSG_R_VERB      710     /* MW_  SCRVERB.H: Record menu verbs    */
#define MSG_R_PASS      740     /* MP_  SCRPASS.C                       */
#define MSG_R_USER      760     /* MU_  SCRUSER.C                       */
#define MSG_R_ASSY      800     /* MA_  SCRASSY.C                       */
#define MSG_R_MAINT     830     /* MV_  SCRMAINT.C                      */
#define MSG_R_LOAN      870     /* MB_  SCRLOAN.C                       */
#define MSG_R_HOLD      895     /* MH_  SCRNFORM.H: loans and holds     */
#define MSG_R_REPORT    950     /* MR_  RPT.C: report and label texts   */
#define MSG_R_SCRRPT    1000    /* MQ_  SCRRPT.C                        */
#define MSG_R_CSV       1040    /* MX_  SCRCSV.C                        */
#define MSG_R_SRV       1070    /* MD_  SRVSCR.C, SRVRUN.C: MSG_SRVFILE */
#define MSG_R_FREE      1150    /* first number not yet assigned        */

/* The ranges from MSG_PAGEMIN up to MSG_PAGEEND are paged (msg_load);
   the others stay loaded: the shared ones before, and the server's from
   MSG_PAGEEND on, as its one screen shows them all the time. */
#define MSG_PAGEMIN     MSG_R_KEYSCR
#define MSG_PAGEEND     MSG_R_SRV

#endif
