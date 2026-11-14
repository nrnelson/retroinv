/*
 * INV.C - the RETROINV client, INV.EXE.
 *
 *   INV [/LOCAL [dir] | /NET name | /COMn] [/BIOS] [/CFG:file]
 *   INV /CMD [/LOCAL [dir] | /NET name | /COMn [/CFG:file]]
 *
 *   (no /CMD) the full-screen client: a login screen, then menus, lists
 *            and forms (DOC/CLIENT.md describes the screens)
 *   /CMD     scripted mode: protocol request lines are read from
 *            standard input and every response line is written to
 *            standard output, CR LF ended, exactly as the protocol sends
 *            it, until the input ends or the session is closed (BYE, or
 *            too many failed logins)
 *   /LOCAL   run the engine inside the program on the data directory
 *            dir; the default (none without the engine, below)
 *   /NET     a NetBIOS session to the server called name (LINK.H), a
 *            NetBIOS name as txt_nbname_ok (COMPAT.H) takes it
 *   /COM1, /COM2  a session to the server over that serial port
 *            (LINK.H), at BAUD= of the settings, with RTS/CTS flow
 *            control when RTSCTS=1
 *   /BIOS    draw the screen through INT 10h only (as BIOS=1)
 *   /CFG:    the settings file (default CFG_NAME in the current
 *            directory); scripted mode reads the settings only with
 *            /COM1 or /COM2
 *
 * The same client built without the engine (THIN of SRCS.MK, which links
 * INVNOL.C and LINKNOL.C in place of INVLOC.C and LINKLOC.C) is the
 * network-only client:
 *
 *   THIN {/NET name | /COMn} [/BIOS] [/CFG:file]
 *   THIN /CMD {/NET name | /COMn [/CFG:file]}
 *
 * It lacks /LOCAL (inv_lacks), so a line with /LOCAL, or naming no
 * connection, is a usage error, as is /NOW:, which goes with /LOCAL
 * alone: built without the engine, it must name a server, as its usage
 * text says.  Everything else is as below.
 *
 * Switches are matched without regard to case and may begin with '-'
 * instead of '/'.  /TCP:, the TCP link of the Windows client (ARGS.H),
 * is no switch of INV's: a usage error.  A directory argument is the word
 * after /LOCAL when that word does not begin with '/' or '-'.  The data
 * directory is dir, else (full-screen client only) DATA= of the
 * settings, else cfg_defdir (CFG.H), DATA\ under the current directory;
 * it must exist, and the data files are created in it when missing.  The
 * default is written with '\' as separator, or with '/' when the
 * program's own name (argv[0]) uses '/' and not '\', as in the host test
 * build.
 *
 * Scripted mode.  Input is read as raw bytes: lines end with LF, a CR
 * before the LF is dropped, and a line longer than the protocol's
 * request limit is answered with error 103 and skipped up to its LF.  A
 * last line without LF is still sent.  A Ctrl-Z byte (1Ah) ends the
 * input, as it ends a DOS text file.  Output goes to DOS handle 1
 * unchanged, so redirected output holds the protocol's bytes; messages
 * go to standard error.  Scripted mode never loads the message file.
 * With /NET, /COM1 or /COM2, BREAK checking is off while the link is
 * open (dos_break_off), so Ctrl-Break and Ctrl-C do not end the
 * program, which would leave the NET session's RECEIVE posted in freed
 * memory or the port's interrupt live, and the critical-error handler
 * is installed, so neither does a critical error DOS lets be failed or
 * retried; a Ctrl-Z typed at the keyboard ends the input.  With /LOCAL
 * Ctrl-Break (or Ctrl-C) ends scripted mode through DOS without closing
 * the data files.  Every request leaves its changes flushed, and an
 * edit cut off part-way leaves the marker file that makes the next open
 * rebuild the affected indexes, so the data stay recoverable.
 *
 * Full-screen client.  Start-up, after cmem_init (the first call of
 * main, before any memory is taken): the settings file; BREAK checking
 * off, so that no break ends INV once a serial link's interrupt is live,
 * and the critical-error handler; the link, whose engine takes its near
 * memory before any far block exists; the message file MSG_FILE from
 * the program's directory, else the current one; the key script when
 * one is given (and the stack fill pattern); the printer capture file
 * of /PRN:; the screen; the palette; then the main loop of TUI.C over
 * the menu table of INVMENU.H.
 * A missing settings file means every default; one that exists but
 * cannot be read, or a name the settings screen could not save to (a
 * .$$$ name, which is the scratch name of a save), stops INV.  Every way
 * out (the main loop returning, the end of a key script, the third
 * failed login, a lost NET link, a failure after the link is open)
 * goes through inv_exit, which overwrites any password a screen holds,
 * then closes the link, the key script, the screen, the critical-error
 * and BREAK handling and the message file, in that order, before
 * INV exits.  Both modes end by waiting for any NetBIOS command the
 * closed link had to leave with the provider (net_drain).
 *
 * Test switches, left out of the usage text:
 *   /NOW:YYYYMMDDHHMMSS  fixes the engine's clock, so that the dates
 *            written to the data files are reproducible, and the date
 *            the reports print and judge loans by (scr_date).  It grants
 *            nothing beyond what DOS DATE and TIME already allow whoever
 *            runs INV on the data directory; a backend whose engine runs
 *            elsewhere (a server) must refuse it, since the clock of the
 *            audit rows then belongs to the server.
 *   /KEYS:file  a key script replaces the keyboard (KEYSCR.H)
 *   /SNAP:file  screen snapshots, only together with /KEYS:
 *   /MDA     act as a monochrome adapter; only the host build's virtual
 *            screen honours it (VID_F_MDA), and a DOS build refuses it
 *            as not available unless the adapter is an MDA anyway
 *   /PRN:file  every print job goes to file instead of the printer of
 *            the settings (prn_capture): the file is created empty at
 *            start-up and each job is appended to it.  A DOS device
 *            name such as PRN is refused.
 * /BIOS, /KEYS:, /SNAP:, /MDA and /PRN: belong to the full-screen
 * client and are refused with /CMD, as is /CFG: without /COM1 or /COM2;
 * /NET is refused with /LOCAL and /NOW:, and without a name or with one
 * that is not a NetBIOS name; /COM1 and /COM2 with each other, /LOCAL,
 * /NET and /NOW:.
 *
 * Exit status: 0 normal end (also at the end of a key script); 1 usage
 * error, /MDA on another adapter, a bad key script, or a /PRN: file
 * that cannot be created or is a device;
 * 2 the data directory could not be opened, or the NET or serial link
 * (the reason is printed), or the server refused the session's HELLO
 * (the reason is shown); 3 DOS older than 3.1; 4 standard input could
 * not be read, standard output could not be written, the data files
 * could not be flushed and closed at the end, or the key script could
 * not be read or a snapshot written; 5 login refused for the third
 * time; 6 no memory for the screen; 7 the message file missing,
 * unreadable, malformed or made for another version; 8 the settings
 * file cannot be read; 9 the connection to the server was lost.
 */
