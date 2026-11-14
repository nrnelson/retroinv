/*
 * MSG.C - the message files, INV.MSG and the server's MSG_SRVFILE, the
 * text-file reader and the screens' text helpers (MSG.H).
 *
 * The texts are kept by range of numbers (MSG_R_), each range as a
 * page: a table of offsets, one per number from the range's first up to
 * its highest with a text (0 for a number without one), then the texts,
 * unescaped and NUL-terminated; an offset counts from the page's start.
 * One far block, msg_res, holds for the whole run an index of the
 * ranges (msg_range, one per msg_rlo entry); when the file has a paged
 * range (from MSG_PAGEMIN up to MSG_PAGEEND), MSG_NSLOT page slots of
 * MSG_PAGE bytes and an area as large as the largest paged range's
 * lines; then the pages of the other ranges, which stay loaded: the
 * shared modules' texts below MSG_PAGEMIN, and the server's from
 * MSG_PAGEEND on.  A paged range's lines are read from the file into
 * that area when a text of it is first wanted, and stored from there as
 * a page in the slot used least recently: a screen pays one read for
 * each paged range it uses, at the first text of it, and never needs
 * memory for it.  The key names of the test scripts (KEYSCR's range)
 * are paged too, so scripted keys read them the same way.  The file
 * msg_load opened is opened again for each such read, so no handle is
 * held between them.
 *
 * msg_load passes over the file twice, each time from a far block of
 * its own filled with one read.  The first pass, over the whole file,
 * checks every line and notes each range's place in the file, the
 * numbers its table covers and the bytes its texts need.  Its block is
 * freed and msg_res takes its place (cmem_replace of COMPAT.H); the
 * second pass stores the pages that stay loaded from the file's start
 * up to the last of their lines, read again into a block freed at once.
 * So msg_res starts where the first block did, under every compiler,
 * and nothing msg_load freed lies under it: a hole there would make
 * what a program needs hang on which later blocks happen to fit it.
 *
 * In the stored text '^^' has become '^', and the places ^1 and ^2 are
 * the bytes MSG_PH1 and MSG_PH2.  The \xHH escapes of those two bytes
 * and of NUL are refused, so a stored byte below MSG_PH2 + 1 is always
 * a place for an argument, and msg_sub needs a single pass that never
 * looks into its arguments.
 */
#define COMPAT_REPLACE
#define MSG_PATHS
#define MSG_LETTER
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "MSGSTMP.H"

#define MSG_PH1         '\001'
#define MSG_PH2         '\002'
#define MSG_RDBUF       128     /* bytes a read copies: T_MSG's CZ_READ */
#define MSG_WHYLEN      48      /* bytes of msg_why's text and its NUL  */
#define MSG_SKIPBUF     16      /* bytes of a cut line's rest at a time */

/* The first number of each range, in order; the last range runs to
   MSG_MAXNUM - 1. */
static const u16 msg_rlo[] = {
    MSG_R_TUI, MSG_R_INV, MSG_R_LISTV, MSG_R_FORM, MSG_R_MEMO,
    MSG_R_KEYSCR, MSG_R_LOGIN, MSG_R_ITEM, MSG_R_LOC, MSG_R_ADMIN,
    MSG_R_SRCH, MSG_R_CODE, MSG_R_SET, MSG_R_VERB, MSG_R_PASS,
    MSG_R_USER, MSG_R_ASSY, MSG_R_MAINT, MSG_R_LOAN, MSG_R_HOLD,
    MSG_R_REPORT, MSG_R_SCRRPT, MSG_R_CSV, MSG_R_SRV, MSG_R_FREE
};
#define MSG_NR          (sizeof msg_rlo / sizeof msg_rlo[0])

/* A slot holding range 0 holds no page: that range, the first, is one
   of the shared ones, which are never paged.  The bounds of the paged
   ranges, MSG_PAGEMIN and MSG_PAGEEND, are msg_rlo entries, so each
   range is paged or stays loaded whole. */
