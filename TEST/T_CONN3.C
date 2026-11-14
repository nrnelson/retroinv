/*
 * T_CONN3.C - the third part of the T_CONN test (see T_CONN.C): login
 * tickets offered only on the slots of a transport whose
 * conn_tops.tickets is 1, with the request's tick; the response that
 * carries a ticket zeroed in the output block once the slot is free,
 * and the ticket never in the slots.  A fake store stands in for the
 * server's.  Then the failed-login throttle (THROTL.H), each LOGIN on a
 * session of its own:
 * - a success clears the count, and a failure THR_SPAN ticks after the
 *   count's first starts a new one;
 * - THR_FAILS failures of ADMIN within THR_SPAN ticks, among which
 *   THR_MAX names of no user's fail, refuse every password LOGIN naming
 *   ADMIN, in any case and with TICKET=NEW, the right password answered
 *   as a wrong one, for THR_HOLD ticks from the last; a ticket LOGIN
 *   still logs in; then the right password does;
 * - the table on its own: no newcomer takes the entry of a user whose
 *   count or refusal goes on; with none free, newcomers' failures count
 *   in the overflow entry, which at THR_FAILS refuses every user outside
 *   the table for THR_HOLD ticks; a newcomer takes an entry that is
 *   over, with the overflow's count when it came from there; a success
 *   of a user outside the table leaves the overflow's count.
 * Last, two sessions saving one item's memo at once (test_notes).
 * It is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_CONN.H holds what the parts share.
 */
#define PROTO_TICKETS
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGUSER.H"
#include "PROTO.H"
#include "CONN.H"
#include "THROTL.H"
#define TF_PART
#define TF_FILE "T_CONN3"
#include "TESTFW.H"
#include "T_CONN.H"

#define TKT             "0123456789ABCDEF0123456789ABCDEF"  /* a ticket  */

static const char k_bad[] = "LOGIN ADMIN bad";     /* a wrong password  */

/* The fake ticket store: TKT, issued to ADMIN on MB1 at g_now, its tag
   kept in g_tag; the drops counted. */
static char g_tag[ENG_TAGLEN];
static u16 g_drops;

static u8 tk_issue(u32 now, const char *user, const char *station,
                   const char *hash, char *t)
{
    TF_EQ(now, g_now);
    TF_STREQ(user, "ADMIN");
    TF_STREQ(station, "MB1");
    memcpy(g_tag, hash, ENG_TAGLEN);
    strcpy(t, TKT);
    return 1;
}

static u8 tk_redeem(u32 now, const char *t, const char *user,
                    const char *station, char *tag)
{
    TF_EQ(now, g_now);
    if (strcmp(t, TKT) != 0 || !txt_ieq(user, "ADMIN") ||
        strcmp(station, "MB1") != 0)
        return 0;
    memcpy(tag, g_tag, ENG_TAGLEN);
    return 1;
}

static void tk_drop(u8 tk)
{
    TF_EQ(tk, 1);
    g_drops++;
}

static void tk_drop_user(const char *user)
{
    TF_STREQ(user, "ADMIN");
}

/* The fake store with the real throttle, which refuses nobody while it
   is closed. */
static proto_tickets g_store = {
    tk_issue, tk_redeem, tk_drop, tk_drop_user, thr_refused, thr_note,
    proto_ticket_cmd
};

void test_tickets(void)
{
    u8 c;

    start(NULL, NS, 0);
    conn_tickets(&g_store);
    c = open_slot("MB1");
    TF_STREQ(ask(c, "HELLO MB1"), k_ok);
    TF_STREQ(ask(c, "LOGIN ADMIN " PW " TICKET=NEW"),
             "-ERR 102 unexpected key FIELD=TICKET\r\n");
    hang(c);

    /* The mailbox's: a ticket, gone from the block when the slot is. */
    g_now += 7;
    c = 0;
    TF_CHECK(claim_as(&g_mbx, &c));
    conn_open(c, g_now, "MB1");
    TF_STREQ(ask(c, "HELLO MB1"), k_ok);
    TF_STREQ(ask(c, "LOGIN ADMIN " PW " TICKET=NEW"),
             "+OK LEVEL=3 NAME=\"System Administrator\" TICKET=" TKT "\r\n");
    TF_EQ(out_has(c, TKT), 1);
    TF_EQ(slots_have(TKT), 0);
    TF_STREQ(ask(c, "BYE TICKET=END"), k_ok);
    TF_EQ(g_drops, 1);
    TF_EQ(conn_state(c), CONN_FREE);
    TF_EQ(out_has(c, TKT), 0);
    TF_EQ(out_has(c, "+OK"), 0);
    TF_STREQ(ev(c), "O:MB1|R:HELLO|R:LOGIN|L:ADMIN|R:BYE|C:|");

    /* The ticket logs in. */
    TF_CHECK(claim_as(&g_mbx, &c));
    conn_open(c, g_now, "MB1");
    TF_STREQ(ask(c, "HELLO MB1"), k_ok);
    TF_STREQ(ask(c, "LOGIN admin TICKET=" TKT),
             "+OK LEVEL=3 NAME=\"System Administrator\"\r\n");
    TF_EQ(slots_have(TKT), 0);
    hang(c);
    conn_tickets(NULL);
    stop();
}

