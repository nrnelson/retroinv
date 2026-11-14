/*
 * T_PROTO.C - protocol test scripts run through proto_dispatch.  This
 * file holds the script runner, the requests and the checks of their
 * output, and main; the directives that set up the data, the files and
 * the host are in T_PROTO2.C, as Watcom C 6.5 runs out of compiler
 * memory on fewer files (T_PROTO.H).
 *
 * usage: T_PROTO dir [script...]
 *
 * dir is a data directory prefix ("OBJ/HOST/PDATA/", "D:\") naming an
 * existing directory that holds nothing but this test's files.  Without
 * scripts, the ones named in default_scripts are run from the program's
 * own directory (as on a DOS test drive).  Before each script, and once
 * more after the last, every engine file, every leftover of an
 * interrupted pack, reindex, create, edit or import, and the files of
 * script_files with their export scratch names (te_remove) are removed
 * from dir; then a fresh engine is opened with the clock fixed at
 * 2026-10-08 12:00:00, the host has no read-only stations, and a new
 * session starts.  Script lines:
 *
 *   # text, or a blank line   comment
 *   > request                 sent to proto_dispatch as it is (">" alone
 *                             sends an empty line)
 *   @SEND text                sent as a request, with \\ and \xHH
 *                             decoded and any other '\' refused; a
 *                             final "*n" (n of 1 to 9 decimal digits)
 *                             repeats the byte before it to n copies
 *                             in all, so that spaces, tabs and long
 *                             lines are visible ("@SEND PING\x20*246");
 *                             a final '*' with no digits or more than
 *                             9 is refused, and one that is text is
 *                             written \x2A; at most SCRLINE - 1 bytes
 *                             after decoding.  $D is replaced before
 *                             decoding, and ends in '\' under DOS, so
 *                             requests naming $D are written with ">"
 *   @REPEAT n                 send the request of the next line, a ">"
 *                             or @SEND, n times (1 to 65535); any other
 *                             next line, or none, is a failure.  Each
 *                             response must be "+OK" alone, or "+OK
 *                             ID=" and a number one more than the
 *                             response before it gave (any number for
 *                             the first), and the "<" lines that follow
 *                             match the last response
 *   < line                    the next output line, compared exactly
 *                             with its CR LF removed ("<" alone: an
 *                             empty line)
 *   <*                        ignore the rest of the current output
 *   @CLOSED                   the last request returned PROTO_R_CLOSE
 *   @SESSION [station]        a new session (sess_init), with the
 *                             transport's fixed station, \\ and \xHH
 *                             decoded, if given
 *   @REOPEN                   close the engine and open it again on the
 *                             same files, keeping the clock
 *   @NOW YYYYMMDD HHMMSS      fix the clock
 *   @READONLY [STA1,STA2]     set the host's read-only stations (none
 *                             without an argument), a list of at most
 *                             PROTO_MAXLINE characters
 *   @ROOM n                   the sink reports n bytes of room less what
 *                             the current response has written (0:
 *                             unlimited)
 *   @TABLE name [FIELDS=a,b]  output an F line naming the fields and one
 *                             line per record, "R" and the values of a
 *                             live record, "D" and those of a deleted
 *                             one, read with eng_rec_read and
 *                             eng_rec_text and quoted as in responses;
 *                             every field, hidden ones included, when
 *                             FIELDS= is absent
 *   @MEMO table recno field count text
 *                             write count copies of text, with \\ and
 *                             \xHH decoded, as the memo field of record
 *                             recno, stored as they are
 *                             (eng_edit_putmemo) and audited as a memo
 *                             change, at most ENG_MAXMEMO bytes in all
 *   @PUT table recno field value
 *                             store value, as it is up to the end of
 *                             the line, in field of record recno,
 *                             converted as eng_edit_put does but free of
 *                             the commands' rules, as another program
 *                             might write it, and audited as a change
 *   @STATUS n                 the host adds n lines "K HOSTLINE=i" to
 *                             STATUS (0: no host lines), after a line
 *                             "K HOSTROOM=r" when the room the writer
 *                             reports to it, r, is limited (@ROOM)
 *   @OTHERS n                 the host counts n other open sessions
 *   @PEERS n [SELF=j] [IDLE=s]
 *                             the host lists n open sessions to ADMIN
 *                             CLIENTS (0, and for every script at its
 *                             start: no peers function, as in local mode;
 *                             n at most PEERS_MAX of T_PROTO2.C, so that
 *                             every slot fits a u8).  Session i is in slot
 *                             2i - 1, of kind NETB for odd i and TCP for
 *                             even i.  Session j (1..n, default 1) is the
 *                             script's session, idle 0, self 1; every
 *                             other session i has self 0, idle s seconds
 *                             (0..4294967295) or, without IDLE=,
 *                             (i - 1) * 60, and the other sessions take
 *                             turns, in order, at four states: before
 *                             HELLO (PROTO_S_NEW, no station), after
 *                             HELLO (PROTO_S_HELLO, station STAk, k its
 *                             slot), logged in (PROTO_S_READY) and logged
 *                             in with a password change pending
 *                             (PROTO_S_MUSTCHG), both with station STAk,
 *                             user Uk and level 2
 *   @STOP n                   the host has a stop function from here
 *                             on (none at a script's start, as in local
 *                             mode), and a check that it has been
 *                             called n times in this script; each call
 *                             is also a check that the response so far
 *                             is exactly "+OK"
 *   @WRITE file               create file; each following line "= text"
 *                             ("=" alone: an empty line) writes text,
 *                             with \\ and \xHH decoded, and CR LF; the
 *                             first line not starting with '=' ends it
 *   @FILE file                output each line of file, ended by CR LF
 *                             in the file, with that CR LF removed and
 *                             '\' shown as \\ and bytes below 20, 7F
 *                             and above as \xHH (so a lone CR or LF and
 *                             a last line without CR LF show too);
 *                             "(no file)" when it cannot be opened
 *   @REMOVE file              remove file if it exists
 *   @TICKETS                  the host has the fake login table of
 *                             T_PROTO3.C, its store empty, from here on
 *                             (none at a script's start, as in local
 *                             mode); its n-th ticket is n in decimal,
 *                             PROTO_TKTLEN digits with leading zeros
 *   @REFUSE [user]            the fake table's throttle refuses the
 *                             password LOGINs naming user from the
 *                             current tick on (nobody without user, and
 *                             at a script's start)
 *   @NOTED                    output a line "N user ok tick" for the
 *                             last login the throttle was told of since
 *                             the last @NOTED, if any
 *   @MBOX                     the session is offered tickets (tkok), as
 *                             a mailbox's is, until the next @SESSION
 *   @STORE                    output a line "T ticket user station
 *                             tick" for each ticket the fake store
 *                             holds, in its entries' order; tick is that
 *                             of its issue or last redeem
 *   @TICK n                   the host's tick of the requests from here
 *                             on (0 at a script's start)
 *   @FAULT n [code]           the n-th call from now that creates,
 *                             writes, truncates, removes or renames a
 *                             file (cf_fault; flushes are not counted)
 *                             fails with code, a CF_E name of COMPAT.H
 *                             or a number of 1 to 255 (CF_EFULL when
 *                             absent), and the calls after it succeed
 *                             again (0: none fails); reset for every
 *                             script
 *
 * "$D" in a request and in a directive line stands for dir, so a script
 * can name files beside the data on every system.
 *
 * Every response is also checked for its framing: no output exactly
 * for an empty request or a session already closed; otherwise one +OK
 * or -ERR line with a three-digit code, or +DATA, data lines (each with
 * a kind letter, dot-stuffed when it begins with '.') and "." with
 * nothing after it; lines ended by CR LF and at most PROTO_MAXRESP
 * bytes; a response without R lines, and CAT LIST, LTYPE LIST and ADMIN
 * CLIENTS, at most PROTO_MAXUNPAGED bytes.
 *
 * The output of each request, @TABLE, @FILE and @STORE must be matched
 * line by line by the "<" lines that follow it, or skipped with "<*"; a
 * missing, extra or different line, a line not ended by CR LF, and an
 * unknown script line are failures, reported with the script name and
 * line number; after the first failing line of an output its other
 * lines are not compared, so one wrong response is reported once.
 * Lines arrive with the CR of a CR LF ending removed, so a script may be
 * stored with either line end.  A failed eng_close (@REOPEN, the end
 * of a script) is a failure too.  Prints each failure and a summary
 * line "T_PROTO PASS n".  Exit status 0 when every check passes, 1 when
 * one fails, 2 for a usage error or a set-up that fails before the
 * first script.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGEDIT.H"
#include "PROTO.H"
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_PROTO.H"

/* The scripts run when none is named, from the program's directory:
   every file of TEST/SCRIPTS.  They are listed because portable code
   has no directory search. */
