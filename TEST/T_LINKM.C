/*
 * T_LINKM.C - unit test for the MBOX backend of the client's link
 * (LINK.C over LINKMBX.C and MBXCLI.C) against the server's mailbox in
 * the same program: TMBOX, CONN, the store of login tickets and the
 * engine on a scratch mailbox.
 *
 * usage: T_LINKM dir
 *
 * dir is the scratch directory: the data directory of a fresh engine,
 * whose ADMIN must change its password, and the mailbox, whose REQUEST
 * and RESPONSE directories the test makes.  The engine's files and the
 * mailbox are removed at the start and at the end.
 *
 * The client's waits are the server's turns: the test's tmr_delay, in
 * place of CTIMER.C's, keeps the text of the request file posted, then
 * runs the server pass by pass, n ticks of its clock each, until the
 * response file is there.  The test's own malloc and free (MALLOC_TESTS)
 * keep the blocks that opening a link allocates, the backend's, the
 * mailbox client's and the link's, and fail a chosen allocation.
 * Checks:
 * - link_open_mbox refuses a station that is no mailbox station and a
 *   directory too long with LINK_EMBXARG, and gives LINK_ENOMEM when
 *   any of its allocations fails, *out NULL and no block left each
 *   time; it touches no file, and link_netname is NULL;
 * - every request is one request file: HELLO alone; VER after HELLO
 *   and the station; LOGIN with TICKET=NEW; with a ticket HELLO, LOGIN
 *   user TICKET=t, the user as typed, and the request; BYE with
 *   TICKET=END; the client's HELLO followed by the ticket's LOGIN and
 *   BYE TICKET=END; without a ticket no LOGIN line;
 * - a ticket is cut from the +OK delivered, the rest of the line kept;
 *   no delivered line holds one; the next files use the ticket of the
 *   LOGIN, and PASSWD's new one after it; the response files of LOGIN
 *   and PASSWD are zeros when the client removes them, another not;
 * - a ticket expired (IDLE=): PING, VER and BYE are posted again
 *   without it and get the server's own answers, the next request going
 *   without a ticket (400); PASSWD, and USER ADD with PASSWORD=, get
 *   -ERR 400 without being posted again, so their password is in one
 *   request file only; a failed LOGIN, also one sent while a ticket is
 *   held, the client's HELLO and BYE leave no ticket, nor any held by
 *   the store;
 * - a disabled user's ticket (403) is refused alike, -ERR 400 and the
 *   callback's failure on it LINK_EOUTPUT;
 * - BYE ends the link (LINK_CLOSED), later requests post nothing;
 *   link_close of a link that holds a ticket posts the file that ends
 *   it, of one that holds none posts nothing;
 * - a failure before the request is its answer: -ERR 501 when a
 *   shutdown begins after the file's HELLO, which ends the link;
 * - no block of a link holds the password once its request has been
 *   sent, none holds the password or a ticket when it is freed, and the
 *   mailbox is empty at the end.
 *
 * The wipe of the line LINKMBX.C builds on the stack is held by review:
 * its frame is dead once the request is queued, and tmr_delay's own
 * frame overlaps it, so no portable check can see it.
 *
 * On the host only, as only Windows programs link the backend.  Prints
 * each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_FIND
#define LINK_MBOX
#define PROTO_TICKETS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TMBOX.H"
#include "TICKET.H"
#include "LINK.H"
#include "MBXCLI.H"
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"

#define PW              "Secret77"      /* ADMIN's password, once set   */
#define BOBPW           "Bobpw123"      /* the second user's            */
#define NEWPW           "Newpw999"      /* a change that never goes     */
#define LOOPS           64      /* server passes a file may take        */
#define IDLE            (30 * TMR_SEC)  /* the idle limit, in ticks     */
#define OUTSZ           1024    /* bytes of the lines a call delivers   */
#define FILESZ          512     /* bytes of the request files it posts  */
#define NBLK            16      /* blocks of the links the test keeps   */
#define NTKT            16      /* tickets the test keeps               */
#define ZBLK            64      /* bytes of the store checked at a time */

static const char *const k_dirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };
static const char k_tkey[] = "TICKET=";
static const eng_time g_t0 = { 2026, 10, 25, 12, 0, 0 };

static char g_sta[MBXCLI_STALEN + 1];   /* the station of the link used */
static char g_out[OUTSZ];       /* the lines delivered, each and '|'    */
static char g_files[FILESZ];    /* the request files posted, their lines
                                   ended by '|', each file by '#', each
                                   ticket written T                     */
