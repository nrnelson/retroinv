/*
 * CFG.H - the client's settings file, INV.CFG (CFG.C).
 *
 * Text lines "KEY=value", ended by CR LF (LF alone is accepted); keys
 * are matched without regard to case, spaces around '=' and at the ends
 * are ignored, and a line starting with ';' is a comment.  Keys:
 *
 *   DATA=dir       data directory of the LOCAL connection (default
 *                  cfg_defdir)
 *   STATION=name   station name sent with HELLO (default: the DOS
 *                  machine name, else CFG_DEFSTATION)
 *   USER=name      user name offered by the login screen
 *   SNOW=0|1       wait for retrace when writing to a CGA
 *   MONO=0|1       use the monochrome palette on a colour adapter
 *   BIOS=0|1       draw through INT 10h only, as /BIOS
 *   ITEMORD=o      first sort order of the item list: ID, LOC, MFR or
 *                  SERIAL
 *   PRINTER=p      the printer port: LPT1 (default), LPT2, LPT3, or PRN
 *                  for the DOS device
 *   PRTYPE=t       EPSON (ESC/P codes, the default) or PLAIN (text only)
 *   PAGELEN=n      lines per printed page, RPT_MINPAGE..RPT_MAXPAGE
 *                  (default CFG_DEFPAGE)
 *   LABELW=n       label width in characters, RPT_MINLABELW..RPT_MAXLABELW
 *                  (default CFG_DEFLABELW)
 *   LABELH=n       label pitch in lines, RPT_MINLABELH..RPT_MAXLABELH
 *                  (default CFG_DEFLABELH)
 *   BAUD=n         speed of the serial connection, a rate of COMPAT.H's
 *                  UART_BAUDOK (default UART_DEFBAUD)
 *   RTSCTS=0|1     RTS/CTS flow control on the serial connection
 *                  (default 0)
 *   WINSPLIT=n     INVWIN's alone: the width of the location tree beside
 *                  the contents, in average characters of the list font,
 *                  0..CFG_SPLITMAX, 0 for the width INVWIN chooses
 *                  (default 0).
 *   WINCOLS.t=w,...  INVWIN's alone: the widths of the columns of its
 *                  lists' table t, ITEM, SER, PART, TREE, SVC, ISVC,
 *                  LOAN, ILOAN, USER or AUDIT, cols[0] to
 *                  cols[CFG_NCOLS - 1] of cfg (WROWS.H), in average
 *                  characters of the list font, in the order of the
 *                  columns: one to CFG_COLMAX numbers of
 *                  0..CFG_COLWMAX separated by commas, 0 for the
 *                  column's own width, as are the columns past those
 *                  given (default all 0).  A save
 *                  writes them up to the last that is not 0, or 0 alone,
 *                  and adds no line for a table whose widths are all 0.
 *   WINPANE.t=s[,w]  INVWIN's alone: the pane beside the list of its
 *                  split screen t, LOC, ITEM, ASSY, LOAN, CAT or USER,
 *                  pane[CFG_P_LOC] to pane[CFG_P_USER] of cfg: s 1 to
 *                  show it and 0 for the list alone, then, if given, w,
 *                  the width of the list beside it in average
 *                  characters of the list font, 0..CFG_SPLITMAX, 0 for
 *                  the width INVWIN chooses, which LOC, whose width is
 *                  WINSPLIT's, does not use.  Without a line the state
 *                  is CFG_PANEAUTO, left to INVWIN, which shows the pane
 *                  on a link whose requests are fast, but USER's, which
 *                  it shows only for s 1; a save writes no
 *                  line for such a pane and drops the one it had, and
 *                  writes w only when it is not 0.  A drag of the bar
 *                  between a list and its pane writes the line, s 1,
 *                  but for LOC.
 * The DOS builds do not know WINSPLIT, WINCOLS and WINPANE, so they keep
 * their lines as any others they do not know.
 * The ranges are RPT.H's and the printer types PRINT.H's.  This header
 * does not include those two, which only CFG.C, the settings screen
 * (SCRSET.C) and the report screens need, so that every screen that
 * includes it compiles in less of Watcom C 6.5's fixed memory.
 *
 * A missing file means every default.  A text value (DATA, STATION,
 * USER) holding a control byte or DEL is bad.  Lines that are not
 * understood (an unknown key, a known key with a bad value, a line over
 * CFG_MAXLINE) are ignored on reading and counted; when a known key
 * occurs more than once, the first line with a good value counts.  A
 * save keeps comments, the order of lines and every line without a
 * known key unchanged; the first line of each known key, whatever its
 * value was, is rewritten in place with the value in effect, its later
 * lines are dropped, and keys not yet in the file are added at its
 * end.  Every line written ends CR LF; a Ctrl-Z (1Ah) ends the file
 * on reading, and what follows it is neither read nor kept.  A save
 * reads the old file line by line and writes each line, rewritten or
 * kept, straight into a scratch file beside it (the path with the
 * extension of its last component replaced by, or given, .$$$), so the
 * file may be of any size; then removes the old file and renames the
 * scratch file to it.  When the file is missing, cfg_load reads the
 * scratch file instead, and cfg_save first renames the scratch file
 * into place, as a save cut off between those two steps leaves it.
 * Both calls refuse with CF_EINVAL, before they touch any file, a path
 * longer than CFG_PATHMAX bytes and one whose extension starts with $$$
 * (A.$$$, A.$$$X, DIR\.$$$), which DOS takes for its own scratch file.
 */
