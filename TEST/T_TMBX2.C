/*
 * T_TMBX2.C - the second part of the T_TMBX test (T_TMBX.H): the
 * phases below, which main of T_TMBX.C runs.
 *
 * Checks:
 * - READONLY= caps the station named by the file;
 * - an empty look is followed by the next only TMBOX_POLL ticks later,
 *   tmbox_poll returning 0 meanwhile; a session's end makes the next
 *   pass look at once;
 * - a .RSP left from before is replaced;
 * - a .RSP that cannot be replaced (a directory of that name) leaves the
 *   .TMP in place, the request gone; that station's next request waits
 *   while another station's is served; once the .RSP can go, the next
 *   look renames the .TMP and serves the request, but not while that
 *   request cannot be read, which keeps it; a .RSP that cannot be
 *   removed, as one a reader holds open (its removal failed once), does
 *   so too, with no failure reported;
 * - a write of the response that fills the disk ends the session, with
 *   no further read of the request, the bytes written so far becoming
 *   the .RSP and the request removed, and is reported on the session's
 *   slot;
 * - a rename that fails is reported on the session's slot, and once more
 *   on slot 0 however many looks try it again, the .TMP kept until it
 *   works;
 * - the request is overwritten with zeros before it is removed: a
 *   removal that keeps failing leaves a request of several pieces, the
 *   last one short, all zeros at its size, and keeps its .TMP back, so
 *   the request is served once whatever the looks; it is reported on
 *   the session's slot and once more on slot 0; while the request
 *   cannot be opened, the .TMP stays back too; once the removal works,
 *   a look puts the .TMP in place; an empty request likewise, and one
 *   whose removal fails with the look for it failing too;
 * - a request that cannot be opened is reported on slot 0 and kept for
 *   a later look, the next station served first; each station's such
 *   failure is reported once however many looks meet it, and again
 *   once the station was served or its request was gone; a .TMP that
 *   cannot be created likewise, the request closed; a read that fails
 *   ends the session, reported;
 * - a shutdown answers the next request 501 into the .RSP, ends the
 *   session and lets no new one open; the last failed LOGIN ends a
 *   session, a BYE ending its request with no further read; tmbox_stop
 *   ends a running session as any end does, the
 *   output CONN still holds written into the .RSP;
 * - on the host, which has no sector slack to look at: the .TMP of a
 *   session that tmbox_stop ends while CONN still holds PING's +OK is
 *   cut back once, after that last write, from the end of its last
 *   DOS_SECTOR, the bytes past the response all zeros, so the .RSP's
 *   last sector holds nothing else.
 */
#define COMPAT_FIND
#define COMPAT_DISK
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "TMBOX.H"
#define TF_PART
#define TF_FILE "T_TMBX2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_TMBX.H"

#define K_NAME          "NAME=\"System Administrator\""  /* ADMIN's    */

static const char k_pong[] = "+OK\r\n";

/* READONLY= by file name. */
void test_readonly(void)
{
    begin("RO1");
    post("RO1", "HELLO X\r\nLOGIN ADMIN " PW "\r\n");
    TF_STREQ(serve("RO1"), "+OK\r\n+OK LEVEL=1 " K_NAME "\r\n");
    post("RW1", "HELLO X\r\nLOGIN ADMIN " PW "\r\n");
    TF_STREQ(serve("RW1"), "+OK\r\n+OK LEVEL=3 " K_NAME "\r\n");
    finish();
}

/* The looks: TMBOX_POLL ticks apart while nothing comes. */
void test_look(void)
{
    u16 i;

    begin(NULL);
    TF_EQ(pass(), 1);
    post("LATE", "PING\r\n");
    for (i = 1; i < TMBOX_POLL; i++)
        TF_EQ(pass(), 0);
    TF_EQ(g_opens, 0);
    TF_CHECK(pass() > 0);
    TF_EQ(g_opens, 1);
    post("NEXT", "PING\r\n");
    TF_STREQ(serve("LATE"), "+OK\r\n");
    TF_EQ(g_opens, 1);
    TF_CHECK(pass() > 0);
    TF_EQ(g_opens, 2);
    TF_STREQ(serve("NEXT"), "+OK\r\n");
    finish();
}

/* Run the server until n sessions have closed in all since begin, or
   LOOPS passes have gone. */
static void until_closes(u16 n)
{
    u16 i;

    for (i = 0; i < LOOPS && g_closes < n; i++)
        pass();
    TF_EQ(g_closes, n);
}

/* 1 when file name of directory dir exists. */
static int there(const char *dir, const char *name)
{
    return tfs_exists(tfs_subpath(dir, name));
}

/* A read hook failing every open, and one failing every read. */
static int fail_open(int op)
{
    return op == CF_OP_OPEN ? CF_ESHARE : 0;
}

