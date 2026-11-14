/*
 * T_LOAN2.C - the second part of the T_LOAN test (see T_LOAN.C): the
 * phases of reservations and in-use equipment.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGEDIT.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "ENGLOAN.H"
#define TF_PART
#define TF_FILE "T_LOAN2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TENGH.H"
#include "T_LOAN.H"

#define HOSTS           18      /* hosts of the repair's phase          */

static u32 g_it;                /* the item held in the hold phases     */
static u32 g_host;              /* their host                           */

/* The pairs text pre followed by the number id, for te_kv (one static
   buffer). */
static const char *with(const char *pre, u32 id)
{
    static char spec[TE_TEXTMAX];

    sprintf(spec, "%s%lu", pre, (unsigned long)id);
    return spec;
}

/* 1 when live item id passes the switch flag (ENG_IF_AVAIL or
   ENG_IF_UNAVAIL) of the item lists. */
static int avail(u32 id, int flag)
{
    eng_ifilt f;
    u32 r;

    TF_EQ(eng_item_filters(g_e, &f, NULL, NULL, NULL, NULL, NULL, flag),
          ENG_OK);
    TF_EQ(eng_item_get(g_e, id, &r), ENG_OK);
    return eng_item_keep(g_e, &f);
}

/* LOAN SET of row id from the pairs of spec. */
static int lset(u32 id, const char *spec)
{
    return eng_loan_set(g_e, id, te_p, te_kv(spec));
}

/* Item id, holding units, may be neither disposed of nor deleted
   (naming field), each refused with the reason why (ASSY INSTALL gives
   the same reasons: TEST/SCRIPTS/LOANS.SCR). */
static void held_refused(u32 id, const char *field, const char *why)
{
    TE_FAIL(eng_item_set(g_e, id, te_p, te_kv("STATUS=D")), ENG_ECONSTR,
            "STATUS", why);
    TE_FAIL(eng_item_del(g_e, id), ENG_ECONSTR, field, why);
    TF_STREQ(ifield(id, "STATUS"), "A");
}

/* LOAN RESERVE and LOAN USE: their refusals, the counters of the item
   and its host, held units counted once whatever their kind, and the
   item rules while any are held. */
