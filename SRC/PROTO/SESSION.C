/*
 * SESSION.C - the state of one protocol session: which station it
 * speaks for, who is logged in, at what level, how many logins have
 * failed, and the login ticket it holds.
 *
 * These functions only change the proto_session; checking passwords,
 * auditing and writing responses are left to the callers in DPSESS.C,
 * and deciding which command may run in which state to proto_gate.
 * Logging out clears the user name, both levels and the ticket, so no
 * later command can act with the rights of a user who is gone.  A
 * station name is 1..ENG_STALEN bytes, none of them a control byte
 * (below 20 hex, or 7F), and neither its first nor its last byte a
 * space: AUDIT.STATION is a space-padded character field, so "STA1 "
 * would be recorded as STA1 while escaping a read-only cap on STA1.  A
 * station given by the transport (a NetBIOS machine name is padded with
 * spaces) has spaces at both ends removed first; if what is left is not
 * a station name (nothing, or a control byte), no station is fixed and
 * HELLO's argument is used as on any other transport.
 */
#define PROTO_TICKETS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "PROTO.H"

static void logout(proto_session *s)
{
    s->user[0] = '\0';
    s->userlevel = 0;
    s->level = 0;
    s->tk = 0;
}

/* 1 when the n bytes at p are a station name by the rules above, 0
   when not. */
static int station_ok(const char *p, u16 n)
{
    const u8 *q;

    if (n == 0 || n > ENG_STALEN || p[0] == ' ' || p[n - 1] == ' ')
        return 0;
    for (q = (const u8 *)p; n > 0; q++, n--)
        if (*q < 0x20 || *q == 0x7F)
            return 0;
    return 1;
}

/* Copy the n bytes of station, a name station_ok accepts, into
   s->station. */
static void set_station(proto_session *s, const char *station, u16 n)
{
    memcpy(s->station, station, n);
    s->station[n] = '\0';
}

void sess_init(proto_session *s, const char *station)
{
    u16 n;

    s->state = PROTO_S_NEW;
    s->fixsta = 0;
    s->station[0] = '\0';
    logout(s);
    s->readonly = 0;
    s->fails = 0;
    s->tkok = 0;
    if (station == NULL)
        return;
    while (*station == ' ')
        station++;
    n = (u16)strlen(station);
    if (n > ENG_STALEN)
        n = ENG_STALEN;
    while (n > 0 && station[n - 1] == ' ')
        n--;
    if (!station_ok(station, n))
        return;
    set_station(s, station, n);
    s->fixsta = 1;
}

int sess_hello(proto_session *s, const char *station, const char *readonly)
{
    u16 n;

    if (s->state == PROTO_S_CLOSED)
        return ENG_EINTERNAL;
    n = (u16)strlen(station);
    if (!station_ok(station, n))
        return ENG_EARG;
    if (!s->fixsta)
        set_station(s, station, n);
    s->readonly = 0;
    if (readonly != NULL && proto_in_list(readonly, s->station))
        s->readonly = 1;
    logout(s);
    s->state = PROTO_S_HELLO;
    return ENG_OK;
}

void sess_login_ok(proto_session *s, const eng_login *l)
{
    u16 n;

    n = (u16)strlen(l->user);
    if (n > ENG_USERLEN)
        n = ENG_USERLEN;
    memcpy(s->user, l->user, n);
    s->user[n] = '\0';
    s->userlevel = l->level;
    s->level = l->level;
    if (s->readonly && s->level > ENG_LVL_READ)
        s->level = ENG_LVL_READ;
    s->fails = 0;
    s->tk = 0;
    s->state = l->mustchg ? PROTO_S_MUSTCHG : PROTO_S_READY;
}

int sess_login_failed(proto_session *s)
{
    logout(s);
    if (s->fails < PROTO_MAXFAIL)
        s->fails++;
    if (s->fails >= PROTO_MAXFAIL) {
        s->state = PROTO_S_CLOSED;
        return 1;
    }
    s->state = PROTO_S_HELLO;
    return 0;
}

void sess_passwd_ok(proto_session *s)
{
    if (s->state == PROTO_S_MUSTCHG)
        s->state = PROTO_S_READY;
}

void sess_bye(proto_session *s)
{
    logout(s);
    s->state = PROTO_S_CLOSED;
}

int sess_level_ok(const proto_session *s, u8 level)
{
    return s->level >= level;
}
