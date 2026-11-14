/*
 * T_USERS.C - unit test for the password hash, logins and user accounts
 * (USERS.C), run through a complete engine.
 *
 * usage: T_USERS [dir]
 *
 * dir (default the current directory) is a directory, or a prefix ending
 * in '\', '/' or ':', that holds the scratch data directory; every
 * engine file there is removed before each fresh open and at the end.
 * The clock is fixed, so every salt and audit row is reproducible.
 *
 * Checks:
 * - the published eng_mix128 vectors and the seeded ADMIN hash (16-bit
 *   compilers show their integer bugs here);
 * - the password rules and the seeded ADMIN account;
 * - logins that succeed and fail, with their L and F audit rows, and the
 *   stored name of the user found whatever the result; MUSTCHG;
 * - the same reads for a refused login whether its name is a user's or
 *   not, with a password or a ticket, and for one refused whatever its
 *   password (refuse);
 * - password changes with their W row and the salt rule;
 * - USER ADD with its rules, duplicates and the reserved name SYSTEM;
 * - eng_user_hash, and logins with a ticket's tag (eng_user_resume): no
 *   audit row and LASTLOGIN as it was when they succeed, one F row of FLD
 *   TICKET for each refusal, the tag checked before the status, USER RESET
 *   and PASSWD revoking it, and a PWHASH cut to the tag matching no ticket;
 * - disabled accounts;
 * - the protection of the last active administrator;
 * - deletion and re-adding; USER RESET;
 * - USER LIST order and its pages;
 * - persistence across a reopen;
 * - that no password typed in the run is in any table or memo file as plain
 *   text;
 * - that an add past the last USER_ID is refused before it reserves an ID;
 * - that two runs with the same clock give the same salts.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, else 1.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGUSER.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"

static eng *g_e;
static u16 g_reads;             /* reads seen by count_read             */
static const eng_time clock0 = { 2026, 10, 8, 12, 0, 0 };

/* The fields of an audit row as AUDIT_IS compares them (te_aud joins
   them with '|'). */
static const char k_audrow[] =
    "STATION USERNAME TBL REC_ID ACTION FLD OLDVAL NEWVAL";

#define AUDIT_IS(r, want) TF_STREQ(te_aud((r), k_audrow), (want))

/* ---------------------------------------------------------------------
 * The engine and reading records.
 * --------------------------------------------------------------------- */

/* Open the data directory (te_open) with the fixed clock and the actor
   TESTSTA/ADMIN into g_e; 0, with a failed check, when the open
   fails. */
static int open_engine(void)
{
    if (!TF_EQ(te_open(&g_e, &clock0), ENG_OK))
        return 0;
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
    return 1;
}

/* Field of the live user name, or "?" when the user is not found. */
static const char *user_field(const char *name, const char *field)
{
    u32 recno;

    if (eng_user_get(g_e, name, &recno) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_USERS, TE_BUF, field);
}

static u32 next_id(int t)
{
    u32 v;

    v = 0;
    TF_EQ(eng_seq_peek(g_e, t, &v), ENG_OK);
    return v;
}

/* The salt the rule gives for user name at the fixed clock, with the
   next audit ID next and the old hash oldhash ("" for a new user). */
static void expect_salt(const char *name, u32 next, const char *oldhash,
                        char *salt)
{
    char seed[ENG_MAXSEED + 40];
    char mix[ENG_HASHLEN + 1];

    sprintf(seed, "20261008120000%s%lu%s", name, (unsigned long)next,
            oldhash);
    eng_mix128((const u8 *)seed, (u16)strlen(seed), mix);
    memcpy(salt, mix, ENG_SALTLEN);
    salt[ENG_SALTLEN] = '\0';
}

/* ---------------------------------------------------------------------
 * The hash and the password rules.
 * --------------------------------------------------------------------- */

static void test_vectors(void)
{
    static const char *const vec[][2] = {
        { "", "875B0C1F68A19BA301F4C449A1452877" },
        { "FFFFx", "92BD92EDB6D9CC40C0A37A7C02FB5F39" },
        { "ABCDxyz", "6E0C4600B312D3363532CFD7396E6957" },
        { "0000admin", "A049CE34F45C0DB9864186CE4761D6AB" },
        { "ADMNADMIN", "34F3E857238FE6CC2B6FC6AFE2B903AF" },
        { "1A2Bsecret", "E2932989995E645505E7CCF6BC07E18D" },
        { "C0DERetro1986", "C95E94C0C913FDDE2978184169D1A750" },
        { "9F3E0123456789ABCDEF", "ADCE03B0219259C4AB2749F815850940" }
    };
    char out[ENG_HASHLEN + 1];
    u8 big[ENG_MAXSEED + 1];
    char cut[ENG_HASHLEN + 1];
    int i;

    for (i = 0; i < (int)(sizeof vec / sizeof vec[0]); i++) {
        memset(out, 'x', sizeof out);
        eng_mix128((const u8 *)vec[i][0], (u16)strlen(vec[i][0]), out);
        TF_STREQ(out, vec[i][1]);
    }
    eng_pw_hash("0000", "admin", out);
    TF_STREQ(out, "A049CE34F45C0DB9864186CE4761D6AB");
    eng_pw_hash("1A2B", "secret", out);
    TF_STREQ(out, "E2932989995E645505E7CCF6BC07E18D");
    eng_pw_hash("9F3E", "0123456789ABCDEF", out);
    TF_STREQ(out, "ADCE03B0219259C4AB2749F815850940");

    /* A message longer than ENG_MAXSEED is cut to it. */
    for (i = 0; i <= ENG_MAXSEED; i++)
        big[i] = (u8)('A' + i % 26);
    eng_mix128(big, ENG_MAXSEED, cut);
    eng_mix128(big, ENG_MAXSEED + 1, out);
    TF_STREQ(out, cut);
    eng_mix128(big, ENG_MAXSEED - 1, out);
    TF_CHECK(strcmp(out, cut) != 0);
}

