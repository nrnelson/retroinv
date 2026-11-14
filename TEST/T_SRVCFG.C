/*
 * T_SRVCFG.C - the server's settings file (SRC/SERVER/SRVCFG.C).
 *
 * usage: T_SRVCFG dir
 *
 * dir is a scratch directory prefix ("OBJ/HOST/SCFGT/", "D:\").  Checks:
 * - srvcfg_default gives every default of SRVCFG.H, the data directory
 *   with DOS_DIRSEP, and LISTENS= at SRVCFG_DEFLISTENS;
 * - a missing file, or a file in a missing directory, gives 0, line 0
 *   and every default;
 * - every key is read in any case of letters, with blanks around '='
 *   and at the ends of the line, among comment and empty lines, with
 *   CR LF and LF line ends mixed and no line end after the last line;
 * - NAME= is upper-cased and refused when empty, longer than
 *   NB_NAMELEN - 1, holding a space or a control byte, or starting with
 *   '*'; DATA= gets its separator unless it ends in '\', '/' or ':', and
 *   is refused when empty or longer than ENG_MAXDIR with it; LOG= is
 *   refused when empty or longer than SRVCFG_LOGMAX;
 * - the numbers SESSIONS=, LISTENS=, IDLE=, BIOS= and SNOW= take their
 *   bounds and refuse one past them, an empty value, a sign, other
 *   characters and a value too large for 16 bits; LISTENS= defaults to
 *   SESSIONS= when that is below SRVCFG_DEFLISTENS, and a LISTENS= above
 *   SESSIONS= is refused at whichever line comes later;
 * - SERIAL= takes COM1, COM2 or both in any case and order, and refuses
 *   another port, a port twice, an empty item and a missing comma;
 *   BAUD= takes every rate of UART_BAUDOK and refuses the others, below,
 *   between and above them; RTSCTS= takes 0 and 1; MAILBOX= is read as
 *   DATA=;
 * - SESSIONS=0, with LISTENS= then 0, is good beside SERIAL= or
 *   MAILBOX=, before or after it, and refused at its own line in a file
 *   that has neither (BAUD= and RTSCTS= enabling nothing); a LISTENS=
 *   with it is refused at whichever line comes later;
 * - READONLY= keeps 1..SRVCFG_ROMAX names of 1..ENG_STALEN characters,
 *   upper-cased, blanks around each removed and inner spaces kept, and
 *   refuses an empty name (leading, trailing or between two commas), one
 *   name too many, a name too long and a control byte;
 * - an unknown key, a key prefix, an empty key, a line with no '=', a
 *   key given twice, a NUL byte, a lone CR and a line longer than
 *   SRVCFG_MAXLINE (CR LF excluded, at the end of the file too) stop the
 *   reading with CF_EINVAL at the right line, counted over CR LF and LF
 *   lines, and leave every default, also after good lines;
 * - a Ctrl-Z ends the file, after a line end, inside a line or at the
 *   start;
 * - a failed open or read (made by the cf_fault_rd hook), the first
 *   read or a later one after good lines, gives its code, line 0 and
 *   every default.
 *
 * The scratch file is removed at the start and at the end.  Prints each
 * failure and a summary line; exit status 0 when every check passes,
 * 1 when one fails, 2 for a usage error.
 */
#define COMPAT_UART
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "SRVCFG.H"
#include "T_FILES.H"
#include "TESTFW.H"

#define PATHMAX         96
#define FILEMAX         1024

static char g_path[PATHMAX + 16];
static char g_buf[FILEMAX];
static u16 g_bad;

static const char *const k_files[] = { "S.CFG", NULL };

/* Write the n bytes of text as the file and load it into c; the load's
   code, and its line into g_bad. */
static int load_n(srvcfg *c, const char *text, u16 n)
{
    if (tfs_write_all(g_path, text, n) != 0)
        return -1;
    g_bad = 0xFFFF;
    return srvcfg_load(c, g_path, &g_bad);
}

/* 1 when c holds every default. */
static int is_default(const srvcfg *c)
{
    srvcfg d;

    srvcfg_default(&d);
    return memcmp(c, &d, sizeof d) == 0;
}

