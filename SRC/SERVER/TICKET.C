/*
 * TICKET.C - the server's store of login tickets (TICKET.H).
 *
 * The far block holds TKT_MAX entries side by side, each copied near to
 * be read or changed; a free entry is all zeros, its user "".  A ticket
 * is matched by its mix, the 16 bytes of eng_mix128 over its digits.
 *
 * The pool is one mix of TK_MIXLEN bytes.  An event is folded into it
 * byte by byte, each pool byte rotated before the new byte is added, so
 * that a byte's earlier inputs move to other bits; only a ticket mixes
 * it whole, as eng_mix128 costs too much on an 8088 to run for every
 * request.
 */
#define COMPAT_FINE
#define PROTO_TICKETS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGUSER.H"
#include "PROTO.H"
#include "TICKET.H"
#include "THROTL.H"

#define TK_MIXLEN       (ENG_HASHLEN / 2)   /* bytes of a mix           */
#define TK_FREE         ((u32)0xFFFFFFFEUL) /* rank of a free entry     */
#define TK_SAME         ((u32)0xFFFFFFFFUL) /* the station's own rank */

/* The u32 members come first, so that no compiler pads between the
   members. */
typedef struct {
    u32  used;                  /* tick of the issue or last redeem     */
    u32  issued;                /* tick of the issue                    */
    u8   mix[TK_MIXLEN];        /* the ticket's mix                     */
    char tag[ENG_TAGLEN];       /* its PWHASH tag, no NUL               */
    char user[ENG_USERLEN + 1]; /* "" for a free entry                  */
    char sta[ENG_STALEN + 1];
} entry;

typedef struct {
    cmem_blk *blk;              /* the entries; NULL when closed        */
    u32  idle;                  /* idle limit in ticks, 0 for none      */
    u32  made;                  /* tickets made                         */
    u8   pool[TK_MIXLEN];
    u8   at;                    /* the pool byte the next one joins     */
} store;

static store tkt_st;

static void get(u8 i, entry *t)
{
    cmem_get(tkt_st.blk, (u16)(i * sizeof *t), t, (u16)sizeof *t);
}

static void put(u8 i, const entry *t)
{
    cmem_put(tkt_st.blk, (u16)(i * sizeof *t), t, (u16)sizeof *t);
}

static void zero(u8 i)
{
    cmem_set(tkt_st.blk, (u16)(i * sizeof(entry)), 0, (u16)sizeof(entry));
}

/* The TK_MIXLEN bytes of the ENG_HASHLEN hex digits at hex into out;
   digits missing after a NUL, or not hex, count as 0. */
static void unhex(u8 *out, const char *hex)
{
    u16 i;
    int v;

    memset(out, 0, TK_MIXLEN);
    for (i = 0; i < ENG_HASHLEN && hex[i] != '\0'; i++) {
        v = txt_hexval(hex[i]);
        if (v > 0)
            out[i / 2] |= (u8)((i & 1) != 0 ? v : v << 4);
    }
}

/* eng_mix128 of the n bytes at msg into out, TK_MIXLEN bytes. */
static void mix(const u8 *msg, u16 n, u8 *out)
{
    char hex[ENG_HASHLEN + 1];

    eng_mix128(msg, n, hex);
    unhex(out, hex);
    txt_wipe(hex, (u16)sizeof hex);
}

/* Append the text s to the n bytes of seed, up to ENG_MAXSEED in all;
   the new length. */
static u16 append(u8 *seed, u16 n, const char *s)
{
    while (*s != '\0' && n < ENG_MAXSEED)
        seed[n++] = (u8)*s++;
    return n;
}

/* Fold the n bytes at x into the pool. */
static void fold(const u8 *x, u16 n)
{
    u8 b;

    for (; n > 0; n--) {
        b = tkt_st.pool[tkt_st.at];
        tkt_st.pool[tkt_st.at] = (u8)(((b << 1) | (b >> 7)) + *x++);
        tkt_st.at = (u8)((tkt_st.at + 1) % TK_MIXLEN);
    }
}

/* Zero every entry expired at now. */
static void sweep(u32 now)
{
    entry t;
    u8 i;

    for (i = 0; i < TKT_MAX; i++) {
        get(i, &t);
        if (t.user[0] != '\0' &&
            ((tkt_st.idle != 0 && now - t.used >= tkt_st.idle) ||
             now - t.issued >= TKT_LIFE))
            zero(i);
    }
}

/* proto_tickets.issue.  The entry taken is the station's own, else a
   free one, else the one idle the longest: after the sweep a live
   entry's idle time is below TKT_LIFE, so ranks below TK_FREE. */
