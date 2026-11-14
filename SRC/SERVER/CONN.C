/*
 * CONN.C - the server's connection slots (CONN.H).
 *
 * The table is allocated at conn_init, sized to the slots asked for:
 * an array of slots in the near heap, each with its session and line
 * assembler, and one far output block of CONN_OUTSZ bytes per slot.
 * The proto_out a request is written through lives on the stack of
 * the one call that dispatches it; its sink appends to the slot's
 * block, which is empty then, since a slot with output unsent is not
 * served.
 *
 * Input is copied out of the transport's far buffer CN_CHUNK bytes at a
 * time and fed to the assembler only up to the end of the next line, so
 * the rest waits where it is for a later pass.  Every byte fed is zeroed
 * in the far buffer at once and the near copy is wiped before the
 * request runs; the assembled line and the proto_out are wiped when it
 * has been answered.  A slot whose session ends drops the input it has
 * not read, zeroed, and its output block is zeroed when it is wiped.
 *
 * The STATUS lines, the peers and the stop callback reach the table
 * through the host's argument, which is the table itself.
 */
#define PROTO_TICKETS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"

/* A slot's output block holds the longest unpaged response with room
   for a page to spare, and is a far block CMEM.C can give.  ADMIN
   CLIENTS can list every slot.
   Each pair below declares one array twice, the second time with size
   2 when the condition fails: the declarations then disagree, which
   every compiler refuses. */
extern char conn_outsz_ok[1];
extern char conn_outsz_ok[(CONN_OUTSZ >= PROTO_MAXUNPAGED + PROTO_ROOMMIN &&
                           CONN_OUTSZ <= CMEM_MAXBLK) ? 1 : 2];
extern char conn_peers_ok[1];
extern char conn_peers_ok[(CONN_MAX <= PROTO_MAXPEERS) ? 1 : 2];

#define CN_CHUNK        64      /* input bytes copied near at a time    */
#define CN_NONE         0xFF    /* no slot                              */

typedef struct {
    const conn_tops *ops;       /* the transport; NULL when CONN_FREE   */
    cmem_blk *out;              /* the output block, CONN_OUTSZ bytes   */
    cmem_blk *rx;               /* the input's buffer, NULL for none    */
    u32  seen;                  /* tick of the last request or send     */
    u16  rxoff;                 /* the input not yet read: rxlen bytes  */
    u16  rxlen;                 /*   at rxoff in rx                     */
    u16  outlen;                /* bytes of output in the block         */
    u16  outsent;               /* of them sent                         */
    u8   state;                 /* CONN_                                */
    u8   asked;                 /* ops->close has been called           */
    u8   live;                  /* CONN_EV_OPEN was sent                */
    u8   answered;              /* answered since the last conn_served  */
    proto_session sess;
    proto_acc acc;
} slot;

typedef struct {
    slot *s;                    /* the slots, NULL when not started     */
    u8   n;                     /* usable slots                         */
    u8   next;                  /* where the next pass starts           */
    u8   self;                  /* slot whose request runs, or CN_NONE  */
    u8   stop;                  /* a shutdown has begun                 */
    u32  idle;                  /* idle timeout in ticks, 0 for none    */
    u32  start;                 /* tick of conn_init                    */
    u32  stopat;                /* tick the shutdown began              */
    u32  now;                   /* latest tick any call was given       */
    conn_event_fn watch;
    proto_host host;
} table;

static table conn_tb;

/* The first words a trace text shows alone, since their second word is
   a station, a user name or a password; a user name is shown only by
   the CONN_EV_LOGIN of a LOGIN that succeeds, which is why LOGIN comes
   first here. */
static const char trace_alone[][CONN_WORDLEN + 1] = {
    "LOGIN", "HELLO", "PASSWD"
};
#define CN_NALONE       ((int)(sizeof trace_alone / sizeof trace_alone[0]))

/* Seconds in a span of t ticks, as COMPAT.H gives them. */
static u32 secs(u32 t)
{
    return t / TMR_10SEC * 10 + t % TMR_10SEC * 10 / TMR_10SEC;
}