#define TUI_VERBTAB
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "LINK.H"
#include "SESS.H"
#include "MSG.H"
#include "TUI.H"
#include "KEYSCR.H"
#include "CFG.H"
#include "ARGS.H"
#include "INVCOM.H"
#include "INVCONN.H"
#include "SCR.H"
#include "INVMENU.H"
#include "SCRVERB.H"

#define EXIT_OK         0
#define EXIT_USAGE      1
#define EXIT_OPEN       SCR_EXIT_OPEN
#define EXIT_DOSVER     3
#define EXIT_IO         4
#define EXIT_SCREEN     6
#define EXIT_MSG        7
#define EXIT_CFG        8
#define EXIT_LOST       TUI_EXIT_LOST

/* Results of inv_switch besides those of ARGS.H. */
#define INV_EHELP       ARGS_EOWN       /* /?                           */
#define INV_ETIME       (ARGS_EOWN + 1) /* a bad /NOW: time             */

#define INBUF           128     /* bytes of standard input read at once */

cfg scr_cfg;
const char *scr_cfgpath = CFG_NAME;
const char *scr_datadir = "";
u8 scr_vidflags;

/* The data directory in use, kept apart from scr_cfg.data, which the
   settings screen may change. */
static char inv_dir[LINK_MAXDIR + 1];

/* The /NOW: time, or NULL for the DOS clock (scr_date). */
static const link_clock *inv_now;

