/*
 * KEYSCR.C - key scripts and screen snapshots (KEYSCR.H).
 *
 * ks_open reads the script once to check every line, then rewinds it;
 * while the program runs, the key source reads one line at a time as
 * keys are asked for, so a snapshot or mask line takes effect exactly
 * between the keys around it; a look for a waiting key may read the
 * next line early, but holds an S, M, C or D line it reads until the
 * next key is asked for, so a click is never typed ahead.  A P line
 * after an S or M line (with no keys between them) is refused, as a
 * look stops at that line, so the P keys could never be seen waiting.
 *
 * vid_verify reports only the first cell where the adapter differs from
 * the shadow; when that cell is masked the snapshot says VRAM=OK, as no
 * later cell can be checked.
 *
 * The key names are messages (tui_keyname), so ks_open needs the
 * message file loaded (msg_load) first.  A snapshot letters at most
 * KS_MAXATTR different attributes (A..Z, a..z); cells of any further
 * attribute are written '?'.
 */
#define TUI_CLICKS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CMOUSE.H"
#include "MSG.H"
#include "TUI.H"
#include "KEYSCR.H"

#define KS_RDBUF        32      /* bytes of the script read at a time   */
#define KS_MAXATTR      52      /* attributes a snapshot letters        */
#define KS_NUMCAP       1000    /* an M line's numbers are below it     */

/* The longest texts of a snapshot's first line after the palette,
   " CURSOR=r,c" and " VRAM=r,c" CR LF with r and c of up to 3 digits,
   and their NUL. */
#define KS_HDRTXT       16

static const char k_stack[] = "=STACK UNUSED=";

/* The texts of the snapshot line for the TP_ palettes and the VID_CUR_
   cursor shapes, indexed by their codes (the build fails when those are
   not 0..2). */
static const char *const ks_pals[] = {
    " PALETTE=COLOR", " PALETTE=MONO", " PALETTE=MONOCGA"
};
static const char *const ks_shapes[] = { ",HIDE", ",LINE", ",BLOCK" };
extern char ks_pals_fit[1];
extern char ks_pals_fit[(TP_COLOR == 0 && TP_MONO == 1 && TP_MONOCGA == 2)
                        ? 1 : 2];
extern char ks_shapes_fit[1];
extern char ks_shapes_fit[(VID_CUR_HIDE == 0 && VID_CUR_LINE == 1 &&
                           VID_CUR_BLOCK == 2) ? 1 : 2];

typedef struct {
    u8 row;
    u8 col;
    u8 len;
} ks_mask;

/* The state of an open script, the key source's arg. */
typedef struct {
    int h;                      /* the script, while on                 */
    int sh;                     /* the snapshot file, or -1             */
    int err;                    /* the CF_E code of a failed read or
                                   snapshot write, which ended the run  */
    u16 pos;                    /* next byte of line to give            */
    u8 on;                      /* a script is open                     */
    u8 ended;
    u8 kind;                    /* 'T', 'K' or 'P' while line has keys  */
    u8 held;                    /* line was read ahead by a peek and is
                                   not yet carried out                  */
    u8 nmask;
    ks_mask masks[KS_MAXMASK];
    char line[KS_MAXLINE + 1];
    msg_rd rd;                  /* the script's reader                  */
    char rbuf[KS_RDBUF];
} ks_state;

static ks_state ks_st;

/* The next script line into s->line; 1, 0 at the end, -1 when longer
   than KS_MAXLINE or holding a NUL, -2 on a read error (its code in
   s->err).  A Ctrl-Z ends the script, as DOS editors may leave one at
   the end. */
static int read_line(ks_state *s)
{
    u16 len;
    int rc;

    rc = msg_rd_line(&s->rd, s->line, KS_MAXLINE, &len);
    if (rc < 0) {
        s->err = -rc;
        return -2;
    }
    if (rc == 0)
        return 0;
    return strlen(s->line) != len ? -1 : 1;
}

/* 1 when c separates the words of a line: a space or a TAB. */
static int is_sp(char c)
{
    return c == ' ' || c == '\t';
}

/* Parse a decimal number below KS_NUMCAP at *p up to the next
   separator; -1 if none. */
static int number(const char **p)
{
    const char *s;
    const char *d;
    u16 v;

    s = *p;
    while (is_sp(*s))
        s++;
    d = s;
    v = msg_dec(&s, KS_NUMCAP);
    if (s == d || v >= KS_NUMCAP || (*s != '\0' && !is_sp(*s)))
        return -1;
    *p = s;
    return (int)v;
}

