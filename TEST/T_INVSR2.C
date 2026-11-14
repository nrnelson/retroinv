/*
 * T_INVSR2.C - the second part of T_INVSRV (T_INVSRV.C has the usage
 * and the list of checks): the F3 runs and their faults, the burst and
 * midnight runs on the host, a new log's opening, and the event log the
 * runs leave.
 */
#define COMPAT_NETBIOS
/* cmem_freeing, for the stuck runs, which only the host makes: Watcom
   C 6.5 runs out of compiler memory on the rest of COMPAT_FIND. */
#ifdef HOST_LINUX
#define COMPAT_FIND
#endif
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "MSG.H"
#include "SRVRUN.H"
#define TF_PART
#define TF_FILE "T_INVSR2"
#include "TESTFW.H"
#include "FAKENB.H"
#include "T_FILES.H"
#include "T_INVSRV.H"

#define LOGMAX          2048    /* bytes of the whole log               */
#define KEYWAIT         8       /* peeks before F3 is typed             */
#define SPINJ           1000    /* ticks the clock is spun on a pass    */
#define MAXALLOC        64      /* far allocations the sweep fails      */
#define STOPSLACK       8       /* ticks past RUN_STOPWAIT to the wait  */
#define GULIMIT         64      /* peeks before F3 and Y in the give-up
                                   run with a session                   */
#define LG_STAMP        20      /* "YYYY-MM-DD HH:MM:SS "               */
#define IDLEDAY         (1440UL * 6 * TMR_10SEC)  /* IDLE=1440, ticks  */

static const char k_newpw[] = "secret01";
#ifdef COMPAT_HOST
static const char k_stuck[] =
    "INVSERV: 1 NetBIOS commands pending; waiting (any key ends the wait)\n";
#endif

static const char k_nologw[] =
    "INVSERV: cannot write the event log *EV.LOG (error *)\n";

static u16 g_allocs;            /* far allocations since the count began */
static u16 g_failat;            /* the one to fail, 0 for none          */
static u16 g_reopen;            /* opens right after a create and close */
static u32 g_seen[3];           /* calls, creates and closes at the last
                                   open                                 */
static u16 g_closeat;           /* that close's number among the closes */
static u8 g_share;              /* fail that open with CF_ESHARE        */

/* The event log the runs leave, each line after its time stamp. */
static const char *const g_log[] = {
    "0 START RETROINV", "1 OPEN PEER", "1 REQ HELLO", "1 REQ LOGIN",
    "1 LOGIN ADMIN", "1 REQ PASSWD", "1 REQ STATUS", "1 REQ ADMIN CLIENTS",
    "1 REQ ITEM LIST", "1 REQ ADMIN SHUTDOWN", "1 REQ PING", "1 CLOSE",
    "0 STOP 0",
    "0 START RETROINV", "0 STOP 0",     /* the F3 run with LOG=         */
    "0 START RETROINV", "0 STOP 0",     /* the close counted            */
    "0 START RETROINV", "0 STOP 0",     /* the log's close failed       */
    "0 START RETROINV", "0 STOP 0",     /* the writes counted           */
    "0 START RETROINV"                  /* the last write failed        */
};
#define NLOG            (sizeof g_log / sizeof g_log[0])

#ifdef COMPAT_HOST
/* The burst run: the log's commits at its start, and its commits and
   ticks from the send to the last answer. */
static struct {
    u16  c0;
    u16  commits;
    u32  t0;
    u32  ticks;
} g_burst;

/* g_burst gets the log's commits and the ticks from the burst's send to
   its last answer. */
