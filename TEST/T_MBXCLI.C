/*
 * T_MBXCLI.C - unit test for the mailbox client (MBXCLI.H), the test
 * playing the server on the files.
 *
 * usage: T_MBXCLI dir
 *
 * dir is the scratch directory, which holds the mailbox: the test makes
 * its REQUEST and RESPONSE directories at the start and removes them,
 * with their files, at the start and at the end.
 *
 * Checks:
 * - mbxcli_open refuses, with *out NULL, an empty station, one longer
 *   than MBXCLI_STALEN or holding another character than a letter, a
 *   digit, '-' or '_' or naming a DOS device (COM5 is none), and a dir
 *   longer than MBXCLI_MAXDIR with its separator; it upper-cases the
 *   station, adds the separator, touches no file, and gives
 *   MBXCLI_ENOMEM when either block cannot be had;
 * - mbxcli_queue refuses a line that would pass MBXCLI_QMAX, queueing
 *   nothing; a post writes the queue, CR LF after each line, as
 *   REQUEST\<station>.REQ, with no .TMP left and a .RSP left from
 *   before removed; a later post sends only the lines queued since;
 * - a post whose response has not come gives MBXCLI_EWAIT after
 *   waitsecs + 1 looks; lines queued then, or while the .REQ is still
 *   there, are refused with MBXCLI_EBUSY at once, with no create and no
 *   wait, and stay queued; a post with
 *   nothing queued or due is MBXCLI_OK at once;
 * - a response file is read and removed; its responses are delivered
 *   in order with RESPRD.H's framing: a single line, a data response up
 *   to its "." with dot-stuffed lines as they are, a line longer than
 *   MBXCLI_MAXRESP in pieces (one ended by a bare LF too), a last piece
 *   "." of a data line ending nothing; then MBXCLI_ENONE; a callback
 *   failure gives MBXCLI_EOUTPUT and drops the rest of that response
 *   only; a file cut short gives MBXCLI_ECUT, then MBXCLI_ENONE: a last
 *   line ending in CR without LF is not delivered, one longer than
 *   MBXCLI_MAXRESP only its whole pieces, a data response keeps the
 *   lines delivered before the end; a new response file
 *   replaces the responses not yet delivered, those the reader holds
 *   too;
 * - a response file of MBXCLI_RSPMAX bytes is read whole; a longer one
 *   gives MBXCLI_EFULL and is removed;
 * - mbxcli_ioerr names the first call that failed in the last attempt
 *   or look of a post that gave MBXCLI_EIO, by the word of MBXCLI_STEPS
 *   (from MBXCLI_SFIND, 0, to MBXCLI_SREMOVE, 9; the longest word
 *   MBXCLI_STEPLEN characters), and its CF_E code: a
 *   removal of a .RSP left from before, the create, write, close and
 *   rename of the request, each failing twice, the open, read, close
 *   and removal of the response; a write or read whose close fails too
 *   is named, not the close; a failure of an earlier attempt or look
 *   is not;
 * - an attempt that fails with nothing of the request in REQUEST\ (a
 *   removal of an old .RSP, a create, write or close, a rename with the
 *   .TMP still there) is made once more, TMR_SEC ticks later, with
 *   waitsecs 0 too: failing once, the request is posted once, with no
 *   .TMP left and the queue zeroed; failing twice gives MBXCLI_EIO with
 *   no third attempt and no wait for a response, no .TMP or .REQ left
 *   and the lines still queued; a .REQ another program made meanwhile
 *   gives MBXCLI_EBUSY and is left alone; every .TMP is all zeros when
 *   it is removed;
 * - the calls a redirector repeats after a reconnect, reported failed
 *   but done with CF_ENOENT or another code: a rename, its .TMP gone, is
 *   a post (one create, the .REQ holding the queue, the response then
 *   read), also when the server has already taken the .REQ; a removal
 *   of the .RSP, the file gone, is a response read; a removal of a .RSP
 *   left from before, the file gone, is no failure (no retry), and
 *   one failing every time leaves it there, zeroed;
 * - a look that opens the .RSP but fails to read, close or remove it,
 *   or whose open fails with another code than CF_ENOENT, keeps the
 *   .RSP and the wait goes on, a look each TMR_SEC: failing once the
 *   response is read on the next look, delivered once; failing on all
 *   waitsecs + 1 looks gives MBXCLI_EIO, the .RSP unchanged, nothing
 *   delivered, the bytes read zeroed, and the next post reads it; a
 *   last look that finds no file gives MBXCLI_EWAIT;
 * - a FIND-REQ that cannot tell the .REQ there or gone (cf_exists
 *   negative) fails an attempt with nothing in REQUEST\: failing once
 *   the request is posted on the retry, failing twice gives MBXCLI_EIO
 *   with FIND-REQ and the code, no create and the lines still queued;
 *   a failed rename whose .TMP cannot be looked for gives MBXCLI_EIO
 *   with RENAME-TMP and the rename's code at once, with no retry, which
 *   could post the request twice (also when the rename was done and the
 *   server took the .REQ), no .TMP left, each removed all zeros, and the
 *   lines still queued;
 * - a removal of a .RSP left from before, and of the response read,
 *   that fails while the look for the file fails too counts as not
 *   done: CLEAR-RSP with nothing posted, REMOVE-RSP;
 * - a .TMP a client that stopped left is zeroed and removed before the
 *   next post makes it anew;
 * - a response file asked to be zeroed (mbxcli_zero_next) is all zeros
 *   when it is removed, and the next one is not; one whose removal
 *   fails stands as read, its responses delivered, and the next post
 *   removes it, all zeros also when its zeroing failed too; the request
 *   carries over a post that fails;
 * - the queue holds only zeros once posted, and both blocks are zeros
 *   when mbxcli_close frees them (the free hook cmem_freeing).
 *
 * The test runs on a virtual clock of its own, which only tmr_delay
 * moves on.  cf_size has no hook, so a cf_size failure (READ-RSP, the
 * .RSP kept) is not reached.
 *
 * Prints each failure and a summary line; exit status 0 when every
 * check passes, 1 when one fails, 2 when the directory name is too
 * long.
 */