/* The row and column of a screen cell at p into *row and *col: the
   text after them, its blanks skipped, or NULL when they are missing
   or off the screen. */
static const char *cell(const char *p, int *row, int *col)
{
    *row = number(&p);
    *col = *row < 0 ? -1 : number(&p);
    if (*col < 0 || *row >= VID_ROWS || *col >= VID_COLS)
        return NULL;
    while (is_sp(*p))
        p++;
    return p;
}

/* Check (apply 0) or carry out (apply 1, into s->masks) an M line's
   arguments at p; 0 when malformed. */
static int mask_line(ks_state *s, const char *p, int apply, u8 *nmask)
{
    int row;
    int col;
    int len;

    while (is_sp(*p))
        p++;
    if (txt_ieqn(p, "CLEAR", 5) && (p[5] == '\0' || is_sp(p[5]))) {
        p += 5;
        while (is_sp(*p))
            p++;
        if (*p != '\0')
            return 0;
        *nmask = 0;
        return 1;
    }
    p = cell(p, &row, &col);
    len = p == NULL ? -1 : number(&p);
    while (len > 0 && is_sp(*p))
        p++;
    if (len < 1 || *p != '\0' || len > VID_COLS || *nmask >= KS_MAXMASK)
        return 0;
    if (apply) {
        s->masks[*nmask].row = (u8)row;
        s->masks[*nmask].col = (u8)col;
        s->masks[*nmask].len = (u8)(col + len > VID_COLS ? VID_COLS - col
                                                         : len);
    }
    (*nmask)++;
    return 1;
}

/* The click of a C line (c 'C') or D line (c 'D') whose arguments, the
   row and column of the cell clicked, are at p; KEY_NONE when they are
   malformed. */
static u16 click_line(char c, const char *p)
{
    int row;
    int col;

    p = cell(p, &row, &col);
    if (p == NULL || *p != '\0')
        return KEY_NONE;
    return (u16)((c == 'C' ? KEY_CLICK : KEY_DCLICK) + row * KEY_CROW + col);
}

/* A snapshot name at p: 1..KS_NAMELEN of A-Z, 0-9, _, alone. */
static int snap_name(const char *p)
{
    u16 n;

    while (is_sp(*p))
        p++;
    n = 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
           *p == '_') {
        p++;
        n++;
    }
    while (is_sp(*p))
        p++;
    return n >= 1 && n <= KS_NAMELEN && *p == '\0';
}

/* Check the line in s->line; 1 if good.  *prev is the command letter
   of the line before it, comments, blank lines and lines without keys
   aside, and receives this line's. */
static int check_line(ks_state *s, u8 *nmask, char *prev)
{
    const char *p;
    const char *w;
    char c;

    p = s->line;
    c = p[0];
    if (c == '\0' || c == '#')
        return 1;
    if (p[1] != '\0' && !is_sp(p[1]))
        return 0;
    c = (char)TXT_UPPER(c);
    if (c == 'P' && (*prev == 'S' || *prev == 'M'))
        return 0;
    if (c == 'T') {
        if (p[1] != '\0' && p[2] != '\0')
            *prev = c;
        return 1;
    }
    if (c == 'S' || c == 'M')
        *prev = c;
    if (c == 'S')
        return snap_name(p + 1);
    if (c == 'M')
        return mask_line(s, p + 1, 0, nmask);
    if (c == 'C' || c == 'D') {
        *prev = c;
        return click_line(c, p + 1) != KEY_NONE;
    }
    if (c != 'K' && c != 'P')
        return 0;
    p++;
    for (;;) {
        while (is_sp(*p))
            p++;
        if (*p == '\0')
            return 1;
        *prev = c;
        w = p;
        while (*p != '\0' && !is_sp(*p))
            p++;
        if (tui_keyname(w, (u16)(p - w)) == KEY_NONE)
            return 0;
    }
}

/* Write the n bytes at t to the snapshot file; 0, also without a file,
   or a CF_E code. */
static int put(ks_state *s, const char *t, u16 n)
{
    if (s->sh < 0 || n == 0)
        return 0;
    return cf_write(s->sh, t, n);
}

/* Write the string t to the snapshot file, as put does. */
static int puts0(ks_state *s, const char *t)
{
    return put(s, t, (u16)strlen(t));
}

/* Append the decimal digits of v to the string t. */
static void cat_num(char *t, u16 v)
{
    char d[TXT_U32LEN + 1];

    txt_u32(d, v);
    strcat(t, d);
}

