/*
 * CFG.C - the client's settings file (CFG.H).
 *
 * Both directions stream the file through a small buffer with the
 * text-file reader of MSG.H, which also ends the file at a Ctrl-Z.  A
 * line of at most CFG_MAXLINE bytes (CR LF excluded) is read whole and
 * may be understood; a longer one is never understood, and a save
 * copies it through in pieces.  When a known key occurs more than
 * once, reading takes the first line whose value is good (a bad one is
 * counted and the next tried); a save rewrites the first line of the
 * key, good or bad, with the value in effect, and drops the later
 * ones.  A choice (ITEMORD, PRINTER, PRTYPE) is matched without regard
 * to case and written in capitals; a number (PAGELEN, LABELW, LABELH,
 * WINSPLIT, each width of WINCOLS, WINPANE's) is one to three decimal
 * digits within its range, and BAUD one of the rates of UART_BAUDOK, in
 * decimal digits.  The keys of WINCOLS are one per table and those of
 * WINPANE one per screen, numbered after the keys of cfg_keys, and read
 * from the word lists cfg_cols and cfg_panes.
 */
#define COMPAT_UART
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "MSG.H"
#include "PRINT.H"
#include "RPT.H"
#include "CFG.H"

#define CFG_CHOICEMAX   5       /* longest PRINTER or PRTYPE word       */
#define CFG_RDBUF       64      /* bytes read from the file at a time   */

/* Key numbers, in the order of cfg_keys. */
#define K_DATA          0
#define K_STATION       1
#define K_USER          2
#define K_SNOW          3
#define K_MONO          4
#define K_BIOS          5
#define K_ITEMORD       6
#define K_PRINTER       7
#define K_PRTYPE        8
#define K_PAGELEN       9
#define K_LABELW        10
#define K_LABELH        11
#define K_BAUD          12
#define K_RTSCTS        13
#ifdef CFG_WINKEYS
#define K_WINSPLIT      14
#define K_WINCOLS       15      /* WINCOLS. of table t: K_WINCOLS + t   */
#define K_WINPANE       (K_WINCOLS + CFG_NCOLS) /* WINPANE., likewise   */
#define K_NAMED         K_WINCOLS
#define K_LAST          (K_WINPANE + CFG_NPANES - 1)
#else
#define K_LAST          K_RTSCTS
#define K_NAMED         (K_LAST + 1)
#endif

/* The keys named alone, K_NAMED of them. */
static const char *const cfg_keys[] = {
    "DATA", "STATION", "USER", "SNOW", "MONO", "BIOS", "ITEMORD",
    "PRINTER", "PRTYPE", "PAGELEN", "LABELW", "LABELH", "BAUD", "RTSCTS"
#ifdef CFG_WINKEYS
    , "WINSPLIT"
#endif
};

#define CFG_NKEYS       (K_LAST + 1)

/* K_NAMED is the number of cfg_keys, the words of cfg_portlist are
   the ports PRN_LPT1..PRN_DOS in order, and those of cfg_typelist the
   types PR_EPSON and PR_PLAIN.  Each pair declares one array twice, the
   second time with size 2 when that fails: the declarations then
   disagree, which every compiler refuses. */
extern char cfg_keys_ok[1];
extern char cfg_keys_ok[(K_NAMED == sizeof cfg_keys / sizeof cfg_keys[0]) ?
                        1 : 2];
extern char cfg_ports_ok[1];
extern char cfg_ports_ok[(PRN_LPT1 == 0 && PRN_DOS == 3) ? 1 : 2];
extern char cfg_types_ok[1];
extern char cfg_types_ok[(PR_PLAIN == PR_EPSON + 1) ? 1 : 2];

const char cfg_ordlist[] = "ID LOC MFR SERIAL";
const char cfg_portlist[] = "LPT1 LPT2 LPT3 PRN";
const char cfg_typelist[] = "EPSON PLAIN";
const char cfg_defdir[CFG_DEFDIRSZ] = "DATA\\";
#ifdef CFG_WINKEYS
/* The first word of WINCOLS's keys, then the names of its tables, in the
   order of cfg's cols (CFG.H). */
static const char cfg_cols[] = "WINCOLS ITEM SER PART TREE SVC ISVC LOAN "
                               "ILOAN USER AUDIT";
/* Likewise WINPANE's, and its screens in the order of cfg's pane. */
static const char cfg_panes[] = "WINPANE LOC ITEM ASSY LOAN CAT USER";
#endif

/* The extension of the scratch file, and the size of its path: the
   settings path, a dot, the extension and a NUL. */