static void event(u8 c, u8 ev, const char *text)
{
    if (conn_tb.watch != NULL)
        conn_tb.watch(c, ev, text);
}

/* Number of CONN_OPEN slots of t. */
static u16 open_count(const table *t)
{
    u16 k;
    u8 i;

    k = 0;
    for (i = 0; i < t->n; i++)
        if (t->s[i].state == CONN_OPEN)
            k++;
    return k;
}

/* 1 when c is a blank, the separator of a request's words. */
static int is_sep(int c)
{
    return c == ' ' || c == '\t';
}

/* Index in line (len bytes) of the first byte from i on that is not a
   blank, or len: a line that skips to len holds no request. */
static u16 skip(const char *line, u16 len, u16 i)
{
    while (i < len && is_sep(line[i]))
        i++;
    return i;
}

/* 1 when the word at line[*i] up to a blank or the end is letters
   alone; it is then appended to text at *n, upper-cased and cut to
   CONN_WORDLEN, and *i moves past it.  An empty word, a blank at
   line[*i] or *i at len, returns 0. */
static int trace_word(char *text, u16 *n, const char *line, u16 len,
                      u16 *i)
{
    u16 j, k;
    int c;

    k = 0;
    for (j = *i; j < len; j++) {
        c = (u8)line[j];
        if (is_sep(c))
            break;
        if (!TXT_ISALPHA(c))
            return 0;
        if (k < CONN_WORDLEN)
            text[*n + k++] = (char)TXT_UPPER(c);
    }
    if (j == *i)
        return 0;
    *n = (u16)(*n + k);
    *i = j;
    return 1;
}

/* Write the trace text of request line (len bytes) into text, which
   holds CONN_TRACELEN + 1 bytes, as conn_event_fn describes it.
   Returns 1 when its first word is LOGIN, else 0. */
static int trace(char *text, const char *line, u16 len)
{
    u16 i, n;
    int k;

    i = 0;
    n = 0;
    if (trace_word(text, &n, line, len, &i)) {
        text[n] = '\0';
        for (k = 0; k < CN_NALONE; k++)
            if (strcmp(text, trace_alone[k]) == 0)
                return k == 0;
        i = skip(line, len, i);
        text[n++] = ' ';
        if (!trace_word(text, &n, line, len, &i))
            n--;
    }
    text[n] = '\0';
    return 0;
}

/* proto_out sink: append to the slot's output block. */
static int out_write(void *arg, const char *buf, u16 len)
{
    slot *sl;

    sl = (slot *)arg;
    if (len > CONN_OUTSZ - sl->outlen)
        return 1;
    cmem_put(sl->out, sl->outlen, buf, len);
    sl->outlen = (u16)(sl->outlen + len);
    return 0;
}

/* proto_out room: the bytes left in the slot's output block. */
static u16 out_room(void *arg)
{
    return (u16)(CONN_OUTSZ - ((slot *)arg)->outlen);
}

/* Zero the input sl has not read and let it go. */
static void drop_input(slot *sl)
{
    if (sl->rx != NULL)
        cmem_set(sl->rx, sl->rxoff, 0, sl->rxlen);
    sl->rx = NULL;
    sl->rxlen = 0;
}

/* Make sl CONN_CLOSING, its input and partial line dropped. */
static void ending(slot *sl)
{
    sl->state = CONN_CLOSING;
    drop_input(sl);
    proto_acc_init(&sl->acc);
}

/* End slot c's session: ending, and ops->close called unless it was
   already. */
static void close_slot(u8 c, slot *sl)
{
    ending(sl);
    if (sl->asked)
        return;
    sl->asked = 1;
    sl->ops->close(c);
}

/* Feed slot c's input to its assembler up to the end of a line; returns
   the PROTO_ACC_ result of the last byte fed (PROTO_ACC_NONE once the
   input is used up, which is then handed back with ops->rx_done). */