void test_holds(void)
{
    char parent[TXT_U32LEN + 1];
    u32 it;
    u32 part;
    u32 disp;
    u32 one;
    u32 r;
    u32 u;
    u32 l;
    u32 na;
    u32 nid;

    it = add_item("CATEGORY=SYS|QTY=5");
    g_host = add_item("CATEGORY=SYS");
    disp = add_item("CATEGORY=SYS|STATUS=D");
    part = add_item("CATEGORY=CARD");
    txt_u32(parent, it);
    TF_EQ(eng_edit_begin(g_e, ENG_T_ITEMS, te_irec(part)), ENG_OK);
    TF_EQ(eng_edit_put(g_e, "PARENT_ID", parent), ENG_OK);
    TF_EQ(eng_edit_commit(g_e, part), ENG_OK);
    na = te_naud();

    TE_FAIL(row(ENG_K_RESERVE, it, "QTY=1"), ENG_EARG, "HOLDER",
            "name who or which item it is for");
    TE_FAIL(row(ENG_K_USE, it, "HOLDER=|HOST=0"), ENG_EARG, "HOLDER",
            "name who or which item it is for");
    TE_FAIL(row(ENG_K_USE, it, "HOLDER=Desk|DUE=20261020"), ENG_EARG, "DUE",
            "field not settable");
    TE_FAIL(row(ENG_K_USE, it, "HOLDER=Desk|DUE_DATE=20261020"), ENG_EARG,
            "DUE_DATE", "field not settable");
    TE_FAIL(row(ENG_K_RESERVE, it, "HOST=999"), ENG_ENOTFOUND, "HOST_ID",
            "no such item");
    TE_FAIL(row(ENG_K_RESERVE, it, with("HOST=", it)), ENG_ECONSTR,
            "HOST_ID", "cannot be used with itself or its own parts");
    TE_FAIL(row(ENG_K_USE, it, with("HOST_ID=", part)), ENG_ECONSTR,
            "HOST_ID", "cannot be used with itself or its own parts");
    TE_FAIL(row(ENG_K_USE, it, with("HOST=", disp)), ENG_ECONSTR, "HOST_ID",
            "item was disposed of");
    TE_FAIL(row(ENG_K_USE, it, "HOST=-1"), ENG_EARG, "HOST_ID", NULL);
    TE_FAIL(row(ENG_K_USE, it, "HOST=1|HOST_ID=1"), ENG_EARG, "HOST_ID",
            NULL);
    TE_FAIL(row(ENG_K_USE, part, "HOLDER=X"), ENG_ECONSTR, "ITEM",
            "installed in an assembly");
    TE_FAIL(row(ENG_K_RESERVE, disp, "HOLDER=X"), ENG_ECONSTR, "ITEM",
            "item was disposed of");
    TE_FAIL(row(ENG_K_RESERVE, it, "HOLDER=X|QTY=6"), ENG_ECONSTR, "QTY",
            "not enough on hand");
    TE_FAIL(row(ENG_K_RESERVE, it, "HOLDER=X|DATE=20261010|DUE=20261009"),
            ENG_EARG, "DUE_DATE", "due date before out date");
    TF_EQ(g_id, 0);
    TF_EQ(te_naud(), na);
    TF_CHECK(!marked());

    /* A reservation, an assignment to a host and a loan hold all five
       units, each counted once. */
    r = hold(ENG_K_RESERVE, it, "HOLDER=Club show|QTY=2|DUE=20261020");
    TF_STREQ(lfield(r, "KIND"), "R");
    TF_STREQ(lfield(r, "HOST_ID"), "0");
    TF_STREQ(lfield(r, "OUT_DATE"), "20261009");
    TF_STREQ(ifield(it, "RESERVED"), "2");
    TF_STREQ(ifield(it, "ON_LOAN"), "0");
    TF_EQ(te_naud(), na + 2);
    TF_STREQ(te_aud(na + 2, "ACTION FLD OLDVAL NEWVAL"), "O|RESERVED|0|2");
    TF_STREQ(te_aud(na + 2, "REC_ID"), with("", it));
    na = te_naud();
    u = hold(ENG_K_USE, it, with("QTY=1|HOST=", g_host));
    TF_STREQ(lfield(u, "KIND"), "U");
    TF_STREQ(lfield(u, "HOLDER"), "");
    TF_STREQ(lfield(u, "DUE_DATE"), "");
    TF_STREQ(ifield(it, "IN_USE"), "1");
    TF_STREQ(ifield(it, "ATTACHED"), "0");
    TF_STREQ(ifield(g_host, "ATTACHED"), "1");
    TF_STREQ(ifield(g_host, "UPDATED"), "20261009");
    TF_EQ(te_naud(), na + 3);
    TF_STREQ(te_aud(na + 2, "ACTION FLD OLDVAL NEWVAL"), "O|IN_USE|0|1");
    TF_STREQ(te_aud(na + 3, "ACTION FLD OLDVAL NEWVAL"), "O|ATTACHED|0|1");
    TF_STREQ(te_aud(na + 3, "REC_ID"), with("", g_host));
    l = lend(it, "HOLDER=Ann|DUE=20261030|QTY=2");
    TF_STREQ(ifield(it, "ON_LOAN"), "2");
    TF_STREQ(ifield(it, "RESERVED"), "2");
    TF_STREQ(ifield(it, "IN_USE"), "1");
    TF_CHECK(!marked());
    COUNTS_OK();
    na = te_naud();
    TE_FAIL(row(ENG_K_LOAN, it, "HOLDER=X|DUE=20261030"), ENG_ECONSTR, "QTY",
            "not enough on hand");
    TE_FAIL(row(ENG_K_RESERVE, it, "HOLDER=X"), ENG_ECONSTR, "QTY",
            "not enough on hand");
    TE_FAIL(row(ENG_K_USE, it, "HOLDER=X"), ENG_ECONSTR, "QTY",
            "not enough on hand");
    TF_EQ(avail(it, ENG_IF_AVAIL), 0);
    TF_EQ(avail(it, ENG_IF_UNAVAIL), 1);

    /* The item rules while units are held. */
    TE_FAIL(eng_item_set(g_e, it, te_p, te_kv("QTY=4")), ENG_ECONSTR, "QTY",
            "quantity below units lent, reserved or in use");
    TE_FAIL(eng_item_split(g_e, it, "1", &nid), ENG_ECONSTR, "QTY",
            "not enough on hand");
    held_refused(it, "ON_LOAN", "item is on loan");
    TE_FAIL(eng_item_set(g_e, g_host, te_p, te_kv("STATUS=D")), ENG_ECONSTR,
            "STATUS", "equipment is reserved for or in use with it");
    TE_FAIL(eng_item_del(g_e, g_host), ENG_ECONSTR, "ATTACHED",
            "equipment is reserved for or in use with it");
    TF_EQ(te_naud(), na);
    TF_EQ(eng_item_set(g_e, it, te_p, te_kv("QTY=6")), ENG_OK);
    TF_EQ(avail(it, ENG_IF_AVAIL), 1);
    TF_EQ(eng_item_split(g_e, it, "1", &nid), ENG_OK);
    TF_STREQ(ifield(it, "QTY"), "5");
    TF_STREQ(ifield(nid, "RESERVED"), "0");
    TF_STREQ(ifield(nid, "IN_USE"), "0");
    TF_EQ(avail(it, ENG_IF_AVAIL), 0);

    /* Each end lowers its own counter only. */
    TF_EQ(eng_loan_return(g_e, l, NULL), ENG_OK);
    TF_STREQ(ifield(it, "ON_LOAN"), "0");
    TF_STREQ(ifield(it, "RESERVED"), "2");
    TF_EQ(avail(it, ENG_IF_AVAIL), 1);
    held_refused(it, "RESERVED", "item is reserved");
    TF_EQ(eng_loan_return(g_e, r, "20261009"), ENG_OK);
    TF_STREQ(ifield(it, "RESERVED"), "0");
    TF_STREQ(ifield(it, "IN_USE"), "1");
    held_refused(it, "IN_USE", "item is in use");
    na = te_naud();
    TF_EQ(eng_loan_return(g_e, u, NULL), ENG_OK);
    TF_STREQ(ifield(it, "IN_USE"), "0");
    TF_STREQ(ifield(g_host, "ATTACHED"), "0");
    TF_EQ(te_naud(), na + 3);
    TF_STREQ(te_aud(na + 2, "ACTION FLD OLDVAL NEWVAL"), "R|IN_USE|1|0");
    TF_STREQ(te_aud(na + 3, "ACTION FLD OLDVAL NEWVAL"), "R|ATTACHED|1|0");
    TE_FAIL(eng_loan_return(g_e, u, NULL), ENG_ECONSTR, "ID",
            "already returned or released");
    TF_EQ(eng_item_set(g_e, g_host, te_p, te_kv("STATUS=D")), ENG_OK);
    TF_EQ(eng_item_set(g_e, g_host, te_p, te_kv("STATUS=A")), ENG_OK);
    TF_CHECK(!marked());
    COUNTS_OK();

    /* A release makes an item available only when nothing else holds
       its units. */
    one = add_item("CATEGORY=SYS|QTY=2");
    r = hold(ENG_K_RESERVE, one, "HOLDER=X");
    u = hold(ENG_K_USE, one, "HOLDER=Y");
    TF_EQ(avail(one, ENG_IF_AVAIL), 0);
    TF_EQ(eng_item_set(g_e, one, te_p, te_kv("QTY=1")), ENG_ECONSTR);
    TF_EQ(eng_loan_return(g_e, r, NULL), ENG_OK);
    TF_EQ(avail(one, ENG_IF_AVAIL), 1);
    TF_EQ(eng_item_set(g_e, one, te_p, te_kv("QTY=1")), ENG_OK);
    TF_EQ(avail(one, ENG_IF_AVAIL), 0);
    TE_FAIL(row(ENG_K_RESERVE, one, "HOLDER=X"), ENG_ECONSTR, "QTY",
            "not enough on hand");
    TF_EQ(eng_loan_return(g_e, u, NULL), ENG_OK);
    TF_EQ(avail(one, ENG_IF_AVAIL), 1);
    TF_EQ(avail(one, ENG_IF_UNAVAIL), 0);
    TF_EQ(avail(disp, ENG_IF_UNAVAIL), 1);
    TF_EQ(avail(part, ENG_IF_UNAVAIL), 1);
    TF_EQ(avail(part, ENG_IF_AVAIL), 0);
    g_it = it;
}