#define COMPAT_FIND
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TMBOX.H"
#include "MBXCLI.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define STA             "STA-1_"        /* the station, as opened       */
#define GOTSZ           1200    /* bytes of the lines a callback keeps  */
#define BIGLEN          (MBXCLI_MAXRESP + 100)  /* a line in two pieces */
#define FULLBLK         512     /* bytes test_full writes at a time     */
#define ZBLK            64      /* bytes checked for zeros at a time    */
#define QLEN            16      /* "LOGIN ADMIN PW" and CR LF           */
#define EDEV            55      /* DOS error 55, a call on a handle of a
                                   network session that is gone         */
#define NPLAN           (CF_OP_EXISTS + 1) /* the ops the hooks plan    */

static const char *const k_dirs[] = { TMBOX_REQDIR, TMBOX_RSPDIR, NULL };
static const char k_req[] = "STA-1_.REQ";
static const char k_tmp[] = "STA-1_.TMP";
static const char k_rsp[] = "STA-1_.RSP";
static const char k_foreign[] = "PING\r\n";  /* another program's .REQ */
/* MBXCLI_STEPS written out, the step names it is checked against. */
static const char k_steps[] = "FIND-REQ CLEAR-RSP CREATE-TMP WRITE-TMP "
                              "CLOSE-TMP RENAME-TMP OPEN-RSP READ-RSP "
                              "CLOSE-RSP REMOVE-RSP";

/* What the hooks do to the calls of one op, numbered from 1 in n: those
   at to at + count - 1 (count 0: at and every later one) fail with
   code, and with done set a rename or removal is carried out first, as
   a call a redirector repeats is. */
typedef struct {
    u16  at;
    u16  count;
    u16  n;
    int  code;
    u8   done;
} plan;

static char g_got[GOTSZ];       /* the lines delivered, each and '|'    */
static int g_failat;            /* the line the callback fails, from 1  */
static int g_lines;             /* lines delivered                      */
static int g_opens;             /* opens the read hook saw              */
static plan g_plan[NPLAN];      /* the hooks' plan of each op           */
static cf_fault_fn g_rec;       /* the fault recorder, which counts     */
static u8 g_tmponly;            /* number only the writes and close after
                                   a create                             */
static u8 g_made;               /* a create not yet closed              */
static u8 g_foreign;            /* a failed write makes a foreign .REQ  */
static u8 g_taken;              /* a done rename's .REQ is then served  */
static u8 g_vanish;             /* a failed read removes the .RSP       */
static u8 g_watch;              /* check each removal's .TMP is zeros   */
static int g_unzeroed;          /* .TMPs removed holding other bytes    */
static int g_rspzero;           /* removals that found the .RSP zeros   */
static int g_rspdirty;          /* and holding other bytes              */
static u32 g_clock;             /* the virtual clock                    */
static u32 g_madeat[2];         /* g_clock at the first two creates     */
static int g_allocs;            /* the allocation cmem_fault fails      */
static int g_freed;             /* blocks the free hook saw             */
static int g_dirty;             /* of them, blocks not all zeros        */

/* The line callback: keep line and '|' in g_got, fail line g_failat. */
static int keep(void *arg, const char *line, u16 len)
{
    size_t n;

    TF_CHECK(arg == (void *)g_got);
    TF_EQ(strlen(line), len);
    n = strlen(g_got);
    if (n + len + 2 <= sizeof g_got) {
        memcpy(g_got + n, line, len);
        strcpy(g_got + n + len, "|");
    }
    return ++g_lines == g_failat;
}

/* The wait of COMPAT.H on the test's virtual clock, in place of
   CTIMER.C's: it moves g_clock on and returns at once, so a post's
   waits cost no time and can be told. */
void tmr_delay(u16 n)
{
    g_clock += n;
}

/* 1 when the next call of plan p is one to fail. */
static int chosen(plan *p)
{
    p->n++;
    return p->code != 0 && p->n >= p->at &&
           (p->count == 0 || p->n - p->at < p->count);
}

/* 1 when file name of directory dir is missing or holds only zeros. */
static int all_zero(const char *dir, const char *name)
{
    char buf[ZBLK];
    u16 got;
    u16 i;

    if (!tfs_exists(tfs_subpath(dir, name)))
        return 1;
    if (tfs_read_all(tfs_subpath(dir, name), buf, ZBLK, &got) != 0)
        return 0;
    for (i = 0; i < got; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
}

static int reads(int op);

/* The disk hook: op counted by the recorder, then its plan carried out
   with the hooks off. */
static int disk(int op)
{
    int rc;

    rc = (*g_rec)(op);
    if (op == CF_OP_CREATE) {
        if (tfs_count(op) <= 2)
            g_madeat[tfs_count(op) - 1] = g_clock;
        g_made = 1;
    }
    if ((op == CF_OP_WRITE || op == CF_OP_CLOSE) && g_tmponly && !g_made)
        return rc;
    if (op == CF_OP_CLOSE)
        g_made = 0;
    cf_fault = NULL;
    cf_fault_rd = NULL;
    if (op == CF_OP_REMOVE && g_watch && !all_zero(TMBOX_REQDIR, k_tmp))
        g_unzeroed++;
    if (op == CF_OP_REMOVE && tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp))) {
        if (all_zero(TMBOX_RSPDIR, k_rsp))
            g_rspzero++;
        else
            g_rspdirty++;
    }
    if (chosen(&g_plan[op])) {
        rc = g_plan[op].code;
        if (g_plan[op].done && op == CF_OP_RENAME)
            TF_EQ(cf_rename(tfs_subpath(TMBOX_REQDIR, k_tmp),
                            tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        if (g_plan[op].done && op == CF_OP_RENAME && g_taken)
            TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        if (g_plan[op].done && op == CF_OP_REMOVE)
            TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
        if (g_foreign && op == CF_OP_WRITE)
            TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, k_req), k_foreign,
                                (u16)strlen(k_foreign)), 0);
    }
    cf_fault = disk;
    cf_fault_rd = reads;
    return rc;
}