void burster(void *arg)
{
    static char line[NBURST * 6 + 1];
    peer *c;
    u16 n;
    u16 i;
    u8 rc;

    c = (peer *)arg;
    if (c->ended)
        return;
    if (c->lsn == 0) {
        if (fnb_peer_call("PEER", k_name, &c->lsn) != NB_OK)
            c->lsn = 0;
        return;
    }
    if (c->step == 0) {
        line[0] = '\0';
        for (i = 0; i < NBURST; i++)
            strcat(line, "PING\r\n");
        g_burst.c0 = tfs_count(CF_OP_COMMIT);
        g_burst.t0 = tmr_ticks();
        TF_EQ(fnb_peer_send(c->lsn, line, (u16)strlen(line)), NB_OK);
        c->step = 1;
        return;
    }
    rc = fnb_peer_recv(c->lsn, c->resp, RESPMAX - 1, &n);
    if (rc == NB_RC_PENDING)
        return;
    if (rc != NB_OK && rc != NB_RC_INCOMPLETE) {
        TF_EQ(rc, NB_RC_SESSCLOSED);
        TF_EQ(c->step, 2);
        c->ended = 1;
        return;
    }
    for (i = 0; i < n; i++)
        if (c->resp[i] == '\n')
            c->rlen++;
    if (c->step == 1 && c->rlen >= NBURST) {
        TF_EQ(c->rlen, NBURST);
        g_burst.commits = (u16)(tfs_count(CF_OP_COMMIT) - g_burst.c0);
        g_burst.ticks = tmr_since(g_burst.t0);
        TF_EQ(fnb_peer_send(c->lsn, "BYE\r\n", 5), NB_OK);
        c->step = 2;
    }
}

u16 stuck_key(keyst *k, int peek)
{
    TF_CHECK(msg(MSG_R_SRV)[0] != '?');
    if (!TF_EQ(conn_init(NULL, NULL, 1, 0, 0), CF_EINVAL))
        conn_free();
    if (!peek) {
        k->xread++;
        fnb_stall(0);
        return 'X';
    }
    if (++k->late == 1)
        k->span = tmr_since(k->t3);
    if (k->late < 3)
        return KEY_NONE;
    if (k->endkey)
        return 'X';
    fnb_stall(0);
    return KEY_NONE;
}

static u16 g_live;              /* far blocks counted_run's run holds   */

/* cmem_fault of counted_run: count the block, never fail it. */
static int count_alloc(u16 size)
{
    TF_CHECK(size >= 1);
    g_live++;
    return 0;
}

/* cmem_freeing of counted_run: count the block.  The one holding the
   buffer of the last RECEIVE must not be freed while a command is still
   in progress, and must be zeroed by then, as it held the requests. */
static void count_free(cmem_blk *b)
{
    u16 off;
    u16 len;
    u16 i;
    u8 c;

    g_live--;
    if (b != fnb_lastbuf(0, &off, &len))
        return;
    TF_EQ(fnb_pending(), 0);
    c = 0;
    for (i = 0; i < cmem_size(b) && c == 0; i++)
        cmem_get(b, i, &c, 1);
    TF_EQ(c, 0);
}

/* run(g_mdir) with its far blocks counted in g_live. */
static int counted_run(void)
{
    int st;

    g_live = 0;
    cmem_fault = count_alloc;
    cmem_freeing = count_free;
    st = run(g_mdir);
    cmem_fault = NULL;
    cmem_freeing = NULL;
    return st;
}

