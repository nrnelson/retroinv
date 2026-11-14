/*
 * TESTFW.H - minimal unit-test support for the T_*.C programs.
 *
 * The helpers are defined here as ordinary external functions, so a test
 * program includes this header plainly in exactly one source file; a
 * test split over several files defines TF_PART before including it in
 * the others, which then get only the declarations.  A test calls
 * the TF_ macros, and ends main with "return tf_done();".  Each failed
 * check prints the file (under TF_TERSE, the file's TF_FILE tag or the
 * test's name), line and expression; tf_done prints a summary line
 * "<name> PASS n" or "<name> FAIL n of m" and returns 0 or 1, so the
 * same program works as a host test and as a DOS test (errorlevel).
 * Opt-in sections serve the client tests: TF_KEYSRC (a scripted key
 * source and the shadow screen read back), TF_CLOCK (the fixed clock of
 * the engine), TF_LINK (requests of the test itself over the engine;
 * it implies TF_CLOCK), TF_MALLOC (malloc counted and made to fail, on
 * the host) and TF_CMPFILE (a file compared with its golden through
 * cf_read).  A test defines the macro before the include, and a test
 * without it compiles none of that section.  Built as a Windows program
 * (COMPAT_WIN16), a test runs from the WinMain of TEST/WTEST.C, below.
 * Strict C89: no variadic macros, no 64-bit types.
 */
#ifndef TESTFW_H
#define TESTFW_H

#ifdef TF_LINK
#ifndef TF_CLOCK
#define TF_CLOCK
#endif
#endif

#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#ifdef TF_KEYSRC
#include <setjmp.h>
#endif
#ifdef TF_CLOCK
#include "LINK.H"
#endif
#ifdef TF_MALLOC
#ifdef COMPAT_HOST
#include <stdlib.h>
#endif
#endif
#ifdef TF_LINK
#include "RQ.H"
#endif

/* Under Windows there is no console, and the C library's printf and
   sprintf import a function Windows 3.0 lacks.  The test's main is
   then tf_main, which the WinMain of TEST/WTEST.C calls, and its printf
   and sprintf, and those below, are tf_printf and tf_sprintf of
   WTEST.C: tf_printf writes the lines printed into a file named after
   the program, as WTEST.C says, and tf_sprintf into its buffer.
   WinMain first sets the program's instance, dos_inst of COMPAT.H,
   which a test that makes a window takes and with which the engine
   reads its schema data from the program's resource (CSCHEMA.C). */
#ifdef COMPAT_WIN16
#define main            tf_main
#define printf          tf_printf
#define sprintf         tf_sprintf
int tf_main(int argc, char **argv);
int tf_printf(const char *fmt, ...);
int tf_sprintf(char *out, const char *fmt, ...);
#endif

/* Count one check and report it when it failed, naming file, the source
   file (NULL: the test's name from tf_begin), and the line. */
int tf_check(int ok, const char *file, int line, const char *what);
int tf_check_long(long got, long want, const char *file, int line,
                  const char *what);
int tf_check_str(const char *got, const char *want, const char *file,
                 int line, const char *what);

/* The directory part of argv0 (up to and including its last '\\', '/'
   or ':') into out of outsz bytes (at least 1); "" when argv0 has none
   or it does not fit. */
void tf_progdir(char *out, u16 outsz, const char *argv0);

/* 1 when s starts with p, else 0; p is evaluated twice.  A macro, so
   a test that does not use it costs the compiler nothing. */
#define tf_starts(s, p) (strncmp((s), (p), strlen(p)) == 0)

#ifndef TF_PART
const char *tf_name = "TEST";
unsigned long tf_checks = 0;
unsigned long tf_fails = 0;

void tf_begin(const char *name)
{
    tf_name = name;
}

int tf_check(int ok, const char *file, int line, const char *what)
{
    tf_checks++;
    if (!ok) {
        tf_fails++;
        printf("%s:%d: check failed: %s\n", file != NULL ? file : tf_name,
               line, what);
    }
    return ok;
}