static const char *const default_scripts[] = {
    "LOGIN.SCR",
    "PERMS.SCR",
    "SYNTAX.SCR",
    "USERS.SCR",
    "ITEMS.SCR",
    "LOCS.SCR",
    "CATS.SCR",
    "LTYPES.SCR",
    "PAGING.SCR",
    "AUDIT.SCR",
    "MAINT.SCR",
    "ASSY.SCR",
    "LOANS.SCR",
    "REPORT.SCR",
    "STATUS.SCR",
    "CSV.SCR",
    "ADMIN.SCR",
    "ADMDATA.SCR",
    "AUDLIST.SCR",
    "SPLIT.SCR",
    "AVAIL.SCR",
    "TAGS.SCR",
    "TICKET.SCR"
};

#define NDEFAULT        (sizeof default_scripts / sizeof default_scripts[0])

/* The files the scripts write beside the data. */
static const char *const script_files[] = {
    "AUDIT.CSV", "BACK.CSV", "CATS.CSV", "EMPTY.CSV", "IN.CSV", "ITEMS.CSV",
    "LOANS.CSV", "LOCS.CSV", "LTYPES.CSV", "MAINT.CSV", "PAGE.CSV",
    "PERMS.CSV", "SAME.CSV", "STOP.CSV", "TWO.CSV", "TWO.TXT", "X.CSV",
    NULL
};