/* The stop's wait for NCBs the provider does not give up. */
void test_stuck(void)
{
    char t[LOGMAX];
    u16 got;

    /* A LISTEN whose cancel the provider does not confirm: the loop
       ends 2 * CONN_SHUTWAIT ticks into the shutdown, tnetb_stop leaves
       the NCB owned, and the stop waits until the provider gives it up,
       with the message file still loaded, then frees every far block
       the run took. */
    reset(NULL, 0);
    g_keys.stuck = 1;
    put_cfg(tfs_dir(), "SESSIONS=1", "SNOW=1");
    fnb_stall(0xFFFFu);
    fnb_hold(1);
    TF_EQ(counted_run(), SRV_EXIT_OK);
    fnb_stall(0);
    fnb_hold(0);
    TF_EQ(g_live, 0);
    TF_EQ(g_keys.late, 3);
    TF_EQ(g_keys.xread, 0);
    TF_CHECK(g_keys.span >= 2 * CONN_SHUTWAIT &&
             g_keys.span < 2 * CONN_SHUTWAIT + STOPSLACK);
    TF_CHECK(err_is(k_stuck));

    /* The same, the wait ended by a key, which the stop reads. */
    reset(NULL, 0);
    g_keys.stuck = 1;
    g_keys.endkey = 1;
    put_cfg(tfs_dir(), "SESSIONS=1", "SNOW=1");
    fnb_stall(0xFFFFu);
    fnb_hold(1);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    fnb_stall(0);
    fnb_hold(0);
    TF_EQ(g_keys.late, 3);
    TF_EQ(g_keys.xread, 1);
    TF_CHECK(err_is(k_stuck));

    /* A session open at the shutdown, its RECEIVE's cancel never
       confirmed: the loop gives up, tnetb_stop ends the session while
       the events still reach the log (its CLOSE line), and the stop
       waits for the NCB, then frees every far block the run took, the
       receive block the RECEIVE wrote into only once it is over. */
    reset(s_idle, 0);
    g_keys.stuck = 1;
    g_keys.limit = GULIMIT;
    put_cfg(tfs_dir(), log_cfg("GU"), "SNOW=1");
    fnb_stall(0xFFFFu);
    fnb_hold(1);
    TF_EQ(counted_run(), SRV_EXIT_OK);
    fnb_stall(0);
    fnb_hold(0);
    TF_EQ(g_live, 0);
    TF_EQ(g_keys.given, 2);
    TF_EQ(g_keys.late, 3);
    TF_CHECK(err_is(k_stuck));
    TF_EQ(tfs_read_all(tfs_path("GU", ".LOG"), t, (u16)(sizeof t - 1),
                       &got), 0);
    t[got] = '\0';
    TF_CHECK(strstr(t, " 1 CLOSE\r\n") != NULL);
}

/* Every pass busy for longer than TMR_SEC passes: NBURST PINGs in one
   message, answered one a pass.  The log's lines are committed TMR_SEC
   ticks apart all the same, at least once during the burst and never
   on every pass. */
void test_burst(void)
{
    reset(NULL, 0);
    g_peer.active = 1;
    fnb_pump(burster, &g_peer);
    memset(&g_burst, 0, sizeof g_burst);
    put_cfg(tfs_dir(), log_cfg("BU"), NULL);
    tfs_fault(TFS_OP(CF_OP_COMMIT), 0, 0, 0);
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    tfs_fault_off();
    TF_EQ(g_peer.step, 2);
    TF_CHECK(g_burst.ticks > 3 * TMR_SEC);
    TF_CHECK(g_burst.commits >= 1);
    TF_CHECK(g_burst.commits <= g_burst.ticks / TMR_SEC + 1);
}

/* An idle session open across midnight: the clock, spun on SPINJ ticks
   a pass by the key source from just before one midnight, with IDLE= a
   day, closes it a day of ticks after its last answer, and the server
   then runs on past a second midnight. */
void test_midnight(void)
{
    u32 i;

    reset(s_idle, 0);
    put_cfg(tfs_dir(), "IDLE=1440", NULL);
    for (i = tmr_ticks(); i < (u32)TMR_DAY - 2 * SPINJ; i = tmr_ticks())
        ;
    g_vlast = tmr_ticks();
    g_vt = 0;
    g_wraps = 0;
    g_spin = SPINJ;
    TF_EQ(run(g_mdir), SRV_EXIT_OK);
    g_spin = 0;
    TF_EQ(g_peer.ended, 1);
    TF_EQ(g_wraps, 2);
    TF_CHECK(g_closed - g_last >= IDLEDAY &&
             g_closed - g_last < IDLEDAY + 3UL * SPINJ);
}
#endif

/* The scratch path of the subdirectory NODIR, missing, with name (or ""
   for the directory itself) into out (PATHMAX bytes). */
static void nodir(char *out, const char *name)
{
    const char *d;
    char sep;

    d = tfs_dir();
    sep = d[strlen(d) - 1];
    sprintf(out, "%sNODIR%c%s", d, sep == ':' ? '\\' : sep, name);
    if (name[0] == '\0')
        out[strlen(out) - 1] = '\0';
}

