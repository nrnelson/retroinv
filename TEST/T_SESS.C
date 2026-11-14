/*
 * T_SESS.C - the session steps of the client (SRC/CLIENT/SESS.C) over
 * the engine and over canned responses.
 *
 * usage: T_SESS dir
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/SDATA/") holding
 * nothing but this test's files; its engine files are removed first and
 * last.  The test links the recording link of TRLINK.H instead of
 * LINK.C, so the request lines are checked exactly and a chosen
 * request can be answered with a response of the test's own.  It is
 * written from the contract in SESS.H and knows nothing of how the
 * steps are done.
 *
 * Covered:
 * - sn_station (the order of choice, the cut to LINK_STALEN, the fallback
 *   reported);
 * - sn_hello (VER asked for only while ver is empty, a failure of either
 *   request, ver left alone, a HELLO refused as unknown sent once more
 *   and no more);
 * - sn_login on every outcome (SN_IN, SN_MUSTCHG, SN_REFUSED, SN_FAILED,
 *   SN_TOOMANY, SN_ENDED), from canned responses and from the engine, the
 *   LEVEL= rule with bad values, the name, level and name left alone on a
 *   failure, the password buffer wiped whole on every outcome;
 * - sn_passwd (the local checks in their order with nothing sent and res
 *   left alone, then SN_PW_DONE, SN_PW_OLDWRONG and SN_PW_FAILED, the three
 *   buffers unchanged, a wrong old password never counted as a failed
 *   login);
 * - sn_status (the counts, "?" for missing ones, a failure);
 * - the exact request lines.
 *
 * Exit status 0 when every check passed, else 1.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "CFG.H"
#include "SESS.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_FILE "T_SESS"
#define TF_LINK
#include "TESTFW.H"

#define PATHMAX         96
#define PADCH           'Z'     /* fills every buffer past its string   */
#define OLDLEVEL        9       /* a level no login gives               */
#define PWBUF           32      /* room for the longest test password   */

static char g_ddir[PATHMAX];
static link_clock g_clk;
static link *g_lk;

/* Close the link, if any, and open a fresh session on the data
   directory; the log and any canned response start afresh. */
static void reopen(void)
{
    char why[LINK_WHYSZ];

    if (g_lk != NULL)
        TF_EQ(link_close(g_lk), 0);
    g_lk = NULL;
    TF_EQ(link_open_local(&g_lk, g_ddir, &g_clk, why, sizeof why), 0);
    tc_lk = g_lk;
    tl_clear();
}

/* Fill a password buffer of SN_PWSZ bytes: pw, then padding. */
static void pwfill(char *buf, const char *pw)
{
    memset(buf, PADCH, SN_PWSZ);
    strcpy(buf, pw);
}