/* What inv_exit has to undo. */
static link *inv_l;
static u8 inv_up;               /* INV_U_ bits                          */
#define INV_U_MSG       0x01
#define INV_U_CRIT      0x02    /* and BREAK checking off               */
#define INV_U_KEYS      0x04
#define INV_U_VID       0x08

/* Write a, b and c on standard error: printf would cost the program
   kilobytes of code for these lines alone. */
static void say(const char *a, const char *b, const char *c)
{
    fputs(a, stderr);
    fputs(b, stderr);
    fputs(c, stderr);
}

/* Write " (error rc)" and the line end on standard error. */
static void say_rc(int rc)
{
    char d[TXT_U32LEN + 2];

    d[0] = '-';
    txt_u32(d + 1, (u32)(rc < 0 ? -(i32)rc : (i32)rc));
    say(" (error ", rc < 0 ? d : d + 1, ")\n");
}

/* Write the program's name (inv_tag), then a, b and c, on standard
   error. */
static void tell(const char *a, const char *b, const char *c)
{
    fputs(inv_tag, stderr);
    say(a, b, c);
}

/* The usage text on f, defdir the default data directory; called before
   /CFG: is applied, so scr_cfgpath still names the default file. */
static void usage(FILE *f, const char *defdir)
{
    fputs(inv_usage_head, f);
    if (inv_usage_dir[0] != '\0') {
        fputs(defdir, f);
        fputs(inv_usage_dir, f);
    }
    fputs("  /NET    use the server called name\n"
          "  /COMn   use the server on serial port n (1 or 2)\n"
          "  /BIOS   draw the screen through the BIOS\n", f);
    fputs("  /CFG:   the settings file (default ", f);
    fputs(scr_cfgpath, f);
    fputs(")\n", f);
    fputs("  /CMD    read protocol requests from standard input and write "
          "the\n"
          "          responses to standard output\n", f);
}

/* Open the link on the data directory dir with the clock now (NULL for
   the DOS clock) into *l; 0, or the failure said on standard error. */
static int open_data(link **l, const char *dir, const link_clock *now)
{
    char why[LINK_WHYSZ];
    int rc;

    rc = link_open_local(l, dir, now, why, (u16)sizeof why);
    if (rc != 0) {
        tell("cannot open the data directory ",
             dir[0] != '\0' ? dir : ".", ": ");
        fputs(why, stderr);
        say_rc(rc);
    }
    return rc;
}

/* open_net names the failures of link_open_net from a table indexed
   from LINK_ENONB. */
extern char inv_netcodes_ok[1];
extern char inv_netcodes_ok[(LINK_ENAME == LINK_ENONB + 1 &&
                             LINK_ECALL == LINK_ENONB + 2 &&
                             LINK_ENOMEM == LINK_ENONB + 3 &&
                             LINK_EREFUSED == LINK_ENONB + 4) ? 1 : 2];

/* Open the NET link to server into *l, saying first on standard error
   that it calls, as the calls can take a minute; 0, or the failure said
   there. */
static int open_net(link **l, const char *server)
{
    static const char *const why[] = {
        "no NetBIOS", "local name refused", "no answer", "no memory",
        "refused"
    };
    char hex[3];
    int rc;
    u8 nbrc;

    tell("calling ", server, "\n");
    rc = link_open_net(l, server, &nbrc);
    if (rc != 0) {
        tell("cannot call ", server, ": ");
        fputs(why[rc - LINK_ENONB], stderr);
        if (nbrc != NB_OK) {
            hex[0] = txt_hexdig[nbrc >> 4];
            hex[1] = txt_hexdig[nbrc & 15];
            hex[2] = '\0';
            say(" (NetBIOS error ", hex, ")");
        }
        fputc('\n', stderr);
    }
    return rc;
}

/* Load MSG_FILE from the program's directory progdir, else the current
   one; 0, or the failure said on standard error. */
static int load_msgs(const char *progdir)
{
    int rc;

    rc = msg_load(progdir, MSG_FILE);
    if (rc != 0) {
        tell(MSG_NOFILE, msg_why()[0] != '\0' ? ": " : "", msg_why());
        fputc('\n', stderr);
    }
    return rc;
}

/* Read the settings file path into scr_cfg; exits with EXIT_CFG when
   cfg_load fails. */