extern char msg_r0_shared[1];
extern char msg_r0_shared[(MSG_R_TUI < MSG_PAGEMIN) ? 1 : 2];

/* A range's entry in the index. */
typedef struct {
    u16 at;                     /* a shared page's offset in msg_res, a
                                   paged range's lines' in the file     */
    u16 len;                    /* bytes of the range's lines           */
    u16 span;                   /* entries of the page's table          */
    u16 size;                   /* bytes of the page                    */
} msg_range;

/* Where the page slots start in msg_res, after the index, and the area
   a page's lines are read into, after the slots; without a paged range
   the loaded pages start at MSG_SLOTS. */
#define MSG_SLOTS       (MSG_NR * sizeof(msg_range))
#define MSG_READAT      (MSG_SLOTS + MSG_NSLOT * (u32)MSG_PAGE)

static char msg_buf[MSG_NBUF][MSG_MAXLEN + 1];
static u8 msg_next;
static const char *msg_dir;     /* the directory and name msg_load got, */
static const char *msg_file;    /* for the page reads                   */
static cmem_blk *msg_res;       /* index, page slots, shared pages      */
/* The page slots, last used first: each one's number and the range it
   holds, or 0. */
static u8 msg_slotn[MSG_NSLOT];
static u8 msg_slotr[MSG_NSLOT];
static char msg_whybuf[MSG_WHYLEN];

/* Reasons msg_why gives in two places.  The shared texts outgrow a far
   block only when the file nearly does. */
static const char k_notmade[] = "not made for this program";
static const char k_toolarge[] = "file too large";

/* The first line of the file is k_stamp, then four hex digits. */
static const char k_stamp[] = "=STAMP ";
#define MSG_STAMPPRE    (sizeof k_stamp - 1)

/* ---------------------------------------------------------------------
 * The message file.
 * --------------------------------------------------------------------- */
static void set_why(u16 lineno, const char *what)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, lineno);
    strcpy(msg_whybuf, "line ");
    strcat(msg_whybuf, num);
    strcat(msg_whybuf, ": ");
    strncat(msg_whybuf, what, MSG_WHYLEN - 1 - strlen(msg_whybuf));
}

/* Open dir + name for reading, denying others write access (dir "" or
   ending in a separator); 0 or a CF_E code. */
static int open_at(const char *dir, const char *name, int *h)
{
    char path[DOS_PATHMAX];

    if (strlen(dir) + strlen(name) >= sizeof path)
        return CF_ENOENT;
    strcpy(path, dir);
    strcat(path, name);
    return cf_open(path, CF_READ | CF_DENYW, h);
}

/* Read n bytes of the open file h from pos into block b at off, with one
   read: 0, the CF_E code of a failed seek or read, or CF_EINVAL when the
   file ends before pos + n. */
static int read_at(int h, u32 pos, cmem_blk *b, u16 off, u16 n)
{
    u16 got;
    int rc;

    rc = cf_seek(h, pos);
    if (rc == 0)
        rc = cf_readblk(h, b, off, n, &got);
    if (rc == 0 && got != n)
        rc = CF_EINVAL;
    return rc;
}

/* Unescape the text t into out; its length, or -1 when malformed (what
   says why).  Unescaping never lengthens a text, and the first byte of
   each plain byte or sequence, the NUL too, is copied as it is read,
   before it is checked: out needs room for t and its NUL, which the line
   t lies in has when out is at or before t.  Only '\\', '^' and the
   bytes below a space are looked at again; the length is checked at
   those, which finds a text too long where a check at every byte
   would. */
