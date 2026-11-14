/*
 * T_CSV3.C - the third part of the T_CSV test (see T_CSV.C): LOCS files
 * whose lines come in any order, the reads that costs, and an export of
 * a tree imported back.  It is a file of its own only to stay within
 * the compiler memory of Watcom C 6.5; T_CSV.H holds what the parts
 * share.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGLOCS.H"
#include "ENGCSV.H"
#define TF_PART
#define TF_FILE "T_CSV3"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TENGH.H"
#include "TEWIPE.H"
#include "T_CSV.H"

#define TRIPLOCS        5       /* locations of test_trip's export      */

static u16 g_reads;              /* reads seen by count_read             */

/* The read hook of import_reads: count, fail nothing. */
static int count_read(int op)
{
    if (op == CF_OP_READ)
        g_reads++;
    return 0;
}

/* The reads (cf_read, cf_readblk) of an import of IN.CSV into LOCS,
   which must add n lines. */
static u16 import_reads(u32 n)
{
    g_reads = 0;
    cf_fault_rd = count_read;
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    cf_fault_rd = NULL;
    TF_EQ(g_errors, 0);
    TF_EQ(g_added, n);
    return g_reads;
}

/* Append to IN.CSV a chain of ENG_MAXLOCDEPTH locations named name and
   their level, each line a window long, so that each is one read of
   the file: parents first, or with reverse 1 children first. */
static void chain_file(const char *name, int reverse)
{
    char head[TE_TEXTMAX];
    u16 i;
    u16 k;

    put_file("IN.CSV", "", 0);
    for (k = 1; k <= ENG_MAXLOCDEPTH; k++) {
        i = reverse ? (u16)(ENG_MAXLOCDEPTH + 1 - k) : k;
        sprintf(head, "%u,%u,%s%u,,BOX,", (unsigned)i, (unsigned)(i - 1),
                name, (unsigned)i);
        pad_out(head, ENG_CSVLINE, "1", "\r\n");
    }
}

/* A chain listed parents first is taken in one sweep of each pass,
   children first in one per level, which here about doubles the reads
   of the import: the first must read at most three quarters as much as
   the second. */
void test_cost(void)
{
    u32 fwd;

    chain_file("Fwd", 0);
    fwd = import_reads(ENG_MAXLOCDEPTH);
    chain_file("Rev", 1);
    TF_CHECK(4 * fwd < 3 * (u32)import_reads(ENG_MAXLOCDEPTH));
}

/* Lines in any order: chains of three in reverse order, by LOC_ID and
   by code; loops of lines; the child of a refused line; a chain of
   ENG_MAXLOCDEPTH + 1 in reverse order, with a line below it. */
void test_order(void)
{
    char line[TE_TEXTMAX];
    u16 i;

    accepted(ENG_T_LOCS, 0, "3,2,C3,RC3,BOX\r\n2,1,C2,,BOX\r\n1,,C1,,BOX\r\n",
             0, 3);
    TF_STREQ(lpath("RC3"), "C1 > C2 > C3");
    accepted(ENG_T_LOCS, 0, ",RB2,RB3,RB3,BOX\r\n,RB1,RB2,RB2,BOX\r\n"
             ",,RB1,RB1,BOX\r\n", 0, 3);
    TF_STREQ(lpath("RB3"), "RB1 > RB2 > RB3");
    put_file("IN.CSV", "1,2,LA,,BOX\r\n2,1,LB,,BOX\r\n5,5,LS,,BOX\r\n", 0);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    if (TF_EQ(g_errors, 3))
        for (i = 0; i < 3; i++)
            import_err(i, i + 1, "PARENT_ID",
                       "cannot be put inside itself or its contents");
    /* The child of a line refused for its name. */
    put_file("IN.CSV", "1,,Twin2,,ROOM\r\n2,,TWIN2,,ROOM\r\n3,2,Kid,,BIN\r\n",
             0);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    if (TF_EQ(g_errors, 2)) {
        import_err(0, 2, "LOC_NAME", NULL);
        import_err(1, 3, "PARENT_ID", "unknown location");
    }
    put_file("IN.CSV", "", 0);
    for (i = ENG_MAXLOCDEPTH + 2; i > 0; i--) {
        sprintf(line, "%u,%u,N%u,,BOX\r\n", (unsigned)(i + 100),
                (unsigned)(i == 1 ? 0 : i + 99), (unsigned)i);
        add_file("IN.CSV", line, 0);
    }
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    if (TF_EQ(g_errors, 2)) {
        import_err(0, 2, "PARENT_ID",
                   "locations nested more than 8 levels deep");
        import_err(1, 1, "PARENT_ID", "unknown location");
    }
}