/* 1 when the n bytes of text load with 0 and line 0 into c. */
static int good_n(srvcfg *c, const char *text, u16 n)
{
    return load_n(c, text, n) == 0 && g_bad == 0;
}

static int good(srvcfg *c, const char *text)
{
    return good_n(c, text, (u16)strlen(text));
}

/* 1 when the n bytes of text stop the reading at line want, leaving
   every default. */
static int bad_n(const char *text, u16 n, u16 want)
{
    srvcfg c;

    return load_n(&c, text, n) == CF_EINVAL && g_bad == want &&
           is_default(&c);
}

static int bad(const char *text, u16 want)
{
    return bad_n(text, (u16)strlen(text), want);
}

/* pre, n copies of ch, then post into g_buf; returns g_buf. */
static const char *fill(const char *pre, int ch, u16 n, const char *post)
{
    size_t k;

    k = strlen(pre);
    strcpy(g_buf, pre);
    memset(g_buf + k, ch, n);
    strcpy(g_buf + k + n, post);
    return g_buf;
}

static void test_defaults(void)
{
    char dir[8];
    char sub[8];
    srvcfg c;

    srvcfg_default(&c);
    strcpy(dir, "DATA");
    dir[4] = DOS_DIRSEP;
    dir[5] = '\0';
    TF_STREQ(c.name, SRVCFG_DEFNAME);
    TF_STREQ(c.data, dir);
    TF_STREQ(c.readonly, "");
    TF_STREQ(c.log, "");
    TF_EQ(c.sessions, CONN_NETMAX);
    TF_EQ(c.listens, SRVCFG_DEFLISTENS);
    TF_EQ(c.idle, SRVCFG_DEFIDLE);
    TF_EQ(c.bios + c.snow, 0);
    TF_EQ(c.serial + c.rtscts, 0);
    TF_EQ(c.baud, UART_DEFBAUD);
    TF_STREQ(c.mailbox, "");

    /* A missing file, in a directory or not. */
    strcpy(sub, "NODIR");
    sub[5] = DOS_DIRSEP;
    strcpy(sub + 6, "S");
    cf_remove(g_path);
    memset(&c, 'x', sizeof c);
    g_bad = 0xFFFF;
    TF_EQ(srvcfg_load(&c, g_path, &g_bad), 0);
    TF_EQ(g_bad, 0);
    TF_CHECK(is_default(&c));
    memset(&c, 'x', sizeof c);
    g_bad = 0xFFFF;
    TF_EQ(srvcfg_load(&c, tfs_path(sub, "CFG"), &g_bad), 0);
    TF_EQ(g_bad, 0);
    TF_CHECK(is_default(&c));

    /* An empty file, and one of comments and blanks. */
    TF_CHECK(good(&c, ""));
    TF_CHECK(is_default(&c));
    TF_CHECK(good(&c, "; nothing\r\n\r\n   \n\t; indented\n"));
    TF_CHECK(is_default(&c));
}

