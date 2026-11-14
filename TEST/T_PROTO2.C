/*
 * T_PROTO2.C - the second half of the T_PROTO test (see T_PROTO.C): the
 * directives that set up the data, the files and the host, and the
 * readers of numbers and escapes in script lines.  It is a file of its
 * own only to stay within the compiler memory of Watcom C 6.5; T_PROTO.H
 * holds what both share.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGEDIT.H"
#include "PROTO.H"
#include "T_PROTO.H"

static char g_ro[PROTO_MAXLINE + 1];    /* @READONLY's list             */
static u16 g_host_lines;                /* @STATUS                      */
static u32 g_fault_left;                /* @FAULT: disk-changing calls
                                           left before the failing one,
                                           0 when none is to fail       */
static int g_fault_code;                /* @FAULT: the failing call's
                                           CF_E code                    */
static int g_wr_h = -1;                 /* @WRITE's file while its "="
                                           lines come, else -1          */

/* The host's own state, its peers and stop functions' argument. */
typedef struct {
    u32 idle;                           /* @PEERS IDLE=                 */
    u16 peers;                          /* @PEERS                       */
    u16 self;                           /* @PEERS SELF=, from 0         */
    u16 stops;                          /* calls of host_stop           */
    u8  fixidle;                        /* IDLE= was given              */
} host_ctl;

static host_ctl g_ctl;

/* The most sessions of @PEERS: slot 2i - 1 of each fits a u8. */
#define PEERS_MAX       128

/* ---------------------------------------------------------------------
 * Numbers and escapes.
 * --------------------------------------------------------------------- */
u16 scan_digits(const char *p, u16 max, u32 *v)
{
    u16 n;

    *v = 0;
    for (n = 0; n < max && TXT_ISDIGIT(p[n]); n++)
        *v = *v * 10 + (u32)(p[n] - '0');
    return n;
}

int number(char **p, u32 *v)
{
    u16 n;

    n = scan_digits(*p, 9, v);
    if (n == 0 || ((*p)[n] != ' ' && (*p)[n] != '\0'))
        return 0;
    *p += n;
    return 1;
}

int set_count(char *arg, u16 *v)
{
    u32 n;

    if (!number(&arg, &n) || *arg != '\0' || n > 0xFFFFu)
        return 0;
    *v = (u16)n;
    return 1;
}

int decode(char *p, u16 *len, int strict)
{
    u16 n;
    u16 i;
    int h1;
    int h2;

    n = 0;
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == '\\' && p[i + 1] == '\\') {
            p[n++] = '\\';
            i++;
        } else if (p[i] == '\\' && p[i + 1] == 'x') {
            h1 = txt_hexval(p[i + 2]);
            h2 = h1 < 0 ? -1 : txt_hexval(p[i + 3]);
            if (h2 < 0)
                return 0;
            p[n++] = (char)(h1 * 16 + h2);
            i += 3;
        } else if (strict && p[i] == '\\') {
            return 0;
        } else {
            p[n++] = p[i];
        }
    }
    p[n] = '\0';
    *len = n;
    return 1;
}

/* ---------------------------------------------------------------------
 * Directives.
 * --------------------------------------------------------------------- */

/* @NOW YYYYMMDD HHMMSS. */
static int set_now(const char *arg)
{
    static const u8 at[] = { 0, 4, 6, 9, 11, 13 };
    static const u8 width[] = { 4, 2, 2, 2, 2, 2 };
    u32 v[sizeof at / sizeof at[0]];
    int i;

    if (strlen(arg) != 15 || arg[8] != ' ')
        return 0;
    for (i = 0; i < (int)(sizeof at / sizeof at[0]); i++)
        if (scan_digits(arg + at[i], width[i], &v[i]) != width[i])
            return 0;
    g_now.year = (u16)v[0];
    g_now.month = (u8)v[1];
    g_now.day = (u8)v[2];
    g_now.hour = (u8)v[3];
    g_now.minute = (u8)v[4];
    g_now.second = (u8)v[5];
    eng_set_now(g_e, &g_now);
    return 1;
}

