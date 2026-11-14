/*
 * INVSCR.C - the request script interpreter of INVTEST (INVSCR.H).
 *
 * The script is read one line ahead of the line being run: the check
 * lines after a request are taken while its response arrives, each as
 * the response line it checks is delivered, so a response is never
 * stored whole.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "NBCLI.H"
#include "SERCLI.H"
#include "MBXCLI.H"
#include "INVSCR.H"

/* Every client cuts response lines at the same length, which LINESZ
   holds. */
extern char invscr_resp_ok[1];
extern char invscr_resp_ok[(SERCLI_MAXRESP == NBCLI_MAXRESP &&
                            MBXCLI_MAXRESP == NBCLI_MAXRESP) ? 1 : 2];

#define SLEEP_MAX       65535u  /* longest @SLEEP, in ticks             */
#define LINESZ          (NBCLI_MAXRESP + 6)  /* "<* ", a response line,
                                   CR LF and NUL                        */

/* The directives, numbered as the D_ values, those that open a session
   first, and the fewest and most words of each one's line, its name
   included. */
#define D_CALL          0
#define D_COM           1
#define D_MBOX          2
#define D_S             3
#define D_WAIT          4
#define D_HANGUP        5
#define D_SLEEP         6
#define D_POST          7
static const char k_dirs[] = "CALL COM MBOX S WAIT HANGUP SLEEP POST";
static const u8 d_min[] = { 3, 3, 4, 2, 2, 2, 2, 2 };
static const u8 d_max[] = { 3, 4, 4, 2, 2, 2, 2, 2 };
#define D_WORDS         4       /* the most words of any directive      */

/* The serial ports @COM names, in port order. */
static const char k_ports[] = "COM1 COM2";

/* Session kinds. */
#define SK_NONE         0       /* closed                               */
#define SK_NB           1       /* NetBIOS                              */
#define SK_SER          2       /* serial line                          */
#define SK_MBX          3       /* mailbox                              */

static const char k_pwkey[] = "PASSWORD=";
#define PWKEY_LEN       ((u16)(sizeof k_pwkey - 1))

static const char k_blanks[] = " \t";
static const char k_badline[] = "bad line";
static const char k_lost[] = "session lost";
static const char k_notopen[] = "session not open";
static const char k_notmbx[] = "not a mailbox session";
static const char k_nomem[] = "no memory";

/* A session of the script. */
typedef struct {
    union {
        nbcli  *nb;
        sercli *ser;
        mbxcli *mbx;
    } h;
    u16  npend;                 /* requests whose response is unread    */
    u16  queued;                /* lines queued for the next @POST      */
    u8   kind;                  /* SK_                                  */
    u8   bye;                   /* BYE is one of the requests           */
} sess;

/* The script being read, with the line read ahead of the one run. */
typedef struct {
    FILE *f;
    u16  num;                   /* number of the line in text           */
    u8   held;                  /* text holds a line not yet taken      */
    char text[LINESZ];
} script;

/* A run of the script. */
typedef struct {
    FILE *out;                  /* the transcript                       */
    script sc;
    sess s[INVSCR_SESSIONS];
    const char *server;         /* NULL for none                        */
    int  cur;                   /* index of the selected session        */
    int  code;                  /* result of a failure (INVSCR_)        */
} invt;

/* What a response is checked with. */
typedef struct {
    invt *t;
    int  num;                   /* the session's number                 */
    u16  bad;                   /* line of the first check not met      */
} check;

/* Write why script line at failed; returns at. */
static u16 fail(invt *t, u16 at, const char *why)
{
    fprintf(t->out, "%s\n", why);
    return at;
}

/* The next script line that is neither blank nor a comment, without its
   CR LF and held until taken (sc->held = 0); NULL at the end.  A line
   too long for text, unless a comment, comes back empty, which no
   command accepts. */