/* The locations of test_trip, by code: their paths after the import
   must be those of the export. */
static const char *const k_trip[TRIPLOCS] = {
    "SH", "B1", "RM", "B2", "HALL"
};

/* Import IN.CSV, with its header, into an emptied data directory that
   holds first the live top-level locations O1 .. On, then compare the
   path of each k_trip location with want. */
static void trip_into(const eng_time *now, u16 n,
                      char want[][ENG_LOCPATHLEN + 1])
{
    char code[8];
    u16 i;

    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&g_e, now), ENG_OK))
        return;
    for (i = 1; i <= n; i++) {
        sprintf(code, "O%u", (unsigned)i);
        te_loc(code, 0);
    }
    if (TF_EQ(import(ENG_T_LOCS, ENG_CSV_HEADER, &g_errors, &g_added),
              ENG_OK) && TF_EQ(g_errors, 0) && TF_EQ(g_added, TRIPLOCS))
        for (i = 0; i < TRIPLOCS; i++)
            TF_STREQ(lpath(k_trip[i]), want[i]);
    TF_EQ(eng_close(g_e), ENG_OK);
}

/* An export of a tree of three levels, one location moved under a newer
   one and one deleted, imports back with every path into an empty data
   directory and into one whose live LOC_IDs are those of the file. */
void test_trip(const eng_time *now)
{
    char want[TRIPLOCS][ENG_LOCPATHLEN + 1];
    char cursor[ENG_CURSORMAX];
    char code[8];
    u32 hall;
    u32 rows;
    u16 n;
    u16 i;
    int calls;

    te_remove(tfs_dir(), NULL);
    if (!TF_EQ(te_open(&g_e, now), ENG_OK))
        return;
    te_loc("GONE", 0);
    te_loc("B1", te_loc("SH", 0));
    te_loc("RM", 0);
    te_loc("B2", 2);
    hall = te_loc("HALL", 0);
    TF_EQ(eng_loc_del(g_e, "GONE"), ENG_OK);
    TF_EQ(eng_loc_set(g_e, "SH", te_p, te_kv("PARENT_ID=RM")), ENG_OK);
    for (i = 0; i < TRIPLOCS; i++)
        strcpy(want[i], lpath(k_trip[i]));
    TF_STREQ(want[1], "RM > SH > B1");
    TF_EQ(export_all(ENG_T_LOCS, "Y", ENG_CSV_REPLACE, 0, "", &calls, &rows),
          ENG_OK);
    TF_CHECK(export_ok(ENG_T_LOCS, "Y"));
    TF_EQ(export_all(ENG_T_LOCS, "IN.CSV", ENG_CSV_HEADER | ENG_CSV_REPLACE,
                     0, "", &calls, &rows), ENG_OK);
    TF_EQ(rows, TRIPLOCS);
    /* An export of two requests with a rename between them that sorts
       the last location first: no row lost or repeated. */
    for (i = 0; i < ENG_EXPROWS; i++) {
        sprintf(code, "H%u", (unsigned)i);
        te_loc(code, hall);
    }
    TF_EQ(eng_export(g_e, ENG_T_LOCS, tfs_path("Y", ""), ENG_CSV_REPLACE, 0,
                     NULL, &n, cursor), ENG_OK);
    TF_EQ(n, ENG_EXPROWS - 1);
    sprintf(code, "H%u", (unsigned)(ENG_EXPROWS - 1));
    TF_EQ(eng_loc_set(g_e, code, te_p, te_kv("LOC_NAME=0H")), ENG_OK);
    TF_EQ(export_all(ENG_T_LOCS, "Y", ENG_CSV_REPLACE, 0, cursor, &calls,
                     &rows), ENG_OK);
    TF_EQ(rows, TRIPLOCS + 1);
    TF_CHECK(export_ok(ENG_T_LOCS, "Y"));
    TF_EQ(eng_close(g_e), ENG_OK);
    trip_into(now, 0, want);
    trip_into(now, TRIPLOCS + 1, want);
}