/* LOAN SET: a host given, moved, taken away; the keys of each kind;
   the refusals, which write nothing; a host given to an ended row
   changes no counter. */
void test_hset(void)
{
    u32 h2;
    u32 r;
    u32 u;
    u32 l;
    u32 na;

    h2 = add_item("CATEGORY=SYS");
    r = hold(ENG_K_RESERVE, g_it, with("HOST=", g_host));
    TF_STREQ(ifield(g_host, "ATTACHED"), "1");
    na = te_naud();
    TF_EQ(lset(r, with("HOST=", h2)), ENG_OK);
    TF_STREQ(lfield(r, "HOST_ID"), with("", h2));
    TF_STREQ(ifield(g_host, "ATTACHED"), "0");
    TF_STREQ(ifield(h2, "ATTACHED"), "1");
    TF_EQ(te_naud(), na + 3);
    TF_STREQ(te_aud(na + 1, "TBL ACTION FLD"), "LOANS|U|HOST_ID");
    TF_STREQ(te_aud(na + 2, "ACTION FLD OLDVAL NEWVAL"), "R|ATTACHED|1|0");
    TF_STREQ(te_aud(na + 3, "ACTION FLD OLDVAL NEWVAL"), "O|ATTACHED|0|1");
    TF_CHECK(!marked());

    u = hold(ENG_K_USE, g_it, "HOLDER=Lab");
    l = lend(g_it, "HOLDER=Ann|DUE=20261030");
    na = te_naud();
    TE_FAIL(lset(r, "HOST=0"), ENG_EARG, "HOLDER",
            "name who or which item it is for");
    TE_FAIL(lset(r, with("HOST=", g_it)), ENG_ECONSTR, "HOST_ID",
            "cannot be used with itself or its own parts");
    TE_FAIL(lset(r, "HOST=999"), ENG_ENOTFOUND, "HOST_ID", "no such item");
    TE_FAIL(lset(r, "QTY=2"), ENG_EARG, "QTY", "field not settable");
    TE_FAIL(lset(r, "KIND=U"), ENG_EARG, "KIND", "field not settable");
    TE_FAIL(lset(r, "DATE=20261001"), ENG_EARG, "DATE",
            "field not settable");
    TE_FAIL(lset(u, "DUE=20261031"), ENG_EARG, "DUE", "field not settable");
    TE_FAIL(lset(u, "HOLDER="), ENG_EARG, "HOLDER",
            "name who or which item it is for");
    TE_FAIL(lset(l, with("HOST=", g_host)), ENG_EARG, "HOST",
            "field not settable");
    TE_FAIL(lset(l, "HOLDER="), ENG_EARG, "HOLDER", "value required");
    TE_FAIL(lset(l, "DUE="), ENG_EARG, "DUE_DATE", "value required");
    TF_EQ(te_naud(), na);
    TF_CHECK(!marked());
    TF_STREQ(ifield(h2, "ATTACHED"), "1");

    TF_EQ(lset(r, "HOLDER=Bob|HOST=0|DUE=20261031"), ENG_OK);
    TF_STREQ(ifield(h2, "ATTACHED"), "0");
    TF_STREQ(lfield(r, "DUE_DATE"), "20261031");
    TF_EQ(lset(u, with("HOST=", g_host)), ENG_OK);
    TF_STREQ(ifield(g_host, "ATTACHED"), "1");
    TF_EQ(lset(u, "NOTES=bench"), ENG_OK);
    TF_STREQ(ifield(g_host, "ATTACHED"), "1");
    COUNTS_OK();
    TF_EQ(eng_loan_return(g_e, r, NULL), ENG_OK);
    TF_EQ(eng_loan_return(g_e, u, NULL), ENG_OK);
    TF_EQ(eng_loan_return(g_e, l, NULL), ENG_OK);
    TF_STREQ(ifield(g_host, "ATTACHED"), "0");
    TF_EQ(lset(r, with("HOST=", g_host)), ENG_OK);
    TF_STREQ(ifield(g_host, "ATTACHED"), "0");
    TF_EQ(lset(r, "HOST=0"), ENG_OK);
    COUNTS_OK();
}