static int take_line(u8 c, slot *sl)
{
    char chunk[CN_CHUNK];
    u16 i, n;
    int a;

    a = PROTO_ACC_NONE;
    while (a == PROTO_ACC_NONE && sl->rxlen > 0) {
        n = sl->rxlen < CN_CHUNK ? sl->rxlen : CN_CHUNK;
        cmem_get(sl->rx, sl->rxoff, chunk, n);
        for (i = 0; i < n && a == PROTO_ACC_NONE; i++)
            a = proto_acc_feed(&sl->acc, (int)(u8)chunk[i]);
        cmem_set(sl->rx, sl->rxoff, 0, i);
        sl->rxoff = (u16)(sl->rxoff + i);
        sl->rxlen = (u16)(sl->rxlen - i);
    }
    txt_wipe(chunk, (u16)sizeof chunk);
    if (sl->rxlen == 0 && sl->rx != NULL) {
        sl->rx = NULL;
        sl->ops->rx_done(c);
    }
    return a;
}

/* Answer the next request waiting in slot c's input, if any, at time
   now; blank lines on the way are consumed.  Returns 1 when one was
   answered. */
static int serve(u8 c, slot *sl, u32 now)
{
    char text[CONN_TRACELEN + 1];
    proto_out o;
    int a, r, login;
    char first;

    do {
        a = take_line(c, sl);
        if (a == PROTO_ACC_NONE)
            return 0;
        text[0] = '\0';
        login = a == PROTO_ACC_LINE &&
                trace(text, sl->acc.buf, sl->acc.len);
        conn_tb.self = c;
        conn_tb.host.others = (u16)(open_count(&conn_tb) - 1);
        conn_tb.host.now = now;
        proto_out_init(&o, out_write, out_room, sl);
        if (conn_tb.stop) {
            r = a == PROTO_ACC_LINE &&
                skip(sl->acc.buf, sl->acc.len, 0) == sl->acc.len ?
                PROTO_R_NONE : PROTO_R_CLOSE;
            if (r == PROTO_R_CLOSE)
                proto_err(&o, ENG_ESHUTDOWN, NULL, NULL);
        } else if (a == PROTO_ACC_LONG) {
            proto_err(&o, ENG_ETOOLONG, NULL, NULL);
            r = PROTO_R_OK;
        } else {
            r = proto_dispatch(&conn_tb.host, &sl->sess, sl->acc.buf,
                               sl->acc.len, &o);
        }
        proto_acc_wipe(&sl->acc);
        txt_wipe(&o, (u16)sizeof o);
        conn_tb.self = CN_NONE;
    } while (r == PROTO_R_NONE);
    sl->seen = now;
    sl->answered = 1;
    event(c, CONN_EV_REQ, text);
    if (login && sl->outlen > 0) {
        cmem_get(sl->out, 0, &first, 1);
        if (first == '+')
            event(c, CONN_EV_LOGIN, sl->sess.user);
    }
    if (r == PROTO_R_CLOSE)
        ending(sl);
    if (sl->outlen > 0)
        sl->ops->kick(c);
    else if (sl->state == CONN_CLOSING)
        close_slot(c, sl);
    return 1;
}

/* proto_host callbacks; arg is the table. */
static void put_status(void *arg, proto_out *o)
{
    const table *t;

    t = (const table *)arg;
    proto_k_num(o, "UPTIME", secs(t->now - t->start));
    proto_k_num(o, "SESSIONS", open_count(t));
}

static int peer_at(void *arg, u16 i, proto_peer *p)
{
    const table *t;
    const slot *sl;
    u8 k;

    t = (const table *)arg;
    for (k = 0; k < t->n; k++) {
        sl = &t->s[k];
        if (sl->state != CONN_OPEN)
            continue;
        if (i-- > 0)
            continue;
        p->idle = k == t->self ? 0 : secs(t->now - sl->seen);
        p->slot = (u8)(k + 1);
        p->state = sl->sess.state;
        p->level = sl->sess.level;
        p->self = (u8)(k == t->self);
        txt_copy(p->kind, sl->ops->kind, (u16)sizeof p->kind);
        txt_copy(p->station, sl->sess.station, (u16)sizeof p->station);
        txt_copy(p->user, sl->sess.user, (u16)sizeof p->user);
        return 0;
    }
    return 1;
}