static int unescape(const char *t, char *out, const char **what)
{
    char *o;
    int h1;
    int h2;
    char c;

    o = out;
    for (;;) {
        c = *t++;
        *o++ = c;
        if ((u8)c > '^' || ((u8)c >= ' ' && c != '\\' && c != '^'))
            continue;
        if (--o - out > MSG_MAXLEN) {
            *what = "text too long";
            return -1;
        }
        if (c == '\0')
            break;
        if ((u8)c < ' ') {
            *what = "control byte";
            return -1;
        }
        if (c == '^') {
            c = *t++;
            if (c == '1')
                c = MSG_PH1;
            else if (c == '2')
                c = MSG_PH2;
            else if (c != '^') {
                *what = "bad ^ sequence";
                return -1;
            }
        } else {
            c = *t++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'x') {
                h1 = txt_hexval(*t++);
                h2 = h1 < 0 ? -1 : txt_hexval(*t++);
                if (h2 < 0 || h1 * 16 + h2 <= MSG_PH2) {
                    *what = "bad \\x escape";
                    return -1;
                }
                c = (char)(u8)(h1 * 16 + h2);
            } else if (c != '\\') {
                *what = "bad escape";
                return -1;
            }
        }
        *o++ = c;
    }
    return (int)(o - out);
}

/* split reads a number below MSG_MAXNUM with msg_dec, which takes a cap
   of at most MSG_DECCAP (the build fails otherwise). */
extern char msg_num_fits[1];
extern char msg_num_fits[(MSG_MAXNUM <= MSG_DECCAP) ? 1 : 2];

/* Split a message line "nnn text" or "nnn": the number into *num and the
   text's start into *text; 0 when malformed. */
static int split(const char *line, u16 *num, const char **text)
{
    const char *p;
    u16 n;

    p = line;
    if (*p == '0')
        return 0;
    n = msg_dec(&p, MSG_MAXNUM);
    if (n >= MSG_MAXNUM || n == 0)
        return 0;
    if (*p == ' ')
        p++;
    else if (*p != '\0')
        return 0;
    *num = n;
    *text = p;
    return 1;
}

/* 1 when s starts with four hex digits, their value then in *v. */
static int hex4(const char *s, u16 *v)
{
    int i;
    int h;

    *v = 0;
    for (i = 0; i < 4; i++) {
        h = txt_hexval(s[i]);
        if (h < 0)
            return 0;
        *v = (u16)(*v * 16 + (u16)h);
    }
    return 1;
}

/* 1 when line, of len bytes, is the stamp of the program's messages. */
static int stamp_ok(const char *line, u16 len)
{
    u16 v;

    return len == MSG_STAMPPRE + 4 &&
           strncmp(line, k_stamp, MSG_STAMPPRE) == 0 &&
           hex4(line + MSG_STAMPPRE, &v) && v == (u16)MSG_STAMP;
}

/* 1 when the range starting at lo is read as a page, else 0. */
static int paged(u16 lo)
{
    return lo >= MSG_PAGEMIN && lo < MSG_PAGEEND;
}

/* The range of message number n (1..MSG_MAXNUM - 1). */
static u16 range(u16 n)
{
    u16 r;

    r = MSG_NR - 1;
    while (msg_rlo[r] > n)
        r--;
    return r;
}

/* Offset in the block of the next byte the block reader r gives. */
static u16 tell(const msg_rd *r)
{
    return (u16)(r->at - r->got + r->pos);
}

/* The next message line of r, unescaped in place in line (MSG_MAXRAW +
   1 bytes), past comments and blank lines; *lineno counts the lines
   read.  1 with its number in *num and its length in *n, 0 at the end,
   or -1 when the line is malformed, what then saying why. */
static int next_text(msg_rd *r, char *line, u16 *lineno, u16 *num, int *n,
                     const char **what)
{
    const char *text;
    u16 len;

    do {
        if (msg_rd_line(r, line, MSG_MAXRAW, &len) <= 0)
            return 0;
        ++*lineno;
        if (len > MSG_MAXRAW) {
            *what = "line too long";
            return -1;
        }
    } while (len == 0 || line[0] == '#');
    if (!split(line, num, &text)) {
        *what = "bad number";
        return -1;
    }
    *n = unescape(text, line, what);
    return *n < 0 ? -1 : 1;
}

