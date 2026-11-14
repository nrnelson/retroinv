/*
 * T_TMBX3.C - the third part of the T_TMBX test (T_TMBX.H): the purge
 * of a request before its session acts on it, which main of T_TMBX.C
 * runs last.
 *
 * DOS writes a whole disk sector even for a short write, the bytes past
 * it taken from whatever its buffer last held, so the engine's first
 * write after a request was read could carry a sector of the request,
 * and a password in it, to a place on the disk no wipe of the request
 * reaches.  So the request is read whole, overwritten with zeros in the
 * file and committed before CONN is given any of it.  A host file shows
 * no such slack, so the checks pin the calls: the hooks log each call
 * the transport makes during tmbox_poll with the request on disk at
 * that instant (its leading zeros, the rest as posted) and whether
 * CONN's slots (conn_bytes) are as at the read, which they stop being
 * once CONN is given a piece; they log the engine's first disk-changing
 * call, which conn_service makes, and the far blocks the transport
 * takes and gives back.  Every session here runs on slot 0.
 *
 * Checks:
 * - a request of one piece, and one of three pieces with PASSWD lines
 *   past its first CONN_RXSZ bytes: after the request's open and the
 *   .TMP's create, one read, then WRITEs zeroing the request from its
 *   start at its full size and one COMMIT, the slots as at the read
 *   throughout, all before the engine's first call, at which the
 *   request on disk is all zeros; no other read nor COMMIT; at the end,
 *   between the .TMP's close and the request's removal, only the
 *   request's close; the responses as without a purge;
 * - the block: none taken for a request that fits CONN_RXSZ with CR LF
 *   (CONN_RXSZ - 2 bytes), else one of its size and CR LF (CONN_RXSZ - 1
 *   bytes, and the three pieces) before the read, given back all zeros
 *   once the slot is free; a last line without LF is answered; the
 *   transport's own block is all zeros when tmbox_stop frees it;
 * - an empty request: one read, no WRITE nor COMMIT, an empty .RSP;
 * - a request refused, as its block cannot be had, or as it is longer
 *   than CMEM_MAXBLK less CR LF (no block then asked for, even just
 *   after a session with a block of its own): CF_ENOMEM once on the
 *   session's slot, no block given back, nothing handed to CONN, an
 *   empty .RSP, and the request zeroed at its full size and removed in
 *   one pass;
 * - a read, a purge WRITE (the first or a later one) or the COMMIT that
 *   fails: reported once on the session's slot, the slots at the
 *   .TMP's close as at the read, nothing answered, the request all
 *   zeros at its full size when it is removed;
 * - a purged request of three pieces whose removal keeps failing stays
 *   all zeros at its full size, its .TMP held back, and is never served
 *   again; once the removal works the .TMP is put in place.
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
#define TF_PART
#define TF_FILE "T_TMBX3"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_TMBX.H"

#define PW2             "OTHER77"       /* ADMIN's password for a while */
#define PG_CRLF         2       /* the CR LF the transport may add      */
#define PG_BAD          0xFFFFu /* the request: not zeros, then as posted */
#define PG_REQSZ        (2 * CONN_RXSZ + 96)    /* g_req: three pieces  */
#define PG_LINE         64      /* longest padding line                 */
#define PG_LOGN         24      /* entries of the log: a session has 15 */
#define PG_LONG         (CMEM_MAXBLK - PG_CRLF + 1) /* too long a request */
#define PG_WRAP         0x10064UL       /* one whose 16-bit size is short */
#define PG_ERR          CF_EACCES       /* the code of a call made to fail */

/* The log's entries other than the CF_OP_ codes, above them all. */
#define PG_ALLOC        (CF_OP_EXISTS + 1) /* the transport took a far block */
#define PG_FREE         (PG_ALLOC + 1)  /* it gave one back             */
#define PG_ENG          (PG_FREE + 1)   /* the engine's first disk-changing
                                           call                         */

/* Bits of an entry. */
#define PG_SAME         1       /* the slots as at the last read        */
#define PG_RUN          2       /* each call saw the request zero from
                                   its start, a WRITE of a run no less
                                   so than the one before               */
#define PG_CLEAR        4       /* a block given back all zeros         */
#define PG_SFREE        8       /* ... with slot 0 free                 */
#define PG_WHOLE        16      /* at a removal: the request all zeros
                                   at its posted size                   */