int tf_check_long(long got, long want, const char *file, int line,
                  const char *what)
{
    tf_checks++;
    if (got != want) {
        tf_fails++;
        printf("%s:%d: %s: got %ld, want %ld\n",
               file != NULL ? file : tf_name, line, what, got, want);
        return 0;
    }
    return 1;
}

int tf_check_str(const char *got, const char *want, const char *file,
                 int line, const char *what)
{
    tf_checks++;
    if (strcmp(got, want) != 0) {
        tf_fails++;
        printf("%s:%d: %s: got \"%s\", want \"%s\"\n",
               file != NULL ? file : tf_name, line, what, got, want);
        return 0;
    }
    return 1;
}

int tf_done(void)
{
    if (tf_fails == 0) {
        printf("%s PASS %lu\n", tf_name, tf_checks);
        return 0;
    }
    printf("%s FAIL %lu of %lu\n", tf_name, tf_fails, tf_checks);
    return 1;
}

void tf_progdir(char *out, u16 outsz, const char *argv0)
{
    size_t n;

    n = strlen(argv0);
    while (n > 0 && argv0[n - 1] != '\\' && argv0[n - 1] != '/' &&
           argv0[n - 1] != ':')
        n--;
    if (n >= outsz)
        n = 0;
    memcpy(out, argv0, n);
    out[n] = '\0';
}
#endif

/* Each macro evaluates to nonzero when the check passed.  Defining
   TF_TERSE (the Watcom C 6.5 build does) leaves out the file name and
   the expression text: a compiler that keeps each string literal of a
   file in memory while compiling it runs out of room on a test with
   hundreds of checks.  A failure then reports, in place of the file,
   TF_FILE when the source file defines it before the include (a short
   tag such as "T_ENGIN2", which tells apart the files of a split test;
   it is kept once, in tf_file, for all the checks) or else the test's
   name, with the line and the values.  Without TF_TERSE the file is
   always __FILE__. */
#ifdef TF_TERSE
#ifdef TF_FILE
static const char tf_file[] = TF_FILE;
#undef TF_FILE
#define TF_FILE tf_file
#else
#define TF_FILE NULL
#endif
#define TF_TEXT(e) ""
#else
#undef TF_FILE
#define TF_FILE __FILE__
#define TF_TEXT(e) #e
#endif
#define TF_CHECK(e)       tf_check((e) ? 1 : 0, TF_FILE, __LINE__, TF_TEXT(e))
#define TF_EQ(got, want)  tf_check_long((long)(got), (long)(want), \
                                        TF_FILE, __LINE__, TF_TEXT(got))
#define TF_STREQ(got, want) tf_check_str((got), (want), \
                                         TF_FILE, __LINE__, TF_TEXT(got))

/* ---------------------------------------------------------------------
 * TF_KEYSRC: keys from a script and the shadow screen read back.
 *
 * tk_feed installs a key source that gives the keys of a scenario one
 * at a time and then KEY_EOS, at which tui_key ends the program through
 * its exit function; none of them is typed ahead, so key_peek sees no
 * key.  tk_exit, installed with tui_set_exit, notes the status in
 * tk_status and returns to setjmp(tk_jb).  Defining TK_EXIT0 also
 * checks that every status is 0.  At the key numbered at (1-based) the
 * source calls tk_copy, when set, before giving the key, so a test can
 * copy the screen as it was when that key was asked for.  The keys are
 * copied: at most TK_MAXKEYS of them (defined before the include to
 * change it); a longer list fails a check and the rest are dropped.
 * --------------------------------------------------------------------- */
#ifdef TF_KEYSRC
#ifndef TK_MAXKEYS
#define TK_MAXKEYS 64
#endif

/* Install the keys k, a list ended by KEY_NONE, with the copy at key
   at (0: none); a check fails when k holds more than TK_MAXKEYS. */
void tk_feed(const u16 *k, u16 at);

/* How many keys the source has given since tk_feed. */
u16 tk_pos(void);

