/*
 * T_TICKET.C - unit test for the server's store of login tickets
 * (TICKET.C).
 *
 * usage: T_TICKET
 *
 * Checks:
 * - a second open refused, and the block there only while the store is
 *   open, all zeros when it opens;
 * - a ticket is PROTO_TKTLEN upper-case hex digits, redeemed with its
 *   number and tag for its user and station, either in any case, and no
 *   SHOWN digits of it running, nor their bytes, are in the store;
 * - a redeem for another user or station is refused and drops the entry;
 *   a ticket one digit off is refused and drops nothing;
 * - the idle limit counted from the last use, none with 0, and TKT_LIFE
 *   counted from the issue whatever the use; an issue sweeps the
 *   expired entries too;
 * - drop, a drop of a number out of range, and drop_user, its user in
 *   any case, leaving the others' tickets;
 * - one ticket per station, a new one replacing it whatever its user;
 * - a full store gives up the entry idle the longest;
 * - an entry dropped, expired or replaced is zeroed;
 * - NDISTINCT tickets in a row all differ;
 * - on the host, whose tmr_fine gives what the test sets: the same state
 *   makes the same ticket, and another PWHASH, chip count, time of day
 *   at the open or a stir in between another one; and two tickets in a
 *   row, a stir between them, are those that TICKET.H's recipe, rebuilt
 *   here (the pool's fold and rekey, the seed), gives;
 * - under DOS, the chip's count moves (tmr_fine).
 * Prints a summary line "T_TICKET PASS n"; exit status 0 when every
 * check passes, else 1.
 */
#define COMPAT_FINE
#define PROTO_TICKETS
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGUSER.H"
#include "PROTO.H"
#include "TICKET.H"
#include "TESTFW.H"

#define NDISTINCT       64      /* tickets made in a row                */
#define BLKMAX          (TKT_MAX * 64)  /* bytes of the store at most   */
#define SHOWN           8       /* digits of a ticket that tell on it   */

static const char k_h1[] = "0123456789ABCDEF0123456789ABCDEF";
static const char k_h2[] = "FEDCBA9876543210FEDCBA9876543210";

static const proto_tickets *g_t;
static char g_tag[ENG_TAGLEN];
static u8 g_blk[BLKMAX];
static char g_seen[NDISTINCT][PROTO_TKTLEN + 1];

/* Open the store afresh with the idle limit idle; 0, with a failed
   check, when it does not open. */
static int fresh(u32 idle)
{
    tkt_close();
    g_t = tkt_open(idle, 0);
    return TF_CHECK(g_t != NULL);
}

static u8 issue(u32 now, const char *user, const char *sta,
                const char *hash, char *t)
{
    return g_t->issue(now, user, sta, hash, t);
}

/* Redeem t, the tag into g_tag. */
static u8 redeem(u32 now, const char *t, const char *user, const char *sta)
{
    return g_t->redeem(now, t, user, sta, g_tag);
}

/* 1 when t is PROTO_TKTLEN upper-case hex digits. */
static int form_ok(const char *t)
{
    u16 n;

    for (n = 0; t[n] != '\0'; n++)
        if (!TXT_ISDIGIT(t[n]) && (t[n] < 'A' || t[n] > 'F'))
            return 0;
    return n == PROTO_TKTLEN;
}

/* The store's bytes into g_blk; their count. */
static u16 store(void)
{
    cmem_blk *b;
    u16 n;

    b = tkt_block();
    if (!TF_CHECK(b != NULL))
        return 0;
    n = cmem_size(b);
    if (!TF_CHECK(n <= BLKMAX))
        return 0;
    cmem_get(b, 0, g_blk, n);
    return n;
}

/* 1 when the n bytes at p are nowhere in the store. */
static int absent(const u8 *p, u16 n)
{
    u16 len;
    u16 i;

    len = store();
    for (i = 0; i + n <= len; i++)
        if (memcmp(g_blk + i, p, n) == 0)
            return 0;
    return 1;
}

/* 1 when the store holds only zeros. */
static int all_zero(void)
{
    u16 len;
    u16 i;

    len = store();
    for (i = 0; i < len; i++)
        if (g_blk[i] != 0)
            return 0;
    return 1;
}

/* The PROTO_TKTLEN / 2 bytes the hex digits at hex spell, into out. */
static void unhex(const char *hex, u8 *out)
{
    u16 i;

    for (i = 0; i < PROTO_TKTLEN / 2; i++)
        out[i] = (u8)((txt_hexval(hex[2 * i]) << 4) |
                      txt_hexval(hex[2 * i + 1]));
}