/* The start-up failures, each said on standard error. */
void test_refusals(void)
{
    char p[PATHMAX + 4];
    char q[PATHMAX];
    char name[NB_NAMELEN];
    u8 num;
    int i;

    reset(NULL, 0);
    TF_EQ(tfs_write_all(tfs_path("SRV", ".CFG"), "BOGUS=1\r\n", 9), 0);
    TF_EQ(run(g_mdir), SRV_EXIT_CFG);
    TF_CHECK(err_is("INVSERV: *SRV.CFG line 1 is not valid\n"));

    reset(NULL, 0);
    fnb_dupname(k_name);
    put_cfg(tfs_dir(), NULL, NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_NAME);
    TF_CHECK(err_is("INVSERV: the name RETROINV is in use\n"));

    /* A full name table: ADD NAME gives NB_RC_NAMEFULL. */
    reset(NULL, 0);
    TF_EQ(nb_present(), 1);
    for (i = 0; i < FNB_NAMES; i++) {
        sprintf(name, "FILL%d", i);
        TF_EQ(nb_add_name(name, &num), NB_OK);
    }
    g_names = FNB_NAMES;
    TF_EQ(run(g_mdir), SRV_EXIT_NAME);
    g_names = 0;
    TF_CHECK(err_is("INVSERV: cannot add the name RETROINV (NetBIOS error "
                    "0E)\n"));
    for (i = 0; i < FNB_NAMES; i++) {
        sprintf(name, "FILL%d", i);
        TF_EQ(nb_delete_name(name), NB_OK);
    }
    TF_EQ(fnb_names(), 0);

    reset(NULL, 0);
    nodir(p, "");
    put_cfg(p, NULL, NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_OPEN);
    TF_CHECK(err_is("INVSERV: cannot open the data directory: *NODIR*"
                    "(error *)\n"));

    reset(NULL, 0);
    nodir(q, "EV.LOG");
    sprintf(p, "LOG=%s", q);
    put_cfg(tfs_dir(), p, NULL);
    TF_EQ(run(g_mdir), SRV_EXIT_IO);
    TF_CHECK(err_is("INVSERV: cannot open the event log *EV.LOG (error *)\n"));

    reset(NULL, 0);
    strcpy(p, "=STAMP 0000\r\n");
    TF_EQ(tfs_write_all(tfs_path("INVSERV", ".MSG"), p, (u16)strlen(p)), 0);
    put_cfg(tfs_dir(), NULL, NULL);
    TF_EQ(run(tfs_dir()), SRV_EXIT_MSG);
    TF_CHECK(err_is("INVSERV: cannot read INVSERV.MSG: *\n"));
}

/* cmem_fault hook: fail far allocation number g_failat. */
static int fail_nth(u16 size)
{
    TF_CHECK(size >= 1);
    return ++g_allocs == g_failat;
}

/* Fail far allocation k, for k = 1, 2, ... until the run succeeds, with
   COM1 and the mailbox served beside two NetBIOS sessions (with six, the
   test's own far memory leaves too little on MS-DOS 3.10 under Turbo
   C): the statuses come in the order of the start, the message file
   (SRV_EXIT_MSG), the data (SRV_EXIT_OPEN), then the slots, the NCBs,
   the port and its buffer, the mailbox's buffer and the screen
   (SRV_EXIT_NOMEM). */