/* The answer to LOGIN line on a session of its own, from MB1 on a slot
   of the mailbox's transport, which offers tickets, at g_now. */
static const char *alone(const char *line)
{
    u8 c;

    c = 0;
    TF_CHECK(claim_as(&g_mbx, &c));
    conn_open(c, g_now, "MB1");
    TF_STREQ(ask(c, "HELLO MB1"), k_ok);
    ask(c, line);
    hang(c);
    return g_out;
}

/* n requests line, a tick apart, each on a session of its own from a
   transport that offers no tickets, each answered want. */
static void tries(const char *line, int n, const char *want)
{
    u8 c;

    while (n-- > 0) {
        c = open_slot("STA1");
        TF_STREQ(ask(c, "HELLO STA1"), k_ok);
        TF_STREQ(ask(c, line), want);
        hang(c);
        g_now++;
    }
}

/* The table on its own, from tick b, with names of no user's: T, the
   target, U1 .. U6, which take the other users' entries, and the
   newcomers V1 .. V9 and W. */
static void test_table(u32 b)
{
    char name[3];
    u32 t;
    int i;

    thr_close();
    TF_EQ(thr_open(), 0);
    name[2] = '\0';
    /* T's count outlives more newcomers than the table holds: they count
       in the overflow entry, which, at THR_FAILS, refuses every user
       outside the table for THR_HOLD ticks. */
    for (i = 1; i < THR_FAILS; i++)
        thr_note(b, "T", 0);
    name[0] = 'U';
    for (i = 1; i < THR_MAX - 1; i++) {
        name[1] = (char)('0' + i);
        thr_note(b, name, 0);
    }
    name[0] = 'V';
    for (i = 1; i < THR_FAILS; i++) {
        name[1] = (char)('0' + i);
        thr_note(b, name, 0);
    }
    TF_EQ(thr_refused(b, "V9"), 0);
    thr_note(b, "V5", 0);
    TF_EQ(thr_refused(b, "V9"), 1);
    TF_EQ(thr_refused(b, "U1"), 0);
    TF_EQ(thr_refused(b, "T"), 0);
    thr_note(b, "T", 0);
    TF_EQ(thr_refused(b, "t"), 1);
    t = b;
    t += THR_HOLD;
    TF_EQ(thr_refused(t - 1, "V9"), 1);
    TF_EQ(thr_refused(t, "V9"), 0);

    /* Once every count is over, a newcomer takes a user's entry, not the
       overflow. */
    t += THR_SPAN;
    for (i = 0; i < THR_FAILS; i++)
        thr_note(t, "W", 0);
    TF_EQ(thr_refused(t, "W"), 1);
    TF_EQ(thr_refused(t, "V9"), 0);

    /* A newcomer that leaves the overflow entry for one that has ended
       takes the overflow's count along; another outside user's success
       leaves the overflow's count alone. */
    thr_close();
    TF_EQ(thr_open(), 0);
    t += THR_HOLD;
    name[0] = 'U';
    for (i = 1; i < THR_MAX; i++) {
        name[1] = (char)('0' + i);
        thr_note(t, name, 0);
    }
    t += THR_SPAN - 1;
    for (i = 1; i < THR_FAILS; i++)
        thr_note(t, "V1", 0);
    thr_note(t, "V2", 1);
    t++;
    thr_note(t, "V1", 0);
    TF_EQ(thr_refused(t, "V1"), 1);
    thr_close();
    TF_EQ(thr_open(), 0);
    name[0] = 'U';
    for (i = 1; i < THR_MAX; i++) {
        name[1] = (char)('0' + i);
        thr_note(t, name, 0);
    }
    for (i = 1; i < THR_FAILS; i++)
        thr_note(t, "V1", 0);
    thr_note(t, "V2", 1);
    thr_note(t, "V3", 0);
    TF_EQ(thr_refused(t, "V4"), 1);
    thr_close();
}

