/*
 * T_CFG.C - the settings file (SRC/CLIENT/CFG.C).
 *
 * usage: T_CFG dir
 *
 * dir is a scratch directory prefix ("OBJ/HOST/CFGT/", "D:\"); the
 * test's files there are removed first and last.
 *
 * Covered:
 * - defaults and a missing file;
 * - reading known keys in any case with spaces around '=', comments,
 *   unknown keys and key prefixes, bad values, over-long lines, duplicates,
 *   LF and CR LF line ends;
 * - saving, which keeps every line it does not understand in place,
 *   rewrites the first of each known key, drops the later ones, appends
 *   missing keys, ends every line CR LF and copies long lines through;
 * - the printer keys (port and type choices, the page and label sizes
 *   within the ranges of RPT.H);
 * - the serial keys (every rate of UART_BAUDOK taken, any other number, a
 *   sign or a letter refused, RTSCTS a flag);
 * - the choice keys (ITEMORD, PRINTER, PRTYPE) refusing a prefix, an
 *   extension, two words or an empty value of one of their words and taking
 *   a listed word in any mix of case;
 * - the scratch file read when the file itself is missing, named by
 *   replacing only the extension of the last path component (a "."
 *   directory in the path left alone);
 * - paths of CFG_PATHMAX characters taken and longer ones refused, as are
 *   scratch names (an extension starting with $$$, after a directory or a
 *   drive) by load and save alike, leaving every file as it was and the
 *   defaults in c, while a name with $$$ anywhere else is taken;
 * - a failed write or close of the scratch file, or a failed removal of
 *   the old file, leaving the old file untouched and no scratch file, the
 *   close reported with its own code;
 * - a text value holding a control byte or DEL refused;
 * - WINSPLIT, in the builds that have it (the Windows client's and the
 *   host's): 0 by default, 0 to CFG_SPLITMAX taken, any other number, a
 *   sign or a letter refused, the first good line taken, rewritten in
 *   place and appended last; in the others, a line of an unknown key,
 *   counted and kept;
 * - WINCOLS.<table>, likewise: every table name of the list in any case,
 *   the prefix itself and any other word refused as an unknown key, one
 *   to CFG_COLMAX numbers of one to three digits up to CFG_COLWMAX
 *   taken, the columns not given 0, anything else a bad line that sets
 *   no column; a save writing the numbers up to the last one not 0, or
 *   0, in place of the first line, and appending, after WINSPLIT and in
 *   the list's order, only the tables with a width; in the builds without
 *   it, a line of an unknown key, counted and kept;
 * - WINPANE.<screen>, likewise: every pane's state CFG_PANEAUTO and its
 *   width 0 by default and without a line; every screen name of the list
 *   in any case, the prefix itself, a table of WINCOLS and any other word
 *   refused as an unknown key; a state of 0 or 1, alone or with a comma
 *   and a width up to CFG_SPLITMAX, taken, anything else a bad line that
 *   sets neither; a save writing the state, and the width when it is not
 *   0, in place of the first line and appending, last and in the list's
 *   order, only the panes with a state, the lines of a pane without one
 *   dropped; in the builds without it, a line of an unknown key, counted
 *   and kept.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define COMPAT_UART
#include "COMPAT.H"
#include "CFG.H"
#include "PRINT.H"
#include "RPT.H"
#include "T_FILES.H"
#include "TESTFW.H"

#define PATHMAX 96
#define FILEMAX 2048

/* The keys a save appends last with their defaults: the serial keys,
   then WINSPLIT where the build has it; and the printer keys before
   them. */
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
#define WINDEF  "WINSPLIT=0\r\n"
#else
#define WINDEF  ""
#endif
#define SERDEF  "BAUD=9600\r\nRTSCTS=0\r\n" WINDEF
#define PRDEF   "PRINTER=LPT1\r\nPRTYPE=EPSON\r\nPAGELEN=66\r\n" \
                "LABELW=35\r\nLABELH=6\r\n" SERDEF

static char g_path[PATHMAX + 16];
static char g_spath[PATHMAX + 16];
static char g_fbuf[FILEMAX + 1];

/* Every file the test makes in the scratch directory. */
static const char *const k_files[] = {
    "NONE.CFG", "A.CFG", "A.$$$", "NEW.CFG", "P.CFG", "Q.CFG", "C.CFG",
    "L.CFG", "S.CFG", "NOEXT", "NOEXT.$$$", "B.CFG", "B.$$$", "X.$$$",
    ".$$$", "x.$$$", "A$$$.CFG", "A$$$.$$$", "A.$$", "A.$$X", "A.$$$X",
    "a.$$$x", "W.CFG", NULL
};

static void name(char *d, const char *n)
{
    strcpy(d, tfs_dir());
    strcat(d, n);
}

static int write_file(const char *p, const char *text)
{
    return tfs_write_all(p, text, (u16)strlen(text));
}

/* The contents of p into g_fbuf, "" when it cannot be read. */
static void read_file(const char *p)
{
    u16 got;

    tfs_read_all(p, g_fbuf, FILEMAX, &got);
    g_fbuf[got] = '\0';
}

static void test_defaults(void)
{
    cfg c;

    cfg_default(&c);
    TF_STREQ(c.data, "");
    TF_STREQ(c.station, "");
    TF_STREQ(c.user, "");
    TF_STREQ(c.itemord, "ID");
    TF_EQ(c.snow + c.mono + c.bios + c.bad + c.firstbad, 0);
    TF_EQ(c.prport, PRN_LPT1);
    TF_EQ(c.prtype, PR_EPSON);
    TF_EQ(c.pagelen, CFG_DEFPAGE);
    TF_EQ(c.labelw, CFG_DEFLABELW);
    TF_EQ(c.labelh, CFG_DEFLABELH);
    TF_EQ(c.baud, UART_DEFBAUD);
    TF_EQ(c.rtscts, 0);
    name(g_path, "NONE.CFG");
    cf_remove(g_path);
    strcpy(c.user, "X");
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "");
    TF_EQ(c.bad, 0);
}