/* 1 when every one of the SN_PWSZ bytes is zero. */
static int pwwiped(const char *buf)
{
    u16 i;

    for (i = 0; i < SN_PWSZ; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
}

/* ---------------------------------------------------------------------
 * sn_station.
 * --------------------------------------------------------------------- */
static void test_station(void)
{
    char out[LINK_STALEN + 2];
    char mach[DOS_MACHLEN + 1];
    const char *want;
    const char *l20;
    const char *l16;

    l20 = "ABCDEFGHIJKLMNOPQRST";
    l16 = "ABCDEFGHIJKLMNOP";
    memset(out, PADCH, sizeof out);
    TF_EQ(sn_station(out, "NODE1", "CFGST"), 0);
    TF_STREQ(out, "NODE1");
    TF_EQ(sn_station(out, "NODE1", ""), 0);
    TF_STREQ(out, "NODE1");
    TF_EQ(sn_station(out, NULL, "CFGST"), 0);
    TF_STREQ(out, "CFGST");

    /* A name given as CFG_DEFSTATION is no fallback. */
    TF_EQ(sn_station(out, NULL, CFG_DEFSTATION), 0);
    TF_STREQ(out, CFG_DEFSTATION);

    /* Neither: the DOS machine name when there is a nonempty one, else
       the fallback. */
    want = CFG_DEFSTATION;
    if (dos_machine_name(mach) == 0 && mach[0] != '\0')
        want = mach;
    TF_EQ(sn_station(out, NULL, ""), want != mach);
    TF_STREQ(out, want);

    /* The cut: LINK_STALEN characters, the byte after them untouched. */
    TF_EQ(strlen(l16), LINK_STALEN);
    memset(out, PADCH, sizeof out);
    sn_station(out, l20, "CFGST");
    TF_EQ(strlen(out), LINK_STALEN);
    TF_CHECK(memcmp(out, l20, LINK_STALEN) == 0);
    TF_EQ(out[LINK_STALEN + 1], PADCH);
    memset(out, PADCH, sizeof out);
    sn_station(out, NULL, l20);
    TF_STREQ(out, l16);
    TF_EQ(out[LINK_STALEN + 1], PADCH);
    memset(out, PADCH, sizeof out);
    sn_station(out, l16, "");
    TF_STREQ(out, l16);
    memset(out, PADCH, sizeof out);
    sn_station(out, NULL, l16);
    TF_STREQ(out, l16);
}

/* ---------------------------------------------------------------------
 * sn_hello.
 * --------------------------------------------------------------------- */
static const char k_unknown[] = "-ERR 101 unknown command";

/* tl_hook: every HELLO refused as unknown, as a line garbled each time. */
static void garble_hello(const char *line, u16 len)
{
    if (len >= 5 && memcmp(line, "HELLO", 5) == 0)
        tl_fail(tl_count(), k_unknown);
}

static void test_hello(void)
{
    char ver[RQ_TEXTLEN + 1];
    rq_res res;

    reopen();
    ver[0] = '\0';
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_OK);
    TF_EQ(res.code, RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_log(), "HELLO ST1\nVER\n");
    TF_CHECK(tf_starts(ver, "RETROINV "));
    TF_CHECK(strlen(ver) > 9);

    /* Asked for once: a ver that is not empty stays. */
    strcpy(ver, "KEEP 1");
    tl_clear();
    TF_EQ(sn_hello(g_lk, "ST2", ver, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_log(), "HELLO ST2\n");
    TF_STREQ(ver, "KEEP 1");

    /* The text of VER's +OK goes into ver. */
    tl_clear();
    tl_fail(2, "+OK RETROINV 9.8.7");
    ver[0] = '\0';
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_OK);
    TF_STREQ(ver, "RETROINV 9.8.7");

    /* HELLO fails: VER is not sent, ver stays. */
    tl_clear();
    tl_fail(1, "-ERR 102 bad station");
    ver[0] = '\0';
    memset(&res, 0, sizeof res);
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_EARG);
    TF_EQ(res.code, RQ_EARG);
    TF_STREQ(res.text, "bad station");
    TF_EQ(tl_count(), 1);
    TF_STREQ(ver, "");
    strcpy(ver, "KEEP 2");
    tl_clear();
    tl_fail(1, "-ERR 500 trouble");
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_EINTERNAL);
    TF_STREQ(ver, "KEEP 2");

    /* A HELLO refused as unknown came garbled: it goes once more. */
    tl_clear();
    tl_fail(1, k_unknown);
    ver[0] = '\0';
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_OK);
    TF_STREQ(tl_log(), "HELLO ST1\nHELLO ST1\nVER\n");
    TF_CHECK(tf_starts(ver, "RETROINV "));

    /* Refused again: no third HELLO, no VER, ver unchanged. */
    tl_clear();
    tl_hook = garble_hello;
    ver[0] = '\0';
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_EUNKNOWN);
    tl_hook = NULL;
    TF_EQ(res.code, RQ_EUNKNOWN);
    TF_STREQ(tl_log(), "HELLO ST1\nHELLO ST1\n");
    TF_STREQ(ver, "");

    /* VER fails: its code, ver unchanged. */
    tl_clear();
    tl_fail(2, "-ERR 300 disk");
    ver[0] = '\0';
    memset(&res, 0, sizeof res);
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_EIO);
    TF_EQ(res.code, RQ_EIO);
    TF_STREQ(res.text, "disk");
    TF_EQ(tl_count(), 2);
    TF_STREQ(ver, "");

    /* The session ended: the code is the closed one. */
    tl_clear();
    tl_fail(1, NULL);
    ver[0] = '\0';
    TF_CHECK(sn_hello(g_lk, "ST1", ver, &res) != RQ_OK);
    TF_EQ(res.closed, 1);
    TF_STREQ(ver, "");
}

/* ---------------------------------------------------------------------
 * sn_login from canned responses.
 * --------------------------------------------------------------------- */
typedef struct {
    const char *resp;           /* the response; NULL ends the session  */
    int want;                   /* the outcome                          */
    int code;                   /* res.code                             */
    u8 level;                   /* *level after, when it is set         */
    const char *name;           /* name after, when it is set           */
} lcase;