/* The read hook: count opens, carry out the plan. */
static int reads(int op)
{
    if (op == CF_OP_OPEN)
        g_opens++;
    if (!chosen(&g_plan[op]))
        return 0;
    if (g_vanish) {
        cf_fault = NULL;
        TF_EQ(cf_remove(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
        cf_fault = disk;
    }
    return g_plan[op].code;
}

/* Plan the calls at to at + count - 1 of op to fail with code. */
static void fail_op(int op, u16 at, u16 count, int code)
{
    g_plan[op].at = at;
    g_plan[op].count = count;
    g_plan[op].code = code;
}

/* Arm both hooks with the plans made, counts cleared. */
static void arm(void)
{
    tfs_fault(TFS_ALLOPS | TFS_OP(CF_OP_CLOSE), 0, 0, 0);
    g_rec = cf_fault;
    cf_fault = disk;
    cf_fault_rd = reads;
    g_opens = 0;
    g_made = 0;
    g_unzeroed = 0;
    g_rspzero = 0;
    g_rspdirty = 0;
}

/* Disarm both hooks and drop the plans; the recorder's counts stay. */
static void disarm(void)
{
    tfs_fault_off();
    cf_fault_rd = NULL;
    memset(g_plan, 0, sizeof g_plan);
    g_tmponly = 0;
    g_foreign = 0;
    g_taken = 0;
    g_vanish = 0;
    g_watch = 0;
}

/* The step of m's last failed call when its code is err, else -2. */
static int step_of(mbxcli *m, int err)
{
    int step;
    int code;

    mbxcli_ioerr(m, &step, &code);
    return code == err ? step : -2;
}

/* Check that m's last failed call is the step named name, code err. */
#define IOERR(m, name, err) \
    TF_EQ(step_of(m, err), txt_word_num(k_steps, name))

/* The allocation hook: fail allocation g_allocs (from 1). */
static int fail_alloc(u16 size)
{
    return size != 0 && --g_allocs == 0;
}

/* 1 when the first size bytes of block b are zero. */
static int blk_zero(cmem_blk *b, u16 size)
{
    char buf[ZBLK];
    u16 at;
    u16 n;
    u16 i;

    for (at = 0; at < size; at = (u16)(at + n)) {
        n = (u16)(size - at < ZBLK ? size - at : ZBLK);
        cmem_get(b, at, buf, n);
        for (i = 0; i < n; i++)
            if (buf[i] != 0)
                return 0;
    }
    return 1;
}

/* 1 when the file at path holds size bytes, every one zero. */
static int zeros(const char *path, u32 size)
{
    char buf[ZBLK];
    u16 got;
    u16 i;

    if (tfs_size(path) != size || size > ZBLK ||
        tfs_read_all(path, buf, ZBLK, &got) != 0)
        return 0;
    for (i = 0; i < got; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
}

/* The next response of m into g_got: its code. */
static int next(mbxcli *m)
{
    g_got[0] = '\0';
    g_lines = 0;
    return mbxcli_response(m, keep, g_got);
}

/* The bytes of file name of directory dir as a string, "?" when it
   cannot be read; in a static buffer. */
static const char *text(const char *dir, const char *name)
{
    static char buf[GOTSZ];
    u16 got;

    if (tfs_read_all(tfs_subpath(dir, name), buf, GOTSZ - 1, &got) != 0)
        return "?";
    buf[got] = '\0';
    return buf;
}

/* Play the server: write the response file holding s. */
static void answer(const char *s)
{
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_RSPDIR, k_rsp), s,
                        (u16)strlen(s)), 0);
}

/* Play the server taking the request: its bytes must be want. */
static void take(const char *want)
{
    TF_STREQ(text(TMBOX_REQDIR, k_req), want);
    TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
}

/* Queue the text line s on m. */
static int queue(mbxcli *m, const char *s)
{
    return mbxcli_queue(m, s, (u16)strlen(s));
}

static void test_open(void)
{
    char dir[MBXCLI_MAXDIR + 2];
    mbxcli *m;

    memset(dir, 'D', sizeof dir - 1);
    dir[sizeof dir - 1] = '\0';
    m = (mbxcli *)(void *)g_got;
    TF_EQ(mbxcli_open(&m, tfs_dir(), ""), MBXCLI_EARG);
    TF_CHECK(m == NULL);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "ABCDEFGHI"), MBXCLI_EARG);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "A.B"), MBXCLI_EARG);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "A B"), MBXCLI_EARG);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "con"), MBXCLI_EARG);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "LPT1"), MBXCLI_EARG);
    TF_EQ(mbxcli_open(&m, tfs_dir(), "CLOCK$"), MBXCLI_EARG);
    if (TF_EQ(mbxcli_open(&m, tfs_dir(), "COM5"), MBXCLI_OK))
        mbxcli_close(m);
    TF_EQ(mbxcli_open(&m, dir, "A"), MBXCLI_EARG);
    dir[MBXCLI_MAXDIR] = '\0';
    TF_EQ(mbxcli_open(&m, dir, "A"), MBXCLI_EARG);
    dir[MBXCLI_MAXDIR - 1] = DOS_DIRSEP;
    if (TF_EQ(mbxcli_open(&m, dir, "ABCDEFGH"), MBXCLI_OK))
        mbxcli_close(m);
    dir[MBXCLI_MAXDIR - 1] = '\0';
    if (TF_EQ(mbxcli_open(&m, dir, "A"), MBXCLI_OK))
        mbxcli_close(m);
    mbxcli_close(NULL);
    cmem_fault = fail_alloc;
    g_allocs = 1;
    TF_EQ(mbxcli_open(&m, tfs_dir(), "A"), MBXCLI_ENOMEM);
    TF_CHECK(m == NULL);
    g_allocs = 2;
    TF_EQ(mbxcli_open(&m, tfs_dir(), "A"), MBXCLI_ENOMEM);
    TF_CHECK(m == NULL);
    cmem_fault = NULL;
}

