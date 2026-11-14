/*
 * T_CSV2.C - the second half of the T_CSV test (see T_CSV.C): the
 * concurrent exports, the import phases and their helpers.  A LOCS line
 * without a header has the columns LOC_ID, PARENT_ID, LOC_NAME,
 * LOC_CODE, LTYPE and CAPACITY.  It is a file of its own only to stay
 * within the compiler memory of Watcom C 6.5; T_CSV.H holds what the
 * parts share.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGTX.H"
#include "ENGITEM.H"
#include "ENGLOCS.H"
#include "ENGCSV.H"
#define TF_PART
#define TF_FILE "T_CSV2"
/* Checks keep no file name or expression text, which would take the
   test past its near data limit under Open Watcom. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#include "TESTFW.H"
#include "T_FILES.H"
#include "TENGH.H"
#include "TEWIPE.H"
#include "T_CSV.H"

#define PADMAX          64      /* spaces pad_out appends per write     */
#define MAXSTEPS        10      /* requests of one of two interleaved
                                   exports                              */
#define FAILLINES       5       /* lines of test_fail's import          */

/* The fields of an audit row as test_import reads them. */
static const char k_aud[] = "TBL REC_ID ACTION FLD NEWVAL";
/* The start of the K row of an import, before the file name. */
static const char k_krow[] = "ITEMS|0|K|IMPORT|";

/* The refusals an import reported. */
static int g_nerr;
u16 g_errors;
u32 g_added;
static u32 g_eline[ENG_IMPERRS];
static int g_ecode[ENG_IMPERRS];
static char g_efield[ENG_IMPERRS][DBF_NAMELEN + 1];
static char g_emsg[ENG_IMPERRS][ENG_TXMAX + 20];

/* The text of field name of live item id, "?" when there is none. */
static const char *ifield(u32 id, const char *name)
{
    u32 r;

    if (eng_item_get(g_e, id, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_ITEMS, TE_BUF, name);
}

/* The text of field name of location code, "?" when there is none. */
static const char *lfield(const char *code, const char *name)
{
    u32 r;

    if (eng_loc_get(g_e, code, &r) != ENG_OK)
        return "?";
    return te_rtext(ENG_T_LOCS, TE_BUF, name);
}

const char *lpath(const char *code)
{
    u32 r;
    u16 d;

    strcpy(g_line, "?");
    if (eng_loc_get(g_e, code, &r) == ENG_OK)
        eng_loc_path(g_e, (u32)te_rnum(ENG_T_LOCS, TE_BUF, "LOC_ID"),
                     g_line, &d);
    return g_line;
}

/* ---------------------------------------------------------------------
 * Exports at the same time.
 * --------------------------------------------------------------------- */

/* One request of an export of ITEMS to name with tag tag, after the
   cursor in after ("" to start), which receives the cursor to continue
   with; the code. */
static int export_step(const char *name, u8 flags, u8 tag, char *after)
{
    char cursor[ENG_CURSORMAX];
    u16 n;
    int rc;

    rc = eng_export(g_e, ENG_T_ITEMS, tfs_path(name, ""), flags, tag, after,
                    &n, cursor);
    strcpy(after, cursor);
    return rc;
}

void test_concurrent(void)
{
    char a[ENG_CURSORMAX];
    char b[ENG_CURSORMAX];
    char c[ENG_CURSORMAX];
    u32 rows;
    int calls;
    int i;

    /* Tags 1 and 2 to paths that differ only in their extension, their
       requests interleaved; a continuation with a tag that has no
       scratch file, as from another session, is refused. */
    cf_remove(tfs_path("X.CSV", ""));
    a[0] = '\0';
    b[0] = '\0';
    TF_EQ(export_step("X.CSV", 0, 1, a), ENG_OK);
    TF_EQ(export_step("X.TXT", 0, 2, b), ENG_OK);
    TF_CHECK(exists("X.&C1"));
    TF_CHECK(exists("X.&C2"));
    strcpy(c, b);
    TF_EQ(export_step("X.TXT", 0, 3, c), ENG_ECURSOR);
    TF_STREQ(eng_errmsg(g_e), "no export in progress");
    for (i = 0; i < MAXSTEPS && (a[0] != '\0' || b[0] != '\0'); i++) {
        if (a[0] != '\0')
            TF_EQ(export_step("X.CSV", 0, 1, a), ENG_OK);
        if (b[0] != '\0')
            TF_EQ(export_step("X.TXT", 0, 2, b), ENG_OK);
    }
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.TXT"));
    TF_CHECK(!exists("X.&C1"));
    TF_CHECK(!exists("X.&C2"));
    /* Two to one path: the first to end puts its file there, and the
       second's last request finds it and is refused; its scratch file
       stays until the next export with its tag replaces it. */
    cf_remove(tfs_path("X.CSV", ""));
    TF_EQ(export_step("X.CSV", 0, 1, a), ENG_OK);
    TF_EQ(export_step("X.CSV", 0, 2, b), ENG_OK);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", 0, 1, a, &calls, &rows),
          ENG_OK);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", 0, 2, b, &calls, &rows),
          ENG_EDUP);
    TF_STREQ(eng_errfield(g_e), "FILE");
    TF_STREQ(eng_errmsg(g_e), "file already exists");
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    TF_CHECK(exists("X.&C2"));
    /* With REPLACE, both end, and the second's file replaces the
       first's. */
    a[0] = '\0';
    b[0] = '\0';
    TF_EQ(export_step("X.CSV", ENG_CSV_REPLACE, 1, a), ENG_OK);
    TF_EQ(export_step("X.CSV", ENG_CSV_REPLACE, 2, b), ENG_OK);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", ENG_CSV_REPLACE, 1, a, &calls,
                     &rows), ENG_OK);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", ENG_CSV_REPLACE, 2, b, &calls,
                     &rows), ENG_OK);
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    TF_CHECK(!exists("X.&C1"));
    TF_CHECK(!exists("X.&C2"));
}