static void load_cfg(const char *path)
{
    int rc;

    rc = cfg_load(&scr_cfg, path);
    if (rc != 0) {
        tell("cannot read the settings file ", path, "");
        say_rc(rc);
        exit(EXIT_CFG);
    }
}

/* Hold the exit while the NetBIOS provider still owns commands of a
   closed link (nba_close), until it gives them back or a key says to
   take the risk. */
static void net_drain(void)
{
    char d[TXT_U32LEN + 1];
    u16 n;

    n = nba_stuck();
    if (n == 0)
        return;
    txt_u32(d, n);
    tell("NetBIOS busy (", d, "); a key quits\n");
    nba_drain();
}

/* The exit status after a closing step returned rc: status, or EXIT_IO
   in place of EXIT_OK when rc is not 0, which is then said on standard
   error as what failed.  Callers remove the critical-error handler
   first. */
static int close_status(int rc, int status, const char *what)
{
    if (rc != 0) {
        tell(what, "", "");
        say_rc(rc);
        if (status == EXIT_OK)
            status = EXIT_IO;
    }
    return status;
}

static const char k_closedata[] = "cannot close the data files";

/* Undo everything start-up did, report a failure to close the data
   files and one of the key script or its snapshot file (ks_close), and
   exit with status (EXIT_IO instead of EXIT_OK after such a failure). */
static void inv_exit(int status)
{
    int rc;
    int krc;

    rc = 0;
    krc = 0;
    scr_wipe();
    if (inv_l != NULL) {
        rc = link_close(inv_l);
        inv_l = NULL;
        tui_s.l = NULL;
    }
    if (inv_up & INV_U_KEYS)
        krc = ks_close();
    if (inv_up & INV_U_VID)
        vid_close();
    if (inv_up & INV_U_CRIT) {
        dos_crit_remove();
        dos_break_restore();
    }
    if (inv_up & INV_U_MSG)
        msg_free();
    inv_up = 0;
    net_drain();
    status = close_status(rc, status, k_closedata);
    exit(close_status(krc, status, "key script failed"));
}

/* 1 if the switch text s (after its '/' or '-') begins with p, ignoring
   case. */
static int sw_prefix(const char *s, const char *p)
{
    return txt_ieqn(s, p, (u16)strlen(p));
}

static u16 digits(const char *s, int n)
{
    u16 v;

    v = 0;
    while (n-- > 0) {
        v = (u16)(v * 10 + (u16)(*s - '0'));
        s++;
    }
    return v;
}

/* Parse YYYYMMDDHHMMSS, a date of the years 1980..2079 (those DOS
   keeps), into *c; 0 if it is not a valid time. */
static int parse_now(const char *s, link_clock *c)
{
    char date[SCR_DATESZ];
    u16 i;

    for (i = 0; i < 14; i++)
        if (!TXT_ISDIGIT(s[i]))
            return 0;
    if (s[14] != '\0')
        return 0;
    memcpy(date, s, SCR_DATESZ - 1);
    date[SCR_DATESZ - 1] = '\0';
    c->year = digits(s, 4);
    c->month = (u8)digits(s + 4, 2);
    c->day = (u8)digits(s + 6, 2);
    c->hour = (u8)digits(s + 8, 2);
    c->minute = (u8)digits(s + 10, 2);
    c->second = (u8)digits(s + 12, 2);
    return txt_date_ok(date) && c->year >= 1980 && c->year <= 2079 &&
           c->hour <= 23 && c->minute <= 59 && c->second <= 59;
}

/* ---------------------------------------------------------------------
 * Scripted mode.
 * --------------------------------------------------------------------- */

/* The buffers of the /CMD loop, on its stack: near data is scarce, and
   the loop runs straight from main. */
typedef struct {
    int  failed;                /* a response line could not be written */
    char in[INBUF];
    char out[LINK_MAXRESP + 2];
} cmd_io;

/* Link callback: one response line to handle 1, CR LF ended. */
static int put_line(void *arg, const char *line, u16 len)
{
    cmd_io *io;

    io = (cmd_io *)arg;
    if (len > LINK_MAXRESP)
        len = LINK_MAXRESP;
    memcpy(io->out, line, len);
    io->out[len] = '\r';
    io->out[len + 1] = '\n';
    if (cf_write(1, io->out, (u16)(len + 2)) != 0) {
        io->failed = 1;
        return 1;
    }
    return 0;
}