static const char k_scratch[] = "$$$";
#define CFG_SCRATCHMAX  (CFG_PATHMAX + 1 + sizeof k_scratch)

/* The end of every line written. */
static const char k_crlf[] = "\r\n";
#define CFG_CRLFLEN     ((u16)(sizeof k_crlf - 1))

#ifdef CFG_WINKEYS
/* What the key s of n bytes names in the word list l, cfg_cols or
   cfg_panes: l's first word, a dot and a word of l after its first,
   whose number from 0 is returned; -1 when it is no such key. */
static int list_key(const char *l, char *s, size_t n)
{
    size_t p;
    char c;
    int i;

    p = (size_t)(strchr(l, ' ') - l);
    if (n <= p + 1 || s[p] != '.' || !txt_ieqn(s, l, (u16)p))
        return -1;
    c = s[n];
    s[n] = '\0';
    i = txt_word_num(l, s + p + 1);
    s[n] = c;
    return i - 1;
}
#endif

/* Split the line s (NUL-terminated): the key's index into *key, the
   trimmed value into *val (s is modified).  1 for a key=value line, 0
   for a blank or comment line, -1 for a line not understood. */
static int split(char *s, int *key, char **val)
{
    char *e;
    char *k;
    int i;
    size_t n;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '\0' || *s == ';')
        return 0;
    e = strchr(s, '=');
    if (e == NULL)
        return -1;
    k = e;
    while (k > s && (k[-1] == ' ' || k[-1] == '\t'))
        k--;
    n = (size_t)(k - s);
    *val = e + 1;
    while (**val == ' ' || **val == '\t')
        (*val)++;
    e = *val + strlen(*val);
    while (e > *val && (e[-1] == ' ' || e[-1] == '\t'))
        e--;
    *e = '\0';
    for (i = 0; i < K_NAMED; i++) {
        if (strlen(cfg_keys[i]) == n && txt_ieqn(s, cfg_keys[i], (u16)n)) {
            *key = i;
            return 1;
        }
    }
#ifdef CFG_WINKEYS
    i = list_key(cfg_cols, s, n);
    if (i >= 0) {
        *key = K_WINCOLS + i;
        return 1;
    }
    i = list_key(cfg_panes, s, n);
    if (i >= 0) {
        *key = K_WINPANE + i;
        return 1;
    }
#endif
    return -1;
}

static int set_flag(u8 *f, const char *v)
{
    if ((v[0] == '0' || v[0] == '1') && v[1] == '\0') {
        *f = (u8)(v[0] - '0');
        return 1;
    }
    return 0;
}

/* A text of at most max bytes into d, refused when it holds a control
   byte or DEL, which the server refuses in a station name. */
static int set_text(char *d, const char *v, size_t max)
{
    size_t j;

    for (j = 0; v[j] != '\0'; j++)
        if ((unsigned char)v[j] < ' ' || v[j] == 0x7F || j == max)
            return 0;
    strcpy(d, v);
    return 1;
}

/* A number of one to three digits from lo to hi into *f. */
static int set_num(u8 *f, const char *v, u8 lo, u8 hi)
{
    u16 n;
    size_t j;

    n = 0;
    for (j = 0; TXT_ISDIGIT(v[j]) && j < 3; j++)
        n = (u16)(n * 10 + (u16)(v[j] - '0'));
    if (j == 0 || v[j] != '\0' || n < lo || n > hi)
        return 0;
    *f = (u8)n;
    return 1;
}

#ifdef CFG_WINKEYS
/* The widths of a table, v, into w (CFG_COLMAX of them): one to
   CFG_COLMAX numbers of one to three digits, at most CFG_COLWMAX,
   separated by commas, those not given 0; 0 when v is bad, w then
   unchanged. */
static int set_cols(u8 *w, const char *v)
{
    u8 t[CFG_COLMAX];
    u16 n;
    int j;
    int k;

    memset(t, 0, sizeof t);
    for (k = 0; k < CFG_COLMAX; k++) {
        n = 0;
        for (j = 0; TXT_ISDIGIT(v[j]) && j < 3; j++)
            n = (u16)(n * 10 + (u16)(v[j] - '0'));
        if (j == 0 || n > CFG_COLWMAX)
            return 0;
        t[k] = (u8)n;
        v += j;
        if (*v == '\0') {
            memcpy(w, t, sizeof t);
            return 1;
        }
        if (*v++ != ',')
            return 0;
    }
    return 0;
}

/* A pane's values, v, into p (CFG_PANEVALS of them): its state, 0 or 1,
   then, if given, a comma and its width, as set_num reads it, else 0;
   0 when v is bad, p then unchanged. */