/* ---------------------------------------------------------------------
 * Import.
 * --------------------------------------------------------------------- */

static void on_err(void *arg, u32 line, const char *field, int code,
                   const char *msg)
{
    if (arg != (void *)g_eline || g_nerr >= ENG_IMPERRS) {
        g_nerr = 99;
        return;
    }
    g_eline[g_nerr] = line;
    g_ecode[g_nerr] = code;
    strcpy(g_efield[g_nerr], field);
    g_emsg[g_nerr][0] = '\0';
    strncat(g_emsg[g_nerr], msg, ENG_TXMAX + 19);
    g_nerr++;
}

int import(int t, u8 flags, u16 *errors, u32 *added)
{
    g_nerr = 0;
    return eng_import(g_e, t, tfs_path("IN.CSV", ""), flags, on_err,
                      (void *)g_eline, errors, added);
}

void import_err(u16 i, u32 line, const char *field, const char *msg)
{
    if (!TF_CHECK(i < g_nerr))
        return;
    TF_EQ(g_eline[i], line);
    TF_STREQ(g_efield[i], field);
    if (msg != NULL)
        TF_STREQ(g_emsg[i], msg);
}

/* Import text s into t: one refusal expected, on line, naming field,
   with code; nothing written. */
static void refused(int t, u8 flags, const char *s, u32 line,
                    const char *field, int code)
{
    u32 items;
    u32 locs;
    u32 audits;
    u32 added;
    u16 errors;

    items = eng_reccount(g_e, ENG_T_ITEMS);
    locs = eng_reccount(g_e, ENG_T_LOCS);
    audits = eng_reccount(g_e, ENG_T_AUDIT);
    put_file("IN.CSV", s, 0);
    TF_EQ(import(t, flags, &errors, &added), ENG_OK);
    if (!TF_EQ(errors, 1) || !TF_EQ(g_nerr, 1) ||
        !TF_EQ(g_eline[0], line) || !TF_STREQ(g_efield[0], field) ||
        !TF_EQ(g_ecode[0], code))
        printf("  import \"%s\"\n", s);
    TF_EQ(added, 0);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), items);
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), locs);
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), audits);
    TF_CHECK(!exists(ENG_EDITMARK));
    TF_CHECK(!exists("IMPORT.&NX"));
}