void test_far(void)
{
    static const int order[] = {
        SRV_EXIT_MSG, SRV_EXIT_OPEN, SRV_EXIT_NOMEM, SRV_EXIT_OK
    };
    char mbox[PATHMAX + 8];
    int seen[4];
    int st;
    int at;
    int r;

    memset(seen, 0, sizeof seen);
    sprintf(mbox, "MAILBOX=%s", tfs_dir());
    put_cfg(tfs_dir(), "SESSIONS=2\r\nSERIAL=COM1", mbox);
    at = 0;
    for (g_failat = 1; g_failat <= MAXALLOC; g_failat++) {
        reset(NULL, 0);
        make_com1();
        g_allocs = 0;
        cmem_fault = fail_nth;
        st = run(g_mdir);
        cmem_fault = NULL;
        for (r = 0; r < 4 && order[r] != st; r++)
            ;
        if (!TF_CHECK(r < 4 && r >= at)) {
            printf("  far allocation %u: status %d\n", g_failat, st);
            break;
        }
        at = r;
        seen[r]++;
        TF_CHECK(st == SRV_EXIT_OK ? err_is("") :
                 st == SRV_EXIT_MSG ?
                 err_is("INVSERV: cannot read INVSERV.MSG: *\n") :
                 st == SRV_EXIT_OPEN ?
                 err_is("INVSERV: cannot open the data directory: *\n") :
                 glob(err_text(),
                      "INVSERV: not enough memory for the sessions\n") ||
                 glob(err_text(), "INVSERV: cannot set up the screen\n"));
        if (st == SRV_EXIT_OK)
            break;
    }
    g_failat = 0;
    TF_CHECK(seen[0] >= 1);
    TF_CHECK(seen[2] >= 3);
    TF_EQ(seen[3], 1);
}

/* An F3 run with the settings put_cfg wrote, the fault recorder armed
   for ops, failing call at (0 for none); returns the status. */
static int f3_run(int ops, u32 at)
{
    int st;

    reset(NULL, KEYWAIT);
    tfs_fault(ops, at, 1, 0);
    st = run(g_mdir);
    tfs_fault_off();
    TF_EQ(g_keys.given, 2);
    return st;
}

void test_f3(void)
{
    u16 commits;
    u16 n;

    put_cfg(tfs_dir(), log_cfg("EV"), "SNOW=1");
    TF_EQ(f3_run(TFS_OP(CF_OP_COMMIT), 0), SRV_EXIT_OK);
    commits = tfs_count(CF_OP_COMMIT);
    TF_EQ(commits, 1);
#ifdef COMPAT_HOST
    TF_EQ(g_keys.snow, 1);
#endif
    TF_EQ(vid_snow_active(), 0);
    put_cfg(tfs_dir(), "SNOW=1", NULL);
    TF_EQ(f3_run(TFS_OP(CF_OP_COMMIT), 0), SRV_EXIT_OK);
    TF_CHECK(commits > tfs_count(CF_OP_COMMIT));

    /* The log's close is the last close of a run with LOG=, one more
       than a run without has, whose last is a data file's. */
    put_cfg(tfs_dir(), log_cfg("EV"), NULL);
    TF_EQ(f3_run(TFS_OP(CF_OP_CLOSE), 0), SRV_EXIT_OK);
    n = tfs_count(CF_OP_CLOSE);
    TF_EQ(f3_run(TFS_OP(CF_OP_CLOSE), n), SRV_EXIT_IO);
    TF_CHECK(err_is(k_nologw));
    put_cfg(tfs_dir(), NULL, NULL);
    TF_EQ(f3_run(TFS_OP(CF_OP_CLOSE), 0), SRV_EXIT_OK);
    TF_EQ(tfs_count(CF_OP_CLOSE), n - 1);
    n = tfs_count(CF_OP_CLOSE);
    TF_EQ(f3_run(TFS_OP(CF_OP_CLOSE), n), SRV_EXIT_IO);
    TF_CHECK(err_is("INVSERV: cannot close the data files (error *)\n"));

    /* The STOP line is the last write of a run with LOG=. */
    put_cfg(tfs_dir(), log_cfg("EV"), NULL);
    TF_EQ(f3_run(TFS_OP(CF_OP_WRITE), 0), SRV_EXIT_OK);
    n = tfs_count(CF_OP_WRITE);
    TF_EQ(f3_run(TFS_OP(CF_OP_WRITE), n), SRV_EXIT_IO);
    TF_CHECK(err_is(k_nologw));
}

/* cf_fault_rd hook: count in g_reopen each open that the recorder saw
   preceded, since the open before it, by one create, one close and
   nothing else; keep that close's number in g_closeat, and fail the
   open with CF_ESHARE when g_share is set. */