static int set_pane(u8 *p, const char *v)
{
    u8 w;

    w = 0;
    if ((v[0] != '0' && v[0] != '1') ||
        (v[1] != '\0' &&
         (v[1] != ',' || !set_num(&w, v + 2, 0, CFG_SPLITMAX))))
        return 0;
    p[CFG_PANE_ON] = (u8)(v[0] - '0');
    p[CFG_PANE_W] = w;
    return 1;
}
#endif

/* Apply key = v to c; 0 when the value is bad. */
static int apply(cfg *c, int key, const char *v)
{
    int i;
    u16 n;

#ifdef CFG_WINKEYS
    if (key >= K_WINPANE)
        return set_pane(c->pane[key - K_WINPANE], v);
    if (key >= K_WINCOLS)
        return set_cols(c->cols[key - K_WINCOLS], v);
#endif
    switch (key) {
    case K_DATA:
        return set_text(c->data, v, LINK_MAXDIR);
    case K_STATION:
        return set_text(c->station, v, LINK_STALEN);
    case K_USER:
        return set_text(c->user, v, LINK_USERLEN);
    case K_SNOW:
        return set_flag(&c->snow, v);
    case K_MONO:
        return set_flag(&c->mono, v);
    case K_BIOS:
        return set_flag(&c->bios, v);
    case K_ITEMORD:
        i = txt_word_num(cfg_ordlist, v);
        if (i < 0)
            return 0;
        txt_word_at(cfg_ordlist, (u16)i, c->itemord, (u16)sizeof c->itemord);
        return 1;
    case K_PRINTER:
        i = txt_word_num(cfg_portlist, v);
        if (i < 0)
            return 0;
        c->prport = (u8)i;
        return 1;
    case K_PRTYPE:
        i = txt_word_num(cfg_typelist, v);
        if (i < 0)
            return 0;
        c->prtype = (u8)(PR_EPSON + i);
        return 1;
    case K_PAGELEN:
        return set_num(&c->pagelen, v, RPT_MINPAGE, RPT_MAXPAGE);
    case K_LABELW:
        return set_num(&c->labelw, v, RPT_MINLABELW, RPT_MAXLABELW);
    case K_LABELH:
        return set_num(&c->labelh, v, RPT_MINLABELH, RPT_MAXLABELH);
    case K_BAUD:
        n = txt_num(v, UART_MAXBAUD);
        if (!UART_BAUDOK(n))
            return 0;
        c->baud = n;
        return 1;
    case K_RTSCTS:
        return set_flag(&c->rtscts, v);
#ifdef CFG_WINKEYS
    case K_WINSPLIT:
        return set_num(&c->split, v, 0, CFG_SPLITMAX);
#endif
    default:
        return 0;
    }
}

void cfg_default(cfg *c)
{
#ifdef CFG_WINKEYS
    int i;
#endif

    memset(c, 0, sizeof *c);
#ifdef CFG_WINKEYS
    for (i = 0; i < CFG_NPANES; i++)
        c->pane[i][CFG_PANE_ON] = CFG_PANEAUTO;
#endif
    strcpy(c->itemord, "ID");
    c->prport = PRN_LPT1;
    c->prtype = PR_EPSON;
    c->pagelen = CFG_DEFPAGE;
    c->labelw = CFG_DEFLABELW;
    c->labelh = CFG_DEFLABELH;
    c->baud = UART_DEFBAUD;
}

/* The scratch file name of path into d (CFG_SCRATCHMAX bytes): the
   extension of its last component replaced by, or given, k_scratch.
   CF_EINVAL for a path longer than CFG_PATHMAX, or one whose extension
   starts with k_scratch, which DOS, cutting an extension to three
   characters, takes for the scratch file itself. */
static int scratch_name(const char *path, char *d)
{
    if (strlen(path) > CFG_PATHMAX ||
        txt_path_ext(d, (u16)CFG_SCRATCHMAX, path, k_scratch, 0) != 0 ||
        strncmp(d, path, strlen(d)) == 0)
        return CF_EINVAL;
    return 0;
}

