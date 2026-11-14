/*
 * T_TMBX.C - the server's mailbox transport (TMBOX.H) on a scratch
 * mailbox, over the real CONN and engine.
 *
 * usage: T_TMBX dir
 *
 * dir is a data directory prefix ("OBJ/HOST/TMBXT/", "D:\") naming an
 * existing directory that holds nothing but this test's files; the
 * engine's files and the mailbox's directories there are removed at
 * the start and at the end.  This file holds the helpers, main and the
 * phases below; T_TMBX2.C and T_TMBX3.C hold the others (see their
 * headers).
 *
 * Checks:
 * - tmbox_start refuses a dir that is empty, longer than ENG_MAXDIR or
 *   not ended by a separator, and a second start, with TMBOX_EARG; a
 *   dir, a REQUEST\ or a RESPONSE\ that does not exist with
 *   TMBOX_ENODIR; no memory with TMBOX_ENOMEM; each leaving nothing
 *   started; it ends the session of every RESPONSE\*.TMP left by a run
 *   that stopped, at each point a session's end can stop: the request,
 *   if still there, zeroed and removed, and the .TMP put in place as
 *   the .RSP, an old .RSP replaced; a removal that fails is reported on
 *   slot 0 and keeps the .TMP back until a look removes the request,
 *   which is never served, but a missing .RSP whose removal fails is no
 *   failure; tmbox_dir gives the dir, "" when not started, and
 *   tmbox_poll does nothing then;
 * - one request file is one session of kind "MBOX" whose station is
 *   the file's base name: its responses are its .RSP, the .REQ and the
 *   .TMP are gone, the slot is free again, and no byte of the password
 *   it set is left in the slots;
 * - a request longer than CONN_RXSZ, a last line without LF, blank
 *   lines and an over-long line; BYE ends the session and the lines
 *   after it are not answered;
 * - requests are served one session at a time in name order after the
 *   station served last, wrapping round, so a station that posts again
 *   at once waits for the others;
 * - on the host, where a file may have one, a device name's request and
 *   .TMP are left as they are at start and by every look, its request
 *   never opened nor served, while another station is served.
 *
 * Prints a summary line "T_TMBX PASS n"; exit status 0 when every check
 * passes, else 1, 2 when the directory name is too long.
 */
#define COMPAT_FIND
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TMBOX.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "T_TMBX.H"

u32 g_now = 1000;
eng *g_e;
char g_opened[TEXTSZ];
u16 g_opens;
u16 g_reqs;
u16 g_closes;
u16 g_fails;
u8 g_fslot;
char g_fsta[ENG_STALEN + 1];
int g_ferr;

static const eng_time g_t0 = { 2026, 10, 13, 12, 0, 0 };
static const char *const g_dirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };
static u16 g_held;              /* heap blocks held when a phase began:
                                   on the host, far blocks too          */
static int g_hfree;             /* the handle free when a phase began   */

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

static void on_event(u8 c, u8 ev, const char *station)
{
    size_t n;

    TF_CHECK(c < CONN_MAX);
    if (ev == CONN_EV_OPEN) {
        g_opens++;
        n = strlen(g_opened);
        if (n + strlen(station) + 2 <= sizeof g_opened) {
            strcpy(g_opened + n, station);
            strcat(g_opened, "|");
        }
    } else if (ev == CONN_EV_REQ) {
        g_reqs++;
    } else if (ev == CONN_EV_CLOSE) {
        g_closes++;
    }
}

static void on_fail(u8 slot, const char *station, int err)
{
    g_fails++;
    g_fslot = slot;
    strcpy(g_fsta, station);
    g_ferr = err;
}

/* Forget the failures reported. */
void fails_reset(void)
{
    g_fails = 0;
    g_fslot = 0;
    g_fsta[0] = '\0';
    g_ferr = 0;
}

/* The handle the next open gets: no handle is left open while it is
   the same. */
static int free_handle(void)
{
    int h;

    if (cf_open(tfs_path("ITEMS", ".DBF"), CF_READ | CF_DENYNONE, &h) != 0)
        return -1;
    cf_close(h);
    return h;
}

/* A table of CONN_MAX slots capping readonly, the failures watched and
   the transport started on the mailbox. */