/* Check every line of the size bytes of the file in f and fill in each
   range's entry in rg[], zeroed by the caller, but for the place of a
   page that stays loaded.  0 or -1, msg_why then saying what and where. */
static int scan(cmem_blk *f, u16 size, msg_range *rg)
{
    msg_rd r;
    char rbuf[MSG_RDBUF];
    char line[MSG_MAXRAW + 1];
    u8 seen[MSG_MAXNUM / 8 + 1];
    u16 len;
    u16 lineno;
    u16 num;
    u16 at;
    u16 g;
    u16 prev;
    const char *what;
    int n;
    int rc;

    lineno = 1;
    prev = MSG_NR;
    what = k_notmade;
    memset(seen, 0, sizeof seen);
    msg_rd_blk(&r, f, 0, size, rbuf, (u16)sizeof rbuf);
    rc = msg_rd_line(&r, line, MSG_MAXRAW, &len) > 0 &&
         stamp_ok(line, len) ? 1 : -1;
    while (rc > 0) {
        at = tell(&r);
        rc = next_text(&r, line, &lineno, &num, &n, &what);
        if (rc <= 0)
            break;
        g = range(num);
        rc = -1;
        if (seen[num >> 3] & (1 << (num & 7)))
            what = "duplicate number";
        else if (g != prev && rg[g].len != 0)
            what = "range split";
        else
            rc = 1;
        if (rc < 0)
            break;
        seen[num >> 3] |= (u8)(1 << (num & 7));
        if (g != prev)
            rg[g].at = at;
        prev = g;
        rg[g].len = (u16)(tell(&r) - rg[g].at);
        if (num - msg_rlo[g] >= rg[g].span) {
            rg[g].size = (u16)(rg[g].size + 2 * (num - msg_rlo[g] + 1 -
                                                 rg[g].span));
            rg[g].span = (u16)(num - msg_rlo[g] + 1);
        }
        rg[g].size = (u16)(rg[g].size + n + 1);
        if (paged(msg_rlo[g]) && rg[g].size > MSG_PAGE) {
            what = "page too large";
            rc = -1;
        }
    }
    if (rc == 0)
        return 0;
    set_why(lineno, what);
    return -1;
}

/* Store the page of range r, of index entry g, at base in msg_res from
   its lines, which start at from in f.  0, or -1 when a line is not one
   of r's texts or the page outgrows g->size, as when the file has
   changed since msg_load. */
static int store(cmem_blk *f, u16 from, u16 r, const msg_range *g,
                 u16 base)
{
    msg_rd rd;
    char rbuf[MSG_RDBUF];
    char line[MSG_MAXRAW + 1];
    u8 off[2];
    const char *what;
    u16 at;
    u16 lineno;
    u16 num;
    int n;
    int rc;

    at = (u16)(base + 2 * g->span);
    lineno = 0;
    cmem_set(msg_res, base, 0, (u16)(2 * g->span));
    msg_rd_blk(&rd, f, from, (u16)(from + g->len), rbuf, (u16)sizeof rbuf);
    while ((rc = next_text(&rd, line, &lineno, &num, &n, &what)) > 0) {
        if (num < msg_rlo[r] || num - msg_rlo[r] >= g->span ||
            (u16)(n + 1) > (u16)(base + g->size - at))
            return -1;
        cmem_put(msg_res, at, line, (u16)(n + 1));
        put_le16(off, (u16)(at - base));
        cmem_put(msg_res, (u16)(base + 2 * (num - msg_rlo[r])), off, 2);
        at = (u16)(at + n + 1);
    }
    return rc;
}

