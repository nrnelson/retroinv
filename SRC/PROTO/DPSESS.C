/*
 * DPSESS.C - the session commands: HELLO LOGIN PASSWD WHOAMI BYE PING
 * VER STATUS.
 *
 * proto_gate has already checked that the session may run the command;
 * these handlers check their arguments, call SESSION.C and the engine,
 * and write the response.  None of them takes KEY=value pairs, and each
 * takes a fixed number of positional arguments: more give ENG_ESYNTAX
 * ("too many arguments"), fewer ENG_EARG naming the missing one.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGUSER.H"

static const char k_station[] = "STATION";
static const char k_level[] = "LEVEL";

int dp_args(proto_call *c, u16 n)
{
    int rc;

    rc = proto_only_keys(c, "");
    if (rc == ENG_OK)
        rc = proto_no_more(c, n);
    return rc;
}

/* HELLO station: names the station and logs any user out. */
int dp_hello(proto_call *c)
{
    const char *sta;
    int rc;

    rc = dp_args(c, 1);
    if (rc == ENG_OK)
        rc = proto_need(c, 0, k_station, &sta);
    if (rc != ENG_OK)
        return rc;
    rc = sess_hello(c->s, sta, c->h->readonly);
    if (rc == ENG_EARG)
        return proto_fail(c, rc, k_station, "bad station name");
    return proto_done(c, rc);
}

/* LOGIN user password.  Every failure once the arguments are present (a
   wrong name or password, a disabled account, an error of the engine)
   logs the session out and counts; the PROTO_MAXFAIL-th one closes the
   session after its -ERR line. */
int dp_login(proto_call *c)
{
    const char *user;
    const char *pw;
    eng_login l;
    proto_out *o;
    int rc;

    o = c->o;
    rc = dp_args(c, 2);
    if (rc == ENG_OK)
        rc = proto_need(c, 0, "USERNAME", &user);
    if (rc == ENG_OK)
        rc = proto_need(c, 1, "PASSWORD", &pw);
    if (rc != ENG_OK)
        return rc;
    rc = eng_user_login(c->h->e, user, pw, 0, &l);
    if (rc != ENG_OK) {
        if (sess_login_failed(c->s))
            c->close = 1;
        return rc;
    }
    sess_login_ok(c->s, &l);
    proto_line_begin(o, "+OK");
    proto_line_num(o, k_level, c->s->level);
    if (l.mustchg)
        proto_line_num(o, "MUSTCHG", 1);
    else
        proto_line_kv(o, "NAME", l.fullname);
    proto_line_end(o);
    return ENG_OK;
}

/* PASSWD old new: the logged-in user's own password.  A wrong old
   password is ENG_EBADPW but not a failed login. */
int dp_passwd(proto_call *c)
{
    const char *oldpw;
    const char *newpw;
    int rc;

    rc = dp_args(c, 2);
    if (rc == ENG_OK)
        rc = proto_need(c, 0, "OLD", &oldpw);
    if (rc == ENG_OK)
        rc = proto_need(c, 1, "NEW", &newpw);
    if (rc != ENG_OK)
        return rc;
    rc = eng_user_passwd(c->h->e, c->s->user, oldpw, newpw);
    if (rc == ENG_OK)
        sess_passwd_ok(c->s);
    return proto_done(c, rc);
}

/* WHOAMI: user, effective level and station. */
int dp_whoami(proto_call *c)
{
    proto_out *o;
    int rc;

    o = c->o;
    rc = dp_args(c, 0);
    if (rc != ENG_OK)
        return rc;
    proto_data_begin(o);
    proto_k_kv(o, "USER", c->s->user);
    proto_k_num(o, k_level, c->s->level);
    proto_k_kv(o, k_station, c->s->station);
    proto_data_end(o);
    return ENG_OK;
}

/* BYE: +OK, then the session is closed. */
int dp_bye(proto_call *c)
{
    int rc;

    rc = dp_args(c, 0);
    if (rc == ENG_OK) {
        sess_bye(c->s);
        c->close = 1;
    }
    return proto_done(c, rc);
}

int dp_ping(proto_call *c)
{
    return proto_done(c, dp_args(c, 0));
}

int dp_ver(proto_call *c)
{
    int rc;

    rc = dp_args(c, 0);
    if (rc != ENG_OK)
        return rc;
    proto_ok(c->o, PROTO_VERSION);
    return ENG_OK;
}

/* The sink wrapped around the output while STATUS writes: it counts
   the bytes of the response and passes each line while the response,
   with the K TRUNCATED=1 line and the terminator after it, stays within
   PROTO_MAXUNPAGED, and drops the line that would not fit and every
   later one.  Only the host's lines can be dropped: the table counts
   take about 150 bytes. */
typedef struct {
    proto_sink below;           /* the output's own sink                */
    u32  total;                 /* bytes of the response so far         */
    u8   cut;                   /* a host line was dropped              */
} status_sink;

/* "K TRUNCATED=1" and "." with their CR LF. */
#define STATUS_TAIL     ((u32)sizeof "K TRUNCATED=1\r\n" - 1 + 3)

static int status_write(void *arg, const char *buf, u16 len)
{
    status_sink *k;

    k = (status_sink *)arg;
    if (k->cut || k->total + len + STATUS_TAIL > PROTO_MAXUNPAGED) {
        k->cut = 1;
        return 0;
    }
    k->total += len;
    return k->below.write(k->below.arg, buf, len);
}

static u16 status_room(void *arg)
{
    return proto_sink_room(&((status_sink *)arg)->below);
}

/* STATUS: the record count of every table but SEQ, deleted records
   included, in table order, then the host's own lines, cut with
   K TRUNCATED=1 where they would take the response past
   PROTO_MAXUNPAGED. */
int dp_status(proto_call *c)
{
    proto_out *o;
    status_sink k;
    int t;
    int rc;

    o = c->o;
    rc = dp_args(c, 0);
    if (rc != ENG_OK)
        return rc;
    k.total = 0;
    k.cut = 0;
    proto_sink_push(o, &k.below, status_write, status_room, &k);
    proto_data_begin(o);
    for (t = 0; t < ENG_NTABLES; t++)
        if (t != ENG_T_SEQ)
            proto_k_num(o, eng_table_def(t)->name, eng_reccount(c->h->e, t));
    if (c->h->status != NULL)
        c->h->status(c->h->statusarg, o);
    proto_sink_pop(o, &k.below);
    if (k.cut)
        proto_k_num(o, "TRUNCATED", 1);
    proto_data_end(o);
    return ENG_OK;
}