int cfg_load(cfg *c, const char *path)
{
    msg_rd r;
    char s[CFG_SCRATCHMAX];
    char rbuf[CFG_RDBUF];
    char line[CFG_MAXLINE + 1];
    char *val;
    u8 seen[CFG_NKEYS];
    u16 n;
    u16 lineno;
    int rc;
    int key;
    int h;

    cfg_default(c);
    lineno = 0;
    memset(seen, 0, sizeof seen);
    rc = scratch_name(path, s);
    if (rc != 0)
        return rc;
    rc = cf_open(path, CF_READ | CF_DENYW, &h);
    if (rc == CF_ENOENT && cf_open(s, CF_READ | CF_DENYW, &h) == 0)
        rc = 0;
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        return 0;
    if (rc != 0)
        return rc;
    msg_rd_init(&r, h, rbuf, (u16)sizeof rbuf);
    for (;;) {
        rc = msg_rd_line(&r, line, CFG_MAXLINE, &n);
        if (rc <= 0)
            break;
        lineno++;
        if (n > CFG_MAXLINE) {
            rc = -1;
        } else {
            rc = split(line, &key, &val);
            if (rc > 0) {
                if (seen[key])
                    rc = 0;
                else if (apply(c, key, val))
                    seen[key] = 1;
                else
                    rc = -1;
            }
        }
        if (rc < 0) {
            if (c->bad == 0)
                c->firstbad = lineno;
            c->bad++;
        }
        rc = 0;
    }
    cf_close(h);
    if (rc < 0) {
        cfg_default(c);
        return -rc;
    }
    return 0;
}

#ifdef CFG_WINKEYS
/* Write to h the line of the key that list_key numbers i in the word
   list l, with the n numbers of w as its value, commas between them. */
static int put_list(int h, const char *l, int i, const u8 *w, int n)
{
    char t[CFG_MAXLINE + 3];
    u16 at;
    int k;

    txt_word_at(l, 0, t, (u16)sizeof t);
    strcat(t, ".");
    at = (u16)strlen(t);
    txt_word_at(l, (u16)(i + 1), t + at, (u16)(sizeof t - at));
    strcat(t, "=");
    for (k = 0; k < n; k++) {
        if (k > 0)
            strcat(t, ",");
        txt_u32(t + strlen(t), w[k]);
    }
    strcat(t, k_crlf);
    return cf_write(h, t, (u16)strlen(t));
}

/* Write the WINCOLS line of table i with its widths w: up to the last
   that is not 0, or 0 alone. */
static int put_cols(int h, const u8 *w, int i)
{
    int n;

    for (n = CFG_COLMAX; n > 1 && w[n - 1] == 0; n--)
        ;
    return put_list(h, cfg_cols, i, w, n);
}

/* 1 when the widths w of a table are all 0, its columns' own. */
static int cols_own(const u8 *w)
{
    int k;

    for (k = 0; k < CFG_COLMAX; k++)
        if (w[k] != 0)
            return 0;
    return 1;
}
#endif

/* Write key's line with its value from c.  A printer port or printer
   type out of range is written as its default. */
static int put_key(int h, const cfg *c, int key)
{
    char t[CFG_MAXLINE + 3];
    char w[CFG_CHOICEMAX + 1];
    const char *v;
    char f[TXT_U32LEN + 1];

#ifdef CFG_WINKEYS
    /* A pane's line: its state, and its width when it is not 0. */
    if (key >= K_WINPANE)
        return put_list(h, cfg_panes, key - K_WINPANE,
                        c->pane[key - K_WINPANE],
                        c->pane[key - K_WINPANE][CFG_PANE_W] != 0 ?
                        CFG_PANEVALS : 1);
    if (key >= K_WINCOLS)
        return put_cols(h, c->cols[key - K_WINCOLS], key - K_WINCOLS);
#endif
    v = f;
    switch (key) {
    case K_DATA:
        v = c->data;
        break;
    case K_STATION:
        v = c->station;
        break;
    case K_USER:
        v = c->user;
        break;
    case K_SNOW:
    case K_MONO:
    case K_BIOS:
    case K_RTSCTS:
        txt_u32(f, (u32)((key == K_SNOW ? c->snow
                          : key == K_MONO ? c->mono
                          : key == K_BIOS ? c->bios : c->rtscts) % 2));
        break;
    case K_ITEMORD:
        v = c->itemord;
        break;
    case K_PRINTER:
        txt_word_at(cfg_portlist,
                    (u16)(c->prport <= PRN_DOS ? c->prport : PRN_LPT1), w,
                    (u16)sizeof w);
        v = w;
        break;
    case K_PRTYPE:
        txt_word_at(cfg_typelist,
                    (u16)(c->prtype >= PR_EPSON && c->prtype <= PR_PLAIN ?
                          c->prtype - PR_EPSON : 0), w, (u16)sizeof w);
        v = w;
        break;
    case K_PAGELEN:
        txt_u32(f, c->pagelen);
        break;
    case K_LABELW:
        txt_u32(f, c->labelw);
        break;
    case K_LABELH:
        txt_u32(f, c->labelh);
        break;
    case K_BAUD:
        txt_u32(f, c->baud);
        break;
#ifdef CFG_WINKEYS
    case K_WINSPLIT:
        txt_u32(f, c->split);
        break;
#endif
    default:
        v = "";
        break;
    }
    strcpy(t, cfg_keys[key]);
    strcat(t, "=");
    strncat(t, v, CFG_MAXLINE - strlen(t));
    strcat(t, k_crlf);
    return cf_write(h, t, (u16)strlen(t));
}