static const lcase g_login[] = {
    /* Logged in, the level and the full name. */
    { "+OK LEVEL=2 NAME=\"Ann Lee\"", SN_IN, RQ_OK, 2, "Ann Lee" },
    { "+OK LEVEL=1 NAME=Ann", SN_IN, RQ_OK, 1, "Ann" },
    { "+OK LEVEL=3 NAME=\"A \\\"B\\\" C\"", SN_IN, RQ_OK, 3, "A \"B\" C" },
    { "+OK LEVEL=3 NAME=\"123456789012345678901234567890\"", SN_IN, RQ_OK, 3,
      "123456789012345678901234567890" },
    { "+OK LEVEL=2", SN_IN, RQ_OK, 2, "" },
    { "+OK NAME=\"Q\" LEVEL=3", SN_IN, RQ_OK, 3, "Q" },
    /* A LEVEL= that is not one digit 1..3 is SN_LVLMIN. */
    { "+OK LEVEL=0 NAME=\"Z\"", SN_IN, RQ_OK, SN_LVLMIN, "Z" },
    { "+OK LEVEL=4", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=9", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=12", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=02", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=2x", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=x", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=-1", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK LEVEL=\"3\"", SN_IN, RQ_OK, 3, "" },
    { "+OK", SN_IN, RQ_OK, SN_LVLMIN, "" },
    { "+OK NAME=\"Q\"", SN_IN, RQ_OK, SN_LVLMIN, "Q" },
    /* The password must be changed first: the name is "". */
    { "+OK LEVEL=3 MUSTCHG=1", SN_MUSTCHG, RQ_OK, 3, "" },
    { "+OK LEVEL=2 MUSTCHG=1", SN_MUSTCHG, RQ_OK, 2, "" },
    { "+OK MUSTCHG=1 LEVEL=1", SN_MUSTCHG, RQ_OK, 1, "" },
    { "+OK LEVEL=7 MUSTCHG=1", SN_MUSTCHG, RQ_OK, SN_LVLMIN, "" },
    { "+OK MUSTCHG=1", SN_MUSTCHG, RQ_OK, SN_LVLMIN, "" },
    /* The session open: refused, or another failure. */
    { "-ERR 401 bad", SN_REFUSED, RQ_EBADPW, 0, NULL },
    { "-ERR 403 off", SN_REFUSED, RQ_EDISABLED, 0, NULL },
    { "-ERR 100 syntax", SN_FAILED, RQ_ESYNTAX, 0, NULL },
    { "-ERR 102 FIELD=USER", SN_FAILED, RQ_EARG, 0, NULL },
    { "-ERR 300 disk", SN_FAILED, RQ_EIO, 0, NULL },
    { "-ERR 400 hello", SN_FAILED, RQ_ENOLOGIN, 0, NULL },
    { "-ERR 404 pw", SN_FAILED, RQ_EMUSTCHG, 0, NULL },
    { "-ERR 500 oops", SN_FAILED, RQ_EINTERNAL, 0, NULL },
    /* The session ended by the response. */
    { "-ERR 401 bad\n", SN_TOOMANY, RQ_EBADPW, 0, NULL },
    { "-ERR 403 off\n", SN_TOOMANY, RQ_EDISABLED, 0, NULL },
    { "-ERR 501 down\n", SN_ENDED, RQ_ESHUTDOWN, 0, NULL },
    { "-ERR 500 oops\n", SN_ENDED, RQ_EINTERNAL, 0, NULL },
    { "-ERR 300 disk\n", SN_ENDED, RQ_EIO, 0, NULL },
    { NULL, SN_ENDED, -1, 0, NULL }
};

static void test_login_canned(void)
{
    char pw[SN_PWSZ];
    char name[LINK_FULLNAMELEN + 1];
    rq_res res;
    u8 level;
    u16 i;
    int rc;
    int set;

    for (i = 0; i < sizeof g_login / sizeof g_login[0]; i++) {
        reopen();
        tl_fail(1, g_login[i].resp);
        pwfill(pw, "pw1234");
        level = OLDLEVEL;
        strcpy(name, "old");
        memset(&res, 0x5A, sizeof res);
        rc = sn_login(g_lk, "ADMIN", pw, &level, name, &res);
        TF_EQ(rc, g_login[i].want);
        TF_EQ(tl_count(), 1);
        TF_STREQ(tl_req(0), "LOGIN ADMIN pw1234");
        if (g_login[i].code >= 0)
            TF_EQ(res.code, g_login[i].code);
        else
            TF_CHECK(res.code != RQ_OK && res.closed);
        TF_CHECK(pwwiped(pw));
        set = g_login[i].want == SN_IN || g_login[i].want == SN_MUSTCHG;
        if (set) {
            TF_EQ(level, g_login[i].level);
            TF_STREQ(name, g_login[i].name);
        } else {
            TF_EQ(level, OLDLEVEL);
            TF_STREQ(name, "old");
        }
        if (g_login[i].want != SN_IN && g_login[i].want != SN_MUSTCHG)
            TF_CHECK(res.code != RQ_OK);
    }
}