#ifndef CFG_H
#define CFG_H

#include "COMPAT.H"
#include "LINK.H"

#define CFG_NAME        "INV.CFG"
#define CFG_MAXLINE     80      /* bytes of a line, CR LF excluded      */
#define CFG_DEFSTATION  "PC"
#define CFG_ORDLEN      6       /* longest ITEMORD= value ("SERIAL")    */
#define CFG_PATHMAX     128     /* longest settings path, NUL excluded  */

/* Printer defaults: 11-inch paper at 6 lines per inch; a 3 1/2 inch
   label at 10 characters per inch; a one-inch label pitch, a 15/16 inch
   label and its gap. */
#define CFG_DEFPAGE     66
#define CFG_DEFLABELW   35
#define CFG_DEFLABELH   6

/* The DOS builds keep their bytes: only the Windows client, and the host
   build for its tests, read WINSPLIT, WINCOLS and WINPANE: the maximum
   of WINSPLIT and of WINPANE's width; WINCOLS's table count, columns per
   table and maximum width; WINPANE's screens, in the order of their
   names, their count, the two values of each, and the state of a pane
   without a line. */
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
#define CFG_WINKEYS
#define CFG_SPLITMAX    255
#define CFG_NCOLS       10
#define CFG_COLMAX      10
#define CFG_COLWMAX     255
#define CFG_P_LOC       0
#define CFG_P_ITEM      1
#define CFG_P_ASSY      2
#define CFG_P_LOAN      3
#define CFG_P_CAT       4
#define CFG_P_USER      5
#define CFG_NPANES      6
#define CFG_PANE_ON     0       /* s: 0, 1 or CFG_PANEAUTO              */
#define CFG_PANE_W      1       /* w                                    */
#define CFG_PANEVALS    2
#define CFG_PANEAUTO    0xFF
#endif

typedef struct {
    char data[LINK_MAXDIR + 1];
    char station[LINK_STALEN + 1];
    char user[LINK_USERLEN + 1];
    char itemord[CFG_ORDLEN + 1];
    u8   snow;
    u8   mono;
    u8   bios;
    u8   prport;                /* PRN_LPT1..PRN_DOS (COMPAT.H)         */
    u8   prtype;                /* PR_EPSON or PR_PLAIN (PRINT.H)       */
    u8   pagelen;
    u8   labelw;
    u8   labelh;
    u8   rtscts;
#ifdef CFG_WINKEYS
    u8   split;                 /* WINSPLIT=, 0 for INVWIN's choice     */
    u8   cols[CFG_NCOLS][CFG_COLMAX];   /* WINCOLS., by table           */
    u8   pane[CFG_NPANES][CFG_PANEVALS];        /* WINPANE., by screen  */
#endif
    u16  baud;
    u16  bad;                   /* lines not understood                 */
    u16  firstbad;              /* line number of the first, or 0       */
} cfg;

/* The choice words of ITEMORD, PRINTER and PRTYPE, separated by single
   spaces as a form's FT_CHOICE list: the item list's sort orders, the
   printer ports by PRN_ port number, and the printer types by PR_ type
   less PR_EPSON. */
extern const char cfg_ordlist[];
extern const char cfg_portlist[];
extern const char cfg_typelist[];

/* The data directory a DATA= left out names, DATA\ under the current
   directory, and its bytes with the NUL. */
#define CFG_DEFDIRSZ    6
extern const char cfg_defdir[CFG_DEFDIRSZ];

/* Every default; data, station and user empty (meaning the defaults
   above), itemord "ID", the printer and serial settings as above, each
   pane's state CFG_PANEAUTO. */
void cfg_default(cfg *c);

/* Read path into c, starting from the defaults.  Returns 0 (also when
   path does not exist and its scratch file cannot be opened, a missing
   file not being an error here), CF_EINVAL for a path refused as
   above, or the CF_E code of a read failure; c holds the defaults
   after every failure. */
int cfg_load(cfg *c, const char *path);

/* Write c to path as described above.  Returns 0 or a CF_E code; the
   old file is untouched unless the scratch file was written completely,
   and the scratch file is left only when the old file was removed and
   the rename failed, for cfg_load to read.  A line of the old file
   longer than CFG_MAXLINE is kept as it is, copied in pieces. */
int cfg_save(const cfg *c, const char *path);

#endif