static char *peek(script *sc)
{
    int n;
    int c;

    while (!sc->held) {
        if (fgets(sc->text, (int)sizeof sc->text, sc->f) == NULL)
            return NULL;
        sc->num++;
        n = (int)strlen(sc->text);
        if (n > 0 && sc->text[n - 1] == '\n') {
            sc->text[--n] = '\0';
        } else if ((c = getc(sc->f)) != EOF && c != '\n') {
            while ((c = getc(sc->f)) != EOF && c != '\n')
                ;
            if (sc->text[0] != '#') {
                sc->text[0] = '\0';
                sc->held = 1;
            }
            continue;
        }
        if (n > 0 && sc->text[n - 1] == '\r')
            sc->text[--n] = '\0';
        sc->held = (u8)(sc->text[0] != '#' &&
                        (int)strspn(sc->text, k_blanks) != n);
    }
    return sc->text;
}

/* The text of line s after its n-character sigil and the space that
   follows it; NULL when another character follows the sigil. */
static char *text_of(char *s, int n)
{
    s += n;
    if (*s == ' ')
        return s + 1;
    return *s == '\0' ? s : NULL;
}

/* The text of the check line s, *glob set to 1 for "<*" and 0 for "<";
   NULL when s is NULL or not a check line. */
static char *expect(char *s, int *glob)
{
    if (s == NULL || s[0] != '<')
        return NULL;
    *glob = s[1] == '*';
    return text_of(s, 1 + *glob);
}

/* 1 when the len characters at s match the pattern p, in which '*'
   matches any run of characters, else 0. */
static int glob_match(const char *p, const char *s, u16 len)
{
    const char *star;
    u16 i;
    u16 mark;

    star = NULL;
    mark = 0;
    i = 0;
    while (i < len) {
        if (*p == '*') {
            star = ++p;
            mark = i;
        } else if (*p != '\0' && *p == s[i]) {
            p++;
            i++;
        } else if (star != NULL) {
            p = star;
            i = ++mark;
        } else {
            return 0;
        }
    }
    while (*p == '*')
        p++;
    return *p == '\0';
}

/* Write the response line and check it against the next script line
   when that is a check line (the clients' line callback; arg is a
   check). */
static int on_line(void *arg, const char *line, u16 len)
{
    check *k;
    script *sc;
    char *want;
    int glob;

    k = (check *)arg;
    sc = &k->t->sc;
    fprintf(k->t->out, len != 0 ? "%d< %s\n" : "%d<\n", k->num, line);
    if (k->bad == 0 && (want = expect(peek(sc), &glob)) != NULL) {
        sc->held = 0;
        if (glob ? !glob_match(want, line, len) :
                   strlen(want) != len || memcmp(want, line, len) != 0)
            k->bad = sc->num;
    }
    return 0;
}

/* 1 when the first word of the request line is w (upper case), ignoring
   case, else 0. */
static int verb_is(const char *line, const char *w)
{
    u16 n;

    line += strspn(line, k_blanks);
    n = (u16)strlen(w);
    return txt_ieqn(line, w, n) &&
           (line[n] == '\0' || line[n] == ' ' || line[n] == '\t');
}

/* The length of the pair value at s: a quoted one through its closing
   quote, a backslash taking the character after it, any other up to the
   next blank. */
static u16 value_len(const char *s)
{
    u16 n;

    if (*s != '"')
        return (u16)strcspn(s, k_blanks);
    for (n = 1; s[n] != '\0' && s[n] != '"'; n++)
        if (s[n] == '\\' && s[n + 1] != '\0')
            n++;
    return s[n] == '"' ? (u16)(n + 1) : n;
}

/* Write the request line of session num to out after its sigil, ">&"
   when amp is 1, else ">", with the arguments of LOGIN and PASSWD and
   the value of every PASSWORD= pair masked. */