/* The /CMD loop; returns an exit status. */
static int run_cmd(link *l)
{
    cmd_io io;
    int pending;                /* bytes of an unfinished line were fed */
    int eof;
    int r;
    u16 got;
    u16 n;

    pending = 0;
    eof = 0;
    r = LINK_OK;
    io.failed = 0;
    while (!eof) {
        if (cf_read(0, io.in, INBUF, &got) != 0) {
            tell("cannot read standard input\n", "", "");
            return EXIT_IO;
        }
        if (got == 0)
            break;
        for (n = 0; n < got && io.in[n] != '\x1A'; n++)
            ;
        if (n < got)
            eof = 1;
        if (n > 0) {
            pending = io.in[n - 1] != '\n';
            r = link_feed(l, io.in, n, put_line, &io);
        }
        /* The input may hold a password. */
        txt_wipe(io.in, INBUF);
        if (r != LINK_OK)
            break;
    }
    if (r == LINK_OK && pending)
        r = link_feed(l, "\n", 1, put_line, &io);
    if (r == LINK_EOUTPUT || io.failed) {
        tell("cannot write standard output\n", "", "");
        return EXIT_IO;
    }
    if (r == LINK_ELOST) {
        tell("connection to the server lost\n", "", "");
        return EXIT_LOST;
    }
    return EXIT_OK;
}

/* ---------------------------------------------------------------------
 * The full-screen client.
 * --------------------------------------------------------------------- */

void scr_date(char *out)
{
    u16 y;
    u8 m;
    u8 d;

    if (inv_now != NULL) {
        y = inv_now->year;
        m = inv_now->month;
        d = inv_now->day;
    } else {
        dos_today(&y, &m, &d);
    }
    txt_u32w(out, y, 4, '0');
    txt_u32w(out + 4, m, 2, '0');
    txt_u32w(out + 6, d, 2, '0');
    out[SCR_DATESZ - 1] = '\0';
}

link *scr_reopen(void)
{
    u8 nbrc;

    link_close(inv_l);
    /* scr_datadir names the server on a NET link. */
    link_open_net(&inv_l, scr_datadir, &nbrc);
    return inv_l;
}

/* Run one command of the menu table. */
static u8 inv_run(u8 cmd)
{
    switch (cmd) {
    case SCMD_ITEMS:
    case SCMD_ITEMS_ID:
    case SCMD_ITEMS_LOC:
    case SCMD_ITEMS_MFR:
    case SCMD_ITEMS_SER:
    case SCMD_ITEM_NEW:
        return scr_items(cmd);
    case SCMD_ASSY:
        return scr_assy(cmd);
    case SCMD_MAINT:
        return scr_maint(cmd);
    case SCMD_LOANS:
        return scr_loans(cmd);
    case SCMD_LOCS:
    case SCMD_LOC_NEW:
        return scr_locs(cmd);
    case SCMD_CATS:
    case SCMD_LTYPES:
        return scr_codes(cmd);
    case SCMD_FIND:
    case SCMD_SERIAL:
    case SCMD_FILTER:
        return scr_search(cmd);
    case SCMD_RPT_LIST:
    case SCMD_RPT_VALUE:
    case SCMD_RPT_LOANS:
    case SCMD_LBL_ITEM:
    case SCMD_LBL_BIN:
        return scr_report(cmd);
    case SCMD_EXPORT:
    case SCMD_IMPORT:
        return scr_csv(cmd);
    case SCMD_USERS:
        return scr_users(cmd);
    case SCMD_BACKUP:
    case SCMD_RESTORE:
    case SCMD_PACK:
    case SCMD_REINDEX:
    case SCMD_AUDIT:
    case SCMD_SESSIONS:
        return scr_admin(cmd);
    case SCMD_SETTINGS:
        return scr_settings(cmd);
    case SCMD_PASSWD:
    case SCMD_MUSTCHG:
        return scr_passwd(cmd);
    default:
        return TUI_CMD_NONE;
    }
}

static const tui_app inv_app = {
    INVMENU_COUNT, invmenu_bar, inv_run, scr_home, scr_login, SCMD_FIND
};