/* Row r of the screen now, trailing spaces removed, in a buffer that
   the next call overwrites. */
const char *tk_row(u8 r);

/* The attribute of the cell at row r, column c of the screen now. */
u8 tk_attr(u8 r, u8 c);

/* The exit function for tui_set_exit: notes status in tk_status and
   leaves with longjmp(tk_jb, 1). */
void tk_exit(int status);

extern jmp_buf tk_jb;
extern int tk_status;                   /* the status of the last exit  */
extern void (*tk_copy)(void);           /* the copy at key at, or NULL  */

#ifndef TF_PART
jmp_buf tk_jb;
int tk_status;
void (*tk_copy)(void);
static u16 tk_keys[TK_MAXKEYS];
static u16 tk_nkeys;
static u16 tk_kpos;
static u16 tk_at;

static u16 tk_src(void *arg, int peek)
{
    TF_CHECK(arg == (void *)tk_keys);
    if (peek)
        return KEY_NONE;
    if (tk_kpos >= tk_nkeys)
        return KEY_EOS;
    tk_kpos++;
    if (tk_kpos == tk_at && tk_copy != NULL)
        (*tk_copy)();
    return tk_keys[tk_kpos - 1];
}

void tk_feed(const u16 *k, u16 at)
{
    tk_nkeys = 0;
    while (tk_nkeys < TK_MAXKEYS && k[tk_nkeys] != KEY_NONE) {
        tk_keys[tk_nkeys] = k[tk_nkeys];
        tk_nkeys++;
    }
    TF_CHECK(k[tk_nkeys] == KEY_NONE);
    tk_kpos = 0;
    tk_at = at;
    key_set_source(tk_src, tk_keys);
}

u16 tk_pos(void)
{
    return tk_kpos;
}

const char *tk_row(u8 r)
{
    static char t[VID_COLS + 1];
    u8 cells[VID_ROWBYTES];
    int c;

    vid_getrow(r, cells);
    for (c = 0; c < VID_COLS; c++)
        t[c] = (char)cells[2 * c];
    t[VID_COLS] = '\0';
    for (c = VID_COLS; c > 0 && t[c - 1] == ' '; c--)
        t[c - 1] = '\0';
    return t;
}

u8 tk_attr(u8 r, u8 c)
{
    u8 cells[VID_ROWBYTES];

    vid_getrow(r, cells);
    return cells[2 * c + 1];
}

void tk_exit(int status)
{
#ifdef TK_EXIT0
    TF_EQ(status, 0);
#endif
    tk_status = status;
    longjmp(tk_jb, 1);
}
#endif
#endif

/* ---------------------------------------------------------------------
 * TF_MALLOC: near allocations counted and made to fail on the host.
 *
 * The host test links with -Wl,--wrap=malloc,--wrap=free (MALLOC_TESTS
 * in SRCS.MK), so every malloc and free call of its own modules comes
 * here and goes on to the C library's.  realloc and calloc are not
 * wrapped, so the modules under test must not use them: free would
 * misread their blocks.  tm_fail counts the malloc calls from then on
 * and makes one of them return NULL; tm_held tells how many blocks are
 * given out and not yet freed, so a test can see that a failed
 * constructor leaves none behind; tm_expect_wiped and tm_wiped tell
 * whether a secret was wiped before its block was freed; tm_has tells
 * whether some block given out and not yet freed holds a text.  Each
 * block carries its size and its place in the list of blocks given out
 * in TM_HDR bytes before it, which keeps the alignment of the C
 * library's blocks, and every byte of the block starts as
 * TM_FILL, so code that counts on a new block being zero, as a fresh one
 * from the host's heap often is, fails here.  The DOS builds cannot count
 * blocks: there tm_held is 0 and TM_HELD checks nothing.
 * --------------------------------------------------------------------- */
#ifdef TF_MALLOC
#ifdef COMPAT_HOST
#define TM_HDR          32      /* bytes before a block: tm_hdr         */
#define TM_FILL         0xA5    /* every byte of a new block            */