/* ASSY INSTALL may not put a host inside the item whose equipment it
   is: neither the host itself nor an assembly holding it, into the item
   or into one of its parts, whatever loan comes before the row; an
   install that holds no host is made, and once the rows end the host
   goes in too, whatever another item's rows say, even with the item's
   IN_USE set too high by hand. */
void test_hinstall(void)
{
    u32 sys;
    u32 mon;
    u32 card;
    u32 box;
    u32 pcb;
    u32 cab;
    u32 u;
    u32 r;

    sys = add_item("CATEGORY=SYS");
    mon = add_item("CATEGORY=PERI|QTY=3");
    card = add_item("CATEGORY=CARD");
    box = add_item("CATEGORY=SYS");
    pcb = add_item("CATEGORY=PART");
    cab = add_item("CATEGORY=PERI");
    lend(mon, "HOLDER=Ann|DUE=20261231");
    u = hold(ENG_K_USE, mon, with("HOST=", sys));
    r = hold(ENG_K_RESERVE, mon, with("HOST=", card));
    TF_EQ(eng_assy_install(g_e, card, box), ENG_OK);
    TF_EQ(eng_assy_install(g_e, pcb, mon), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, sys, mon), ENG_ECONSTR, "PARENT",
            "cannot be used with itself or its own parts");
    TE_FAIL(eng_assy_install(g_e, box, pcb), ENG_ECONSTR, "PARENT",
            "cannot be used with itself or its own parts");
    TE_FAIL(eng_assy_install(g_e, card, pcb), ENG_ECONSTR, "PARENT",
            "cannot be used with itself or its own parts");
    TF_STREQ(ifield(sys, "PARENT_ID"), "0");
    TF_STREQ(ifield(card, "PARENT_ID"), with("", box));
    TF_EQ(eng_assy_install(g_e, sys, box), ENG_OK);
    TF_EQ(eng_loan_return(g_e, u, NULL), ENG_OK);
    TE_FAIL(eng_assy_install(g_e, box, pcb), ENG_ECONSTR, "PARENT",
            "cannot be used with itself or its own parts");
    TE_FAIL(eng_assy_install(g_e, card, pcb), ENG_ECONSTR, "PARENT",
            "cannot be used with itself or its own parts");
    TF_EQ(eng_loan_return(g_e, r, NULL), ENG_OK);
    hold(ENG_K_RESERVE, cab, with("HOST=", card));
    hold(ENG_K_USE, mon, "HOLDER=Lab");
    raw_num(ENG_T_ITEMS, te_irec(mon), "IN_USE", 2);
    TF_EQ(eng_assy_install(g_e, card, pcb), ENG_OK);
    raw_num(ENG_T_ITEMS, te_irec(mon), "IN_USE", 1);
    TF_EQ(eng_assy_install(g_e, box, pcb), ENG_OK);
    TF_STREQ(ifield(card, "PARENT_ID"), with("", pcb));
    TF_STREQ(ifield(box, "PARENT_ID"), with("", pcb));
    COUNTS_OK();
}