static const char sample[] =
    "; settings\r\n"
    "data = C:\\INV\\DATA\\ \r\n"
    "\r\n"
    "Station=LAB1\n"
    "COLOUR=1\r\n"
    "SNOW=1\r\n"
    "MONO=2\r\n"
    "  BIOS  =  1\r\n"
    "ItemOrd=mfr\r\n"
    "USER=ADMIN\r\n"
    "station=SECOND\r\n"
    "no equals sign\r\n";

static void test_load(void)
{
    char t[256];
    size_t n;
    cfg c;

    name(g_path, "A.CFG");
    TF_EQ(write_file(g_path, sample), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.data, "C:\\INV\\DATA\\");
    TF_STREQ(c.station, "LAB1");
    TF_STREQ(c.user, "ADMIN");
    TF_STREQ(c.itemord, "MFR");
    TF_EQ(c.snow, 1);
    TF_EQ(c.mono, 0);
    TF_EQ(c.bios, 1);
    TF_EQ(c.bad, 3);
    TF_EQ(c.firstbad, 5);

    /* Over-long lines and values. */
    memset(t, 'x', sizeof t);
    memcpy(t, "USER=", 5);
    t[CFG_MAXLINE] = '\r';
    t[CFG_MAXLINE + 1] = '\n';
    t[CFG_MAXLINE + 2] = '\0';
    strcat(t, "SNOW=1\r\nUSER=TOOLONGNAME\r\nSTATION=");
    n = strlen(t);
    memset(t + n, 's', LINK_STALEN + 1);
    strcpy(t + n + LINK_STALEN + 1, "\r\nITEMORD=SIZE\r\nDATA=");
    TF_EQ(write_file(g_path, t), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "");
    TF_EQ(c.snow, 1);
    TF_EQ(c.bad, 4);
    TF_EQ(c.firstbad, 1);
    TF_STREQ(c.data, "");

    /* A line of exactly CFG_MAXLINE bytes is read. */
    memset(t, ' ', CFG_MAXLINE);
    memcpy(t, "USER=BOB", 8);
    strcpy(t + CFG_MAXLINE, "\n");
    TF_EQ(write_file(g_path, t), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "BOB");
    TF_EQ(c.bad, 0);

    /* A Ctrl-Z ends the file; a last line without its end counts. */
    TF_EQ(write_file(g_path, "USER=AL\r\n\032USER=X\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "AL");
    TF_EQ(write_file(g_path, "MONO=1"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.mono, 1);

    /* A text value holding a control byte or DEL is bad: the default
       stays. */
    TF_EQ(write_file(g_path, "STATION=A\001B\r\nUSER=A\177\r\n"
                     "DATA=X\tY\r\nSTATION=OK\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.station, "OK");
    TF_STREQ(c.user, "");
    TF_STREQ(c.data, "");
    TF_EQ(c.bad, 3);

    /* A key's prefix is no key: the line is bad, the value unused. */
    TF_EQ(write_file(g_path, "DAT=x\r\nUSE = y\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.data, "");
    TF_STREQ(c.user, "");
    TF_EQ(c.bad, 2);
    TF_EQ(c.firstbad, 1);
}

static void test_save(void)
{
    cfg c;

    name(g_path, "A.CFG");
    name(g_spath, "A.$$$");
    TF_EQ(write_file(g_path, sample), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    strcpy(c.station, "LAB2");
    c.mono = 1;
    c.snow = 0;
    strcpy(c.data, "");
    TF_EQ(cfg_save(&c, g_path), 0);
    TF_CHECK(!tfs_exists(g_spath));
    read_file(g_path);
    TF_STREQ(g_fbuf,
             "; settings\r\n"
             "DATA=\r\n"
             "\r\n"
             "STATION=LAB2\r\n"
             "COLOUR=1\r\n"
             "SNOW=0\r\n"
             "MONO=1\r\n"
             "BIOS=1\r\n"
             "ITEMORD=MFR\r\n"
             "USER=ADMIN\r\n"
             "no equals sign\r\n" PRDEF);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.station, "LAB2");
    TF_EQ(c.mono, 1);
    TF_EQ(c.bad, 2);

    /* A new file gets every key. */
    name(g_path, "NEW.CFG");
    cf_remove(g_path);
    cfg_default(&c);
    strcpy(c.user, "JO");
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "DATA=\r\nSTATION=\r\nUSER=JO\r\nSNOW=0\r\nMONO=0\r\n"
                   "BIOS=0\r\nITEMORD=ID\r\n" PRDEF);
}

/* The printer keys: every value in any case, the ends of each range,
   values just outside them and other bad values (counted, the default
   kept or the first good line taken), and a save writing the values in
   effect in capitals. */
static const char prsample[] =
    "printer = prn\r\n"
    "PRTYPE=Plain\r\n"
    "PAGELEN=19\r\n"
    "PAGELEN=020\r\n"
    "LABELW=80\r\n"
    "LABELH=21\r\n"
    "LABELH=+5\r\n"
    "LABELH=\r\n"
    "LABELH=1000\r\n"
    "PRINTER=LPT4\r\n"
    "PRTYPE=IBM\r\n";

static void test_printer(void)
{
    static const char *const ports[4] = { "LPT1", "lpt2", "Lpt3", "PRN" };
    char t[128];
    cfg c;
    int i;

    name(g_path, "P.CFG");
    TF_EQ(write_file(g_path, prsample), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.prport, PRN_DOS);
    TF_EQ(c.prtype, PR_PLAIN);
    TF_EQ(c.pagelen, RPT_MINPAGE);
    TF_EQ(c.labelw, CFG_DEFLABELW);
    TF_EQ(c.labelh, CFG_DEFLABELH);
    TF_EQ(c.bad, 6);
    TF_EQ(c.firstbad, 3);
    c.labelh = RPT_MAXLABELH;
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "PRINTER=PRN\r\nPRTYPE=PLAIN\r\nPAGELEN=20\r\n"
                   "LABELW=35\r\nLABELH=20\r\nDATA=\r\nSTATION=\r\n"
                   "USER=\r\nSNOW=0\r\nMONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n"
                   SERDEF);

    for (i = 0; i < 4; i++) {
        strcpy(t, "PRINTER=");
        strcat(t, ports[i]);
        strcat(t, "\r\nPAGELEN=99\r\nLABELW=79\r\nLABELW=20\r\n"
                  "LABELH=3\r\nPRTYPE=epson\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.prport, i);
        TF_EQ(c.prtype, PR_EPSON);
        TF_EQ(c.pagelen, RPT_MAXPAGE);
        TF_EQ(c.labelw, RPT_MAXLABELW);
        TF_EQ(c.labelh, RPT_MINLABELH);
        TF_EQ(c.bad, 0);
    }

    /* Values out of range in c are saved as the defaults. */
    cfg_default(&c);
    c.prport = PRN_DOS + 1;
    c.prtype = 0;
    name(g_path, "Q.CFG");
    cf_remove(g_path);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "DATA=\r\nSTATION=\r\nUSER=\r\nSNOW=0\r\nMONO=0\r\n"
                   "BIOS=0\r\nITEMORD=ID\r\n" PRDEF);
}

/* The choice keys take only one of their words, whole, in any case: a
   prefix, an extension, two words or nothing is a bad line and leaves
   the default; the first good line after the bad ones is taken. */
static const char chsample[] =
    "ITEMORD=SER\r\n"
    "ITEMORD=SERIALX\r\n"
    "PRINTER=LPT\r\n"
    "PRTYPE=EPSONS\r\n"
    "ITEMORD=I\r\n"
    "ITEMORD=IDS\r\n"
    "PRINTER=LPT12\r\n"
    "PRINTER=PR\r\n"
    "PRTYPE=EPS\r\n"
    "PRTYPE=PLAINX\r\n"
    "ITEMORD=ID LOC\r\n"
    "PRTYPE=\r\n"
    "ITEMORD=sErIaL\r\n"
    "PRINTER=lPt3\r\n"
    "PRTYPE=pLaIn\r\n";

static void test_choices(void)
{
    static const char *const ords[4] = { "id", "Loc", "mFR", "SERIAL" };
    static const char *const want[4] = { "ID", "LOC", "MFR", "SERIAL" };
    char t[64];
    cfg c;
    int i;

    name(g_path, "C.CFG");

    /* Only the four refused values: every default kept. */
    TF_EQ(write_file(g_path, "ITEMORD=SER\r\nITEMORD=SERIALX\r\n"
                           "PRINTER=LPT\r\nPRTYPE=EPSONS\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.itemord, "ID");
    TF_EQ(c.prport, PRN_LPT1);
    TF_EQ(c.prtype, PR_EPSON);
    TF_EQ(c.bad, 4);
    TF_EQ(c.firstbad, 1);

    /* Refused values, then good ones in mixed case. */
    TF_EQ(write_file(g_path, chsample), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.itemord, "SERIAL");
    TF_EQ(c.prport, PRN_LPT3);
    TF_EQ(c.prtype, PR_PLAIN);
    TF_EQ(c.bad, 12);
    TF_EQ(c.firstbad, 1);

    /* Every ITEMORD word in mixed case, taken and stored in capitals. */
    for (i = 0; i < 4; i++) {
        strcpy(t, "ItemOrd = ");
        strcat(t, ords[i]);
        strcat(t, "\r\nPrType=ePsOn\r\nprinter=Prn\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_STREQ(c.itemord, want[i]);
        TF_EQ(c.prtype, PR_EPSON);
        TF_EQ(c.prport, PRN_DOS);
        TF_EQ(c.bad, 0);
        TF_EQ(c.firstbad, 0);
    }
}

/* The serial keys: every rate of UART_BAUDOK and both flags in any
   order with the bad lines before them counted; the values in effect
   saved, a rate out of range in c as UART_DEFBAUD. */
static void test_serial(void)
{
    static const char *const bad[] = {
        "BAUD=600", "BAUD=300", "BAUD=57600", "BAUD=9601", "BAUD=0",
        "BAUD=", "BAUD=+9600", "BAUD=96OO", "BAUD=76800", "BAUD=3600",
        "RTSCTS=2", "RTSCTS=", "RTSCTS=01"
    };
    char t[64];
    cfg c;
    u32 b;
    int i;

    name(g_path, "S.CFG");
    for (b = UART_MINBAUD; b <= UART_MAXBAUD; b *= 2) {
        strcpy(t, "baud = ");
        txt_u32(t + strlen(t), b);
        strcat(t, "\r\nRtsCts=1\r\nBAUD=1200\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.baud, b);
        TF_EQ(c.rtscts, 1);
        TF_EQ(c.bad, 0);
    }
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        strcpy(t, bad[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.baud, UART_DEFBAUD);
        TF_EQ(c.rtscts, 0);
        TF_EQ(c.bad, 1);
    }
    TF_EQ(write_file(g_path, "BAUD=19200\r\nRTSCTS=1\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "BAUD=19200\r\nRTSCTS=1\r\nDATA=\r\nSTATION=\r\n"
                   "USER=\r\nSNOW=0\r\nMONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n"
                   "PRINTER=LPT1\r\nPRTYPE=EPSON\r\nPAGELEN=66\r\n"
                   "LABELW=35\r\nLABELH=6\r\n" WINDEF);
}

#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
/* WINSPLIT: 0 (automatic) by default and for no line, every value of 0
   to CFG_SPLITMAX in any case of the key, any other number, a sign, a
   letter or nothing a bad line, the first good line taken; a save
   rewrites the first line in place, drops the others and appends the
   key last to a file without it. */
static void test_split(void)
{
    static const char *const bad[] = {
        "WINSPLIT=256", "WINSPLIT=-1", "WINSPLIT=+5", "WINSPLIT=",
        "WINSPLIT=1x", "WINSPLIT=0255", "WINSPLIT=1000"
    };
    char t[64];
    cfg c;
    int i;

    cfg_default(&c);
    TF_EQ(c.split, 0);
    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "USER=AL\r\n"), 0);
    c.split = 9;
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.split, 0);
    for (i = 0; i <= CFG_SPLITMAX; i += i < 3 ? 1 : 63) {
        strcpy(t, "WinSplit = ");
        txt_u32(t + strlen(t), (u32)i);
        strcat(t, "\r\nWINSPLIT=7\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.split, i);
        TF_EQ(c.bad, 0);
    }
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        strcpy(t, bad[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.split, 0);
        TF_EQ(c.bad, 1);
    }
    TF_EQ(write_file(g_path, "WINSPLIT=300\r\nUSER=AL\r\nWINSPLIT=12\r\n"
                           "WINSPLIT=40\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.split, 12);
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    c.split = 33;
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINSPLIT=33\r\nUSER=AL\r\nDATA=\r\nSTATION=\r\n"
                   "SNOW=0\r\nMONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n"
                   "PRINTER=LPT1\r\nPRTYPE=EPSON\r\nPAGELEN=66\r\n"
                   "LABELW=35\r\nLABELH=6\r\nBAUD=9600\r\nRTSCTS=0\r\n");
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.split, 33);
    TF_EQ(c.bad, 0);
}

/* The tables of WINCOLS, in the order of the rows of cfg's cols. */
static const char *const k_tabs[CFG_NCOLS] = {
    "ITEM", "SER", "PART", "TREE", "SVC", "ISVC", "LOAN", "ILOAN", "USER",
    "AUDIT"
};

/* 1 when row r of c's cols holds the n widths of w and 0 after them. */
static int cols_are(const cfg *c, int r, const u8 *w, int n)
{
    int k;

    for (k = 0; k < CFG_COLMAX; k++)
        if (c->cols[r][k] != (k < n ? w[k] : 0))
            return 0;
    return 1;
}

/* 1 when every row of c's cols is 0. */
static int cols_none(const cfg *c)
{
    int r;

    for (r = 0; r < CFG_NCOLS; r++)
        if (!cols_are(c, r, NULL, 0))
            return 0;
    return 1;
}

/* WINCOLS.<table>: each table's row read in any case of the key, the
   prefix and any word not a table refused as an unknown key; good and
   bad values; the first good line taken; a save rewriting the first
   line in place with the numbers up to the last one not 0 (or 0),
   dropping the others and appending, after WINSPLIT, only the tables
   with a width, in the list's order. */
static void test_cols(void)
{
    static const char *const good[] = {
        "0", "255", "000,010,7", "1,2,3,4,5,6,7,8,9,10", "3,0,0",
        "0,0,0,0,0,0,0,0,0,255"
    };
    static const u8 goodw[][CFG_COLMAX] = {
        { 0 }, { 255 }, { 0, 10, 7 }, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 },
        { 3 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 255 }
    };
    static const char *const bad[] = {
        "", "256", "1000", "0255", "1,,2", ",1", "1,", ",", "1 ,2", "1, 2",
        "1 2", "-1", "+1", "1x", "1;2", "5,6,256", "1,2,3,4,5,6,7,8,9,10,11"
    };
    static const char *const keys[] = {
        "WINCOLS.WINCOLS", "WINCOLS.", "WINCOLS", "WINCOLS.ITEMS",
        "WINCOLS.ITE", "WINCOLSX.ITEM", "WINCOLS.ITEM.X", "WINCOLS ITEM",
        "XWINCOLS.ITEM"
    };
    static const u8 w2040[] = { 20, 0, 40 };
    static const u8 w7[] = { 0, 0, 7 };
    static const u8 w9[] = { 9 };
    static const u8 w4[] = { 4 };
    char t[128];
    cfg c;
    int i;
    int r;

    cfg_default(&c);
    TF_CHECK(cols_none(&c));
    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "USER=AL\r\n"), 0);
    memset(c.cols, 9, sizeof c.cols);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(cols_none(&c));
    /* Each table its own row, the key in any case, the later line of the
       same key not taken. */
    for (r = 0; r < CFG_NCOLS; r++) {
        strcpy(t, r % 2 ? "wincols." : "WinCols.");
        strcat(t, k_tabs[r]);
        strcat(t, " = ");
        txt_u32(t + strlen(t), (u32)(r + 1));
        strcat(t, ",77\r\nWINCOLS.");
        strcat(t, k_tabs[r]);
        strcat(t, "=5\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.cols[r][0], r + 1);
        TF_EQ(c.cols[r][1], 77);
        TF_EQ(c.cols[r][2], 0);
        c.cols[r][0] = 0;
        c.cols[r][1] = 0;
        TF_CHECK(cols_none(&c));
        TF_EQ(c.bad, 0);
    }
    for (i = 0; i < (int)(sizeof good / sizeof good[0]); i++) {
        strcpy(t, "WINCOLS.AUDIT=");
        strcat(t, good[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        memset(c.cols, 9, sizeof c.cols);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(cols_are(&c, CFG_NCOLS - 1, goodw[i], CFG_COLMAX));
        TF_EQ(c.bad, 0);
    }
    /* A bad value sets no column, not even those before the fault. */
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        strcpy(t, "WINCOLS.ITEM=");
        strcat(t, bad[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(cols_none(&c));
        TF_EQ(c.bad, 1);
    }
    /* The prefix is not a table, nor is any other word: unknown keys,
       which set nothing, WINSPLIT included. */
    for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        strcpy(t, keys[i]);
        strcat(t, "=5\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(cols_none(&c));
        TF_EQ(c.split, 0);
        TF_EQ(c.bad, 1);
    }
    /* The first good line taken, a bad one before it counted. */
    TF_EQ(write_file(g_path, "WINCOLS.ITEM=1,,2\r\nUSER=AL\r\n"
                           "wincols.item=20,0,40\r\nWINCOLS.ITEM=9\r\n"
                           "wincols.tree=4\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(cols_are(&c, 0, w2040, 3));
    TF_CHECK(cols_are(&c, 3, w4, 1));
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    /* The save: ITEM's first line rewritten, its later ones dropped;
       TREE's line, all 0 now, written 0; SER and AUDIT, with a width,
       appended after WINSPLIT; the tables without one not added. */
    c.cols[3][0] = 0;
    c.cols[1][2] = 7;
    c.cols[CFG_NCOLS - 1][CFG_COLMAX - 1] = 255;
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINCOLS.ITEM=20,0,40\r\nUSER=AL\r\nWINCOLS.TREE=0\r\n"
                   "DATA=\r\nSTATION=\r\nSNOW=0\r\nMONO=0\r\nBIOS=0\r\n"
                   "ITEMORD=ID\r\nPRINTER=LPT1\r\nPRTYPE=EPSON\r\n"
                   "PAGELEN=66\r\nLABELW=35\r\nLABELH=6\r\nBAUD=9600\r\n"
                   "RTSCTS=0\r\nWINSPLIT=0\r\nWINCOLS.SER=0,0,7\r\n"
                   "WINCOLS.AUDIT=0,0,0,0,0,0,0,0,0,255\r\n");
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(cols_are(&c, 0, w2040, 3));
    TF_CHECK(cols_are(&c, 1, w7, 3));
    TF_CHECK(cols_are(&c, 3, NULL, 0));
    TF_EQ(c.cols[CFG_NCOLS - 1][CFG_COLMAX - 1], 255);
    TF_EQ(c.bad, 0);
    /* The longest line: ten widths of CFG_COLWMAX. */
    memset(c.cols, 0, sizeof c.cols);
    memset(c.cols[7], CFG_COLWMAX, CFG_COLMAX);
    c.cols[0][0] = 9;
    TF_EQ(write_file(g_path, "USER=AL\r\n"), 0);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "USER=AL\r\nDATA=\r\nSTATION=\r\nSNOW=0\r\nMONO=0\r\n"
                   "BIOS=0\r\nITEMORD=ID\r\n" PRDEF "WINCOLS.ITEM=9\r\n"
                   "WINCOLS.ILOAN=255,255,255,255,255,255,255,255,255,"
                   "255\r\n");
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(cols_are(&c, 0, w9, 1));
    TF_EQ(c.cols[7][0], CFG_COLWMAX);
    TF_EQ(c.cols[7][CFG_COLMAX - 1], CFG_COLWMAX);
    TF_EQ(c.bad, 0);
}

/* The screens of WINPANE, in the order of the rows of cfg's pane. */
static const char *const k_panes[CFG_NPANES] = {
    "LOC", "ITEM", "ASSY", "LOAN", "CAT", "USER"
};

/* 1 when pane r of c has state on and width w. */
static int pane_is(const cfg *c, int r, int on, int w)
{
    return c->pane[r][CFG_PANE_ON] == on && c->pane[r][CFG_PANE_W] == w;
}

/* 1 when every pane of c but pane r (none for -1) has no state and no
   width, as without a line. */
static int panes_auto(const cfg *c, int r)
{
    int k;

    for (k = 0; k < CFG_NPANES; k++)
        if (k != r && !pane_is(c, k, CFG_PANEAUTO, 0))
            return 0;
    return 1;
}

/* WINPANE.<screen>: each screen's pane read in any case of the key, the
   prefix and any word not a screen refused as an unknown key; good and
   bad values; the first good line taken; a save rewriting the first
   line in place, the width only when it is not 0, dropping the others
   and every line of a pane without a state, and appending last, in the
   list's order, only the panes with one. */
static void test_pane(void)
{
    static const char *const good[] = {
        "0", "1", "0,0", "1,255", "1,007", "0,30"
    };
    static const u8 goodv[][CFG_PANEVALS] = {
        { 0, 0 }, { 1, 0 }, { 0, 0 }, { 1, 255 }, { 1, 7 }, { 0, 30 }
    };
    static const char *const bad[] = {
        "", "2", "01", "10", "-1", "+1", "x", "1x", "1,", ",5", "1,256",
        "1,1000", "1,0255", "1,5,6", "1 ,5", "1, 5", "1;5", "1,-5", "1,x"
    };
    static const char *const keys[] = {
        "WINPANE.WINPANE", "WINPANE.", "WINPANE", "WINPANE.LOCS",
        "WINPANE.LO", "WINPANEX.LOC", "WINPANE.LOC.X", "WINPANE LOC",
        "XWINPANE.LOC", "WINPANE.TREE", "WINCOLS.LOC"
    };
    char t[128];
    cfg c;
    int i;
    int r;

    cfg_default(&c);
    TF_CHECK(panes_auto(&c, -1));
    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "USER=AL\r\n"), 0);
    memset(c.pane, 1, sizeof c.pane);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(panes_auto(&c, -1));
    /* Each screen its own pane, the key in any case, the later line of
       the same key not taken. */
    for (r = 0; r < CFG_NPANES; r++) {
        strcpy(t, r % 2 ? "winpane." : "WinPane.");
        strcat(t, k_panes[r]);
        strcat(t, " = 1,");
        txt_u32(t + strlen(t), (u32)(r + 1));
        strcat(t, "\r\nWINPANE.");
        strcat(t, k_panes[r]);
        strcat(t, "=0\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(pane_is(&c, r, 1, r + 1));
        TF_CHECK(panes_auto(&c, r));
        TF_EQ(c.bad, 0);
    }
    for (i = 0; i < (int)(sizeof good / sizeof good[0]); i++) {
        strcpy(t, "WINPANE.USER=");
        strcat(t, good[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(pane_is(&c, CFG_P_USER, goodv[i][CFG_PANE_ON],
                         goodv[i][CFG_PANE_W]));
        TF_CHECK(panes_auto(&c, CFG_P_USER));
        TF_EQ(c.bad, 0);
    }
    /* A bad value sets neither the state nor the width. */
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
        strcpy(t, "WINPANE.LOC=");
        strcat(t, bad[i]);
        strcat(t, "\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(panes_auto(&c, -1));
        TF_EQ(c.bad, 1);
    }
    /* The prefix is not a screen, nor is a table of WINCOLS or any other
       word, nor a screen a table: unknown keys, which set nothing. */
    for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        strcpy(t, keys[i]);
        strcat(t, "=1\r\n");
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_CHECK(panes_auto(&c, -1));
        TF_CHECK(cols_none(&c));
        TF_EQ(c.bad, 1);
    }
    /* The first good line taken, a bad one before it counted. */
    TF_EQ(write_file(g_path, "WINPANE.ITEM=1,\r\nUSER=AL\r\n"
                           "winpane.item=1,20\r\nWINPANE.ITEM=0\r\n"
                           "WINPANE.LOC=0\r\nwinpane.cat=1\r\n"
                           "WINPANE.LOC=1\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(pane_is(&c, CFG_P_ITEM, 1, 20));
    TF_CHECK(pane_is(&c, CFG_P_LOC, 0, 0));
    TF_CHECK(pane_is(&c, CFG_P_CAT, 1, 0));
    TF_CHECK(pane_is(&c, CFG_P_ASSY, CFG_PANEAUTO, 0));
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    /* The save: ITEM's first line rewritten without its width, now 0,
       its later ones dropped; LOC's lines dropped with its state, though
       it keeps a width; CAT's rewritten with a width; USER, with a state,
       appended after WINSPLIT; the panes without one not added. */
    c.pane[CFG_P_ITEM][CFG_PANE_W] = 0;
    c.pane[CFG_P_LOC][CFG_PANE_ON] = CFG_PANEAUTO;
    c.pane[CFG_P_LOC][CFG_PANE_W] = 9;
    c.pane[CFG_P_CAT][CFG_PANE_W] = 7;
    c.pane[CFG_P_USER][CFG_PANE_ON] = 0;
    c.pane[CFG_P_USER][CFG_PANE_W] = CFG_SPLITMAX;
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINPANE.ITEM=1\r\nUSER=AL\r\nWINPANE.CAT=1,7\r\n"
                   "DATA=\r\nSTATION=\r\nSNOW=0\r\nMONO=0\r\nBIOS=0\r\n"
                   "ITEMORD=ID\r\n" PRDEF "WINPANE.USER=0,255\r\n");
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(pane_is(&c, CFG_P_ITEM, 1, 0));
    TF_CHECK(pane_is(&c, CFG_P_LOC, CFG_PANEAUTO, 0));
    TF_CHECK(pane_is(&c, CFG_P_ASSY, CFG_PANEAUTO, 0));
    TF_CHECK(pane_is(&c, CFG_P_LOAN, CFG_PANEAUTO, 0));
    TF_CHECK(pane_is(&c, CFG_P_CAT, 1, 7));
    TF_CHECK(pane_is(&c, CFG_P_USER, 0, CFG_SPLITMAX));
    TF_EQ(c.bad, 0);
    /* A width beside a table's: the panes after every WINCOLS line. */
    c.cols[CFG_NCOLS - 1][0] = 4;
    TF_EQ(write_file(g_path, "USER=AL\r\n"), 0);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "USER=AL\r\nDATA=\r\nSTATION=\r\nSNOW=0\r\nMONO=0\r\n"
                   "BIOS=0\r\nITEMORD=ID\r\n" PRDEF "WINCOLS.AUDIT=4\r\n"
                   "WINPANE.ITEM=1\r\nWINPANE.CAT=1,7\r\n"
                   "WINPANE.USER=0,255\r\n");
}
#else
/* Without the Windows client's key, a WINSPLIT line is a line of an
   unknown key: counted, and kept by a save as it is. */
static void test_split(void)
{
    cfg c;

    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "WINSPLIT=20\r\nUSER=AL\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "AL");
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINSPLIT=20\r\nUSER=AL\r\nDATA=\r\nSTATION=\r\n"
                   "SNOW=0\r\nMONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n" PRDEF);
}

/* Likewise a WINCOLS line. */
static void test_cols(void)
{
    cfg c;

    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "WINCOLS.ITEM=20,0,40\r\nUSER=AL\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "AL");
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINCOLS.ITEM=20,0,40\r\nUSER=AL\r\nDATA=\r\n"
                   "STATION=\r\nSNOW=0\r\nMONO=0\r\nBIOS=0\r\n"
                   "ITEMORD=ID\r\n" PRDEF);
}

/* Likewise a WINPANE line. */
static void test_pane(void)
{
    cfg c;

    name(g_path, "W.CFG");
    TF_EQ(write_file(g_path, "WINPANE.LOC=0\r\nUSER=AL\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "AL");
    TF_EQ(c.bad, 1);
    TF_EQ(c.firstbad, 1);
    TF_EQ(cfg_save(&c, g_path), 0);
    read_file(g_path);
    TF_STREQ(g_fbuf, "WINPANE.LOC=0\r\nUSER=AL\r\nDATA=\r\nSTATION=\r\n"
                   "SNOW=0\r\nMONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n" PRDEF);
}
#endif

/* Lines longer than CFG_MAXLINE are copied through unchanged, whatever
   their length, with a CR LF end. */
static void test_long_lines(void)
{
    char t[FILEMAX];
    char want[FILEMAX];
    cfg c;
    u16 n;

    name(g_path, "L.CFG");
    for (n = CFG_MAXLINE + 1; n < CFG_MAXLINE + 4; n++) {
        memset(t, 'y', n);
        t[n - 1] = 'z';
        strcpy(t + n, "\r\nUSER=X\n");
        memcpy(want, t, n);
        strcpy(want + n, "\r\nUSER=Y\r\nDATA=\r\nSTATION=\r\nSNOW=0\r\n"
                         "MONO=0\r\nBIOS=0\r\nITEMORD=ID\r\n" PRDEF);
        TF_EQ(write_file(g_path, t), 0);
        TF_EQ(cfg_load(&c, g_path), 0);
        TF_EQ(c.bad, 1);
        strcpy(c.user, "Y");
        TF_EQ(cfg_save(&c, g_path), 0);
        read_file(g_path);
        TF_STREQ(g_fbuf, want);
    }
    /* A long line of LF end, a CR inside it, and one cut by the end of
       the file. */
    memset(t, 'a', 500);
    t[100] = '\r';
    t[CFG_MAXLINE] = '\r';
    t[CFG_MAXLINE + 1] = '\r';
    strcpy(t + 500, "\nMONO=1\n");
    memset(t + 508, 'b', 300);
    t[808] = '\0';
    TF_EQ(write_file(g_path, t), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.mono, 1);
    TF_EQ(c.bad, 2);
    TF_EQ(c.firstbad, 1);
    TF_EQ(cfg_save(&c, g_path), 0);
    memcpy(want, t, 500);
    strcpy(want + 500, "\r\nMONO=1\r\n");
    memset(want + 510, 'b', 300);
    strcpy(want + 810, "\r\nDATA=\r\nSTATION=\r\nUSER=\r\nSNOW=0\r\n"
                       "BIOS=0\r\nITEMORD=ID\r\n" PRDEF);
    read_file(g_path);
    TF_STREQ(g_fbuf, want);
}

/* Poison c with non-default values, so that a load which does not
   restore the defaults shows. */
static void poison(cfg *c)
{
    cfg_default(c);
    strcpy(c->user, "X");
    strcpy(c->data, "D");
    strcpy(c->station, "S");
    strcpy(c->itemord, "SERIAL");
    c->snow = c->mono = c->bios = c->rtscts = 1;
    c->prport = PRN_DOS;
    c->pagelen = 20;
    c->labelw = 20;
    c->labelh = 20;
    c->baud = 1200;
    c->bad = 5;
    c->firstbad = 3;
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
    c->split = 9;
    memset(c->cols, 9, sizeof c->cols);
#endif
}

static int is_default(const cfg *c)
{
    cfg d;

    cfg_default(&d);
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
    if (c->split != d.split || memcmp(c->cols, d.cols, sizeof c->cols) != 0)
        return 0;
#endif
    return strcmp(c->user, d.user) == 0 && strcmp(c->data, d.data) == 0 &&
           strcmp(c->station, d.station) == 0 &&
           strcmp(c->itemord, d.itemord) == 0 &&
           c->snow == d.snow && c->mono == d.mono && c->bios == d.bios &&
           c->rtscts == d.rtscts && c->prport == d.prport &&
           c->prtype == d.prtype && c->pagelen == d.pagelen &&
           c->labelw == d.labelw && c->labelh == d.labelh &&
           c->baud == d.baud && c->bad == d.bad &&
           c->firstbad == d.firstbad;
}

/* The scratch file replaces the extension of the last component. */
static void test_scratch(void)
{
    cfg c;

    name(g_path, "NOEXT");
    name(g_spath, "NOEXT.$$$");
    cf_remove(g_path);
    cf_remove(g_spath);
    poison(&c);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_CHECK(is_default(&c));
    TF_CHECK(!tfs_exists(g_path) && !tfs_exists(g_spath));
    TF_EQ(write_file(g_spath, "USER=SCR\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "SCR");
    TF_CHECK(!tfs_exists(g_path) && tfs_exists(g_spath));
    /* A save cut off between its steps is finished by the next. */
    strcpy(c.station, "S");
    TF_EQ(cfg_save(&c, g_path), 0);
    TF_CHECK(!tfs_exists(g_spath));
    read_file(g_path);
    TF_STREQ(g_fbuf, "USER=SCR\r\nDATA=\r\nSTATION=S\r\nSNOW=0\r\nMONO=0\r\n"
                   "BIOS=0\r\nITEMORD=ID\r\n" PRDEF);

    name(g_path, "B.CFG");
    name(g_spath, "B.$$$");
    cf_remove(g_path);
    TF_EQ(write_file(g_spath, "MONO=1\r\n"), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_EQ(c.mono, 1);
    cf_remove(g_spath);
}

/* A dot in a directory part is not an extension: through the directory
   "." the scratch file of NOEXT is still NOEXT.$$$. */
static void test_scratch_dot(void)
{
    const char *dir;
    char dotted[PATHMAX + 64];
    size_t n;
    cfg c;

    dir = tfs_dir();
    n = strlen(dir);
    cfg_default(&c);
    if (n > 0 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) {
        name(g_path, "NOEXT");
        name(g_spath, "NOEXT.$$$");
        name(dotted, ".$$$");
        cf_remove(g_path);
        cf_remove(dotted);
        TF_EQ(write_file(g_spath, "USER=DOT\r\n"), 0);
        strcpy(dotted, dir);
        dotted[n] = '.';
        dotted[n + 1] = dir[n - 1];
        strcpy(dotted + n + 2, "NOEXT");
        TF_EQ(cfg_load(&c, dotted), 0);
        TF_STREQ(c.user, "DOT");
        TF_EQ(cfg_save(&c, dotted), 0);
        TF_CHECK(tfs_exists(g_path) && !tfs_exists(g_spath));
        name(dotted, ".$$$");
        TF_CHECK(!tfs_exists(dotted));
    }

    /* A path of CFG_PATHMAX characters is taken (this one names no
       file); one more is refused, also by a save, although its scratch
       name, with the extension replaced, would fit. */
    strcpy(dotted, dir);
    memset(dotted + n, 'p', CFG_PATHMAX + 1 - n);
    dotted[CFG_PATHMAX] = '\0';
    strcpy(c.user, "X");
    TF_EQ(cfg_load(&c, dotted), 0);
    TF_STREQ(c.user, "");
    dotted[CFG_PATHMAX] = 'p';
    strcpy(dotted + CFG_PATHMAX + 1 - 4, ".CFG");
    TF_EQ(cfg_load(&c, dotted), CF_EINVAL);
    TF_EQ(cfg_save(&c, dotted), CF_EINVAL);

    /* A scratch file name cannot be saved to. */
    name(g_path, "X.$$$");
    TF_EQ(cfg_save(&c, g_path), CF_EINVAL);
    TF_CHECK(!tfs_exists(g_path));
}

/* Load and save refuse p with CF_EINVAL; when keep is not NULL, the
   file p holds it first, and holds it unchanged afterwards; else p
   names no file before and after. */
static void refused(const char *p, const char *keep)
{
    cfg c;

    if (keep != NULL)
        TF_EQ(write_file(p, keep), 0);
    else
        cf_remove(p);
    poison(&c);
    TF_EQ(cfg_load(&c, p), CF_EINVAL);
    TF_CHECK(is_default(&c));
    poison(&c);
    TF_EQ(cfg_save(&c, p), CF_EINVAL);
    if (keep != NULL) {
        read_file(p);
        TF_STREQ(g_fbuf, keep);
    } else {
        TF_CHECK(!tfs_exists(p));
    }
}

/* A scratch name has no scratch file of its own: refused before any
   file is touched, in upper or lower case, after a directory of either
   separator and after a drive letter. */
static void test_scratch_names(void)
{
    char p[PATHMAX + 16];
    char lng[CFG_PATHMAX + 1];

    name(p, "A.$$$");
    refused(p, "USER=KEEP\r\n");
    name(p, "x.$$$");
    refused(p, "USER=KEEP\r\n");
    name(p, "X.$$$");
    refused(p, "USER=KEEP\r\n");
    /* An ordinary file beside it is not touched either. */
    name(g_path, "A.CFG");
    TF_EQ(write_file(g_path, "USER=OLD\r\n"), 0);
    name(p, "A.$$$");
    refused(p, "USER=KEEP\r\n");
    read_file(g_path);
    TF_STREQ(g_fbuf, "USER=OLD\r\n");

    name(p, ".$$$");
    refused(p, NULL);
    refused("NODIR\\.$$$", NULL);
    refused("NODIR/.$$$", NULL);
    /* On DOS an extension cut to three characters makes A.$$$X the
       scratch file itself. */
    name(p, "A.$$$X");
    refused(p, NULL);
    name(p, "a.$$$x");
    refused(p, NULL);
    /* The drive is not part of the name; the file would be on A:. */
    refused("A:.$$$", NULL);
    refused("a:.$$$", NULL);

    /* A path of exactly CFG_PATHMAX bytes ending in .$$$ is refused
       as a scratch name, not accepted as the longest path. */
    memset(lng, 'p', CFG_PATHMAX);
    lng[CFG_PATHMAX] = '\0';
    strcpy(lng + CFG_PATHMAX - 4, ".$$$");
    refused(lng, NULL);
}

/* $$$ anywhere but at the start of an extension makes an ordinary name. */
static void test_scratch_like(void)
{
    static const char *const names[] = {
        "A$$$.CFG", "A.$$", "A.$$X", NULL
    };
    char p[PATHMAX + 16];
    cfg c;
    int i;

    for (i = 0; names[i] != NULL; i++) {
        name(p, names[i]);
        TF_EQ(write_file(p, "USER=OK\r\n"), 0);
        poison(&c);
        TF_EQ(cfg_load(&c, p), 0);
        TF_STREQ(c.user, "OK");
        TF_EQ(cfg_save(&c, p), 0);
        read_file(p);
        TF_CHECK(strncmp(g_fbuf, "USER=OK\r\n", 9) == 0);
    }
    /* No file made by a save is left as a scratch file. */
    name(p, "A$$$.$$$");
    TF_CHECK(!tfs_exists(p));
    name(p, "A.$$$");
    TF_CHECK(!tfs_exists(p));
}

static void test_failures(void)
{
    cfg c;

    name(g_path, "A.CFG");
    name(g_spath, "A.$$$");
    TF_EQ(write_file(g_path, sample), 0);
    TF_EQ(cfg_load(&c, g_path), 0);
    strcpy(c.user, "OTHER");
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 0, CF_EFULL);
    TF_EQ(cfg_save(&c, g_path), CF_EFULL);
    tfs_fault_off();
    TF_CHECK(!tfs_exists(g_spath));
    read_file(g_path);
    TF_STREQ(g_fbuf, sample);

    /* Removed but not renamed: the scratch file is read instead. */
    tfs_fault(TFS_OP(CF_OP_RENAME), 1, 0, CF_EACCES);
    TF_EQ(cfg_save(&c, g_path), CF_EACCES);
    tfs_fault_off();
    TF_CHECK(!tfs_exists(g_path));
    TF_CHECK(tfs_exists(g_spath));
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "OTHER");
    TF_EQ(cfg_save(&c, g_path), 0);
    TF_CHECK(tfs_exists(g_path) && !tfs_exists(g_spath));

    /* The old file cannot be removed: it stays as it was, and so that a
       load cannot take it for a save cut off, the scratch file goes. */
    strcpy(c.user, "HELD");
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 1, CF_EACCES);
    TF_EQ(cfg_save(&c, g_path), CF_EACCES);
    tfs_fault_off();
    TF_CHECK(!tfs_exists(g_spath));
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "OTHER");

    /* The scratch file's close, the second after the old file's, fails:
       its code is the result, and the old file stays. */
    strcpy(c.user, "CLOSED");
    tfs_fault(TFS_OP(CF_OP_CLOSE), 2, 1, CF_EACCES);
    TF_EQ(cfg_save(&c, g_path), CF_EACCES);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_CLOSE), 2);
    TF_CHECK(!tfs_exists(g_spath));
    TF_EQ(cfg_load(&c, g_path), 0);
    TF_STREQ(c.user, "OTHER");
}

int main(int argc, char **argv)
{
    tf_begin("T_CFG");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_CFG dir\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    tfs_wipe(k_files);
    test_defaults();
    test_load();
    test_save();
    test_printer();
    test_choices();
    test_serial();
    test_split();
    test_cols();
    test_pane();
    test_long_lines();
    test_scratch();
    test_scratch_dot();
    test_scratch_names();
    test_scratch_like();
    test_failures();
    tfs_wipe(k_files);
    return tf_done();
}