/* The text of the response is in res, and no part of the request. */
static void test_login_text(void)
{
    char pw[SN_PWSZ];
    char name[LINK_FULLNAMELEN + 1];
    rq_res res;
    u8 level;

    reopen();
    tl_fail(1, "-ERR 401 wrong user or password");
    pwfill(pw, "hunter22");
    TF_EQ(sn_login(g_lk, "BOB", pw, &level, name, &res), SN_REFUSED);
    TF_STREQ(res.text, "wrong user or password");
    TF_STREQ(tl_req(0), "LOGIN BOB hunter22");
    TF_CHECK(strstr(res.text, "hunter22") == NULL);

    /* A password of the full length: every byte of the buffer wiped. */
    reopen();
    tl_fail(1, "+OK LEVEL=1");
    pwfill(pw, "0123456789ABCDEF");
    TF_EQ(strlen(pw), LINK_PWMAX);
    TF_EQ(sn_login(g_lk, "BOB", pw, &level, name, &res), SN_IN);
    TF_STREQ(tl_req(0), "LOGIN BOB 0123456789ABCDEF");
    TF_CHECK(pwwiped(pw));
}

/* ---------------------------------------------------------------------
 * sn_passwd.
 * --------------------------------------------------------------------- */
typedef struct {
    const char *old;
    const char *nw;
    const char *rep;
    int want;
} pcase;

static const pcase g_pwlocal[] = {
    /* A repetition that differs comes first, whatever else is wrong. */
    { "secret1", "newpw1", "newpw2", SN_PW_NOMATCH },
    { "secret1", "newpw1", "", SN_PW_NOMATCH },
    { "secret1", "", "newpw1", SN_PW_NOMATCH },
    { "a", "bb", "ccc", SN_PW_NOMATCH },
    { "abcd", "abcd", "abcx", SN_PW_NOMATCH },
    { "abcd", "abcd", "ABCD", SN_PW_NOMATCH },
    { "abcd", "abc", "abcd", SN_PW_NOMATCH },
    /* An invalid new password, before it is compared with the old. */
    { "secret1", "", "", SN_PW_INVALID },
    { "secret1", "a", "a", SN_PW_INVALID },
    { "secret1", "abc", "abc", SN_PW_INVALID },
    { "secret1", "0123456789ABCDEFG", "0123456789ABCDEFG", SN_PW_INVALID },
    { "secret1", "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz",
      SN_PW_INVALID },
    { "secret1", "ab cd", "ab cd", SN_PW_INVALID },
    { "secret1", " abcd", " abcd", SN_PW_INVALID },
    { "secret1", "abcd ", "abcd ", SN_PW_INVALID },
    { "secret1", "ab\tcd", "ab\tcd", SN_PW_INVALID },
    { "secret1", "ab\x1F" "cd", "ab\x1F" "cd", SN_PW_INVALID },
    { "secret1", "ab\x7F" "cd", "ab\x7F" "cd", SN_PW_INVALID },
    { "secret1", "ab\x80" "cd", "ab\x80" "cd", SN_PW_INVALID },
    { "secret1", "ab\xFF" "cd", "ab\xFF" "cd", SN_PW_INVALID },
    { "abc", "abc", "abc", SN_PW_INVALID },
    { "0123456789ABCDEFG", "0123456789ABCDEFG", "0123456789ABCDEFG",
      SN_PW_INVALID },
    { "ab cd", "ab cd", "ab cd", SN_PW_INVALID },
    /* The same as the old. */
    { "abcd", "abcd", "abcd", SN_PW_SAME },
    { "0123456789ABCDEF", "0123456789ABCDEF", "0123456789ABCDEF",
      SN_PW_SAME },
    { "!~~~", "!~~~", "!~~~", SN_PW_SAME }
};