/* Posts, waits and refusals, the test taking each request. */
static void test_post(void)
{
    char line[MBXCLI_QMAX];
    mbxcli *m;
    u32 t0;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), "sta-1_"), MBXCLI_OK))
        return;
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
    answer("+OK OLD\r\n");
    TF_EQ(queue(m, "HELLO X"), MBXCLI_OK);
    TF_EQ(queue(m, "LOGIN ADMIN PW"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_tmp)), 0);
    arm();
    t0 = g_clock;
    TF_EQ(mbxcli_post(m, 3), MBXCLI_EWAIT);
    disarm();
    TF_EQ(g_opens, 4);
    TF_EQ(g_clock - t0, 3 * TMR_SEC);
    TF_EQ(next(m), MBXCLI_ENONE);
    take("HELLO X\r\nLOGIN ADMIN PW\r\n");
    answer("+OK\r\n+OK LEVEL=3\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK LEVEL=3|");
    TF_EQ(next(m), MBXCLI_ENONE);

    /* A .REQ still there holds the next post. */
    TF_EQ(queue(m, "PING"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    answer("+OK PONG\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(queue(m, "BYE"), MBXCLI_OK);
    arm();
    t0 = g_clock;
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EBUSY);
    disarm();
    TF_EQ(g_clock, t0);
    TF_EQ(tfs_count(CF_OP_CREATE), 0);
    take("PING\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("BYE\r\n");
    answer("+OK BYE\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK BYE|");

    /* The queue's limit. */
    memset(line, 'Q', sizeof line);
    TF_EQ(mbxcli_queue(m, line, MBXCLI_QMAX - 1), MBXCLI_EFULL);
    TF_EQ(mbxcli_queue(m, line, MBXCLI_QMAX - 2), MBXCLI_OK);
    TF_EQ(mbxcli_queue(m, line, 0), MBXCLI_EFULL);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_EQ(tfs_size(tfs_subpath(TMBOX_REQDIR, k_req)), MBXCLI_QMAX);
    TF_EQ(tfs_peek(tfs_subpath(TMBOX_REQDIR, k_req), MBXCLI_QMAX - 3,
                   line, 3), 0);
    TF_CHECK(memcmp(line, "Q\r\n", 3) == 0);
    TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, k_req)), 0);

    /* Lines queued while the response is due are refused. */
    TF_EQ(queue(m, "X"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EBUSY);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
    mbxcli_close(m);
}