static int fail_read(int op)
{
    return op == CF_OP_READ ? CF_EACCES : 0;
}

/* A read hook failing every look for a file. */
static int fail_exists(int op)
{
    return op == CF_OP_EXISTS ? CF_EACCES : 0;
}

/* A read hook counting the reads in g_reads. */
static u16 g_reads;

static int count_reads(int op)
{
    if (op == CF_OP_READ)
        g_reads++;
    return 0;
}

void test_stale(void)
{
    begin(NULL);
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "ST.RSP"), "OLD", 3), 0);
    post("ST", "PING\r\n");
    TF_STREQ(serve("ST"), k_pong);
    finish();
}

void test_held(void)
{
    u16 i;

    begin(NULL);
    TF_EQ(cf_mkdir(tfs_subpath(TMBOX_RSPDIR, "HLD.RSP")), 0);
    post("HLD", "PING\r\n");
    until_closes(1);
    TF_STREQ(text(TMBOX_RSPDIR, "HLD.TMP"), k_pong);
    TF_EQ(there(TMBOX_REQDIR, "HLD.REQ"), 0);
    post("HLD", "HELLO X\r\n");
    post("OTH", "PING\r\n");
    until_closes(2);
    run(3 * TMBOX_POLL);
    TF_STREQ(g_opened, "HLD|OTH|");
    TF_EQ(there(TMBOX_REQDIR, "HLD.REQ"), 1);
    TF_STREQ(text(TMBOX_RSPDIR, "HLD.TMP"), k_pong);
    TF_STREQ(serve("OTH"), k_pong);
    TF_EQ(cf_rmdir(tfs_subpath(TMBOX_RSPDIR, "HLD.RSP")), 0);
    cf_fault_rd = fail_read;
    run(3 * TMBOX_POLL);
    cf_fault_rd = NULL;
    TF_EQ(there(TMBOX_REQDIR, "HLD.REQ"), 1);
    TF_EQ(there(TMBOX_RSPDIR, "HLD.RSP"), 0);
    for (i = 0; i < LOOPS && !there(TMBOX_RSPDIR, "HLD.RSP"); i++)
        pass();
    TF_STREQ(text(TMBOX_RSPDIR, "HLD.RSP"), k_pong);
    TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, "HLD.RSP")), 0);
    TF_STREQ(serve("HLD"), "+OK\r\n");
    TF_STREQ(g_opened, "HLD|OTH|HLD|");
    TF_EQ(there(TMBOX_RSPDIR, "HLD.TMP"), 0);

    /* A .RSP whose removal fails once, as one held open does: not
       reported, and the .TMP renamed by the next look. */
    fails_reset();
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, "ONC.RSP"), "OLD", 3), 0);
    post("ONC", "PING\r\n");
    tfs_fault(TFS_OP(CF_OP_REMOVE), 2, 1, CF_ESHARE);
    until_closes(4);
    TF_EQ(tfs_failop(), CF_OP_REMOVE);
    TF_STREQ(text(TMBOX_RSPDIR, "ONC.TMP"), k_pong);
    TF_STREQ(text(TMBOX_RSPDIR, "ONC.RSP"), "OLD");
    for (i = 0; i < LOOPS && there(TMBOX_RSPDIR, "ONC.TMP"); i++)
        pass();
    TF_STREQ(serve("ONC"), k_pong);
    tfs_fault_off();
    TF_EQ(g_fails, 0);
    finish();
}

void test_full(void)
{
    begin(NULL);
    post("FUL", "PING\r\nPING\r\n");
    cf_fault_part = 3;
    /* Write 1 purges the request; write 2, the response, fills the disk. */
    tfs_fault(TFS_OP(CF_OP_WRITE), 2, 1, 0);
    g_reads = 0;
    cf_fault_rd = count_reads;
    until_closes(1);
    cf_fault_rd = NULL;
    tfs_fault_off();
    cf_fault_part = 0;
    TF_EQ(g_reads, 1);
    TF_EQ(g_reqs, 1);
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 1);
    TF_STREQ(g_fsta, "FUL");
    TF_EQ(g_ferr, CF_EFULL);
    TF_EQ(there(TMBOX_REQDIR, "FUL.REQ"), 0);
    TF_STREQ(serve("FUL"), "+OK");
    finish();
}