static void show_request(FILE *out, int num, int amp, const char *line)
{
    const char *p;
    u16 n;

    fprintf(out, amp ? "%d>& " : "%d> ", num);
    if (verb_is(line, "LOGIN") || verb_is(line, "PASSWD")) {
        n = (u16)strspn(line, k_blanks);
        n = (u16)(n + strcspn(line + n, k_blanks));
        fprintf(out, "%.*s ***\n", (int)n, line);
        return;
    }
    for (p = line; *p != '\0'; p += n) {
        n = (u16)strcspn(p, k_blanks);
        if (n >= PWKEY_LEN && txt_ieqn(p, k_pwkey, PWKEY_LEN)) {
            fprintf(out, "%.*s***", (int)PWKEY_LEN, p);
            n = (u16)(PWKEY_LEN + value_len(p + PWKEY_LEN));
        } else {
            fprintf(out, "%.*s", (int)n, p);
        }
        if (p[n] != '\0')
            putc(p[n++], out);
    }
    putc('\n', out);
}

/* Close session s, sending BYE first on a serial line when bye is 1
   (sercli_close), and mark it closed. */
static void close_sess(sess *s, int bye)
{
    switch (s->kind) {
    case SK_NB:
        nbcli_close(s->h.nb);
        break;
    case SK_SER:
        sercli_close(s->h.ser, bye);
        break;
    case SK_MBX:
        mbxcli_close(s->h.mbx);
        break;
    default:
        break;
    }
    memset(s, 0, sizeof *s);
}

/* Deliver the oldest unread response of session s to on_line with k;
   1 when it came whole, else 0. */
static int deliver(sess *s, check *k)
{
    switch (s->kind) {
    case SK_NB:
        return nbcli_response(s->h.nb, on_line, k, INVSCR_WAIT) ==
               NBCLI_OK;
    case SK_SER:
        return sercli_response(s->h.ser, on_line, k, INVSCR_WAIT) ==
               SERCLI_OK;
    default:
        return mbxcli_response(s->h.mbx, on_line, k) == MBXCLI_OK;
    }
}

/* Read the oldest unread response of session index i, the line that
   asked for it being script line at, and check it; returns 0 or the
   number of the line that failed.  The session is closed once the
   response to its BYE has been read, the server having ended it. */
static u16 response(invt *t, int i, u16 at)
{
    sess *s;
    check k;

    s = &t->s[i];
    if (s->npend == 0)
        return fail(t, at, "no response pending");
    s->npend--;
    k.t = t;
    k.num = i + 1;
    k.bad = 0;
    if (!deliver(s, &k))
        return fail(t, at, k_lost);
    if (k.bad != 0)
        return fail(t, k.bad, "response line differs");
    if (s->bye && s->npend == 0)
        close_sess(s, 0);
    return 0;
}

/* Run the request line s ("> line" or ">& line"); returns 0 or the
   number of the line that failed. */
static u16 request(invt *t, char *s)
{
    sess *v;
    u16 at;
    u16 n;
    int amp;
    int rc;

    at = t->sc.num;
    v = &t->s[t->cur];
    amp = s[1] == '&';
    s = text_of(s, 1 + amp);
    if (s == NULL)
        return fail(t, at, k_badline);
    if (v->kind == SK_NONE)
        return fail(t, at, k_notopen);
    if (v->kind == SK_MBX && !amp)
        return fail(t, at, "> on a mailbox session");
    if (s[strspn(s, k_blanks)] == '\0')
        return fail(t, at, "blank request");
    show_request(t->out, t->cur + 1, amp, s);
    n = (u16)strlen(s);
    if (v->kind == SK_MBX) {
        if (mbxcli_queue(v->h.mbx, s, n) != MBXCLI_OK)
            return fail(t, at, "mailbox queue full");
        v->queued++;
    } else {
        rc = v->kind == SK_NB ? nbcli_send(v->h.nb, s, n) != NBCLI_OK :
                                sercli_send(v->h.ser, s, n) != SERCLI_OK;
        if (rc)
            return fail(t, at, k_lost);
        v->npend++;
    }
    if (verb_is(s, "BYE"))
        v->bye = 1;
    return amp ? 0 : response(t, t->cur, at);
}