static void test_passwd_local(void)
{
    rq_res res;
    rq_res before;
    char old[PWBUF];
    char nw[PWBUF];
    char rep[PWBUF];
    char o2[PWBUF];
    char n2[PWBUF];
    char r2[PWBUF];
    u16 i;
    int rc;

    reopen();
    for (i = 0; i < sizeof g_pwlocal / sizeof g_pwlocal[0]; i++) {
        memset(old, PADCH, sizeof old);
        memset(nw, PADCH, sizeof nw);
        memset(rep, PADCH, sizeof rep);
        strcpy(old, g_pwlocal[i].old);
        strcpy(nw, g_pwlocal[i].nw);
        strcpy(rep, g_pwlocal[i].rep);
        memcpy(o2, old, sizeof old);
        memcpy(n2, nw, sizeof nw);
        memcpy(r2, rep, sizeof rep);
        memset(&res, 0x5A, sizeof res);
        before = res;
        rc = sn_passwd(g_lk, old, nw, rep, &res);
        TF_EQ(rc, g_pwlocal[i].want);
        TF_EQ(tl_count(), 0);
        TF_CHECK(memcmp(&res, &before, sizeof res) == 0);
        TF_CHECK(memcmp(old, o2, sizeof old) == 0);
        TF_CHECK(memcmp(nw, n2, sizeof nw) == 0);
        TF_CHECK(memcmp(rep, r2, sizeof rep) == 0);
    }
}

typedef struct {
    const char *old;
    const char *nw;
    const char *resp;           /* NULL ends the session                */
    int want;
    int code;                   /* res.code, -1: some failure, closed   */
} qcase;

static const qcase g_pwsend[] = {
    /* The limits of a valid new password, and every printable byte. */
    { "x", "abcd", "+OK", SN_PW_DONE, RQ_OK },
    { "old1", "0123456789ABCDEF", "+OK", SN_PW_DONE, RQ_OK },
    { "old1", "!~~~", "+OK", SN_PW_DONE, RQ_OK },
    { "old1", "a\"b=c\\d", "+OK", SN_PW_DONE, RQ_OK },
    { "oldpw", "newpw", "+OK", SN_PW_DONE, RQ_OK },
    { "old1", "ABCD", "+OK", SN_PW_DONE, RQ_OK },
    /* The old password is wrong, the session open. */
    { "old1", "newpw", "-ERR 401 old password", SN_PW_OLDWRONG, RQ_EBADPW },
    /* Anything else. */
    { "old1", "newpw", "-ERR 401 old password\n", SN_PW_FAILED, RQ_EBADPW },
    { "old1", "newpw", "-ERR 403 off", SN_PW_FAILED, RQ_EDISABLED },
    { "old1", "newpw", "-ERR 102 FIELD=NEW", SN_PW_FAILED, RQ_EARG },
    { "old1", "newpw", "-ERR 202 same", SN_PW_FAILED, RQ_ECONSTR },
    { "old1", "newpw", "-ERR 300 disk", SN_PW_FAILED, RQ_EIO },
    { "old1", "newpw", "-ERR 400 login", SN_PW_FAILED, RQ_ENOLOGIN },
    { "old1", "newpw", "-ERR 500 oops", SN_PW_FAILED, RQ_EINTERNAL },
    { "old1", "newpw", "-ERR 501 down\n", SN_PW_FAILED, RQ_ESHUTDOWN },
    { "old1", "newpw", NULL, SN_PW_FAILED, -1 }
};

static void test_passwd_sent(void)
{
    rq_res res;
    char old[PWBUF];
    char nw[PWBUF];
    char o2[PWBUF];
    char n2[PWBUF];
    u16 i;
    int rc;

    for (i = 0; i < sizeof g_pwsend / sizeof g_pwsend[0]; i++) {
        reopen();
        tl_fail(1, g_pwsend[i].resp);
        memset(old, PADCH, sizeof old);
        memset(nw, PADCH, sizeof nw);
        strcpy(old, g_pwsend[i].old);
        strcpy(nw, g_pwsend[i].nw);
        memcpy(o2, old, sizeof old);
        memcpy(n2, nw, sizeof nw);
        memset(&res, 0x5A, sizeof res);
        rc = sn_passwd(g_lk, old, nw, nw, &res);
        TF_EQ(rc, g_pwsend[i].want);
        TF_EQ(tl_count(), 1);
        if (g_pwsend[i].code >= 0)
            TF_EQ(res.code, g_pwsend[i].code);
        else
            TF_CHECK(res.code != RQ_OK && res.closed);
        TF_CHECK(memcmp(old, o2, sizeof old) == 0);
        TF_CHECK(memcmp(nw, n2, sizeof nw) == 0);
        TF_CHECK(strncmp(tl_req(0), "PASSWD ", 7) == 0);
    }
    reopen();
    tl_fail(1, "+OK");
    TF_EQ(sn_passwd(g_lk, "oldpw", "newpw", "newpw", &res), SN_PW_DONE);
    TF_STREQ(tl_req(0), "PASSWD oldpw newpw");
}

/* ---------------------------------------------------------------------
 * sn_status from canned responses.
 * --------------------------------------------------------------------- */
typedef struct {
    const char *resp;
    int want;                   /* the return, -1: some failure         */
    const char *n[SN_NCOUNTS];
} scase;

