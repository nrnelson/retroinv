/*
 * SRVRUN.C - the server's run: start-up, loop and shutdown (SRVRUN.H).
 *
 * The run's state is file-scope, set afresh by every srv_run: the
 * settings, the engine, the loop's tick count and the event log.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#define COMPAT_FIND
#define PROTO_TICKETS
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TNETB.H"
#include "TSERIAL.H"
#include "TMBOX.H"
#include "TICKET.H"
#include "THROTL.H"
#include "SRVCFG.H"
#include "MSG.H"
#include "SRVSCR.H"
#include "SRVRUN.H"

/* A log line: "YYYY-MM-DD HH:MM:SS ", the u8 slot and a space, the
   event word and a space, the text, CR LF.  txt_u32 writes the slot
   into the room of all that follows the stamp, which holds its
   TXT_U32LEN + 1 bytes. */
#define LG_STAMP        (DOS_STAMPLEN + 1)
#define LG_SLOT         3       /* digits of the largest u8             */
#define LG_WORD         5       /* characters of the longest event word */
#define LG_LINE         (LG_STAMP + LG_SLOT + 1 + LG_WORD + 1 + \
                         CONN_TRACELEN + 2)

/* Messages, MSG_R_SRV range (beside SRVSCR.C's), for the failures said
   once the message file is loaded. */
#define MD_NODATA       1101    /* the data: ^1 the reason, ^2 the code */
#define MD_NOLOG        1102    /* the log not opened: ^1 path, ^2 code */
#define MD_NOMEM        1103    /* no memory for the sessions           */
#define MD_NAMEUSE      1104    /* the name ^1 in use                   */
#define MD_NONAME       1105    /* ADD NAME of ^1 failed, ^2 its code   */
#define MD_NOSCREEN     1106    /* no screen                            */
#define MD_NOCLOSE      1107    /* the data not closed: ^1 the code     */
#define MD_NOLOGW       1108    /* log not written: ^1 path, ^2 code    */
#define MD_STUCK        1109    /* ^1 NCBs still owned: waiting         */
#define MD_NOPORT       1110    /* no serial port COM^1                 */
#define MD_PORTUSE      1111    /* the serial port COM^1 in use         */
#define MD_NOMBOX       1112    /* the mailbox ^1 or its directories
                                   missing                              */

static const char k_prog[] = "INVSERV: ";

/* Ticks into a shutdown after which the loop ends with slots still in
   use: CONN closes them after CONN_SHUTWAIT, the transport gets as
   long again to confirm it. */
#define RUN_STOPWAIT    (2 * CONN_SHUTWAIT)

/* The words of the CONN_EV_ events, from CONN_EV_OPEN on. */
static const char ev_words[] = "OPEN LOGIN REQ CLOSE";

static srvcfg srv_cfg;
static eng *srv_eng;

/* The loop's tick count. */
static tmr_clock srv_clock;

/* The event log: its handle (-1 for none), whether it holds lines not
   yet committed, when it was last committed, and the first error that
   closed it (0 for none). */
static int srv_log;
static u8 srv_logdirty;
static u32 srv_logat;
static int srv_logrc;

/* Say text n of the message file, with a1 and a2 (msg_sub), as a line
   on standard error after the program's name. */
static void say(u16 n, const char *a1, const char *a2)
{
    fputs(k_prog, stderr);
    fputs(msg_sub(n, a1, a2), stderr);
    fputc('\n', stderr);
}

/* Write a line on standard error: the program's name, a, b, c and d.
   printf would cost the program kilobytes of code for the few lines it
   writes before its message file is read. */
static void err4(const char *a, const char *b, const char *c,
                 const char *d)
{
    fputs(k_prog, stderr);
    fputs(a, stderr);
    fputs(b, stderr);
    fputs(c, stderr);
    fputs(d, stderr);
    fputc('\n', stderr);
}

/* ---------------------------------------------------------------------
 * The event log.
 * --------------------------------------------------------------------- */

/* Open the log path for appending, denying others write access, after
   creating it when it is missing; 0 or the CF_E code.  DOS creates a
   file in compatibility mode, so a new log is closed and opened again.
   A log another program makes between the open and the create is
   opened as it is, never truncated. */