/* Split s in place at its spaces into its words, storing at most max of
   them in w; returns their number, max + 1 when there are more, or 0
   when s holds a tab or a word is empty (s is empty, starts or ends with
   a space or holds two in a row). */
static int split(char *s, char **w, int max)
{
    int n;

    for (n = 0;; n++) {
        if (*s == ' ' || *s == '\0')
            return 0;
        if (n == max)
            return max + 1;
        w[n] = s;
        while (*s != ' ' && *s != '\0') {
            if (*s == '\t')
                return 0;
            s++;
        }
        if (*s == '\0')
            return n + 1;
        *s++ = '\0';
    }
}

/* Open session index i from the local name name, the @CALL being script
   line at; returns 0 or at.  A CALL that timed out or was aborted is
   tried again too: the server may have dropped the session as it
   opened (NetBEUI does so when the session request comes late), or the
   network broken it, and may be listening again. */
static u16 call(invt *t, int i, const char *name, u16 at)
{
    u32 start;
    int rc;
    u8 nbrc;

    if (t->server == NULL) {
        t->code = INVSCR_CALL;
        return fail(t, at, "no server named");
    }
    start = tmr_ticks();
    while ((rc = nbcli_open(&t->s[i].h.nb, t->server, name, &nbrc)) ==
           NBCLI_ECALL &&
           (nbrc == NB_RC_NOANSWER || nbrc == NB_RC_REJECTED ||
            nbrc == NB_RC_TIMEOUT || nbrc == NB_RC_ABORTED) &&
           tmr_since(start) < INVSCR_CALLTIME)
        tmr_delay(TMR_SEC);
    if (rc != NBCLI_OK) {
        fprintf(t->out, "%s, NetBIOS rc=%02X\n",
                rc == NBCLI_ENAME ? "local name not added" :
                rc == NBCLI_ECALL ? "call failed" : k_nomem, nbrc);
        t->code = INVSCR_CALL;
        return at;
    }
    t->s[i].kind = SK_NB;
    return 0;
}

/* Open session index i on the serial port named port at the rate baud
   (NULL for UART_DEFBAUD), the @COM being script line at; returns 0 or
   at. */
static u16 com(invt *t, int i, const char *port, const char *baud,
               u16 at)
{
    int p;
    int rc;
    u16 b;

    p = txt_word_num(k_ports, port) + 1;
    b = baud != NULL ? txt_num(baud, UART_MAXBAUD) : UART_DEFBAUD;
    if (p < 1 || !UART_BAUDOK(b))
        return fail(t, at, k_badline);
    rc = sercli_open(&t->s[i].h.ser, (u8)p, b, 0);
    if (rc != SERCLI_OK) {
        t->code = INVSCR_PORT;
        return fail(t, at, rc == SERCLI_ENOPORT ? "no such port" :
                           rc == SERCLI_EBUSY ? "port in use" : k_nomem);
    }
    t->s[i].kind = SK_SER;
    return 0;
}

/* Open mailbox session index i for station in the mailbox dir, the
   @MBOX being script line at; returns 0 or at. */
static u16 mbox(invt *t, int i, const char *station, const char *dir,
                u16 at)
{
    int rc;

    rc = mbxcli_open(&t->s[i].h.mbx, dir, station);
    if (rc != MBXCLI_OK)
        return fail(t, at, rc == MBXCLI_EARG ? k_badline : k_nomem);
    t->s[i].kind = SK_MBX;
    return 0;
}

/* Post the lines queued on mailbox session s and wait for its response
   file, the @POST being script line at; returns 0 or at.  A file call
   that failed is named with its DOS error. */