/* Check that tm_held is n; nonzero when it is. */
#define TM_HELD(n)      TF_EQ(tm_held(), n)

/* Count malloc calls from now on, the nth of them failing (1 the next
   one, 0 none). */
void tm_fail(u16 nth);

/* malloc calls since tm_fail. */
u16 tm_calls(void);

/* Blocks malloc has given out and free has not taken back. */
u16 tm_held(void);

/* Watch the n bytes at p, which lie inside a block malloc gave out,
   until free takes that block back. */
void tm_expect_wiped(const void *p, u16 n);

/* 1 when the block holding the bytes tm_expect_wiped named has been
   freed since, every one of them zero; else 0.  It ends the watch. */
int tm_wiped(void);

/* 1 when some block malloc has given out and free has not taken back
   holds the n bytes at p, the cmem_ blocks of the host among them. */
int tm_has(const void *p, u16 n);

void *__real_malloc(size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n);
void __wrap_free(void *p);

#ifndef TF_PART
typedef struct tm_hdr {
    struct tm_hdr *prev;
    struct tm_hdr *next;
    size_t n;
} tm_hdr;

/* A tm_hdr fits in the TM_HDR bytes before a block. */
extern char tm_hdr_ok[1];
extern char tm_hdr_ok[(sizeof(tm_hdr) <= TM_HDR) ? 1 : 2];

static tm_hdr *tm_list;                 /* the blocks given out         */
static u16 tm_nth;
static u16 tm_n;
static u16 tm_nheld;
static const char *tm_wp;               /* the bytes watched, or NULL   */
static u16 tm_wn;
static int tm_wok;                      /* freed, every byte zero       */

void tm_fail(u16 nth)
{
    tm_nth = nth;
    tm_n = 0;
}

u16 tm_calls(void)
{
    return tm_n;
}

u16 tm_held(void)
{
    return tm_nheld;
}

void tm_expect_wiped(const void *p, u16 n)
{
    tm_wp = (const char *)p;
    tm_wn = n;
    tm_wok = 0;
}

int tm_wiped(void)
{
    int ok;

    ok = tm_wok;
    tm_wp = NULL;
    tm_wok = 0;
    return ok;
}

int tm_has(const void *p, u16 n)
{
    const tm_hdr *h;
    const char *b;
    size_t i;

    for (h = tm_list; h != NULL; h = h->next) {
        b = (const char *)h + TM_HDR;
        for (i = 0; i + n <= h->n; i++)
            if (memcmp(b + i, p, n) == 0)
                return 1;
    }
    return 0;
}

void *__wrap_malloc(size_t n)
{
    char *b;

    tm_n++;
    if (tm_nth != 0 && tm_n == tm_nth)
        return NULL;
    b = (char *)__real_malloc(TM_HDR + n);
    if (b == NULL)
        return NULL;
    ((tm_hdr *)b)->n = n;
    ((tm_hdr *)b)->prev = NULL;
    ((tm_hdr *)b)->next = tm_list;
    if (tm_list != NULL)
        tm_list->prev = (tm_hdr *)b;
    tm_list = (tm_hdr *)b;
    memset(b + TM_HDR, TM_FILL, n);
    tm_nheld++;
    return b + TM_HDR;
}

void __wrap_free(void *p)
{
    char *b;
    tm_hdr *h;
    size_t n;
    u16 i;

    if (p == NULL)
        return;
    b = (char *)p;
    h = (tm_hdr *)(b - TM_HDR);
    n = h->n;
    if (h->prev != NULL)
        h->prev->next = h->next;
    else
        tm_list = h->next;
    if (h->next != NULL)
        h->next->prev = h->prev;
    if (tm_wp != NULL && tm_wp >= b && (size_t)(tm_wp - b) + tm_wn <= n) {
        for (i = 0; i < tm_wn && tm_wp[i] == 0; i++)
            ;
        tm_wok = i == tm_wn;
        tm_wp = NULL;
    }
    tm_nheld--;
    __real_free(b - TM_HDR);
}
#endif
#else
/* What TM_HELD gives in place of a check: 1 when n is 0, as every
   count of tm_held is in these builds. */
