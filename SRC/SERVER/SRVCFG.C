/*
 * SRVCFG.C - the server's settings file, INVSERV.CFG (SRVCFG.H).
 *
 * The file is streamed through a small buffer one line at a time, and
 * each line is applied to the settings as soon as it is read, so the
 * first bad line ends the reading.  LISTENS= is 0 while the file is
 * read, meaning not given, so that SESSIONS= and LISTENS= can be
 * checked against each other on whichever of their lines comes later;
 * its default is filled in at the end, as is the check that the file
 * enables a transport.
 */
#define COMPAT_UART
#define COMPAT_FIND
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "CONN.H"
#include "SRVCFG.H"

#define SC_RDBUF        64      /* bytes read from the file at a time   */
#define SC_EOF          0x1A    /* Ctrl-Z, the end of a text file       */

/* Key numbers, in the order of the words of keys. */
#define K_NAME          0
#define K_DATA          1
#define K_READONLY      2
#define K_SESSIONS      3
#define K_LISTENS       4
#define K_SERIAL        5
#define K_BAUD          6
#define K_RTSCTS        7
#define K_MAILBOX       8
#define K_IDLE          9
#define K_LOG           10
#define K_BIOS          11
#define K_SNOW          12

/* Each key has its bit in the u16 of the keys seen. */
extern char srvcfg_keys_fit[1];
extern char srvcfg_keys_fit[(K_SNOW < 16) ? 1 : 2];

static const char keys[] = "NAME DATA READONLY SESSIONS LISTENS SERIAL "
    "BAUD RTSCTS MAILBOX IDLE LOG BIOS SNOW";

/* A port of SERIAL=: these letters and the port's digit. */
static const char k_com[] = "COM";
#define COM_LEN         ((u16)(sizeof k_com - 1))

/* The file being read: its handle, the bytes read ahead and whether
   its end (or a Ctrl-Z) has been reached. */
typedef struct {
    int  h;
    u16  pos;                   /* next byte of buf                     */
    u16  len;                   /* bytes in buf                         */
    u8   end;
    char buf[SC_RDBUF];
} sc_rd;

static int blank(int ch)
{
    return ch == ' ' || ch == '\t';
}

/* 1 when n is 1..max and none of the n bytes at v is a control byte
   (below 20h, or 7Fh). */
static int text_ok(const char *v, size_t n, size_t max)
{
    size_t i;

    if (n == 0 || n > max)
        return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)v[i] < 0x20 || v[i] == 0x7F)
            return 0;
    return 1;
}

/* Read the next line of r into line (SRVCFG_MAXLINE + 2 bytes), its
   CR LF or LF dropped and a NUL added, and its length into *n; 0 or the
   CF_E code of a failed read.  A line too long to keep gives *n above
   SRVCFG_MAXLINE and line unfinished, its rest left unread.  The text
   after the last line end is a line of its own, empty when the file
   ends with a line end; r->end is then set. */
static int get_line(sc_rd *r, char *line, u16 *n)
{
    char ch;
    int rc;

    *n = 0;
    for (;;) {
        if (r->pos == r->len) {
            r->pos = 0;
            rc = cf_read(r->h, r->buf, SC_RDBUF, &r->len);
            if (rc != 0)
                return rc;
            if (r->len == 0) {
                r->end = 1;
                break;
            }
        }
        ch = r->buf[r->pos++];
        if (ch == '\n')
            break;
        if (ch == SC_EOF) {
            r->end = 1;
            break;
        }
        /* SRVCFG_MAXLINE bytes and a CR are the most a line may hold. */
        if (*n > SRVCFG_MAXLINE) {
            (*n)++;
            return 0;
        }
        line[(*n)++] = ch;
    }
    if (*n > 0 && line[*n - 1] == '\r')
        (*n)--;
    line[*n] = '\0';
    return 0;
}

/* Split the line s: the key's number into *key, the value without the
   blanks around it into *val (s is modified).  1 for a line KEY=value
   of a known key, 0 for an empty or comment line, -1 for any other. */