static u8 tk_issue(u32 now, const char *user, const char *station,
                   const char *hash, char *t)
{
    u8 seed[ENG_MAXSEED];
    entry e;
    u32 rank;
    u32 r;
    u16 n;
    u16 fine;
    u8 pick;
    u8 i;

    sweep(now);
    rank = 0;
    pick = 0;
    for (i = 0; i < TKT_MAX; i++) {
        get(i, &e);
        r = e.user[0] == '\0' ? TK_FREE :
            txt_ieq(e.sta, station) ? TK_SAME : now - e.used;
        if (i == 0 || r > rank) {
            rank = r;
            pick = i;
        }
    }

    memcpy(seed, tkt_st.pool, TK_MIXLEN);
    n = TK_MIXLEN;
    put_le32(seed + n, tkt_st.made);
    n = (u16)(n + 4);
    fine = tmr_fine();
    put_le16(seed + n, fine);
    n = (u16)(n + 2);
    put_le32(seed + n, now);
    n = (u16)(n + 4);
    unhex(seed + n, hash);
    n = (u16)(n + TK_MIXLEN);
    n = append(seed, n, user);
    n = append(seed, n, station);
    eng_mix128(seed, n, t);

    seed[TK_MIXLEN] = 'R';
    put_le32(seed + TK_MIXLEN + 1, tkt_st.made);
    mix(seed, TK_MIXLEN + 5, tkt_st.pool);
    tkt_st.made++;
    txt_wipe(seed, (u16)sizeof seed);

    memset(&e, 0, sizeof e);
    e.used = now;
    e.issued = now;
    mix((const u8 *)t, PROTO_TKTLEN, e.mix);
    memcpy(e.tag, hash, ENG_TAGLEN);
    txt_copy(e.user, user, (u16)sizeof e.user);
    txt_copy(e.sta, station, (u16)sizeof e.sta);
    put(pick, &e);
    return (u8)(pick + 1);
}

/* proto_tickets.redeem. */
static u8 tk_redeem(u32 now, const char *t, const char *user,
                    const char *station, char *tag)
{
    u8 m[TK_MIXLEN];
    entry e;
    u8 i;

    sweep(now);
    mix((const u8 *)t, PROTO_TKTLEN, m);
    for (i = 0; i < TKT_MAX; i++) {
        get(i, &e);
        if (e.user[0] == '\0' || memcmp(e.mix, m, TK_MIXLEN) != 0)
            continue;
        if (!txt_ieq(e.user, user) || !txt_ieq(e.sta, station)) {
            zero(i);
            return 0;
        }
        e.used = now;
        put(i, &e);
        memcpy(tag, e.tag, ENG_TAGLEN);
        return (u8)(i + 1);
    }
    return 0;
}

/* proto_tickets.drop. */
static void tk_drop(u8 tk)
{
    if (tk >= 1 && tk <= TKT_MAX)
        zero((u8)(tk - 1));
}

/* proto_tickets.drop_user, which every PASSWD calls, the store open or
   not. */
static void tk_drop_user(const char *user)
{
    entry e;
    u8 i;

    if (tkt_st.blk == NULL)
        return;
    for (i = 0; i < TKT_MAX; i++) {
        get(i, &e);
        if (e.user[0] != '\0' && txt_ieq(e.user, user))
            zero(i);
    }
}

static proto_tickets tkt_ops = {
    tk_issue, tk_redeem, tk_drop, tk_drop_user, thr_refused, thr_note,
    proto_ticket_cmd
};

proto_tickets *tkt_logins(void)
{
    return &tkt_ops;
}

proto_tickets *tkt_open(u32 idle, u32 ticks)
{
    if (tkt_st.blk != NULL)
        return NULL;
    tkt_st.blk = cmem_alloc((u16)(TKT_MAX * sizeof(entry)));
    if (tkt_st.blk == NULL)
        return NULL;
    cmem_set(tkt_st.blk, 0, 0, (u16)(TKT_MAX * sizeof(entry)));
    tkt_st.idle = idle;
    tkt_stir(ticks, 0);
    return &tkt_ops;
}

void tkt_close(void)
{
    if (tkt_st.blk == NULL)
        return;
    cmem_set(tkt_st.blk, 0, 0, (u16)(TKT_MAX * sizeof(entry)));
    cmem_free(tkt_st.blk);
    txt_wipe(&tkt_st, (u16)sizeof tkt_st);
}

void tkt_stir(u32 ticks, u16 passes)
{
    u8 x[8];
    u16 fine;

    if (tkt_st.blk == NULL)
        return;
    fine = tmr_fine();
    put_le32(x, ticks);
    put_le16(x + 4, passes);
    put_le16(x + 6, fine);
    fold(x, (u16)sizeof x);
}

cmem_blk *tkt_block(void)
{
    return tkt_st.blk;
}