typedef struct {
    u8  op;                     /* CF_OP_ code, or PG_ALLOC .. PG_ENG   */
    u8  fl;                     /* PG_ bits                             */
    u8  pass;                   /* the server pass it came in           */
    u16 n;                      /* calls: more for a run of WRITEs      */
    u16 v;                      /* the request's leading zeros (PG_BAD),
                                   after a run its last; a block's size */
} pg_ev;

static char *g_req;             /* PG_REQSZ bytes: the request posted,
                                   or its start                         */
static u16 g_len;               /* the length of g_req posted           */
static u32 g_size;              /* the size of the request posted       */
static u8 *g_slots;             /* the slots at the last read           */
static u16 g_slotsz;            /* their size                           */
static pg_ev *g_log;            /* PG_LOGN entries                      */
static u16 g_nlog;              /* entries logged                       */
static u8 g_pass;               /* passes since watched began           */
static int g_intm;              /* 1 inside tmbox_poll                  */
static int g_engseen;           /* the engine's first call is logged    */
static int g_zall;              /* 0: the request is too long to read
                                   at every call                        */
static int g_failop;            /* the CF_OP_ code to fail, 0 for none  */
static u16 g_failnth;           /* the call of it to fail               */
static u16 g_nth;               /* calls of it so far                   */
static u16 g_failsz;            /* the block size to refuse, 0 for none */
static int g_stop;              /* 1 while tmbox_stop frees its block   */

/* The bytes of the request on disk zero from its start: their number,
   g_len for all, or PG_BAD when the rest is not as posted or the size
   is not g_len. */
static u16 purged(void)
{
    char buf[TB_ZBLK];
    u32 size;
    u16 at;
    u16 z;
    u16 n;
    u16 i;
    int h;

    if (cf_open(tfs_subpath(TMBOX_REQDIR, "PUR." TMBOX_REQEXT),
                CF_READ | CF_DENYNONE, &h) != 0)
        return PG_BAD;
    z = cf_size(h, &size) == 0 && size == g_len ? g_len : PG_BAD;
    for (at = 0; z != PG_BAD && at < g_len; at = (u16)(at + n)) {
        if (cf_read(h, buf, (u16)sizeof buf, &n) != 0 || n == 0) {
            z = PG_BAD;
            break;
        }
        for (i = 0; i < n && z != PG_BAD; i++) {
            if (z == g_len && buf[i] != 0)
                z = (u16)(at + i);
            if (z != g_len && buf[i] != g_req[at + i])
                z = PG_BAD;
        }
    }
    cf_close(h);
    return z;
}

/* Log op with bits fl and value v. */
static void add(int op, int fl, u16 v)
{
    pg_ev *e;

    if (g_nlog == PG_LOGN)
        return;
    e = &g_log[g_nlog++];
    e->op = (u8)op;
    e->fl = (u8)fl;
    e->pass = g_pass;
    e->n = 1;
    e->v = v;
}

/* Log the call op of the transport, or the engine's first, a WRITE
   after a WRITE joining its run. */
static void note(int op)
{
    const void *slots;
    pg_ev *e;
    u16 size;
    u16 z;
    int fl;

    if (!g_intm) {
        if (g_engseen || op == CF_OP_OPEN || op == CF_OP_READ ||
            op == CF_OP_EXISTS)
            return;
        g_engseen = 1;
        op = PG_ENG;
    }
    z = g_zall ? purged() : PG_BAD;
    slots = conn_bytes(&size);
    if (op == CF_OP_READ)
        memcpy(g_slots, slots, g_slotsz);
    fl = memcmp(g_slots, slots, g_slotsz) == 0 ? PG_SAME : 0;
    if (op == CF_OP_REMOVE &&
        zeros(TMBOX_REQDIR, "PUR." TMBOX_REQEXT, g_size))
        fl |= PG_WHOLE;
    if (op == CF_OP_WRITE && g_nlog > 0 &&
        g_log[g_nlog - 1].op == CF_OP_WRITE) {
        e = &g_log[g_nlog - 1];
        if (z != PG_BAD && z >= e->v)
            fl |= PG_RUN;
        e->fl = (u8)(e->fl & fl);
        e->n++;
        e->v = z;
    } else {
        add(op, z != PG_BAD ? fl | PG_RUN : fl, z);
    }
}

/* The disk hooks, for reads and changes alike: note the call with the
   hooks off, and fail the g_failnth-th call of g_failop that the
   transport makes. */
static int on_call(int op)
{
    int fail;

    fail = g_intm && op == g_failop && ++g_nth == g_failnth;
    cf_fault = NULL;
    cf_fault_rd = NULL;
    note(op);
    cf_fault = on_call;
    cf_fault_rd = on_call;
    return fail ? PG_ERR : 0;
}