/* The INSTALL check reads the item's own rows: it stops once its open
   rows hold its units, so an item whose open row comes before
   ENG_MAXREADS ended ones takes the install; an open row past
   ENG_MAXREADS of them is refused rather than passed, whatever it
   names.  In a data directory of its own, as its rows would not fit the
   RAM disk of the DOS runs beside the others. */
void test_hmany(void)
{
    u32 sys;
    u32 mon;
    u32 other;
    u32 first;
    u32 i;

    sys = add_item("CATEGORY=SYS");
    mon = add_item("CATEGORY=PERI|QTY=2");
    other = add_item("CATEGORY=CARD");
    first = hold(ENG_K_USE, mon, "HOLDER=Lab");
    for (i = 0; i < ENG_MAXREADS; i++)
        TF_EQ(eng_loan_return(g_e, hold(ENG_K_USE, mon, "HOLDER=Lab"),
                              NULL), ENG_OK);
    TF_EQ(eng_assy_install(g_e, other, mon), ENG_OK);
    TF_EQ(eng_loan_return(g_e, first, NULL), ENG_OK);
    hold(ENG_K_USE, mon, "HOLDER=Lab");
    TE_FAIL(eng_assy_install(g_e, sys, mon), ENG_ECONSTR, "PARENT",
            "item is in use");
    TF_EQ(eng_loan_return(g_e, g_id, NULL), ENG_OK);
    hold(ENG_K_RESERVE, mon, with("HOST=", sys));
    TE_FAIL(eng_assy_install(g_e, sys, mon), ENG_ECONSTR, "PARENT",
            "item is reserved");
    TF_EQ(eng_loan_return(g_e, g_id, NULL), ENG_OK);
    TF_EQ(eng_assy_install(g_e, sys, mon), ENG_OK);
    COUNTS_OK();
}

/* LOAN LIST by host, by kind and by holder, paged; LOAN OVERDUE takes
   in a reservation whose date has passed, never an assignment. */
