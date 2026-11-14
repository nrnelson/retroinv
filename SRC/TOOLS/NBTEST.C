/*
 * NBTEST.C - NetBIOS session probe between two machines.
 *
 *   NBTEST SERVER name [/WAIT:seconds] [/ASYNC] [/EXIT]
 *   NBTEST CLIENT localname remotename [/COUNT:n] [/ASYNC] [/EXIT]
 *
 * The server adds name, listens for a call from any name for up to
 * /WAIT: seconds (1..MAX_WAIT_SECS, DEF_WAIT_SECS if omitted; 0 is
 * rejected because the probe must not wait for ever) and answers each
 * message: "PONG n" to "PING n", "BULK len" to a bulk message of len
 * bytes, and "BYE" to "QUIT", after which it ends.  A bulk message is
 * bytes 'A' to 'Z' over and over.  The server uses the wait-form nb_
 * commands and receives each message whole into a buffer of NB_MSGMAX
 * bytes.  With /ASYNC it uses an nba set instead, with a far receive
 * buffer of RX_LEN bytes, so that it takes a bulk message in pieces,
 * through the "incomplete" return code and the RECEIVEs that continue
 * it, and it keeps the RECEIVE of the next message in progress while it
 * sends each reply.
 *
 * In both modes a LISTEN that ends with NB_RC_ABORTED lost a call
 * before its session opened (NetBEUI drops a session whose request
 * comes late, as on a loaded host); the server then listens again for
 * the rest of the wait.
 *
 * The client adds localname, calls remotename (retrying for
 * CALL_RETRY_SECS while the server may still be starting, or after a
 * call the server dropped, which times out), exchanges n PING/PONG
 * pairs (1..MAX_COUNT, DEF_COUNT if /COUNT: is omitted), sends a bulk
 * message of NB_MSGMAX bytes, then sends QUIT and expects BYE.
 * With /ASYNC it places each CALL as a no-wait command (nba_call) and
 * polls it to completion, printing first a line "NBTEST CALL remotename
 * from localname, no-wait" and with each CALL's result "pending=1" when
 * the CALL was still in progress once posted; the session then runs on
 * the wait-form commands as without /ASYNC.  The client reports the
 * time taken by the PING/PONG exchange in whole seconds.
 *
 * Switches match in any case and may begin with '-' for '/'.  /? alone
 * writes the usage text to standard output and exits 0; any other bad
 * command line writes it to standard error and nothing to standard
 * output.  Otherwise every line of standard output starts "NBTEST ",
 * each step printing its NetBIOS return code in hex, and the last line
 * is the verdict, "NBTEST PASS" or "NBTEST FAIL reason".
 *
 * Exit status: 0 pass, 1 usage error, 2 NetBIOS not present, 3 the test
 * failed, 4 DOS older than 3.1.  With /EXIT the status is also written
 * to the QEMU isa-debug-exit port so a test VM ends.  Names are
 * upper-cased and cut to NB_NAMELEN - 1 characters.
 */
#define COMPAT_NETBIOS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"

#define EXIT_PASS       0
#define EXIT_USAGE      1
#define EXIT_NONB       2
#define EXIT_FAIL       3
#define EXIT_DOSVER     4

#define DEF_COUNT       10
#define MAX_COUNT       30000
#define CALL_RETRY_SECS 60
#define DEF_WAIT_SECS   120
#define MAX_WAIT_SECS   3600
#define MSG_MAX         64

/* The /ASYNC server's NCBs, and its far buffer: the receive buffer, then
   the reply. */
#define H_RX            0       /* LISTEN, then each RECEIVE            */
#define H_TX            1       /* each SEND                            */
#define H_COUNT         2
#define RX_LEN          512
#define TX_OFF          RX_LEN
#define BUF_LEN         (RX_LEN + MSG_MAX)

/* The /ASYNC client's one NCB, which places each CALL. */
#define H_CALL          0

/* Session timeouts in 500 ms units, for the server's wait for the next
   message and the client's wait for a reply. */
#define SRV_RTO         120
#define SRV_STO         120
#define CLI_RTO         60
#define CLI_STO         60