static void test_pwrules(void)
{
    TF_EQ(eng_pw_valid(""), 0);
    TF_EQ(eng_pw_valid("abc"), 0);
    TF_EQ(eng_pw_valid("abcd"), 1);
    TF_EQ(eng_pw_valid("!~!~"), 1);
    TF_EQ(eng_pw_valid("0123456789abcdef"), 1);
    TF_EQ(eng_pw_valid("0123456789abcdefg"), 0);
    TF_EQ(eng_pw_valid("ab cd"), 0);
    TF_EQ(eng_pw_valid("abc\177"), 0);
    TF_EQ(eng_pw_valid("abc\t"), 0);
    TF_EQ(eng_pw_valid("abc\200"), 0);
}

/* ---------------------------------------------------------------------
 * Accounts.
 * --------------------------------------------------------------------- */

static void test_seed(void)
{
    u32 r;
    u32 n;
    int found;

    TF_EQ(eng_reccount(g_e, ENG_T_USERS), 1);
    TF_STREQ(user_field("ADMIN", "USER_ID"), "1");
    TF_STREQ(user_field("ADMIN", "FULLNAME"), "System Administrator");
    TF_STREQ(user_field("ADMIN", "GROUPNAME"), "ADMIN");
    TF_STREQ(user_field("ADMIN", "LEVEL"), "3");
    TF_STREQ(user_field("ADMIN", "SALT"), "0000");
    TF_STREQ(user_field("ADMIN", "PWHASH"),
             "A049CE34F45C0DB9864186CE4761D6AB");
    TF_STREQ(user_field("ADMIN", "STATUS"), "A");
    TF_STREQ(user_field("ADMIN", "MUSTCHG"), "T");
    TF_STREQ(user_field("ADMIN", "LASTLOGIN"), "");
    TF_STREQ(user_field("ADMIN", "CREATED"), "20261008");
    TF_STREQ(user_field("admin", "USERNAME"), "ADMIN");
    TF_EQ(next_id(ENG_T_USERS), 2);

    /* The seed's I row, written by the system. */
    found = 0;
    n = te_naud();
    for (r = 1; r <= n; r++)
        if (strcmp(te_rtext(ENG_T_AUDIT, r, "TBL"), "USERS") == 0) {
            found++;
            AUDIT_IS(r, "SYSTEM|SYSTEM|USERS|1|I|USER_ID||1");
        }
    TF_EQ(found, 1);
}