void test_hlists(void)
{
    char want[LISTMAX];
    eng_scan s;
    u32 a;
    u32 b;
    u32 x[7];
    u32 w;
    int pages;
    int i;

    a = add_item("CATEGORY=SYS|QTY=9");
    b = add_item("CATEGORY=SYS|QTY=9");
    x[0] = hold(ENG_K_RESERVE, a, "HOLDER=Fair|DATE=20261001|DUE=20261005");
    x[1] = hold(ENG_K_USE, a, with("HOST=", g_host));
    x[2] = lend(a, "HOLDER=Q|DATE=20261001|DUE=20261008");
    x[3] = hold(ENG_K_RESERVE, a, with("HOST=", g_host));
    x[4] = hold(ENG_K_USE, a, "HOLDER=Desk");
    x[5] = hold(ENG_K_RESERVE, b, with("HOLDER=Z|DUE=20261111|HOST=",
                                       g_host));
    x[6] = hold(ENG_K_USE, b, with("HOST=", g_host));
    TF_EQ(eng_loan_return(g_e, x[6], NULL), ENG_OK);
    COUNTS_OK();

    /* The open rows naming the host, by due date (blank first); the
       last page of one row finds that no other row is the host's. */
    sprintf(want, "%lu %lu %lu", (unsigned long)x[1], (unsigned long)x[3],
            (unsigned long)x[5]);
    TF_STREQ(ids(0, 0, g_host, 0, 0, NULL, 25, &pages), want);
    TF_EQ(pages, 1);
    TF_STREQ(ids(0, 0, g_host, 0, 0, NULL, 1, &pages), want);
    TF_EQ(pages, 4);
    sprintf(want, "%lu %lu", (unsigned long)x[1], (unsigned long)x[3]);
    TF_STREQ(ids(0, a, g_host, 0, 0, NULL, 1, &pages), want);
    sprintf(want, "%lu %lu", (unsigned long)x[3], (unsigned long)x[5]);
    TF_STREQ(ids(0, 0, g_host, ENG_K_RESERVE, 0, NULL, 1, &pages), want);
    sprintf(want, "%lu", (unsigned long)x[5]);
    TF_STREQ(ids(0, b, g_host, ENG_K_RESERVE, 0, NULL, 1, &pages), want);
    TE_FAIL(eng_loan_scan(g_e, &s, 0, 999, 0, NULL, 0, NULL), ENG_ENOTFOUND,
            "HOST", "no such item");

    /* By kind, of one item, all or open. */
    sprintf(want, "%lu %lu", (unsigned long)x[0], (unsigned long)x[3]);
    TF_STREQ(ids(0, a, 0, ENG_K_RESERVE, 0, NULL, 1, &pages), want);
    sprintf(want, "%lu %lu", (unsigned long)x[1], (unsigned long)x[4]);
    TF_STREQ(ids(0, a, 0, ENG_K_USE, 1, NULL, 25, &pages), want);
    sprintf(want, "%lu", (unsigned long)x[2]);
    TF_STREQ(ids(0, a, 0, ENG_K_LOAN, 0, NULL, 25, &pages), want);
    sprintf(want, "%lu", (unsigned long)x[6]);
    TF_STREQ(ids(0, b, 0, ENG_K_USE, 0, NULL, 25, &pages), want);
    TF_STREQ(ids(0, b, 0, ENG_K_USE, 1, NULL, 25, &pages), "");

    /* By holder: the whole HOLDER in any case, alone (a page of one
       row, then one past it that keeps none) and with each other
       filter, and one as wide as the field; a part or more of it keeps
       nothing, an empty one every row. */
    g_holder = "fAIR";
    sprintf(want, "%lu", (unsigned long)x[0]);
    TF_STREQ(ids(0, 0, 0, 0, 0, NULL, 1, &pages), want);
    TF_EQ(pages, 2);
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), want);
    TF_STREQ(ids(0, b, 0, 0, 0, NULL, 25, &pages), "");
    g_holder = "Fai";
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), "");
    g_holder = "Fairs";
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), "");
    g_holder = "DESK";
    sprintf(want, "%lu", (unsigned long)x[4]);
    TF_STREQ(ids(0, a, 0, ENG_K_USE, 1, NULL, 25, &pages), want);
    TF_STREQ(ids(0, a, 0, ENG_K_RESERVE, 0, NULL, 25, &pages), "");
    g_holder = "z";
    sprintf(want, "%lu", (unsigned long)x[5]);
    TF_STREQ(ids(0, 0, g_host, 0, 0, NULL, 25, &pages), want);
    g_holder = "Q";
    TF_STREQ(ids(0, 0, g_host, 0, 0, NULL, 25, &pages), "");
    g_holder = "";
    sprintf(want, "%lu %lu %lu %lu %lu", (unsigned long)x[0],
            (unsigned long)x[1], (unsigned long)x[2], (unsigned long)x[3],
            (unsigned long)x[4]);
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), want);
    g_holder = "abcdefghijklmnopqrstuvwxyz0123";
    w = lend(a, "HOLDER=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123|DUE=20261111");
    sprintf(want, "%lu", (unsigned long)w);
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), want);
    TF_EQ(eng_loan_return(g_e, w, NULL), ENG_OK);
    g_holder = NULL;
    TF_EQ(eng_loan_scan(g_e, &s, 0, 0, 0, "123456789012345678901234567890",
                        0, NULL), ENG_OK);
    TE_FAIL(eng_loan_scan(g_e, &s, 0, 0, 0,
                          "1234567890123456789012345678901", 0, NULL),
            ENG_EARG, "HOLDER", "filter value too long");

    /* Overdue: the open loans and reservations due before ASOF. */
    sprintf(want, "10 12 %lu", (unsigned long)x[0]);
    TF_STREQ(ids(1, 0, 0, 0, 0, "20261006", 25, &pages), want);
    sprintf(want, "10 12 %lu 7 8 %lu", (unsigned long)x[0],
            (unsigned long)x[2]);
    TF_STREQ(ids(1, 0, 0, 0, 0, NULL, 25, &pages), want);
    for (i = 0; i < 6; i++)
        TF_EQ(eng_loan_return(g_e, x[i], NULL), ENG_OK);
    COUNTS_OK();

    /* A holder's rows open or ended. */
    g_holder = "q";
    TF_STREQ(ids(0, a, 0, 0, 1, NULL, 25, &pages), "");
    TF_STREQ(ids(0, 0, 0, 0, 1, NULL, 25, &pages), "");
    sprintf(want, "%lu", (unsigned long)x[2]);
    TF_STREQ(ids(0, a, 0, 0, 0, NULL, 25, &pages), want);
    g_holder = NULL;
}