void accepted(int t, u8 flags, const char *s, u16 len, u32 n)
{
    u32 added;
    u16 errors;

    put_file("IN.CSV", s, len);
    if (!TF_EQ(import(t, flags, &errors, &added), ENG_OK) ||
        !TF_EQ(errors, 0) || !TF_EQ(added, n))
        printf("  import \"%s\": %s %s\n", s, eng_errfield(g_e),
               eng_errmsg(g_e));
}

void pad_out(const char *head, u16 len, const char *value,
             const char *end)
{
    char pad[PADMAX];
    u16 n;

    memset(pad, ' ', sizeof pad);
    add_file("IN.CSV", head, 0);
    for (n = (u16)(len - strlen(head) - strlen(value)); n > PADMAX;
         n -= PADMAX)
        add_file("IN.CSV", pad, PADMAX);
    if (n > 0)
        add_file("IN.CSV", pad, n);
    add_file("IN.CSV", value, 0);
    add_file("IN.CSV", end, 0);
}

/* Append to IN.CSV the line of the bin named and coded code, its
   CAPACITY value after spaces, len characters in all, then end. */
static void pad_line(const char *code, u16 len, const char *value,
                     const char *end)
{
    char head[PADMAX];

    sprintf(head, ",,%s,%s,BIN,", code, code);
    pad_out(head, len, value, end);
}

/* Lines holding control bytes: a NUL, an ESC, a TAB. */
static const char k_ctrl[] = "CATEGORY,MFR,MODEL,QTY\r\n"
                             "SYS,IBM\0junk,5150,7\r\n"
                             "SYS,Apple,II\033e,1\r\n"
                             "SYS,\tTandy,CoCo,1\r\n";

void test_parse(void)
{
    accepted(ENG_T_LOCS, 0,
             ",,\"Shelf, top\",\"B1\",bin,12\r\n"
             " , , \"Say \"\"x\"\"\" , B2 , BIN ,\n"
             "\r\n   \n\n"
             "9,,\"b3\",b3,BIN\r\n"
             ",,  bare  value ,B4,BIN,\"\"\r\n"
             ",,B5,B5,BIN\032,,B6,B6,BIN\r\n", 0, 5);
    TF_STREQ(lfield("B1", "LOC_NAME"), "Shelf, top");
    TF_STREQ(lfield("B1", "LTYPE"), "BIN");
    TF_STREQ(lfield("B1", "CAPACITY"), "12");
    TF_STREQ(lfield("B2", "LOC_NAME"), "Say \"x\"");
    TF_STREQ(lfield("B2", "CAPACITY"), "");
    TF_STREQ(lfield("B3", "LOC_NAME"), "b3");
    TF_STREQ(lfield("B3", "LOC_ID"), "5");
    TF_STREQ(lfield("B4", "LOC_NAME"), "bare  value");
    TF_EQ(eng_loc_get(g_e, "B6", &g_added), ENG_ENOTFOUND);
    accepted(ENG_T_LOCS, 0, ",,B6,B6,BIN", 0, 1);
    /* A last line ending in a bare CR. */
    accepted(ENG_T_LOCS, 0, ",,B7,B7,box\r", 0, 1);
    TF_STREQ(lfield("B7", "LTYPE"), "BOX");
    accepted(ENG_T_LOCS, 0, "", 0, 0);
    refused(ENG_T_LOCS, 0, ",,C1,C1,BIN\r\n\"C2,x\r\n", 2, "LOC_ID",
            ENG_EARG);
    TF_STREQ(g_emsg[0], "bad quoting");
    refused(ENG_T_LOCS, 0, "\"C2\"x,y\r\n", 1, "LOC_ID", ENG_EARG);
    refused(ENG_T_LOCS, 0, ",\"d\" \"e\"\r\n", 1, "PARENT_ID", ENG_EARG);
    refused(ENG_T_LOCS, 0, ",,a,C2,BIN,1,e\r\n", 1, "LINE", ENG_EARG);
    TF_STREQ(g_emsg[0], "too many columns");
    refused(ENG_T_LOCS, 0, "\r\n,,a,C2,BIN,1,\r\n", 2, "LINE", ENG_EARG);
    /* A byte below 20h but TAB refuses its line, whose values a NUL
       would cut short; a TAB is left to the checks of its field. */
    put_file("IN.CSV", k_ctrl, (u16)(sizeof k_ctrl - 1));
    TF_EQ(import(ENG_T_ITEMS, ENG_CSV_HEADER, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 3);
    TF_EQ(g_added, 0);
    import_err(0, 2, "LINE", "bad character");
    import_err(1, 3, "LINE", "bad character");
    import_err(2, 4, "MFR", "bad character");
    TF_EQ(g_ecode[0], ENG_EARG);
    /* Lines of ENG_CSVLINE characters with either end, and longer. */
    put_file("IN.CSV", "", 0);
    pad_line("D1", ENG_CSVLINE, "1", "\r\n");
    pad_line("D2", ENG_CSVLINE, "2", "\n");
    pad_line("D3", ENG_CSVLINE + 1, "3", "\r\n");
    add_file("IN.CSV", ",,D4,D4,BIN,4\r\n", 0);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 1);
    TF_EQ(g_eline[0], 3);
    TF_STREQ(g_efield[0], "LINE");
    TF_STREQ(g_emsg[0], "line too long");
    /* The same without the long line: three added. */
    put_file("IN.CSV", "", 0);
    pad_line("D1", ENG_CSVLINE, "1", "\r\n");
    pad_line("D2", ENG_CSVLINE, "2", "\n");
    add_file("IN.CSV", ",,D4,D4,BIN,4", 0);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 0);
    TF_EQ(g_added, 3);
    TF_STREQ(lfield("D1", "CAPACITY"), "1");
    TF_STREQ(lfield("D2", "CAPACITY"), "2");
    TF_STREQ(lfield("D4", "CAPACITY"), "4");
}