static void test_keys(void)
{
    srvcfg c;

    TF_CHECK(good(&c,
                  "; INVSERV.CFG\r\n"
                  "name = Lab-Srv1\r\n"
                  "\r\n"
                  "  Data\t=\tC:\\INV\\DB  \n"
                  "READONLY=FRONT DESK\r\n"
                  "sessions=3\r\n"
                  "Listens= 2\n"
                  "IDLE=0\r\n"
                  "log=C:\\INV\\SERVER.LOG\r\n"
                  "BIOS=1\r\n"
                  "snow=1"));
    TF_STREQ(c.name, "LAB-SRV1");
    TF_CHECK(strncmp(c.data, "C:\\INV\\DB", 9) == 0);
    TF_EQ(c.data[9], DOS_DIRSEP);
    TF_EQ(c.data[10], '\0');
    TF_STREQ(c.readonly, "FRONT DESK");
    TF_EQ(c.sessions, 3);
    TF_EQ(c.listens, 2);
    TF_EQ(c.idle, 0);
    TF_STREQ(c.log, "C:\\INV\\SERVER.LOG");
    TF_EQ(c.bios, 1);
    TF_EQ(c.snow, 1);

    /* NAME=. */
    TF_CHECK(good(&c, fill("NAME=", 'n', NB_NAMELEN - 1, "\r\n")));
    TF_STREQ(c.name, fill("", 'N', NB_NAMELEN - 1, ""));
    TF_CHECK(bad(fill("NAME=", 'n', NB_NAMELEN, "\r\n"), 1));
    TF_CHECK(bad("NAME=\r\n", 1));
    TF_CHECK(bad("NAME=A B\r\n", 1));
    TF_CHECK(bad("NAME=*SRV\r\n", 1));
    TF_CHECK(bad("NAME=A\tB\r\n", 1));
    TF_CHECK(bad("NAME=A\177\r\n", 1));
    TF_CHECK(good(&c, "NAME=S*\xC4\r\n"));
    TF_STREQ(c.name, "S*\xC4");

    /* DATA=. */
    TF_CHECK(good(&c, "DATA=A:"));
    TF_STREQ(c.data, "A:");
    TF_CHECK(good(&c, "DATA=D/"));
    TF_STREQ(c.data, "D/");
    TF_CHECK(good(&c, fill("DATA=", 'd', ENG_MAXDIR - 1, "")));
    TF_EQ(strlen(c.data), ENG_MAXDIR);
    TF_EQ(c.data[ENG_MAXDIR - 1], DOS_DIRSEP);
    TF_CHECK(good(&c, fill("DATA=", 'd', ENG_MAXDIR - 1, "\\")));
    TF_EQ(strlen(c.data), ENG_MAXDIR);
    TF_CHECK(bad(fill("DATA=", 'd', ENG_MAXDIR, ""), 1));
    TF_CHECK(bad(fill("DATA=", 'd', ENG_MAXDIR, "\\"), 1));
    TF_CHECK(bad("DATA=  \r\n", 1));
    TF_CHECK(bad("DATA=A\tB\r\n", 1));

    /* LOG=. */
    TF_CHECK(good(&c, fill("LOG=", 'l', SRVCFG_LOGMAX, "")));
    TF_EQ(strlen(c.log), SRVCFG_LOGMAX);
    TF_CHECK(bad(fill("LOG=", 'l', SRVCFG_LOGMAX + 1, ""), 1));
    TF_CHECK(bad("LOG=\r\n", 1));
}

static void test_numbers(void)
{
    char t[64];
    srvcfg c;

    TF_CHECK(good(&c, "SESSIONS=1\r\n"));
    TF_EQ(c.sessions, 1);
    TF_EQ(c.listens, 1);
    sprintf(t, "SESSIONS=%d\r\n", SRVCFG_DEFLISTENS + 1);
    TF_CHECK(good(&c, t));
    TF_EQ(c.listens, SRVCFG_DEFLISTENS);
    sprintf(t, "SESSIONS=%d\r\nLISTENS=%d\r\n", CONN_NETMAX, CONN_NETMAX);
    TF_CHECK(good(&c, t));
    TF_EQ(c.sessions, CONN_NETMAX);
    TF_EQ(c.listens, CONN_NETMAX);
    sprintf(t, "LISTENS=%d\r\n", CONN_NETMAX);
    TF_CHECK(good(&c, t));
    TF_EQ(c.listens, CONN_NETMAX);
    sprintf(t, "SESSIONS=%d\r\n", CONN_NETMAX + 1);
    TF_CHECK(bad(t, 1));
    sprintf(t, "LISTENS=%d\r\n", CONN_NETMAX + 1);
    TF_CHECK(bad(t, 1));
    TF_CHECK(bad("LISTENS=0\r\n", 1));
    TF_CHECK(bad("SESSIONS=2\r\nLISTENS=3\r\n", 2));
    TF_CHECK(bad("LISTENS=3\r\n;\r\nSESSIONS=2\r\n", 3));
    TF_CHECK(good(&c, "LISTENS=2\r\nSESSIONS=2\r\n"));
    TF_EQ(c.listens, 2);

    TF_CHECK(good(&c, "IDLE=1\r\n"));
    TF_EQ(c.idle, 1);
    sprintf(t, "IDLE=%d\r\n", SRVCFG_IDLEMAX);
    TF_CHECK(good(&c, t));
    TF_EQ(c.idle, SRVCFG_IDLEMAX);
    TF_CHECK(good(&c, "IDLE=007\r\n"));
    TF_EQ(c.idle, 7);
    sprintf(t, "IDLE=%d\r\n", SRVCFG_IDLEMAX + 1);
    TF_CHECK(bad(t, 1));
    TF_CHECK(bad("IDLE=65537\r\n", 1));
    TF_CHECK(bad("IDLE=4294967306\r\n", 1));
    TF_CHECK(bad("IDLE=\r\n", 1));
    TF_CHECK(bad("IDLE=-1\r\n", 1));
    TF_CHECK(bad("IDLE=+1\r\n", 1));
    TF_CHECK(bad("IDLE=1 0\r\n", 1));
    TF_CHECK(bad("IDLE=10m\r\n", 1));

    TF_CHECK(good(&c, "BIOS=0\r\nSNOW=0\r\n"));
    TF_EQ(c.bios + c.snow, 0);
    TF_CHECK(bad("BIOS=2\r\n", 1));
    TF_CHECK(bad("SNOW=Y\r\n", 1));
    TF_CHECK(bad("SNOW=\r\n", 1));
}

