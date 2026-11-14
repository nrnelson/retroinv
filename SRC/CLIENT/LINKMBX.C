/*
 * LINKMBX.C - the MBOX backend of the link (LINK.H): the server's
 * mailbox (MBXCLI.H) behind the remote calls of LINKREM.H, logged in
 * with a login ticket.
 *
 * A mailbox session lasts one request file, so every request is a file
 * of its own: HELLO with the link's station, then, while the backend
 * holds a ticket, LOGIN user TICKET=t, then the request.  The answers
 * to the lines before the request are read here; the request's own is
 * the one delivered (DOC/PROTOCOL.md, Identity and login).  By its
 * command word:
 *
 *   LOGIN    sent with TICKET=NEW and without the ticket's LOGIN: the
 *            ticket its +OK ends with is kept and cut from the line
 *            delivered, and a failed LOGIN leaves no ticket
 *   PASSWD   its +OK TICKET=t replaces the ticket, cut likewise, as the
 *            change ends every ticket of the user
 *   BYE      sent with TICKET=END while a ticket is held
 *   HELLO    the client's log-out: the line alone, then, while a ticket
 *            is held, its LOGIN and BYE TICKET=END, which end it
 *
 * The response files of LOGIN and PASSWD, which hold a ticket, are
 * zeroed before they are removed (mbxcli_zero_next).
 *
 * A failed line before the request ends the server's session, so its
 * answer is the request's: -ERR 501 from a server that began to shut
 * down while it served the file, or a storage error or 500 to the
 * ticket's LOGIN, the server then keeping the ticket.  (A server that
 * shuts down takes no new file: the request then gets no answer.)  A
 * ticket the server refuses, 401 or 403 (it expired, its user's
 * password changed or the user was disabled), is dropped.  BYE, PING
 * and VER, which need no login, are then posted again without it; any
 * other request is answered -ERR 400 here, as the server answers a
 * command that needs a login, which sends the client back to its login
 * (RQERR.H).  So a request, and any password in it, goes in one request
 * file only.
 *
 * The request is posted by response, not by send: send keeps the line's
 * address, which LINK.C's call of response follows while the line is
 * still the caller's, so that the line can be posted again with no copy
 * of it kept here.
 *
 * Secrets.  The backend never holds a password: a request goes from
 * the caller's buffer, through a line built on the stack and wiped once
 * queued, into the mailbox client's queue, zeroed once posted.  It
 * holds the station, the ticket's user and the ticket, the ticket wiped
 * when it ends and all of them when the backend is freed; a line that
 * ends with a ticket is handed on from a copy of what comes before it.
 */
#define LINK_MBOX
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "LINKREM.H"
#include "MBXCLI.H"

/* The kinds of request: from 1 the number of the command word in
   k_verbs, of which BYE, PING and VER, LM_BYE and up, need no login;
   the longest word; the protocol's answers to a refused ticket. */
#define LM_OTHER        0
#define LM_HELLO        1
#define LM_LOGIN        2
#define LM_PASSWD       3
#define LM_BYE          4
#define LM_VERBLEN      6
#define LM_EBADPW       401
#define LM_EDISABLED    403

/* Seconds the close waits for the answer to the file that ends the
   ticket, so that the station's next program does not take that answer
   for its own.  It is one call: when the post fails, the ticket is left
   to expire in the server's ticket store. */
#define LM_ENDWAIT      10

/* What the answers to the lines before the request leave (lm_rd.state). */
#define LM_NEXT         0       /* the request's answer comes next      */
#define LM_SAID         1       /* a failure was its answer             */
#define LM_REFUSED      2       /* the ticket was refused               */

static const char k_verbs[] = "HELLO LOGIN PASSWD BYE PING VER";
static const char w_hello[] = "HELLO ";
static const char w_login[] = "LOGIN ";
static const char w_bye[] = "BYE";
static const char k_tkey[] = " TICKET=";
static const char k_new[] = "NEW";
static const char k_end[] = "END";

/* The answer to a command that needs a login: the server's, without its
   reason. */
static const char k_nologin[] = "-ERR 400";

/* A request with TICKET= and its value after it, the longest line. */
#define LM_LINESZ       (LINK_MAXLINE + sizeof k_tkey - 1 + sizeof k_new - 1)

typedef struct {
    mbxcli *m;
    const char *line;           /* the request send was given, len
                                   bytes                                */
    u16  len;
    u8   kind;                  /* its LM_                              */
    char sta[MBXCLI_STALEN + 1];
    char user[LINK_USERLEN + 1];    /* the ticket's user                */
    char tkt[LINK_TKTLEN + 1];  /* the ticket, "" while none is held    */
} lm_sess;

/* A line being built, which may hold a password or the ticket. */
typedef struct {
    char b[LM_LINESZ];
    u16  n;
} lm_line;

/* The reading of one request file's answers. */
typedef struct {
    lm_sess *t;
    link_line_fn fn;            /* the caller's, with arg               */
    void *arg;
    u8   state;                 /* LM_NEXT, LM_SAID or LM_REFUSED       */
    u8   tk;                    /* the answer read is the ticket's
                                   LOGIN's                              */
} lm_rd;