static int log_open(const char *path)
{
    u32 size;
    int h;
    int rc;

    rc = cf_open(path, CF_RDWR | CF_DENYW, &h);
    if (rc == CF_ENOENT) {
        rc = cf_create_new(path, &h);
        if (rc == 0)
            rc = cf_close(h);
        else if (rc == CF_EEXIST)
            rc = 0;
        if (rc == 0)
            rc = cf_open(path, CF_RDWR | CF_DENYW, &h);
    }
    if (rc == 0) {
        rc = cf_size(h, &size);
        if (rc == 0)
            rc = cf_seek(h, size);
        if (rc != 0)
            cf_close(h);
        else
            srv_log = h;
    }
    return rc;
}

/* Close the log after the error rc (0 for none), keeping the first. */
static void log_close(int rc)
{
    int rc2;

    if (srv_log < 0)
        return;
    rc2 = cf_close(srv_log);
    srv_log = -1;
    if (rc == 0)
        rc = rc2;
    if (srv_logrc == 0)
        srv_logrc = rc;
}

/* Append the line of event word for slot with text to the log. */
static void log_line(u8 slot, const char *word, const char *text)
{
    char ln[LG_LINE];
    u16 n;
    int rc;

    if (srv_log < 0)
        return;
    dos_stamp(ln);
    ln[DOS_STAMPLEN] = ' ';
    n = (u16)(LG_STAMP + txt_u32(ln + LG_STAMP, slot));
    ln[n++] = ' ';
    txt_copy(ln + n, word, LG_WORD + 1);
    n = (u16)(n + strlen(ln + n));
    if (text[0] != '\0') {
        ln[n++] = ' ';
        txt_copy(ln + n, text, CONN_TRACELEN + 1);
        n = (u16)(n + strlen(ln + n));
    }
    ln[n++] = '\r';
    ln[n++] = '\n';
    rc = cf_write(srv_log, ln, n);
    if (rc != 0)
        log_close(rc);
    else
        srv_logdirty = 1;
}

/* Commit the log's new lines when the pass at now was idle or TMR_SEC
   ticks have gone since the last commit. */
static void log_sync(u32 now, int idle)
{
    int rc;

    if (!srv_logdirty || (!idle && now - srv_logat < TMR_SEC))
        return;
    srv_logdirty = 0;
    srv_logat = now;
    if (srv_log >= 0) {
        rc = dos_commit_handle(srv_log);
        if (rc != 0)
            log_close(rc);
    }
}

/* tmbox_watch callback: the MBOX line of the log. */
static void on_mbox(u8 slot, const char *station, int err)
{
    char t[TXT_U32LEN + 1 + TMBOX_STALEN + 1];
    u16 n;

    n = txt_u32(t, (u32)err);
    t[n++] = ' ';
    txt_copy(t + n, station, TMBOX_STALEN + 1);
    log_line(slot, "MBOX", t);
}

/* conn_watch callback: the trace line and the log. */
static void on_event(u8 c, u8 ev, const char *text)
{
    char word[LG_WORD + 1];

    srvscr_trace(c, ev, text);
    txt_word_at(ev_words, (u16)(ev - CONN_EV_OPEN), word, (u16)sizeof word);
    log_line((u8)(c + 1), word, text);
}

/* ---------------------------------------------------------------------
 * Start, loop and stop.
 * --------------------------------------------------------------------- */

/* Say that there is no memory for the sessions: SRV_EXIT_NOMEM. */
static int no_mem(void)
{
    say(MD_NOMEM, NULL, NULL);
    return SRV_EXIT_NOMEM;
}

/* Start the transports of the settings in the order of SRVRUN.H;
   SRV_EXIT_OK, or the status of the failure, said on standard error,
   with what was started left for stop_all. */
