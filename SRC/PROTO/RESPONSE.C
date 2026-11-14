/*
 * RESPONSE.C - the writing side of the line protocol: responses built
 * line by line for a sink, and the standard text of every error code.
 *
 * The writer assembles one line of at most PROTO_MAXRESP bytes in
 * proto_out.line, the dot-stuffing '.' included: a line inside +DATA
 * that begins with '.' takes parts only up to PROTO_MAXRESP - 1 bytes.
 * At the end of the line the stuffing '.' is inserted and CR LF
 * appended, in the two bytes the buffer has beyond PROTO_MAXRESP, and
 * the whole line goes to the sink in one call.  proto_err while +DATA is
 * open cannot start a new response, so it ends the open one with
 * "K ERROR=code" and the terminator instead.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "ENGINE.H"
#include "ENGTX.H"
#include "PROTO.H"

void proto_out_init(proto_out *o, proto_write_fn write, proto_room_fn room,
                    void *arg)
{
    o->write = write;
    o->room = room;
    o->arg = arg;
    o->failed = 0;
    o->indata = 0;
    o->cut = 0;
    o->len = 0;
}

int proto_out_failed(const proto_out *o)
{
    return o->failed;
}

/* Room of the sink room (NULL for unlimited) with its arg. */
static u16 room_of(proto_room_fn room, void *arg)
{
    if (room == NULL)
        return 0xFFFFu;
    return room(arg);
}

u16 proto_out_room(const proto_out *o)
{
    return room_of(o->room, o->arg);
}

void proto_sink_push(proto_out *o, proto_sink *saved, proto_write_fn write,
                     proto_room_fn room, void *arg)
{
    saved->write = o->write;
    saved->room = o->room;
    saved->arg = o->arg;
    o->write = write;
    o->room = room;
    o->arg = arg;
}

void proto_sink_pop(proto_out *o, const proto_sink *saved)
{
    o->write = saved->write;
    o->room = saved->room;
    o->arg = saved->arg;
}

u16 proto_sink_room(const proto_sink *s)
{
    return room_of(s->room, s->arg);
}

/* 1 if val must be quoted. */
static int needs_quote(const char *val)
{
    const u8 *p;

    if (*val == '\0')
        return 1;
    for (p = (const u8 *)val; *p != 0; p++)
        if (*p == ' ' || *p == '"' || *p == '\\' || *p == '=' ||
            *p < 0x20 || *p == 0x7F)
            return 1;
    return 0;
}

/* Lengths of parts are measured only up to PART_OVER, which no part
   can fit, so the counts stay small on a 16-bit int. */
#define PART_OVER (PROTO_MAXRESP + 1)

static u16 str_len(const char *s)
{
    size_t n;

    n = strlen(s);
    if (n > PROTO_MAXRESP)
        return PART_OVER;
    return (u16)n;
}

/* Bytes val takes on the wire, quotes and escapes included, or
   PART_OVER when that is more than PROTO_MAXRESP. */
static u16 value_len(const char *val)
{
    const u8 *p;
    u16 n;

    if (!needs_quote(val))
        return str_len(val);
    n = 2;
    for (p = (const u8 *)val; *p != 0 && n <= PROTO_MAXRESP; p++) {
        if (*p == '"' || *p == '\\' || *p == '\n')
            n = (u16)(n + 2);
        else if (*p < 0x20 || *p == 0x7F)
            n = (u16)(n + 4);
        else
            n++;
    }
    if (n > PROTO_MAXRESP)
        return PART_OVER;
    return n;
}

/* 1 if a part of n bytes fits the line; otherwise it is left out.  A
   line inside +DATA that begins with '.' keeps a byte free for the
   dot-stuffing, so no line sent is longer than PROTO_MAXRESP.  The
   pieces of a part are measured up to PART_OVER, so n and the line's
   length with it stay far within a u16. */
static int line_fits(proto_out *o, u16 n, int dot)
{
    u16 limit;

    limit = PROTO_MAXRESP;
    if (o->indata && dot)
        limit--;
    if (o->len + n > limit) {
        o->cut = 1;
        return 0;
    }
    return 1;
}

/* 1 if the line being built begins with '.'. */
static int starts_dot(proto_out *o)
{
    return o->len > 0 && o->line[0] == '.';
}

static void put_raw(proto_out *o, const char *s, u16 n)
{
    memcpy(o->line + o->len, s, n);
    o->len = (u16)(o->len + n);
}

static void put_char(proto_out *o, char c)
{
    o->line[o->len++] = c;
}

static void put_value(proto_out *o, const char *val)
{
    const u8 *p;

    if (!needs_quote(val)) {
        put_raw(o, val, (u16)strlen(val));
        return;
    }
    put_char(o, '"');
    for (p = (const u8 *)val; *p != 0; p++) {
        if (*p == '"' || *p == '\\') {
            put_char(o, '\\');
            put_char(o, (char)*p);
        } else if (*p == '\n') {
            put_char(o, '\\');
            put_char(o, 'n');
        } else if (*p < 0x20 || *p == 0x7F) {
            put_char(o, '\\');
            put_char(o, 'x');
            put_char(o, txt_hexdig[*p >> 4]);
            put_char(o, txt_hexdig[*p & 0x0F]);
        } else {
            put_char(o, (char)*p);
        }
    }
    put_char(o, '"');
}