int tm_nocheck(u16 n);

#define tm_held()       0
#define TM_HELD(n)      tm_nocheck(n)

#ifndef TF_PART
int tm_nocheck(u16 n)
{
    return n == 0;
}
#endif
#endif
#endif

/* ---------------------------------------------------------------------
 * TF_CMPFILE: an output file compared with its golden, through the cf_
 * calls of CDOS.C, which the test links.
 * --------------------------------------------------------------------- */
#ifdef TF_CMPFILE
/* 1 when the files path and gold hold the same bytes; else 0, and a
   line saying which file cannot be read or at which byte they first
   differ.  A test counts it as one check: TF_CHECK(tf_cmpfile(..)). */
int tf_cmpfile(const char *path, const char *gold);

#ifndef TF_PART
int tf_cmpfile(const char *path, const char *gold)
{
    char a[128];
    char b[128];
    int ha;
    int hb;
    u16 na;
    u16 nb;
    u16 i;
    u32 at;
    int ok;

    at = 0;
    ok = 0;
    if (cf_open(path, CF_READ, &ha) != 0) {
        printf("  cannot read %s\n", path);
        return 0;
    }
    if (cf_open(gold, CF_READ, &hb) != 0) {
        cf_close(ha);
        printf("  cannot read %s\n", gold);
        return 0;
    }
    for (;;) {
        if (cf_read(ha, a, (u16)sizeof a, &na) != 0 ||
            cf_read(hb, b, (u16)sizeof b, &nb) != 0) {
            printf("  cannot read %s or %s\n", path, gold);
            break;
        }
        i = 0;
        while (i < na && i < nb && a[i] == b[i])
            i++;
        at += i;
        if (i < na || i < nb) {
            printf("  %s differs from %s at byte %lu\n", path, gold,
                   (unsigned long)at);
            break;
        }
        if (na == 0) {
            ok = 1;
            break;
        }
    }
    cf_close(ha);
    cf_close(hb);
    return ok;
}
#endif
#endif

/* ---------------------------------------------------------------------
 * TF_CLOCK: the fixed clock of the client tests.
 * --------------------------------------------------------------------- */
#ifdef TF_CLOCK
/* Fill c with 2026-10-08 12:00:00. */
void tc_clock(link_clock *c);

#ifndef TF_PART
void tc_clock(link_clock *c)
{
    c->year = 2026;
    c->month = 10;
    c->day = 8;
    c->hour = 12;
    c->minute = 0;
    c->second = 0;
}
#endif
#endif

/* ---------------------------------------------------------------------
 * TF_LINK: the test's own requests over the link tc_lk, which the test
 * opens (link_open_local) with the clock of tc_clock.
 * --------------------------------------------------------------------- */
#ifdef TF_LINK
/* Send the request words over tc_lk; the rq_send code. */
int tc_send(const char *line);

/* Say HELLO, log in as ADMIN, set its password and add the top-level
   shelf A1 (named and coded A1, LOC_ID 1), checking each answer is
   RQ_OK; 1 when all four were. */
int tc_admin(void);

extern link *tc_lk;

#ifndef TF_PART
link *tc_lk;

int tc_send(const char *line)
{
    rq q;
    rq_res res;

    rq_begin(&q, line);
    return rq_send(tc_lk, &q, NULL, NULL, &res);
}

int tc_admin(void)
{
    int ok;

    ok = TF_EQ(tc_send("HELLO TEST"), RQ_OK);
    ok &= TF_EQ(tc_send("LOGIN ADMIN admin"), RQ_OK);
    ok &= TF_EQ(tc_send("PASSWD admin secret1"), RQ_OK);
    ok &= TF_EQ(tc_send("LOC ADD LOC_NAME=A1 LTYPE=SHELF LOC_CODE=A1"),
                RQ_OK);
    return ok;
}
#endif
#endif

#endif