/* Offset in msg_res of the slot holding the page of range r, of index
   entry g.  When no slot does, the page is read from the file msg_load
   opened into the slot used least recently.  A page that cannot be read
   is kept with an empty table, so that its texts answer "?nnn" without
   another try until the slot is used again. */
static u16 page(u16 r, const msg_range *g)
{
    u16 base;
    u8 s;
    int i;
    int h;
    int rc;

    i = 0;
    while (i < MSG_NSLOT - 1 && msg_slotr[i] != r)
        i++;
    s = msg_slotn[i];
    base = (u16)(MSG_SLOTS + s * MSG_PAGE);
    if (msg_slotr[i] != r) {
        rc = open_at(msg_dir, msg_file, &h);
        if (rc == 0) {
            rc = read_at(h, g->at, msg_res, (u16)MSG_READAT, g->len);
            cf_close(h);
        }
        if (rc != 0 || store(msg_res, (u16)MSG_READAT, r, g, base) != 0)
            cmem_set(msg_res, base, 0, (u16)(2 * g->span));
    }
    for (; i > 0; i--) {
        msg_slotr[i] = msg_slotr[i - 1];
        msg_slotn[i] = msg_slotn[i - 1];
    }
    msg_slotr[0] = (u8)r;
    msg_slotn[0] = s;
    return base;
}

int msg_load(const char *dir, const char *file)
{
    msg_range rg[MSG_NR];
    msg_range *g;
    cmem_blk *f;
    u32 size;
    u32 total;
    u32 loaded;
    u16 area;
    u16 end;
    u16 e;
    u16 r;
    int h;
    int rc;

    f = NULL;
    size = 0;
    total = 0;
    area = 0;
    end = 0;
    msg_free();
    msg_whybuf[0] = '\0';
    rc = msg_fopen(&dir, file, &h);
    msg_dir = dir;
    msg_file = file;
    if (rc != 0) {
        strcpy(msg_whybuf, "cannot open the file");
        return rc;
    }
    rc = cf_size(h, &size);
    if (rc == 0 && size > CMEM_MAXBLK) {
        strcpy(msg_whybuf, k_toolarge);
        rc = -1;
    } else if (rc == 0 && size > 0)
        rc = msg_fread(h, 0, (u16)size, &f);
    memset(rg, 0, sizeof rg);
    if (rc == 0)
        rc = scan(f, (u16)size, rg);
    cmem_free(f);
    f = NULL;
    /* The read area takes the longest lines of a paged range; the
       second pass needs the file up to end, where the last lines of a
       loaded range end. */
    for (r = 0, g = rg; r < MSG_NR; r++, g++) {
        if (paged(msg_rlo[r])) {
            if (g->len > area)
                area = g->len;
        } else {
            total += g->size;
            e = (u16)(g->at + g->len);
            if (e > end)
                end = e;
        }
    }
    loaded = area != 0 ? MSG_READAT + area : MSG_SLOTS;
    total += loaded;
    if (rc == 0 && total > CMEM_MAXBLK) {
        strcpy(msg_whybuf, k_toolarge);
        rc = -1;
    }
    if (rc == 0) {
        msg_res = cmem_replace((u16)total, (u16)size);
        if (msg_res == NULL)
            rc = CF_ENOMEM;
        else if (end != 0)
            rc = msg_fread(h, 0, end, &f);
    }
    cf_close(h);
    /* scan has checked these lines, so a page is refused only when the
       file changed between the two reads: it is then not the file that
       was sized, as one that ends early is not (read_at). */
    for (r = 0, g = rg; rc == 0 && r < MSG_NR; r++, g++) {
        if (!paged(msg_rlo[r]) && g->len != 0) {
            if (store(f, g->at, r, g, (u16)loaded) != 0)
                rc = CF_EINVAL;
            g->at = (u16)loaded;
            loaded += g->size;
        }
    }
    if (rc == 0)
        cmem_put(msg_res, 0, rg, (u16)sizeof rg);
    cmem_free(f);
    if (rc != 0) {
        if (rc == CF_ENOMEM)
            strcpy(msg_whybuf, "not enough memory");
        else if (rc > 0)
            strcpy(msg_whybuf, "cannot read the file");
        msg_free();
    }
    return rc;
}