/* 1 when the line of len bytes starts with +OK. */
static int is_ok(const char *line, u16 len)
{
    return len >= 3 && memcmp(line, "+OK", 3) == 0;
}

/* Append the n bytes at s to the line l. */
static void add(lm_line *l, const char *s, u16 n)
{
    memcpy(l->b + l->n, s, n);
    l->n = (u16)(l->n + n);
}

/* Queue the line l on m, unless *rc holds a failure already, which then
   stays; empty l. */
static void put(mbxcli *m, lm_line *l, int *rc)
{
    if (*rc == MBXCLI_OK)
        *rc = mbxcli_queue(m, l->b, l->n);
    l->n = 0;
}

/* The word after any blanks at *p, before e: its start, its length into
   *n, *p moved past it. */
static const char *word(const char **p, const char *e, u16 *n)
{
    const char *s;

    s = *p;
    while (s < e && (*s == ' ' || *s == '\t'))
        s++;
    *p = s;
    while (*p < e && **p != ' ' && **p != '\t')
        (*p)++;
    *n = (u16)(*p - s);
    return s;
}

/* Take t's request apart: its kind, and for LOGIN its user into
   t->user, "" when that is longer than a user name. */
static void parse(lm_sess *t)
{
    char verb[LM_VERBLEN + 1];
    const char *p;
    const char *e;
    const char *s;
    u16 n;

    p = t->line;
    e = p + t->len;
    s = word(&p, e, &n);
    t->kind = LM_OTHER;
    if (n <= LM_VERBLEN) {
        memcpy(verb, s, n);
        verb[n] = '\0';
        t->kind = (u8)(txt_word_num(k_verbs, verb) + 1);
    }
    if (t->kind != LM_LOGIN)
        return;
    s = word(&p, e, &n);
    if (n > LINK_USERLEN)
        n = 0;
    memcpy(t->user, s, n);
    t->user[n] = '\0';
}

/* Drop the ticket t holds and its user. */
static void drop(lm_sess *t)
{
    txt_wipe(t->user, (u16)sizeof t->user);
    txt_wipe(t->tkt, (u16)sizeof t->tkt);
}

/* Queue the lines of the request file of t's request, with the ticket's
   LOGIN when tk: MBXCLI_OK or MBXCLI_EFULL. */
static int queue_file(lm_sess *t, int tk)
{
    lm_line l;
    int rc;

    rc = MBXCLI_OK;
    l.n = 0;
    if (t->kind == LM_HELLO) {
        add(&l, t->line, t->len);
    } else {
        add(&l, w_hello, (u16)(sizeof w_hello - 1));
        add(&l, t->sta, (u16)strlen(t->sta));
    }
    put(t->m, &l, &rc);
    if (tk) {
        add(&l, w_login, (u16)(sizeof w_login - 1));
        add(&l, t->user, (u16)strlen(t->user));
        add(&l, k_tkey, (u16)(sizeof k_tkey - 1));
        add(&l, t->tkt, LINK_TKTLEN);
        put(t->m, &l, &rc);
    }
    if (t->kind != LM_HELLO)
        add(&l, t->line, t->len);
    else if (tk)
        add(&l, w_bye, (u16)(sizeof w_bye - 1));
    if (t->kind == LM_LOGIN) {
        add(&l, k_tkey, (u16)(sizeof k_tkey - 1));
        add(&l, k_new, (u16)(sizeof k_new - 1));
    } else if (tk && (t->kind == LM_BYE || t->kind == LM_HELLO)) {
        add(&l, k_tkey, (u16)(sizeof k_tkey - 1));
        add(&l, k_end, (u16)(sizeof k_end - 1));
    }
    if (l.n > 0)
        put(t->m, &l, &rc);
    txt_wipe(&l, (u16)sizeof l);
    return rc;
}

/* LR_ code of a result of mbxcli_post or mbxcli_response. */
static int lr_code(int rc)
{
    return rc == MBXCLI_OK ? 0 : rc == MBXCLI_EOUTPUT ? LR_EOUTPUT :
           LR_ELOST;
}

/* mbxcli_line_fn of the answer to a line before the request: LM_NEXT
   for +OK, LM_REFUSED for the ticket's LOGIN refused, else the line
   handed on as the request's answer. */
static int pre_line(void *arg, const char *line, u16 len)
{
    lm_rd *r;
    int code;

    r = (lm_rd *)arg;
    if (is_ok(line, len)) {
        r->state = LM_NEXT;
        return 0;
    }
    code = link_err_code(line, len);
    if (r->tk && (code == LM_EBADPW || code == LM_EDISABLED)) {
        r->state = LM_REFUSED;
        return 0;
    }
    return r->fn(r->arg, line, len);
}

/* Post the request file of t's request, with the ticket's LOGIN when
   tk, and read the answers to the lines before the request into
   r->state; 0, LR_ELOST or LR_EOUTPUT. */