void begin(const char *readonly)
{
    g_held = tm_held();
    g_hfree = free_handle();
    TF_EQ(conn_init(g_e, readonly, CONN_MAX, 0, g_now), 0);
    conn_watch(on_event);
    tmbox_watch(on_fail);
    fails_reset();
    g_opened[0] = '\0';
    g_opens = 0;
    g_reqs = 0;
    g_closes = 0;
    TF_EQ(tmbox_start(tfs_dir()), TMBOX_OK);
}

/* Stop the transport, check what it left and free the table. */
void finish(void)
{
    u8 c;

    tmbox_stop();
    TF_STREQ(tmbox_dir(), "");
    TF_EQ(tmbox_poll(g_now), 0);
    tmbox_stop();
    for (c = 0; c < CONN_MAX; c++)
        TF_EQ(conn_state(c), CONN_FREE);
    TF_EQ(g_closes, g_opens);
    tmbox_watch(NULL);
    conn_free();
    TM_HELD(g_held);
    TF_EQ(free_handle(), g_hfree);
}

/* One pass of the server's loop at g_now, the clock then moved on a
   tick; what tmbox_poll returned. */
u16 pass(void)
{
    u16 n;

    n = tmbox_poll(g_now);
    conn_service(g_now);
    conn_timeouts(g_now);
    g_now++;
    return n;
}

void run(u16 n)
{
    while (n-- > 0)
        pass();
}

/* Put the request file of station sta holding text in place. */
void post(const char *sta, const char *text)
{
    char name[DOS_FINDNAME];

    strcpy(name, sta);
    strcat(name, "." TMBOX_REQEXT);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, name), text,
                        (u16)strlen(text)), 0);
}

/* The bytes of file name of directory dir of the mailbox as a string,
   "?" when it cannot be read; in a static buffer. */
const char *text(const char *dir, const char *name)
{
    static char buf[TEXTSZ];
    u16 got;

    if (tfs_read_all(tfs_subpath(dir, name), buf, TEXTSZ - 1, &got) != 0)
        return "?";
    buf[got] = '\0';
    return buf;
}

/* Run the server until station sta's .REQ is gone, its .RSP there and
   no slot in use, and return the .RSP's text, the file removed; "?"
   when that does not come within LOOPS passes. */
const char *serve(const char *sta)
{
    char req[DOS_FINDNAME];
    char rsp[DOS_FINDNAME];
    const char *s;
    u16 i;

    strcpy(req, sta);
    strcat(req, "." TMBOX_REQEXT);
    strcpy(rsp, sta);
    strcat(rsp, "." TMBOX_RSPEXT);
    for (i = 0; i < LOOPS; i++) {
        if (!tfs_exists(tfs_subpath(TMBOX_REQDIR, req)) &&
            tfs_exists(tfs_subpath(TMBOX_RSPDIR, rsp)) &&
            conn_count() == 0)
            break;
        pass();
    }
    if (i == LOOPS)
        return "?";
    s = text(TMBOX_RSPDIR, rsp);
    cf_remove(tfs_subpath(TMBOX_RSPDIR, rsp));
    return s;
}

/* 1 when file name of directory dir of the mailbox holds size bytes,
   every one zero. */