static void test_login(void)
{
    eng_login l;
    u32 n;

    n = te_naud();
    TF_EQ(eng_user_login(g_e, "ADMIN", "ADMIN", 0, &l), ENG_EBADPW);
    TF_EQ(l.level, 0);
    TF_EQ(te_naud(), n + 1);
    AUDIT_IS(n + 1, "TESTSTA||USERS|1|F|USERNAME||ADMIN");

    TF_STREQ(l.user, "ADMIN");
    TF_EQ(eng_user_login(g_e, "NOBODY", "whatever", 0, &l), ENG_EBADPW);
    TF_STREQ(l.user, "");
    AUDIT_IS(n + 2, "TESTSTA||USERS|0|F|USERNAME||?");
    TF_EQ(eng_user_login(g_e, "bad name!", "whatever", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 3, "TESTSTA||USERS|0|F|USERNAME||?");
    TF_EQ(eng_user_login(g_e, "", "whatever", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 4, "TESTSTA||USERS|0|F|USERNAME||?");
    TF_EQ(eng_user_login(g_e, "SYSTEM", "whatever", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 5, "TESTSTA||USERS|0|F|USERNAME||?");
    /* A password typed as the name is not kept; a real user's name is
       written as stored. */
    TF_EQ(eng_user_login(g_e, "Ret#1986", "x", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 6, "TESTSTA||USERS|0|F|USERNAME||?");
    TF_EQ(eng_user_login(g_e, "admin", "wrong", 0, &l), ENG_EBADPW);
    TF_STREQ(l.user, "ADMIN");
    AUDIT_IS(n + 7, "TESTSTA||USERS|1|F|USERNAME||ADMIN");
    TF_EQ(eng_user_login(g_e, "ADMIN", "admin ", 0, &l), ENG_EBADPW);
    TF_EQ(eng_user_login(g_e, "ADMIN", "adm", 0, &l), ENG_EBADPW);
    TF_EQ(te_naud(), n + 9);
    TF_STREQ(user_field("ADMIN", "LASTLOGIN"), "");

    /* The seeded password, the name in any case: MUSTCHG pending. */
    eng_set_actor(g_e, "TESTSTA", "");
    TF_EQ(eng_user_login(g_e, "admin", "admin", 0, &l), ENG_OK);
    TF_EQ(l.level, 3);
    TF_EQ(l.mustchg, 1);
    TF_STREQ(l.user, "ADMIN");
    TF_STREQ(l.fullname, "System Administrator");
    TF_EQ(te_naud(), n + 10);
    AUDIT_IS(n + 10, "TESTSTA|ADMIN|USERS|1|L|||");
    TF_STREQ(user_field("ADMIN", "LASTLOGIN"), "20261008");
    TF_STREQ(user_field("ADMIN", "MUSTCHG"), "T");
}

/* A cf_fault_rd hook that counts the reads in g_reads. */
static int count_read(int op)
{
    if (op == CF_OP_READ)
        g_reads++;
    return 0;
}

/* The reads of a login of name that is refused (ENG_EBADPW): with pw
   and refuse, or, pw NULL, with a ticket the store does not hold. */
static u16 refused_reads(const char *name, const char *pw, int refuse)
{
    eng_login l;

    g_reads = 0;
    cf_fault_rd = count_read;
    TF_EQ(pw != NULL ? eng_user_login(g_e, name, pw, refuse, &l) :
          eng_user_resume(g_e, name, 0, "", &l), ENG_EBADPW);
    cf_fault_rd = NULL;
    return g_reads;
}

/* A refused login reads as much whether it names a user or not, and so
   does a password login refused whatever its password (run with ADMIN's
   password admin), so that the time of the answer tells none of them
   apart.  Leaves the actor as test_login did. */
static void test_alike(void)
{
    u16 reads;
    u32 n;

    n = te_naud();
    reads = refused_reads("ADMIN", "whatever", 0);
    TF_CHECK(reads != 0);
    TF_EQ(refused_reads("NOBODY", "whatever", 0), reads);
    TF_EQ(refused_reads("ADMIN", "admin", 1), reads);
    TF_EQ(te_naud(), n + 3);
    AUDIT_IS(n + 3, "TESTSTA||USERS|1|F|USERNAME||ADMIN");
    TF_EQ(refused_reads("NOBODY", NULL, 0), refused_reads("ADMIN", NULL, 0));
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

static void test_passwd(void)
{
    eng_login l;
    char salt[ENG_SALTLEN + 1];
    char hash[ENG_HASHLEN + 1];
    u32 n;
    u32 next;

    n = te_naud();
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "ADMIN", "Retro1986"), ENG_EBADPW);
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "adm", "Retro1986"), ENG_EBADPW);
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "", "Retro1986"), ENG_EBADPW);
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "admin", "abc"), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "NEW");
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "admin", "with space"), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "NEW");
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "admin", "admin"), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "NEW");
    TF_EQ(eng_user_passwd(g_e, "NOBODY", "admin", "Retro1986"),
          ENG_ENOTFOUND);
    /* Refusals write nothing: a wrong old password is not a failed
       login. */
    TF_EQ(te_naud(), n);
    TF_STREQ(user_field("ADMIN", "SALT"), "0000");

    next = next_id(ENG_T_AUDIT);
    TF_EQ(eng_user_passwd(g_e, "admin", "admin", "Retro1986"), ENG_OK);
    expect_salt("ADMIN", next, "A049CE34F45C0DB9864186CE4761D6AB", salt);
    TF_STREQ(user_field("ADMIN", "SALT"), salt);
    eng_pw_hash(salt, "Retro1986", hash);
    TF_STREQ(user_field("ADMIN", "PWHASH"), hash);
    TF_STREQ(user_field("ADMIN", "MUSTCHG"), "F");
    TF_EQ(te_naud(), n + 1);
    TF_EQ((u32)te_rnum(ENG_T_AUDIT, n + 1, "AUD_ID"), next);
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|1|W|PWHASH||");

    TF_EQ(eng_user_login(g_e, "ADMIN", "admin", 0, &l), ENG_EBADPW);
    TF_EQ(eng_user_login(g_e, "ADMIN", "retro1986", 0, &l), ENG_EBADPW);
    TF_EQ(eng_user_login(g_e, "ADMIN", "Retro1986", 0, &l), ENG_OK);
    TF_EQ(l.mustchg, 0);
    TF_EQ(l.level, 3);
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "Retro1986", "Retro1986"), ENG_EARG);
}