const char *msg_why(void)
{
    return msg_whybuf;
}

void msg_free(void)
{
    u8 s;

    cmem_free(msg_res);
    msg_res = NULL;
    for (s = 0; s < MSG_NSLOT; s++) {
        msg_slotr[s] = 0;
        msg_slotn[s] = s;
    }
}

/* Text n as stored into raw (MSG_MAXLEN + 1 bytes); 0 if there is none
   or its page cannot be read, raw then holding "?nnn". */
static int fetch(u16 n, char *raw)
{
    msg_range g;
    u8 off[2];
    u16 base;
    u16 o;
    u16 r;
    u16 lo;
    u16 size;

    base = 0;
    o = 0;
    if (msg_res != NULL && n > 0 && n < MSG_MAXNUM) {
        r = range(n);
        lo = msg_rlo[r];
        cmem_get(msg_res, (u16)(r * sizeof g), &g, (u16)sizeof g);
        if (n - lo < g.span)
            base = paged(lo) ? page(r, &g) : g.at;
        if (base != 0) {
            cmem_get(msg_res, (u16)(base + 2 * (n - lo)), off, 2);
            o = get_le16(off);
        }
    }
    if (o == 0) {
        raw[0] = '?';
        txt_u32(raw + 1, n);
        return 0;
    }
    o = (u16)(o + base);
    size = (u16)(cmem_size(msg_res) - o);
    if (size > MSG_MAXLEN + 1)
        size = MSG_MAXLEN + 1;
    cmem_get(msg_res, o, raw, size);
    raw[MSG_MAXLEN] = '\0';
    return 1;
}

/* Text n with its places filled, cut to size - 1 bytes, into out;
   returns the length. */
static u16 expand(u16 n, const char *a1, const char *a2, char *out,
                  u16 size)
{
    char raw[MSG_MAXLEN + 1];
    const char *p;
    const char *a;
    u16 k;

    fetch(n, raw);
    k = 0;
    for (p = raw; *p != '\0' && k + 1 < size; p++) {
        if (*p == MSG_PH1 || *p == MSG_PH2) {
            a = *p == MSG_PH1 ? a1 : a2;
            if (a == NULL)
                continue;
            while (*a != '\0' && k + 1 < size)
                out[k++] = *a++;
        } else {
            out[k++] = *p;
        }
    }
    out[k] = '\0';
    return k;
}

/* The next of the rotating buffers. */
static char *next_buf(void)
{
    char *b;

    b = msg_buf[msg_next];
    msg_next = (u8)((msg_next + 1) % MSG_NBUF);
    return b;
}

const char *msg(u16 n)
{
    char *b;

    b = next_buf();
    expand(n, NULL, NULL, b, (u16)sizeof msg_buf[0]);
    return b;
}

const char *msg_letter(u16 first, const char *letters, const char *v)
{
    const char *p;

    p = v[0] != '\0' ? strchr(letters, v[0]) : NULL;
    return p != NULL ? msg((u16)(first + (p - letters))) : NULL;
}

const char *msg_sub(u16 n, const char *a1, const char *a2)
{
    char out[MSG_MAXLEN + 1];
    char *b;
    u16 k;

    k = expand(n, a1, a2, out, (u16)sizeof out);
    b = next_buf();
    memcpy(b, out, (size_t)k + 1);
    return b;
}

u16 msg_get(u16 n, char *buf, u16 size)
{
    if (size == 0)
        return 0;
    return expand(n, NULL, NULL, buf, size);
}

/* ---------------------------------------------------------------------
 * Reading text files.
 * --------------------------------------------------------------------- */
