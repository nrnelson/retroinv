/*
 * T_SCRE.C - the item, location, code list, search, assembly, maintenance
 * and loan screens (SRC/CLIENT/SCRITEM.C, SCRLOC.C, SCRCODE.C, SCRSRCH.C,
 * SCRASSY.C, SCRMAINT.C, SCRLOAN.C) run under the key scripts
 * TEST/KEYS/SCRE*.KEY, their screens compared with golden snapshots, by the
 * harness of TEST/T_SCRH.C: the program's name, its scripts and the
 * commands it starts.  It links no report or CSV screen (TEST/T_SCRR.C has
 * those), so that its deepest scripts run with the free memory INV is
 * documented to need.
 *
 * usage: T_SCRE [options] dir [file...], as TEST/T_SCRH.C gives them.
 *
 * Some scripts must send a given number of requests after the login
 * (reqcounts): every list asks for its first page once when it opens,
 * and again only for a key or a form left, so a list that filled its
 * first page twice would send more, and an item form loads its item
 * again after a tab answered 200.
 */
#define TF_TERSE
#define TF_PART
#define TF_FILE "T_SCRE"
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LISTV.H"
#include "SCR.H"
#include "TRLINK.H"
#include "T_SCRH.H"
#include "TESTFW.H"

const char th_name[] = "T_SCRE";

/* Answers several scripts give in the engine's place, each kept once. */
static const char k_gone[] = "-ERR 200 not found";
static const char k_gone_id[] = "-ERR 200 not found FIELD=ID";
static const char k_perm[] = "-ERR 203 permission denied";
static const char k_expired[] = "-ERR 400 HELLO or LOGIN required";
static const char k_mustchg[] = "-ERR 404 password change required";
/* Answers of one script each, named to keep its row short. */
static const char k_baddate[] =
    "-ERR 102 bad or missing argument FIELD=MDATE";
static const char k_inst[] = "-ERR 202 installed in an assembly FIELD=ITEM";
static const char k_notinst[] =
    "-ERR 202 not part of an assembly FIELD=CHILD";
/* The first page of an item list as it was before another session
   deleted item 99. */
static const char k_stale[] = "+DATA\n"
    "F ITEM_ID CATEGORY MFR MODEL WORKS QTY AVAIL LOC_ID LOC_NAME STATUS\n"
    "R 1 MOBO Hercules M07-B Y 2 Y 5 \"Shelf A, bottom\" A\n"
    "R 99 CPU Logitech M99-Z ? 1 Y 6 \"Bin B1\" A\n.";

/* A command of the scripts alone: show the note a screen left pending
   (lv_note_show) and wait for a key, so that a snapshot shows whether
   one was left. */
#define TH_NOTE         TUI_CMD_MAX

/* The seed sets of the scripts that show every kind of record. */
#define SD_RECS         (SD_ASSY | SD_MAINT | SD_LOAN)