void test_header(void)
{
    u32 r;

    accepted(ENG_T_LOCS, ENG_CSV_HEADER,
             "\r\nltype,Loc_Code,loc_name\r\nbin,E1,Z9\r\n"
             "\"Box\",\"E2\",Z8\r\n", 0, 2);
    TF_STREQ(lfield("E1", "LOC_NAME"), "Z9");
    TF_STREQ(lfield("E2", "LTYPE"), "BOX");
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "LOC_CODE,FOO\r\nE3,x\r\n", 1,
            "FOO", ENG_EARG);
    TF_STREQ(g_emsg[0], "unknown column");
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "LOC_CODE,NOTES\r\nE3,x\r\n", 1,
            "NOTES", ENG_EARG);
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "LOC_CODE,A B\r\nE3,x\r\n", 1,
            "LINE", ENG_EARG);
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "LOC_CODE,,LTYPE\r\nE3\r\n", 1,
            "LINE", ENG_EARG);
    refused(ENG_T_LOCS, ENG_CSV_HEADER,
            "\n\nltype,loc_code,LTYPE\r\nE3\r\n", 3, "LTYPE", ENG_EARG);
    TF_STREQ(g_emsg[0], "repeated column");
    /* Header errors stop the check: no line after it is counted. */
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "BAD\r\n\"E3\r\n\"E4\r\n", 1,
            "BAD", ENG_EARG);
    /* Without LOC_NAME every line lacks its name. */
    refused(ENG_T_LOCS, ENG_CSV_HEADER, "LTYPE\r\nBIN\r\n", 2, "LOC_NAME",
            ENG_EARG);
    /* Duplicates in the file and against the table. */
    refused(ENG_T_LOCS, 0, ",,F1,F1,BIN\r\n,,F2,F2,BIN\r\n,,F3,f1,BIN\r\n",
            3, "LOC_CODE", ENG_EDUP);
    TF_STREQ(g_emsg[0], "duplicate in file");
    refused(ENG_T_LOCS, 0, ",,F1,F1,BIN\r\n,,F2,A1,BIN\r\n", 2, "LOC_CODE",
            ENG_EDUP);
    TF_STREQ(g_emsg[0], "location code already used");
    refused(ENG_T_LOCS, 0, ",,Same,,BIN\r\n,,SAME,,BIN\r\n", 2,
            "LOC_NAME", ENG_EDUP);
    TF_STREQ(g_emsg[0], "duplicate in file");
    refused(ENG_T_LOCS, 0, ",,a1,,BIN\r\n", 1, "LOC_NAME", ENG_EDUP);
    TF_STREQ(g_emsg[0], "name already used at this level");
    /* ITEMS by header, defaults applied, read-only columns ignored. */
    r = eng_reccount(g_e, ENG_T_ITEMS);
    accepted(ENG_T_ITEMS, ENG_CSV_HEADER,
             "category,MFR,QTY,ITEM_ID,ON_LOAN,CREATED,WORKS,TAGS\r\n"
             "sys,Apple,3,7,2,19990101,,\" a,b  A\"\r\n", 0, 1);
    TF_STREQ(ifield(r + 1, "MFR"), "Apple");
    TF_STREQ(ifield(r + 1, "CATEGORY"), "SYS");
    TF_STREQ(ifield(r + 1, "QTY"), "3");
    TF_STREQ(ifield(r + 1, "ON_LOAN"), "0");
    TF_STREQ(ifield(r + 1, "COND"), "");
    TF_STREQ(ifield(r + 1, "WORKS"), "?");
    TF_STREQ(ifield(r + 1, "TAGS"), "a,b");
    TF_STREQ(ifield(r + 1, "STATUS"), "A");
    TF_STREQ(ifield(r + 1, "CREATED"), "20261008");
}