void test_throttle(void)
{
    char bad[PROTO_MAXRESP];
    char line[sizeof "LOGIN X0 bad"];
    u32 t0;
    int i;

    start(NULL, NS, 0);
    TF_EQ(thr_open(), 0);
    TF_EQ(thr_open(), 1);
    conn_tickets(&g_store);
    txt_copy(bad, alone(k_bad), (u16)sizeof bad);
    TF_CHECK(tf_starts(bad, "-ERR 401 "));
    TF_CHECK(tf_starts(alone("LOGIN ADMIN " PW), "+OK "));

    /* A success clears the count; a failure THR_SPAN ticks after the
       count's first starts a new one. */
    tries(k_bad, THR_FAILS - 1, bad);
    TF_CHECK(tf_starts(alone("LOGIN ADMIN " PW), "+OK "));
    tries(k_bad, THR_FAILS - 1, bad);
    TF_CHECK(tf_starts(alone("LOGIN ADMIN " PW), "+OK "));
    tries(k_bad, THR_FAILS - 1, bad);
    g_now += THR_SPAN;
    tries(k_bad, THR_FAILS - 1, bad);
    TF_CHECK(tf_starts(alone("LOGIN ADMIN " PW), "+OK "));

    /* THR_FAILS failures, between which THR_MAX names of no user's fail
       and push nothing out, start a refusal at the last one, on every
       transport. */
    tries("LOGIN admin bad", THR_FAILS - 1, bad);
    strcpy(line, "LOGIN X0 bad");
    for (i = 1; i <= THR_MAX; i++) {
        line[7] = (char)('0' + i);
        tries(line, 1, bad);
    }
    t0 = g_now;
    tries(k_bad, 1, bad);
    tries("LOGIN ADMIN " PW, 1, bad);
    TF_STREQ(alone("LOGIN ADMIN " PW), bad);
    TF_STREQ(alone("LOGIN admin " PW " TICKET=NEW"), bad);
    g_now = t0 + THR_HOLD - 1;
    TF_STREQ(alone("LOGIN ADMIN " PW), bad);
    TF_STREQ(alone("LOGIN admin TICKET=" TKT),
             "+OK LEVEL=3 NAME=\"System Administrator\"\r\n");
    g_now++;
    TF_CHECK(tf_starts(alone("LOGIN ADMIN " PW), "+OK "));
    conn_tickets(NULL);
    stop();
    test_table(g_now);
}

/* "ITEM <verb> id" and the rest, asked on slot c. */
static const char *on_item(u8 c, const char *verb, const char *id,
                           const char *rest)
{
    char line[PROTO_MAXLINE + 1];

    strcpy(line, "ITEM ");
    strcat(line, verb);
    strcat(line, id);
    strcat(line, rest);
    return ask(c, line);
}

#define NOTES(c, rest)  on_item(c, "NOTES ", id, rest)

/* A mailbox slot opened from station and logged in. */
static u8 mbx_slot(const char *station)
{
    u8 c;

    c = 0;
    TF_CHECK(claim_as(&g_mbx, &c));
    conn_open(c, g_now, station);
    login(c, station);
    return c;
}

/* Two sessions save one item's memo at once, their requests in turn:
   in parts (A's NEW=, B's NEW=, then their END=s in either order), the
   memo is the whole of the one whose END= came last, and a session that
   ends after its NEW= leaves the memo as it was, the next session on
   its slot replacing its stage; two sessions of one station and user
   kept apart; a mailbox station's stage followed across its sessions,
   and two mailbox stations sharing a stage file refused, not mixed;
   with SET and ADD=, as
   an older client saves it, A's SET, B's SET, A's ADD=, B's ADD= mix the
   two, which a memo saved in parts never does. */