static void test_add(void)
{
    eng_login l;
    char salt[ENG_SALTLEN + 1];
    char hash[ENG_HASHLEN + 1];
    u32 n;
    u32 next;
    u32 users;

    eng_set_actor(g_e, "TESTSTA", "ADMIN");
    te_kv("LEVEL=2|PASSWORD=pass1|FULLNAME=Bob Smith|GROUP=ops");
    n = te_naud();
    next = next_id(ENG_T_AUDIT);
    TF_EQ(eng_user_add(g_e, "bob", te_p, 4), ENG_OK);
    TF_STREQ(user_field("BOB", "USER_ID"), "2");
    TF_STREQ(user_field("BOB", "USERNAME"), "BOB");
    TF_STREQ(user_field("BOB", "FULLNAME"), "Bob Smith");
    TF_STREQ(user_field("BOB", "GROUPNAME"), "OPS");
    TF_STREQ(user_field("BOB", "LEVEL"), "2");
    TF_STREQ(user_field("BOB", "STATUS"), "A");
    TF_STREQ(user_field("BOB", "MUSTCHG"), "T");
    TF_STREQ(user_field("BOB", "CREATED"), "20261008");
    TF_STREQ(user_field("BOB", "LASTLOGIN"), "");
    expect_salt("BOB", next, "", salt);
    TF_STREQ(user_field("BOB", "SALT"), salt);
    eng_pw_hash(salt, "pass1", hash);
    TF_STREQ(user_field("BOB", "PWHASH"), hash);
    TF_EQ(te_naud(), n + 1);
    TF_EQ((u32)te_rnum(ENG_T_AUDIT, n + 1, "AUD_ID"), next);
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|2|I|USER_ID||2");

    TF_EQ(eng_user_login(g_e, "BOB", "pass1", 0, &l), ENG_OK);
    TF_EQ(l.level, 2);
    TF_EQ(l.mustchg, 1);
    TF_STREQ(l.fullname, "Bob Smith");
    eng_set_actor(g_e, "TESTSTA", "ADMIN");

    /* Refusals change nothing and reserve no ID. */
    n = te_naud();
    users = eng_reccount(g_e, ENG_T_USERS);
    next = next_id(ENG_T_USERS);
    TF_EQ(eng_user_add(g_e, "BOB", te_p, 4), ENG_EDUP);
    TF_STREQ(eng_errfield(g_e), "USERNAME");
    TF_EQ(eng_user_add(g_e, "Bob", te_p, 4), ENG_EDUP);
    TF_EQ(eng_user_add(g_e, "ADMIN", te_p, 4), ENG_EDUP);
    TF_EQ(eng_user_add(g_e, "SYSTEM", te_p, 4), ENG_EDUP);
    TF_STREQ(eng_errfield(g_e), "USERNAME");
    TF_EQ(eng_user_add(g_e, "system", te_p, 4), ENG_EDUP);
    TF_EQ(eng_user_add(g_e, "", te_p, 4), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "USERNAME");
    TF_EQ(eng_user_add(g_e, "NINECHARS", te_p, 4), ENG_EARG);
    TF_EQ(eng_user_add(g_e, "A-B", te_p, 4), ENG_EARG);
    TF_EQ(eng_user_add(g_e, "A B", te_p, 4), ENG_EARG);
    TF_EQ(eng_user_add(g_e, "CAROL", te_p + 1, 3), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "LEVEL");
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 1), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "PASSWORD");
    te_p[0].val = "4";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "LEVEL");
    te_p[0].val = "0";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    te_p[0].val = "";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    te_p[0].val = "33";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    te_p[0].val = "3";
    te_p[1].val = "abc";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "PASSWORD");
    te_p[1].val = "pass1";
    te_p[2].val = "0123456789012345678901234567890";
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "FULLNAME");
    te_kv("LEVEL=3|PASSWORD=pass1|FULLNAME=Carol|GROUP=ops|STATUS=A");
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 5), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "STATUS");
    te_kv("LEVEL=3|PASSWORD=pass1|FULLNAME=Carol|GROUP=ops|GROUPNAME=X");
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 5), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "GROUPNAME");
    TF_EQ(te_naud(), n);
    TF_EQ(eng_reccount(g_e, ENG_T_USERS), users);
    TF_EQ(next_id(ENG_T_USERS), next);

    /* CAROL, a second administrator. */
    TF_EQ(eng_user_add(g_e, "CAROL", te_p, 4), ENG_OK);
    TF_STREQ(user_field("CAROL", "USER_ID"), "3");
    TF_STREQ(user_field("CAROL", "LEVEL"), "3");
}

/* Store the first n characters of hash as user name's PWHASH; 1, or 0
   with a failed check. */
static int put_pwhash(const char *name, const char *hash, u16 n)
{
    char v[ENG_HASHLEN + 1];
    u32 recno;

    memcpy(v, hash, n);
    v[n] = '\0';
    if (!TF_EQ(eng_user_get(g_e, name, &recno), ENG_OK) ||
        !TF_EQ(eng_edit_begin(g_e, ENG_T_USERS, recno), ENG_OK))
        return 0;
    if (!TF_EQ(eng_edit_put(g_e, "PWHASH", v), ENG_OK)) {
        eng_edit_cancel(g_e);
        return 0;
    }
    return TF_EQ(eng_edit_commit(g_e, 0), ENG_OK);
}