/* 1 when no SHOWN digits running in ticket t, nor the bytes they
   spell, are in the store. */
static int hidden(const char *t)
{
    u8 raw[PROTO_TKTLEN / 2];
    u16 i;

    unhex(t, raw);
    for (i = 0; i + SHOWN <= PROTO_TKTLEN; i++)
        if (!absent((const u8 *)t + i, SHOWN) ||
            (i % 2 == 0 && !absent(raw + i / 2, SHOWN / 2)))
            return 0;
    return 1;
}

static void test_open(void)
{
    TF_CHECK(tkt_block() == NULL);
    g_t = tkt_open(0, 0);
    TF_CHECK(g_t != NULL);
    TF_CHECK(tkt_open(0, 0) == NULL);
    TF_CHECK(all_zero());
    tkt_close();
    TF_CHECK(tkt_block() == NULL);
    tkt_close();
    tkt_stir(1, 1);
    TF_CHECK(tkt_block() == NULL);
}

static void test_redeem(void)
{
    char t[PROTO_TKTLEN + 1];
    char last;
    u8 k;

    if (!fresh(0))
        return;
    k = issue(10, "ADMIN", "STA1", k_h1, t);
    TF_CHECK(k >= 1 && k <= TKT_MAX);
    TF_CHECK(form_ok(t));
    TF_CHECK(hidden(t));
    TF_EQ(redeem(20, t, "admin", "sta1"), k);
    TF_CHECK(memcmp(g_tag, k_h1, ENG_TAGLEN) == 0);
    TF_EQ(redeem(30, t, "ADMIN", "STA1"), k);
    TF_CHECK(hidden(t));

    /* One digit off: refused, the ticket still there. */
    last = t[PROTO_TKTLEN - 1];
    t[PROTO_TKTLEN - 1] = (char)(last == '0' ? '1' : '0');
    TF_EQ(redeem(40, t, "ADMIN", "STA1"), 0);
    t[PROTO_TKTLEN - 1] = last;
    TF_EQ(redeem(41, t, "ADMIN", "STA1"), k);

    /* Another user, another station: refused, and the entry gone. */
    TF_EQ(redeem(60, t, "BOB", "STA1"), 0);
    TF_EQ(redeem(61, t, "ADMIN", "STA1"), 0);
    TF_CHECK(all_zero());
    issue(70, "ADMIN", "STA1", k_h1, t);
    TF_EQ(redeem(71, t, "ADMIN", "STA2"), 0);
    TF_EQ(redeem(72, t, "ADMIN", "STA1"), 0);
    TF_CHECK(all_zero());
}

static void test_expiry(void)
{
    char t[PROTO_TKTLEN + 1];
    u8 k;
    u8 k2;

    /* Idle for 100 ticks: each use starts the count again. */
    if (!fresh(100))
        return;
    k = issue(1000, "ADMIN", "STA1", k_h1, t);
    TF_EQ(redeem(1099, t, "ADMIN", "STA1"), k);
    TF_EQ(redeem(1198, t, "ADMIN", "STA1"), k);
    TF_EQ(redeem(1298, t, "ADMIN", "STA1"), 0);
    TF_CHECK(all_zero());

    /* An issue sweeps: the expired entry is free for the next. */
    k = issue(2000, "ADMIN", "STA1", k_h1, t);
    k2 = issue(2100, "BOB", "STA2", k_h2, t);
    TF_EQ(k2, k);

    /* No idle limit: only the lifetime. */
    if (!fresh(0))
        return;
    k = issue(5, "ADMIN", "STA1", k_h1, t);
    TF_EQ(redeem(5 + TKT_LIFE - 1, t, "ADMIN", "STA1"), k);
    TF_EQ(redeem(5 + TKT_LIFE, t, "ADMIN", "STA1"), 0);

    /* The lifetime counts from the issue, used or not. */
    if (!fresh(TKT_LIFE / 2))
        return;
    k = issue(0, "ADMIN", "STA1", k_h1, t);
    TF_EQ(redeem(TKT_LIFE / 2 - 1, t, "ADMIN", "STA1"), k);
    TF_EQ(redeem(TKT_LIFE - 2, t, "ADMIN", "STA1"), k);
    TF_EQ(redeem(TKT_LIFE, t, "ADMIN", "STA1"), 0);
    TF_CHECK(all_zero());
}