static int start_transports(void)
{
    char n[TXT_U32LEN + 1];
    int rc;
    u8 nbrc;
    u8 bad;

    if (srv_cfg.sessions != 0) {
        rc = tnetb_start(srv_cfg.name, srv_cfg.sessions, srv_cfg.listens,
                         &nbrc);
        if (rc == TNETB_ENOMEM)
            return no_mem();
        if (rc == TNETB_ENAME) {
            say(MD_NAMEUSE, srv_cfg.name, NULL);
            return SRV_EXIT_NAME;
        }
        if (rc != TNETB_OK) {
            n[0] = txt_hexdig[nbrc >> 4];
            n[1] = txt_hexdig[nbrc & 0x0F];
            n[2] = '\0';
            say(MD_NONAME, srv_cfg.name, n);
            return SRV_EXIT_NAME;
        }
    }
    if (srv_cfg.serial != 0) {
        rc = tserial_start(srv_cfg.serial, srv_cfg.baud,
                           (u8)(srv_cfg.rtscts ? UART_F_RTSCTS : 0), &bad);
        if (rc == TSER_ENOMEM)
            return no_mem();
        if (rc != TSER_OK) {
            txt_u32(n, bad);
            say(rc == TSER_EBUSY ? MD_PORTUSE : MD_NOPORT, n, NULL);
            return SRV_EXIT_PORT;
        }
    }
    if (srv_cfg.mailbox[0] != '\0') {
        tmbox_watch(on_mbox);
        rc = tmbox_start(srv_cfg.mailbox);
        if (rc == TMBOX_ENOMEM)
            return no_mem();
        if (rc != TMBOX_OK) {
            say(MD_NOMBOX, srv_cfg.mailbox, NULL);
            return SRV_EXIT_MBOX;
        }
    }
    return SRV_EXIT_OK;
}

/* Start everything in the order of SRVRUN.H, the settings from cfgpath
   and the message file from progdir; SRV_EXIT_OK, or the status of the
   failure, said on standard error, with what was started left for
   stop_all. */
static int start(const char *cfgpath, const char *progdir)
{
    char why[ENG_WHYSZ];
    char n[TXT_U32LEN + 2];
    u32 idle;
    u16 bad;
    u8 nslots;
    u8 ports;
    u8 vflags;
    int rc;

    dos_crit_install();
    rc = srvcfg_load(&srv_cfg, cfgpath, &bad);
    if (rc != 0) {
        if (bad != 0) {
            txt_u32(n, bad);
            err4(cfgpath, " line ", n, " is not valid");
        } else {
            strcpy(n + txt_u32(n, (u32)rc), ")");
            err4("cannot read ", cfgpath, " (error ", n);
        }
        return SRV_EXIT_CFG;
    }
    if (srv_cfg.sessions != 0 && !nb_present()) {
        fputs(k_prog, stderr);
        fputs("no NetBIOS provider is loaded; SESSIONS=0 in INVSERV.CFG "
              "serves\nonly SERIAL= and MAILBOX=\n", stderr);
        return SRV_EXIT_NONB;
    }
    if (msg_load(progdir, MSG_SRVFILE) != 0) {
        err4("cannot read " MSG_SRVFILE, msg_why()[0] != '\0' ? ": " : "",
             msg_why(), "");
        return SRV_EXIT_MSG;
    }
    rc = eng_open(&srv_eng, srv_cfg.data, NULL, why, (u16)sizeof why);
    if (rc != ENG_OK) {
        txt_u32(n, (u32)rc);
        say(MD_NODATA, why, n);
        return SRV_EXIT_OPEN;
    }
    if (srv_cfg.log[0] != '\0') {
        rc = log_open(srv_cfg.log);
        if (rc != 0) {
            txt_u32(n, (u32)rc);
            say(MD_NOLOG, srv_cfg.log, n);
            return SRV_EXIT_IO;
        }
        log_line(0, "START", srv_cfg.name);
    }
    tmr_clock_start(&srv_clock);
    nslots = (u8)(srv_cfg.sessions + (srv_cfg.mailbox[0] != '\0'));
    for (ports = srv_cfg.serial; ports != 0; ports >>= 1)
        nslots = (u8)(nslots + (ports & 1));
    /* Minutes to ticks: TMR_10SEC for every ten seconds. */
    idle = (u32)srv_cfg.idle * 6 * TMR_10SEC;
    if (conn_init(srv_eng, srv_cfg.readonly[0] != '\0' ? srv_cfg.readonly :
                  NULL, nslots, idle, srv_clock.now) != 0)
        return no_mem();
    if (thr_open() != 0 || (srv_cfg.mailbox[0] != '\0' &&
                            tkt_open(idle, srv_clock.tick) == NULL))
        return no_mem();
    conn_tickets(tkt_logins());
    dos_break_off();
    rc = start_transports();
    if (rc != SRV_EXIT_OK)
        return rc;
    vflags = 0;
    if (srv_cfg.bios)
        vflags |= VID_F_BIOS;
    if (srv_cfg.snow)
        vflags |= VID_F_SNOW;
    if (srvscr_open(vflags, &srv_cfg) != 0) {
        say(MD_NOSCREEN, NULL, NULL);
        return SRV_EXIT_NOMEM;
    }
    conn_watch(on_event);
    return SRV_EXIT_OK;
}