static char g_tkt[NTKT][LINK_TKTLEN + 1];   /* the tickets seen         */
static int g_ntkt;
static int g_last;              /* the ticket of the last file, or -1   */
static u32 g_now;               /* the server's clock                   */
static int g_inserver;          /* the server's pass runs               */
static int g_rspzero;           /* the last .RSP the client removed was
                                   zeros: 1, else 0, -1 before one      */
static u16 g_reqs;              /* CONN_EV_REQ seen                     */
static u16 g_shutat;            /* shut down at that many, 0 never      */
static int g_fail;              /* take reports a failure               */

/* The heap: the blocks recorded while g_rec is set, their sizes, a
   malloc call to fail and the blocks freed holding a secret. */
static void *g_blk[NBLK];
static size_t g_bsz[NBLK];
static int g_nblk;
static int g_rec;
static int g_calls;
static int g_failat;
static int g_leaky;

void *__real_malloc(size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n);
void __wrap_free(void *p);

/* 1 when the n bytes at p hold the text s. */
static int holds(const void *p, size_t n, const char *s)
{
    const char *b;
    size_t k;
    size_t i;

    b = (const char *)p;
    k = strlen(s);
    for (i = 0; i + k <= n; i++)
        if (memcmp(b + i, s, k) == 0)
            return 1;
    return 0;
}

/* 1 when the n bytes at p hold the password or a ticket seen. */
static int secret(const void *p, size_t n)
{
    int i;

    if (holds(p, n, PW) || holds(p, n, BOBPW))
        return 1;
    for (i = 0; i < g_ntkt; i++)
        if (holds(p, n, g_tkt[i]))
            return 1;
    return 0;
}

void *__wrap_malloc(size_t n)
{
    void *p;

    if (++g_calls == g_failat)
        return NULL;
    p = __real_malloc(n);
    if (p != NULL && g_rec && TF_CHECK(g_nblk < NBLK)) {
        g_blk[g_nblk] = p;
        g_bsz[g_nblk++] = n;
    }
    return p;
}

void __wrap_free(void *p)
{
    int i;

    for (i = 0; i < g_nblk; i++)
        if (g_blk[i] == p)
            break;
    if (p != NULL && i < g_nblk) {
        if (secret(p, g_bsz[i]))
            g_leaky++;
        g_blk[i] = g_blk[--g_nblk];
        g_bsz[i] = g_bsz[g_nblk];
    }
    __real_free(p);
}

/* 1 when a block of the links held holds s. */
static int held(const char *s)
{
    int i;

    for (i = 0; i < g_nblk; i++)
        if (holds(g_blk[i], g_bsz[i], s))
            return 1;
    return 0;
}

/* The path of the station's file of extension ext in directory dir. */
static const char *sta_path(const char *dir, const char *ext)
{
    char name[DOS_FINDNAME];

    strcpy(name, g_sta);
    strcat(name, ".");
    strcat(name, ext);
    return tfs_subpath(dir, name);
}

/* 1 when the n bytes at s start with a ticket's digits. */
static int is_ticket(const char *s, u16 n)
{
    u16 i;

    if (n < LINK_TKTLEN)
        return 0;
    for (i = 0; i < LINK_TKTLEN; i++)
        if (txt_hexval(s[i]) < 0)
            return 0;
    return 1;
}

/* The number of the ticket whose digits s starts with in g_tkt, which
   keeps it when it is new; -1 when g_tkt is full. */
static int ticket_no(const char *s)
{
    int k;

    for (k = 0; k < g_ntkt; k++)
        if (memcmp(g_tkt[k], s, LINK_TKTLEN) == 0)
            return k;
    if (!TF_CHECK(g_ntkt < NTKT))
        return -1;
    memcpy(g_tkt[g_ntkt], s, LINK_TKTLEN);
    g_tkt[g_ntkt][LINK_TKTLEN] = '\0';
    return g_ntkt++;
}

/* Append the n bytes of a request file to g_files, each CR LF as '|',
   each ticket after TICKET= as T, g_last its number. */