#define PATHMAX         (ENG_MAXPATH + 4)

/* The state of the script being run. */
sink g_snk;
proto_out g_out;
eng *g_e;
eng_time g_now;
proto_host g_host;
proto_session g_sess;
static const char *g_dir;               /* the data directory prefix    */
static const char *g_scr;               /* the script's path            */
static u32 g_lineno;                    /* its line being run           */
static u32 g_scr_fails;                 /* its failures so far          */
static int g_rc;                        /* proto_dispatch's last result */
static char g_req[SCRLINE];             /* the last request, for reports */
static u16 g_repeat;                    /* @REPEAT: sends of the next
                                           request, 0 when not given    */

/* ---------------------------------------------------------------------
 * The output sink.
 * --------------------------------------------------------------------- */
int sink_write(void *arg, const char *buf, u16 len)
{
    sink *k;

    k = (sink *)arg;
    if (len > OUTMAX - k->len) {
        k->over = 1;
        return 1;
    }
    memcpy(k->buf + k->len, buf, len);
    k->len = (u16)(k->len + len);
    return 0;
}

static u16 sink_room(void *arg)
{
    sink *k;

    k = (sink *)arg;
    if (k->room == 0)
        return 0xFFFFu;
    return k->len >= k->room ? 0 : (u16)(k->room - k->len);
}

/* Empty the sink and start the writer afresh. */
static void out_reset(void)
{
    g_snk.len = 0;
    g_snk.pos = 0;
    g_snk.over = 0;
    g_snk.bad = 0;
    proto_out_init(&g_out, sink_write, sink_room, &g_snk);
}

/* ---------------------------------------------------------------------
 * Checks and failures.
 * --------------------------------------------------------------------- */
static void count_fail(void)
{
    tf_fails++;
    g_scr_fails++;
}

