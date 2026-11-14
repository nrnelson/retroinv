/*
 * T_SCRH.C - the harness of the screen tests T_SCRE (TEST/T_SCRE.C)
 * and T_SCRR (TEST/T_SCRR.C), see TEST/T_SCRH.H: screens of the
 * full-screen client run under key scripts, their snapshots compared
 * with golden snapshots.
 *
 * usage: T_SCRE [-u] [-v] [-g golddir] [-p filedir] [-m msgdir] dir
 *               [file...]
 *        (T_SCRR the same)
 *
 * dir is a scratch directory prefix ("OBJ/HOST/SCRET/", "D:\") holding
 * nothing but this test's files: the data files and the snapshot files.
 * Each file argument ending in .KEY is a key script to run (other
 * arguments, such as the goldens put beside the scripts on a boot
 * floppy, are ignored, and file arguments without a script run
 * nothing); without any file argument, every script of the program's
 * table is run from the program's own directory.  msgdir holds INV.MSG
 * and the help file INV.HLP (default: the program's directory) and is
 * refused when longer than LINK_MAXDIR, the help's directory limit
 * (tui_set_helpdir); golddir the goldens (default: the directory of
 * each script), filedir those of the files a program compares through
 * th_check_file (print jobs, exported files; default the same).  -u
 * writes the snapshots and those files as the new goldens instead of
 * comparing them; -v prints each script's requests.
 *
 * Before each run the data directory is emptied (te_remove) and seeded by
 * TEST/T_SCRH2.C through requests on a LOCAL link (TEST/TRLINK.C, which
 * keeps the engine's headers out of this file) with the clock fixed at
 * 2026-10-08 12:00:00: three users (ADMIN level 3, OPER level 2, READER
 * level 1, none with a password change pending), a tree of eleven
 * locations in three rooms, the attic's shelf empty, and TH_SEEDITEMS
 * items spread over the categories, makers and seven of the locations,
 * one of them with notes.  So every script starts from the same data
 * whatever ran before it.  A script's table entry may add seed sets of
 * its own (SD_ flags, see seed_sets of T_SCRH2.C), so the other scripts'
 * screens stay as they are: assemblies
 * nested deeper than the Parts tab indents, an assembly with several pages
 * of parts, maintenance records (more than a page of them, one with notes),
 * and loans that are overdue, due today, due later and returned.
 *
 * A script is named after its file (SCRE1.KEY), its number its row of the
 * table (th_known); the row gives the user it logs in as, the commands the
 * home screen starts in turn (it quits when they are used up), and
 * optionally one request, counted from the first after the login, that is
 * answered with an error line of the test's own instead (TRLINK's tl_fail),
 * standing in for a record deleted by another session or a refusal by the
 * server, or at which the session ends, as when the server has gone away,
 * or both, an answer ending in '\n' ending the session after it.  A script
 * listed in the program's memfaults also has some of its far allocations
 * fail (COMPAT.H's cmem_fault), as when memory has run out, so its
 * snapshots show what the screens then tell the user.  Both counts start
 * again at a login a failed request asked for, and the request log (tl_log,
 * the SF_RO check) holds what followed it.  With SF_HOME the home screen
 * reads a key between the commands, as INV's does, so a message left by a
 * screen that could not open can be seen there.  The screens run inside
 * tui_run with a login function that logs in through requests, so the
 * global keys (F3, F10) work as in INV.  A script must leave every screen
 * it opens: the run must come back from tui_run with every key used and
 * every far block the screens took freed (counted through COMPAT.H's
 * cmem_fault and cmem_freeing hooks); on the host, every near block the run
 * took must be freed by its end too, an exit inside a screen included
 * (tm_held).  Only a script with SF_EXIT ends inside a screen instead,
 * through the exit function, at the end of its keys or after a fatal
 * error's box, with the status th_status (0 unless the program's th_begin
 * sets another), which runs the program's th_onexit as INV's exit path runs
 * its wipe.  A script with SF_NET runs as on a NET link (tui_s.conn), so
 * the screens take their remote paths: the idle hook's PINGs (key_idle) and
 * scr_reopen, which the program gives.  For a script of a read-only user
 * (SF_RO) no request it sent may change data, which shows that the screens
 * refused the level-2 keys without asking the server.
 *
 * The snapshot file is compared line by line with the golden NAME.SNC;
 * its =STACK UNUSED= line, which the DOS builds must write, is not
 * compared but must show at least STACK_MARGIN bytes.  On a monochrome
 * adapter the golden is NAME.SNM when it exists; otherwise the text of
 * NAME.SNC is compared and the attributes are not.  A script with
 * SF_MONO runs a second time on a monochrome screen (see run_script),
 * which gives the host build its NAME.SNM goldens.  The snapshot file of
 * a script that agreed with its golden (or became it) is removed, and
 * the data files once the last script has run, so what is left in dir
 * is what failed.
 *
 * The last check is that the run compared every snapshot and every
 * file of th_check_file with its golden, or none with -u.
 *
 * Before the first script every script name of the side tables (the
 * program's memfaults, and its own through th_check_tables) must name a
 * row of th_scripts, so that a misspelt name cannot turn a fault off.
 */