static void keep_file(const char *buf, u16 n)
{
    size_t at;
    u16 i;

    at = strlen(g_files);
    if (!TF_CHECK(at + n + 2 <= FILESZ))
        return;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\r')
            continue;
        g_files[at++] = buf[i] == '\n' ? '|' : buf[i];
        if (at >= sizeof k_tkey - 1 &&
            memcmp(g_files + at - (sizeof k_tkey - 1), k_tkey,
                   sizeof k_tkey - 1) == 0 &&
            is_ticket(buf + i + 1, (u16)(n - i - 1))) {
            g_last = ticket_no(buf + i + 1);
            g_files[at++] = 'T';
            i = (u16)(i + LINK_TKTLEN);
        }
    }
    g_files[at++] = '#';
    g_files[at] = '\0';
}

/* The disk hook: note whether the station's .RSP, when the client
   removes it, is zeros. */
static int on_disk(int op)
{
    char buf[FILESZ];
    u16 got;
    u16 i;

    if (op != CF_OP_REMOVE || g_inserver)
        return 0;
    cf_fault = NULL;
    if (tfs_exists(sta_path(TMBOX_RSPDIR, TMBOX_RSPEXT)) &&
        TF_EQ(tfs_read_all(sta_path(TMBOX_RSPDIR, TMBOX_RSPEXT), buf,
                           (u16)sizeof buf, &got), 0)) {
        for (i = 0; i < got && buf[i] == 0; i++)
            ;
        g_rspzero = got > 0 && i == got;
    }
    cf_fault = on_disk;
    return 0;
}

static void on_event(u8 c, u8 ev, const char *text)
{
    TF_EQ(c, 0);
    TF_CHECK(!holds(text, strlen(text), PW));
    if (ev == CONN_EV_REQ)
        g_reqs++;
}

/* The wait of COMPAT.H, in place of CTIMER.C's: keep the request file
   posted, then run the server, n ticks a pass, until the response file
   is there; a shutdown begins once g_shutat requests are answered. */
void tmr_delay(u16 n)
{
    char buf[FILESZ];
    u16 got;
    int i;

    cf_fault = NULL;
    if (tfs_exists(sta_path(TMBOX_REQDIR, TMBOX_REQEXT)) &&
        TF_EQ(tfs_read_all(sta_path(TMBOX_REQDIR, TMBOX_REQEXT), buf,
                           (u16)sizeof buf, &got), 0))
        keep_file(buf, got);
    g_inserver = 1;
    for (i = 0; i < LOOPS &&
         !tfs_exists(sta_path(TMBOX_RSPDIR, TMBOX_RSPEXT)); i++) {
        g_now += n;
        tmbox_poll(g_now);
        conn_service(g_now);
        if (g_shutat != 0 && g_reqs >= g_shutat) {
            conn_shutdown(g_now);
            g_shutat = 0;
        }
        conn_timeouts(g_now);
    }
    g_inserver = 0;
    cf_fault = on_disk;
}

/* link_line_fn: keep the line and '|' in g_out; g_fail. */
static int take(void *arg, const char *line, u16 len)
{
    size_t n;

    TF_CHECK(arg == (void *)g_out);
    TF_EQ(strlen(line), len);
    TF_CHECK(!holds(line, len, "TICKET"));
    n = strlen(g_out);
    if (TF_CHECK(n + len + 2 <= sizeof g_out)) {
        memcpy(g_out + n, line, len);
        strcpy(g_out + n + len, "|");
    }
    return g_fail;
}

/* Send line over l from station sta: its result, the lines delivered
   in g_out and the files posted in g_files. */
static int say(link *l, const char *sta, const char *line)
{
    strcpy(g_sta, sta);
    g_out[0] = '\0';
    g_files[0] = '\0';
    g_last = -1;
    g_rspzero = -1;
    return link_send(l, line, (u16)strlen(line), take, g_out);
}

/* Check that line, sent over l from sta, posts the files want and gets
   the answer out (its first bytes when prefix). */
static void check(link *l, const char *sta, const char *line,
                  const char *want, const char *out, int prefix)
{
    TF_EQ(say(l, sta, line), LINK_OK);
    TF_STREQ(g_files, want);
    if (prefix)
        TF_EQ(strncmp(g_out, out, strlen(out)), 0);
    else
        TF_STREQ(g_out, out);
}

/* Log ADMIN in over l from sta with a ticket. */
static void login(link *l, const char *sta)
{
    char want[FILESZ];

    sprintf(want, "HELLO %s|LOGIN ADMIN " PW " TICKET=NEW|#", sta);
    check(l, sta, "LOGIN ADMIN " PW, want, "+OK LEVEL=3 ", 1);
}