static const scase g_status[] = {
    { "+DATA\nK ITEMS=12\nK LOCS=3\nK CATS=9\n.", RQ_OK, { "12", "3", "9" } },
    { "+DATA\nK CATS=9\nK ITEMS=12\nK LOCS=3\n.", RQ_OK, { "12", "3", "9" } },
    { "+DATA\nK TABLES=1\nK ITEMS=0\nK LOCS=0\nK SEQ=4\nK CATS=0\n"
      "K UPTIME=77\nK SESSIONS=2\n.", RQ_OK, { "0", "0", "0" } },
    { "+DATA\nK ITEMS=5\n.", RQ_OK, { "5", "?", "?" } },
    { "+DATA\nK LOCS=5\n.", RQ_OK, { "?", "5", "?" } },
    { "+DATA\nK CATS=5\nK UPTIME=1\n.", RQ_OK, { "?", "?", "5" } },
    { "+DATA\nK ITEMS=5\nK CATS=7\n.", RQ_OK, { "5", "?", "7" } },
    { "+DATA\n.", RQ_OK, { "?", "?", "?" } },
    { "+DATA\nK ITEMS=4294967295\nK LOCS=1\nK CATS=2\n.", RQ_OK,
      { "4294967295", "1", "2" } },
    { "+DATA\nK ITEMS=12345678901234\nK LOCS=1\nK CATS=2\n.", RQ_OK,
      { "1234567890", "1", "2" } },
    /* A failure: the counts read before it. */
    { "+DATA\nK ITEMS=5\nK LOCS=6\nK ERROR=300\n.", RQ_EIO,
      { "5", "6", "?" } },
    { "+DATA\nK ERROR=300\n.", RQ_EIO, { "?", "?", "?" } },
    { "-ERR 404 password", RQ_EMUSTCHG, { "?", "?", "?" } },
    { "-ERR 400 login", RQ_ENOLOGIN, { "?", "?", "?" } },
    { "-ERR 500 oops", RQ_EINTERNAL, { "?", "?", "?" } },
    { "-ERR 501 down\n", RQ_ESHUTDOWN, { "?", "?", "?" } },
    { NULL, -1, { "?", "?", "?" } }
};

static void test_status_canned(void)
{
    sn_counts c;
    rq_res res;
    u16 i;
    u16 k;
    int rc;

    for (i = 0; i < sizeof g_status / sizeof g_status[0]; i++) {
        reopen();
        tl_fail(1, g_status[i].resp);
        memset(&c, PADCH, sizeof c);
        memset(&res, 0x5A, sizeof res);
        rc = sn_status(g_lk, &c, &res);
        if (g_status[i].want >= 0) {
            TF_EQ(rc, g_status[i].want);
            TF_EQ(res.code, g_status[i].want);
        } else {
            TF_CHECK(rc != RQ_OK);
            TF_EQ(res.code, rc);
        }
        TF_EQ(tl_count(), 1);
        TF_STREQ(tl_req(0), "STATUS");
        for (k = 0; k < SN_NCOUNTS; k++)
            TF_STREQ(c.n[k], g_status[i].n[k]);
    }
    TF_EQ(SN_C_ITEMS, 0);
    TF_EQ(SN_C_LOCS, 1);
    TF_EQ(SN_C_CATS, 2);
}

/* ---------------------------------------------------------------------
 * The real engine: a whole session.
 * --------------------------------------------------------------------- */

/* sn_login as ADMIN with pw; the outcome, with level and name in the
   globals below. */
static u8 g_level;
static char g_name[LINK_FULLNAMELEN + 1];
static rq_res g_res;

static int login_as(const char *user, const char *pw)
{
    char buf[SN_PWSZ];
    int rc;

    pwfill(buf, pw);
    g_level = OLDLEVEL;
    strcpy(g_name, "old");
    rc = sn_login(g_lk, user, buf, &g_level, g_name, &g_res);
    TF_CHECK(pwwiped(buf));
    return rc;
}

/* Say HELLO again, which logs out; the version is known. */
static void hello_again(void)
{
    char ver[RQ_TEXTLEN + 1];
    rq_res res;

    strcpy(ver, "V");
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_OK);
}