static void send_bytes(proto_out *o, const char *buf, u16 n)
{
    if (o->failed)
        return;
    if (o->write(o->arg, buf, n) != 0)
        o->failed = 1;
}

void proto_line_begin(proto_out *o, const char *word)
{
    u16 n;

    o->len = 0;
    o->cut = 0;
    n = str_len(word);
    if (line_fits(o, n, word[0] == '.'))
        put_raw(o, word, n);
}

void proto_line_word(proto_out *o, const char *word)
{
    u16 n;

    n = str_len(word);
    if (line_fits(o, (u16)(n + 1), starts_dot(o))) {
        put_char(o, ' ');
        put_raw(o, word, n);
    }
}

void proto_line_value(proto_out *o, const char *val)
{
    u16 n;

    n = value_len(val);
    if (line_fits(o, (u16)(n + 1), starts_dot(o))) {
        put_char(o, ' ');
        put_value(o, val);
    }
}

void proto_line_kv(proto_out *o, const char *key, const char *val)
{
    u16 k;
    u16 n;

    k = str_len(key);
    n = value_len(val);
    if (line_fits(o, (u16)(k + n + 2), starts_dot(o))) {
        put_char(o, ' ');
        put_raw(o, key, k);
        put_char(o, '=');
        put_value(o, val);
    }
}

void proto_line_num(proto_out *o, const char *key, u32 n)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, n);
    proto_line_kv(o, key, num);
}

void proto_line_unum(proto_out *o, u32 n)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, n);
    proto_line_word(o, num);
}

void proto_line_end(proto_out *o)
{
    if (o->indata && o->len > 0 && o->line[0] == '.') {
        memmove(o->line + 1, o->line, o->len);
        o->line[0] = '.';
        o->len++;
    }
    o->line[o->len++] = '\r';
    o->line[o->len++] = '\n';
    send_bytes(o, o->line, o->len);
    o->len = 0;
}

void proto_k_kv(proto_out *o, const char *key, const char *val)
{
    proto_line_begin(o, "K");
    proto_line_kv(o, key, val);
    proto_line_end(o);
}

void proto_k_num(proto_out *o, const char *key, u32 n)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, n);
    proto_k_kv(o, key, num);
}

void proto_ok(proto_out *o, const char *text)
{
    proto_line_begin(o, "+OK");
    if (text != NULL && *text != '\0')
        proto_line_word(o, text);
    proto_line_end(o);
}

void proto_err(proto_out *o, int code, const char *text, const char *field)
{
    if (o->indata) {
        proto_k_num(o, "ERROR", (u32)code);
        proto_data_end(o);
        return;
    }
    if (text == NULL)
        text = proto_errtext(code);
    proto_line_begin(o, "-ERR");
    proto_line_unum(o, (u32)code);
    if (*text != '\0')
        proto_line_word(o, text);
    if (field != NULL && *field != '\0')
        proto_line_kv(o, "FIELD", field);
    proto_line_end(o);
}

void proto_data_begin(proto_out *o)
{
    o->indata = 0;
    proto_line_begin(o, "+DATA");
    proto_line_end(o);
    o->indata = 1;
}

void proto_data_end(proto_out *o)
{
    o->indata = 0;
    proto_line_begin(o, ".");
    proto_line_end(o);
}

/* The text number of each error code's standard text. */
static const u16 err_texts[][2] = {
    { ENG_ESYNTAX,   ENG_TX_SYNTAX },
    { ENG_EUNKNOWN,  ENG_TX_UNKCMD },
    { ENG_EARG,      ENG_TX_BADARG },
    { ENG_ETOOLONG,  ENG_TX_LONGLINE },
    { ENG_ENOTAVAIL, ENG_TX_NOTAVAIL },
    { ENG_ENOTFOUND, ENG_TX_NOTFOUND },
    { ENG_EDUP,      ENG_TX_DUP },
    { ENG_ECONSTR,   ENG_TX_CONSTR },
    { ENG_EPERM,     ENG_TX_PERM },
    { ENG_ECURSOR,   ENG_TX_BADCUR },
    { ENG_EIO,       ENG_TX_IOERR },
    { ENG_EINDEX,    ENG_TX_IXCORRUPT },
    { ENG_EFULL,     ENG_TX_DISKFULL },
    { ENG_EHANDLES,  ENG_TX_HANDLES },
    { ENG_EINUSE,    ENG_TX_INUSEBY },
    { ENG_ENOLOGIN,  ENG_TX_NOLOGIN },
    { ENG_EBADPW,    ENG_TX_BADLOGIN },
    { ENG_EBUSY,     ENG_TX_BUSY },
    { ENG_EDISABLED, ENG_TX_DISABLED },
    { ENG_EMUSTCHG,  ENG_TX_MUSTCHG },
    { ENG_EINTERNAL, ENG_TX_INTERNAL },
    { ENG_ESHUTDOWN, ENG_TX_SHUTDOWN }
};

/* The last text proto_errtext copied out of far data. */
static char proto_errbuf[ENG_TXMAX + 1];

const char *proto_errtext(int code)
{
    u16 tx;
    u16 i;

    tx = ENG_TX_ERROR;
    for (i = 0; i < sizeof err_texts / sizeof err_texts[0]; i++)
        if ((int)err_texts[i][0] == code)
            tx = err_texts[i][1];
    eng_tx_copy(tx, proto_errbuf);
    return proto_errbuf;
}