/* The framing of the responses of one file. */
static void test_frame(void)
{
    static char big[BIGLEN + 3];
    static char dat[2 * MBXCLI_MAXRESP + 24];
    mbxcli *m;
    u16 n;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    TF_EQ(queue(m, "X"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("X\r\n");
    answer("+OK\r\n+DATA\r\nA\r\n..B\r\n\r\n.\r\n-401 NO\r\n"
           "+DATA\r\nC\r\n.\r\n+OK END\r");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+DATA|A|..B||.|");
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "-401 NO|");
    g_failat = 1;
    TF_EQ(next(m), MBXCLI_EOUTPUT);
    TF_STREQ(g_got, "+DATA|");
    g_failat = 0;
    TF_EQ(next(m), MBXCLI_ECUT);
    TF_STREQ(g_got, "");
    TF_EQ(next(m), MBXCLI_ENONE);

    /* A long line in pieces, then a data response cut short. */
    memset(big, 'L', BIGLEN);
    strcpy(big + BIGLEN, "\r\n");
    TF_EQ(queue(m, "Y"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("Y\r\n");
    answer(big);
    TF_EQ(tfs_poke(tfs_subpath(TMBOX_RSPDIR, k_rsp), BIGLEN + 2,
                   "+DATA\r\nD\r\n", 10), 0);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_EQ(g_lines, 2);
    TF_EQ(strlen(g_got), BIGLEN + 2);
    TF_EQ(g_got[MBXCLI_MAXRESP], '|');
    TF_EQ(next(m), MBXCLI_ECUT);
    TF_STREQ(g_got, "+DATA|D|");
    TF_EQ(next(m), MBXCLI_ENONE);

    /* A long last line cut short delivers its whole pieces only. */
    big[BIGLEN] = '\0';
    TF_EQ(queue(m, "U"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("U\r\n");
    answer(big);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_ECUT);
    TF_EQ(g_lines, 1);
    TF_EQ(strlen(g_got), MBXCLI_MAXRESP + 1);
    TF_EQ(next(m), MBXCLI_ENONE);

    /* In a data response, a piece "." of a long line ends nothing, and a
       line of MBXCLI_MAXRESP + 1 bytes and a bare LF goes in two. */
    strcpy(dat, "+DATA\r\n");
    n = (u16)strlen(dat);
    memset(dat + n, 'X', MBXCLI_MAXRESP);
    strcpy(dat + n + MBXCLI_MAXRESP, ".\r\n");
    n = (u16)strlen(dat);
    memset(dat + n, 'Y', MBXCLI_MAXRESP + 1);
    strcpy(dat + n + MBXCLI_MAXRESP + 1, "\n.\r\n+OK\r\n");
    TF_EQ(queue(m, "V"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("V\r\n");
    answer(dat);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_EQ(g_lines, 6);
    TF_EQ(strlen(g_got), 2 * MBXCLI_MAXRESP + 14);
    TF_CHECK(strncmp(g_got + 6 + MBXCLI_MAXRESP, "|.|Y", 4) == 0);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");

    /* A line of MBXCLI_MAXRESP bytes is one piece; a new file replaces
       the responses not delivered. */
    big[MBXCLI_MAXRESP] = '\0';
    strcat(big, "\r\n+OK\r\n");
    TF_EQ(queue(m, "Z"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("Z\r\n");
    answer(big);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_EQ(g_lines, 1);
    TF_EQ(strlen(g_got), MBXCLI_MAXRESP + 1);
    TF_EQ(queue(m, "W"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("W\r\n");
    answer("+OK NEW\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK NEW|");
    TF_EQ(next(m), MBXCLI_ENONE);
    mbxcli_close(m);
}

/* Post line on m and play the server answering with a response file of
   size bytes, the first of them s, the last LF. */
static void answer_size(mbxcli *m, const char *line, const char *s,
                        u32 size)
{
    static char buf[FULLBLK];
    u32 left;
    u16 n;
    int h;

    TF_EQ(queue(m, line), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_EQ(cf_remove(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
    memset(buf, 'F', sizeof buf);
    if (!TF_EQ(cf_create(tfs_subpath(TMBOX_RSPDIR, k_rsp), &h), 0))
        return;
    TF_EQ(cf_write(h, s, (u16)strlen(s)), 0);
    for (left = size - (u32)strlen(s) - 1; left > 0; left -= n) {
        n = left < sizeof buf ? (u16)left : (u16)sizeof buf;
        TF_EQ(cf_write(h, buf, n), 0);
    }
    TF_EQ(cf_write(h, "\n", 1), 0);
    TF_EQ(cf_close(h), 0);
}

/* A response file of MBXCLI_RSPMAX bytes, and one longer. */
static void test_full(void)
{
    mbxcli *m;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    answer_size(m, "X", "+OK\r\n", MBXCLI_RSPMAX);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");
    TF_EQ(next(m), MBXCLI_OK);
    TF_EQ(g_lines, (MBXCLI_RSPMAX - 6 + MBXCLI_MAXRESP - 1) /
          MBXCLI_MAXRESP);
    answer_size(m, "Y", "+OK\r\n", MBXCLI_RSPMAX + 1UL);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EFULL);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    TF_EQ(next(m), MBXCLI_ENONE);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    mbxcli_close(m);
}

/* A step of a post failing: op failing with code (op2 too, with
   CF_EFULL, when not 0) in the attempts (look 0) or the looks. */
typedef struct {
    u8   op;
    u8   op2;
    u8   look;
    int  code;
    const char *step;
} stepcase;

static const stepcase k_stepcases[] = {
    { CF_OP_REMOVE, 0, 0, CF_EACCES, "CLEAR-RSP" },
    { CF_OP_CREATE, 0, 0, CF_ENOPATH, "CREATE-TMP" },
    { CF_OP_WRITE, 0, 0, EDEV, "WRITE-TMP" },
    { CF_OP_CLOSE, 0, 0, CF_EBADF, "CLOSE-TMP" },
    { CF_OP_WRITE, CF_OP_CLOSE, 0, EDEV, "WRITE-TMP" },
    { CF_OP_RENAME, 0, 0, CF_EACCES, "RENAME-TMP" },
    { CF_OP_OPEN, 0, 1, CF_ESHARE, "OPEN-RSP" },
    { CF_OP_READ, 0, 1, EDEV, "READ-RSP" },
    { CF_OP_CLOSE, 0, 1, CF_EBADF, "CLOSE-RSP" },
    { CF_OP_READ, CF_OP_CLOSE, 1, EDEV, "READ-RSP" },
    { CF_OP_REMOVE, 0, 1, CF_EACCES, "REMOVE-RSP" }
};

/* Each step failing, named by mbxcli_ioerr; then a failure of an
   earlier attempt, and of an earlier look, not named. */
static void test_steps(void)
{
    char word[MBXCLI_STEPLEN + 1];
    char want[MBXCLI_STEPLEN + 2];
    const stepcase *c;
    mbxcli *m;
    size_t longest;
    int i;

    TF_EQ(MBXCLI_SFIND, 0);
    TF_EQ(MBXCLI_SREMOVE, 9);
    TF_STREQ(MBXCLI_STEPS, k_steps);
    longest = 0;
    for (i = 0; i <= MBXCLI_SREMOVE; i++) {
        txt_word_at(MBXCLI_STEPS, (u16)i, word, (u16)sizeof word);
        txt_word_at(k_steps, (u16)i, want, (u16)sizeof want);
        TF_STREQ(word, want);
        if (strlen(want) > longest)
            longest = strlen(want);
    }
    TF_EQ(longest, MBXCLI_STEPLEN);
    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    for (c = k_stepcases;
         c < k_stepcases + sizeof k_stepcases / sizeof k_stepcases[0]; c++) {
        if (c->look) {
            TF_EQ(queue(m, "L"), MBXCLI_OK);
            TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
            take("L\r\n");
            answer("+OK L\r\n");
        } else {
            TF_EQ(queue(m, "E"), MBXCLI_OK);
            answer("+OK OLD\r\n");
        }
        fail_op(c->op, 1, (u16)(c->look ? 0 : 2), c->code);
        if (c->op2 != 0)
            fail_op(c->op2, 1, (u16)(c->look ? 0 : 2), CF_EFULL);
        g_tmponly = (u8)!c->look;
        arm();
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EIO);
        disarm();
        IOERR(m, c->step, c->code);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_tmp)), 0);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        if (c->look) {
            TF_STREQ(text(TMBOX_RSPDIR, k_rsp), "+OK L\r\n");
            TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
            TF_EQ(next(m), MBXCLI_OK);
            TF_STREQ(g_got, "+OK L|");
        } else {
            TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
            take("E\r\n");
            answer("+OK E\r\n");
            TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
        }
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    }

    /* The first attempt fails at CLEAR-RSP, the second at CREATE-TMP. */
    TF_EQ(queue(m, "E"), MBXCLI_OK);
    answer("+OK OLD\r\n");
    fail_op(CF_OP_REMOVE, 1, 1, CF_EACCES);
    fail_op(CF_OP_CREATE, 1, 1, CF_ENOPATH);
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EIO);
    disarm();
    IOERR(m, "CREATE-TMP", CF_ENOPATH);

    /* The first look fails at OPEN-RSP, the second at READ-RSP. */
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("E\r\n");
    answer("+OK E\r\n");
    fail_op(CF_OP_OPEN, 1, 1, CF_ESHARE);
    fail_op(CF_OP_READ, 1, 0, EDEV);
    arm();
    TF_EQ(mbxcli_post(m, 1), MBXCLI_EIO);
    disarm();
    TF_EQ(g_opens, 2);
    IOERR(m, "READ-RSP", EDEV);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    mbxcli_close(m);
}

/* Post the line s on m, failing as planned with only the .TMP's writes
   and close numbered and every removal's .TMP checked; returns the
   result. */
static int post_failing(mbxcli *m, const char *s)
{
    int rc;

    TF_EQ(queue(m, s), MBXCLI_OK);
    g_tmponly = 1;
    g_watch = 1;
    arm();
    rc = mbxcli_post(m, 0);
    disarm();
    TF_EQ(g_unzeroed, 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_tmp)), 0);
    return rc;
}

/* Play the server answering the request want, and read that answer. */
static void settle(mbxcli *m, const char *want)
{
    take(want);
    answer("+OK\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");
}

/* An attempt failing once, then twice, at the create, write, close and
   rename (failing without being done) of the request. */
static void test_retry(void)
{
    static const u8 ops[] = {
        CF_OP_CREATE, CF_OP_WRITE, CF_OP_CLOSE, CF_OP_RENAME
    };
    static const char *const steps[] = {
        "CREATE-TMP", "WRITE-TMP", "CLOSE-TMP", "RENAME-TMP"
    };
    cmem_blk *q;
    cmem_blk *r;
    mbxcli *m;
    u32 t0;
    int i;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    mbxcli_blocks(m, &q, &r);
    for (i = 0; i < (int)sizeof ops; i++) {
        fail_op(ops[i], 1, 1, EDEV);
        t0 = g_clock;
        TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EWAIT);
        TF_EQ(g_clock - t0, TMR_SEC);
        TF_EQ(g_madeat[1] - g_madeat[0], TMR_SEC);
        TF_EQ(tfs_count(CF_OP_CREATE), 2);
        TF_EQ(tfs_count(CF_OP_RENAME), ops[i] == CF_OP_RENAME ? 2 : 1);
        TF_CHECK(blk_zero(q, QLEN));
        settle(m, "LOGIN ADMIN PW\r\n");

        fail_op(ops[i], 1, 2, EDEV);
        t0 = g_clock;
        TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EIO);
        TF_EQ(g_clock - t0, TMR_SEC);
        TF_EQ(tfs_count(CF_OP_CREATE), 2);
        IOERR(m, steps[i], EDEV);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        TF_CHECK(!blk_zero(q, QLEN));
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
        settle(m, "LOGIN ADMIN PW\r\n");
    }

    /* The removal of a .RSP left from before fails once: the second
       attempt removes it. */
    answer("+OK OLD\r\n");
    fail_op(CF_OP_REMOVE, 1, 1, CF_EACCES);
    TF_EQ(post_failing(m, "C"), MBXCLI_EWAIT);
    TF_EQ(tfs_count(CF_OP_CREATE), 1);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    settle(m, "C\r\n");

    /* That removal reported failed but done, with another code than
       CF_ENOENT, is no failure; one that fails every time is CLEAR-RSP
       after two attempts, the old .RSP still there, zeroed. */
    answer("+OK OLD\r\n");
    fail_op(CF_OP_REMOVE, 1, 1, EDEV);
    g_plan[CF_OP_REMOVE].done = 1;
    t0 = g_clock;
    TF_EQ(post_failing(m, "C"), MBXCLI_EWAIT);
    TF_EQ(g_clock, t0);
    TF_EQ(tfs_count(CF_OP_CREATE), 1);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    settle(m, "C\r\n");
    answer("+OK OLD\r\n");
    fail_op(CF_OP_REMOVE, 1, 0, CF_EACCES);
    TF_EQ(post_failing(m, "C"), MBXCLI_EIO);
    IOERR(m, "CLEAR-RSP", CF_EACCES);
    TF_EQ(tfs_count(CF_OP_REMOVE), 2);
    TF_CHECK(zeros(tfs_subpath(TMBOX_RSPDIR, k_rsp), 9));
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    settle(m, "C\r\n");

    /* Another program's .REQ there by the second attempt holds it. */
    fail_op(CF_OP_WRITE, 1, 1, EDEV);
    g_foreign = 1;
    TF_EQ(post_failing(m, "C"), MBXCLI_EBUSY);
    TF_EQ(tfs_count(CF_OP_CREATE), 1);
    take(k_foreign);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    settle(m, "C\r\n");

    /* A .TMP left as by a client that stopped before its rename, holding
       a password, is zeroed before the next post makes it anew: with
       every create and every removal after the first failing, it stays
       all zeros. */
    TF_EQ(tfs_write_all(tfs_subpath(TMBOX_REQDIR, k_tmp),
                        "LOGIN ADMIN PW\r\n", QLEN), 0);
    TF_EQ(queue(m, "LOGIN ADMIN PW"), MBXCLI_OK);
    fail_op(CF_OP_REMOVE, 2, 0, CF_EACCES);
    fail_op(CF_OP_CREATE, 1, 0, CF_EACCES);
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EIO);
    disarm();
    TF_CHECK(zeros(tfs_subpath(TMBOX_REQDIR, k_tmp), QLEN));
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_tmp)), 0);
    settle(m, "LOGIN ADMIN PW\r\n");
    mbxcli_close(m);
}