static int split(char *s, int *key, char **val)
{
    char *e;
    char *k;

    while (blank(*s))
        s++;
    if (*s == '\0' || *s == ';')
        return 0;
    e = strchr(s, '=');
    if (e == NULL)
        return -1;
    for (k = e; k > s && blank(k[-1]); k--)
        ;
    *k = '\0';
    *key = txt_word_num(keys, s);
    if (*key < 0)
        return -1;
    for (e++; blank(*e); e++)
        ;
    *val = e;
    e += strlen(e);
    while (e > *val && blank(e[-1]))
        e--;
    *e = '\0';
    return 1;
}

/* 1 when v is decimal digits worth lo..hi into *n, else 0. */
static int number(const char *v, u16 lo, u16 hi, u16 *n)
{
    u32 x;

    x = 0;
    if (*v == '\0')
        return 0;
    for (; *v != '\0'; v++) {
        if (!TXT_ISDIGIT(*v))
            return 0;
        x = x * 10 + (u32)(*v - '0');
        if (x > hi)
            return 0;
    }
    if (x < lo)
        return 0;
    *n = (u16)x;
    return 1;
}

/* v is 0 or 1 into *f; 0 when it is neither. */
static int flag(u8 *f, const char *v)
{
    u16 n;

    if (!number(v, 0, 1, &n))
        return 0;
    *f = (u8)n;
    return 1;
}

/* The NetBIOS name v, upper-cased, into d (NB_NAMELEN bytes); 0 when it
   is not one. */
static int set_name(char *d, const char *v)
{
    if (!txt_nbname_ok(v))
        return 0;
    txt_ucopy(d, v, NB_NAMELEN);
    return 1;
}

/* The directory v into d (ENG_MAXDIR + 1 bytes) with DOS_DIRSEP added
   unless it ends in '\', '/' or ':'; 0 when it is not text of which the
   result is at most ENG_MAXDIR characters. */
static int set_dir(char *d, const char *v)
{
    size_t n;
    int addsep;

    n = strlen(v);
    addsep = n > 0 && v[n - 1] != '\\' && v[n - 1] != '/' &&
             v[n - 1] != ':';
    if (!text_ok(v, n, (size_t)(ENG_MAXDIR - addsep)))
        return 0;
    strcpy(d, v);
    if (addsep) {
        d[n] = DOS_DIRSEP;
        d[n + 1] = '\0';
    }
    return 1;
}

/* The next item of the comma-separated list at *v: its start, with
   its length, the blanks around it left out, into *n.  *v moves past
   the item's comma, or becomes NULL after the last item. */
static const char *item(const char **v, size_t *n)
{
    const char *s;
    const char *e;

    s = *v;
    while (blank(*s))
        s++;
    for (e = s; *e != ',' && *e != '\0'; e++)
        ;
    for (*n = (size_t)(e - s); *n > 0 && blank(s[*n - 1]); (*n)--)
        ;
    *v = *e == ',' ? e + 1 : NULL;
    return s;
}

/* The station list v into d (as srvcfg.readonly), each name upper-cased
   and the names joined by single commas; 0 when v is not
   1..SRVCFG_ROMAX names of 1..ENG_STALEN characters other than control
   bytes. */
static int set_list(char *d, const char *v)
{
    const char *s;
    size_t n;
    int count;

    count = 0;
    while (v != NULL) {
        s = item(&v, &n);
        if (!text_ok(s, n, ENG_STALEN) || ++count > SRVCFG_ROMAX)
            return 0;
        if (count > 1)
            *d++ = ',';
        txt_ucopy(d, s, (u16)(n + 1));
        d += n;
    }
    return 1;
}

/* The port list v into *ports (as srvcfg.serial); 0 when v is not one
   or more different ports COM1..COM<UART_MAXPORT>. */
static int set_ports(u8 *ports, const char *v)
{
    const char *s;
    size_t n;
    u8 bit;

    *ports = 0;
    while (v != NULL) {
        s = item(&v, &n);
        if (n != COM_LEN + 1 || !txt_ieqn(s, k_com, COM_LEN) ||
            s[COM_LEN] < '1' || s[COM_LEN] > '0' + UART_MAXPORT)
            return 0;
        bit = (u8)(1 << (s[COM_LEN] - '1'));
        if (*ports & bit)
            return 0;
        *ports |= bit;
    }
    return 1;
}