static void test_ticket(void)
{
    eng_pair p[1];
    eng_login l;
    char hash[ENG_HASHLEN + 1];
    char tag[ENG_TAGLEN];
    u32 n;

    TF_EQ(eng_user_hash(g_e, "NOBODY", hash), ENG_ENOTFOUND);
    TF_EQ(eng_user_hash(g_e, "SYSTEM", hash), ENG_ENOTFOUND);
    n = te_naud();
    TF_EQ(eng_user_hash(g_e, "carol", hash), ENG_OK);
    TF_STREQ(hash, user_field("CAROL", "PWHASH"));
    memcpy(tag, hash, ENG_TAGLEN);
    TF_EQ(eng_user_resume(g_e, "carol", 1, tag, &l), ENG_OK);
    TF_EQ(l.level, 3);
    TF_EQ(l.mustchg, 1);
    TF_STREQ(l.user, "CAROL");
    TF_STREQ(l.fullname, "Carol");
    TF_EQ(te_naud(), n);
    TF_STREQ(user_field("CAROL", "LASTLOGIN"), "");

    /* No ticket in the store, a wrong tag, a name that is no user. */
    TF_EQ(eng_user_resume(g_e, "CAROL", 0, tag, &l), ENG_EBADPW);
    TF_EQ(l.level, 0);
    AUDIT_IS(n + 1, "TESTSTA||USERS|3|F|TICKET||CAROL");
    tag[ENG_TAGLEN - 1] = (char)(tag[ENG_TAGLEN - 1] == '0' ? '1' : '0');
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EBADPW);
    AUDIT_IS(n + 2, "TESTSTA||USERS|3|F|TICKET||CAROL");
    TF_EQ(eng_user_resume(g_e, "Ret#1986", 1, tag, &l), ENG_EBADPW);
    AUDIT_IS(n + 3, "TESTSTA||USERS|0|F|TICKET||?");
    TF_EQ(eng_user_resume(g_e, "SYSTEM", 0, tag, &l), ENG_EBADPW);
    AUDIT_IS(n + 4, "TESTSTA||USERS|0|F|TICKET||?");
    TF_EQ(te_naud(), n + 4);

    /* Disabled: the right tag learns it, a wrong one does not. */
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
    p[0].key = "STATUS"; p[0].val = "D";
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_OK);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EBADPW);
    memcpy(tag, hash, ENG_TAGLEN);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EDISABLED);
    AUDIT_IS(n + 7, "TESTSTA||USERS|3|F|TICKET||CAROL");
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
    p[0].val = "A";
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_OK);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_OK);

    /* USER RESET, then PASSWD: each makes the tag before it stale. */
    TF_EQ(eng_user_reset(g_e, "CAROL", "pass2"), ENG_OK);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EBADPW);
    TF_EQ(eng_user_hash(g_e, "CAROL", hash), ENG_OK);
    memcpy(tag, hash, ENG_TAGLEN);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_OK);
    eng_set_actor(g_e, "TESTSTA", "CAROL");
    TF_EQ(eng_user_passwd(g_e, "CAROL", "pass2", "pass1"), ENG_OK);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EBADPW);
    TF_EQ(eng_user_hash(g_e, "CAROL", hash), ENG_OK);
    memcpy(tag, hash, ENG_TAGLEN);
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_OK);
    TF_EQ(l.mustchg, 0);
    TF_STREQ(user_field("CAROL", "LASTLOGIN"), "");

    /* A PWHASH cut to its tag matches no ticket. */
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
    TF_CHECK(put_pwhash("CAROL", tag, ENG_TAGLEN));
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_EBADPW);
    TF_CHECK(put_pwhash("CAROL", hash, ENG_HASHLEN));
    TF_EQ(eng_user_resume(g_e, "CAROL", 1, tag, &l), ENG_OK);
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

static void test_disable(void)
{
    eng_pair p[2];
    eng_login l;
    u32 n;

    n = te_naud();
    p[0].key = "STATUS"; p[0].val = "d";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_OK);
    TF_STREQ(user_field("BOB", "STATUS"), "D");
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|2|U|STATUS|A|D");
    TF_EQ(eng_user_login(g_e, "BOB", "pass1", 0, &l), ENG_EDISABLED);
    TF_STREQ(l.user, "BOB");
    AUDIT_IS(n + 2, "TESTSTA||USERS|2|F|USERNAME||BOB");
    TF_EQ(eng_user_login(g_e, "BOB", "wrong", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 3, "TESTSTA||USERS|2|F|USERNAME||BOB");
    eng_set_actor(g_e, "TESTSTA", "ADMIN");

    p[0].val = "X";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "STATUS");
    p[0].val = "";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_EARG);
    p[0].key = "PASSWORD"; p[0].val = "pass2";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "PASSWORD");
    p[0].key = "MUSTCHG"; p[0].val = "F";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_EARG);
    p[0].key = "LEVEL"; p[0].val = "9";
    TF_EQ(eng_user_set(g_e, "BOB", p, 1), ENG_EARG);
    TF_EQ(eng_user_set(g_e, "NOBODY", p, 0), ENG_ENOTFOUND);
    TF_EQ(te_naud(), n + 3);

    p[0].key = "STATUS"; p[0].val = "A";
    p[1].key = "GROUP"; p[1].val = "lab";
    TF_EQ(eng_user_set(g_e, "bob", p, 2), ENG_OK);
    TF_STREQ(user_field("BOB", "GROUPNAME"), "LAB");
    TF_EQ(eng_user_login(g_e, "BOB", "pass1", 0, &l), ENG_OK);
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