/* A message received in pieces: its length, the number of pieces, 1
   while every byte matches the bulk pattern, and its first MSG_MAX - 1
   bytes as a C string. */
typedef struct {
    u16 len;
    u16 pieces;
    int bulk;
    char text[MSG_MAX];
} rxmsg;

/* The settings of the switches, 0 when not given. */
static u16 nbt_count;           /* /COUNT:                              */
static u16 nbt_wait;            /* /WAIT:                               */
static u8 nbt_async;            /* 1 under /ASYNC                       */
static u8 nbt_exit;             /* 1 under /EXIT                        */

static const char k_any[] = "*";
static const char k_quit[] = "QUIT";
static const char k_bye[] = "BYE";
static const char k_bulkfmt[] = "BULK %u";
static const char k_sendfmt[] = "SEND %s rc=%02X\n";

/* The reasons of a failure. */
static const char k_memory[] = "memory";
static const char k_listen[] = "listen";
static const char k_listento[] = "listen timeout";
static const char k_receive[] = "receive";
static const char k_send[] = "send";
static const char k_unexpected[] = "unexpected message";

/* Copy a command-line name into dst, upper-cased and truncated. */
static void copy_name(char *dst, const char *src)
{
    int i;
    char c;

    for (i = 0; i < NB_NAMELEN - 1 && src[i] != '\0'; i++) {
        c = src[i];
        dst[i] = (char)TXT_UPPER(c);
    }
    dst[i] = '\0';
}

/* Seconds in timer ticks, rounded up by one tick. */
static u32 secs_ticks(u16 secs)
{
    return (u32)secs * TMR_10SEC / 10U + 1U;
}

/* Whole seconds left of a wait of secs seconds that began at start, 0
   once it is over. */
static u16 secs_left(u32 start, u16 secs)
{
    u32 spent;

    spent = tmr_since(start) * 10U / TMR_10SEC;
    return spent < secs ? (u16)(secs - spent) : 0;
}

/* Byte i of a bulk message. */
static char bulk_byte(u16 i)
{
    return (char)('A' + i % 26);
}