static void stop_at(void *arg)
{
    conn_shutdown(((const table *)arg)->now);
}

/* ---------------------------------------------------------------------
 * The table.
 * --------------------------------------------------------------------- */

int conn_init(eng *e, const char *readonly, u8 nslots, u32 idle, u32 now)
{
    slot *s;
    u8 i;

    if (nslots < 1 || nslots > CONN_MAX || conn_tb.s != NULL)
        return CF_EINVAL;
    s = (slot *)malloc(nslots * sizeof *s);
    if (s == NULL)
        return CF_ENOMEM;
    memset(s, 0, nslots * sizeof *s);
    for (i = 0; i < nslots; i++) {
        s[i].out = cmem_alloc(CONN_OUTSZ);
        if (s[i].out == NULL) {
            while (i-- > 0)
                cmem_free(s[i].out);
            free(s);
            return CF_ENOMEM;
        }
    }
    conn_tb.s = s;
    conn_tb.n = nslots;
    conn_tb.next = 0;
    conn_tb.self = CN_NONE;
    conn_tb.stop = 0;
    conn_tb.idle = idle;
    conn_tb.start = now;
    conn_tb.now = now;
    memset(&conn_tb.host, 0, sizeof conn_tb.host);
    conn_tb.host.e = e;
    conn_tb.host.readonly = readonly;
    conn_tb.host.status = put_status;
    conn_tb.host.statusarg = &conn_tb;
    conn_tb.host.peers = peer_at;
    conn_tb.host.stop = stop_at;
    conn_tb.host.hostarg = &conn_tb;
    return 0;
}

void conn_free(void)
{
    u8 i;

    if (conn_tb.s == NULL)
        return;
    for (i = 0; i < conn_tb.n; i++)
        cmem_free(conn_tb.s[i].out);
    free(conn_tb.s);
    conn_tb.s = NULL;
    conn_tb.n = 0;
    conn_tb.stop = 0;
}

void conn_watch(conn_event_fn fn)
{
    conn_tb.watch = fn;
}

void conn_tickets(proto_tickets *t)
{
    conn_tb.host.tickets = t;
}