static void test_engine_failures(void)
{
    /* LOGIN before HELLO: another failure, and not a failed login. */
    reopen();
    TF_EQ(login_as("ADMIN", "admin"), SN_FAILED);
    TF_EQ(g_res.code, RQ_ENOLOGIN);
    TF_EQ(g_level, OLDLEVEL);
    TF_STREQ(g_name, "old");
    TF_STREQ(tl_log(), "LOGIN ADMIN admin\n");

    /* Three failed logins end the session. */
    reopen();
    hello_again();
    TF_EQ(login_as("ADMIN", "wrong1"), SN_REFUSED);
    TF_EQ(g_res.code, RQ_EBADPW);
    TF_EQ(login_as("NOBODY", "wrong2"), SN_REFUSED);
    TF_EQ(g_res.code, RQ_EBADPW);
    TF_EQ(login_as("ADMIN", "wrong3"), SN_TOOMANY);
    TF_EQ(g_res.code, RQ_EBADPW);
    TF_EQ(g_level, OLDLEVEL);
    tl_clear();
    TF_EQ(login_as("ADMIN", "admin"), SN_ENDED);
    TF_EQ(g_res.code, RQ_ECLOSED);
    TF_EQ(g_res.closed, 1);

    /* A success resets the count. */
    reopen();
    hello_again();
    TF_EQ(login_as("ADMIN", "wrong1"), SN_REFUSED);
    TF_EQ(login_as("ADMIN", "wrong2"), SN_REFUSED);
    TF_EQ(login_as("ADMIN", "admin"), SN_MUSTCHG);
    TF_EQ(login_as("ADMIN", "wrong1"), SN_REFUSED);
    TF_EQ(login_as("ADMIN", "wrong2"), SN_REFUSED);
    TF_EQ(login_as("ADMIN", "admin"), SN_MUSTCHG);

    /* The session ended before. */
    reopen();
    TF_EQ(tc_send("BYE"), RQ_OK);
    tl_clear();
    g_level = OLDLEVEL;
    TF_EQ(login_as("ADMIN", "admin"), SN_ENDED);
    TF_EQ(g_res.code, RQ_ECLOSED);
    TF_EQ(g_level, OLDLEVEL);
}

static void count_of(const sn_counts *c, u16 k, long *v)
{
    char *end;

    TF_CHECK(c->n[k][0] != '?');
    *v = strtol(c->n[k], &end, 10);
    TF_EQ(*end, 0);
}