/* Print one line of the run: "NBTEST ", then fmt with its arguments. */
static void say(const char *fmt, ...)
{
    va_list ap;

    printf("NBTEST ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* End the VM with code under /EXIT, and return code. */
static int leave(int code)
{
    if (nbt_exit)
        cexit_vm((u8)code);
    return code;
}

/* Print the verdict, "NBTEST PASS" for EXIT_PASS and otherwise
   "NBTEST FAIL" and reason, and leave with code. */
static int finish(int code, const char *reason)
{
    if (code == EXIT_PASS)
        say("PASS\n");
    else
        say("FAIL %s\n", reason);
    fflush(stdout);
    return leave(code);
}

/* Receive one message into buf as a C string. */
static u8 recv_msg(u8 lsn, char *buf)
{
    u16 len;
    u8 rc;

    len = (u16)(MSG_MAX - 1);
    rc = nb_recv(lsn, buf, &len);
    if (rc != NB_OK)
        len = 0;
    buf[len] = '\0';
    return rc;
}

/* Add the NetBIOS name and print the ADD NAME line; returns the NetBIOS
   return code. */
static u8 add_name(const char *name)
{
    u8 rc;
    u8 num;

    num = 0;
    rc = nb_add_name(name, &num);
    say("ADD NAME %s rc=%02X num=%02X\n", name, rc, num);
    return rc;
}

/* Hang up session lsn unless it is 0, delete name, close set (NULL for
   none) and free its buffer buf, and finish: a pass when reason is NULL,
   otherwise a failure for reason.  NCBs that the provider still owns
   after the close fail the run, which then waits until they complete or
   a key is pressed before it ends, and keeps buf. */
static int end_session(u8 lsn, const char *name, nba *set, cmem_blk *buf,
                       const char *reason)
{
    u16 kept;
    u8 rc;

    if (lsn != 0) {
        rc = nb_hangup(lsn);
        say("HANGUP rc=%02X\n", rc);
    }
    rc = nb_delete_name(name);
    say("DELETE NAME %s rc=%02X\n", name, rc);
    kept = nba_close(set);
    if (kept != 0) {
        say("CLOSE kept %u NCBs\n", (unsigned)kept);
        fflush(stdout);
        if (reason == NULL)
            reason = "ncbs kept";
    }
    if (nba_drain() == 0)
        cmem_free(buf);
    return finish(reason == NULL ? EXIT_PASS : EXIT_FAIL, reason);
}

/* Start an empty message in m. */
static void rx_start(rxmsg *m)
{
    m->len = 0;
    m->pieces = 0;
    m->bulk = 1;
    m->text[0] = '\0';
}

/* Append the n bytes at p to the message m. */
static void rx_add(rxmsg *m, const char *p, u16 n)
{
    for (; n > 0; n--, p++, m->len++) {
        if (m->len < MSG_MAX - 1) {
            m->text[m->len] = *p;
            m->text[m->len + 1] = '\0';
        }
        if (*p != bulk_byte(m->len))
            m->bulk = 0;
    }
}

/* Print the message m, received with return code rc, and write into
   reply the answer to it, "" for an unexpected message. */
static void answer(const rxmsg *m, u8 rc, char *reply)
{
    say("RECEIVE rc=%02X len=%u pieces=%u msg=%.16s\n",
        rc, (unsigned)m->len, (unsigned)m->pieces, m->text);
    if (m->len < MSG_MAX - 1 && strncmp(m->text, "PING ", 5) == 0)
        sprintf(reply, "PONG %s", m->text + 5);
    else if (strcmp(m->text, k_quit) == 0)
        strcpy(reply, k_bye);
    else if (m->bulk && m->len != 0)
        sprintf(reply, k_bulkfmt, (unsigned)m->len);
    else
        reply[0] = '\0';
}

/* Receive the next message of session lsn into m through wait-form
   RECEIVEs into buf, of NB_MSGMAX bytes; returns the return code of the
   last. */
static u8 recv_wait(u8 lsn, char *buf, rxmsg *m)
{
    u16 len;
    u8 rc;

    rx_start(m);
    do {
        len = NB_MSGMAX;
        rc = nb_recv(lsn, buf, &len);
        m->pieces++;
        rx_add(m, buf, len);
    } while (rc == NB_RC_INCOMPLETE);
    return rc;
}

/* Wait for the command on NCB h of set to complete, idling, for up to
   limit ticks (0 = no limit); returns its final code, or NB_RC_PENDING
   if the time ran out. */
static u8 wait_done(nba *set, u16 h, u32 limit)
{
    u32 start;
    u8 rc;

    start = tmr_ticks();
    while ((rc = nba_poll(set, h)) == NB_RC_PENDING &&
           (limit == 0 || tmr_since(start) < limit))
        dos_idle();
    return rc;
}

/* Receive into m the message whose first RECEIVE on H_RX of set, into
   the receive buffer of buf, was posted with result rc, posting the
   RECEIVEs that continue it; returns the final code of the last, or
   the code of a post the provider refused. */
static u8 recv_async(nba *set, u8 lsn, cmem_blk *buf, rxmsg *m, u8 rc)
{
    char piece[MSG_MAX];
    u16 len;
    u16 off;
    u16 n;

    rx_start(m);
    while (rc == NB_OK) {
        rc = wait_done(set, H_RX, 0);
        m->pieces++;
        len = nba_len(set, H_RX);
        for (off = 0; off < len; off = (u16)(off + n)) {
            n = (u16)(len - off);
            if (n > (u16)sizeof piece)
                n = (u16)sizeof piece;
            cmem_get(buf, off, piece, n);
            rx_add(m, piece, n);
        }
        if (rc != NB_RC_INCOMPLETE)
            break;
        rc = nba_recv(set, H_RX, lsn, buf, 0, RX_LEN);
    }
    return rc;
}

/* Send reply on session lsn from the reply area of buf through H_TX of
   set and wait for the SEND; returns its return code. */
static u8 send_async(nba *set, u8 lsn, cmem_blk *buf, const char *reply)
{
    u16 len;
    u8 rc;

    len = (u16)strlen(reply);
    cmem_put(buf, TX_OFF, reply, len);
    rc = nba_send(set, H_TX, lsn, buf, TX_OFF, len);
    if (rc == NB_OK)
        rc = wait_done(set, H_TX, 0);
    return rc;
}

/* Serve one session as name with the wait-form commands, listening for
   a call for up to wait_secs seconds, until QUIT; returns the exit
   status. */
static int run_server(const char *name, u16 wait_secs)
{
    rxmsg m;
    char reply[MSG_MAX];
    char *buf;
    const char *reason;
    u32 start;
    u16 left;
    u8 rc;
    u8 lsn;

    buf = (char *)malloc(NB_MSGMAX);
    if (buf == NULL)
        return end_session(0, name, NULL, NULL, k_memory);
    say("LISTEN %s from * for %u s ...\n", name, (unsigned)wait_secs);
    fflush(stdout);
    reason = NULL;
    start = tmr_ticks();
    left = wait_secs;
    do {
        rc = nb_listen_wait(name, k_any, &lsn, SRV_RTO, SRV_STO, left);
        say("LISTEN %s rc=%02X lsn=%02X\n", name, rc, lsn);
    } while (rc == NB_RC_ABORTED &&
             (left = secs_left(start, wait_secs)) != 0);
    if (rc != NB_OK) {
        reason = k_listen;
        if (rc == NB_RC_TIMEOUT || rc == NB_RC_PENDING) {
            say("CANCEL rc=%02X\n", nb_last_cancel_rc());
            reason = rc == NB_RC_TIMEOUT ? k_listento
                                         : "listen timeout and cancel failed";
        }
    }

    while (reason == NULL) {
        rc = recv_wait(lsn, buf, &m);
        answer(&m, rc, reply);
        if (rc != NB_OK) {
            reason = k_receive;
        } else if (reply[0] == '\0') {
            reason = k_unexpected;
        } else {
            rc = nb_send(lsn, reply, (u16)strlen(reply));
            say(k_sendfmt, reply, rc);
            if (rc != NB_OK)
                reason = k_send;
            else if (strcmp(reply, k_bye) == 0)
                break;
        }
    }
    free(buf);
    return end_session(lsn, name, NULL, NULL, reason);
}

/* run_server with the no-wait commands of an nba set.  The RECEIVE of
   the next message is posted before the SEND of a reply, so that both
   are in progress at once. */
static int run_server_async(const char *name, u16 wait_secs)
{
    rxmsg m;
    char reply[MSG_MAX];
    char caller[NB_NAMELEN];
    const char *reason;
    nba *set;
    cmem_blk *buf;
    u32 start;
    u16 left;
    int quit;
    u8 rc;
    u8 tx;
    u8 lsn;

    set = NULL;
    buf = cmem_alloc(BUF_LEN);
    if (buf == NULL || nba_open(&set, H_COUNT) != 0)
        return end_session(0, name, set, buf, k_memory);

    say("LISTEN %s from * for %u s, no-wait ...\n", name,
        (unsigned)wait_secs);
    fflush(stdout);
    reason = NULL;
    lsn = 0;
    start = tmr_ticks();
    left = wait_secs;
    do {
        rc = nba_listen(set, H_RX, name, k_any, SRV_RTO, SRV_STO);
        if (rc == NB_OK)
            rc = wait_done(set, H_RX, secs_ticks(left));
        if (rc == NB_RC_PENDING) {
            rc = nba_cancel(set, H_RX);
            say("CANCEL rc=%02X LISTEN rc=%02X\n", rc, nba_poll(set, H_RX));
            reason = k_listento;
        } else {
            nba_callname(set, H_RX, caller);
            say("LISTEN %s rc=%02X lsn=%02X caller=%s\n",
                name, rc, nba_lsn(set, H_RX), caller);
        }
    } while (reason == NULL && rc == NB_RC_ABORTED &&
             (left = secs_left(start, wait_secs)) != 0);
    if (reason == NULL && rc != NB_OK)
        reason = k_listen;

    if (reason == NULL) {
        lsn = nba_lsn(set, H_RX);
        rc = nba_recv(set, H_RX, lsn, buf, 0, RX_LEN);
    }
    while (reason == NULL) {
        rc = recv_async(set, lsn, buf, &m, rc);
        answer(&m, rc, reply);
        if (rc != NB_OK) {
            reason = k_receive;
        } else if (reply[0] == '\0') {
            reason = k_unexpected;
        } else {
            quit = strcmp(reply, k_bye) == 0;
            if (!quit)
                rc = nba_recv(set, H_RX, lsn, buf, 0, RX_LEN);
            tx = send_async(set, lsn, buf, reply);
            say(k_sendfmt, reply, tx);
            if (tx != NB_OK)
                reason = k_send;
            else if (quit)
                break;
        }
    }
    return end_session(lsn, name, set, buf, reason);
}

/* Send the len bytes at buf as one message on session lsn, receive the
   reply, print both under the name what and check that the reply is
   expect; returns NULL, or the reason of a failure. */
static const char *exchange(u8 lsn, const char *what, const char *buf,
                            u16 len, const char *expect)
{
    char reply[MSG_MAX];
    u8 rc;

    rc = nb_send(lsn, buf, len);
    if (rc != NB_OK) {
        say(k_sendfmt, what, rc);
        return k_send;
    }
    rc = recv_msg(lsn, reply);
    say("%s rc=%02X reply=%s\n", what, rc, reply);
    if (rc != NB_OK)
        return k_receive;
    if (strcmp(reply, expect) != 0)
        return "wrong reply";
    return NULL;
}

/* Call remote as local, retrying for CALL_RETRY_SECS a second apart,
   with the wait-form CALL or, under async, a no-wait one, whose line
   tells whether it was still in progress when the post returned
   (pending=1); exchange count PING/PONG pairs, the bulk message, then
   QUIT and BYE; returns the exit status. */
static int run_client(const char *local, const char *remote, u16 count,
                      int async)
{
    char msg[MSG_MAX];
    char expect[MSG_MAX];
    char *bulk;
    const char *reason;
    nba *set;
    u32 start;
    u32 ticks;
    u16 i;
    u16 attempt;
    int pending;
    u8 rc;
    u8 lsn;

    set = NULL;
    if (async) {
        if (nba_open(&set, H_CALL + 1) != 0)
            return end_session(0, local, NULL, NULL, k_memory);
        say("CALL %s from %s, no-wait ...\n", remote, local);
    }
    pending = 0;
    reason = NULL;
    attempt = 0;
    start = tmr_ticks();
    for (;;) {
        attempt++;
        if (set == NULL) {
            rc = nb_call(local, remote, &lsn, CLI_RTO, CLI_STO);
        } else {
            rc = nba_call(set, H_CALL, local, remote, CLI_RTO, CLI_STO);
            pending = rc == NB_OK &&
                      nba_poll(set, H_CALL) == NB_RC_PENDING;
            if (rc == NB_OK)
                rc = wait_done(set, H_CALL, 0);
            lsn = nba_lsn(set, H_CALL);
        }
        say("CALL %s attempt %u rc=%02X lsn=%02X%s\n", remote,
            (unsigned)attempt, rc, lsn,
            set == NULL ? "" : pending ? " pending=1" : " pending=0");
        fflush(stdout);
        if (rc == NB_OK)
            break;
        if (tmr_since(start) >= secs_ticks(CALL_RETRY_SECS)) {
            reason = "call";
            break;
        }
        tmr_delay(TMR_SEC);
    }

    start = tmr_ticks();
    for (i = 1; reason == NULL && i <= count; i++) {
        sprintf(msg, "PING %u", (unsigned)i);
        sprintf(expect, "PONG %u", (unsigned)i);
        reason = exchange(lsn, msg, msg, (u16)strlen(msg), expect);
    }
    ticks = tmr_since(start);

    if (reason == NULL) {
        say("ELAPSED %ld s for %u round trips\n",
            (long)(ticks * 10UL / TMR_10SEC), (unsigned)count);
        bulk = (char *)malloc(NB_MSGMAX);
        if (bulk == NULL) {
            reason = k_memory;
        } else {
            for (i = 0; i < NB_MSGMAX; i++)
                bulk[i] = bulk_byte(i);
            sprintf(expect, k_bulkfmt, (unsigned)NB_MSGMAX);
            reason = exchange(lsn, "BULK", bulk, NB_MSGMAX, expect);
            free(bulk);
        }
    }
    if (reason == NULL)
        reason = exchange(lsn, k_quit, k_quit,
                          (u16)(sizeof k_quit - 1), k_bye);
    return end_session(lsn, local, set, NULL, reason);
}

/* Write the usage text to f. */
static void usage(FILE *f)
{
    fprintf(f,
            "usage: NBTEST SERVER name [/WAIT:seconds] [/ASYNC] [/EXIT]\n"
            "usage: NBTEST CLIENT localname remotename [/COUNT:n] [/ASYNC] "
            "[/EXIT]\n");
}

/* Take the switch s, its '/' or '-' skipped, into the settings; returns
   0 for a switch that is unknown, repeated or out of range. */
static int take_switch(const char *s)
{
    if (txt_ieq(s, "EXIT") && !nbt_exit)
        return nbt_exit = 1;
    if (txt_ieq(s, "ASYNC") && !nbt_async)
        return nbt_async = 1;
    if (txt_ieqn(s, "WAIT:", 5) && nbt_wait == 0)
        return (nbt_wait = txt_num(s + 5, MAX_WAIT_SECS)) != 0;
    if (txt_ieqn(s, "COUNT:", 6) && nbt_count == 0)
        return (nbt_count = txt_num(s + 6, MAX_COUNT)) != 0;
    return 0;
}

int main(int argc, char **argv)
{
    const char *arg[3];
    const char *a;
    char n1[NB_NAMELEN];
    char n2[NB_NAMELEN];
    int nargs;
    int server;
    int bad;
    int i;
    u8 major;
    u8 minor;

    cmem_init();
    if (!dos_version(&major, &minor)) {
        fprintf(stderr, "NBTEST: requires MS-DOS 3.1 or later\n");
        return EXIT_DOSVER;
    }
    a = argv[1];
    if (argc == 2 && (a[0] == '/' || a[0] == '-') && txt_ieq(a + 1, "?")) {
        usage(stdout);
        return EXIT_PASS;
    }
    nargs = 0;
    bad = 0;
    for (i = 1; i < argc; i++) {
        a = argv[i];
        if (a[0] == '/' || a[0] == '-') {
            bad |= !take_switch(a + 1);
        } else {
            if (nargs < 3)
                arg[nargs] = a;
            nargs++;
        }
    }
    server = nargs == 2 && txt_ieq(arg[0], "SERVER") && nbt_count == 0;
    if (server) {
        copy_name(n1, arg[1]);
        bad |= n1[0] == '\0';
    } else if (nargs == 3 && txt_ieq(arg[0], "CLIENT") && nbt_wait == 0) {
        copy_name(n1, arg[1]);
        copy_name(n2, arg[2]);
        bad |= n1[0] == '\0' || n2[0] == '\0';
    } else {
        bad = 1;
    }
    if (bad) {
        usage(stderr);
        return leave(EXIT_USAGE);
    }
    if (nbt_wait == 0)
        nbt_wait = DEF_WAIT_SECS;
    if (nbt_count == 0)
        nbt_count = DEF_COUNT;
    say("NetBIOS session probe\n");

    if (!nb_present())
        return finish(EXIT_NONB, "netbios not present");
    say("NetBIOS present\n");
    if (add_name(n1) != NB_OK)
        return finish(EXIT_FAIL, "add name");

    if (!server)
        return run_client(n1, n2, nbt_count, nbt_async);
    if (nbt_async)
        return run_server_async(n1, nbt_wait);
    return run_server(n1, nbt_wait);
}