static u16 post(invt *t, sess *s, u16 at)
{
    static const char steps[] = MBXCLI_STEPS;
    char word[MBXCLI_STEPLEN + 1];
    int step;
    int err;
    int rc;

    if (s->kind != SK_MBX)
        return fail(t, at, k_notmbx);
    rc = mbxcli_post(s->h.mbx, INVSCR_WAIT);
    if (rc == MBXCLI_EIO) {
        mbxcli_ioerr(s->h.mbx, &step, &err);
        txt_word_at(steps, (u16)step, word, (u16)sizeof word);
        fprintf(t->out, "mailbox failed: %s, DOS error %d\n", word, err);
        return at;
    }
    if (rc != MBXCLI_OK)
        return fail(t, at, rc == MBXCLI_EWAIT ? "no response file" :
                           "mailbox failed");
    s->npend = (u16)(s->npend + s->queued);
    s->queued = 0;
    return 0;
}

/* Run the directive line s (after its '@'); returns 0 or the number of
   the line that failed. */
static u16 directive(invt *t, char *s)
{
    char *w[D_WORDS];
    sess *v;
    u16 at;
    u16 num;
    int nw;
    int d;
    int i;

    at = t->sc.num;
    fprintf(t->out, "@%s\n", s);
    nw = split(s, w, D_WORDS);
    d = nw >= 2 ? txt_word_num(k_dirs, w[0]) : -1;
    if (d < 0 || nw < d_min[d] || nw > d_max[d])
        return fail(t, at, k_badline);
    num = txt_num(w[1], d == D_SLEEP ? SLEEP_MAX : INVSCR_SESSIONS);
    if (num == 0)
        return fail(t, at, k_badline);
    if (d == D_SLEEP) {
        tmr_delay(num);
        return 0;
    }
    i = (int)num - 1;
    v = &t->s[i];
    if (d < D_S && v->kind != SK_NONE)
        return fail(t, at, "session already open");
    if (d < D_S)
        t->cur = i;
    if (d == D_CALL)
        return call(t, i, w[2], at);
    if (d == D_COM)
        return com(t, i, w[2], nw > d_min[D_COM] ? w[3] : NULL, at);
    if (d == D_MBOX)
        return mbox(t, i, w[2], w[3], at);
    if (v->kind == SK_NONE)
        return fail(t, at, k_notopen);
    switch (d) {
    case D_S:
        t->cur = i;
        return 0;
    case D_WAIT:
        return response(t, i, at);
    case D_POST:
        return post(t, v, at);
    case D_HANGUP:
    default:
        close_sess(v, 0);
        return 0;
    }
}

/* Run the script; returns 0 when every line passed, else the number of
   the line that failed, t->code then holding the exit status. */
static u16 run(invt *t)
{
    char *s;
    int glob;
    u16 bad;

    bad = 0;
    while (bad == 0 && (s = peek(&t->sc)) != NULL) {
        t->sc.held = 0;
        if (s[0] == '>')
            bad = request(t, s);
        else if (s[0] == '@')
            bad = directive(t, s + 1);
        else
            bad = fail(t, t->sc.num, expect(s, &glob) != NULL ?
                       "no response line left to check" : k_badline);
    }
    return bad;
}

int invscr_run(FILE *out, const char *server, const char *path,
               u16 *line)
{
    invt t;
    int i;

    *line = 0;
    if (server != NULL && !nb_present()) {
        fprintf(out, "NetBIOS not present on INT 5Ch\n");
        return INVSCR_NONB;
    }
    memset(&t, 0, sizeof t);
    t.sc.f = fopen(path, "r");
    if (t.sc.f == NULL) {
        fprintf(out, "cannot open %s\n", path);
        return INVSCR_FAIL;
    }
    t.out = out;
    t.server = server;
    t.code = INVSCR_FAIL;
    *line = run(&t);
    fclose(t.sc.f);
    for (i = 0; i < INVSCR_SESSIONS; i++)
        close_sess(&t.s[i], 1);
    return *line == 0 ? INVSCR_PASS : t.code;
}