static void test_transports(void)
{
    static const char *const ports[] = {
        "", "COM", "COM0", "COM3", "COM12", "LPT1", "COM1,COM1",
        "COM1, com1", "COM1,", ",COM1", "COM1,,COM2", "COM1 COM2", NULL
    };
    static const u16 bauds[] = {
        0, 1, 600, 1199, 1201, 2000, 3600, 7200, 14400, 28800, 38401u,
        57600u, 65535u
    };
    char t[48];
    srvcfg c;
    u32 b;
    int i;

    TF_CHECK(good(&c, "SERIAL=COM1\r\n"));
    TF_EQ(c.serial, 1);
    TF_CHECK(good(&c, "serial = com2\r\n"));
    TF_EQ(c.serial, 2);
    TF_CHECK(good(&c, "SERIAL=Com2 ,\tcom1\r\n"));
    TF_EQ(c.serial, 3);
    for (i = 0; ports[i] != NULL; i++) {
        sprintf(t, "SERIAL=%s\r\n", ports[i]);
        if (!TF_CHECK(bad(t, 1)))
            printf("  %s", t);
    }

    for (b = UART_MINBAUD; b <= UART_MAXBAUD; b *= 2) {
        sprintf(t, "BAUD=%lu\r\n", (unsigned long)b);
        TF_CHECK(good(&c, t));
        TF_EQ(c.baud, b);
    }
    for (i = 0; i < (int)(sizeof bauds / sizeof bauds[0]); i++) {
        sprintf(t, "BAUD=%u\r\n", bauds[i]);
        if (!TF_CHECK(bad(t, 1)))
            printf("  %s", t);
    }
    TF_CHECK(bad("BAUD=\r\n", 1));
    TF_CHECK(bad("BAUD=4294976496\r\n", 1));
    TF_CHECK(bad("BAUD=6553609600\r\n", 1));

    TF_CHECK(good(&c, "RTSCTS=1\r\n"));
    TF_EQ(c.rtscts, 1);
    TF_CHECK(good(&c, "RTSCTS=0\r\n"));
    TF_EQ(c.rtscts, 0);
    TF_CHECK(bad("RTSCTS=2\r\n", 1));

    TF_CHECK(good(&c, "MAILBOX=M:"));
    TF_STREQ(c.mailbox, "M:");
    TF_CHECK(good(&c, fill("MAILBOX=", 'm', ENG_MAXDIR - 1, "")));
    TF_EQ(strlen(c.mailbox), ENG_MAXDIR);
    TF_EQ(c.mailbox[ENG_MAXDIR - 1], DOS_DIRSEP);
    TF_CHECK(bad(fill("MAILBOX=", 'm', ENG_MAXDIR, ""), 1));
    TF_CHECK(bad("MAILBOX=\r\n", 1));

    /* SESSIONS=0 needs another transport, given before or after it. */
    TF_CHECK(good(&c, "SESSIONS=0\r\nSERIAL=COM1\r\n"));
    TF_EQ(c.sessions, 0);
    TF_EQ(c.listens, 0);
    TF_EQ(c.serial, 1);
    TF_CHECK(good(&c, "MAILBOX=M\\\r\n;\r\nSESSIONS=0\r\n"));
    TF_EQ(c.sessions, 0);
    TF_STREQ(c.mailbox, "M\\");
    TF_CHECK(bad("SESSIONS=0\r\n", 1));
    TF_CHECK(bad("; none\r\nSESSIONS=0\r\nBAUD=2400\r\nRTSCTS=1\r\n", 2));
    TF_CHECK(bad("SERIAL=COM1\r\nSESSIONS=0\r\nLISTENS=1\r\n", 3));
    TF_CHECK(bad("LISTENS=1\r\nSERIAL=COM1\r\nSESSIONS=0\r\n", 3));
}