void fail_here(const char *what, const char *a, const char *b)
{
    count_fail();
    printf("%s:%lu: %s", g_scr, (unsigned long)g_lineno, what);
    if (a != NULL)
        printf(": expected \"%s\"", a);
    if (b != NULL)
        printf(", got \"%s\"", b);
    printf("\n");
    if (g_req[0] != '\0')
        printf("    after request: %s\n", g_req);
}

void count_check(void)
{
    tf_checks++;
}

/* The next output line into line (CR LF removed, at most SCRLINE - 1
   bytes kept); 0 when the output is used up. */
static int out_next(char *line, int *crlf)
{
    u16 i;
    u16 n;

    if (g_snk.pos >= g_snk.len)
        return 0;
    i = g_snk.pos;
    while (i < g_snk.len && g_snk.buf[i] != '\n')
        i++;
    *crlf = i < g_snk.len && i > g_snk.pos && g_snk.buf[i - 1] == '\r';
    n = (u16)(i - g_snk.pos - (*crlf ? 1 : 0));
    if (n > SCRLINE - 1)
        n = SCRLINE - 1;
    memcpy(line, g_snk.buf + g_snk.pos, n);
    line[n] = '\0';
    g_snk.pos = (u16)(i < g_snk.len ? i + 1 : i);
    return 1;
}

/* Every output line must have been matched by now. */
static void out_finish(void)
{
    char got[SCRLINE];
    int crlf;

    if (g_snk.over) {
        count_check();
        fail_here("output overflow", NULL, NULL);
    }
    if (!g_snk.bad && out_next(got, &crlf)) {
        count_check();
        fail_here("extra output line", "(end of output)", got);
    }
    out_reset();
}

static void expect_line(const char *want)
{
    char got[SCRLINE];
    int crlf;

    if (g_snk.bad)
        return;
    count_check();
    if (!out_next(got, &crlf)) {
        fail_here("missing output line", want, "(end of output)");
        g_snk.bad = 1;
        return;
    }
    if (!crlf) {
        fail_here("line not ended by CR LF", want, got);
        g_snk.bad = 1;
        return;
    }
    if (strcmp(got, want) != 0) {
        fail_here("output differs", want, got);
        g_snk.bad = 1;
    }
}

/* ---------------------------------------------------------------------
 * The engine.
 * --------------------------------------------------------------------- */
static int open_engine(void)
{
    char why[ENG_WHYSZ];
    int rc;

    why[0] = '\0';
    rc = eng_open(&g_e, g_dir, &g_now, why, (u16)sizeof why);
    g_host.e = g_e;
    if (rc != ENG_OK) {
        count_check();
        printf("%s:%lu: eng_open: error %d (%s)\n", g_scr,
               (unsigned long)g_lineno, rc, why);
        count_fail();
    }
    return rc;
}

/* Close the engine, a check that its close succeeds. */
static void close_engine(void)
{
    int rc;

    if (g_e != NULL) {
        count_check();
        rc = eng_close(g_e);
        if (rc != ENG_OK) {
            printf("%s:%lu: eng_close: error %d\n", g_scr,
                   (unsigned long)g_lineno, rc);
            count_fail();
        }
    }
    g_e = NULL;
    g_host.e = NULL;
}

/* ---------------------------------------------------------------------
 * Directives.
 * --------------------------------------------------------------------- */

/* 1 if the n bytes at p are "-ERR " and a code of three digits, alone
   or followed by a space. */
static int err_line(const char *p, u16 n)
{
    u16 i;

    if (n < 8 || memcmp(p, "-ERR ", 5) != 0)
        return 0;
    for (i = 5; i < 8; i++)
        if (!TXT_ISDIGIT(p[i]))
            return 0;
    return n == 8 || p[8] == ' ';
}

/* 1 if the n bytes at p begin with a data kind letter (F R L H P K C)
   alone or followed by a space; an empty line has none. */
static int data_kind(const char *p, u16 n)
{
    if (n == 0 || strchr("FRLHPKC", p[0]) == NULL)
        return 0;
    return n == 1 || p[1] == ' ';
}