/* The rename and the removal of the .RSP reported failed but done, with
   CF_ENOENT and with another code; the rename also with the .REQ taken
   by the server before its answer comes, as in a reconnect. */
static void test_redone(void)
{
    static const int codes[] = { CF_ENOENT, EDEV };
    cmem_blk *q;
    cmem_blk *r;
    mbxcli *m;
    int i;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    mbxcli_blocks(m, &q, &r);
    for (i = 0; i < 4; i++) {
        TF_EQ(queue(m, "LOGIN ADMIN PW"), MBXCLI_OK);
        fail_op(CF_OP_RENAME, 1, 1, codes[i & 1]);
        g_plan[CF_OP_RENAME].done = 1;
        g_taken = (u8)(i >> 1);
        arm();
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
        disarm();
        TF_EQ(tfs_count(CF_OP_CREATE), 1);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_tmp)), 0);
        TF_CHECK(blk_zero(q, QLEN));
        if (i >> 1)
            TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        else
            take("LOGIN ADMIN PW\r\n");
        answer("+OK ONE\r\n+OK TWO\r\n");
        fail_op(CF_OP_REMOVE, 1, 1, codes[i & 1]);
        g_plan[CF_OP_REMOVE].done = 1;
        arm();
        TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
        disarm();
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK ONE|");
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK TWO|");
        TF_EQ(next(m), MBXCLI_ENONE);
        TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    }
    mbxcli_close(m);
}

