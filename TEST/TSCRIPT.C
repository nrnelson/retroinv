/*
 * TSCRIPT.C - the request script of TEST/T_INV.SH, for the end-to-end
 * tests (TSCRIPT.H).
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#define TF_PART
#include "TESTFW.H"
#include "TEWIPE.H"
#include "T_FILES.H"
#include "TSCRIPT.H"

#define LONGLEN         300     /* the over-long line's bytes           */
#define FEEDSZ          13      /* bytes link_feed takes at a time      */

/* The script around its HELLO's station and its line of LONGLEN
   bytes. */
static const char k_hello[] = "\r\nHELLO ";
static const char k_body[] = "\r\nLOGIN ADMIN admin\r\nITEM LIST\r\n"
    "PASSWD admin " TSC_PW "\r\nLOGIN ADMIN " TSC_PW "\n"
    "ITEM ADD CATEGORY=SYS MFR=IBM MODEL=5150 "
    "DESCR=\"IBM Personal Computer\" SERIALNO=0012345\r\n"
    "ITEM ADD CATEGORY=DRV MFR=Tandon MODEL=TM100-2 QTY=2\r\n";
static const char k_tail[] = "\r\nITEM GET 1\r\nITEM LIST\r\nping\r\n"
    "BYE\r\nPING\r\n";
static const char k_golden[] = "INVCMD.TXT";

extern char tsc_fits[1];
extern char tsc_fits[(sizeof k_hello + LINK_STALEN + sizeof k_body +
                      LONGLEN + sizeof k_tail - 2 <= TSC_SIZE) ? 1 : 2];

u16 tsc_script(char *buf, const char *station)
{
    u16 n;

    strcpy(buf, k_hello);
    strcat(buf, station);
    strcat(buf, k_body);
    n = (u16)strlen(buf);
    memset(buf + n, 'X', LONGLEN);
    strcpy(buf + n + LONGLEN, k_tail);
    return (u16)(n + LONGLEN + sizeof k_tail - 1);
}

void tsc_golden(char *out, int argc, char **argv)
{
    if (argc > 2 && strlen(argv[2]) < DOS_PATHMAX) {
        strcpy(out, argv[2]);
    } else {
        tf_progdir(out, (u16)(DOS_PATHMAX - sizeof k_golden + 1),
                   argc > 0 ? argv[0] : "");
        strcat(out, k_golden);
    }
}

int tsc_take(void *arg, const char *line, u16 len)
{
    tn_text *t;

    t = (tn_text *)arg;
    if (!TF_CHECK(t->n + len + 2 < TSC_OUTSZ))
        return 1;
    memcpy(t->b + t->n, line, len);
    t->n = (u16)(t->n + len);
    t->b[t->n++] = '\r';
    t->b[t->n++] = '\n';
    t->b[t->n] = '\0';
    return 0;
}

void tsc_same(const tn_text *a, const tn_text *b, const char *what)
{
    u16 i;

    for (i = 0; i < a->n && i < b->n && a->b[i] == b->b[i]; i++)
        ;
    if (!TF_CHECK(i == a->n && i == b->n))
        printf("  %s: %u and %u bytes, first difference at byte %u\n",
               what, (unsigned)a->n, (unsigned)b->n, (unsigned)i);
}

void tsc_run(link *l, const char *script, u16 n, tn_text *t)
{
    u16 at;
    u16 k;
    u16 got;
    int rc;

    rc = LINK_OK;
    for (at = 0; at < n && rc == LINK_OK; at = (u16)(at + k)) {
        k = (u16)(n - at < FEEDSZ ? n - at : FEEDSZ);
        rc = link_feed(l, script + at, k, tsc_take, t);
    }
    TF_EQ(rc, LINK_CLOSED);
    got = t->n;
    TF_EQ(link_send(l, "PING", 4, tsc_take, t), LINK_CLOSED);
    TF_EQ(t->n, got);
}

void tsc_local(const char *script, u16 n, tn_text *t)
{
    static const link_clock clk = { TSC_CLOCK };
    char why[TFS_WHYSZ];
    link *l;

    te_remove(tfs_dir(), NULL);
    why[0] = '\0';
    if (!TF_EQ(link_open_local(&l, tfs_dir(), &clk, why, (u16)sizeof why),
               0)) {
        printf("link_open_local: %s\n", why);
        return;
    }
    tsc_run(l, script, n, t);
    TF_EQ(link_close(l), 0);
}