/* Open a link from sta, its blocks recorded. */
static link *open_link(const char *sta)
{
    link *l;

    g_rec = 1;
    TF_EQ(link_open_mbox(&l, tfs_dir(), sta), 0);
    g_rec = 0;
    return l;
}

/* Close l, posting the files want. */
static void close_link(link *l, const char *sta, const char *want)
{
    strcpy(g_sta, sta);
    g_files[0] = '\0';
    TF_EQ(link_close(l), 0);
    TF_STREQ(g_files, want);
}

/* 1 when the store holds no ticket: its block all zeros. */
static int store_empty(void)
{
    char buf[ZBLK];
    cmem_blk *b;
    u16 size;
    u16 at;
    u16 n;
    u16 i;

    b = tkt_block();
    if (b == NULL)
        return 0;
    size = cmem_size(b);
    for (at = 0; at < size; at = (u16)(at + n)) {
        n = (u16)(size - at < ZBLK ? size - at : ZBLK);
        cmem_get(b, at, buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* 1 when the station sta has no file left in the mailbox. */
static int mbox_clean(const char *sta)
{
    strcpy(g_sta, sta);
    return !tfs_exists(sta_path(TMBOX_REQDIR, TMBOX_REQEXT)) &&
           !tfs_exists(sta_path(TMBOX_REQDIR, TMBOX_TMPEXT)) &&
           !tfs_exists(sta_path(TMBOX_RSPDIR, TMBOX_RSPEXT)) &&
           !tfs_exists(sta_path(TMBOX_RSPDIR, TMBOX_TMPEXT));
}

static void test_open(void)
{
    char dir[MBXCLI_MAXDIR + 2];
    link *l;
    int rc;
    int k;

    memset(dir, 'D', sizeof dir - 1);
    dir[sizeof dir - 1] = '\0';
    l = (link *)(void *)g_out;
    TF_EQ(link_open_mbox(&l, tfs_dir(), "TOOLONG99"), LINK_EMBXARG);
    TF_CHECK(l == NULL);
    TF_EQ(link_open_mbox(&l, tfs_dir(), "A.B"), LINK_EMBXARG);
    TF_EQ(link_open_mbox(&l, tfs_dir(), "CON"), LINK_EMBXARG);
    TF_EQ(link_open_mbox(&l, tfs_dir(), ""), LINK_EMBXARG);
    TF_EQ(link_open_mbox(&l, dir, "A"), LINK_EMBXARG);
    TF_CHECK(l == NULL);
    g_rec = 1;
    for (k = 1; k < 2 * NBLK; k++) {
        g_calls = 0;
        g_failat = k;
        l = (link *)(void *)g_out;
        rc = link_open_mbox(&l, tfs_dir(), "MBX0");
        if (rc == 0)
            break;
        TF_EQ(rc, LINK_ENOMEM);
        TF_CHECK(l == NULL);
        TF_EQ(g_nblk, 0);
    }
    g_failat = 0;
    g_rec = 0;
    TF_CHECK(k > 3);
    if (!TF_EQ(rc, 0))
        return;
    TF_CHECK(link_netname(l) == NULL);
    TF_CHECK(mbox_clean("MBX0"));
    close_link(l, "MBX0", "");
    TF_EQ(g_nblk, 0);
}

/* A session of ADMIN on MBX1: its forced change, the ticket used,
   expired, ended by a failed LOGIN, and the second user's ticket
   refused once disabled; returns the link, logged in. */
static link *test_session(void)
{
    char ping[OUTSZ];
    link *l;
    link *b;
    int t1;

    l = open_link("MBX1");
    if (l == NULL)
        return NULL;
    check(l, "MBX1", "HELLO MBX1", "HELLO MBX1|#", "+OK|", 0);
    check(l, "MBX1", "VER", "HELLO MBX1|VER|#", "+OK RETROINV ", 1);
    TF_EQ(g_rspzero, 0);
    check(l, "MBX1", "LOGIN ADMIN admin",
          "HELLO MBX1|LOGIN ADMIN admin TICKET=NEW|#",
          "+OK LEVEL=3 MUSTCHG=1|", 0);
    TF_EQ(g_rspzero, 1);
    check(l, "MBX1", "PASSWD admin " PW,
          "HELLO MBX1|LOGIN ADMIN TICKET=T|PASSWD admin " PW "|#",
          "+OK|", 0);
    TF_EQ(g_rspzero, 1);
    TF_CHECK(!held(PW));
    t1 = g_last;
    check(l, "MBX1", "WHOAMI", "HELLO MBX1|LOGIN ADMIN TICKET=T|WHOAMI|#",
          "+DATA|K USER=ADMIN|K LEVEL=3|", 1);
    TF_EQ(g_rspzero, 0);
    TF_CHECK(g_last >= 0 && g_last != t1);
    t1 = g_last;
    check(l, "MBX1", "PING", "HELLO MBX1|LOGIN ADMIN TICKET=T|PING|#", "+OK",
          1);
    TF_EQ(g_last, t1);
    strcpy(ping, g_out);

    /* An expired ticket: PING and VER go again without it. */
    g_now += IDLE + TMR_SEC;
    check(l, "MBX1", "PING",
          "HELLO MBX1|LOGIN ADMIN TICKET=T|PING|#HELLO MBX1|PING|#", ping,
          0);
    check(l, "MBX1", "WHOAMI", "HELLO MBX1|WHOAMI|#", "-ERR 400 ", 1);
    TF_CHECK(store_empty());
    login(l, "MBX1");
    g_now += IDLE + TMR_SEC;
    check(l, "MBX1", "VER",
          "HELLO MBX1|LOGIN ADMIN TICKET=T|VER|#HELLO MBX1|VER|#",
          "+OK RETROINV ", 1);

    /* Any other request gets -ERR 400 without going again, so that a
       password goes in one file only. */
    login(l, "MBX1");
    g_now += IDLE + TMR_SEC;
    check(l, "MBX1", "PASSWD " PW " " NEWPW,
          "HELLO MBX1|LOGIN ADMIN TICKET=T|PASSWD " PW " " NEWPW "|#",
          "-ERR 400|", 0);
    login(l, "MBX1");
    g_now += IDLE + TMR_SEC;
    check(l, "MBX1", "USER ADD BOB LEVEL=1 PASSWORD=" BOBPW,
          "HELLO MBX1|LOGIN ADMIN TICKET=T|USER ADD BOB LEVEL=1 PASSWORD="
          BOBPW "|#", "-ERR 400|", 0);
    TF_CHECK(store_empty());

    /* A failed LOGIN leaves no ticket, not even one held before it; the
       user goes as typed. */
    login(l, "MBX1");
    check(l, "MBX1", "LOGIN ADMIN NOTMYPW",
          "HELLO MBX1|LOGIN ADMIN NOTMYPW TICKET=NEW|#", "-ERR 401 ", 1);
    check(l, "MBX1", "WHOAMI", "HELLO MBX1|WHOAMI|#", "-ERR 400 ", 1);
    check(l, "MBX1", "LOGIN admin " PW,
          "HELLO MBX1|LOGIN admin " PW " TICKET=NEW|#", "+OK LEVEL=3 ", 1);
    TF_EQ(g_rspzero, 1);
    TF_CHECK(!held(PW));
    check(l, "MBX1", "USER ADD BOB LEVEL=1 PASSWORD=" BOBPW,
          "HELLO MBX1|LOGIN admin TICKET=T|USER ADD BOB LEVEL=1 PASSWORD="
          BOBPW "|#", "+OK", 1);

    /* BOB's ticket refused once BOB is disabled. */
    b = open_link("MBX2");
    if (b == NULL)
        return l;
    check(b, "MBX2", "HELLO MBX2", "HELLO MBX2|#", "+OK|", 0);
    check(b, "MBX2", "LOGIN BOB " BOBPW,
          "HELLO MBX2|LOGIN BOB " BOBPW " TICKET=NEW|#",
          "+OK LEVEL=1 MUSTCHG=1|", 0);
    TF_CHECK(!held(BOBPW));
    check(l, "MBX1", "USER SET BOB STATUS=D",
          "HELLO MBX1|LOGIN admin TICKET=T|USER SET BOB STATUS=D|#", "+OK",
          1);
    g_fail = 1;
    TF_EQ(say(b, "MBX2", "WHOAMI"), LINK_EOUTPUT);
    g_fail = 0;
    TF_STREQ(g_files, "HELLO MBX2|LOGIN BOB TICKET=T|WHOAMI|#");
    TF_STREQ(g_out, "-ERR 400|");
    close_link(b, "MBX2", "");
    return l;
}

/* The client's log-out, BYE and the close of a link holding a ticket. */
static void test_end(link *l)
{
    link *c;

    check(l, "MBX1", "HELLO MBX1",
          "HELLO MBX1|LOGIN admin TICKET=T|BYE TICKET=END|#", "+OK|", 0);
    TF_CHECK(store_empty());
    check(l, "MBX1", "WHOAMI", "HELLO MBX1|WHOAMI|#", "-ERR 400 ", 1);
    login(l, "MBX1");
    TF_EQ(say(l, "MBX1", "BYE"), LINK_CLOSED);
    TF_STREQ(g_files, "HELLO MBX1|LOGIN ADMIN TICKET=T|BYE TICKET=END|#");
    TF_STREQ(g_out, "+OK|");
    TF_CHECK(store_empty());
    TF_EQ(say(l, "MBX1", "PING"), LINK_CLOSED);
    TF_STREQ(g_files, "");
    TF_STREQ(g_out, "");
    close_link(l, "MBX1", "");

    c = open_link("MBX3");
    if (c == NULL)
        return;
    check(c, "MBX3", "HELLO MBX3", "HELLO MBX3|#", "+OK|", 0);
    login(c, "MBX3");
    close_link(c, "MBX3", "HELLO MBX3|LOGIN ADMIN TICKET=T|BYE TICKET=END|#");
    TF_CHECK(store_empty());

    /* A BYE whose ticket expired goes again without it. */
    c = open_link("MBX3");
    if (c == NULL)
        return;
    login(c, "MBX3");
    g_now += IDLE + TMR_SEC;
    TF_EQ(say(c, "MBX3", "BYE"), LINK_CLOSED);
    TF_STREQ(g_files, "HELLO MBX3|LOGIN ADMIN TICKET=T|BYE TICKET=END|#"
                      "HELLO MBX3|BYE|#");
    TF_STREQ(g_out, "+OK|");
    close_link(c, "MBX3", "");
    TF_CHECK(store_empty());
    TF_CHECK(mbox_clean("MBX1"));
    TF_CHECK(mbox_clean("MBX2"));
    TF_CHECK(mbox_clean("MBX3"));
    TF_EQ(g_nblk, 0);
    TF_EQ(g_leaky, 0);
}

/* A shutdown begun after the file's HELLO: its 501 is the answer. */
static void test_shutdown(void)
{
    link *l;

    l = open_link("MBX4");
    if (l == NULL)
        return;
    check(l, "MBX4", "HELLO MBX4", "HELLO MBX4|#", "+OK|", 0);
    login(l, "MBX4");
    g_shutat = (u16)(g_reqs + 1);
    TF_EQ(say(l, "MBX4", "PING"), LINK_CLOSED);
    TF_STREQ(g_files, "HELLO MBX4|LOGIN ADMIN TICKET=T|PING|#");
    TF_EQ(strncmp(g_out, "-ERR 501 ", 9), 0);
    close_link(l, "MBX4", "");
    TF_CHECK(mbox_clean("MBX4"));
    TF_EQ(g_nblk, 0);
    TF_EQ(g_leaky, 0);
}

int main(int argc, char **argv)
{
    char why[TFS_WHYSZ];
    proto_tickets *t;
    link *l;
    eng *e;

    cmem_init();
    tf_begin("T_LINKM");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    tfs_dirs(k_dirs, 0);
    if (!TF_EQ(tfs_dirs(k_dirs, 1), 0))
        return tf_done();
    why[0] = '\0';
    if (!TF_EQ(eng_open(&e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return tf_done();
    }
    g_now = 1000;
    if (TF_EQ(conn_init(e, NULL, 1, IDLE, g_now), 0)) {
        t = tkt_open(IDLE, g_now);
        if (TF_CHECK(t != NULL)) {
            conn_tickets(t);
            conn_watch(on_event);
            cf_fault = on_disk;
            if (TF_EQ(tmbox_start(tfs_dir()), TMBOX_OK)) {
                test_open();
                l = test_session();
                if (l != NULL)
                    test_end(l);
                test_shutdown();
                tmbox_stop();
            }
            cf_fault = NULL;
            conn_watch(NULL);
            conn_tickets(NULL);
            tkt_close();
        }
        conn_free();
    }
    TF_EQ(eng_close(e), ENG_OK);
    te_remove(tfs_dir(), NULL);
    tfs_dirs(k_dirs, 0);
    return tf_done();
}