static int see_open(int op)
{
    u32 now[3];
    int rc;

    if (op != CF_OP_OPEN)
        return 0;
    rc = 0;
    now[0] = tfs_calls();
    now[1] = tfs_count(CF_OP_CREATE);
    now[2] = tfs_count(CF_OP_CLOSE);
    if (now[0] == g_seen[0] + 2 && now[1] == g_seen[1] + 1 &&
        now[2] == g_seen[2] + 1) {
        g_reopen++;
        g_closeat = (u16)now[2];
        if (g_share)
            rc = CF_ESHARE;
    }
    memcpy(g_seen, now, sizeof g_seen);
    return rc;
}

/* A run with LOG= naming NL.LOG, which is removed first, and the
   recorder armed for closes, failing close number at (0 for none);
   returns the status. */
static int newlog_run(u32 at)
{
    int st;

    cf_remove(tfs_path("NL", ".LOG"));
    reset(NULL, KEYWAIT);
    g_reopen = 0;
    memset(g_seen, 0, sizeof g_seen);
    tfs_fault(TFS_OP(CF_OP_CLOSE), at, 1, 0);
    st = run(g_mdir);
    tfs_fault_off();
    return st;
}

/* 1 when the last run said that NL.LOG could not be opened, with the
   error code; else 0, the text printed. */
static int nolog_is(int code)
{
    char p[72];

    sprintf(p, "INVSERV: cannot open the event log *NL.LOG (error %d)\n",
            code);
    return err_is(p);
}

/* A missing log is created, closed and opened again, so that it is
   held denying writes as an existing one is (the host ignores sharing
   modes, so the reopen is what shows); an existing log is opened at
   once.  A failure of the close or of the reopen stops the start with
   SRV_EXIT_IO, saying the code. */
void test_newlog(void)
{
    u16 made;
    u16 closeat;

    put_cfg(tfs_dir(), log_cfg("NL"), NULL);
    cf_fault_rd = see_open;
    TF_EQ(newlog_run(0), SRV_EXIT_OK);
    TF_EQ(g_keys.given, 2);
    TF_EQ(g_reopen, 1);
    made = tfs_count(CF_OP_CREATE);
    closeat = g_closeat;
    g_reopen = 0;
    memset(g_seen, 0, sizeof g_seen);
    TF_EQ(f3_run(TFS_OP(CF_OP_CLOSE), 0), SRV_EXIT_OK);
    TF_EQ(g_reopen, 0);
    TF_EQ(tfs_count(CF_OP_CREATE), made - 1);

    g_share = 1;
    TF_EQ(newlog_run(0), SRV_EXIT_IO);
    g_share = 0;
    TF_EQ(g_reopen, 1);
    TF_CHECK(nolog_is(CF_ESHARE));
    TF_EQ(newlog_run(closeat), SRV_EXIT_IO);
    TF_EQ(g_reopen, 0);
    TF_CHECK(nolog_is(CF_EFULL));
    cf_fault_rd = NULL;
}

int log_is(const char *base, const char *const *want, u16 n)
{
    static char t[LOGMAX];
    char *s;
    char *e;
    u16 got;
    u16 i;
    int k;

    if (!TF_EQ(tfs_read_all(tfs_path(base, ".LOG"), t, LOGMAX - 1, &got),
               0))
        return 0;
    t[got] = '\0';
    if (!TF_CHECK(strstr(t, k_newpw) == NULL))
        return 0;
    s = t;
    for (i = 0; i < n; i++) {
        e = strstr(s, "\r\n");
        if (!TF_CHECK(e != NULL))
            return 0;
        *e = '\0';
        for (k = 0; k < LG_STAMP; k++)
            if (!TF_CHECK(k == 4 || k == 7 ? s[k] == '-' :
                          k == 13 || k == 16 ? s[k] == ':' :
                          k == 10 || k == 19 ? s[k] == ' ' :
                          TXT_ISDIGIT(s[k])))
                return 0;
        if (!TF_STREQ(s + LG_STAMP, want[i]))
            return 0;
        s = e + 2;
    }
    return TF_EQ(*s, '\0');
}

/* Check the log of the runs with LOG= naming EV.LOG against g_log. */
void check_log(void)
{
    log_is("EV", g_log, NLOG);
}