static void test_drop(void)
{
    char t1[PROTO_TKTLEN + 1];
    char t2[PROTO_TKTLEN + 1];
    char t3[PROTO_TKTLEN + 1];
    u8 k;

    if (!fresh(0))
        return;
    k = issue(1, "ADMIN", "STA1", k_h1, t1);
    g_t->drop(0);
    g_t->drop(TKT_MAX + 1);
    TF_EQ(redeem(2, t1, "ADMIN", "STA1"), k);
    g_t->drop(k);
    TF_EQ(redeem(3, t1, "ADMIN", "STA1"), 0);
    TF_CHECK(all_zero());

    issue(4, "ADMIN", "STA1", k_h1, t1);
    issue(5, "ADMIN", "STA2", k_h1, t2);
    k = issue(6, "BOB", "STA3", k_h2, t3);
    g_t->drop_user("admin");
    TF_EQ(redeem(7, t1, "ADMIN", "STA1"), 0);
    TF_EQ(redeem(8, t2, "ADMIN", "STA2"), 0);
    TF_EQ(redeem(9, t3, "BOB", "STA3"), k);
    TF_CHECK(memcmp(g_tag, k_h2, ENG_TAGLEN) == 0);
}

static void test_station(void)
{
    char t1[PROTO_TKTLEN + 1];
    char t2[PROTO_TKTLEN + 1];
    u8 k1;
    u8 k2;

    if (!fresh(0))
        return;
    k1 = issue(10, "ADMIN", "STA1", k_h1, t1);
    k2 = issue(20, "BOB", "sta1", k_h2, t2);
    TF_EQ(k2, k1);
    TF_CHECK(hidden(t1));
    TF_EQ(redeem(30, t1, "ADMIN", "STA1"), 0);
    TF_EQ(redeem(31, t2, "BOB", "STA1"), k2);
}

static void test_full(void)
{
    char t[TKT_MAX][PROTO_TKTLEN + 1];
    char sta[2];
    char last[PROTO_TKTLEN + 1];
    u8 k[TKT_MAX];
    u8 seen;
    u8 i;

    if (!fresh(0))
        return;
    sta[1] = '\0';
    seen = 0;
    for (i = 0; i < TKT_MAX; i++) {
        sta[0] = (char)('A' + i);
        k[i] = issue((u32)100 + i, "ADMIN", sta, k_h1, t[i]);
        if (k[i] >= 1 && k[i] <= TKT_MAX)
            seen |= (u8)(1 << (k[i] - 1));
    }
    TF_EQ(seen, (u8)((1 << TKT_MAX) - 1));
    /* A uses its ticket again: B is now idle the longest. */
    TF_EQ(redeem(200, t[0], "ADMIN", "A"), k[0]);
    TF_EQ(issue(300, "ADMIN", "Z", k_h1, last), k[1]);
    TF_EQ(redeem(301, t[1], "ADMIN", "B"), 0);
    TF_EQ(redeem(302, last, "ADMIN", "Z"), k[1]);
    sta[0] = 'A';
    TF_EQ(redeem(303, t[0], "ADMIN", sta), k[0]);
    for (i = 2; i < TKT_MAX; i++) {
        sta[0] = (char)('A' + i);
        TF_EQ(redeem(304, t[i], "ADMIN", sta), k[i]);
    }
}

static void test_distinct(void)
{
    u16 i;
    u16 j;
    u16 same;

    if (!fresh(0))
        return;
    for (i = 0; i < NDISTINCT; i++)
        issue(i, "ADMIN", "STA1", k_h1, g_seen[i]);
    same = 0;
    for (i = 1; i < NDISTINCT; i++)
        for (j = 0; j < i; j++)
            if (strcmp(g_seen[i], g_seen[j]) == 0)
                same++;
    TF_EQ(same, 0);
}

#ifdef COMPAT_HOST
/* The first ticket of a store opened at the time of day ticks with the
   chip at fine, stirred once more when stir, for hash; into t. */
static void first(u32 ticks, u16 fine, int stir, const char *hash, char *t)
{
    tkt_close();
    tmr_fine_set(fine);
    g_t = tkt_open(0, ticks);
    if (!TF_CHECK(g_t != NULL))
        return;
    if (stir)
        tkt_stir(ticks, 1);
    issue(0, "ADMIN", "STA1", hash, t);
}