/* Serve until the shutdown has ended every session, or for RUN_STOPWAIT
   ticks after it began (SRVRUN.H). */
static void serve(void)
{
    u32 now;
    u32 stopat;
    u16 work;
    u16 passes;
    int stopping;

    stopping = 0;
    stopat = 0;
    passes = 0;
    for (;;) {
        now = tmr_clock_now(&srv_clock);
        work = tnetb_poll(now);
        work = (u16)(work + tserial_poll(now));
        work = (u16)(work + tmbox_poll(now));
        work = (u16)(work + conn_service(now));
        passes++;
        if (work != 0) {
            tkt_stir(srv_clock.tick, passes);
            passes = 0;
        }
        now = tmr_clock_now(&srv_clock);
        conn_served(now);
        conn_timeouts(now);
        if (srvscr_poll(now) == SRVSCR_QUIT)
            conn_shutdown(now);
        if (conn_done())
            return;
        if (conn_stopping() && !stopping) {
            stopping = 1;
            stopat = now;
        } else if (stopping && now - stopat >= RUN_STOPWAIT) {
            return;
        }
        log_sync(now, work == 0);
        if (work == 0)
            dos_idle();
    }
}

/* Undo whatever start did, wait for the NCBs the provider still owns,
   and return status, or SRV_EXIT_IO in place of SRV_EXIT_OK when the
   data files could not be closed or the log written.  Each failure it
   says comes after the message file was loaded, since nothing that can
   fail here is started before it. */
static int stop_all(int status)
{
    char n[TXT_U32LEN + 1];
    u16 kept;
    u16 left;
    int rc;

    kept = tnetb_stop();
    tserial_stop();
    tmbox_stop();
    tkt_close();
    thr_close();
    rc = 0;
    if (srv_eng != NULL) {
        rc = eng_close(srv_eng);
        srv_eng = NULL;
    }
    srvscr_close();
    if (rc != 0) {
        txt_u32(n, (u32)rc);
        say(MD_NOCLOSE, n, NULL);
        if (status == SRV_EXIT_OK)
            status = SRV_EXIT_IO;
    }
    left = cstk_unused();
    if (left != CSTK_UNKNOWN) {
        txt_u32(n, left);
        log_line(0, "STACK", n);
    }
    txt_u32(n, (u32)status);
    log_line(0, "STOP", n);
    log_close(0);
    if (srv_logrc != 0) {
        txt_u32(n, (u32)srv_logrc);
        say(MD_NOLOGW, srv_cfg.log, n);
        if (status == SRV_EXIT_OK)
            status = SRV_EXIT_IO;
    }
    if (kept != 0) {
        txt_u32(n, kept);
        say(MD_STUCK, n, NULL);
        nba_drain();
        tnetb_stop();
    }
    /* The slots' output blocks go last: a SEND the provider still owned
       was sending from one. */
    conn_free();
    msg_free();
    conn_watch(NULL);
    dos_break_restore();
    dos_crit_remove();
    return status;
}

int srv_run(const char *cfgpath, const char *progdir)
{
    int st;

    srv_eng = NULL;
    srv_log = -1;
    srv_logdirty = 0;
    srv_logat = 0;
    srv_logrc = 0;
    st = start(cfgpath, progdir);
    if (st == SRV_EXIT_OK)
        serve();
    return stop_all(st);
}