/* FIND-REQ unable to look for the .REQ, once and then twice; then a
   failed rename whose .TMP cannot be looked for, not done, and done
   with the .REQ taken by the server.  An attempt makes cf_exists calls
   for the .REQ, then for the old .RSP its removal did not find, then
   for the .TMP after a failed rename. */
static void test_unknown(void)
{
    cmem_blk *q;
    cmem_blk *r;
    mbxcli *m;
    u32 t0;
    int i;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    mbxcli_blocks(m, &q, &r);
    fail_op(CF_OP_EXISTS, 1, 1, CF_EACCES);
    t0 = g_clock;
    TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EWAIT);
    TF_EQ(g_clock - t0, TMR_SEC);
    TF_EQ(tfs_count(CF_OP_CREATE), 1);
    TF_CHECK(blk_zero(q, QLEN));
    settle(m, "LOGIN ADMIN PW\r\n");

    fail_op(CF_OP_EXISTS, 1, 2, CF_EACCES);
    t0 = g_clock;
    TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EIO);
    TF_EQ(g_clock - t0, TMR_SEC);
    TF_EQ(tfs_count(CF_OP_CREATE), 0);
    IOERR(m, "FIND-REQ", CF_EACCES);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
    TF_CHECK(!blk_zero(q, QLEN));
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    settle(m, "LOGIN ADMIN PW\r\n");

    for (i = 0; i < 2; i++) {
        fail_op(CF_OP_RENAME, 1, 1, EDEV);
        g_plan[CF_OP_RENAME].done = (u8)i;
        g_taken = (u8)i;
        fail_op(CF_OP_EXISTS, 3, 1, CF_EACCES);
        t0 = g_clock;
        TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EIO);
        TF_EQ(g_clock, t0);
        TF_EQ(tfs_count(CF_OP_CREATE), 1);
        IOERR(m, "RENAME-TMP", EDEV);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
        TF_CHECK(!blk_zero(q, QLEN));
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
        settle(m, "LOGIN ADMIN PW\r\n");
    }
    mbxcli_close(m);
}

/* Removals of the .RSP that fail with the look after them failing too
   (cf_exists negative): the file counts as there, for the .RSP left
   from before and for the response read alike. */
static void test_gone(void)
{
    mbxcli *m;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    answer("OLD");
    fail_op(CF_OP_REMOVE, 1, 0, EDEV);
    fail_op(CF_OP_EXISTS, 2, 1, CF_EACCES);
    TF_EQ(post_failing(m, "LOGIN ADMIN PW"), MBXCLI_EIO);
    IOERR(m, "CLEAR-RSP", EDEV);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_REQDIR, k_req)), 0);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    settle(m, "LOGIN ADMIN PW\r\n");

    TF_EQ(queue(m, "D"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("D\r\n");
    answer("+OK\r\n");
    fail_op(CF_OP_REMOVE, 1, 0, EDEV);
    fail_op(CF_OP_EXISTS, 1, 0, CF_EACCES);
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EIO);
    disarm();
    IOERR(m, "REMOVE-RSP", EDEV);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK|");
    mbxcli_close(m);
}

/* Looks failing at the open, read, close and removal of the .RSP: once,
   then on every look. */