#define TUI_VERBTAB
#define TF_TERSE
#define TF_CLOCK
#define TF_MALLOC
#define TF_CMPFILE
#define COMPAT_FIND
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"
#include "KEYSCR.H"
#include "SCR.H"
#include "SCRVERB.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#include "T_SCRH.H"
#include "TESTFW.H"

#define LINEMAX         300     /* longest snapshot line (=ATTR) and more */
#define STACK_MARGIN    1024    /* stack bytes a run must leave unused,
                                   at most MSG_DECCAP (stack_check)     */

cfg scr_cfg;
const char *scr_cfgpath = "TEST.CFG";

char th_ddir[TH_PATHMAX];
link_clock th_clk;
const script *th_cur;
char th_curname[TH_NAMESZ];
link *th_lk;
int th_status;
void (*th_onexit)(void);
/* The -m, -g and -p directories: main's argv or its own program
   directory, which live as long as the run, so no copy takes near data. */
static const char *g_mdir;
static const char *g_gdir = "";
static const char *g_pdir = "";
static const char *g_kdir;      /* the directory of the script running,
                                   run_script's, while it runs         */
static int g_update;
static int g_verbose;
static u16 g_ngold;             /* snapshots and files to compare       */
static u16 g_ncmp;              /* of them compared with their goldens  */
static int g_homes;
static u16 g_allocs;            /* far allocations since the login      */
static u16 g_live;              /* far blocks the script holds          */
static const memfault *g_curmf; /* the script's entry, or NULL          */
static jmp_buf g_jb;
static int g_status;            /* the status a script's exit gave      */

/* The seeded users' names and passwords, by AD, OP and RD. */
static const char *const users[] = { "ADMIN", "OPER", "READER" };
static const char *const pws[] = { "secret1", "oper22", "read22" };