void test_import(void)
{
    static const char badcat[] = "1,ZZZ\r\n";
    u32 items;
    u32 audits;
    u32 id;
    u16 i;

    /* An export imported back: positional columns, read-only ignored. */
    items = eng_reccount(g_e, ENG_T_ITEMS);
    audits = eng_reccount(g_e, ENG_T_AUDIT);
    put_file("IN.CSV", row1, 0);
    add_file("IN.CSV", row2, 0);
    add_file("IN.CSV", row4, 0);
    add_file("IN.CSV", "\032", 0);
    TF_EQ(import(ENG_T_ITEMS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 0);
    TF_EQ(g_added, 3);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), items + 3);
    id = items + 2;
    TF_STREQ(ifield(id, "MFR"), "  Lead");
    TF_STREQ(ifield(id, "MODEL"), "Say \"hi\"");
    TF_STREQ(ifield(id, "DESCR"), "Caf\202");
    TF_STREQ(ifield(id, "QTY"), "3");
    TF_STREQ(ifield(id, "LOC_ID"), "1");
    TF_STREQ(ifield(id, "ACQ_DATE"), "19840101");
    TF_STREQ(ifield(id, "ACQ_PRICE"), "12.50");
    TF_STREQ(ifield(id, "ON_LOAN"), "0");
    TF_STREQ(ifield(id, "COND"), "GD");
    TF_STREQ(ifield(id, "WORKS"), "I");
    TF_STREQ(ifield(id, "TAGS"), "Retro,isa");
    TF_STREQ(ifield(items + 3, "DESCR"), " x");
    TF_STREQ(ifield(items + 1, "ACQ_PRICE"), "");
    /* One K row first, then one I row per record. */
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), audits + 4);
    strcpy(g_line, k_krow);
    strcat(g_line, tfs_path("IN.CSV", ""));
    g_line[sizeof k_krow - 1 + ENG_AUDVAL] = '\0';
    TF_STREQ(te_aud(audits + 1, k_aud), g_line);
    sprintf(g_line, "ITEMS|%lu|I|ITEM_ID|%lu", (unsigned long)items + 1,
            (unsigned long)items + 1);
    TF_STREQ(te_aud(audits + 2, k_aud), g_line);
    TF_CHECK(!exists(ENG_EDITMARK));
    /* Refusals: line numbers, fields, codes, more than are reported. */
    put_file("IN.CSV", "1,SYS\r\n\r\n1,ZZZ\r\n1,\"SYS\r\n", 0);
    add_file("IN.CSV", "1,SYS,,,,,,,,,,,,,,,,,,,,,,,\r\n,SYS,,,,,,BAD\r\n", 0);
    add_file("IN.CSV", ",SYS,,,,,,,,-1\r\n,,X\r\n,SYS,,,,,,,,,,,,,Q9\r\n", 0);
    add_file("IN.CSV", ",SYS,,,,,,,X\r\n,SYS,,,,,,,,,,,,,,,,,,,a*b\r\n", 0);
    TF_EQ(import(ENG_T_ITEMS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 9);
    TF_EQ(g_nerr, 9);
    TF_EQ(g_added, 0);
    TF_EQ(g_eline[0], 3);
    TF_STREQ(g_efield[0], "CATEGORY");
    TF_EQ(g_ecode[0], ENG_ECONSTR);
    TF_STREQ(g_emsg[0], "unknown category");
    TF_EQ(g_eline[1], 4);
    TF_STREQ(g_efield[1], "CATEGORY");
    TF_STREQ(g_emsg[1], "bad quoting");
    TF_EQ(g_eline[2], 5);
    TF_STREQ(g_efield[2], "LINE");
    TF_EQ(g_eline[3], 6);
    TF_STREQ(g_efield[3], "COND");
    TF_EQ(g_eline[4], 7);
    TF_STREQ(g_efield[4], "QTY");
    TF_EQ(g_eline[5], 8);
    TF_STREQ(g_efield[5], "CATEGORY");
    TF_EQ(g_ecode[5], ENG_EARG);
    TF_EQ(g_eline[6], 9);
    TF_STREQ(g_efield[6], "LOC_ID");
    TF_EQ(g_ecode[6], ENG_ECONSTR);
    TF_EQ(g_eline[7], 10);
    TF_STREQ(g_efield[7], "WORKS");
    TF_EQ(g_ecode[7], ENG_EARG);
    TF_EQ(g_eline[8], 11);
    TF_STREQ(g_efield[8], "TAGS");
    TF_STREQ(g_emsg[8], "each tag 1-12 of A-Z a-z 0-9 - _ . / +");
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), items + 3);
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), audits + 4);
    put_file("IN.CSV", "", 0);
    for (i = 0; i < ENG_IMPERRS + 2; i++)
        add_file("IN.CSV", badcat, 0);
    TF_EQ(import(ENG_T_ITEMS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, ENG_IMPERRS + 2);
    TF_EQ(g_nerr, ENG_IMPERRS);
    TF_EQ(g_eline[ENG_IMPERRS - 1], ENG_IMPERRS);
    /* Tables, a missing file, an empty file. */
    TF_EQ(import(ENG_T_CATS, 0, &g_errors, &g_added), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "TABLE");
    TF_EQ(eng_import(g_e, ENG_T_ITEMS, tfs_path("X.DBF", ""), 0, on_err,
                     (void *)g_eline, &g_errors, &g_added), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "FILE");
    cf_remove(tfs_path("IN.CSV", ""));
    TF_EQ(import(ENG_T_ITEMS, 0, &g_errors, &g_added), ENG_ENOTFOUND);
    TF_STREQ(eng_errfield(g_e), "FILE");
    accepted(ENG_T_ITEMS, ENG_CSV_HEADER, "", 0, 0);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), items + 3);
}