const script th_scripts[] = {
    { AD, { SCMD_ITEMS_ID },
      SF_MONO, 0, 0, NULL },                      /* 1 paging          */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 2 scroll, End     */
    { AD, { SCMD_ITEMS_ID, SCMD_ITEMS_LOC, SCMD_ITEMS_MFR, SCMD_ITEMS_SER },
      0, 0, 0, NULL },                            /* 3 sort orders     */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 4 add, picks      */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 5 edit, delete    */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 6 errors, copy    */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 7 locations       */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 8 add, refusals   */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 9 move, below, edit */
    { AD, { SCMD_FIND },
      0, 0, 0, NULL },                            /* 10 text, serial   */
    { AD, { SCMD_FILTER },
      0, 0, 0, NULL },                            /* 11 filters, F3    */
    { RD, { SCMD_ITEMS_ID },
      SF_RO | SF_MONO, 0, 0, NULL },              /* 12 level 1        */
    { OP, { SCMD_SERIAL },
      0, 0, 0, NULL },                            /* 13 level 2, check */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 14 notes          */
    { RD, { SCMD_LOCS },
      SF_RO, 0, 0, NULL },                        /* 15 level 1, locs  */
    { RD, { SCMD_ITEMS_ID },
      SF_RO, 0, 0, NULL },                        /* 16 level 1, notes */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 2, k_gone },                          /* 17 200, Del N     */
    { AD, { SCMD_LOCS },
      0, 0, 4, k_perm },                          /* 18 server 203     */
    { AD, { SCMD_ITEMS_LOC, SCMD_ITEMS_MFR, SCMD_ITEMS_SER },
      0, 0, 0, NULL },                            /* 19 direct entry   */
    { AD, { SCMD_ITEM_NEW, SCMD_LOC_NEW, SCMD_ITEMS },
      0, 0, 0, NULL },                            /* 20 direct entry   */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 21 partial ADD    */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 22 deep stack     */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 5, k_gone },                          /* 23 NOTES fails    */
    { AD, { SCMD_ASSY },
      SF_MONO, SD_ASSY, 0, NULL },                /* 24 assemblies, Parts */
    { AD, { SCMD_ITEMS_ID },
      0, SD_ASSY | SD_LOAN, 0, NULL },            /* 25 install, take out */
    { AD, { SCMD_ITEMS_ID },
      0, SD_ASSY, 0, NULL },                      /* 26 list F5, F7, F8 */
    { AD, { SCMD_MAINT },
      0, SD_MAINT, 0, NULL },                     /* 27 recent service */
    { AD, { SCMD_ITEMS_ID },
      0, SD_MAINT, 0, NULL },                     /* 28 Service tab, notes */
    { AD, { SCMD_LOANS },
      SF_MONO, SD_LOAN, 0, NULL },                /* 29 open, late, edit */
    { AD, { SCMD_LOANS },
      0, SD_LOAN, 0, NULL },                      /* 30 lend, return   */
    { AD, { SCMD_ITEMS_ID },
      0, SD_LOAN, 0, NULL },                      /* 31 Loans tab, held */
    { RD, { SCMD_ASSY, SCMD_MAINT, SCMD_LOANS, SCMD_ITEMS_ID },
      SF_RO, SD_RECS, 0, NULL },                  /* 32 level 1        */
    { AD, { SCMD_MAINT },
      0, SD_ASSY | SD_MAINT, 0, NULL },           /* 33 deepest: service */
    { AD, { SCMD_LOCS },
      0, SD_LOAN, 0, NULL },                      /* 34 deepest: loans */
    { AD, { SCMD_LOANS },
      0, SD_LOAN, 2, k_gone_id },                 /* 35 RETURN fails   */
    { AD, { SCMD_MAINT },
      0, SD_MAINT, 2, k_gone_id },                /* 36 GET fails      */
    { AD, { SCMD_ASSY, SCMD_MAINT, SCMD_LOANS, SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 37 empty lists, tabs */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 38 save first     */
    { AD, { SCMD_ITEMS_ID },
      0, SD_RECS, 0, NULL },                      /* 39 help of the tabs */
    { AD, { SCMD_ITEMS_ID },
      0, SD_MAINT, 0, NULL },                     /* 40 deepest: memo  */
    { AD, { SCMD_ASSY },
      0, SD_BOM, 0, NULL },                       /* 41 BOM paging     */
    { AD, { SCMD_LOCS, SCMD_ITEMS_ID },
      SF_HOME, 0, 0, NULL },                      /* 42 no memory      */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 43 copy, tabs     */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 44 copy, save     */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 3, k_gone },                          /* 45 copy, no GET   */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 46 copy, parts    */
    { AD, { SCMD_LOANS },
      0, SD_LOAN, 5, k_perm },                    /* 47 OVERDUE fails  */
    { AD, { SCMD_ITEMS_ID, SCMD_ITEMS_ID },
      0, 0, 2, k_expired },                       /* 48 pick list: login */
    { AD, { SCMD_FILTER },
      0, 0, 1, "-ERR 402 server busy" },          /* 49 pick list: busy */
    { AD, { SCMD_MAINT },
      0, SD_MAINT, 4, k_baddate },                /* 50 save: field    */
    { AD, { SCMD_MAINT },
      0, SD_MAINT, 4, k_gone },                   /* 51 save: 200      */
    { AD, { SCMD_MAINT, SCMD_LOCS },
      0, SD_MAINT, 4, k_expired },                /* 52 notes: login   */
    { AD, { SCMD_ITEM_NEW, SCMD_LOC_NEW, SCMD_LOC_NEW },
      SF_HOME, 0, 0, NULL },                      /* 53 saved from menu */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 1, k_stale },                         /* 54 F5: gone       */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 1, k_stale },                         /* 55 F7: gone       */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 4, "-ERR 300 database I/O error" },   /* 56 NOTES count: 300 */
    { AD, { SCMD_LOCS, SCMD_ITEMS_ID },
      0, SD_ASSY, 7, k_stale },                   /* 57 Del: refused, gone */
    { AD, { SCMD_ITEMS_ID, SCMD_LOCS },
      0, 0, 3, k_mustchg },                       /* 58 item list: 404 */
    { AD, { SCMD_ITEMS_ID, SCMD_ITEMS_ID, SCMD_ITEMS_ID, TH_NOTE },
      0, 0, 6, k_mustchg },                       /* 59 memo, copy: 404 */
    { AD, { SCMD_ITEMS_ID, SCMD_LOCS },
      0, 0, 2, NULL },                            /* 60 session closed */
    { AD, { SCMD_LOANS },
      0, SD_LOAN, 4, k_inst },                    /* 61 lend: 202 ITEM */
    { AD, { SCMD_ITEMS_ID },
      0, SD_ASSY, 11, k_notinst },                /* 62 F8: 202 CHILD  */
    { AD, { SCMD_ITEMS_ID, SCMD_LOCS },
      0, 0, 5, k_expired },                       /* 63 save: login    */
    { AD, { SCMD_ITEMS_ID, SCMD_LOCS },
      0, 0, 6, "-ERR 501 server shutting down" },  /* 64 memo save: 501 */
    { AD, { SCMD_CATS },
      0, 0, 0, NULL },                            /* 65 categories     */
    { RD, { SCMD_LTYPES, SCMD_FILTER },
      SF_RO, 0, 0, NULL },                        /* 66 types, filter  */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 67 tree keys      */
    { AD, { SCMD_LOCS },
      0, 0, 0, NULL },                            /* 68 new in contents */
    { AD, { SCMD_LTYPES },
      0, 0, 0, NULL },                            /* 69 type order     */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 0, NULL },                            /* 70 F5 no-op       */
    { AD, { SCMD_LOANS },
      0, SD_LOAN | SD_GONE, 0, NULL },            /* 71 reserve, use   */
    { AD, { SCMD_ITEMS_ID },
      0, SD_LOAN, 0, NULL },                      /* 72 host, dispose  */
    { AD, { SCMD_ITEMS_ID },
      0, 0, 5, k_gone },                          /* 73 tab: 200, reload */
    { AD, { SCMD_LOANS },
      0, 0, 2, k_perm }                           /* 74 pick no mem, 203 */
};
const u16 th_nscripts = (u16)(sizeof th_scripts / sizeof th_scripts[0]);