/* The table named name, or -1. */
static int table_num(const char *name)
{
    int t;

    for (t = 0; t < ENG_NTABLES; t++)
        if (txt_ieq(name, eng_table_def(t)->name))
            return t;
    return -1;
}

/* @TABLE name [FIELDS=a,b]. */
static int dump_table(char *arg)
{
    u16 idx[DBF_MAXFIELDS];
    char val[ENG_MAXVAL];
    const eng_tdef *d;
    char *sp;
    char *spec;
    char *p;
    char *q;
    u16 n;
    u16 i;
    u32 r;
    u32 count;
    int t;
    int f;

    spec = NULL;
    sp = strchr(arg, ' ');
    if (sp != NULL) {
        *sp = '\0';
        spec = sp + 1;
        if (strncmp(spec, "FIELDS=", 7) != 0)
            return 0;
        spec += 7;
    }
    t = table_num(arg);
    if (t < 0)
        return 0;
    d = eng_table_def(t);
    n = 0;
    if (spec == NULL) {
        for (i = 0; i < d->nfields; i++)
            idx[n++] = i;
    } else {
        for (p = spec; *p != '\0'; p = q) {
            q = strchr(p, ',');
            if (q != NULL)
                *q++ = '\0';
            else
                q = p + strlen(p);
            f = eng_field_num(g_e, t, p);
            if (f < 0 || n >= DBF_MAXFIELDS)
                return 0;
            idx[n++] = (u16)f;
        }
    }

    proto_line_begin(&g_out, "F");
    for (i = 0; i < n; i++)
        proto_line_word(&g_out, eng_field(g_e, t, idx[i])->name);
    if (g_out.cut)
        return 0;
    proto_line_end(&g_out);
    count = eng_reccount(g_e, t);
    for (r = 1; r <= count; r++) {
        if (eng_rec_read(g_e, t, r) != ENG_OK)
            return 0;
        proto_line_begin(&g_out,
                         dbf_is_deleted(eng_dbf(g_e, t)) ? "D" : "R");
        for (i = 0; i < n; i++) {
            if (eng_rec_text(g_e, t, idx[i], val) != ENG_OK)
                return 0;
            proto_line_value(&g_out, val);
        }
        if (g_out.cut)
            return 0;
        proto_line_end(&g_out);
    }
    return 1;
}

/* The host's STATUS lines: "K HOSTROOM=r" when the room the writer
   reports, r, is limited, then "K HOSTLINE=i" for i = 1 .. *arg. */
static void host_status(void *arg, proto_out *o)
{
    u16 n;
    u16 i;
    u16 r;

    n = *(u16 *)arg;
    r = proto_out_room(o);
    if (r != 0xFFFFu) {
        proto_line_begin(o, "K");
        proto_line_num(o, "HOSTROOM", r);
        proto_line_end(o);
    }
    for (i = 1; i <= n; i++) {
        proto_line_begin(o, "K");
        proto_line_num(o, "HOSTLINE", i);
        proto_line_end(o);
    }
}

/* The host's sessions of @PEERS, as T_PROTO.C describes them. */
static int host_peers(void *arg, u16 i, proto_peer *p)
{
    static const u8 turns[4] = {
        PROTO_S_NEW, PROTO_S_HELLO, PROTO_S_READY, PROTO_S_MUSTCHG
    };
    const host_ctl *k;

    k = (const host_ctl *)arg;
    if (i >= k->peers)
        return 1;
    p->slot = (u8)(2 * i + 1);
    p->self = (u8)(i == k->self);
    strcpy(p->kind, i % 2 != 0 ? "TCP" : "NETB");
    if (i == k->self) {
        p->idle = 0;
        strcpy(p->station, g_sess.station);
        strcpy(p->user, g_sess.user);
        p->level = g_sess.level;
        p->state = g_sess.state;
        return 0;
    }
    p->idle = k->fixidle ? k->idle : (u32)i * 60;
    p->station[0] = '\0';
    p->user[0] = '\0';
    p->level = 0;
    p->state = turns[(i - (i > k->self ? 1 : 0)) % 4];
    if (p->state != PROTO_S_NEW) {
        strcpy(p->station, "STA");
        txt_u32(p->station + 3, p->slot);
    }
    if (p->state == PROTO_S_READY || p->state == PROTO_S_MUSTCHG) {
        p->user[0] = 'U';
        txt_u32(p->user + 1, p->slot);
        p->level = ENG_LVL_OPER;
    }
    return 0;
}