/* Where the data drive is small: more lines than its free space allows
   by the estimate are refused before anything is written. */
void test_room(void)
{
    char line[24];
    const char *dir;
    u32 avail;
    u32 n;
    u32 i;
    u32 items;

    dir = tfs_dir();
    if (dos_disk_free(dir[0] != '\0' && dir[1] == ':' ?
                      (u8)((dir[0] & 0x5F) - 'A' + 1) : 0, &avail) != 0 ||
        avail > 512UL * 1024UL) {
        printf("T_CSV: disk-space refusal not checked (free %lu)\n",
               (unsigned long)avail);
        return;
    }
    n = (avail - ENG_IMPSLACK) / ENG_IMPROWCOST + 2;
    put_file("IN.CSV", "", 0);
    for (i = 0; i < n; i++) {
        sprintf(line, ",,R%05lu,,BIN\r\n", (unsigned long)i);
        add_file("IN.CSV", line, 0);
    }
    items = eng_reccount(g_e, ENG_T_LOCS);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_EFULL);
    TF_STREQ(eng_errfield(g_e), "FILE");
    TF_STREQ(eng_errmsg(g_e), "not enough disk space");
    TF_EQ(g_errors, 0);
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), items);
}

/* A tree imported in one file: parents named by the code of an earlier
   line, at any depth up to ENG_MAXLOCDEPTH, beside live ones named by ID
   or code; the names under each parent once, as the second pass would
   find them; items placed by ID or code. */