void test_rename(void)
{
    u16 i;

    begin(NULL);
    tfs_fault(TFS_OP(CF_OP_RENAME), 1, 0, CF_EACCES);
    post("REN", "PING\r\n");
    until_closes(1);
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 1);
    TF_STREQ(g_fsta, "REN");
    TF_EQ(g_ferr, CF_EACCES);
    TF_EQ(there(TMBOX_RSPDIR, "REN.RSP"), 0);
    TF_STREQ(text(TMBOX_RSPDIR, "REN.TMP"), k_pong);
    for (i = 0; i < LOOPS && g_fails < 2; i++)
        pass();
    run(4 * TMBOX_POLL);
    TF_EQ(g_fails, 2);
    TF_EQ(g_fslot, 0);
    TF_STREQ(g_fsta, "REN");
    TF_CHECK(tfs_count(CF_OP_RENAME) > 3);
    tfs_fault_off();
    TF_STREQ(serve("REN"), k_pong);
    TF_EQ(g_fails, 2);
    TF_EQ(there(TMBOX_RSPDIR, "REN.TMP"), 0);
    finish();
}

void test_zero(void)
{
    static char req[2 * CONN_RXSZ + 16];
    u16 n;

    begin(NULL);
    req[0] = '\0';
    for (n = 0; strlen(req) <= 2 * CONN_RXSZ; n++)
        strcat(req, "PING\r\n");
    TF_CHECK(strlen(req) % CONN_RXSZ != 0);
    post("ZER", req);
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
    until_closes(1);
    run(3 * TMBOX_POLL);
    tfs_fault_off();
    TF_EQ(g_opens, 1);
    TF_EQ(g_fails, 2);
    TF_EQ(g_fslot, 0);
    TF_EQ(g_ferr, CF_EACCES);
    TF_EQ(there(TMBOX_RSPDIR, "ZER.RSP"), 0);
    TF_CHECK(zeros(TMBOX_REQDIR, "ZER.REQ", (u32)strlen(req)));
    cf_fault_rd = fail_open;
    run(3 * TMBOX_POLL);
    cf_fault_rd = NULL;
    TF_EQ(g_fails, 2);
    TF_EQ(there(TMBOX_RSPDIR, "ZER.RSP"), 0);
    TF_EQ(strlen(serve("ZER")), n * (sizeof k_pong - 1));
    TF_EQ(g_opens, 1);

    post("EMP", "");
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
    until_closes(2);
    run(3 * TMBOX_POLL);
    tfs_fault_off();
    TF_EQ(g_opens, 2);
    TF_STREQ(text(TMBOX_RSPDIR, "EMP.TMP"), "");
    TF_EQ(there(TMBOX_RSPDIR, "EMP.RSP"), 0);
    TF_STREQ(serve("EMP"), "");
    TF_EQ(g_opens, 2);

    /* A removal that fails whose look fails too: the request counts as
       there, so the .TMP stays back. */
    post("LKF", "PING\r\n");
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
    cf_fault_rd = fail_exists;
    until_closes(3);
    cf_fault_rd = NULL;
    tfs_fault_off();
    TF_EQ(there(TMBOX_RSPDIR, "LKF.RSP"), 0);
    TF_STREQ(serve("LKF"), k_pong);
    TF_EQ(g_opens, 3);
    finish();
}

void test_badopen(void)
{
    u16 i;

    begin(NULL);
    post("B1", "PING\r\n");
    post("B2", "PING\r\n");
    cf_fault_rd = fail_open;
    pass();
    cf_fault_rd = NULL;
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 0);
    TF_STREQ(g_fsta, "B1");
    TF_EQ(g_ferr, CF_ESHARE);
    TF_EQ(g_opens, 0);
    TF_EQ(there(TMBOX_REQDIR, "B1.REQ"), 1);
    TF_STREQ(serve("B2"), k_pong);
    TF_STREQ(serve("B1"), k_pong);
    TF_STREQ(g_opened, "B2|B1|");

    /* Each station's failure once, while its request is there; again
       once it was served, and once it was gone. */
    fails_reset();
    post("B1", "PING\r\n");
    post("B2", "PING\r\n");
    cf_fault_rd = fail_open;
    run(4 * TMBOX_POLL);
    TF_EQ(g_fails, 2);
    cf_fault_rd = NULL;
    TF_STREQ(serve("B1"), k_pong);
    TF_STREQ(serve("B2"), k_pong);
    post("B1", "PING\r\n");
    cf_fault_rd = fail_open;
    run(2 * TMBOX_POLL);
    TF_EQ(g_fails, 3);
    cf_fault_rd = NULL;
    TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, "B1.REQ")), 0);
    run(2 * TMBOX_POLL);
    post("B1", "PING\r\n");
    cf_fault_rd = fail_open;
    run(2 * TMBOX_POLL);
    TF_EQ(g_fails, 4);
    cf_fault_rd = NULL;
    TF_STREQ(serve("B1"), k_pong);

    fails_reset();
    post("C1", "PING\r\n");
    tfs_fault(TFS_OP(CF_OP_CREATE), 1, 1, CF_EACCES);
    for (i = 0; i < LOOPS && g_fails == 0; i++)
        pass();
    tfs_fault_off();
    TF_EQ(g_fslot, 0);
    TF_STREQ(g_fsta, "C1");
    TF_EQ(g_ferr, CF_EACCES);
    TF_EQ(there(TMBOX_REQDIR, "C1.REQ"), 1);
    TF_STREQ(serve("C1"), k_pong);

    fails_reset();
    post("R1", "PING\r\n");
    cf_fault_rd = fail_read;
    until_closes(7);
    cf_fault_rd = NULL;
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 1);
    TF_STREQ(g_fsta, "R1");
    TF_EQ(g_ferr, CF_EACCES);
    TF_EQ(there(TMBOX_REQDIR, "R1.REQ"), 0);
    TF_STREQ(serve("R1"), "");
    finish();
}