/* 1 if the last request is CAT LIST, LTYPE LIST or ADMIN CLIENTS, the
   lists with R lines that are not paged. */
static int unpaged_list(void)
{
    char w1[8];
    char w2[8];
    const char *p;
    u16 i;

    p = g_req;
    for (i = 0; *p != '\0' && *p != ' ' && i < 7; i++)
        w1[i] = *p++;
    w1[i] = '\0';
    while (*p == ' ')
        p++;
    for (i = 0; *p != '\0' && *p != ' ' && i < 7; i++)
        w2[i] = *p++;
    w2[i] = '\0';
    return ((txt_ieq(w1, "CAT") || txt_ieq(w1, "LTYPE")) &&
            txt_ieq(w2, "LIST")) ||
           (txt_ieq(w1, "ADMIN") && txt_ieq(w2, "CLIENTS"));
}

/* Check the framing of the response in the sink: nothing exactly when
   the request was no request (PROTO_R_NONE) or the session was already
   closed; otherwise one "+OK" or "-ERR" line, or "+DATA", data lines and
   "." with nothing after it, every data line non-empty with a kind
   letter and dot-stuffed when it begins with '.', and -ERR with a
   three-digit code; every line ended by CR LF and at most PROTO_MAXRESP
   bytes before it; and a response without R lines (GET, NOTES, WHOAMI,
   STATUS, errors), or an unpaged list, at most PROTO_MAXUNPAGED bytes. */
static void check_frame(int closed)
{
    const char *b;
    const char *bad;
    const char *p;
    u16 i;
    u16 start;
    u16 n;
    u16 lines;
    u16 m;
    int data;
    int ended;
    int rlines;

    count_check();
    if (closed || g_rc == PROTO_R_NONE) {
        if (g_snk.len != 0)
            fail_here("output where none is allowed", NULL, NULL);
        return;
    }
    if (g_snk.len == 0) {
        fail_here("no response", NULL, NULL);
        return;
    }
    b = g_snk.buf;
    bad = NULL;
    i = 0;
    lines = 0;
    data = 0;
    ended = 0;
    rlines = 0;
    while (i < g_snk.len && bad == NULL) {
        start = i;
        while (i < g_snk.len && b[i] != '\n')
            i++;
        if (i == g_snk.len || i == start || b[i - 1] != '\r') {
            bad = "line not ended by CR LF";
            break;
        }
        n = (u16)(i - start - 1);
        i++;
        if (n > PROTO_MAXRESP) {
            bad = "line longer than PROTO_MAXRESP";
        } else if (ended) {
            bad = "output after the terminator";
        } else if (lines == 0) {
            if (n == 5 && memcmp(b + start, "+DATA", 5) == 0)
                data = 1;
            else if (!(n >= 3 && memcmp(b + start, "+OK", 3) == 0 &&
                       (n == 3 || b[start + 3] == ' ')) &&
                     !err_line(b + start, n))
                bad = "not a response line";
            else
                ended = 1;
        } else if (n == 1 && b[start] == '.') {
            ended = 1;
        } else {
            /* A data line: a dot-stuffed one is checked without its
               first '.', and must then begin with a '.' itself. */
            p = b + start;
            m = n;
            if (m >= 2 && p[0] == '.') {
                if (p[1] != '.')
                    bad = "data line beginning with '.' not dot-stuffed";
                p++;
                m--;
            }
            if (bad == NULL && !data_kind(p, m))
                bad = "data line without a valid kind letter";
            if (m >= 1 && p[0] == 'R')
                rlines = 1;
        }
        lines++;
    }
    if (bad == NULL && data && !ended)
        bad = "+DATA without its terminator";
    if (bad == NULL && (!rlines || unpaged_list()) &&
        g_snk.len > PROTO_MAXUNPAGED)
        bad = "unpaged response longer than PROTO_MAXUNPAGED";
    if (bad != NULL)
        fail_here(bad, NULL, NULL);
}

/* Send the len bytes at req as a request and check its framing; after
   @REPEAT, as many times as it asks, checking each response as it
   says. */
