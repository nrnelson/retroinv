/*
 * SESS.C - opening and keeping a client's session (SESS.H).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "CFG.H"
#include "SESS.H"

/* The DOS machine name fits a station whole. */
extern char sn_mach_fits[1];
extern char sn_mach_fits[(DOS_MACHLEN <= LINK_STALEN) ? 1 : 2];

/* The STATUS keys, in the order of sn_counts.n. */
static const char *const counts[SN_NCOUNTS] = { "ITEMS", "LOCS", "CATS" };

int sn_station(char *out, const char *net, const char *cfgsta)
{
    char mach[DOS_MACHLEN + 1];
    const char *s;
    int dflt;

    s = net != NULL ? net : cfgsta;
    dflt = 0;
    if (s[0] == '\0') {
        dflt = dos_machine_name(mach);
        s = dflt ? CFG_DEFSTATION : mach;
    }
    txt_copy(out, s, LINK_STALEN + 1);
    return dflt;
}

int sn_hello(link *l, const char *station, char *ver, rq_res *res)
{
    rq q;
    int tries;

    /* A HELLO refused as unknown came garbled: on a serial line, by
       noise ahead of it or by a server port that opened while it was on
       the way.  The refusal answered the garbled line, so the session
       is in step and the HELLO goes once more. */
    tries = 2;
    do {
        rq_begin(&q, "HELLO");
        rq_word(&q, station);
    } while (rq_send(l, &q, NULL, NULL, res) == RQ_EUNKNOWN && --tries > 0);
    if (res->code != RQ_OK || ver[0] != '\0')
        return res->code;
    rq_begin(&q, "VER");
    if (rq_send(l, &q, NULL, NULL, res) == RQ_OK)
        strcpy(ver, res->text);
    return res->code;
}

int sn_login(link *l, const char *user, char *pw, u8 *level, char *name,
             rq_res *res)
{
    rq q;
    char v[LINK_LEVELLEN + 2];  /* a digit, and room to see more        */

    rq_begin(&q, "LOGIN");
    rq_word(&q, user);
    rq_word(&q, pw);
    txt_wipe(pw, SN_PWSZ);
    if (rq_send(l, &q, NULL, NULL, res) != RQ_OK) {
        if (res->code == RQ_EBADPW || res->code == RQ_EDISABLED)
            return res->closed ? SN_TOOMANY : SN_REFUSED;
        return res->closed ? SN_ENDED : SN_FAILED;
    }
    rq_okval(res, "LEVEL", v, (u16)sizeof v);
    *level = (u8)(v[0] >= '0' + SN_LVLMIN && v[0] <= '0' + SN_LVLMAX &&
                  v[1] == '\0' ? v[0] - '0' : SN_LVLMIN);
    name[0] = '\0';
    if (rq_okval(res, "MUSTCHG", v, (u16)sizeof v) && v[0] == '1')
        return SN_MUSTCHG;
    rq_okval(res, "NAME", name, LINK_FULLNAMELEN + 1);
    return SN_IN;
}

/* The first local check the new password nw fails (SN_PW_), or
   SN_PW_DONE. */
static int pw_check(const char *old, const char *nw, const char *rep)
{
    const char *p;

    if (strcmp(nw, rep) != 0)
        return SN_PW_NOMATCH;
    for (p = nw; *p != '\0'; p++)
        if ((unsigned char)*p < 0x21 || (unsigned char)*p > 0x7E)
            return SN_PW_INVALID;
    if (p - nw < LINK_PWMIN || p - nw > LINK_PWMAX)
        return SN_PW_INVALID;
    if (strcmp(nw, old) == 0)
        return SN_PW_SAME;
    return SN_PW_DONE;
}

int sn_passwd(link *l, const char *old, const char *nw, const char *rep,
              rq_res *res)
{
    rq q;
    int st;

    st = pw_check(old, nw, rep);
    if (st != SN_PW_DONE)
        return st;
    rq_begin(&q, "PASSWD");
    rq_word(&q, old);
    rq_word(&q, nw);
    if (rq_send(l, &q, NULL, NULL, res) == RQ_OK)
        return SN_PW_DONE;
    return res->code == RQ_EBADPW && !res->closed ? SN_PW_OLDWRONG :
           SN_PW_FAILED;
}

/* rq_fn: the record counts from the K lines of STATUS. */
static int count_line(void *arg, const rq_line *ln)
{
    sn_counts *c;
    int i;

    c = (sn_counts *)arg;
    if (ln->kind != 'K' || ln->n < 2)
        return 0;
    for (i = 0; i < SN_NCOUNTS; i++)
        if (strcmp(ln->v[0], counts[i]) == 0)
            txt_copy(c->n[i], ln->v[1], TXT_U32LEN + 1);
    return 0;
}

int sn_status(link *l, sn_counts *c, rq_res *res)
{
    rq q;
    int i;

    for (i = 0; i < SN_NCOUNTS; i++)
        strcpy(c->n[i], "?");
    rq_begin(&q, "STATUS");
    return rq_send(l, &q, count_line, c, res);
}