static void test_readonly(void)
{
    char t[ENG_STALEN + 1];
    srvcfg c;
    int i;

    TF_CHECK(good(&c, "READONLY= a , b\t,C\r\n"));
    TF_STREQ(c.readonly, "A,B,C");
    TF_CHECK(good(&c, "READONLY=Lab 1,x\xE4\r\n"));
    TF_STREQ(c.readonly, "LAB 1,X\xE4");

    /* SRVCFG_ROMAX names of ENG_STALEN characters fill the field. */
    strcpy(g_buf, "READONLY=");
    for (i = 0; i < SRVCFG_ROMAX; i++) {
        if (i > 0)
            strcat(g_buf, ",");
        memset(t, 'A' + i, ENG_STALEN);
        t[ENG_STALEN] = '\0';
        strcat(g_buf, t);
    }
    TF_CHECK(good(&c, g_buf));
    TF_EQ(strlen(c.readonly), sizeof c.readonly - 1);
    TF_CHECK(strncmp(c.readonly + sizeof c.readonly - 1 - ENG_STALEN, t,
                     ENG_STALEN) == 0);
    strcpy(g_buf, "READONLY=");
    for (i = 0; i <= SRVCFG_ROMAX; i++)
        strcat(g_buf, i > 0 ? ",S" : "S");
    TF_CHECK(bad(g_buf, 1));
    TF_CHECK(bad(fill("READONLY=A,", 'b', ENG_STALEN + 1, ",C"), 1));
    TF_CHECK(bad("READONLY=\r\n", 1));
    TF_CHECK(bad("READONLY=A,,B\r\n", 1));
    TF_CHECK(bad("READONLY=A, ,B\r\n", 1));
    TF_CHECK(bad("READONLY=A,\r\n", 1));
    TF_CHECK(bad("READONLY=,A\r\n", 1));
    TF_CHECK(bad("READONLY=A\tB\r\n", 1));
}

