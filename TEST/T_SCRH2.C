/*
 * T_SCRH2.C - the seeding of the screen tests' harness (TEST/T_SCRH.H):
 * the requests that fill a fresh data directory before each script,
 * kept apart from TEST/T_SCRH.C, which Watcom C 6.5 has no room left to
 * compile with them.  T_SCRH.C describes the data seeded.
 */
#define TF_TERSE
#define TF_PART
#define TF_FILE "T_SCRH2"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#include "T_SCRH.H"
#include "TESTFW.H"

/* The SD_BOM nest (seed_sets) keeps BOM_TOP and item 23 below the four
   items it nests from TH_SEEDITEMS - 4 on (the build fails otherwise). */
extern char th_bom_fits[1];
extern char th_bom_fits[(BOM_TOP < TH_SEEDITEMS - 4 && 23 < TH_SEEDITEMS - 4)
                        ? 1 : 2];

int th_req(const char *line)
{
    rq q;
    rq_res res;

    rq_begin(&q, line);
    if (rq_send(th_lk, &q, NULL, NULL, &res) == RQ_OK)
        return 1;
    th_say("%s -> %d %s\n", line, res.code, res.text);
    return 0;
}

static const char *const makers[] = {
    "IBM", "Commodore", "Tandon", "Hercules", "Western Digital",
    "Seagate", "Logitech"
};
static const char *const cats[] = {
    "SYS", "MOBO", "CPU", "CARD", "DRV", "PERI", "SW", "MAN", "CBL", "PART"
};
/* The grade, works state and tags of the seeded items, in turn. */
static const char *const states[] = {
    "COND=NEW WORKS=Y TAGS=retro", "COND=EXC WORKS=Y",
    "COND=GD WORKS=I TAGS=retro,ISA", "COND=FR", "COND=PR WORKS=N",
    "WORKS=N TAGS=parts", "", "WORKS=N TAGS=Parts,spares"
};
/* The locations seeded, each after the one it is inside (by its code):
   three rooms, a cabinet in the office, shelves, bins and drawers.  The
   items lie at the seven of item_locs, the garage's floor among them;
   the attic's shelf E1 holds none. */
typedef struct {
    const char *code;
    const char *name;
    const char *type;
    const char *in;
    u8   capacity;
} locrow;

static const locrow loc_rows[] = {
    { "GAR", "Garage", "ROOM", NULL, 0 },
    { "OFF", "Office", "ROOM", NULL, 0 },
    { "ATT", "Attic", "ROOM", NULL, 0 },
    { "A1", "Shelf A, top", "SHELF", "GAR", 12 },
    { "A2", "Shelf A, bottom", "SHELF", "GAR", 12 },
    { "B1", "Bin B1", "BIN", "A1", 6 },
    { "B2", "Bin B2", "BIN", "A1", 6 },
    { "CAB", "Cabinet", "CABINET", "OFF", 0 },
    { "C1", "Drawer 1", "DRAWER", "CAB", 20 },
    { "D1", "Desk", "SHELF", "OFF", 4 },
    { "E1", "Attic shelf", "SHELF", "ATT", 10 }
};

static const char *const item_locs[] = {
    "A1", "A2", "B1", "B2", "C1", "GAR", "D1"
};

/* Install child in parent for each "child parent" pair: item 1 holds 2
   and 3, and below 3 hangs the chain 4, 7, 8, 9, 10, 13, 14, 15, 16, so
   16 is ten levels below item 1, deeper than SCR_BOMINDENT; item 20
   holds 21, which holds 22. */
static const char *const assy_pairs[] = {
    "2 1", "3 1", "4 3", "7 4", "8 7", "9 8", "10 9", "13 10", "14 13",
    "15 14", "16 15", "21 20", "22 21"
};

/* Loans and holds as of the fixed date 2026-10-08: loan 1 (item 5)
   and loan 4 (item 6) overdue, loan 2 (item 11, two of three) due
   later, loan 3 (item 6) returned, loan 5 (item 17) due today; item 12
   reserved (row 6, until later) and item 19 in use with item 5, its
   host (row 7). */
static const char *const loan_reqs[] = {
    "LOAN OUT 5 HOLDER=\"Jane Smith\" CONTACT=555-0101 DATE=20260920 "
    "DUE=20261001 NOTES=\"for the VCF\"",
    "LOAN OUT 11 HOLDER=\"Bob Jones\" QTY=2 DUE=20261101",
    "LOAN OUT 6 HOLDER=\"Alexander Longname-Borrower\" DATE=20260901 "
    "DUE=20260910",
    "LOAN RETURN 3 DATE=20260915",
    "LOAN OUT 6 HOLDER=Dora DATE=20260920 DUE=20261002",
    "LOAN OUT 17 HOLDER=Carl DUE=20261008",
    "LOAN RESERVE 12 HOLDER=\"Club show\" DUE=20261020",
    "LOAN USE 19 HOST=5"
};