void test_notes(void)
{
    static const char k_ab[] = "+DATA\r\nL A-first\r\nL A-last\r\n.\r\n";
    char id[ENG_IDLEN + 1];
    u8 a, b, c, d;

    start(NULL, NS, 0);
    a = open_slot("NA");
    login(a, "NA");
    b = open_slot("NB");
    login(b, "NB");
    TF_CHECK(tf_starts(ask(a, "ITEM ADD CATEGORY=SYS"), "+OK ID="));
    id[0] = '\0';
    strncat(id, g_out + 7, ENG_IDLEN);
    id[strcspn(id, "\r")] = '\0';
    TF_STREQ(NOTES(a, " NEW=A-first"), k_ok);
    TF_STREQ(NOTES(b, " NEW=B-first"), k_ok);
    TF_STREQ(NOTES(a, " END=A-last"), k_ok);
    TF_STREQ(NOTES(b, " END=B-last"), k_ok);
    TF_STREQ(NOTES(a, ""), "+DATA\r\nL B-first\r\nL B-last\r\n.\r\n");
    TF_STREQ(NOTES(a, " NEW=A-first"), k_ok);
    TF_STREQ(NOTES(b, " NEW=B-first"), k_ok);
    TF_STREQ(NOTES(b, " END=B-last"), k_ok);
    TF_STREQ(NOTES(a, " END=A-last"), k_ok);
    TF_STREQ(NOTES(b, ""), k_ab);
    TF_STREQ(NOTES(a, " PART=x"),
             "-ERR 204 no note being saved FIELD=NOTES\r\n");
    TF_STREQ(NOTES(a, " NEW=gone"), k_ok);
    hang(a);
    TF_STREQ(NOTES(b, ""), k_ab);
    a = open_slot("NA");
    login(a, "NA");
    TF_STREQ(NOTES(a, " NEW=A-first"), k_ok);
    TF_STREQ(NOTES(a, " END=A-last"), k_ok);
    TF_STREQ(NOTES(b, ""), k_ab);

    /* Two sessions of one station and user, as the TCP bridge's or two
       on one NetBIOS name, keep their stages apart in both orders, and
       one cannot end the other's. */
    c = open_slot("NA");
    login(c, "NA");
    TF_STREQ(NOTES(a, " NEW=S-first"), k_ok);
    TF_STREQ(NOTES(c, " NEW=T-first"), k_ok);
    TF_STREQ(NOTES(a, " END=S-last"), k_ok);
    TF_STREQ(NOTES(c, " END=T-last"), k_ok);
    TF_STREQ(NOTES(b, ""), "+DATA\r\nL T-first\r\nL T-last\r\n.\r\n");
    TF_STREQ(NOTES(a, " NEW=S-first"), k_ok);
    TF_STREQ(NOTES(c, " NEW=T-first"), k_ok);
    TF_STREQ(NOTES(c, " END=T-last"), k_ok);
    TF_STREQ(NOTES(a, " END=S-last"), k_ok);
    TF_STREQ(NOTES(b, ""), "+DATA\r\nL S-first\r\nL S-last\r\n.\r\n");
    TF_STREQ(NOTES(a, " NEW=gone"), k_ok);
    hang(a);
    TF_STREQ(NOTES(c, " END=x"),
             "-ERR 204 no note being saved FIELD=NOTES\r\n");
    hang(c);

    /* A mailbox station's save goes on in its next session, each of one
       request, whatever slot it takes. */
    a = mbx_slot("MB1");
    TF_STREQ(NOTES(a, " NEW=C-first"), k_ok);
    hang(a);
    c = open_slot("NC");
    login(c, "NC");
    a = mbx_slot("MB1");
    TF_STREQ(NOTES(a, " END=C-last"), k_ok);
    hang(a);
    hang(c);
    TF_STREQ(NOTES(b, ""), "+DATA\r\nL C-first\r\nL C-last\r\n.\r\n");

    /* Two mailbox stations whose stages share a file (MB9 and MB10):
       the later NEW= takes it, the other's END= is refused, and neither
       mixes. */
    a = mbx_slot("MB9");
    d = mbx_slot("MB10");
    TF_STREQ(NOTES(a, " NEW=X-first"), k_ok);
    TF_STREQ(NOTES(d, " NEW=Y-first"), k_ok);
    TF_STREQ(NOTES(a, " END=X-last"),
             "-ERR 204 no note being saved FIELD=NOTES\r\n");
    TF_STREQ(NOTES(d, " END=Y-last"), k_ok);
    TF_STREQ(NOTES(b, ""), "+DATA\r\nL Y-first\r\nL Y-last\r\n.\r\n");
    hang(d);
    hang(a);

    a = open_slot("NA");
    login(a, "NA");
    TF_STREQ(on_item(a, "SET ", id, " NOTES=A-first"), k_ok);
    TF_STREQ(on_item(b, "SET ", id, " NOTES=B-first"), k_ok);
    TF_STREQ(NOTES(a, " ADD=A-last"), k_ok);
    TF_STREQ(NOTES(b, " ADD=B-last"), k_ok);
    TF_STREQ(NOTES(a, ""),
             "+DATA\r\nL B-first\r\nL A-last\r\nL B-last\r\n.\r\n");
    stop();
}