static void test_bad_lines(void)
{
    srvcfg c;

    TF_CHECK(bad("COLOUR=1\r\n", 1));
    TF_CHECK(bad("NAM=X\r\n", 1));
    TF_CHECK(bad("NAMES=X\r\n", 1));
    TF_CHECK(bad("=X\r\n", 1));
    TF_CHECK(bad("NA ME=X\r\n", 1));
    TF_CHECK(bad("NAME\r\n", 1));
    TF_CHECK(bad("SNOW=1\r\nsnow=1\r\n", 2));
    TF_CHECK(bad("SNOW=1\r\nSNOW=X\r\n", 2));

    /* Line numbers over CR LF and LF, and good lines undone. */
    TF_CHECK(bad("NAME=X\r\n; c\n\nSNOW=1\r\n\r\nJUNK\r\nBIOS=1\r\n", 6));
    TF_CHECK(bad("\n\n\n\n\n\n\n\n\nX\n", 10));

    /* A NUL byte and a lone CR. */
    TF_CHECK(bad_n("SNOW=1\r\nLOG=A\0B\r\n", 17, 2));
    TF_CHECK(bad_n("; \0\r\n", 5, 1));
    TF_CHECK(bad("NAME=A\rB\r\n", 1));
    TF_CHECK(bad("LOG=A\r\r\n", 1));

    /* SRVCFG_MAXLINE bytes are a line, with CR LF or LF or at the end;
       one more is too long, at the end too. */
    TF_CHECK(good(&c, fill(";", 'x', SRVCFG_MAXLINE - 1, "\r\nSNOW=1\r\n")));
    TF_EQ(c.snow, 1);
    TF_CHECK(good(&c, fill(";", 'x', SRVCFG_MAXLINE - 1, "\nSNOW=1")));
    TF_EQ(c.snow, 1);
    TF_CHECK(good(&c, fill("SNOW=1\n;", 'x', SRVCFG_MAXLINE - 1, "")));
    TF_CHECK(good(&c, fill("LOG=L", ' ', SRVCFG_MAXLINE - 5, "\r\n")));
    TF_STREQ(c.log, "L");
    TF_CHECK(bad(fill(";", 'x', SRVCFG_MAXLINE, "\r\n"), 1));
    TF_CHECK(bad(fill(";", 'x', SRVCFG_MAXLINE + 1, "\n"), 1));
    TF_CHECK(bad(fill("SNOW=1\n;", 'x', SRVCFG_MAXLINE, "\n"), 2));
    TF_CHECK(bad(fill("SNOW=1\n;", 'x', SRVCFG_MAXLINE, ""), 2));
    TF_CHECK(bad(fill("SNOW=1\n;", 'x', SRVCFG_MAXLINE - 1, "\r\r\n"), 2));
    /* A line many reads of the file long, as long as g_buf holds with
       its ends, its NUL and a few bytes to spare: still line 2. */
    TF_CHECK(bad(fill("\n;", 'x', FILEMAX - 8, "\n"), 2));
}

/* The cf_fault_rd hook: call g_failat of op g_failop fails. */
static int g_failop;
static int g_failat;

static int fail_rd(int op)
{
    if (op != g_failop || --g_failat != 0)
        return 0;
    return CF_EACCES;
}

/* 1 when the load of text, with call at of op failed, gives CF_EACCES,
   line 0 and every default. */
static int failed(const char *text, int op, int at)
{
    srvcfg c;
    int rc;

    if (tfs_write_all(g_path, text, (u16)strlen(text)) != 0)
        return 0;
    g_failop = op;
    g_failat = at;
    g_bad = 0xFFFF;
    cf_fault_rd = fail_rd;
    rc = srvcfg_load(&c, g_path, &g_bad);
    cf_fault_rd = NULL;
    return rc == CF_EACCES && g_bad == 0 && is_default(&c);
}

static void test_failures(void)
{
    TF_CHECK(failed("SNOW=1\r\n", CF_OP_OPEN, 1));
    TF_CHECK(failed("SNOW=1\r\n", CF_OP_READ, 1));
    TF_CHECK(failed(fill("SNOW=1\r\n;", 'x', 100, "\r\nBIOS=1\r\n"),
                    CF_OP_READ, 2));
}

static void test_ctrlz(void)
{
    srvcfg c;

    TF_CHECK(good(&c, "SNOW=1\r\n\032JUNK\r\n"));
    TF_EQ(c.snow, 1);
    TF_CHECK(good(&c, "SNOW=1\032JUNK\r\nX\r\n"));
    TF_EQ(c.snow, 1);
    TF_CHECK(good(&c, "\032JUNK\r\n"));
    TF_CHECK(is_default(&c));
    TF_CHECK(bad("SNOW=1\r\nJUNK\032\r\n", 2));
}

int main(int argc, char **argv)
{
    tf_begin("T_SRVCFG");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_SRVCFG dir\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    strcpy(g_path, tfs_path("S", "CFG"));
    tfs_wipe(k_files);
    test_defaults();
    test_keys();
    test_numbers();
    test_transports();
    test_readonly();
    test_bad_lines();
    test_ctrlz();
    test_failures();
    tfs_wipe(k_files);
    return tf_done();
}