int conn_claim(const conn_tops *ops, u8 *c)
{
    u8 i;

    if (conn_tb.stop)
        return 0;
    for (i = 0; i < conn_tb.n; i++) {
        if (conn_tb.s[i].state == CONN_FREE) {
            conn_tb.s[i].ops = ops;
            conn_tb.s[i].state = CONN_LISTEN;
            *c = i;
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * A slot's session.
 * --------------------------------------------------------------------- */

void conn_open(u8 c, u32 now, const char *station)
{
    slot *sl;

    sl = &conn_tb.s[c];
    conn_tb.now = now;
    sl->seen = now;
    sess_init(&sl->sess, station);
    sl->sess.tkok = sl->ops->tickets;
    if (conn_tb.stop) {
        close_slot(c, sl);
        return;
    }
    sl->state = CONN_OPEN;
    sl->live = 1;
    event(c, CONN_EV_OPEN, sl->sess.station);
}

void conn_input(u8 c, cmem_blk *b, u16 off, u16 len)
{
    slot *sl;

    sl = &conn_tb.s[c];
    if (sl->state != CONN_OPEN) {
        cmem_set(b, off, 0, len);
        return;
    }
    sl->rx = b;
    sl->rxoff = off;
    sl->rxlen = len;
}

u16 conn_out_peek(u8 c, cmem_blk **b, u16 *off)
{
    slot *sl;

    sl = &conn_tb.s[c];
    if (sl->outlen == 0)
        return 0;
    *b = sl->out;
    *off = sl->outsent;
    return (u16)(sl->outlen - sl->outsent);
}

void conn_out_done(u8 c, u32 now, u16 n)
{
    slot *sl;

    sl = &conn_tb.s[c];
    conn_tb.now = now;
    sl->seen = now;
    sl->outsent = (u16)(sl->outsent + n);
    if (sl->outsent < sl->outlen)
        return;
    sl->outlen = 0;
    sl->outsent = 0;
    if (sl->state == CONN_CLOSING)
        close_slot(c, sl);
}

void conn_closed(u8 c)
{
    slot *sl;
    u8 live;

    if (c >= conn_tb.n || conn_tb.s[c].state == CONN_FREE)
        return;
    sl = &conn_tb.s[c];
    live = sl->live;
    drop_input(sl);
    proto_acc_init(&sl->acc);
    txt_wipe(&sl->sess, (u16)sizeof sl->sess);
    cmem_set(sl->out, 0, 0, CONN_OUTSZ);
    sl->ops = NULL;
    sl->outlen = 0;
    sl->outsent = 0;
    sl->state = CONN_FREE;
    sl->asked = 0;
    sl->live = 0;
    if (live)
        event(c, CONN_EV_CLOSE, "");
}

/* ---------------------------------------------------------------------
 * Service, timeouts and shutdown.
 * --------------------------------------------------------------------- */

u16 conn_service(u32 now)
{
    slot *sl;
    u16 done;
    u8 c, i;

    conn_tb.now = now;
    done = 0;
    c = conn_tb.next;
    if (++conn_tb.next >= conn_tb.n)
        conn_tb.next = 0;
    for (i = 0; i < conn_tb.n; i++) {
        sl = &conn_tb.s[c];
        if (sl->state == CONN_OPEN && sl->outlen == 0 && sl->rx != NULL &&
            serve(c, sl, now))
            done++;
        if (++c == conn_tb.n)
            c = 0;
    }
    return done;
}

void conn_served(u32 now)
{
    slot *sl;
    u8 c;

    conn_tb.now = now;
    for (c = 0; c < conn_tb.n; c++) {
        sl = &conn_tb.s[c];
        if (sl->answered)
            sl->seen = now;
        sl->answered = 0;
    }
}

void conn_timeouts(u32 now)
{
    slot *sl;
    int late;
    u8 c;

    conn_tb.now = now;
    late = conn_tb.stop && now - conn_tb.stopat >= CONN_SHUTWAIT;
    for (c = 0; c < conn_tb.n; c++) {
        sl = &conn_tb.s[c];
        if (sl->state == CONN_FREE || sl->asked)
            continue;
        if (late || (sl->state != CONN_LISTEN && conn_tb.idle != 0 &&
                     now - sl->seen >= conn_tb.idle))
            close_slot(c, sl);
    }
}

void conn_shutdown(u32 now)
{
    u8 c;

    if (conn_tb.stop)
        return;
    conn_tb.now = now;
    conn_tb.stop = 1;
    conn_tb.stopat = now;
    for (c = 0; c < conn_tb.n; c++)
        if (conn_tb.s[c].state == CONN_LISTEN)
            close_slot(c, &conn_tb.s[c]);
}

int conn_stopping(void)
{
    return conn_tb.stop;
}

int conn_done(void)
{
    u8 c;

    if (!conn_tb.stop)
        return 0;
    for (c = 0; c < conn_tb.n; c++)
        if (conn_tb.s[c].state != CONN_FREE)
            return 0;
    return 1;
}

/* ---------------------------------------------------------------------
 * Queries.
 * --------------------------------------------------------------------- */

u8 conn_state(u8 c)
{
    return c < conn_tb.n ? conn_tb.s[c].state : (u8)CONN_FREE;
}

u16 conn_count(void)
{
    return open_count(&conn_tb);
}

int conn_info(u16 i, proto_peer *p)
{
    return peer_at(&conn_tb, i, p);
}

u32 conn_uptime(u32 now)
{
    conn_tb.now = now;
    return secs(now - conn_tb.start);
}

const void *conn_bytes(u16 *size)
{
    *size = (u16)(conn_tb.n * sizeof *conn_tb.s);
    return conn_tb.s;
}