int zeros(const char *dir, const char *name, u32 size)
{
    char buf[TB_ZBLK];
    u32 at;
    u16 n;
    u16 i;

    if (tfs_size(tfs_subpath(dir, name)) != size)
        return 0;
    for (at = 0; at < size; at += n) {
        n = size - at < sizeof buf ? (u16)(size - at) : (u16)sizeof buf;
        if (tfs_peek(tfs_subpath(dir, name), at, buf, n) != 0)
            return 0;
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* 1 when the n bytes at p hold s. */
int holds(const char *p, u16 n, const char *s)
{
    u16 k;
    u16 i;

    k = (u16)strlen(s);
    for (i = 0; i + k <= n; i++)
        if (memcmp(p + i, s, k) == 0)
            return 1;
    return 0;
}

/* ---------------------------------------------------------------------
 * Start-up and service.
 * --------------------------------------------------------------------- */

/* A cmem_fault hook: no memory. */
static int no_mem(u16 size)
{
    return size != 0;
}

void test_start(void)
{
    char dir[DOS_PATHMAX + 1];
    size_t n;

    TF_STREQ(tmbox_dir(), "");
    TF_EQ(tmbox_poll(g_now), 0);
    tmbox_stop();
    TF_EQ(tmbox_start(""), TMBOX_EARG);
    memset(dir, 'D', ENG_MAXDIR);
    dir[ENG_MAXDIR] = DOS_DIRSEP;
    dir[ENG_MAXDIR + 1] = '\0';
    TF_EQ(tmbox_start(dir), TMBOX_EARG);
    strcpy(dir, tfs_path("NODIR", ""));
    TF_EQ(tmbox_start(dir), TMBOX_EARG);
    n = strlen(dir);
    dir[n] = DOS_DIRSEP;
    dir[n + 1] = '\0';
    TF_EQ(tmbox_start(dir), TMBOX_ENODIR);
    cmem_fault = no_mem;
    TF_EQ(tmbox_start(tfs_dir()), TMBOX_ENOMEM);
    cmem_fault = NULL;
    TF_STREQ(tmbox_dir(), "");
    tfs_dirs(g_dirs + 1, 0);
    TF_EQ(tmbox_start(tfs_dir()), TMBOX_ENODIR);
    tfs_dirs(g_dirs, 0);
    tfs_dirs(g_dirs + 1, 1);
    TF_EQ(tmbox_start(tfs_dir()), TMBOX_ENODIR);
    TF_EQ(tfs_dirs(g_dirs, 1), 0);
    TF_STREQ(tmbox_dir(), "");

    /* The leftovers of a run that stopped in a session, before its
       request was removed: the request is zeroed (its removal, failed,
       is reported on slot 0), and the .TMP becomes the .RSP only once
       a look has removed it, with no session. */
    post("OLD1", "PING\r\n");
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD1.TMP"), "A", 1), 0);
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 1, CF_EACCES);
    begin(NULL);
    tfs_fault_off();
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 0);
    TF_STREQ(g_fsta, "OLD1");
    TF_EQ(g_ferr, CF_EACCES);
    TF_STREQ(tmbox_dir(), tfs_dir());
    TF_EQ(tmbox_start(tfs_dir()), TMBOX_EARG);
    TF_CHECK(zeros(TMBOX_REQDIR, "OLD1.REQ", 6));
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "OLD1.RSP")), 0);
    TF_STREQ(serve("OLD1"), "A");
    TF_EQ(g_opens, 0);
    TF_EQ(g_fails, 1);
    finish();

    /* Before the old .RSP was removed, and after: put in place. */
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD2.TMP"), "B", 1), 0);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD2.RSP"), "X", 1), 0);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD3.TMP"), "C", 1), 0);
    begin(NULL);
    TF_EQ(g_fails, 0);
    finish();
    TF_STREQ(serve("OLD2"), "B");
    TF_STREQ(serve("OLD3"), "C");
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "OLD2.TMP")), 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "OLD3.TMP")), 0);

    /* The removal of a missing .RSP refused with a code other than not
       found, as DOSBox-X may: the .TMP is put in place all the same. */
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "OLD4.TMP"), "D", 1), 0);
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 1, CF_EACCES);
    begin(NULL);
    tfs_fault_off();
    TF_EQ(tfs_failop(), CF_OP_REMOVE);
    TF_EQ(g_fails, 0);
    finish();
    TF_STREQ(serve("OLD4"), "D");
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "OLD4.TMP")), 0);
}

/* One session: its station, kind and files, and the password gone. */
void test_one(void)
{
    const void *bytes;
    proto_peer p;
    u16 size;
    u16 i;

    begin(NULL);
    post("ONE", "HELLO X\r\nLOGIN ADMIN admin\r\nPASSWD admin " PW "\r\n");
    for (i = 0; i < LOOPS && g_opens == 0; i++)
        pass();
    TF_EQ(conn_info(0, &p), 0);
    TF_STREQ(p.kind, "MBOX");
    TF_STREQ(p.station, "ONE");
    TF_STREQ(serve("ONE"), "+OK\r\n+OK LEVEL=3 MUSTCHG=1\r\n+OK\r\n");
    TF_STREQ(g_opened, "ONE|");
    TF_EQ(g_reqs, 3);
    TF_EQ(g_closes, 1);
    TF_EQ(g_fails, 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, "ONE.REQ")), 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "ONE.TMP")), 0);
    bytes = conn_bytes(&size);
    TF_CHECK(!holds((const char *)bytes, size, PW));
    TF_CHECK(!holds((const char *)bytes, size, "ADMIN"));
    TF_CHECK(!holds((const char *)bytes, size, "admin"));
    finish();
}