/* The settings, switches and test hooks of one run. */
typedef struct {
    args a;                     /* the switches of ARGS.H               */
    const char *defdir;
    const char *keys;           /* /KEYS: file, or NULL                 */
    const char *snap;           /* /SNAP: file, or NULL                 */
    const char *prn;            /* /PRN: file, or NULL                  */
    const char *prog;           /* argv[0], or ""                       */
    const link_clock *now;      /* &clk after /NOW:, else NULL          */
    link_clock clk;
    u8   cmd;                   /* /CMD                                 */
    u8   vflags;                /* VID_F_ from the switches             */
} inv_opts;

/* args_parse's function: INV's own switches into the inv_opts arg. */
static int inv_switch(void *arg, const char *s)
{
    inv_opts *o;

    o = (inv_opts *)arg;
    if (txt_ieq(s, "?"))
        return INV_EHELP;
    if (txt_ieq(s, "CMD") && !o->cmd) {
        o->cmd = 1;
    } else if (sw_prefix(s, "NOW:") && o->now == NULL) {
        if (!parse_now(s + 4, &o->clk))
            return INV_ETIME;
        o->now = &o->clk;
    } else if (txt_ieq(s, "BIOS") && !(o->vflags & VID_F_BIOS)) {
        o->vflags |= VID_F_BIOS;
    } else if (txt_ieq(s, "MDA") && !(o->vflags & VID_F_MDA)) {
        o->vflags |= VID_F_MDA;
    } else if (sw_prefix(s, "KEYS:") && s[5] != '\0' && o->keys == NULL) {
        o->keys = s + 5;
    } else if (sw_prefix(s, "SNAP:") && s[5] != '\0' && o->snap == NULL) {
        o->snap = s + 5;
    } else if (sw_prefix(s, "PRN:") && s[4] != '\0' && o->prn == NULL) {
        o->prn = s + 4;
    } else {
        return ARGS_EBAD;
    }
    return ARGS_OK;
}

/* Open the link of the switches o into *l: NET, SERIAL, or LOCAL on the
   data directory dir; 0, or the failure said on standard error. */
static int open_link(link **l, const inv_opts *o, const char *dir)
{
    if (o->a.net != NULL)
        return open_net(l, o->a.net);
    if (o->a.com != 0)
        return invcom_open(l, o->a.com, &scr_cfg);
    return open_data(l, dir, o->now);
}

/* Run the full-screen client; never returns. */
static void run_tui(const inv_opts *o)
{
    char why[LINK_WHYSZ];
    char progdir[LINK_MAXDIR + 1];
    const char *dir;
    u8 vflags;
    int rc;

    vflags = o->vflags;
    load_cfg(scr_cfgpath);
    inv_now = o->now;
    txt_path_dir(progdir, (u16)sizeof progdir, o->prog);
    dir = o->a.dir;
    scr_datadir = o->a.net;
    if (o->a.net == NULL) {
        if (dir == NULL)
            dir = scr_cfg.data[0] != '\0' ? scr_cfg.data : o->defdir;
        if (o->a.com != 0)
            dir = o->a.comname;
        strncpy(inv_dir, dir, LINK_MAXDIR);
        inv_dir[LINK_MAXDIR] = '\0';
        scr_datadir = inv_dir;
    }
    dos_break_off();
    dos_crit_install();
    inv_up |= INV_U_CRIT;
    rc = open_link(&inv_l, o, dir);
    if (rc != 0) {
        inv_l = NULL;
        inv_exit(EXIT_OPEN);
    }
    if (load_msgs(progdir) != 0)
        inv_exit(EXIT_MSG);
    inv_up |= INV_U_MSG;

    if (o->keys != NULL) {
        rc = ks_open(o->keys, o->snap, why, (u16)sizeof why);
        if (rc != 0) {
            tell("bad key script: ", why[0] != '\0' ? why : o->keys,
                 "\n");
            inv_exit(EXIT_USAGE);
        }
        inv_up |= INV_U_KEYS;
        cstk_fill();
    }
    if (o->prn != NULL) {
        rc = prn_capture(o->prn);
        if (rc != 0) {
            tell("cannot capture to ", o->prn, "");
            say_rc(rc);
            inv_exit(EXIT_USAGE);
        }
    }

    if (scr_cfg.bios)
        vflags |= VID_F_BIOS;
    if (scr_cfg.snow)
        vflags |= VID_F_SNOW;
    scr_vidflags = vflags;
    if (vid_open(vflags) != 0) {
        tell("cannot set up the screen\n", "", "");
        inv_exit(EXIT_SCREEN);
    }
    inv_up |= INV_U_VID;
    if ((vflags & VID_F_MDA) && vid_adapter() != VID_A_MDA) {
        vid_close();
        inv_up &= (u8)~INV_U_VID;
        tell("/MDA is not available in this version\n", "", "");
        inv_exit(EXIT_USAGE);
    }
    key_mouse();
    /* vid_mono does not tell mode 7 from mode 2: an EGA or VGA showing
       mode 7 on a monochrome monitor gets TP_MONOCGA, which lacks only
       the underline and still keeps every role distinct. */
    if (vid_mono())
        tui_palette((u8)(vid_adapter() == VID_A_MDA ? TP_MONO : TP_MONOCGA));
    else
        tui_palette((u8)(scr_cfg.mono ? TP_MONOCGA : TP_COLOR));

    tui_s.l = inv_l;
    tui_s.conn = o->a.net != NULL ? TUI_CONN_NET :
                 o->a.com != 0 ? TUI_CONN_COM : TUI_CONN_LOCAL;
    tui_s.level = 0;
    sn_station(tui_s.station, link_netname(inv_l), scr_cfg.station);
    tui_set_exit(inv_exit);
    tui_set_helpdir(progdir);
    tui_verbs(SV_OPER, MW_OPEN);
    tui_run(&inv_app);
    inv_exit(EXIT_OK);
}