/* 1 when the cell at row, col is masked. */
static int masked(const ks_state *s, u8 row, u8 col)
{
    u8 i;

    for (i = 0; i < s->nmask; i++)
        if (s->masks[i].row == row && col >= s->masks[i].col &&
            col < s->masks[i].col + s->masks[i].len)
            return 1;
    return 0;
}

/* The letter of the i-th attribute of a snapshot, '?' from KS_MAXATTR
   on. */
static char letter(u8 i)
{
    if (i < KS_MAXATTR / 2)
        return (char)('A' + i);
    if (i < KS_MAXATTR)
        return (char)('a' + i - KS_MAXATTR / 2);
    return '?';
}

/* Write a snapshot titled name now; 0 or a CF_E code.  Without a
   snapshot file, nothing is written and 0 is returned. */
static int ks_snap(ks_state *s, const char *name)
{
    u8 cells[VID_ROWBYTES];
    char line[VID_COLS + 5];
    u8 attrs[KS_MAXATTR];
    u8 na;
    u8 row;
    u8 col;
    u8 vr;
    u8 vc;
    u8 shape;
    u8 a;
    u8 i;
    char c;
    char t[KS_HDRTXT];
    int rc;

    if (s->sh < 0)
        return 0;
    rc = puts0(s, "=SNAP ");
    if (rc == 0)
        rc = puts0(s, name);
    if (rc == 0)
        rc = puts0(s, ks_pals[tui_pal()]);
    vid_cursor_get(&row, &col, &shape);
    if (shape == VID_CUR_HIDE) {
        row = 0;
        col = 0;
    }
    strcpy(t, " CURSOR=");
    cat_num(t, row);
    strcat(t, ",");
    cat_num(t, col);
    if (rc == 0)
        rc = puts0(s, t);
    if (rc == 0)
        rc = puts0(s, ks_shapes[shape]);
    if (vid_verify(&vr, &vc) == 0 || masked(s, vr, vc)) {
        strcpy(t, " VRAM=OK");
    } else {
        strcpy(t, " VRAM=");
        cat_num(t, vr);
        strcat(t, ",");
        cat_num(t, vc);
    }
    strcat(t, "\r\n");
    if (rc == 0)
        rc = puts0(s, t);
    na = 0;
    for (row = 0; row < VID_ROWS && rc == 0; row++) {
        vid_getrow(row, cells);
        line[0] = '|';
        for (col = 0; col < VID_COLS; col++) {
            c = (char)cells[2 * col];
            a = cells[2 * col + 1];
            if (masked(s, row, col)) {
                c = '?';
                a = 0;
            } else if (c == '\0' || c == '\n' || c == '\r' || c == '\032') {
                c = '?';
            }
            line[1 + col] = c;
            for (i = 0; i < na && attrs[i] != a; i++)
                ;
            if (i == na && na < KS_MAXATTR)
                attrs[na++] = a;
        }
        strcpy(line + 1 + VID_COLS, "|\r\n");
        rc = put(s, line, VID_COLS + 4);
    }
    if (rc == 0)
        rc = puts0(s, "=ATTR");
    for (i = 0; i < na && rc == 0; i++) {
        t[0] = ' ';
        t[1] = letter(i);
        t[2] = '=';
        t[3] = txt_hexdig[attrs[i] >> 4];
        t[4] = txt_hexdig[attrs[i] & 15];
        rc = put(s, t, 5);
    }
    if (rc == 0)
        rc = puts0(s, "\r\n");
    for (row = 0; row < VID_ROWS && rc == 0; row++) {
        vid_getrow(row, cells);
        line[0] = '|';
        for (col = 0; col < VID_COLS; col++) {
            a = (u8)(masked(s, row, col) ? 0 : cells[2 * col + 1]);
            for (i = 0; i < na && attrs[i] != a; i++)
                ;
            line[1 + col] = letter(i);
        }
        strcpy(line + 1 + VID_COLS, "|\r\n");
        rc = put(s, line, VID_COLS + 4);
    }
    return rc;
}

/* The key source, on the ks_state arg: the next key of the script;
   with peek, the next key when it is typed ahead (a P line's), left to
   be read, else KEY_NONE.  A peek reads ahead past comments and the
   keys already given, and sets up the next K, T or P line, whose keys
   are taken only by reads; an S, M, C or D line it finds is held, to
   be carried out by the next read, so that it still takes effect
   between the keys around it. */