static void test_lastadmin(void)
{
    eng_pair p[2];
    eng_login l;
    u32 n;

    /* ADMIN and CAROL are administrators: either may stop being one. */
    p[0].key = "LEVEL"; p[0].val = "2";
    TF_EQ(eng_user_set(g_e, "ADMIN", p, 1), ENG_OK);
    n = te_naud();
    AUDIT_IS(n, "TESTSTA|ADMIN|USERS|1|U|LEVEL|3|2");
    /* CAROL is now the only one. */
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "LEVEL");
    p[0].key = "STATUS"; p[0].val = "D";
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "STATUS");
    TF_EQ(eng_user_del(g_e, "CAROL"), ENG_ECONSTR);
    /* Changes that keep her an active administrator are allowed. */
    p[0].key = "LEVEL"; p[0].val = "3";
    p[1].key = "STATUS"; p[1].val = "A";
    TF_EQ(eng_user_set(g_e, "CAROL", p, 2), ENG_OK);
    p[0].key = "FULLNAME"; p[0].val = "Carol Jones";
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_OK);
    TF_EQ(te_naud(), n + 1);
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|3|U|FULLNAME|Carol|Carol Jones");
    TF_STREQ(user_field("CAROL", "LEVEL"), "3");
    TF_STREQ(user_field("CAROL", "STATUS"), "A");

    /* A disabled level-3 user does not count. */
    p[0].key = "LEVEL"; p[0].val = "3";
    TF_EQ(eng_user_set(g_e, "ADMIN", p, 1), ENG_OK);
    p[0].key = "STATUS"; p[0].val = "D";
    TF_EQ(eng_user_set(g_e, "ADMIN", p, 1), ENG_OK);
    TF_EQ(eng_user_del(g_e, "CAROL"), ENG_ECONSTR);
    TF_EQ(eng_user_set(g_e, "CAROL", p, 1), ENG_ECONSTR);
    /* Nor does a deleted one: re-enable ADMIN, delete CAROL, and ADMIN
       is the last again. */
    p[0].val = "A";
    TF_EQ(eng_user_set(g_e, "ADMIN", p, 1), ENG_OK);
    TF_EQ(eng_user_del(g_e, "CAROL"), ENG_OK);
    TF_EQ(eng_user_del(g_e, "ADMIN"), ENG_ECONSTR);
    p[0].key = "LEVEL"; p[0].val = "1";
    TF_EQ(eng_user_set(g_e, "ADMIN", p, 1), ENG_ECONSTR);
    TF_STREQ(user_field("ADMIN", "LEVEL"), "3");
    TF_STREQ(user_field("ADMIN", "STATUS"), "A");
    TF_EQ(eng_user_login(g_e, "CAROL", "pass1", 0, &l), ENG_EBADPW);
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