static void send_request(const char *req, u16 len)
{
    u32 id;
    u32 prev;
    u16 k;
    u16 n;
    int closed;

    if (req != g_req)
        memcpy(g_req, req, len);
    g_req[len] = '\0';
    prev = 0;
    k = 0;
    do {
        closed = g_sess.state == PROTO_S_CLOSED;
        out_reset();
        g_rc = proto_dispatch(&g_host, &g_sess, req, len, &g_out);
        check_frame(closed);
        if (g_repeat == 0)
            return;
        count_check();
        id = 0;
        n = 0;
        if (g_snk.len > 7 && memcmp(g_snk.buf, "+OK ID=", 7) == 0)
            n = scan_digits(g_snk.buf + 7, 9, &id);
        /* "+OK" and CR LF, or "+OK ID=", n digits and CR LF */
        if (g_snk.len != (id != 0 ? n + 9 : 5) ||
            memcmp(g_snk.buf, "+OK", 3) != 0 ||
            (k != 0 && id != (prev != 0 ? prev + 1 : 0))) {
            fail_here("response to repeat not +OK or the next ID", NULL,
                      NULL);
            g_snk.bad = 1;
            break;
        }
        prev = id;
    } while (++k < g_repeat);
    g_repeat = 0;
}

/* @SEND text: text decoded, its last byte repeated to count copies in
   all when it ends in "*count", sent as a request; 0, sending nothing,
   for a bad escape or a final '*' followed by no number or a bad one. */
static int send_text(char *arg)
{
    char *star;
    char *p;
    u32 count;
    u16 n;

    count = 1;
    star = strrchr(arg, '*');
    if (star != NULL) {
        p = star + 1;
        while (TXT_ISDIGIT(*p))
            p++;
        if (*p == '\0') {
            p = star + 1;
            if (!number(&p, &count))
                return 0;
            *star = '\0';
        }
    }
    if (!decode(arg, &n, 1) || n == 0 || count == 0 ||
        count > (u32)(SCRLINE - n))
        return 0;
    memcpy(g_req, arg, n);
    for (; count > 1; count--) {
        g_req[n] = g_req[n - 1];
        n++;
    }
    send_request(g_req, n);
    return 1;
}

/* line with every "$D" replaced by the data directory, in a static
   buffer; NULL when the result does not fit it. */
static char *subst_dir(const char *line)
{
    static char buf[SCRLINE];
    size_t n;
    size_t dl;

    n = 0;
    dl = strlen(g_dir);
    while (*line != '\0') {
        if (line[0] == '$' && line[1] == 'D') {
            if (n + dl >= sizeof buf)
                return NULL;
            memcpy(buf + n, g_dir, dl);
            n += dl;
            line += 2;
        } else {
            if (n + 1 >= sizeof buf)
                return NULL;
            buf[n++] = *line++;
        }
    }
    buf[n] = '\0';
    return buf;
}

/* One script line; 0 if it is not a valid line. */
static int do_line(char *line)
{
    char *arg;
    u16 n;

    if (g_repeat != 0 && line[0] != '>' &&
        strncmp(line, "@SEND ", 6) != 0) {
        g_repeat = 0;
        return 0;
    }
    if (line[0] == '=')
        return write_line(line);
    write_end();
    if (line[0] == '\0' || line[0] == '#')
        return 1;
    if (line[0] == '<') {
        if (line[1] == '*' && line[2] == '\0') {
            g_snk.pos = g_snk.len;
            return 1;
        }
        if (line[1] == '\0') {
            expect_line("");
            return 1;
        }
        if (line[1] != ' ')
            return 0;
        expect_line(line + 2);
        return 1;
    }

    out_finish();
    if (line[0] == '>' || line[0] == '@') {
        line = subst_dir(line);
        if (line == NULL)
            return 0;
    }
    if (line[0] == '>') {
        if (line[1] == '\0') {
            send_request("", 0);
            return 1;
        }
        if (line[1] != ' ')
            return 0;
        send_request(line + 2, (u16)strlen(line + 2));
        return 1;
    }
    if (line[0] != '@')
        return 0;

    g_req[0] = '\0';
    arg = strchr(line, ' ');
    if (arg != NULL)
        *arg++ = '\0';
    if (strcmp(line, "@SEND") == 0 && arg != NULL)
        return send_text(arg);
    if (strcmp(line, "@REPEAT") == 0 && arg != NULL)
        return set_count(arg, &g_repeat) && g_repeat != 0;
    if (strcmp(line, "@CLOSED") == 0 && arg == NULL) {
        count_check();
        if (g_rc != PROTO_R_CLOSE)
            fail_here("session not closed", NULL, NULL);
        return 1;
    }
    if (strcmp(line, "@SESSION") == 0) {
        if (arg != NULL && !decode(arg, &n, 1))
            return 0;
        sess_init(&g_sess, arg);
        return 1;
    }
    if (strcmp(line, "@REOPEN") == 0 && arg == NULL) {
        close_engine();
        return open_engine() == ENG_OK;
    }
    return directive(line, arg);
}