/* Apply key (a K_ number) = v to c; 0 when the value is bad. */
static int apply(srvcfg *c, int key, const char *v)
{
    u16 n;

    switch (key) {
    case K_NAME:
        return set_name(c->name, v);
    case K_DATA:
        return set_dir(c->data, v);
    case K_READONLY:
        return set_list(c->readonly, v);
    case K_SESSIONS:
        if (!number(v, 0, CONN_NETMAX, &n) || c->listens > n)
            return 0;
        c->sessions = (u8)n;
        return 1;
    case K_LISTENS:
        if (!number(v, 1, c->sessions, &n))
            return 0;
        c->listens = (u8)n;
        return 1;
    case K_SERIAL:
        return set_ports(&c->serial, v);
    case K_BAUD:
        if (!number(v, UART_MINBAUD, UART_MAXBAUD, &n) || !UART_BAUDOK(n))
            return 0;
        c->baud = n;
        return 1;
    case K_RTSCTS:
        return flag(&c->rtscts, v);
    case K_MAILBOX:
        return set_dir(c->mailbox, v);
    case K_IDLE:
        if (!number(v, 0, SRVCFG_IDLEMAX, &n))
            return 0;
        c->idle = n;
        return 1;
    case K_LOG:
        if (!text_ok(v, strlen(v), SRVCFG_LOGMAX))
            return 0;
        strcpy(c->log, v);
        return 1;
    case K_BIOS:
        return flag(&c->bios, v);
    case K_SNOW:
    default:
        return flag(&c->snow, v);
    }
}

/* The default LISTENS= of c's SESSIONS=. */
static void default_listens(srvcfg *c)
{
    c->listens = (u8)(c->sessions < SRVCFG_DEFLISTENS ? c->sessions :
                      SRVCFG_DEFLISTENS);
}

void srvcfg_default(srvcfg *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->name, SRVCFG_DEFNAME);
    set_dir(c->data, "DATA");
    c->sessions = CONN_NETMAX;
    default_listens(c);
    c->baud = UART_DEFBAUD;
    c->idle = SRVCFG_DEFIDLE;
}

int srvcfg_load(srvcfg *c, const char *path, u16 *badline)
{
    sc_rd r;
    char line[SRVCFG_MAXLINE + 2];
    char *val;
    u16 seen;
    u16 lineno;
    u16 sessline;
    u16 n;
    int key;
    int ok;
    int rc;

    seen = 0;
    lineno = 0;
    sessline = 0;
    srvcfg_default(c);
    *badline = 0;
    rc = cf_open(path, CF_READ | CF_DENYW, &r.h);
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        return 0;
    if (rc != 0)
        return rc;
    r.pos = 0;
    r.len = 0;
    r.end = 0;
    c->listens = 0;
    do {
        rc = get_line(&r, line, &n);
        if (rc != 0)
            break;
        lineno++;
        if (n > SRVCFG_MAXLINE || strlen(line) != n) {
            ok = -1;
        } else {
            ok = split(line, &key, &val);
            if (ok > 0 && ((seen & (1u << key)) || !apply(c, key, val)))
                ok = -1;
            else if (ok > 0)
                seen |= (u16)(1u << key);
            if (ok > 0 && key == K_SESSIONS)
                sessline = lineno;
        }
        if (ok < 0) {
            rc = CF_EINVAL;
            *badline = lineno;
        }
    } while (rc == 0 && !r.end);
    cf_close(r.h);
    if (rc == 0 && c->sessions == 0 && c->serial == 0 &&
        c->mailbox[0] == '\0') {
        rc = CF_EINVAL;
        *badline = sessline;
    }
    if (rc != 0) {
        srvcfg_default(c);
        return rc;
    }
    if (c->listens == 0)
        default_listens(c);
    return 0;
}