/* The seed sets of the current script (its SD_ flags). */
static int seed_sets(void)
{
    char line[LINK_MAXLINE + 1];
    int ok;
    u16 i;
    u16 top;

    ok = 1;
    if (th_cur->seed & SD_ASSY)
        for (i = 0; i < sizeof assy_pairs / sizeof assy_pairs[0]; i++) {
            sprintf(line, "ASSY INSTALL %s", assy_pairs[i]);
            ok &= th_req(line);
        }
    /* All the other items installed in item BOM_TOP, except the three
       from TH_SEEDITEMS - 4 on, which hang below 23, and the next one,
       which hangs below the first of them: TH_SEEDITEMS - 1 parts,
       several pages of the Parts tab. */
    if (th_cur->seed & SD_BOM)
        for (i = 1; i <= TH_SEEDITEMS; i++) {
            if (i == BOM_TOP)
                continue;
            top = BOM_TOP;
            if (i >= TH_SEEDITEMS - 4 && i <= TH_SEEDITEMS - 2)
                top = 23;
            else if (i == TH_SEEDITEMS - 1)
                top = TH_SEEDITEMS - 4;
            sprintf(line, "ASSY INSTALL %u %u", (unsigned)i, (unsigned)top);
            ok &= th_req(line);
        }
    /* Two records of item 5 (one with notes), one of item 12, twenty
       of items 30 to 32 (more than a page), and the last one recorded,
       of item 1. */
    if (th_cur->seed & SD_MAINT) {
        ok &= th_req("MAINT ADD 5 MTYPE=RP SUMMARY=\"Recapped the power "
                  "supply\" COST=12.50 DONE_BY=\"N. Fisher\" "
                  "MDATE=20261001 NOTES=\"Two caps.\\nTested OK.\"");
        ok &= th_req("MAINT ADD 12 MTYPE=CL SUMMARY=\"Cleaned the keyboard\"");
        ok &= th_req("MAINT ADD 5 MTYPE=TS SUMMARY=Tested MDATE=20250101");
        for (i = 0; i < 20; i++) {
            sprintf(line, "MAINT ADD %u MTYPE=IN SUMMARY=\"Check %u\" "
                    "MDATE=202609%02u", (unsigned)(30 + i % 3),
                    (unsigned)i, (unsigned)(i + 1));
            ok &= th_req(line);
        }
        ok &= th_req("MAINT ADD 1 MTYPE=MD SUMMARY=\"Fitted a turbo switch\" "
                  "COST=4 MDATE=20261007");
    }
    if (th_cur->seed & SD_LOAN)
        for (i = 0; i < sizeof loan_reqs / sizeof loan_reqs[0]; i++)
            ok &= th_req(loan_reqs[i]);
    if (th_cur->seed & SD_GONE)
        ok &= th_req("ITEM SET 27 STATUS=D");
    return ok;
}

static int seed_reqs(void)
{
    char line[LINK_MAXLINE + 1];
    int ok;
    int i;

    ok = th_req("HELLO TEST");
    ok &= th_req("LOGIN ADMIN admin");
    ok &= th_req("PASSWD admin secret1");
    ok &= th_req("USER ADD OPER LEVEL=2 FULLNAME=\"Otto Operator\" "
              "PASSWORD=oper1");
    ok &= th_req("USER ADD READER LEVEL=1 FULLNAME=\"Rita Reader\" "
              "PASSWORD=read1");
    for (i = 0; i < (int)(sizeof loc_rows / sizeof loc_rows[0]); i++) {
        sprintf(line, "LOC ADD LOC_CODE=%s LOC_NAME=\"%s\" LTYPE=%s "
                "PARENT_ID=%s", loc_rows[i].code, loc_rows[i].name,
                loc_rows[i].type,
                loc_rows[i].in != NULL ? loc_rows[i].in : "0");
        if (loc_rows[i].capacity != 0)
            sprintf(line + strlen(line), " CAPACITY=%u",
                    (unsigned)loc_rows[i].capacity);
        ok &= th_req(line);
    }
    for (i = 1; i <= TH_SEEDITEMS; i++) {
        sprintf(line, "ITEM ADD CATEGORY=%s MFR=\"%s\" MODEL=M%02d-%c "
                "SERIALNO=SN-%04d LOC_ID=%s %s QTY=%d "
                "DESCR=\"Seeded item %d\" ACQ_DATE=1987%02d%02d "
                "ACQ_PRICE=%d.50",
                cats[i % 10], makers[(i * 3) % 7], (i * 7) % 50,
                (char)('A' + i % 5), (i * 37) % 1000, item_locs[i % 7],
                states[i % 8], 1 + i % 3, i, 1 + i % 12, 1 + i % 28,
                i * 3);
        ok &= th_req(line);
    }
    ok &= th_req("ITEM SET 5 DESCR=\"Commodore 64 breadbin\" TAGS=c64,8bit "
              "NOTES=\"Works.\\nNew keyboard.\"");
    ok &= th_req("ITEM SET 12 MODEL=\"5150 CGA\" ITEMTYPE=\"ISA CGA\" "
              "SERIALNO=SN-0007");
    ok &= seed_sets();
    ok &= th_seed();
    ok &= th_req("LOGIN OPER oper1");
    ok &= th_req("PASSWD oper1 oper22");
    ok &= th_req("LOGIN READER read1");
    ok &= th_req("PASSWD read1 read22");
    ok &= th_req("BYE");
    return ok;
}

int th_reseed(void)
{
    char why[LINK_WHYSZ];
    int rc;

    tl_clear();
    te_remove(th_ddir, NULL);
    rc = link_open_local(&th_lk, th_ddir, &th_clk, why, (u16)sizeof why);
    if (!TF_EQ(rc, 0)) {
        th_say("%s\n", why);
        return 0;
    }
    rc = seed_reqs();
    TF_CHECK(rc);
    TF_EQ(link_close(th_lk), 0);
    th_lk = NULL;
    return rc;
}
