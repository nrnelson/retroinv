/*
 * DPTKT.C - LOGIN, PASSWD and BYE on a host with a login table
 * (proto_ticket_cmd of PROTO.H): the password LOGIN through the
 * failed-login throttle, LOGIN TICKET=NEW, LOGIN with a ticket,
 * PASSWD's new ticket and BYE TICKET=END.
 *
 * DISPATCH.C runs these in place of DPSESS.C's only when the host has a
 * login table, so that a program without one, as a client's local mode
 * is, links none of this.  On a session offered no tickets, TICKET= is
 * refused as any other unexpected key, and BYE is DPSESS.C's own; the
 * password login and its +OK, and PASSWD's arguments, are written here
 * again rather than shared, as sharing them would cost the client code.
 * A ticket travels only in LOGIN and in the answers of LOGIN TICKET=NEW
 * and PASSWD, always as the last pair of the line, and every copy made
 * here is wiped before the command returns.
 */
#define PROTO_TICKETS
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGUSER.H"

static const char k_ticket[] = "TICKET";

/* 1 when p is a ticket: PROTO_TKTLEN upper-case hex digits. */
static int is_ticket(const char *p)
{
    u16 n;
    char ch;

    for (n = 0; (ch = p[n]) != '\0'; n++)
        if (n >= PROTO_TKTLEN ||
            !(TXT_ISDIGIT(ch) || (ch >= 'A' && ch <= 'F')))
            return 0;
    return n == PROTO_TKTLEN;
}

/* ENG_EARG ("bad ticket") naming TICKET unless the value t is word,
   matched without regard to case, or, when hex is not NULL, a ticket,
   *hex then set to 1 (else 0). */
static int ticket_form(proto_call *c, const char *t, const char *word,
                       int *hex)
{
    if (hex != NULL)
        *hex = 0;
    if (txt_ieq(t, word))
        return ENG_OK;
    if (hex == NULL || !is_ticket(t))
        return proto_fail(c, ENG_EARG, k_ticket, "bad ticket");
    *hex = 1;
    return ENG_OK;
}

/* A new ticket of the store for user on the session's station into t
   (PROTO_TKTLEN + 1 bytes), its number into *tk. */
static int issue(proto_call *c, const char *user, char *t, u8 *tk)
{
    char hash[ENG_HASHLEN + 1];
    int rc;

    rc = eng_user_hash(c->h->e, user, hash);
    if (rc == ENG_OK)
        *tk = c->h->tickets->issue(c->h->now, user, c->s->station, hash, t);
    txt_wipe(hash, (u16)sizeof hash);
    return rc;
}

/* The +OK of a login l: its level, MUSTCHG=1 or its user's NAME=, and
   TICKET=t unless t is NULL. */
static void reply(const proto_call *c, const eng_login *l, const char *t)
{
    proto_out *o;

    o = c->o;
    proto_line_begin(o, "+OK");
    proto_line_num(o, "LEVEL", c->s->level);
    if (l->mustchg)
        proto_line_num(o, "MUSTCHG", 1);
    else
        proto_line_kv(o, "NAME", l->fullname);
    if (t != NULL)
        proto_line_kv(o, k_ticket, t);
    proto_line_end(o);
}

/* LOGIN user password, with newtkt also TICKET=NEW, a failure to make
   the ticket counting as the login's.  A user the throttle refuses has
   the password checked as one that never matches: the answer, the audit
   row and the work are those of a wrong password.  The outcome of every
   other password check of a live user is noted. */
static int pw_login(proto_call *c, const char *user, const char *pw,
                    int newtkt)
{
    char t[PROTO_TKTLEN + 1];
    proto_tickets *k;
    eng_login l;
    int held;
    u8 tk;
    int rc;

    k = c->h->tickets;
    held = k->refused(c->h->now, user);
    rc = eng_user_login(c->h->e, user, pw, held, &l);
    if (!held && l.user[0] != '\0' && (rc == ENG_OK || rc == ENG_EBADPW))
        k->note(c->h->now, l.user, rc == ENG_OK);
    tk = 0;
    if (rc == ENG_OK && newtkt)
        rc = issue(c, l.user, t, &tk);
    if (rc != ENG_OK) {
        if (sess_login_failed(c->s))
            c->close = 1;
        return rc;
    }
    sess_login_ok(c->s, &l);
    c->s->tk = tk;
    reply(c, &l, newtkt ? t : NULL);
    txt_wipe(t, (u16)sizeof t);
    return ENG_OK;
}