int msg_fopen(const char **dir, const char *name, int *h)
{
    if (**dir != '\0' && open_at(*dir, name, h) == 0)
        return 0;
    *dir = "";
    return open_at(*dir, name, h);
}

int msg_fread(int h, u32 pos, u16 n, cmem_blk **out)
{
    int rc;

    *out = cmem_alloc(n);
    if (*out == NULL)
        return CF_ENOMEM;
    rc = read_at(h, pos, *out, 0, n);
    if (rc != 0) {
        cmem_free(*out);
        *out = NULL;
    }
    return rc;
}

int msg_section(const char *dir, const char *name, const char *key,
                cmem_blk **out, u16 *n)
{
    msg_rd r;
    char rbuf[MSG_RDBUF];
    char line[MSG_IDXLINE + 1];
    u16 k;
    u16 len;
    u16 pos;
    int h;
    int rc;

    k = (u16)strlen(key);
    *out = NULL;
    *n = 0;
    rc = msg_fopen(&dir, name, &h);
    if (rc != 0)
        return rc;
    msg_rd_init(&r, h, rbuf, (u16)sizeof rbuf);
    rc = -1;
    while (msg_rd_line(&r, line, MSG_IDXLINE, &len) > 0 && line[0] == '=') {
        if (len == k + 11 && txt_ieqn(line + 1, key, k) &&
            line[k + 1] == ' ' && hex4(line + k + 2, &pos) &&
            line[k + 6] == ' ' && hex4(line + k + 7, n)) {
            if (*n > 0)
                rc = msg_fread(h, pos, *n, out);
            break;
        }
    }
    cf_close(h);
    return rc;
}

void msg_rd_init(msg_rd *r, int h, char *buf, u16 bufsz)
{
    r->buf = buf;
    r->blk = NULL;
    r->h = h;
    r->size = bufsz;
    r->pos = 0;
    r->got = 0;
    r->at = 0;
    r->eof = 0;
    r->cr = 0;
}

void msg_rd_blk(msg_rd *r, cmem_blk *b, u16 from, u16 to, char *buf,
                u16 bufsz)
{
    msg_rd_init(r, -1, buf, bufsz);
    r->blk = b;
    r->at = from;
    r->end = to;
}

int msg_rd_piece(msg_rd *r, char *p, u16 max, u16 *n, int *eol)
{
    const char *s;
    const char *lf;
    u16 k;
    u16 m;
    int rc;

    *n = 0;
    *eol = 0;
    for (;;) {
        if (r->pos >= r->got) {
            if (r->eof)
                break;
            if (r->blk != NULL) {
                r->got = (u16)(r->end - r->at);
                if (r->got > r->size)
                    r->got = r->size;
                cmem_get(r->blk, r->at, r->buf, r->got);
                r->at = (u16)(r->at + r->got);
            } else {
                rc = cf_read(r->h, r->buf, r->size, &r->got);
                if (rc != 0)
                    return -rc;
            }
            r->pos = 0;
            /* A Ctrl-Z ends the file: the bytes before it are the last,
               and the block's place moves back onto it. */
            s = (const char *)memchr(r->buf, '\032', r->got);
            if (s != NULL) {
                k = (u16)(s - r->buf);
                r->at = (u16)(r->at - (r->got - k));
                r->got = k;
                r->eof = 1;
            }
            if (r->got == 0) {
                r->eof = 1;
                break;
            }
        }
        s = r->buf + r->pos;
        k = (u16)(r->got - r->pos);
        lf = (const char *)memchr(s, '\n', k);
        if (lf != NULL)
            k = (u16)(lf - s);
        /* A CR held back at the end of the bytes read before is text,
           unless the line ends right after it. */
        if (r->cr && lf != s) {
            if (*n >= max)
                return 1;
            p[(*n)++] = '\r';
        }
        r->cr = 0;
        /* The bytes up to the line end or the end of the bytes read,
           but for a CR right before either: one before the line end is
           dropped, one at the end of the bytes read held back. */
        m = k;
        if (m != 0 && s[m - 1] == '\r')
            m--;
        /* What does not fit waits for the next piece; the piece ends
           here only when a text byte follows. */
        if (m > max - *n) {
            if (*n == max)
                return 1;
            m = (u16)(max - *n);
            k = m;
            lf = NULL;
        }
        memcpy(p + *n, s, m);
        *n = (u16)(*n + m);
        r->pos = (u16)(r->pos + k);
        if (lf != NULL) {
            r->pos++;
            *eol = 1;
            return 1;
        }
        r->cr = (u8)(m != k);
    }
    /* The last line, if a byte of it was read: a held CR counts. */
    *eol = *n != 0 || r->cr;
    r->cr = 0;
    return *eol;
}