/* The host's stop function: counts its calls, and checks that the
   response holds exactly "+OK" when it is called. */
static void host_stop(void *arg)
{
    ((host_ctl *)arg)->stops++;
    count_check();
    if (g_snk.len != 5 || memcmp(g_snk.buf, "+OK\r\n", 5) != 0)
        fail_here("stop called before +OK", NULL, NULL);
}

/* 1 if p is wholly a decimal number of 0..0xFFFFFFFF, which goes into
   *v. */
static int whole_u32(const char *p, u32 *v)
{
    u16 n;

    n = scan_digits(p, TXT_U32LEN, v);
    return n != 0 && p[n] == '\0' &&
           (n < TXT_U32LEN || strcmp(p, "4294967295") <= 0);
}

/* @PEERS n [SELF=j] [IDLE=s]. */
static int set_peers(char *arg)
{
    u32 n;
    u32 self;
    u32 idle;

    idle = 0;
    self = 1;
    if (!number(&arg, &n) || n > PEERS_MAX)
        return 0;
    if (strncmp(arg, " SELF=", 6) == 0) {
        arg += 6;
        if (!number(&arg, &self) || self < 1 || self > n)
            return 0;
    }
    if (*arg != '\0' &&
        (strncmp(arg, " IDLE=", 6) != 0 || !whole_u32(arg + 6, &idle)))
        return 0;
    g_ctl.peers = (u16)n;
    g_ctl.self = (u16)(self - 1);
    g_ctl.idle = idle;
    g_ctl.fixidle = (u8)(*arg != '\0');
    g_host.peers = n != 0 ? host_peers : NULL;
    return 1;
}

/* @STOP n: the stop function from now on, and the check of its calls. */
static int set_stop(char *arg)
{
    u16 n;

    if (!set_count(arg, &n))
        return 0;
    g_host.stop = host_stop;
    count_check();
    if (g_ctl.stops != n)
        fail_here("stop calls differ", NULL, NULL);
    return 1;
}

/* @MEMO table recno field count text: count copies of text, with \\
   and \xHH decoded, become the memo of field, stored as they are. */
static int put_memo(char *arg)
{
    char *p;
    char *fld;
    char *mb;
    u32 recno;
    u32 count;
    u32 k;
    u16 len;
    u16 tl;
    int t;
    int f;
    int rc;

    p = strchr(arg, ' ');
    if (p == NULL)
        return 0;
    *p++ = '\0';
    t = table_num(arg);
    if (t < 0 || !number(&p, &recno) || recno == 0 || *p != ' ')
        return 0;
    fld = p + 1;
    p = strchr(fld, ' ');
    if (p == NULL)
        return 0;
    *p++ = '\0';
    f = eng_field_num(g_e, t, fld);
    if (f < 0 || eng_field(g_e, t, (u16)f)->type != 'M')
        return 0;
    if (!number(&p, &count) || count == 0 || *p != ' ')
        return 0;
    p++;
    if (!decode(p, &tl, 0) || tl == 0 || count > ENG_MAXMEMO / tl)
        return 0;

    rc = eng_edit_begin(g_e, t, recno);
    if (rc != ENG_OK)
        return 0;
    mb = eng_memobuf(g_e);
    len = 0;
    for (k = 0; k < count; k++) {
        memcpy(mb + len, p, tl);
        len = (u16)(len + tl);
    }
    rc = eng_edit_putmemo(g_e, (u16)f, len);
    if (rc != ENG_OK) {
        eng_edit_cancel(g_e);
        return 0;
    }
    return eng_edit_commit(g_e, 0) == ENG_OK;
}