/* The lines of a request. */
void test_lines(void)
{
    static char req[CONN_RXSZ + PROTO_MAXLINE + 16];
    static char want[TEXTSZ];
    const char *s;
    u16 n;

    begin(NULL);
    strcpy(req, "HELLO X\r\n\r\n   \r\n");
    strcpy(want, "+OK\r\n");
    for (n = 0; strlen(req) < CONN_RXSZ; n++) {
        strcat(req, "PING\r\n");
        strcat(want, "+OK\r\n");
    }
    n = (u16)strlen(req);
    memset(req + n, 'L', PROTO_MAXLINE + 1);
    strcpy(req + n + PROTO_MAXLINE + 1, "\r\nPING");
    strcat(want, "-ERR 103 line too long\r\n+OK\r\n");
    post("LINES", req);
    s = serve("LINES");
    TF_STREQ(s, want);
    post("BYE", "HELLO X\r\nBYE\r\nPING\r\n");
    TF_STREQ(serve("BYE"), "+OK\r\n+OK\r\n");
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, "BYE.REQ")), 0);
    post("NOTHING", "");
    TF_STREQ(serve("NOTHING"), "");
    TF_STREQ(g_opened, "LINES|BYE|NOTHING|");
    finish();
}

/* Name order after the station served last, wrapping round. */
void test_order(void)
{
    u16 i;

    begin(NULL);
    post("C", "PING\r\n");
    post("B", "PING\r\n");
    for (i = 0; i < LOOPS && g_closes < 1; i++)
        pass();
    TF_STREQ(g_opened, "B|");
    post("B", "PING\r\n");
    post("A", "PING\r\n");
    for (i = 0; i < LOOPS && g_closes < 4; i++)
        pass();
    TF_STREQ(g_opened, "B|C|A|B|");
    TF_EQ(g_closes, 4);
    cf_remove(tfs_subpath(TMBOX_RSPDIR, "A.RSP"));
    cf_remove(tfs_subpath(TMBOX_RSPDIR, "B.RSP"));
    cf_remove(tfs_subpath(TMBOX_RSPDIR, "C.RSP"));
    finish();
}

#ifdef COMPAT_HOST
/* A read hook counting the opens in g_fopens. */
static u16 g_fopens;

static int count_opens(int op)
{
    if (op == CF_OP_OPEN)
        g_fopens++;
    return 0;
}

/* Device names, which only the host lets a file take, each with a
   request and a .TMP: neither touched at start nor by the looks, the
   request never opened, and none served, while another station is. */
void test_device(void)
{
    static const char *const reqs[] = { "AUX.REQ", "CON.REQ" };
    static const char *const tmps[] = { "AUX.TMP", "CON.TMP" };
    int i;

    for (i = 0; i < 2; i++) {
        TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, reqs[i]),
                            "PING\r\n", 6), 0);
        TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, tmps[i]), "A", 1), 0);
    }
    begin(NULL);
    g_fopens = 0;
    cf_fault_rd = count_opens;
    run(2 * TMBOX_POLL);
    cf_fault_rd = NULL;
    TF_EQ(g_fopens, 0);
    post("D", "PING\r\n");
    TF_STREQ(serve("D"), "+OK\r\n");
    run(2 * TMBOX_POLL);
    TF_STREQ(g_opened, "D|");
    TF_EQ(g_fails, 0);
    finish();
    for (i = 0; i < 2; i++) {
        TF_STREQ(text(TMBOX_REQDIR, reqs[i]), "PING\r\n");
        TF_STREQ(text(TMBOX_RSPDIR, tmps[i]), "A");
        TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, reqs[i])), 0);
        TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, tmps[i])), 0);
    }
}
#endif

int main(int argc, char **argv)
{
    char why[TFS_WHYSZ];

    cmem_init();
    tf_begin("T_TMBX");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), NULL);
    tfs_dirs(g_dirs, 0);
    why[0] = '\0';
    if (!TF_EQ(tfs_dirs(g_dirs, 1), 0) ||
        !TF_EQ(eng_open(&g_e, tfs_dir(), &g_t0, why, (u16)sizeof why),
               ENG_OK)) {
        printf("eng_open: %s\n", why);
        return tf_done();
    }
    test_start();
    test_one();
    test_lines();
    test_order();
#ifdef COMPAT_HOST
    test_device();
#endif
    test_readonly();
    test_look();
    test_stale();
    test_held();
    test_full();
    test_rename();
    test_zero();
    test_badopen();
    test_ends();
    test_purge();
    eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    tfs_dirs(g_dirs, 0);
    return tf_done();
}