/* LOGIN user TICKET=t. */
static int resume(proto_call *c, const char *user, const char *t)
{
    char tag[ENG_TAGLEN];
    eng_login l;
    u8 tk;
    int rc;

    tk = c->h->tickets->redeem(c->h->now, t, user, c->s->station, tag);
    rc = eng_user_resume(c->h->e, user, tk != 0, tag, &l);
    if (rc != ENG_OK) {
        if (tk != 0 && (rc == ENG_EBADPW || rc == ENG_EDISABLED))
            c->h->tickets->drop(tk);
        sess_bye(c->s);
        c->close = 1;
        return rc;
    }
    sess_login_ok(c->s, &l);
    c->s->tk = tk;
    reply(c, &l, NULL);
    return ENG_OK;
}

/* LOGIN, with TICKET= as PROTO.H says. */
static int login(proto_call *c)
{
    const char *user;
    const char *pw;
    const char *t;
    int hex;
    int rc;

    t = c->s->tkok ? proto_get(c->r, k_ticket) : NULL;
    hex = 0;
    rc = t == NULL ? dp_args(c, 2) : proto_args_end(c, 2, k_ticket);
    if (rc == ENG_OK)
        rc = proto_need(c, 0, "USERNAME", &user);
    if (rc == ENG_OK && t != NULL)
        rc = ticket_form(c, t, "NEW", &hex);
    if (rc == ENG_OK)
        rc = hex ? proto_no_more(c, 1) : proto_need(c, 1, "PASSWORD", &pw);
    if (rc != ENG_OK)
        return rc;
    return hex ? resume(c, user, t) : pw_login(c, user, pw, t != NULL);
}

/* PASSWD old new, as in DPSESS.C; success also drops every ticket of
   the user and gives the session that held one a new one, or, when the
   new PWHASH cannot be read, none. */
static int passwd(proto_call *c)
{
    char t[PROTO_TKTLEN + 1];
    const char *oldpw;
    const char *newpw;
    proto_out *o;
    u8 tk;
    int rc;

    o = c->o;
    rc = dp_args(c, 2);
    if (rc == ENG_OK)
        rc = proto_need(c, 0, "OLD", &oldpw);
    if (rc == ENG_OK)
        rc = proto_need(c, 1, "NEW", &newpw);
    if (rc == ENG_OK)
        rc = eng_user_passwd(c->h->e, c->s->user, oldpw, newpw);
    if (rc != ENG_OK)
        return rc;
    sess_passwd_ok(c->s);
    c->h->tickets->drop_user(c->s->user);
    tk = 0;
    if (c->s->tk != 0)
        issue(c, c->s->user, t, &tk);
    c->s->tk = tk;
    if (tk == 0)
        return proto_done(c, ENG_OK);
    proto_line_begin(o, "+OK");
    proto_line_kv(o, k_ticket, t);
    proto_line_end(o);
    txt_wipe(t, (u16)sizeof t);
    return ENG_OK;
}

/* BYE, or BYE TICKET=END, which drops the session's ticket first: +OK,
   then the session is closed. */
static int bye(proto_call *c)
{
    const char *t;
    int rc;

    t = proto_get(c->r, k_ticket);
    if (t == NULL || !c->s->tkok)
        return dp_bye(c);

    rc = proto_args_end(c, 0, k_ticket);
    if (rc == ENG_OK)
        rc = ticket_form(c, t, "END", NULL);
    if (rc == ENG_OK) {
        if (c->s->tk != 0)
            c->h->tickets->drop(c->s->tk);
        sess_bye(c->s);
        c->close = 1;
    }
    return proto_done(c, rc);
}

int proto_ticket_cmd(proto_call *c)
{
    const char *verb;

    verb = c->r->arg[0];
    if (txt_ieq(verb, "LOGIN"))
        return login(c);
    return txt_ieq(verb, "BYE") ? bye(c) : passwd(c);
}