void test_tree(void)
{
    char line[TE_TEXTMAX];
    u32 r;
    u16 d;
    u16 i;

    /* Parents by code, of a live location or an earlier line, and by
       the LOC_ID an earlier line has in the file. */
    accepted(ENG_T_LOCS, 0,
             "7,,Room,RM,ROOM\r\n"
             ",rm,Shelf,SH,SHELF\r\n"
             ",SH,Bin,,BIN\r\n"
             ",7,Bin,,BIN\r\n"
             ",a1,Box,,BOX\r\n", 0, 5);
    if (TF_EQ(eng_loc_get(g_e, "SH", &r), ENG_OK)) {
        TF_EQ(eng_loc_path(g_e, (u32)te_rnum(ENG_T_LOCS, TE_BUF, "LOC_ID"),
                           NULL, &d), ENG_OK);
        TF_EQ(d, 2);
    }
    /* A parent line may come after its child; two lines with one name
       under one parent, given by code or ID or not yet written; past the
       depth limit through other lines. */
    accepted(ENG_T_LOCS, 0, ",X1,Early,XE,BIN\r\n,,X,X1,BIN\r\n", 0, 2);
    TF_STREQ(lpath("XE"), "X > Early");
    sprintf(line, "PARENT_ID,LOC_NAME,LTYPE\r\nA1,Kid,BIN\r\n%s,KID,BIN\r\n",
            lfield("A1", "LOC_ID"));
    refused(ENG_T_LOCS, ENG_CSV_HEADER, line, 3, "LOC_NAME", ENG_EDUP);
    /* One name under two live parents, by ID and by code, and at the
       top level; in a file with a LOC_ID column a number names no live
       location. */
    sprintf(line, "PARENT_ID,LOC_NAME,LTYPE\r\n%s,Twin,BIN\r\nRM,Twin,BIN\r\n"
            ",Twin,BIN\r\n", lfield("A1", "LOC_ID"));
    accepted(ENG_T_LOCS, ENG_CSV_HEADER, line, 0, 3);
    sprintf(line, ",%s,Kid,,BIN\r\n", lfield("A1", "LOC_ID"));
    refused(ENG_T_LOCS, 0, line, 1, "PARENT_ID", ENG_ECONSTR);
    TF_STREQ(g_emsg[0], "unknown location");
    refused(ENG_T_LOCS, 0,
            ",,R2,R2,ROOM\r\n,R2,Dup,,BIN\r\n,r2,dup,,BIN\r\n", 3,
            "LOC_NAME", ENG_EDUP);
    /* A code with blanks around it is no line's; a code twice in the
       file is refused whatever becomes of the lines (here the first is
       refused for its name); a name under a line of the file is not one
       of the top level. */
    refused(ENG_T_LOCS, 0, ",,Qroom,QR,ROOM\r\n,\"QR \",Qshelf,,SHELF\r\n", 2,
            "PARENT_ID", ENG_ECONSTR);
    put_file("IN.CSV", ",,Dup,K1,BIN\r\n,,DUP,K2,BIN\r\n,,Other,K2,BIN\r\n",
             0);
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    if (TF_EQ(g_errors, 2)) {
        TF_EQ(g_eline[0], 3);
        TF_STREQ(g_efield[0], "LOC_CODE");
        TF_EQ(g_eline[1], 2);
        TF_STREQ(g_efield[1], "LOC_NAME");
    }
    accepted(ENG_T_LOCS, 0, ",,Room9,R9,ROOM\r\n,R9,Room,,BIN\r\n", 0, 2);
    put_file("IN.CSV", "", 0);
    for (i = 1; i <= ENG_MAXLOCDEPTH + 1; i++) {
        if (i == 1)
            sprintf(line, ",,L1,L1,BOX\r\n");
        else
            sprintf(line, ",L%u,L%u,L%u,BOX\r\n", (unsigned)(i - 1),
                    (unsigned)i, (unsigned)i);
        add_file("IN.CSV", line, 0);
    }
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 1);
    TF_EQ(g_eline[0], ENG_MAXLOCDEPTH + 1);
    TF_STREQ(g_efield[0], "PARENT_ID");
    TF_STREQ(g_emsg[0], "locations nested more than 8 levels deep");
    /* The same through the LOC_IDs of the file. */
    put_file("IN.CSV", "", 0);
    for (i = 1; i <= ENG_MAXLOCDEPTH + 1; i++) {
        sprintf(line, "%u,%u,M%u,,BOX\r\n", (unsigned)(i + 90),
                (unsigned)(i == 1 ? 0 : i + 89), (unsigned)i);
        add_file("IN.CSV", line, 0);
    }
    TF_EQ(import(ENG_T_LOCS, 0, &g_errors, &g_added), ENG_OK);
    TF_EQ(g_errors, 1);
    TF_EQ(g_eline[0], ENG_MAXLOCDEPTH + 1);
    TF_STREQ(g_emsg[0], "locations nested more than 8 levels deep");
    /* A LOC_ID twice in the file; one named before its line. */
    refused(ENG_T_LOCS, 0, "97,,Lab1,,ROOM\r\n097,,Lab2,,ROOM\r\n", 2,
            "LOC_ID", ENG_EDUP);
    accepted(ENG_T_LOCS, 0, "98,99,Lab3,L3,ROOM\r\n99,,Lab4,,ROOM\r\n", 0, 2);
    TF_STREQ(lpath("L3"), "Lab4 > Lab3");
    /* A LOC_ID must be an ID: not 0, not above ENG_MAXID (no cut to its
       last digits), not a code; a PARENT_ID of 0 is the top level. */
    refused(ENG_T_LOCS, 0, "0,,Az,,ROOM\r\n,0,Bz,,ROOM\r\n", 1, "LOC_ID",
            ENG_EARG);
    TF_STREQ(g_emsg[0], "bad value");
    refused(ENG_T_LOCS, 0, "123456789,,Ay,,ROOM\r\n23456789,,By,,ROOM\r\n",
            1, "LOC_ID", ENG_EARG);
    refused(ENG_T_LOCS, 0, "23456789,,By,,ROOM\r\n,123456789,Cy,,ROOM\r\n",
            2, "PARENT_ID", ENG_ECONSTR);
    refused(ENG_T_LOCS, 0, "X1,,Ax,,ROOM\r\n", 1, "LOC_ID", ENG_EARG);
    accepted(ENG_T_LOCS, 0, ",0,Bz,,ROOM\r\n", 0, 1);

    /* Items by a location's ID or code; one that names none. */
    accepted(ENG_T_ITEMS, ENG_CSV_HEADER,
             "category,LOC_ID\r\nsys,RM\r\nsys,1\r\nsys,0\r\n", 0, 3);
    refused(ENG_T_ITEMS, ENG_CSV_HEADER, "category,loc_id\r\nsys,ZZ\r\n", 2,
            "LOC_ID", ENG_ECONSTR);
}

/* A disk-changing call that fails in the second pass: the one half way
   through the calls an undisturbed import makes. */
void test_fail(void)
{
    static const char five[] = ",SYS,,Five\r\n";
    u32 total;
    u16 i;
    int rc;

    put_file("IN.CSV", "", 0);
    for (i = 0; i < FAILLINES; i++)
        add_file("IN.CSV", five, 0);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    rc = import(ENG_T_ITEMS, 0, &g_errors, &g_added);
    tfs_fault_off();
    total = tfs_calls();
    TF_EQ(rc, ENG_OK);
    TF_EQ(g_added, FAILLINES);
    tfs_fault(TFS_ALLOPS, total / 2, 1, CF_EFULL);
    rc = import(ENG_T_ITEMS, 0, &g_errors, &g_added);
    tfs_fault_off();
    TF_CHECK(rc >= ENG_EIO && rc <= ENG_EINUSE);
    TF_CHECK(g_added > 0 && g_added < FAILLINES);
    TF_CHECK(exists(ENG_EDITMARK));
}