/* @PUT table recno field value: value, converted as eng_edit_put does,
   becomes field of record recno, outside the rules of the commands. */
static int put_field(char *arg)
{
    char *p;
    char *fld;
    u32 recno;
    int t;
    int rc;

    p = strchr(arg, ' ');
    if (p == NULL)
        return 0;
    *p++ = '\0';
    t = table_num(arg);
    if (t < 0 || !number(&p, &recno) || recno == 0 || *p != ' ')
        return 0;
    fld = p + 1;
    p = strchr(fld, ' ');
    if (p == NULL)
        return 0;
    *p++ = '\0';
    if (eng_edit_begin(g_e, t, recno) != ENG_OK)
        return 0;
    rc = eng_edit_put(g_e, fld, p);
    if (rc != ENG_OK) {
        eng_edit_cancel(g_e);
        return 0;
    }
    return eng_edit_commit(g_e, 0) == ENG_OK;
}

/* The CF_E codes @FAULT takes by name: fault_names holds their names,
   each followed by a space, in the order of fault_codes. */
static const char fault_names[] = "CF_ENOENT CF_ENOPATH CF_EMFILE "
    "CF_EACCES CF_EBADF CF_ENOMEM CF_EINVACC CF_EINVAL CF_ESHARE CF_ELOCK "
    "CF_EFULL ";
static const u8 fault_codes[] = {
    CF_ENOENT, CF_ENOPATH, CF_EMFILE, CF_EACCES, CF_EBADF, CF_ENOMEM,
    CF_EINVACC, CF_EINVAL, CF_ESHARE, CF_ELOCK, CF_EFULL
};

/* The cf_fault hook of @FAULT. */
static int fault_hook(int op)
{
    if (op == CF_OP_COMMIT || op == CF_OP_CLOSE || g_fault_left == 0 ||
        --g_fault_left != 0)
        return 0;
    return g_fault_code;
}

/* @FAULT n [code]. */
static int set_fault(char *arg)
{
    const char *p;
    size_t n;
    u32 left;
    u16 code;
    u16 i;

    if (!number(&arg, &left))
        return 0;
    code = CF_EFULL;
    if (*arg == ' ') {
        arg++;
        if (strchr(arg, ' ') != NULL)
            return 0;
        n = strlen(arg);
        p = fault_names;
        for (i = 0; *p != '\0' &&
                    (strncmp(p, arg, n) != 0 || p[n] != ' '); i++)
            p = strchr(p, ' ') + 1;
        if (*p != '\0')
            code = fault_codes[i];
        else if (!set_count(arg, &code) || code == 0 || code > 255)
            return 0;
    }
    g_fault_left = left;
    g_fault_code = (int)code;
    cf_fault = left != 0 ? fault_hook : NULL;
    return 1;
}

void write_end(void)
{
    if (g_wr_h >= 0)
        cf_close(g_wr_h);
    g_wr_h = -1;
}

int write_line(char *line)
{
    u16 n;

    if (g_wr_h < 0 || (line[1] != '\0' && line[1] != ' '))
        return 0;
    n = 0;
    if (line[1] != '\0' && !decode(line + 2, &n, 0))
        return 0;
    return (n == 0 || cf_write(g_wr_h, line + 2, n) == 0) &&
           cf_write(g_wr_h, "\r\n", 2) == 0;
}

/* Byte c of a file into the output as @FILE shows it. */
static void show_byte(int c)
{
    char x[4];

    x[0] = '\\';
    x[1] = (char)(c == '\\' ? '\\' : 'x');
    x[2] = txt_hexdig[(c >> 4) & 15];
    x[3] = txt_hexdig[c & 15];
    if (c == '\\') {
        sink_write(&g_snk, x, 2);
    } else if (c < 0x20 || c >= 0x7F) {
        sink_write(&g_snk, x, 4);
    } else {
        x[0] = (char)c;
        sink_write(&g_snk, x, 1);
    }
}