static int exchange(lm_sess *t, int tk, lm_rd *r)
{
    int rc;
    int n;
    int i;

    rc = queue_file(t, tk);
    if (t->kind == LM_LOGIN || (tk && t->kind == LM_PASSWD))
        mbxcli_zero_next(t->m);
    if (rc == MBXCLI_OK)
        rc = mbxcli_post(t->m, LINK_NETWAIT);
    r->state = LM_NEXT;
    n = t->kind == LM_HELLO ? 0 : 1 + tk;
    for (i = 0; rc == MBXCLI_OK && r->state == LM_NEXT && i < n; i++) {
        r->tk = (u8)(i == 1);
        r->state = LM_SAID;
        rc = mbxcli_response(t->m, pre_line, r);
    }
    return lr_code(rc);
}

/* Where the ticket pair at the end of the line of len bytes starts,
   or len when it ends with none. */
static u16 ticket_at(const char *line, u16 len)
{
    u16 n;
    u16 i;

    n = (u16)(sizeof k_tkey - 1 + LINK_TKTLEN);
    if (len < n || memcmp(line + len - n, k_tkey, sizeof k_tkey - 1) != 0)
        return len;
    for (i = (u16)(len - LINK_TKTLEN); i < len; i++)
        if (txt_hexval(line[i]) < 0)
            return len;
    return (u16)(len - n);
}

/* mbxcli_line_fn of the request's answer: each line to the caller's
   fn, a +OK to LOGIN or PASSWD that ends with a ticket handed on
   without it, the ticket kept while its user is known. */
static int main_line(void *arg, const char *line, u16 len)
{
    char cut[LINK_MAXRESP + 1];
    lm_rd *r;
    lm_sess *t;
    u16 n;

    r = (lm_rd *)arg;
    t = r->t;
    n = ticket_at(line, len);
    if (n == len || !is_ok(line, len) ||
        (t->kind != LM_LOGIN && t->kind != LM_PASSWD))
        return r->fn(r->arg, line, len);
    if (t->user[0] != '\0') {
        memcpy(t->tkt, line + len - LINK_TKTLEN, LINK_TKTLEN);
        t->tkt[LINK_TKTLEN] = '\0';
    }
    memcpy(cut, line, n);
    cut[n] = '\0';
    return r->fn(r->arg, cut, n);
}

/* LINKREM.H's send: keep the line for response. */
static int lm_send(void *s, const char *line, u16 len)
{
    lm_sess *t;

    t = (lm_sess *)s;
    t->line = line;
    t->len = len;
    return 0;
}

static int lm_response(void *s, link_line_fn fn, void *arg)
{
    lm_sess *t;
    lm_rd r;
    int tk;
    int rc;

    t = (lm_sess *)s;
    r.t = t;
    r.fn = fn;
    r.arg = arg;
    parse(t);
    tk = t->tkt[0] != '\0' && t->kind != LM_LOGIN;
    if (t->kind == LM_LOGIN)
        txt_wipe(t->tkt, (u16)sizeof t->tkt);
    rc = exchange(t, tk, &r);
    if (t->kind == LM_HELLO || t->kind == LM_BYE)
        drop(t);
    if (rc == 0 && r.state == LM_REFUSED) {
        drop(t);
        if (t->kind >= LM_BYE)
            rc = exchange(t, 0, &r);
        else if (fn(arg, k_nologin, (u16)(sizeof k_nologin - 1)) != 0)
            rc = LR_EOUTPUT;
    }
    if (rc == 0 && r.state == LM_NEXT)
        rc = lr_code(mbxcli_response(t->m, main_line, &r));
    t->line = NULL;
    return rc;
}

/* Free s once the session has ended or been lost, every byte wiped. */
static void lm_ended(void *s)
{
    lm_sess *t;

    t = (lm_sess *)s;
    mbxcli_close(t->m);
    txt_wipe(t, (u16)sizeof *t);
    free(t);
}

/* End the live session: the ticket, while one is held, ended by a file
   of its own whose answer is waited for LM_ENDWAIT seconds at most, so
   that the station's next program does not take that answer for its
   own; then free s. */
static void lm_close(void *s)
{
    lm_sess *t;

    t = (lm_sess *)s;
    t->line = w_bye;
    t->len = (u16)(sizeof w_bye - 1);
    t->kind = LM_BYE;
    if (t->tkt[0] != '\0' && queue_file(t, 1) == MBXCLI_OK)
        mbxcli_post(t->m, LM_ENDWAIT);
    lm_ended(s);
}

static const link_rops mbx_ops = {
    lm_send, lm_response, lm_ended, lm_close
};

int link_open_mbox(link **out, const char *dir, const char *station)
{
    lm_sess *t;
    int rc;

    *out = NULL;
    t = (lm_sess *)malloc(sizeof *t);
    if (t == NULL)
        return LINK_ENOMEM;
    memset(t, 0, sizeof *t);
    rc = mbxcli_open(&t->m, dir, station);
    if (rc != MBXCLI_OK) {
        free(t);
        return rc == MBXCLI_EARG ? LINK_EMBXARG : LINK_ENOMEM;
    }
    strcpy(t->sta, station);
    rc = link_open_remote(out, t, &mbx_ops, NULL);
    if (rc != 0)
        lm_ended(t);
    return rc;
}