int msg_rd_line(msg_rd *r, char *line, u16 max, u16 *len)
{
    char skip[MSG_SKIPBUF];
    u16 n;
    int eol;
    int rc;

    rc = msg_rd_piece(r, line, max, len, &eol);
    if (rc <= 0)
        return rc;
    line[*len] = '\0';
    if (!eol)
        *len = (u16)(max + 1);
    while (!eol) {
        rc = msg_rd_piece(r, skip, (u16)sizeof skip, &n, &eol);
        if (rc <= 0)
            return rc;
    }
    return 1;
}

/* ---------------------------------------------------------------------
 * Text helpers of the screens.
 * --------------------------------------------------------------------- */
void msg_path(char *out, const char *path, u16 w)
{
    u16 n;
    u16 s;
    u16 i;

    n = (u16)strlen(path);
    if (n <= w) {
        memcpy(out, path, (size_t)n + 1);
        return;
    }
    /* The tail after the cut starts at the first separator's end from
       which it fits; when there is none, the last name stands alone, cut
       at its end with MSG_CUTMARK when even that is too long. */
    s = (u16)(n + MSG_PATHCUTW - w);
    for (i = s; i < n; i++)
        if (memcmp(path + i - MSG_PATHSEPW, MSG_PATHSEP, MSG_PATHSEPW) == 0)
            break;
    if (i < n) {
        memcpy(out, MSG_PATHCUT, MSG_PATHCUTW);
        memcpy(out + MSG_PATHCUTW, path + i, (size_t)(n - i) + 1);
        return;
    }
    for (i = n; i > 0; i--)
        if (i >= MSG_PATHSEPW &&
            memcmp(path + i - MSG_PATHSEPW, MSG_PATHSEP, MSG_PATHSEPW) == 0)
            break;
    if (n - i <= w) {
        memcpy(out, path + i, (size_t)(n - i) + 1);
        return;
    }
    memcpy(out, path + i, (size_t)w - 1);
    out[w - 1] = MSG_CUTMARK;
    out[w] = '\0';
}

int msg_date(char *out, const char *wire)
{
    u16 i;
    u16 k;
    char c;

    for (i = 0; i < MSG_DATELEN; i++) {
        c = wire[i];
        if (!TXT_ISDIGIT(c))
            break;
    }
    if (i < MSG_DATELEN || wire[i] != '\0') {
        out[0] = '\0';
        return 0;
    }
    /* The hyphens go before the month and the day. */
    k = 0;
    for (i = 0; i < MSG_DATELEN; i++) {
        if (i == 4 || i == 6)
            out[k++] = '-';
        out[k++] = wire[i];
    }
    out[k] = '\0';
    return 1;
}

u16 msg_dec(const char **s, u16 cap)
{
    const char *p;
    u16 v;
    char c;

    p = *s;
    v = 0;
    while (v < cap) {
        c = *p;
        if (!TXT_ISDIGIT(c))
            break;
        v = (u16)(v * 10 + (u16)(c - '0'));
        p++;
    }
    *s = p;
    return v;
}