/* A directory whose counters were damaged by hand, those of many hosts
   among them, repaired by the next open with one row by SYSTEM per
   correction; a second pass changes nothing. */
void test_hheal(void)
{
    u32 big;
    u32 none;
    u32 neg;
    u32 mix;
    u32 lone;
    u32 cap;
    u32 cl;
    u32 cr;
    u32 h[HOSTS];
    u32 odd[2];
    u32 low;
    u32 na;
    u32 n;
    u32 k;
    int i;

    big = add_item("CATEGORY=SYS|QTY=40");
    none = add_item("CATEGORY=SYS");
    cap = add_item("CATEGORY=SYS|QTY=3");
    neg = add_item("CATEGORY=SYS|QTY=3");
    mix = add_item("CATEGORY=SYS|QTY=5");
    lone = add_item("CATEGORY=SYS");
    for (i = 0; i < HOSTS; i++)
        h[i] = add_item("CATEGORY=SYS");
    for (i = 0; i < HOSTS; i++) {
        hold(ENG_K_USE, big, with("HOST=", h[i]));
        if (i >= 1 && i <= 2)
            TF_EQ(eng_loan_get(g_e, g_id, &odd[i - 1]), ENG_OK);
    }
    hold(ENG_K_USE, big, with("HOST=", h[HOSTS - 1]));
    TF_EQ(eng_loan_get(g_e, hold(ENG_K_RESERVE, mix, "HOLDER=Club"), &low),
          ENG_OK);
    hold(ENG_K_RESERVE, mix, "HOLDER=Club|QTY=2");
    hold(ENG_K_RESERVE, big, "HOLDER=Club|QTY=3");
    lend(big, "HOLDER=Ann|DUE=20261030|QTY=2");
    cl = lend(cap, "HOLDER=Bo|DUE=20261030|QTY=2");
    cr = hold(ENG_K_RESERVE, cap, "HOLDER=Club");
    COUNTS_OK();
    /* A loan naming a host (another program's doing) is not counted on
       it; a loan's QTY raised past what its item holds leaves no room for
       the reservation. */
    raw_num(ENG_T_LOANS, eng_reccount(g_e, ENG_T_LOANS) - 1, "HOST_ID",
            (i32)h[0]);
    raw_num(ENG_T_LOANS, eng_reccount(g_e, ENG_T_LOANS) - 1, "QTY", 3);
    /* Two in-use rows whose KIND is blank and unknown count as loans;
       a negative ON_LOAN beside a RESERVED of no row hides neither. */
    TF_EQ(te_raw_put(ENG_T_LOANS, odd[0], -1, "KIND", "", 0), 0);
    TF_EQ(te_raw_put(ENG_T_LOANS, odd[1], -1, "KIND", "X", 0), 0);
    raw_num(ENG_T_ITEMS, te_irec(neg), "ON_LOAN", -2);
    raw_num(ENG_T_ITEMS, te_irec(neg), "RESERVED", 2);
    raw_num(ENG_T_ITEMS, te_irec(big), "ON_LOAN", 7);
    raw_num(ENG_T_ITEMS, te_irec(big), "RESERVED", 0);
    raw_num(ENG_T_ITEMS, te_irec(big), "IN_USE", 3);
    raw_num(ENG_T_ITEMS, te_irec(h[0]), "ATTACHED", 0);
    raw_num(ENG_T_ITEMS, te_irec(h[HOSTS / 2]), "ATTACHED", 4);
    raw_num(ENG_T_ITEMS, te_irec(h[HOSTS - 1]), "ATTACHED", 1);
    raw_num(ENG_T_ITEMS, te_irec(none), "ATTACHED", 2);
    raw_num(ENG_T_ITEMS, te_irec(none), "RESERVED", 1);
    raw_num(ENG_T_ITEMS, te_irec(g_host), "ATTACHED", 1);
    /* A negative QTY on an open row takes nothing from the one beside
       it; a negative ATTACHED with no row naming the host is cleared. */
    raw_num(ENG_T_LOANS, low, "QTY", -4);
    raw_num(ENG_T_ITEMS, te_irec(lone), "ATTACHED", -1);
    na = te_naud();
    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;

    TF_EQ(te_marker_put("LOANS"), 0);
    if (!TF_EQ(te_open(&g_e, &g_next), ENG_OK))
        return;
    TF_CHECK(!marked());
    TF_STREQ(ifield(big, "ON_LOAN"), "4");
    TF_STREQ(ifield(big, "RESERVED"), "3");
    TF_STREQ(ifield(big, "IN_USE"), with("", HOSTS - 1));
    TF_STREQ(ifield(h[1], "ATTACHED"), "0");
    TF_STREQ(ifield(h[2], "ATTACHED"), "0");
    TF_STREQ(ifield(neg, "ON_LOAN"), "0");
    TF_STREQ(ifield(neg, "RESERVED"), "0");
    TF_STREQ(ifield(h[0], "ATTACHED"), "1");
    TF_STREQ(ifield(h[HOSTS / 2], "ATTACHED"), "1");
    TF_STREQ(ifield(h[HOSTS - 1], "ATTACHED"), "2");
    TF_STREQ(ifield(none, "ATTACHED"), "0");
    TF_STREQ(ifield(none, "RESERVED"), "0");
    TF_STREQ(ifield(g_host, "ATTACHED"), "0");
    TF_STREQ(ifield(cap, "ON_LOAN"), "3");
    TF_STREQ(ifield(cap, "RESERVED"), "0");
    TF_STREQ(ifield(mix, "RESERVED"), "2");
    TF_STREQ(ifield(lone, "ATTACHED"), "0");
    n = te_naud() - na;
    TF_EQ(n, 17);
    for (k = 1; k <= n; k++)
        TF_STREQ(te_aud(na + k, "USERNAME TBL"), "SYSTEM|ITEMS");
    /* The first pass meets the host whose ATTACHED fell to 0 among the
       rows of item big, before it corrects big. */
    TF_STREQ(te_aud(na + 1, "ACTION FLD OLDVAL NEWVAL"), "O|ATTACHED|0|1");
    TF_STREQ(te_aud(na + 1, "REC_ID"), with("", h[0]));
    TF_STREQ(te_aud(na + 2, "ACTION FLD OLDVAL NEWVAL"), "R|ON_LOAN|7|4");
    TF_STREQ(te_aud(na + 3, "ACTION FLD OLDVAL NEWVAL"), "O|RESERVED|0|3");
    TF_STREQ(te_aud(na + 4, "ACTION FLD OLDVAL NEWVAL"),
             with("O|IN_USE|3|", HOSTS - 1));

    na = te_naud();
    TF_EQ(eng_loan_heal(g_e), ENG_OK);
    TF_EQ(te_naud(), na);
    TF_EQ(eng_loan_return(g_e, cl, NULL), ENG_OK);
    TF_EQ(eng_loan_return(g_e, cr, NULL), ENG_OK);
    TF_STREQ(ifield(cap, "ON_LOAN"), "0");
    TF_STREQ(ifield(cap, "RESERVED"), "0");
    COUNTS_OK();
}