static void test_delete(void)
{
    eng_pair p[2];
    eng_login l;
    u32 recno;
    u32 n;

    n = te_naud();
    TF_EQ(eng_user_del(g_e, "BOB"), ENG_OK);
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|2|D|||DELETED");
    TF_EQ(eng_user_get(g_e, "BOB", &recno), ENG_ENOTFOUND);
    TF_EQ(recno, 0);
    TF_EQ(eng_user_del(g_e, "BOB"), ENG_ENOTFOUND);
    TF_EQ(eng_user_reset(g_e, "BOB", "pass2"), ENG_ENOTFOUND);
    TF_EQ(eng_user_login(g_e, "BOB", "pass1", 0, &l), ENG_EBADPW);
    AUDIT_IS(n + 2, "TESTSTA||USERS|0|F|USERNAME||?");
    eng_set_actor(g_e, "TESTSTA", "ADMIN");

    /* A deleted name may be added again, with a new ID. */
    p[0].key = "LEVEL";    p[0].val = "1";
    p[1].key = "PASSWORD"; p[1].val = "bobagain";
    TF_EQ(eng_user_add(g_e, "BOB", p, 2), ENG_OK);
    TF_STREQ(user_field("BOB", "USER_ID"), "4");
    TF_STREQ(user_field("BOB", "FULLNAME"), "");
    TF_EQ(eng_user_login(g_e, "BOB", "bobagain", 0, &l), ENG_OK);
    TF_EQ(l.level, 1);
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

static void test_reset(void)
{
    eng_login l;
    char salt[ENG_SALTLEN + 1];
    char old[ENG_HASHLEN + 1];
    char hash[ENG_HASHLEN + 1];
    u32 n;
    u32 next;

    n = te_naud();
    TF_EQ(eng_user_reset(g_e, "BOB", "xyz"), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "PASSWORD");
    TF_EQ(eng_user_reset(g_e, "BOB", "toolongpassword17"), ENG_EARG);
    TF_EQ(te_naud(), n);

    TF_EQ(eng_user_passwd(g_e, "BOB", "bobagain", "bob2bob2"), ENG_OK);
    TF_STREQ(user_field("BOB", "MUSTCHG"), "F");
    strcpy(old, user_field("BOB", "PWHASH"));
    n = te_naud();
    next = next_id(ENG_T_AUDIT);
    TF_EQ(eng_user_reset(g_e, "bob", "Fresh#99"), ENG_OK);
    expect_salt("BOB", next, old, salt);
    TF_STREQ(user_field("BOB", "SALT"), salt);
    eng_pw_hash(salt, "Fresh#99", hash);
    TF_STREQ(user_field("BOB", "PWHASH"), hash);
    TF_STREQ(user_field("BOB", "MUSTCHG"), "T");
    TF_EQ(te_naud(), n + 1);
    TF_EQ((u32)te_rnum(ENG_T_AUDIT, n + 1, "AUD_ID"), next);
    AUDIT_IS(n + 1, "TESTSTA|ADMIN|USERS|4|W|PWHASH||");
    TF_EQ(eng_user_login(g_e, "BOB", "bob2bob2", 0, &l), ENG_EBADPW);
    TF_EQ(eng_user_login(g_e, "BOB", "Fresh#99", 0, &l), ENG_OK);
    TF_EQ(l.mustchg, 1);
    eng_set_actor(g_e, "TESTSTA", "ADMIN");
}

/* Up to max users of USER LIST from cursor after (NULL: the start),
   their names each followed by ',' into names (ENG_USERLEN + 1 bytes a
   name, NUL in), and into cur (ENG_CURSORMAX) the cursor of the next
   page, "" when none remains; names is "ERR" when a call fails. */
static void scan_page(const char *after, u16 max, char *names, char *cur)
{
    eng_scan s;
    u32 recno;
    u16 n;
    int more;

    names[0] = '\0';
    cur[0] = '\0';
    if (eng_user_scan(g_e, &s, after) != ENG_OK) {
        strcpy(names, "ERR");
        return;
    }
    for (n = 0; n < max; n++) {
        if (eng_scan_next(g_e, &s, &recno) != ENG_OK) {
            strcpy(names, "ERR");
            return;
        }
        if (recno == 0)
            break;
        strcat(names, te_rtext(ENG_T_USERS, TE_BUF, "USERNAME"));
        strcat(names, ",");
    }
    if (eng_scan_more(g_e, &s, &more) != ENG_OK)
        strcpy(names, "ERR");
    else if (more)
        eng_scan_cursor(&s, cur);
}

/* USER LIST: live users in name order, in one page and one a page.
   CAROL's deleted entry lies after BOB, so page 2 ends with a cursor
   and page 3 is empty. */
static void test_scan(void)
{
    char names[4 * (ENG_USERLEN + 1) + 1];
    char cur[ENG_CURSORMAX];
    char next[ENG_CURSORMAX];

    scan_page(NULL, 3, names, cur);
    TF_STREQ(names, "ADMIN,BOB,");
    scan_page(NULL, 1, names, cur);
    TF_STREQ(names, "ADMIN,");
    TF_STREQ(cur, "USERS1:41444D494E:1");
    scan_page(cur, 1, names, next);
    TF_STREQ(names, "BOB,");
    TF_STREQ(next, "USERS1:424F42:4");
    scan_page(next, 1, names, cur);
    TF_STREQ(names, "");
    TF_STREQ(cur, "");
}

/* The users and their passwords survive a close and reopen, and the
   reopen seeds nothing. */
static void test_reopen(void)
{
    eng_login l;
    u32 users;

    users = eng_reccount(g_e, ENG_T_USERS);
    TF_EQ(eng_close(g_e), ENG_OK);
    if (!open_engine())
        return;
    TF_EQ(eng_reccount(g_e, ENG_T_USERS), users);
    TF_EQ(eng_user_login(g_e, "ADMIN", "Retro1986", 0, &l), ENG_OK);
    TF_EQ(l.mustchg, 0);
    TF_EQ(eng_user_login(g_e, "BOB", "Fresh#99", 0, &l), ENG_OK);
    TF_EQ(eng_user_login(g_e, "CAROL", "pass1", 0, &l), ENG_EBADPW);
    TF_EQ(eng_close(g_e), ENG_OK);
}

/* No password typed in the run, accepted or refused or typed as a
   name, is in any table or memo file as plain text.  Left out are the
   typed words that are parts of stored names or texts: "ADM", "ADMIN",
   "ADMIN " and "x".  Every table's .DBF must be there; a .DBT may be
   missing. */
static void test_secrets(void)
{
    static const char *const typed[] = {
        "Retro1986", "retro1986", "pass1", "pass2", "bobagain", "bob2bob2",
        "Fresh#99", "Ret#1986", "whatever", "wrong", "toolongpassword17",
        "with space", "abc", "xyz", "admin"
    };
    static const char *const exts[] = { ".DBF", ".DBT" };
    const char *path;
    char *buf;
    u32 size;
    u16 got;
    u16 at;
    u16 len;
    u16 k;
    int t;
    int x;
    int found;

    found = 0;
    for (t = 0; t < ENG_NTABLES; t++)
        for (x = 0; x < 2; x++) {
            path = tfs_path(eng_table_def(t)->name, exts[x]);
            size = tfs_size(path);
            if (x == 0)
                TF_CHECK(size != TFS_NOFILE);
            if (size == TFS_NOFILE || !TF_CHECK(size < 0x7FFFUL))
                continue;
            buf = (char *)malloc((size_t)size + 1u);
            if (!TF_CHECK(buf != NULL))
                continue;
            if (TF_EQ(tfs_read_all(path, buf, (u16)size, &got), 0))
                for (k = 0; k < sizeof typed / sizeof typed[0]; k++) {
                    len = (u16)strlen(typed[k]);
                    for (at = 0; at + len <= got; at++)
                        if (memcmp(buf + at, typed[k], len) == 0) {
                            printf("%s holds %s\n", path, typed[k]);
                            found++;
                        }
                }
            free(buf);
        }
    TF_EQ(found, 0);
}

/* USER_ID is N(4): the add that takes 9999 succeeds, the next is refused
   before it reserves an ID, appending nothing. */
static void test_idlimit(void)
{
    u32 users;
    u32 n;
    u32 v;
    u16 np;

    if (!open_engine())
        return;
    np = te_kv("LEVEL=1|PASSWORD=pass1");
    v = next_id(ENG_T_USERS);
    TF_EQ(eng_seq_next(g_e, ENG_T_USERS, (u16)(9999 - v), &v), ENG_OK);
    TF_EQ(eng_user_add(g_e, "EVE", te_p, np), ENG_OK);
    TF_STREQ(user_field("EVE", "USER_ID"), "9999");
    n = te_naud();
    users = eng_reccount(g_e, ENG_T_USERS);
    TF_EQ(eng_user_add(g_e, "FRANK", te_p, np), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "USER_ID");
    TF_STREQ(eng_errmsg(g_e), "no ID numbers left");
    TF_EQ(te_naud(), n);
    TF_EQ(eng_reccount(g_e, ENG_T_USERS), users);
    TF_EQ(next_id(ENG_T_USERS), 10000);
    TF_STREQ(user_field("FRANK", "USERNAME"), "?");
    TF_EQ(eng_close(g_e), ENG_OK);
}

/* The salts of a fixed sequence of changes on a fresh data directory,
   joined into out, at the given time of day. */
static void salt_run(u8 second, char *out)
{
    eng_time t;
    u16 np;

    out[0] = '\0';
    te_remove(tfs_dir(), NULL);
    if (!open_engine())
        return;
    memcpy(&t, &clock0, sizeof t);
    t.second = second;
    eng_set_now(g_e, &t);
    np = te_kv("LEVEL=2|PASSWORD=first1");
    TF_EQ(eng_user_passwd(g_e, "ADMIN", "admin", "second2"), ENG_OK);
    strcat(out, user_field("ADMIN", "SALT"));
    TF_EQ(eng_user_add(g_e, "DAVE", te_p, np), ENG_OK);
    strcat(out, user_field("DAVE", "SALT"));
    TF_EQ(eng_user_reset(g_e, "DAVE", "first1"), ENG_OK);
    strcat(out, user_field("DAVE", "SALT"));
    TF_EQ(eng_close(g_e), ENG_OK);
}

static void test_repro(void)
{
    char a[3 * ENG_SALTLEN + 1];
    char b[3 * ENG_SALTLEN + 1];
    char c[3 * ENG_SALTLEN + 1];

    salt_run(0, a);
    salt_run(0, b);
    salt_run(1, c);
    TF_EQ(strlen(a), 3 * ENG_SALTLEN);
    TF_STREQ(b, a);
    TF_CHECK(strcmp(c, a) != 0);
}

int main(int argc, char **argv)
{
    tf_begin("T_USERS");
    if (argc > 1)
        tfs_set_dir(argv[1]);
    test_vectors();
    test_pwrules();
    te_remove(tfs_dir(), NULL);
    if (open_engine()) {
        test_seed();
        test_login();
        test_alike();
        test_passwd();
        test_add();
        test_ticket();
        test_disable();
        test_lastadmin();
        test_delete();
        test_reset();
        test_scan();
        test_reopen();
        test_secrets();
        test_idlimit();
    }
    test_repro();
    te_remove(tfs_dir(), NULL);
    return tf_done();
}