/* SCRE42's first far allocation is the location tree's list of type
   names, so that screen cannot open; its fifth is that of the location
   pick list of the item form.  SCRE74's fifth is the rows of the item
   pick list of a new loan's prompt. */
const memfault th_memfaults[] = {
    { "SCRE42", { 1, 5 } },
    { "SCRE74", { 5, 0 } },
    { NULL, { 0, 0 } }
};

u8 th_run(u8 cmd)
{
    if (cmd >= SCMD_ITEMS && cmd <= SCMD_ITEM_NEW)
        return scr_items(cmd);
    if (cmd == SCMD_LOCS || cmd == SCMD_LOC_NEW)
        return scr_locs(cmd);
    if (cmd == SCMD_CATS || cmd == SCMD_LTYPES)
        return scr_codes(cmd);
    if (cmd >= SCMD_FIND && cmd <= SCMD_FILTER)
        return scr_search(cmd);
    if (cmd == SCMD_ASSY)
        return scr_assy(cmd);
    if (cmd == SCMD_MAINT)
        return scr_maint(cmd);
    if (cmd == SCMD_LOANS)
        return scr_loans(cmd);
    if (cmd == TH_NOTE) {
        lv_note_show();
        tui_key();
    }
    return TUI_CMD_NONE;
}

/* The requests a script sends after the login, in all: the item list
   paged, the location tree with a location's contents (and whether it
   holds others), the assemblies with their parts (each item form also
   asking for its location's path), the recent service records, the
   open loans, and a part's refused removal with the parts asked for
   again. */
typedef struct {
    const char *name;
    u16  n;
} reqcount;

static const reqcount reqcounts[] = {
    { "SCRE1", 4 },
    { "SCRE7", 11 },
    { "SCRE24", 27 },
    { "SCRE27", 21 },
    { "SCRE29", 11 },
    { "SCRE62", 13 },
    { "SCRE70", 5 },
    { "SCRE73", 9 },
    { NULL, 0 }
};

/* These screens read no settings beyond the item order, and the scripts
   need no seed of their own and nothing set up around them. */
void th_setup(void)
{
}

void th_check_tables(void)
{
    u16 i;

    for (i = 0; reqcounts[i].name != NULL; i++)
        th_known(reqcounts[i].name);
}

int th_seed(void)
{
    return 1;
}

int th_begin(void)
{
    return 0;
}

void th_login(void)
{
}

void th_end(void)
{
    u16 i;

    for (i = 0; reqcounts[i].name != NULL; i++)
        if (strcmp(reqcounts[i].name, th_curname) == 0)
            TF_EQ(tl_count(), reqcounts[i].n);
}