int main(int argc, char **argv)
{
    char defdir[CFG_DEFDIRSZ];
    const char *w;
    inv_opts o;
    link *l;
    int rc;
    int st;
    u8 major;
    u8 minor;

    cmem_init();
    if (!dos_version(&major, &minor)) {
        tell("requires MS-DOS 3.1 or later\n", "", "");
        return EXIT_DOSVER;
    }
    strcpy(defdir, cfg_defdir);
    if (argc > 0 && strchr(argv[0], '/') != NULL &&
        strchr(argv[0], '\\') == NULL)
        defdir[sizeof defdir - 2] = '/';
    o.defdir = defdir;
    o.keys = NULL;
    o.snap = NULL;
    o.prn = NULL;
    o.prog = argc > 0 ? argv[0] : "";
    o.now = NULL;
    o.cmd = 0;
    o.vflags = 0;

    rc = args_parse(&o.a, argc, argv, inv_switch, &o, &w);
    switch (rc) {
    case INV_EHELP:
        usage(stdout, defdir);
        return EXIT_OK;
    case INV_ETIME:
        tell("bad time: ", w, "\n");
        break;
    case ARGS_EBAD:
        tell("bad or repeated argument: ", w, "\n");
        break;
    case ARGS_ENONAME:
        tell("/NET needs the server's name\n", "", "");
        break;
    default:
        break;
    }
    if (rc != ARGS_OK ||
        (o.cmd && (o.vflags != 0 || o.keys != NULL || o.snap != NULL ||
                   o.prn != NULL || (o.a.cfg != NULL && o.a.com == 0))) ||
        (o.now != NULL && o.a.kind != ARGS_LOCAL) ||
        (o.a.kind & inv_lacks) != 0 ||
        (o.snap != NULL && o.keys == NULL)) {
        usage(stderr, defdir);
        return EXIT_USAGE;
    }

    if (o.a.cfg != NULL)
        scr_cfgpath = o.a.cfg;
    if (!o.cmd)
        run_tui(&o);

    if (o.a.dir == NULL)
        o.a.dir = defdir;
    if (o.a.com != 0)
        load_cfg(scr_cfgpath);
    if (o.a.com != 0 || o.a.kind == ARGS_NET)
        dos_break_off();
    dos_crit_install();
    rc = open_link(&l, &o, o.a.dir);
    if (rc != 0) {
        dos_crit_remove();
        dos_break_restore();
        return EXIT_OPEN;
    }
    st = run_cmd(l);
    rc = link_close(l);
    /* As in inv_exit, the critical-error handler is removed before
       close_status reports, so that a critical error while writing the
       report gets DOS's own prompt. */
    dos_crit_remove();
    dos_break_restore();
    net_drain();
    return close_status(rc, st, k_closedata);
}