#ifdef COMPAT_HOST
/* What seal_seen saw: the cuts of a file, the size of station SEA's
   .TMP at the last, and the end of its last byte that is not zero. */
static u16 g_cuts;
static u32 g_csize;
static u32 g_cend;

/* A disk hook noting each cut of a file, the .TMP as it is then. */
static int seal_seen(int op)
{
    char buf[TB_ZBLK];
    const char *tmp;
    u32 at;
    u16 n;
    u16 i;

    if (op != CF_OP_TRUNC)
        return 0;
    cf_fault = NULL;
    g_cuts++;
    tmp = tfs_subpath(TMBOX_RSPDIR, "SEA." TMBOX_TMPEXT);
    g_csize = tfs_size(tmp);
    g_cend = 0;
    for (at = 0; at < g_csize; at += n) {
        n = g_csize - at < sizeof buf ? (u16)(g_csize - at) :
            (u16)sizeof buf;
        if (tfs_peek(tmp, at, buf, n) != 0)
            break;
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                g_cend = at + i + 1;
    }
    cf_fault = seal_seen;
    return 0;
}
#endif

void test_ends(void)
{
    u16 i;

    begin(NULL);
    post("SD", "HELLO X\r\nPING\r\nPING\r\n");
    for (i = 0; i < LOOPS && g_reqs < 1; i++)
        pass();
    conn_shutdown(g_now);
    post("SD2", "PING\r\n");
    TF_STREQ(serve("SD"), "+OK\r\n-ERR 501 server shutting down\r\n");
    run(3 * TMBOX_POLL);
    TF_EQ(g_opens, 1);
    TF_EQ(conn_done(), 1);
    TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, "SD2.REQ")), 0);
    finish();

    begin(NULL);
    post("BYE", "HELLO X\r\nBYE\r\n");
    g_reads = 0;
    cf_fault_rd = count_reads;
    until_closes(1);
    cf_fault_rd = NULL;
    TF_EQ(g_reads, 1);
    TF_STREQ(serve("BYE"), "+OK\r\n+OK\r\n");
    post("BAD", "HELLO X\r\nLOGIN ADMIN B1\r\nLOGIN ADMIN B2\r\n"
         "LOGIN ADMIN B3\r\nPING\r\n");
    TF_STREQ(serve("BAD"), "+OK\r\n-ERR 401 bad user name or password\r\n"
             "-ERR 401 bad user name or password\r\n"
             "-ERR 401 bad user name or password\r\n");
    post("STP", "HELLO X\r\nPING\r\nPING\r\n");
    for (i = 0; i < LOOPS && g_reqs < 8; i++)
        pass();
    tmbox_stop();
    TF_EQ(conn_state(0), CONN_FREE);
    TF_EQ(g_closes, 3);
    TF_EQ(there(TMBOX_REQDIR, "STP.REQ"), 0);
    TF_STREQ(text(TMBOX_RSPDIR, "STP.RSP"), "+OK\r\n+OK\r\n");
    TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, "STP.RSP")), 0);
    finish();

#ifdef COMPAT_HOST
    begin(NULL);
    post("SEA", "HELLO X\r\nPING\r\n");
    for (i = 0; i < LOOPS && g_reqs < 2; i++)
        pass();
    g_cuts = 0;
    cf_fault = seal_seen;
    tmbox_stop();
    cf_fault = NULL;
    TF_EQ(g_cuts, 1);
    TF_EQ(g_csize, DOS_SECTOR);
    TF_EQ(g_cend, 2 * (sizeof k_pong - 1));
    TF_EQ(tfs_size(tfs_subpath(TMBOX_RSPDIR, "SEA." TMBOX_RSPEXT)),
          2 * (sizeof k_pong - 1));
    TF_STREQ(serve("SEA"), "+OK\r\n+OK\r\n");
    finish();
#endif
}