/* @FILE file: its lines into the output. */
static int show_file(const char *path)
{
    u8 buf[64];
    u16 got;
    u16 i;
    int h;
    int cr;
    int part;
    int rc;

    if (cf_open(path, CF_READ | CF_DENYNONE, &h) != 0) {
        sink_write(&g_snk, "(no file)\r\n", 11);
        return 1;
    }
    cr = 0;
    part = 0;
    while ((rc = cf_read(h, buf, (u16)sizeof buf, &got)) == 0 && got > 0)
        for (i = 0; i < got; i++) {
            if (cr && buf[i] == '\n') {
                sink_write(&g_snk, "\r\n", 2);
                cr = part = 0;
                continue;
            }
            if (cr)
                show_byte('\r');
            cr = buf[i] == '\r';
            if (!cr)
                show_byte(buf[i]);
            part = 1;
        }
    if (cr)
        show_byte('\r');
    if (part)
        sink_write(&g_snk, "\r\n", 2);
    cf_close(h);
    return rc == 0;
}

void directives_reset(void)
{
    g_host.readonly = NULL;
    g_host.others = 0;
    g_host.status = NULL;
    g_host.statusarg = NULL;
    g_host_lines = 0;
    g_host.peers = NULL;
    g_host.stop = NULL;
    g_host.hostarg = &g_ctl;
    g_ctl.peers = 0;
    g_ctl.self = 0;
    g_ctl.stops = 0;
    g_ctl.idle = 0;
    g_ctl.fixidle = 0;
    cf_fault = NULL;
    g_fault_left = 0;
    tickets_reset();
}

int directive(const char *name, char *arg)
{
    if (strcmp(name, "@NOW") == 0 && arg != NULL)
        return set_now(arg);
    if (strcmp(name, "@READONLY") == 0) {
        if (arg == NULL) {
            g_host.readonly = NULL;
        } else {
            if (strlen(arg) > PROTO_MAXLINE)
                return 0;
            strcpy(g_ro, arg);
            g_host.readonly = g_ro;
        }
        return 1;
    }
    if (strcmp(name, "@ROOM") == 0 && arg != NULL)
        return set_count(arg, &g_snk.room);
    if (strcmp(name, "@TABLE") == 0 && arg != NULL)
        return dump_table(arg);
    if (strcmp(name, "@MEMO") == 0 && arg != NULL)
        return put_memo(arg);
    if (strcmp(name, "@PUT") == 0 && arg != NULL)
        return put_field(arg);
    if (strcmp(name, "@FAULT") == 0 && arg != NULL)
        return set_fault(arg);
    if (strcmp(name, "@OTHERS") == 0 && arg != NULL)
        return set_count(arg, &g_host.others);
    if (strcmp(name, "@WRITE") == 0 && arg != NULL) {
        if (cf_create(arg, &g_wr_h) == 0)
            return 1;
        g_wr_h = -1;
        return 0;
    }
    if (strcmp(name, "@FILE") == 0 && arg != NULL)
        return show_file(arg);
    if (strcmp(name, "@REMOVE") == 0 && arg != NULL) {
        cf_remove(arg);
        return 1;
    }
    if (strcmp(name, "@STATUS") == 0 && arg != NULL) {
        if (!set_count(arg, &g_host_lines))
            return 0;
        g_host.status = g_host_lines != 0 ? host_status : NULL;
        g_host.statusarg = &g_host_lines;
        return 1;
    }
    if (strcmp(name, "@PEERS") == 0 && arg != NULL)
        return set_peers(arg);
    if (strcmp(name, "@STOP") == 0 && arg != NULL)
        return set_stop(arg);
    return tickets_directive(name, arg);
}