static void run_script(const char *path)
{
    static const eng_time start = { 2026, 10, 8, 12, 0, 0 };
    char line[SCRLINE];
    FILE *f;
    size_t n;

    g_scr = path;
    g_lineno = 0;
    g_scr_fails = 0;
    g_req[0] = '\0';
    g_rc = PROTO_R_OK;
    g_snk.room = 0;
    g_repeat = 0;
    directives_reset();
    out_reset();

    /* The script is opened before the engine, so its handle is one of
       the run-time library's first 20. */
    f = fopen(path, "r");
    if (f == NULL) {
        count_check();
        fail_here("cannot open script", NULL, NULL);
        return;
    }
    te_remove(g_dir, script_files);
    memcpy(&g_now, &start, sizeof g_now);
    if (open_engine() != ENG_OK) {
        fclose(f);
        return;
    }
    sess_init(&g_sess, NULL);

    while (fgets(line, (int)sizeof line, f) != NULL) {
        g_lineno++;
        n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') {
            line[--n] = '\0';
        } else if (!feof(f)) {
            count_check();
            fail_here("script line too long", NULL, NULL);
            break;
        }
        if (n > 0 && line[n - 1] == '\r')
            line[--n] = '\0';
        if (!do_line(line)) {
            count_check();
            fail_here("bad script line", NULL, line);
        }
    }
    g_lineno++;
    if (g_repeat != 0) {
        count_check();
        fail_here("@REPEAT without a request", NULL, NULL);
    }
    write_end();
    out_finish();
    fclose(f);
    cf_fault = NULL;
    close_engine();
    printf("%s: %s\n", path, g_scr_fails == 0 ? "ok" : "FAILED");
}

int main(int argc, char **argv)
{
    char path[PATHMAX];
    size_t dlen;
    u16 i;
    int a;

    tf_begin("T_PROTO");
    if (argc < 2) {
        printf("usage: T_PROTO dir [script...]\n");
        return 2;
    }
    g_dir = argv[1];
    if (strlen(g_dir) > ENG_MAXDIR) {
        printf("T_PROTO: data directory too long\n");
        return 2;
    }
    if (dos_set_handles(ENG_PROCHANDLES) != 0) {
        printf("T_PROTO: cannot enlarge the handle table\n");
        return 2;
    }
    g_snk.buf = (char *)malloc(OUTMAX);
    if (g_snk.buf == NULL) {
        printf("T_PROTO: out of memory\n");
        return 2;
    }
    out_reset();

    if (argc > 2) {
        for (a = 2; a < argc; a++)
            run_script(argv[a]);
    } else {
        tf_progdir(path, (u16)sizeof path, argv[0]);
        dlen = strlen(path);
        for (i = 0; i < NDEFAULT; i++) {
            if (dlen + strlen(default_scripts[i]) + 1 > PATHMAX)
                continue;
            strcpy(path + dlen, default_scripts[i]);
            run_script(path);
        }
    }
    te_remove(g_dir, script_files);
    free(g_snk.buf);
    return tf_done();
}