/* The allocation hook: log the transport's, refusing one of g_failsz
   bytes. */
static int on_alloc(u16 size)
{
    if (!g_intm)
        return 0;
    add(PG_ALLOC, 0, size);
    return size == g_failsz;
}

/* 1 when every byte of b is zero. */
static int blank(cmem_blk *b)
{
    char buf[TB_ZBLK];
    u16 at;
    u16 n;
    u16 i;

    for (at = 0; at < cmem_size(b); at = (u16)(at + n)) {
        n = (u16)(cmem_size(b) - at);
        if (n > sizeof buf)
            n = (u16)sizeof buf;
        cmem_get(b, at, buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* 1 when a run of the bytes of b is PW. */
static int pw_in(cmem_blk *b)
{
    char w[sizeof PW - 1];
    u16 off;

    for (off = 0; off + sizeof w <= cmem_size(b); off++) {
        cmem_get(b, off, w, sizeof w);
        if (memcmp(w, PW, sizeof w) == 0)
            return 1;
    }
    return 0;
}

/* The free hook: log the blocks the transport gives back in a pass;
   while it stops, its own block is all zeros and no block of it or of
   CONN holds the password. */
static void on_free(cmem_blk *b)
{
    if (g_intm) {
        add(PG_FREE, (blank(b) ? PG_CLEAR : 0) |
            (conn_state(0) == CONN_FREE ? PG_SFREE : 0), cmem_size(b));
    } else if (g_stop) {
        TF_CHECK(!pw_in(b));
        if (cmem_size(b) == CONN_RXSZ)
            TF_CHECK(blank(b));
    }
}

/* Run the server, the hooks set and the log cleared, until the session
   of the request posted has closed. */
static void watched(void)
{
    u16 closes;
    u16 i;

    memset(g_log, 0, PG_LOGN * sizeof *g_log);
    g_nlog = 0;
    g_pass = 0;
    g_engseen = 0;
    g_nth = 0;
    closes = g_closes;
    cf_fault = on_call;
    cf_fault_rd = on_call;
    cmem_fault = on_alloc;
    cmem_freeing = on_free;
    for (i = 0; i < LOOPS && g_closes == closes; i++) {
        g_intm = 1;
        tmbox_poll(g_now);
        g_intm = 0;
        conn_service(g_now);
        conn_timeouts(g_now);
        g_now++;
        g_pass++;
    }
    cf_fault = NULL;
    cf_fault_rd = NULL;
    cmem_fault = NULL;
    cmem_freeing = NULL;
    TF_CHECK(g_nlog < PG_LOGN);
}

/* The index of the first entry of op at i or after, g_nlog for none. */
static u16 next(int op, u16 i)
{
    while (i < g_nlog && g_log[i].op != op)
        i++;
    return i;
}

/* The calls of op logged. */
static u16 calls(int op)
{
    u16 n;
    u16 i;

    n = 0;
    for (i = 0; i < g_nlog; i++)
        if (g_log[i].op == op)
            n = (u16)(n + g_log[i].n);
    return n;
}

/* g_req as station PUR's request. */
static void put(void)
{
    g_len = (u16)strlen(g_req);
    g_size = g_len;
    post("PUR", g_req);
}

/* Lengthen g_req to n bytes with lines of spaces; n is its length or 2
   or more bytes beyond it. */
static void pad(u16 n)
{
    u16 at;
    u16 k;

    for (at = (u16)strlen(g_req); at < n; at = (u16)(at + k)) {
        k = n - at > PG_LINE ? PG_LINE : (u16)(n - at);
        if (n - at - k == 1)
            k--;
        memset(g_req + at, ' ', (size_t)(k - 2));
        memcpy(g_req + at + k - 2, "\r\n", 2);
    }
    g_req[at] = '\0';
}

/* g_req: a login as ADMIN, then lines of spaces to n bytes, then
   tail. */
static void login(u16 n, const char *tail)
{
    strcpy(g_req, "HELLO X\r\nLOGIN ADMIN " PW "\r\n");
    pad(n);
    strcat(g_req, tail);
}

/* 1 when s is n lines, each a "+OK" response. */
static int all_ok(const char *s, u16 n)
{
    for (; n > 0; n--) {
        if (strncmp(s, "+OK", 3) != 0 || (s = strchr(s, '\n')) == NULL)
            return 0;
        s++;
    }
    return *s == '\0';
}

/* The session of g_req, which CONN answers with lines "+OK" lines and
   which takes a block of blk bytes (0: the transport's own): purged
   before CONN has any of it, its block as said, nothing reported. */
static void served(u16 lines, u16 blk)
{
    u16 rd;
    u16 cm;
    u16 eng;
    u16 cl;
    u16 i;

    put();
    watched();
    rd = next(CF_OP_READ, next(CF_OP_CREATE, next(CF_OP_OPEN, 0)));
    cm = (u16)(rd + 2);
    eng = next(PG_ENG, 0);
    TF_EQ(calls(CF_OP_READ), 1);
    TF_EQ(calls(CF_OP_COMMIT), 1);
    if (TF_CHECK(cm < eng && eng < g_nlog)) {
        TF_EQ(g_log[rd + 1].op, CF_OP_WRITE);
        TF_EQ(g_log[rd + 1].fl, PG_SAME | PG_RUN);
        TF_EQ(g_log[cm].op, CF_OP_COMMIT);
        TF_EQ(g_log[cm].v, g_len);
        TF_CHECK(g_log[cm].fl & PG_SAME);
        TF_EQ(g_log[eng].v, g_len);
    }
    cl = next(CF_OP_CLOSE, cm);
    if (TF_CHECK(cl + 2 < g_nlog)) {
        TF_EQ(g_log[cl + 1].op, CF_OP_CLOSE);
        TF_EQ(g_log[cl + 2].op, CF_OP_REMOVE);
    }
    TF_EQ(calls(PG_ALLOC), blk != 0);
    TF_EQ(calls(PG_FREE), blk != 0);
    if (blk != 0) {
        i = next(PG_ALLOC, 0);
        TF_CHECK(i < rd);
        TF_EQ(g_log[i].v, blk);
        i = next(PG_FREE, 0);
        TF_EQ(g_log[i].v, blk);
        TF_EQ(g_log[i].fl, PG_CLEAR | PG_SFREE);
    }
    TF_CHECK(all_ok(serve("PUR"), lines));
    TF_EQ(g_fails, 0);
}

/* Requests of one piece and of three, either side of the block's
   bound, and an empty one. */
static void test_served(void)
{
    u16 rm;

    strcpy(g_req, "HELLO X\r\nLOGIN ADMIN " PW "\r\nPASSWD " PW " " PW2
           "\r\nITEM ADD CATEGORY=SYS\r\nPASSWD " PW2 " " PW "\r\nBYE\r\n");
    served(6, 0);

    /* The first PASSWD line across the first piece's end, the second in
       the third piece. */
    login(CONN_RXSZ - 8, "PASSWD " PW " " PW2 "\r\n");
    pad(2 * CONN_RXSZ);
    strcat(g_req, "ITEM ADD CATEGORY=SYS\r\nPASSWD " PW2 " " PW
           "\r\nBYE\r\n");
    served(6, (u16)(strlen(g_req) + PG_CRLF));

    login(CONN_RXSZ - PG_CRLF - 4, "PING");
    served(3, 0);
    login(CONN_RXSZ - PG_CRLF - 3, "PING");
    served(3, CONN_RXSZ + 1);

    g_req[0] = '\0';
    put();
    watched();
    TF_EQ(calls(CF_OP_READ), 1);
    TF_EQ(calls(CF_OP_WRITE), 0);
    TF_EQ(calls(CF_OP_COMMIT), 0);
    TF_EQ(calls(PG_ALLOC), 0);
    rm = next(CF_OP_REMOVE, 0);
    TF_CHECK(next(CF_OP_READ, 0) < rm && rm < g_nlog);
    TF_STREQ(serve("PUR"), "");
    TF_EQ(g_fails, 0);
}

/* The end of a session that failed with err before CONN had any of the
   request: reported once, nothing answered, the request all zeros at
   its full size when removed. */
static void failed(int err)
{
    const void *bytes;
    u16 size;
    u16 rm;

    rm = next(CF_OP_REMOVE, 0);
    if (TF_CHECK(rm < g_nlog))
        TF_CHECK(g_log[rm].fl & PG_WHOLE);
    TF_EQ(g_reqs, 0);
    TF_EQ(g_fails, 1);
    TF_EQ(g_fslot, 1);
    TF_STREQ(g_fsta, "PUR");
    TF_EQ(g_ferr, err);
    TF_STREQ(serve("PUR"), "");
    bytes = conn_bytes(&size);
    TF_CHECK(!holds((const char *)bytes, size, PW));
}

/* The session of a request refused for want of a block: no block
   given back, the request zeroed and removed in the same pass. */
static void refused(void)
{
    u16 rm;

    g_reqs = 0;
    fails_reset();
    watched();
    rm = next(CF_OP_REMOVE, 0);
    if (TF_CHECK(rm >= 2 && rm < g_nlog)) {
        TF_EQ(g_log[rm - 2].op, CF_OP_WRITE);
        TF_EQ(g_log[rm - 2].pass, g_log[rm].pass);
    }
    TF_EQ(calls(PG_FREE), 0);
    failed(CF_ENOMEM);
}

/* Post as PUR's request g_req followed by spaces, n bytes in all. */
static void put_long(u32 n)
{
    char buf[PG_LINE];
    u32 at;
    u16 k;
    int h;
    int rc;

    memset(buf, ' ', sizeof buf);
    g_len = (u16)strlen(g_req);
    g_size = n;
    rc = cf_create(tfs_subpath(TMBOX_REQDIR, "PUR." TMBOX_REQEXT), &h);
    if (rc == 0) {
        rc = cf_write(h, g_req, g_len);
        for (at = g_len; rc == 0 && at < n; at += k) {
            k = n - at < sizeof buf ? (u16)(n - at) : (u16)sizeof buf;
            rc = cf_write(h, buf, k);
        }
        cf_close(h);
    }
    TF_EQ(rc, 0);
}

/* Requests refused: no block of their size to be had, one too long for
   any, and one whose size a 16-bit count would make short. */
static void test_refused(void)
{
    login(CONN_RXSZ + 100, "PING\r\n");
    put();
    g_failsz = (u16)(g_len + PG_CRLF);
    refused();
    g_failsz = 0;
    TF_EQ(calls(PG_ALLOC), 1);

    /* A session with a block of its own just before, which the refusal
       must not touch again. */
    fails_reset();
    login(CONN_RXSZ + 100, "PING\r\n");
    served(3, (u16)(strlen(g_req) + PG_CRLF));
    g_zall = 0;
    login(0, "");
    put_long(PG_LONG);
    refused();
    TF_EQ(calls(PG_ALLOC), 0);
    put_long(PG_WRAP);
    refused();
    TF_EQ(calls(PG_ALLOC), 0);
    g_zall = 1;
}

/* A request whose call n of op fails as the session loads it. */
static void load_fails(int op, u16 n)
{
    u16 cl;

    login(300, "PASSWD " PW " " PW2 "\r\n");
    put();
    g_reqs = 0;
    fails_reset();
    g_failop = op;
    g_failnth = n;
    watched();
    g_failop = 0;
    cl = next(CF_OP_CLOSE, next(CF_OP_READ, 0));
    if (TF_CHECK(cl < g_nlog))
        TF_CHECK(g_log[cl].fl & PG_SAME);
    failed(PG_ERR);
}

/* A purged request whose removal keeps failing. */
static void test_kept(void)
{
    u16 opens;
    u16 closes;
    u16 i;

    login(CONN_RXSZ + 40, "PASSWD " PW " " PW2 "\r\n");
    pad(2 * CONN_RXSZ + 40);
    strcat(g_req, "PASSWD " PW2 " " PW "\r\n");
    put();
    opens = g_opens;
    closes = g_closes;
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, PG_ERR);
    for (i = 0; i < LOOPS && g_closes == closes; i++)
        pass();
    run(3 * TMBOX_POLL);
    tfs_fault_off();
    TF_CHECK(zeros(TMBOX_REQDIR, "PUR." TMBOX_REQEXT, g_len));
    TF_CHECK(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "PUR." TMBOX_TMPEXT)));
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, "PUR." TMBOX_RSPEXT)), 0);
    TF_EQ(g_opens, opens + 1);
    TF_CHECK(all_ok(serve("PUR"), 4));
    TF_EQ(g_opens, opens + 1);
}

void test_purge(void)
{
    begin(NULL);
    conn_bytes(&g_slotsz);
    g_slots = (u8 *)malloc(g_slotsz);
    g_req = (char *)malloc(PG_REQSZ);
    g_log = (pg_ev *)malloc(PG_LOGN * sizeof *g_log);
    g_zall = 1;
    if (TF_CHECK(g_slots != NULL && g_req != NULL && g_log != NULL)) {
        test_served();
        test_refused();
        load_fails(CF_OP_READ, 1);
        load_fails(CF_OP_WRITE, 1);
        load_fails(CF_OP_WRITE, 2);
        load_fails(CF_OP_COMMIT, 1);
        test_kept();
    }
    free(g_slots);
    free(g_req);
    free(g_log);
    g_stop = 1;
    cmem_freeing = on_free;
    finish();
    cmem_freeing = NULL;
    g_stop = 0;
}
