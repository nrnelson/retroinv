/*
 * ARGS.C - the connection switches of the client programs (ARGS.H).
 *
 * The rules between connections are judged after the whole scan, as
 * the callers judge their own rules between switches, so that a bad
 * word anywhere on the line is reported before any clash.
 */
#define COMPAT_FIND
#include <stddef.h>
#include "COMPAT.H"
#include "ARGS.H"

/* The connection switches matched whole, a word list of COMPAT.H, and
   their kinds by word number; /COMn is word n + 1.  /TCP: and /MBOX:,
   matched up to the colon as /CFG: is, take the numbers after them,
   AR_TCPWORD and AR_MBOXWORD. */
static const char k_conns[] = "LOCAL NET COM1 COM2";
static const u8 kinds[] = {
    ARGS_LOCAL, ARGS_NET, ARGS_COM, ARGS_COM
#ifdef ARGS_WINSW
    , ARGS_TCP, ARGS_MBOX
#endif
};

static const char k_cfg[] = "CFG:";
#ifdef ARGS_WINSW
#define AR_TCPWORD      4
#define AR_MBOXWORD     5
static const char k_tcp[] = "TCP:";
static const char k_mbox[] = "MBOX:";
#endif

static int is_switch(const char *w)
{
    return w[0] == '/' || w[0] == '-';
}

#ifdef ARGS_WINSW
/* 1 when s is a /TCP: value, addr[:port] as ARGS.H gives it, read into
   a->ip and a->port; else 0. */
static int tcp_addr(args *a, const char *s)
{
    u16 v;
    int n;
    int i;
    char c;

    for (i = 0; i < 4; i++) {
        v = 0;
        for (n = 0; n < 3; n++) {
            c = *s;
            if (!TXT_ISDIGIT(c))
                break;
            v = (u16)(v * 10 + (c - '0'));
            s++;
        }
        if (n == 0 || v > 255 || (i < 3 && *s++ != '.'))
            return 0;
        a->ip[i] = (u8)v;
    }
    a->port = 0;
    if (*s == ':') {
        a->port = txt_num(s + 1, 0xFFFFu);
        return a->port != 0;
    }
    return *s == '\0';
}
#endif

int args_parse(args *a, int argc, char **argv, args_fn fn, void *arg,
               const char **word)
{
    const char *clash;          /* the switch of a second connection    */
    const char *next;           /* the word a switch takes, or NULL     */
    const char *w;
    const char *s;
    char **end;
    char **p;
    int seen;                   /* ARGS_ kinds named                    */
    int rc;
    int e;
    int k;
    int j;

    a->dir = NULL;
    a->net = NULL;
    a->cfg = NULL;
#ifdef ARGS_WINSW
    a->tcp = NULL;
    a->mbox = NULL;
#endif
    a->comname[0] = '\0';
    a->com = 0;
    clash = NULL;
    seen = 0;
    rc = ARGS_OK;
    end = argv + argc;
    for (p = argv + 1; p < end; p++) {
        w = *p;
        /* A word that is no switch matches no switch name. */
        s = is_switch(w) ? w + 1 : "";
        j = txt_word_num(k_conns, s);
#ifdef ARGS_WINSW
        if (j < 0 && txt_ieqn(s, k_tcp, (u16)(sizeof k_tcp - 1)))
            j = AR_TCPWORD;
        else if (j < 0 && txt_ieqn(s, k_mbox, (u16)(sizeof k_mbox - 1)))
            j = AR_MBOXWORD;
#endif
        e = ARGS_OK;
        if (j < 0) {
            if (txt_ieqn(s, k_cfg, (u16)(sizeof k_cfg - 1))) {
                s += sizeof k_cfg - 1;
                if (*s == '\0' || a->cfg != NULL)
                    e = ARGS_EBAD;
                else
                    a->cfg = s;
            } else if (fn != NULL) {
                e = fn(arg, s);
            }
        } else {
            k = kinds[j];
            next = NULL;
            if ((k & (ARGS_LOCAL | ARGS_NET)) && p + 1 < end &&
                !is_switch(p[1]))
                next = *++p;
            if (seen & k) {
                e = ARGS_EBAD;
            } else {
                if (seen != 0 && clash == NULL)
                    clash = w;
                seen |= k;
                if (k == ARGS_COM) {
                    a->com = (u8)(j - 1);
                    txt_ucopy(a->comname, s, (u16)sizeof a->comname);
#ifdef ARGS_WINSW
                } else if (k == ARGS_TCP) {
                    s += sizeof k_tcp - 1;
                    if (tcp_addr(a, s))
                        a->tcp = s;
                    else
                        e = ARGS_EBAD;
                } else if (k == ARGS_MBOX) {
                    s += sizeof k_mbox - 1;
                    if (*s != '\0')
                        a->mbox = s;
                    else
                        e = ARGS_EBAD;
#endif
                } else if (k == ARGS_LOCAL) {
                    a->dir = next;
                } else if (next == NULL) {
                    e = ARGS_ENONAME;
                } else {
                    a->net = next;
                }
            }
        }
        if (e != ARGS_OK && rc == ARGS_OK) {
            rc = e;
            *word = w;
        }
    }
    a->kind = (u8)(seen != 0 ? seen : ARGS_LOCAL);
    if (rc != ARGS_OK)
        return rc;
    *word = clash;
    if (clash != NULL)
        return ARGS_ECLASH;
    if (a->net != NULL && !txt_nbname_ok(a->net)) {
        *word = a->net;
        return ARGS_EBADNAME;
    }
    return ARGS_OK;
}