void th_say(const char *fmt, ...)
{
    va_list ap;

    printf("%s: ", th_name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* The fault hook of cmem_alloc: the allocations listed for the script
   in memfaults fail, as when memory has run out, and every other one is
   counted in g_live until cmem_free takes it back (mem_freed); with -v
   every allocation is printed, which finds the numbers to list. */
static int mem_fault(u16 size)
{
    u16 i;

    g_allocs++;
    if (g_verbose)
        th_say("allocation %u of %u bytes\n", (unsigned)g_allocs,
            (unsigned)size);
    for (i = 0; g_curmf != NULL && i < TH_MAXFAULTS; i++)
        if (g_curmf->n[i] == g_allocs)
            return 1;
    g_live++;
    return 0;
}

/* The free hook of cmem_free: block b is one the script held. */
static void mem_freed(cmem_blk *b)
{
    if (TF_CHECK(b != NULL && g_live > 0))
        g_live--;
}

/* ---------------------------------------------------------------------
 * Files.
 * --------------------------------------------------------------------- */
typedef struct {
    int  h;
    u16  pos;
    u16  got;
    char buf[256];
} rd;

static int rd_open(rd *r, const char *path)
{
    r->pos = 0;
    r->got = 0;
    return cf_open(path, CF_READ, &r->h);
}

/* The next line without its CR LF; 0 at the end of the file. */
static int rd_line(rd *r, char *line, u16 max)
{
    u16 n;
    int any;
    char c;

    n = 0;
    any = 0;
    for (;;) {
        if (r->pos >= r->got) {
            r->pos = 0;
            if (cf_read(r->h, r->buf, (u16)sizeof r->buf, &r->got) != 0)
                r->got = 0;
            if (r->got == 0)
                break;
        }
        c = r->buf[r->pos++];
        any = 1;
        if (c == '\n')
            break;
        if (c != '\r' && n + 1 < max)
            line[n++] = c;
    }
    line[n] = '\0';
    return any;
}

static void join(char *d, const char *dir, const char *name, const char *ext)
{
    strcpy(d, dir);
    strcat(d, name);
    strcat(d, ext);
}

/* ---------------------------------------------------------------------
 * The screens' main loop.
 * --------------------------------------------------------------------- */
static u8 t_login(u8 cmd)
{
    rq q;
    rq_res res;
    char lv[4];
    u16 i;

    if (cmd != TUI_CMD_LOGIN)
        return TUI_CMD_QUIT;
    if (!TF_CHECK(th_req("HELLO TEST")))
        return TUI_CMD_QUIT;
    rq_begin(&q, "LOGIN");
    rq_word(&q, users[th_cur->user]);
    rq_word(&q, pws[th_cur->user]);
    if (!TF_EQ(rq_send(th_lk, &q, NULL, NULL, &res), RQ_OK) ||
        !TF_EQ(rq_okval(&res, "LEVEL", lv, (u16)sizeof lv), 1))
        return TUI_CMD_QUIT;
    rq_okval(&res, "NAME", tui_s.name, (u16)sizeof tui_s.name);
    strcpy(tui_s.user, users[th_cur->user]);
    tui_s.level = (u8)(lv[0] - '0');
    tui_frame();
    tl_clear();
    if (th_cur->failn != 0)
        tl_fail(th_cur->failn, th_cur->fail);
    g_allocs = 0;
    g_curmf = NULL;
    for (i = 0; th_memfaults[i].name != NULL; i++)
        if (strcmp(th_memfaults[i].name, th_curname) == 0)
            g_curmf = &th_memfaults[i];
    th_login();
    return TUI_CMD_NONE;
}

/* With SF_HOME, before each command after the first one key is read,
   as INV's home screen reads one, so a script can take a snapshot of
   what a screen left on the hint row. */
static u8 t_home(u8 cmd)
{
    if (cmd != TUI_CMD_NONE || g_homes >= TH_MAXCMDS ||
        th_cur->cmd[g_homes] == 0)
        return TUI_CMD_QUIT;
    if ((th_cur->flags & SF_HOME) && g_homes > 0)
        tui_key();
    return th_cur->cmd[g_homes++];
}

static const tui_app app = { 0, NULL, th_run, t_home, t_login, SCMD_FIND };

/* The exit function of the screens (tui_set_exit): a script that ends
   the program inside a screen returns to run_once with its status,
   after th_onexit. */
static void exit_run(int status)
{
    g_status = status;
    if (th_onexit != NULL)
        th_onexit();
    longjmp(g_jb, 1);
}

/* ---------------------------------------------------------------------
 * Comparing snapshots.
 * --------------------------------------------------------------------- */
/* The DOS builds write the =STACK line; the host build cannot measure. */
#ifdef COMPAT_HOST
#define STACK_LINE      0
#else
#define STACK_LINE      1
#endif

/* Drop " PALETTE=..." from a =SNAP line. */
static void no_palette(char *s)
{
    char *p;
    char *e;

    p = strstr(s, " PALETTE=");
    if (strncmp(s, "=SNAP", 5) != 0 || p == NULL)
        return;
    e = p + 1;
    while (*e != '\0' && *e != ' ')
        e++;
    memmove(p, e, strlen(e) + 1);
}

/* Check the =STACK UNUSED= line of the snapshot file snap, which the
   DOS builds must have written: its figure must be STACK_MARGIN or
   more. */
static void stack_check(const char *snap)
{
    rd ra;
    char la[LINEMAX];
    const char *n;
    int found;

    found = 0;
    if (rd_open(&ra, snap) != 0)
        return;
    while (rd_line(&ra, la, LINEMAX))
        if (strncmp(la, "=STACK UNUSED=", 14) == 0) {
            th_say("%s %s\n", th_curname, la + 1);
            n = la + 14;
            TF_CHECK(msg_dec(&n, STACK_MARGIN) >= STACK_MARGIN);
            found = 1;
        }
    cf_close(ra.h);
    if (!TF_CHECK(found || !STACK_LINE))
        th_say("%s has no =STACK line\n", th_curname);
}

/* Compare the snapshot file snap with the golden gold; textonly leaves
   out the attributes.  1 when they agree. */
static int compare(const char *snap, const char *gold, int textonly)
{
    rd ra;
    rd rb;
    char la[LINEMAX];
    char lb[LINEMAX];
    u16 line;
    u16 skip;
    int ea;
    int eb;
    int ok;

    g_ncmp++;
    line = 0;
    skip = 0;
    ok = 1;
    if (rd_open(&ra, snap) != 0) {
        th_say("cannot read %s\n", snap);
        return 0;
    }
    if (rd_open(&rb, gold) != 0) {
        th_say("cannot read %s\n", gold);
        cf_close(ra.h);
        return 0;
    }
    for (;;) {
        for (;;) {
            ea = rd_line(&ra, la, LINEMAX);
            line++;
            if (!ea || strncmp(la, "=STACK UNUSED=", 14) != 0)
                break;
        }
        eb = rd_line(&rb, lb, LINEMAX);
        if (!ea || !eb) {
            if (ea != eb) {
                th_say("%s and %s differ in length\n", snap, gold);
                ok = 0;
            }
            break;
        }
        if (textonly) {
            if (skip > 0) {
                skip--;
                continue;
            }
            if (strncmp(la, "=ATTR", 5) == 0 &&
                strncmp(lb, "=ATTR", 5) == 0) {
                skip = VID_ROWS;
                continue;
            }
            no_palette(la);
            no_palette(lb);
        }
        if (strcmp(la, lb) != 0) {
            th_say("%s line %u differs from %s\n  got  %s\n"
                "  want %s\n", snap, (unsigned)line, gold, la, lb);
            ok = 0;
            break;
        }
    }
    cf_close(ra.h);
    cf_close(rb.h);
    return ok;
}

/* Write the snapshot as the golden, without its =STACK line. */
static int write_golden(const char *snap, const char *gold)
{
    rd ra;
    char la[LINEMAX + 2];
    int out;
    int rc;
    int rc2;

    if (rd_open(&ra, snap) != 0)
        return 0;
    rc = cf_create(gold, &out);
    if (rc == 0) {
        while (rc == 0 && rd_line(&ra, la, LINEMAX)) {
            if (strncmp(la, "=STACK UNUSED=", 14) == 0)
                continue;
            strcat(la, "\r\n");
            rc = cf_write(out, la, (u16)strlen(la));
        }
        rc2 = cf_close(out);
        if (rc == 0)
            rc = rc2;
    }
    cf_close(ra.h);
    th_say(rc == 0 ? "wrote %s\n" : "cannot write %s\n", gold);
    return rc == 0;
}

/* Compare the file path byte for byte with the script's golden NAME
   plus ext, in the -p directory or else beside the script (tf_cmpfile),
   or with -u copy it there; 1 when they agree or it was copied. */
int th_check_file(const char *path, const char *ext)
{
    char gold[TH_PATHMAX + 16];
    char a[128];
    int ha;
    int hb;
    u16 na;
    int ok;

    join(gold, g_pdir[0] != '\0' ? g_pdir : g_kdir, th_curname, ext);
    if (cf_open(path, CF_READ, &ha) != 0) {
        th_say("cannot read %s\n", path);
        return TF_CHECK(0);
    }
    g_ngold++;
    if (!g_update) {
        cf_close(ha);
        g_ncmp++;
        return TF_CHECK(tf_cmpfile(path, gold));
    }
    ok = cf_create(gold, &hb) == 0;
    if (ok) {
        while (ok && cf_read(ha, a, (u16)sizeof a, &na) == 0 && na > 0)
            ok = cf_write(hb, a, na) == 0;
        ok &= cf_close(hb) == 0;
    }
    cf_close(ha);
    th_say(ok ? "wrote %s\n" : "cannot write %s\n", gold);
    return TF_CHECK(ok);
}

/* ---------------------------------------------------------------------
 * Running one script.
 * --------------------------------------------------------------------- */
/* 1 when request line r changes data: a second word ADD, SET, DEL,
   COPY, UNDEL, INSTALL, REMOVE, MOVE, OUT or RETURN, an ADD= pair
   (NOTES), the command EXPORT (which writes a file) or IMPORT, or an
   ADMIN request other than CLIENTS.  ADMIN is told by its letters ADM,
   which no other command starts with, and CLIENTS by its C: a literal
   would take near data from every screen test. */
static int changes(const char *r)
{
    static const char *const w[] = { " ADD", " SET", " DEL", " COPY",
                                     " UNDEL", " INSTALL", " REMOVE",
                                     " MOVE", " OUT", " RETURN" };
    const char *p;
    size_t n;
    u16 i;

    p = strchr(r, ' ');
    if (strncmp(r, "EXPORT ", 7) == 0 || strncmp(r, "IMPORT ", 7) == 0)
        return 1;
    if (p == NULL)
        return 0;
    if (r[0] == 'A' && r[1] == 'D' && r[2] == 'M')
        return p[1] != 'C';
    for (i = 0; i < sizeof w / sizeof w[0]; i++) {
        n = strlen(w[i]);
        if (strncmp(p, w[i], n) == 0 && (p[n] == ' ' || p[n] == '\0'))
            return 1;
    }
    return strstr(r, " ADD=") != NULL;
}

/* After a script of a read-only user: no request may have changed
   anything (the refusals come from the screens, without a request). */
static void check_ro(void)
{
    u16 i;
    u16 n;
    int bad;

    n = tl_count();
    bad = !TF_CHECK(!tl_full());
    if (bad)
        th_say("%s: log full\n", th_curname);
    for (i = 0; i < n; i++)
        if (!TF_CHECK(!changes(tl_req(i)))) {
            bad = 1;
            th_say("%s sent %s\n", th_curname, tl_req(i));
        }
    if (!bad)
        th_say("%s sent %u requests, none changing data\n", th_curname,
            (unsigned)n);
}

/* Run the current script from keypath once, its goldens in dir. */
static void run_once(const char *keypath, const char *dir)
{
    char snap[TH_PATHMAX + 16];
    char gold[TH_PATHMAX + 16];
    char why[LINK_WHYSZ];
    u16 held;
    int exited;
    int rc;

    held = tm_held();
    if (!th_reseed())
        return;
    rc = link_open_local(&th_lk, th_ddir, &th_clk, why, (u16)sizeof why);
    if (!TF_EQ(rc, 0)) {
        th_say("%s\n", why);
        return;
    }
    join(snap, th_ddir, th_curname, ".SNP");
    rc = ks_open(keypath, snap, why, (u16)sizeof why);
    if (!TF_EQ(rc, 0)) {
        th_say("key script %s\n", why);
        link_close(th_lk);
        th_lk = NULL;
        return;
    }
    tui_s.l = th_lk;
    tui_s.conn = (u8)(th_cur->flags & SF_NET ? TUI_CONN_NET : TUI_CONN_LOCAL);
    strcpy(tui_s.station, "TEST");
    tui_palette((u8)(vid_mono() ? TP_MONO : TP_COLOR));
    /* No help topic and the cursor hidden, as at INV's start (vid_open),
       whatever the last script left. */
    tui_topic(NULL);
    vid_cursor(0, 0, VID_CUR_HIDE);
    tui_clear(0, VID_ROWS - 1);
    g_homes = 0;
    g_kdir = dir;
    th_status = 0;
    TF_EQ(th_begin(), 0);
    g_allocs = 0;
    g_live = 0;
    g_curmf = NULL;
    cmem_fault = mem_fault;
    cmem_freeing = mem_freed;
    cstk_fill();
    exited = 0;
    tui_verbs(SV_OPER, MW_OPEN);
    if (setjmp(g_jb) == 0)
        tui_run(&app);
    else
        exited = 1;
    cmem_fault = NULL;
    cmem_freeing = NULL;
    th_end();
    if (!TF_EQ(exited, (th_cur->flags & SF_EXIT) != 0) ||
        (exited && !TF_EQ(g_status, th_status)))
        th_say(exited ? "%s ended inside a screen (status %d)\n" :
            "%s did not end inside a screen\n", th_curname, g_status);
    if (!exited && !TF_EQ(key_read(), KEY_EOS))
        th_say("%s has keys left after the screens ended\n",
            th_curname);
    if (!exited && !TF_EQ(g_live, 0))
        th_say("%s left %u far blocks\n", th_curname, (unsigned)g_live);
    TF_EQ(ks_close(), 0);
    if (g_verbose)
        printf("%s", tl_log());
    if (th_cur->flags & SF_RO)
        check_ro();
    TF_EQ(link_close(th_lk), 0);
    th_lk = NULL;
    tui_s.l = NULL;
    TM_HELD(held);
    stack_check(snap);
    g_ngold++;
    join(gold, g_gdir[0] != '\0' ? g_gdir : dir, th_curname,
         vid_mono() ? ".SNM" : ".SNC");
    if (g_update) {
        rc = write_golden(snap, gold);
    } else if (vid_mono() && cf_exists(gold) != 1) {
        join(gold, g_gdir[0] != '\0' ? g_gdir : dir, th_curname, ".SNC");
        th_say("%s compared without attributes\n", th_curname);
        rc = compare(snap, gold, 1);
    } else {
        rc = compare(snap, gold, 0);
    }
    if (TF_CHECK(rc))
        cf_remove(snap);
}

const script *th_known(const char *name)
{
    const char *p;
    size_t n;
    u16 i;

    /* The program's name less its T_ is the scripts' prefix. */
    n = strlen(th_name + 2);
    i = 0;
    if (strncmp(name, th_name + 2, n) == 0 && name[n] >= '1' &&
        name[n] <= '9')
        for (p = name + n; *p >= '0' && *p <= '9' && i <= th_nscripts; p++)
            i = (u16)(i * 10 + (u16)(*p - '0'));
    else
        p = name;
    if (TF_CHECK(*p == '\0' && i >= 1 && i <= th_nscripts))
        return &th_scripts[i - 1];
    th_say("no script %s\n", name);
    return NULL;
}

/* Run the script keypath (its table entry found by its file name),
   and once more on a monochrome screen when it has SF_MONO: the host
   build then acts as an MDA (VID_F_MDA), which the DOS builds ignore,
   running it on their own adapter again. */
static void run_script(const char *keypath)
{
    char name[TH_PATHMAX];
    char dir[TH_PATHMAX];
    const char *base;
    const char *p;
    size_t n;

    base = keypath;
    for (p = keypath; *p != '\0'; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    n = (size_t)(base - keypath);
    if (!TF_CHECK(n < sizeof dir)) {
        th_say("script path %s too long\n", keypath);
        return;
    }
    memcpy(dir, keypath, n);
    dir[n] = '\0';
    for (n = 0; base[n] != '\0' && base[n] != '.' && n + 1 < sizeof name;
         n++)
        name[n] = (char)(base[n] >= 'a' && base[n] <= 'z' ?
                         base[n] - 'a' + 'A' : base[n]);
    name[n] = '\0';
    th_cur = th_known(name);
    if (th_cur == NULL)
        return;
    strcpy(th_curname, name);
    th_say("%s\n", th_curname);
    run_once(keypath, dir);
    if (th_cur->flags & SF_MONO) {
        vid_close();
        if (TF_EQ(vid_open(VID_F_MDA), 0)) {
            th_say("%s on a monochrome screen\n", th_curname);
            run_once(keypath, dir);
            vid_close();
        }
        TF_EQ(vid_open(0), 0);
    }
}

int main(int argc, char **argv)
{
    char prog[TH_PATHMAX];
    char key[TH_PATHMAX + 16];
    int a;
    int nkeys;
    int files;
    size_t n;
    u16 i;

    a = 1;
    nkeys = 0;
    tf_begin(th_name);
    cmem_init();
    tf_progdir(prog, (u16)sizeof prog, argv[0]);
    g_mdir = prog;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (strcmp(argv[a], "-u") == 0)
            g_update = 1;
        else if (strcmp(argv[a], "-v") == 0)
            g_verbose = 1;
        else if (strcmp(argv[a], "-g") == 0 && a + 1 < argc &&
                 strlen(argv[a + 1]) < TH_PATHMAX)
            g_gdir = argv[++a];
        else if (strcmp(argv[a], "-m") == 0 && a + 1 < argc &&
                 strlen(argv[a + 1]) < TH_PATHMAX)
            g_mdir = argv[++a];
        else if (strcmp(argv[a], "-p") == 0 && a + 1 < argc &&
                 strlen(argv[a + 1]) < TH_PATHMAX)
            g_pdir = argv[++a];
        else
            break;
    }
    if (a >= argc || argv[a][0] == '-' || strlen(argv[a]) >= TH_PATHMAX) {
        printf("usage: %s [-u] [-v] [-g golddir] [-p filedir] [-m msgdir] "
               "dir [file...]\n", th_name);
        return 2;
    }
    if (strlen(g_mdir) > LINK_MAXDIR) {
        th_say("message directory %s is longer than %u "
            "characters\n", g_mdir, (unsigned)LINK_MAXDIR);
        return 2;
    }
    strcpy(th_ddir, argv[a++]);
    strcpy(scr_cfg.itemord, "ID");
    th_setup();
    tc_clock(&th_clk);
    for (i = 0; th_memfaults[i].name != NULL; i++)
        th_known(th_memfaults[i].name);
    th_check_tables();
    if (!TF_EQ(msg_load(g_mdir, MSG_FILE), 0)) {
        th_say("%s\n", msg_why());
        return tf_done();
    }
    tui_set_helpdir(g_mdir);
    if (!TF_EQ(vid_open(0), 0))
        return tf_done();
    tui_set_exit(exit_run);
    files = argc - a;
    for (; a < argc; a++) {
        n = strlen(argv[a]);
        if (n > 4 && n < TH_PATHMAX &&
            (strcmp(argv[a] + n - 4, ".KEY") == 0 ||
             strcmp(argv[a] + n - 4, ".key") == 0)) {
            run_script(argv[a]);
            nkeys++;
        }
    }
    if (nkeys == 0 && files == 0)
        for (i = 1; i <= th_nscripts; i++) {
            strcpy(th_curname, th_name + 2);
            txt_u32(th_curname + strlen(th_curname), i);
            join(key, prog, th_curname, ".KEY");
            run_script(key);
        }
    TF_EQ(g_ncmp, g_update ? 0 : g_ngold);
    te_remove(th_ddir, NULL);
    vid_close();
    msg_free();
    return tf_done();
}