static void test_engine_session(void)
{
    char ver[RQ_TEXTLEN + 1];
    char o[PWBUF];
    char n[PWBUF];
    char r[PWBUF];
    sn_counts c0;
    sn_counts c1;
    rq_res res;
    rq_res before;
    long a0;
    long a1;
    long l0;
    long l1;
    long k0;
    long k1;
    u16 i;

    reopen();
    ver[0] = '\0';
    TF_EQ(sn_hello(g_lk, "ST1", ver, &res), RQ_OK);
    TF_CHECK(tf_starts(ver, "RETROINV "));

    /* The first login of the fresh system: the password must change. */
    tl_clear();
    TF_EQ(login_as("ADMIN", "admin"), SN_MUSTCHG);
    TF_EQ(g_res.code, RQ_OK);
    TF_EQ(g_level, 3);
    TF_STREQ(g_name, "");
    TF_STREQ(tl_log(), "LOGIN ADMIN admin\n");

    /* Until it does, the counts are refused (404) and stay "?". */
    memset(&c0, PADCH, sizeof c0);
    TF_EQ(sn_status(g_lk, &c0, &res), RQ_EMUSTCHG);
    TF_EQ(res.code, RQ_EMUSTCHG);
    for (i = 0; i < SN_NCOUNTS; i++)
        TF_STREQ(c0.n[i], "?");

    /* A wrong old password is 401 with the session open, not a failed
       login: more than LINK_MAXFAIL of them change nothing. */
    for (i = 0; i < LINK_MAXFAIL + 2; i++) {
        tl_clear();
        TF_EQ(sn_passwd(g_lk, "wrong1", "secret1", "secret1", &res),
              SN_PW_OLDWRONG);
        TF_EQ(res.code, RQ_EBADPW);
        TF_STREQ(tl_log(), "PASSWD wrong1 secret1\n");
    }

    /* Local refusals send nothing, and leave res as it was. */
    tl_clear();
    memset(&res, 0x5A, sizeof res);
    before = res;
    strcpy(o, "admin");
    strcpy(n, "secret1");
    strcpy(r, "secret2");
    TF_EQ(sn_passwd(g_lk, o, n, r, &res), SN_PW_NOMATCH);
    TF_CHECK(memcmp(&res, &before, sizeof res) == 0);
    TF_EQ(tl_count(), 0);

    tl_clear();
    TF_EQ(sn_passwd(g_lk, "admin", "secret1", "secret1", &res), SN_PW_DONE);
    TF_EQ(res.code, RQ_OK);
    TF_STREQ(tl_log(), "PASSWD admin secret1\n");

    /* Logged in now: the counts, and what adding records does. */
    TF_EQ(sn_status(g_lk, &c0, &res), RQ_OK);
    TF_EQ(res.code, RQ_OK);
    TF_STREQ(tl_req(1), "STATUS");
    for (i = 0; i < SN_NCOUNTS; i++)
        TF_CHECK(strlen(c0.n[i]) <= TXT_U32LEN);
    TF_EQ(tc_send("LOC ADD LOC_NAME=A1 LTYPE=ROOM LOC_CODE=A1"), RQ_OK);
    TF_EQ(tc_send("LOC ADD LOC_NAME=B2 LTYPE=ROOM"), RQ_OK);
    TF_EQ(tc_send("CAT ADD ZZ1 CAT_NAME=Test"), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(tc_send("ITEM ADD CATEGORY=SYS MFR=X MODEL=Y LOC_ID=A1"),
              RQ_OK);
    TF_EQ(sn_status(g_lk, &c1, &res), RQ_OK);
    count_of(&c0, SN_C_ITEMS, &a0);
    count_of(&c1, SN_C_ITEMS, &a1);
    count_of(&c0, SN_C_LOCS, &l0);
    count_of(&c1, SN_C_LOCS, &l1);
    count_of(&c0, SN_C_CATS, &k0);
    count_of(&c1, SN_C_CATS, &k1);
    TF_EQ(a1 - a0, 3);
    TF_EQ(l1 - l0, 2);
    TF_EQ(k1 - k0, 1);

    /* The full name, and the level of an account added. */
    TF_EQ(tc_send("USER SET ADMIN FULLNAME=\"Ann Lee\""), RQ_OK);
    TF_EQ(tc_send("USER ADD BOB LEVEL=2 PASSWORD=bobpw1 FULLNAME=\"Bob B\""),
          RQ_OK);

    /* HELLO again logs out; the new password logs in. */
    hello_again();
    tl_clear();
    TF_EQ(login_as("ADMIN", "secret1"), SN_IN);
    TF_EQ(g_res.code, RQ_OK);
    TF_EQ(g_level, 3);
    TF_STREQ(g_name, "Ann Lee");
    TF_STREQ(tl_log(), "LOGIN ADMIN secret1\n");
    hello_again();
    TF_EQ(login_as("ADMIN", "admin"), SN_REFUSED);

    /* BOB has to change his password first, at his level. */
    TF_EQ(login_as("BOB", "bobpw1"), SN_MUSTCHG);
    TF_EQ(g_level, 2);
    TF_STREQ(g_name, "");
    TF_EQ(sn_passwd(g_lk, "bobpw1", "bobpw1", "bobpw1", &res), SN_PW_SAME);
    TF_EQ(sn_passwd(g_lk, "bobpw1", "bobpw2", "bobpw2", &res), SN_PW_DONE);
    hello_again();
    TF_EQ(login_as("BOB", "bobpw2"), SN_IN);
    TF_EQ(g_level, 2);
    TF_STREQ(g_name, "Bob B");

    /* A password with the bytes that need quotes travels intact. */
    tl_clear();
    TF_EQ(sn_passwd(g_lk, "bobpw2", "a\"b=c\\d", "a\"b=c\\d", &res),
          SN_PW_DONE);
    TF_STREQ(tl_log(), "PASSWD bobpw2 \"a\\\"b=c\\\\d\"\n");
    hello_again();
    tl_clear();
    TF_EQ(login_as("BOB", "a\"b=c\\d"), SN_IN);
    TF_STREQ(tl_log(), "LOGIN BOB \"a\\\"b=c\\\\d\"\n");
    TF_EQ(g_level, 2);

    /* A disabled account is refused (403), the session open. */
    hello_again();
    TF_EQ(login_as("ADMIN", "secret1"), SN_IN);
    TF_EQ(tc_send("USER SET BOB STATUS=D"), RQ_OK);
    hello_again();
    TF_EQ(login_as("BOB", "a\"b=c\\d"), SN_REFUSED);
    TF_EQ(g_res.code, RQ_EDISABLED);
    TF_EQ(g_level, OLDLEVEL);
    TF_STREQ(g_name, "old");
}

int main(int argc, char **argv)
{
    tf_begin("T_SESS");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_SESS dir\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&g_clk);
    test_station();
    test_hello();
    test_login_canned();
    test_login_text();
    test_passwd_local();
    test_passwd_sent();
    test_status_canned();
    test_engine_failures();
    test_engine_session();
    if (g_lk != NULL)
        TF_EQ(link_close(g_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}