static u16 ks_src(void *arg, int peek)
{
    ks_state *s;
    u16 st;
    u16 k;
    char c;

    s = (ks_state *)arg;
    while (!s->ended) {
        if (s->kind == 'T' && s->line[s->pos] != '\0')
            return peek ? KEY_NONE : (u16)(u8)s->line[s->pos++];
        if (s->kind == 'K' || s->kind == 'P') {
            while (is_sp(s->line[s->pos]))
                s->pos++;
            if (s->line[s->pos] != '\0') {
                if (peek && s->kind == 'K')
                    return KEY_NONE;
                st = s->pos;
                while (s->line[s->pos] != '\0' && !is_sp(s->line[s->pos]))
                    s->pos++;
                k = tui_keyname(s->line + st, (u16)(s->pos - st));
                if (k == KEY_NONE)
                    continue;
                if (peek)
                    s->pos = st;
                return k;
            }
        }
        s->kind = 0;
        if (s->held) {
            s->held = 0;
        } else if (read_line(s) <= 0) {
            s->ended = 1;
            break;
        }
        c = (char)TXT_UPPER(s->line[0]);
        if (peek && (c == 'S' || c == 'M' || c == 'C' || c == 'D')) {
            s->held = 1;
            return KEY_NONE;
        }
        switch (c) {
        case 'T':
            s->kind = 'T';
            s->pos = (u16)(s->line[1] == '\0' ? 1 : 2);
            break;
        case 'K':
        case 'P':
            s->kind = (u8)c;
            s->pos = 1;
            break;
        case 'C':
        case 'D':
            return click_line(c, s->line + 1);
        case 'S':
            st = 1;
            while (is_sp(s->line[st]))
                st++;
            k = st;
            while (s->line[k] != '\0' && !is_sp(s->line[k]))
                k++;
            s->line[k] = '\0';
            s->err = ks_snap(s, s->line + st);
            if (s->err != 0)
                s->ended = 1;
            break;
        case 'M':
            mask_line(s, s->line + 1, 1, &s->nmask);
            break;
        default:
            break;
        }
    }
    return peek ? KEY_NONE : KEY_EOS;
}

/* Close the script and the snapshot file; 0, or the CF_E code of the
   first close that failed. */
static int close_all(ks_state *s)
{
    int rc;
    int rc2;

    rc = cf_close(s->h);
    if (s->sh >= 0) {
        rc2 = cf_close(s->sh);
        if (rc == 0)
            rc = rc2;
    }
    s->on = 0;
    return rc;
}

int ks_open(const char *keys, const char *snap, char *why, u16 whysz)
{
    ks_state *s;
    u16 lineno;
    u8 nmask;
    char prev;
    int rc;
    char num[TXT_U32LEN + 1];

    if (whysz > 0)
        why[0] = '\0';
    ks_close();
    s = &ks_st;
    rc = cf_open(keys, CF_READ | CF_DENYW, &s->h);
    if (rc != 0)
        return rc;
    s->on = 1;
    s->sh = -1;
    msg_rd_init(&s->rd, s->h, s->rbuf, (u16)sizeof s->rbuf);
    lineno = 0;
    nmask = 0;
    prev = 0;
    for (;;) {
        rc = read_line(s);
        if (rc == 0)
            break;
        lineno++;
        if (rc > 0 && check_line(s, &nmask, &prev))
            continue;
        close_all(s);
        txt_copy(why, keys, whysz);
        if (rc == -2)
            return s->err;
        if (whysz > 0) {
            txt_u32(num, lineno);
            strncat(why, ":", (size_t)(whysz - 1) - strlen(why));
            strncat(why, num, (size_t)(whysz - 1) - strlen(why));
        }
        return -1;
    }
    rc = cf_seek(s->h, 0);
    if (rc == 0 && snap != NULL) {
        rc = cf_create(snap, &s->sh);
        if (rc != 0)
            s->sh = -1;
    }
    if (rc != 0) {
        close_all(s);
        return rc;
    }
    msg_rd_init(&s->rd, s->h, s->rbuf, (u16)sizeof s->rbuf);
    s->ended = 0;
    s->err = 0;
    s->kind = 0;
    s->held = 0;
    s->nmask = 0;
    key_set_source(ks_src, s);
    return 0;
}

int ks_close(void)
{
    ks_state *s;
    char t[sizeof k_stack + TXT_U32LEN + 2];
    u16 n;
    int rc;
    int rc2;

    s = &ks_st;
    if (!s->on)
        return 0;
    key_set_source(NULL, NULL);
    rc = s->err;
    n = cstk_unused();
    if (n != CSTK_UNKNOWN) {
        strcpy(t, k_stack);
        cat_num(t, n);
        strcat(t, "\r\n");
        rc2 = puts0(s, t);
        if (rc == 0)
            rc = rc2;
    }
    rc2 = close_all(s);
    if (rc == 0)
        rc = rc2;
    return rc;
}