static void test_looks(void)
{
    static const u8 ops[] = {
        CF_OP_OPEN, CF_OP_READ, CF_OP_CLOSE, CF_OP_REMOVE
    };
    static const char *const steps[] = {
        "OPEN-RSP", "READ-RSP", "CLOSE-RSP", "REMOVE-RSP"
    };
    static const char k_two[] = "+OK A\r\n+OK B\r\n";
    cmem_blk *q;
    cmem_blk *r;
    mbxcli *m;
    u32 t0;
    int i;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    mbxcli_blocks(m, &q, &r);
    for (i = 0; i < (int)sizeof ops; i++) {
        TF_EQ(queue(m, "D"), MBXCLI_OK);
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
        take("D\r\n");
        answer(k_two);
        fail_op(ops[i], 1, 1, ops[i] == CF_OP_OPEN ? CF_ESHARE : EDEV);
        arm();
        t0 = g_clock;
        TF_EQ(mbxcli_post(m, 2), MBXCLI_OK);
        disarm();
        TF_EQ(g_opens, 2);
        TF_EQ(g_clock - t0, TMR_SEC);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK A|");
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK B|");
        TF_EQ(next(m), MBXCLI_ENONE);

        TF_EQ(queue(m, "D"), MBXCLI_OK);
        TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
        take("D\r\n");
        answer(k_two);
        fail_op(ops[i], 1, 0, ops[i] == CF_OP_OPEN ? CF_ESHARE : EDEV);
        arm();
        t0 = g_clock;
        TF_EQ(mbxcli_post(m, 1), MBXCLI_EIO);
        disarm();
        TF_EQ(g_opens, 2);
        TF_EQ(g_clock - t0, TMR_SEC);
        IOERR(m, steps[i], ops[i] == CF_OP_OPEN ? CF_ESHARE : EDEV);
        TF_STREQ(text(TMBOX_RSPDIR, k_rsp), k_two);
        TF_EQ(next(m), MBXCLI_ENONE);
        if (ops[i] != CF_OP_OPEN)
            TF_CHECK(blk_zero(r, (u16)strlen(k_two)));
        TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
        TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK A|");
        TF_EQ(next(m), MBXCLI_OK);
        TF_STREQ(g_got, "+OK B|");
        TF_EQ(next(m), MBXCLI_ENONE);
    }

    /* A failed look, then a last look that finds no file. */
    TF_EQ(queue(m, "D"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("D\r\n");
    answer(k_two);
    fail_op(CF_OP_READ, 1, 1, EDEV);
    g_vanish = 1;
    arm();
    TF_EQ(mbxcli_post(m, 1), MBXCLI_EWAIT);
    disarm();
    TF_EQ(g_opens, 2);
    answer(k_two);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(next(m), MBXCLI_OK);
    mbxcli_close(m);
}

/* Response files asked to be zeroed. */
static void test_zero(void)
{
    mbxcli *m;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    TF_EQ(queue(m, "LOGIN A B TICKET=NEW"), MBXCLI_OK);
    mbxcli_zero_next(m);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("LOGIN A B TICKET=NEW\r\n");
    answer("+OK TICKET=SECRET\r\n");
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    disarm();
    TF_EQ(g_rspzero, 1);
    TF_EQ(g_rspdirty, 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK TICKET=SECRET|");

    /* The next response file is not zeroed. */
    TF_EQ(queue(m, "PING"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("PING\r\n");
    answer("+OK PONG\r\n");
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    disarm();
    TF_EQ(g_rspzero, 0);
    TF_EQ(g_rspdirty, 1);

    /* A zeroed file whose removal fails stands as read; the next post
       removes it. */
    TF_EQ(queue(m, "X"), MBXCLI_OK);
    mbxcli_zero_next(m);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("X\r\n");
    answer("+OK X\r\n");
    fail_op(CF_OP_REMOVE, 1, 0, CF_EACCES);
    arm();
    TF_EQ(mbxcli_post(m, 3), MBXCLI_OK);
    disarm();
    TF_EQ(g_opens, 1);
    TF_CHECK(zeros(tfs_subpath(TMBOX_RSPDIR, k_rsp), 7));
    TF_EQ(next(m), MBXCLI_OK);
    TF_STREQ(g_got, "+OK X|");
    TF_EQ(queue(m, "Y"), MBXCLI_OK);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    settle(m, "Y\r\n");

    /* One whose zeroing fails too is zeroed by the next post before it
       is removed. */
    TF_EQ(queue(m, "V"), MBXCLI_OK);
    mbxcli_zero_next(m);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("V\r\n");
    answer("+OK V\r\n");
    fail_op(CF_OP_WRITE, 1, 0, CF_EACCES);
    fail_op(CF_OP_REMOVE, 1, 0, CF_EACCES);
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    disarm();
    TF_STREQ(text(TMBOX_RSPDIR, k_rsp), "+OK V\r\n");
    TF_EQ(next(m), MBXCLI_OK);
    TF_EQ(queue(m, "U"), MBXCLI_OK);
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    disarm();
    TF_EQ(g_rspzero, 1);
    TF_EQ(g_rspdirty, 0);
    TF_EQ(tfs_exists(tfs_subpath(TMBOX_RSPDIR, k_rsp)), 0);
    settle(m, "U\r\n");

    /* The request carries over a post that fails. */
    TF_EQ(queue(m, "Z"), MBXCLI_OK);
    mbxcli_zero_next(m);
    fail_op(CF_OP_CREATE, 1, 2, EDEV);
    TF_EQ(post_failing(m, "W"), MBXCLI_EIO);
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    take("Z\r\nW\r\n");
    answer("+OK Z\r\n+OK W\r\n");
    arm();
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    disarm();
    TF_EQ(g_rspzero, 1);
    mbxcli_close(m);
}

/* The free hook: count the blocks freed, and those not all zeros. */
static void check_free(cmem_blk *b)
{
    g_freed++;
    if (!blk_zero(b, cmem_size(b)))
        g_dirty++;
}

/* The queue zeroed once posted, both blocks at close. */
static void test_wipe(void)
{
    cmem_blk *q;
    cmem_blk *r;
    mbxcli *m;

    if (!TF_EQ(mbxcli_open(&m, tfs_dir(), STA), MBXCLI_OK))
        return;
    TF_EQ(queue(m, "LOGIN ADMIN PW"), MBXCLI_OK);
    mbxcli_blocks(m, &q, &r);
    TF_CHECK(!blk_zero(q, QLEN));
    TF_EQ(mbxcli_post(m, 0), MBXCLI_EWAIT);
    TF_CHECK(blk_zero(q, QLEN));
    take("LOGIN ADMIN PW\r\n");
    answer("+OK DATA\r\n");
    TF_EQ(mbxcli_post(m, 0), MBXCLI_OK);
    TF_EQ(queue(m, "PASSWD ADMIN PW NEW"), MBXCLI_OK);
    TF_CHECK(!blk_zero(r, QLEN));
    g_freed = 0;
    g_dirty = 0;
    cmem_freeing = check_free;
    mbxcli_close(m);
    cmem_freeing = NULL;
    TF_EQ(g_freed, 2);
    TF_EQ(g_dirty, 0);
}

int main(int argc, char **argv)
{
    cmem_init();
    tf_begin("T_MBXCLI");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    tfs_dirs(k_dirs, 0);
    if (!TF_EQ(tfs_dirs(k_dirs, 1), 0))
        return tf_done();
    test_open();
    test_post();
    test_frame();
    test_full();
    test_steps();
    test_retry();
    test_redone();
    test_unknown();
    test_gone();
    test_looks();
    test_zero();
    test_wipe();
    tfs_dirs(k_dirs, 0);
    return tf_done();
}