/* Copy the rest of a line longer than CFG_MAXLINE from r to out, from
   its first piece (n bytes in line, which holds CFG_MAXLINE) on, and end
   it with CR LF; 0 or a CF_E code.  The reader has dropped the line's
   own CR LF already, so every byte given is written unchanged. */
static int copy_long(msg_rd *r, int out, char *line, u16 n)
{
    int eol;
    int rc;

    eol = 0;
    for (;;) {
        if (n > 0) {
            rc = cf_write(out, line, n);
            if (rc != 0)
                return rc;
        }
        if (eol)
            break;
        rc = msg_rd_piece(r, line, CFG_MAXLINE, &n, &eol);
        if (rc < 0)
            return -rc;
        if (rc == 0)
            break;
    }
    return cf_write(out, k_crlf, CFG_CRLFLEN);
}

/* Write the old file (open on in, or none when in < 0) to out with each
   known key's first line rewritten from c and its later lines dropped,
   then add the keys it lacked, but a table's WINCOLS whose widths are
   all 0; the WINPANE of a pane whose state is CFG_PANEAUTO has no line,
   so its old ones are dropped and none is added; 0 or a CF_E code. */
static int copy_over(int in, int out, const cfg *c)
{
    msg_rd r;
    char rbuf[CFG_RDBUF];
    char line[CFG_MAXLINE];
    char work[CFG_MAXLINE + 1];
    char *val;
    u8 done[CFG_NKEYS];
    u16 n;
    int eol;
    int rc;
    int key;

    rc = 0;
    memset(done, 0, sizeof done);
#ifdef CFG_WINKEYS
    for (key = 0; key < CFG_NPANES; key++)
        if (c->pane[key][CFG_PANE_ON] == CFG_PANEAUTO)
            done[K_WINPANE + key] = 1;
#endif
    if (in >= 0) {
        msg_rd_init(&r, in, rbuf, (u16)sizeof rbuf);
        for (;;) {
            rc = msg_rd_piece(&r, line, CFG_MAXLINE, &n, &eol);
            if (rc <= 0)
                break;
            rc = 0;
            if (!eol) {
                rc = copy_long(&r, out, line, n);
            } else {
                memcpy(work, line, n);
                work[n] = '\0';
                if (split(work, &key, &val) > 0) {
                    if (!done[key])
                        rc = put_key(out, c, key);
                    done[key] = 1;
                } else {
                    if (n > 0)
                        rc = cf_write(out, line, n);
                    if (rc == 0)
                        rc = cf_write(out, k_crlf, CFG_CRLFLEN);
                }
            }
            if (rc != 0)
                break;
        }
        if (rc < 0)
            rc = -rc;
    }
#ifdef CFG_WINKEYS
    for (key = 0; key < CFG_NCOLS; key++)
        if (cols_own(c->cols[key]))
            done[K_WINCOLS + key] = 1;
#endif
    for (key = 0; key < (int)CFG_NKEYS && rc == 0; key++)
        if (!done[key])
            rc = put_key(out, c, key);
    return rc;
}

int cfg_save(const cfg *c, const char *path)
{
    char s[CFG_SCRATCHMAX];
    int in;
    int out;
    int rc;
    int rc2;

    in = -1;
    rc = scratch_name(path, s);
    if (rc != 0)
        return rc;
    rc = cf_open(path, CF_READ | CF_DENYW, &in);
    if (rc == CF_ENOENT && cf_rename(s, path) == 0)
        rc = cf_open(path, CF_READ | CF_DENYW, &in);
    if (rc == CF_ENOENT || rc == CF_ENOPATH) {
        in = -1;
    } else if (rc != 0) {
        return rc;
    }
    rc = cf_create(s, &out);
    if (rc != 0) {
        if (in >= 0)
            cf_close(in);
        return rc;
    }
    rc = copy_over(in, out, c);
    if (in >= 0)
        cf_close(in);
    rc2 = cf_close(out);
    if (rc == 0)
        rc = rc2;
    if (rc == 0) {
        rc = cf_remove(path);
        if (rc == 0 || rc == CF_ENOENT)
            return cf_rename(s, path);
    }
    cf_remove(s);
    return rc;
}