static void test_inputs(void)
{
    char a[PROTO_TKTLEN + 1];
    char b[PROTO_TKTLEN + 1];

    first(7, 1, 0, k_h1, a);
    first(7, 1, 0, k_h1, b);
    TF_STREQ(b, a);
    first(7, 1, 0, k_h2, b);
    TF_CHECK(strcmp(b, a) != 0);
    first(7, 2, 0, k_h1, b);
    TF_CHECK(strcmp(b, a) != 0);
    first(8, 1, 0, k_h1, b);
    TF_CHECK(strcmp(b, a) != 0);
    first(7, 1, 1, k_h1, b);
    TF_CHECK(strcmp(b, a) != 0);
    tmr_fine_set(0);
}

/* The recipe of TICKET.H, rebuilt here: the pool, its fold and its
   rekey, and the seed of a ticket. */
typedef struct {
    u8   pool[PROTO_TKTLEN / 2];
    u8   at;                    /* the pool byte the next one joins     */
    u32  made;
} model;

/* Fold the stir of ticks, passes and the chip's count fine into m. */
static void m_stir(model *m, u32 ticks, u16 passes, u16 fine)
{
    u8 x[8];
    u16 i;
    u8 b;

    put_le32(x, ticks);
    put_le16(x + 4, passes);
    put_le16(x + 6, fine);
    for (i = 0; i < 8; i++) {
        b = m->pool[m->at];
        m->pool[m->at] = (u8)(((b << 1) | (b >> 7)) + x[i]);
        m->at = (u8)((m->at + 1) % sizeof m->pool);
    }
}

/* The ticket m makes at now with the chip's count fine for user on sta
   of PWHASH hash, into t; then the pool's rekey. */
static void m_issue(model *m, u32 now, u16 fine, const char *user,
                    const char *sta, const char *hash, char *t)
{
    u8 seed[ENG_MAXSEED];
    char hex[ENG_HASHLEN + 1];
    u16 n;

    memcpy(seed, m->pool, sizeof m->pool);
    n = (u16)sizeof m->pool;
    put_le32(seed + n, m->made);
    put_le16(seed + n + 4, fine);
    put_le32(seed + n + 6, now);
    n = (u16)(n + 10);
    unhex(hash, seed + n);
    n = (u16)(n + PROTO_TKTLEN / 2);
    memcpy(seed + n, user, strlen(user));
    n = (u16)(n + strlen(user));
    memcpy(seed + n, sta, strlen(sta));
    n = (u16)(n + strlen(sta));
    eng_mix128(seed, n, t);
    seed[sizeof m->pool] = 'R';
    put_le32(seed + sizeof m->pool + 1, m->made);
    eng_mix128(seed, (u16)(sizeof m->pool + 5), hex);
    unhex(hex, m->pool);
    m->made++;
}

/* Two tickets in a row, a stir between them, each input of its own
   value, as the recipe gives them. */
static void test_recipe(void)
{
    char t[PROTO_TKTLEN + 1];
    char want[PROTO_TKTLEN + 1];
    model m;

    memset(&m, 0, sizeof m);
    tkt_close();
    tmr_fine_set(0x1234);
    g_t = tkt_open(0, 0x00ABCDEFUL);
    if (!TF_CHECK(g_t != NULL))
        return;
    m_stir(&m, 0x00ABCDEFUL, 0, 0x1234);
    tmr_fine_set(0x5A5A);
    issue(77, "ADMIN", "STA1", k_h1, t);
    m_issue(&m, 77, 0x5A5A, "ADMIN", "STA1", k_h1, want);
    TF_STREQ(t, want);
    tmr_fine_set(0x0F0F);
    tkt_stir(0x00123456UL, 9);
    m_stir(&m, 0x00123456UL, 9, 0x0F0F);
    tmr_fine_set(0xC3C3);
    issue(4321, "BOB", "MB2", k_h2, t);
    m_issue(&m, 4321, 0xC3C3, "BOB", "MB2", k_h2, want);
    TF_STREQ(t, want);
    tmr_fine_set(0);
}
#else
static void test_fine(void)
{
    u16 a;
    u16 i;
    int moved;

    a = tmr_fine();
    moved = 0;
    for (i = 0; i < 1000 && !moved; i++)
        moved = tmr_fine() != a;
    TF_CHECK(moved);
}
#endif

int main(void)
{
    cmem_init();
    tf_begin("T_TICKET");
    test_open();
    test_redeem();
    test_expiry();
    test_drop();
    test_station();
    test_full();
    test_distinct();
#ifdef COMPAT_HOST
    test_inputs();
    test_recipe();
#else
    test_fine();
#endif
    tkt_close();
    return tf_done();
}
