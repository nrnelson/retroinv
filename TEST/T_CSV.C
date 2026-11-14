/*
 * T_CSV.C - unit test for CSV export and import (CSV.C), run through a
 * complete engine.  This file holds the file writers, the export phases
 * and main; the import phases are in T_CSV2.C and T_CSV3.C, as Watcom
 * C 6.5 runs out of compiler memory on one file (T_CSV.H).
 *
 * usage: T_CSV [dir]
 *
 * dir (default the current directory) is a directory, or a prefix ending
 * in '\', '/' or ':', that holds the scratch data directory (tfs_set_dir
 * of T_FILES.H); every engine file and every file of the test there is
 * removed before the run and at the end.  The clock is fixed at 8
 * October 2026.  Checks:
 *   - table names and paths: the exportable and importable tables,
 *     USERS and SEQ refused; path length, refused bytes, a directory
 *     name, the refused extensions also where DOS cuts a longer one or
 *     a longer base name, more than one dot, the DOS device names;
 *   - export: an ITEMS file compared byte for byte with the expected
 *     text (blank N and D written empty, an empty C as "", leading
 *     spaces kept, a '"' doubled, a code page 437 byte unchanged, a
 *     stored control byte written as a space, a deleted record left
 *     out, CR LF and the final Ctrl-Z), with and without the field-name
 *     line; a LOANS file likewise, a loan and a reservation for a host
 *     with their KIND and HOST_ID; every other exportable table against
 *     the same rules applied
 *     to eng_rec_text; paging over more than two pages of ENG_EXPROWS
 *     records with deleted ones at the page edges, the cursor, the
 *     scratch file and its rename; an existing file without and with
 *     REPLACE, also one that appears before the last request, and a
 *     write that fails in a continuation, each retried with the same
 *     cursor after the scratch file was cut back; a continuation
 *     without its scratch file; a tag out of range;
 *   - exports run at the same time (T_CSV2.C): with different tags to
 *     paths that differ only in their extension, each file whole; a
 *     continuation with another tag refused; two to one path, the
 *     second to end refused (ENG_EDUP naming FILE) or, with REPLACE,
 *     its file put in place;
 *   - the checks of ADD (eng_item_check, eng_loc_check) against ITEM ADD
 *     and LOC ADD on accepted and refused pairs: the same code, field
 *     and text, and nothing written by a check;
 *   - import parsing: quotes, "" inside quotes, commas inside quotes,
 *     spaces around values, LF and CR LF, blank lines, Ctrl-Z, a last
 *     line without its end, fewer columns, too many, bad quoting, lines
 *     at and over ENG_CSVLINE, lines holding a NUL or another byte below
 *     20h (a TAB left to its field's checks);
 *   - import: positional and header columns (any case and order), the
 *     read-only columns ignored, the defaults of ADD, header errors
 *     (unknown, memo, repeated column), LOCS codes, names and LOC_IDs
 *     repeated in the file or against the table, parents named by the
 *     code or LOC_ID of another line, before or after it, and the depth
 *     through them, a loop of lines and a chain too deep, a number
 *     naming a live location only in a file without a LOC_ID column, an
 *     export of a tree with a moved location imported back into an
 *     empty and a filled directory, items' locations by ID or code,
 *     each refused by the first pass as the second would refuse it, the
 *     refusals with their line, field, code and
 *     text, more than ENG_IMPERRS of them counted, nothing written when
 *     a line is refused, the K and I audit rows of a success, a missing
 *     file, an empty one, a disk too full for the estimate (only where
 *     the drive is small, as the RAM disk of the MS-DOS 3.10 run), and a
 *     write that fails in the second pass (the records added so far).
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when dir is too long.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGITEM.H"
#include "ENGLOCS.H"
#include "ENGMAINT.H"
#include "ENGLOAN.H"
#include "ENGCSV.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "TENGH.H"
#include "T_CSV.H"

#define FILEMAX     1200        /* an exactly compared file             */
#define MSGMAX      200         /* an engine error text, NUL included   */
#define MAXCALLS    100         /* requests of one export               */
#define NFIXTURE    4           /* items of the fixture                 */
#define NPAGED      120         /* items added for the paging test      */
#define NDELETED    4           /* of all these, items deleted          */

/* Files the test writes besides the engine's; te_remove also removes
   their export scratch names. */
static const char *const k_files[] = {
    "X.CSV", "X.TXT", "Y", "IN.CSV", NULL
};

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

eng *g_e;
char g_line[LINEMAX];
static char g_file[FILEMAX];
static u8 g_rd[LINEMAX];

/* ---------------------------------------------------------------------
 * Files.
 * --------------------------------------------------------------------- */

void put_file(const char *name, const char *s, u16 n)
{
    if (n == 0)
        n = (u16)strlen(s);
    TF_EQ(tfs_write_all(tfs_path(name, ""), s, n), 0);
}

void add_file(const char *name, const char *s, u16 n)
{
    u32 size;
    int rc;
    int h;

    if (n == 0)
        n = (u16)strlen(s);
    rc = cf_open(tfs_path(name, ""), CF_RDWR | CF_DENYRW, &h);
    if (rc == 0) {
        rc = cf_size(h, &size);
        if (rc == 0)
            rc = cf_seek(h, size);
        if (rc == 0)
            rc = cf_write(h, s, n);
        cf_close(h);
    }
    TF_EQ(rc, 0);
}

/* File name into g_file (at most FILEMAX bytes); its length, or -1. */
static i32 get_file(const char *name)
{
    u16 got;

    if (tfs_read_all(tfs_path(name, ""), g_file, FILEMAX, &got) != 0)
        return -1;
    return (i32)got;
}

int exists(const char *name)
{
    return tfs_exists(tfs_path(name, ""));
}

/* ---------------------------------------------------------------------
 * Export.
 * --------------------------------------------------------------------- */

/* The record in table t's buffer as the line the export must write,
   from eng_rec_text, into g_line; its length, or 0 when a field cannot
   be read, or 0 and a failed check when the line does not fit. */
static u16 expect_line(int t)
{
    char v[ENG_MAXVAL];
    const dbf_field *fd;
    u16 nf;
    u16 f;
    u16 n;
    u16 i;
    u8 c;

    nf = dbf_nfields(eng_dbf(g_e, t));
    n = 0;
    for (f = 0; f < nf; f++) {
        fd = eng_field(g_e, t, f);
        if (fd->type == 'M')
            continue;
        if (eng_rec_text(g_e, t, f, v) != ENG_OK)
            return 0;
        /* A comma, the quotes, every byte doubled, CR LF. */
        if (n + 2 * strlen(v) + 5 > LINEMAX) {
            printf("expect_line: a %s line is over LINEMAX\n",
                   eng_table_def(t)->name);
            TF_CHECK(0);
            return 0;
        }
        if (n > 0)
            g_line[n++] = ',';
        if (fd->type == 'C')
            g_line[n++] = '"';
        for (i = 0; v[i] != '\0'; i++) {
            c = (u8)v[i];
            if (c < 0x20 || c == 0x7F)
                c = ' ';
            if (c == '"')
                g_line[n++] = '"';
            g_line[n++] = (char)c;
        }
        if (fd->type == 'C')
            g_line[n++] = '"';
    }
    g_line[n++] = '\r';
    g_line[n++] = '\n';
    return n;
}

/* Read the next n bytes of file h into g_rd; 1 when they equal s. */
static int file_next(int h, const char *s, u16 n)
{
    u16 got;

    if (cf_read(h, g_rd, n, &got) != 0 || got != n)
        return 0;
    return memcmp(g_rd, s, n) == 0;
}

int export_ok(int t, const char *name)
{
    u32 n;
    u32 r;
    u16 len;
    u16 got;
    int h;
    int ok;

    if (cf_open(tfs_path(name, ""), CF_READ | CF_DENYNONE, &h) != 0)
        return 0;
    ok = 1;
    n = eng_reccount(g_e, t);
    for (r = 1; ok && r <= n; r++) {
        ok = eng_rec_read(g_e, t, r) == ENG_OK;
        if (ok && dbf_is_deleted(eng_dbf(g_e, t)))
            continue;
        len = ok ? expect_line(t) : 0;
        if (ok && (len == 0 || !file_next(h, g_line, len))) {
            printf("export of %s differs at record %lu\n",
                   eng_table_def(t)->name, (unsigned long)r);
            ok = 0;
        }
    }
    if (ok)
        ok = file_next(h, "\032", 1) && cf_read(h, g_rd, 1, &got) == 0 &&
             got == 0;
    cf_close(h);
    return ok;
}

int export_all(int t, const char *name, u8 flags, u8 tag,
               const char *after, int *calls, u32 *rows)
{
    char cursor[ENG_CURSORMAX];
    char next[ENG_CURSORMAX];
    u16 n;
    int rc;

    *calls = 0;
    *rows = 0;
    strcpy(next, after);
    do {
        rc = eng_export(g_e, t, tfs_path(name, ""), flags, tag, next, &n,
                        cursor);
        (*calls)++;
        *rows += n;
        strcpy(next, cursor);
    } while (rc == ENG_OK && cursor[0] != '\0' && *calls < MAXCALLS);
    return rc;
}

static const char hdr_items[] =
    "ITEM_ID,CATEGORY,ITEMTYPE,MFR,MODEL,SERIALNO,DESCR,COND,WORKS,QTY,"
    "ON_LOAN,RESERVED,IN_USE,ATTACHED,LOC_ID,PARENT_ID,ACQ_DATE,ACQ_SRC,"
    "ACQ_PRICE,CUR_VALUE,TAGS,STATUS,CREATED,UPDATED\r\n";
const char row1[] =
    "1,\"SYS\",\"\",\"IBM\",\"\",\"\",\"\",\"\",\"?\",1,0,1,0,0,0,0,,\"\","
    ",,\"\",\"A\",20261008,20261008\r\n";
const char row2[] =
    "2,\"SYS\",\"\",\"  Lead\",\"Say \"\"hi\"\"\",\"\",\"Caf\202\",\"GD\","
    "\"I\",3,1,0,0,1,1,0,19840101,\"\",12.50,0.00,\"Retro,isa\",\"A\","
    "20261008,20261008\r\n";
const char row4[] =
    "4,\"SYS\",\"\",\"Ctl\",\"\",\"\",\" x\",\"\",\"?\",1,0,0,0,0,0,0,,"
    "\"\",,,\"\",\"A\",20261008,20261008\r\n";

/* The LOANS export of the fixture, with its header. */
static const char loans_csv[] =
    "LOAN_ID,ITEM_ID,KIND,QTY,HOLDER,HOST_ID,CONTACT,OUT_DATE,DUE_DATE,"
    "RET_DATE,NOTES\r\n"
    "1,2,\"L\",1,\"Jane\",0,\"\",20261008,20261101,,\"\"\r\n"
    "2,1,\"R\",1,\"\",2,\"\",20261008,,,\"\"\r\n\032";

/* Items 1 to 4 (3 deleted, 4 with a control byte stored by hand), the
   location A1, a maintenance record, a loan of item 2 and a reservation
   of item 1 for item 2. */
static void fixture(void)
{
    const dbf_field *fd;
    dbf *d;
    u32 id;
    u32 r;

    TF_EQ(te_loc("A1", 0), 1);
    TF_EQ(eng_item_add(g_e, te_p, te_kv("CATEGORY=SYS|MFR=IBM"), &id),
          ENG_OK);
    te_p[0].key = "CATEGORY";
    te_p[0].val = "SYS";
    te_p[1].key = "MFR";
    te_p[1].val = "  Lead";
    te_p[2].key = "MODEL";
    te_p[2].val = "Say \"hi\"";
    te_p[3].key = "DESCR";
    te_p[3].val = "Caf\202";
    TF_EQ(eng_item_add(g_e, te_p, 4, &id), ENG_OK);
    TF_EQ(eng_item_set(g_e, id, te_p,
                       te_kv("QTY=3|LOC_ID=A1|ACQ_DATE=19840101|"
                             "ACQ_PRICE=12.5|CUR_VALUE=0|COND=gd|WORKS=i|"
                             "TAGS=Retro  isa,retro")), ENG_OK);
    TF_EQ(eng_item_add(g_e, te_p, te_kv("CATEGORY=SYS|MFR=Gone"), &id),
          ENG_OK);
    TF_EQ(eng_item_del(g_e, id), ENG_OK);
    TF_EQ(eng_item_add(g_e, te_p, te_kv("CATEGORY=SYS|MFR=Ctl|DESCR=ax"),
                       &id), ENG_OK);
    if (TF_EQ(eng_item_get(g_e, id, &r), ENG_OK)) {
        d = eng_dbf(g_e, ENG_T_ITEMS);
        fd = eng_field(g_e, ENG_T_ITEMS,
                       (u16)eng_field_num(g_e, ENG_T_ITEMS, "DESCR"));
        dbf_recbuf(d)[fd->offset] = 0x01;
        TF_EQ(dbf_write(d), DBF_OK);
        TF_EQ(dbf_flush(d), DBF_OK);
    }
    TF_EQ(eng_maint_add(g_e, 2, te_p,
                        te_kv("MTYPE=CL|SUMMARY=Clean|COST=1.5"), &id),
          ENG_OK);
    TF_EQ(eng_loan_out(g_e, 2, ENG_K_LOAN, te_p,
                       te_kv("HOLDER=Jane|DUE_DATE=20261101"), &id),
          ENG_OK);
    TF_EQ(eng_loan_out(g_e, 1, ENG_K_RESERVE, te_p, te_kv("HOST=2"), &id),
          ENG_OK);
}

static void test_names(void)
{
    static const char *const bad[] = {
        "", "A B", "X*.CSV", "X?", "A\"B", "C:", "DIR\\", "DIR/", "X.DBF",
        "x.ndx", "X.Dbt", "X.$$$", "X.&C0", "A,B", "A;B", "A|B", "A=B",
        "A\tB", "X.DBFX", "ITEMS.DBFX", "EDITING.$$$X", "EDITING12.$$$",
        "ITEMS.&DB", "X.%NX", "X.#NX", "X.$", ".CSV", "DIR\\.CSV",
        "A.B.C", "CON", "con.csv", "DIR\\PRN.TXT", "C:NUL", "Aux.X",
        "COM1", "com4.csv", "LPT1", "D:\\X\\lpt3.prn", "CLOCK$",
        "clock$.x", NULL
    };
    static const char *const good[] = {
        "X.CSV", "C:\\DIR\\X.TXT", "../X", "NOEXT", "X.DB", "X.CS",
        "DIR.DBF\\X", "CON\\X.CSV", "CONS.CSV", "COM5", "COM0.TXT",
        "LPT4", "COMX", "CO.CSV", "COX.CSV", "NUX", "PRX.TXT",
        "CLOCK.CSV", "PRNTOUTS.TXT", "X.", NULL
    };
    char longp[ENG_CSVPATH + 2];
    int t;
    int i;

    TF_EQ(eng_csv_table(g_e, "items", 0, &t), ENG_OK);
    TF_EQ(t, ENG_T_ITEMS);
    TF_EQ(eng_csv_table(g_e, "Audit", 0, &t), ENG_OK);
    TF_EQ(t, ENG_T_AUDIT);
    TF_EQ(eng_csv_table(g_e, "LOCS", 1, &t), ENG_OK);
    TF_EQ(t, ENG_T_LOCS);
    TF_EQ(eng_csv_table(g_e, "AUDIT", 1, &t), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "TABLE");
    TF_STREQ(eng_errmsg(g_e), "table not allowed");
    TF_EQ(t, -1);
    TF_EQ(eng_csv_table(g_e, "CATS", 1, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "ltypes", 0, &t), ENG_OK);
    TF_EQ(t, ENG_T_LTYPES);
    TF_EQ(eng_csv_table(g_e, "LTYPES", 1, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "USERS", 0, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "SEQ", 0, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "ITEM", 0, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "ITEMSX", 0, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, "", 0, &t), ENG_EARG);
    TF_EQ(eng_csv_table(g_e, NULL, 0, &t), ENG_EARG);
    for (i = 0; bad[i] != NULL; i++)
        if (!TF_EQ(eng_csv_path(g_e, bad[i]), ENG_EARG))
            printf("  path \"%s\"\n", bad[i]);
    TF_STREQ(eng_errfield(g_e), "FILE");
    TF_STREQ(eng_errmsg(g_e), "bad file name");
    TF_EQ(eng_csv_path(g_e, NULL), ENG_EARG);
    for (i = 0; good[i] != NULL; i++)
        if (!TF_EQ(eng_csv_path(g_e, good[i]), ENG_OK))
            printf("  path \"%s\"\n", good[i]);
    memset(longp, 'A', sizeof longp);
    longp[ENG_CSVPATH] = '\0';
    TF_EQ(eng_csv_path(g_e, longp), ENG_OK);
    longp[ENG_CSVPATH] = 'A';
    longp[ENG_CSVPATH + 1] = '\0';
    TF_EQ(eng_csv_path(g_e, longp), ENG_EARG);
    longp[0] = (char)0x82;
    longp[ENG_CSVPATH] = '\0';
    TF_EQ(eng_csv_path(g_e, longp), ENG_OK);
}

static void test_export(void)
{
    char cursor[ENG_CURSORMAX];
    i32 n;
    u32 rows;
    u16 r16;
    int calls;
    int t;

    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", 0, 0, "", &calls, &rows),
          ENG_OK);
    TF_EQ(calls, 1);
    TF_EQ(rows, 3);
    n = get_file("X.CSV");
    strcpy(g_line, row1);
    strcat(g_line, row2);
    strcat(g_line, row4);
    strcat(g_line, "\032");
    TF_EQ(n, strlen(g_line));
    TF_CHECK(n > 0 && memcmp(g_file, g_line, (size_t)n) == 0);
    TF_CHECK(!exists("X.&C0"));
    /* An existing file: refused unless replaced. */
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), ENG_CSV_HEADER,
                     0, NULL, &r16, cursor), ENG_EDUP);
    TF_STREQ(eng_errfield(g_e), "FILE");
    TF_STREQ(eng_errmsg(g_e), "file already exists");
    TF_CHECK(!exists("X.&C0"));
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", ENG_CSV_HEADER | ENG_CSV_REPLACE,
                     0, "", &calls, &rows), ENG_OK);
    n = get_file("X.CSV");
    strcpy(g_line, hdr_items);
    strcat(g_line, row1);
    strcat(g_line, row2);
    strcat(g_line, row4);
    strcat(g_line, "\032");
    TF_EQ(n, strlen(g_line));
    TF_CHECK(n > 0 && memcmp(g_file, g_line, (size_t)n) == 0);
    /* LOANS with the kind and host of each row. */
    TF_EQ(export_all(ENG_T_LOANS, "L.CSV",
                     ENG_CSV_HEADER | ENG_CSV_REPLACE, 0, "", &calls, &rows),
          ENG_OK);
    TF_EQ(rows, 2);
    n = get_file("L.CSV");
    TF_EQ(n, strlen(loans_csv));
    TF_CHECK(n > 0 && memcmp(g_file, loans_csv, (size_t)n) == 0);
    /* The other tables, a file without an extension. */
    for (t = ENG_T_LOCS; t <= ENG_T_AUDIT; t++) {
        TF_EQ(export_all(t, "Y", ENG_CSV_REPLACE, 0, "", &calls, &rows),
              ENG_OK);
        TF_CHECK(rows > 0);
        TF_CHECK(!exists("Y.&C0"));
        if (!TF_CHECK(export_ok(t, "Y")))
            printf("  table %s\n", eng_table_def(t)->name);
    }
    TF_EQ(eng_export(g_e, ENG_T_USERS, tfs_path("Y", ""), 0, 0, NULL,
                     &r16, cursor), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "TABLE");
    TF_EQ(eng_export(g_e, ENG_T_SEQ, tfs_path("Y", ""), 0, 0, NULL,
                     &r16, cursor), ENG_EARG);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("Y.DBF", ""), 0, 0, NULL,
                     &r16, cursor), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "FILE");
    /* A continuation whose scratch file is gone, a cursor of another
       table, a tag out of range, a directory that does not exist. */
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0,
                     "ITEMS::2", &r16, cursor), ENG_ECURSOR);
    TF_STREQ(eng_errmsg(g_e), "no export in progress");
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0,
                     "LOCS::1", &r16, cursor), ENG_ECURSOR);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0,
                     ENG_EXPTAGS, NULL, &r16, cursor), ENG_EINTERNAL);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("NODIR/X.CSV", ""), 0, 0,
                     NULL, &r16, cursor), ENG_ENOTFOUND);
    TF_STREQ(eng_errfield(g_e), "FILE");
}

static void test_paging(void)
{
    char cursor[ENG_CURSORMAX];
    char after[ENG_CURSORMAX];
    char want[ENG_CURSORMAX];
    u32 id;
    u32 rows;
    u32 size;
    u16 n;
    int calls;
    int i;

    for (i = 0; i < NPAGED; i++)
        TF_EQ(eng_item_add(g_e, te_p, te_kv("CATEGORY=SYS|MFR=Paged"), &id),
              ENG_OK);
    /* Item IDs are record numbers here.  Deleted at the page edges: the
       last record of the first page, the first and the last of the
       second (the fixture's item 3 is deleted as well). */
    TF_EQ(eng_item_del(g_e, ENG_EXPROWS), ENG_OK);
    TF_EQ(eng_item_del(g_e, ENG_EXPROWS + 1), ENG_OK);
    TF_EQ(eng_item_del(g_e, 2 * ENG_EXPROWS), ENG_OK);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                     ENG_CSV_REPLACE, 0, NULL, &n, cursor), ENG_OK);
    TF_EQ(n, ENG_EXPROWS - 2);
    sprintf(want, "ITEMS::%lX", (unsigned long)ENG_EXPROWS);
    TF_STREQ(cursor, want);
    TF_CHECK(exists("X.&C0"));
    TF_CHECK(exists("X.CSV"));      /* the old one, until the end */
    strcpy(after, cursor);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                     ENG_CSV_REPLACE, 0, after, &n, cursor), ENG_OK);
    TF_EQ(n, ENG_EXPROWS - 2);
    sprintf(want, "ITEMS::%lX", (unsigned long)(2 * ENG_EXPROWS));
    TF_STREQ(cursor, want);
    strcpy(after, cursor);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                     ENG_CSV_REPLACE, 0, after, &n, cursor), ENG_OK);
    TF_EQ(n, NFIXTURE + NPAGED - 2 * ENG_EXPROWS);
    TF_STREQ(cursor, "");
    TF_CHECK(!exists("X.&C0"));
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    /* A target that appears before the last request is not replaced;
       the scratch file is cut back, so the retry writes the same bytes. */
    cf_remove(tfs_path("X.CSV", ""));
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0, NULL,
                     &n, cursor), ENG_OK);
    put_file("X.CSV", "old", 0);
    strcpy(after, cursor);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0, after,
                     &n, cursor), ENG_OK);
    strcpy(after, cursor);
    size = tfs_size(tfs_path("X.&C0", ""));
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0, after,
                     &n, cursor), ENG_EDUP);
    TF_EQ(get_file("X.CSV"), 3);
    TF_EQ(tfs_size(tfs_path("X.&C0", "")), size);
    cf_remove(tfs_path("X.CSV", ""));
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""), 0, 0, after,
                     &n, cursor), ENG_OK);
    TF_STREQ(cursor, "");
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    /* The fifth write fails, in a continuation: cut back, retried. */
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                     ENG_CSV_REPLACE, 0, NULL, &n, cursor), ENG_OK);
    strcpy(after, cursor);
    size = tfs_size(tfs_path("X.&C0", ""));
    tfs_fault(TFS_OP(CF_OP_WRITE), 5, 1, CF_EFULL);
    TF_EQ(eng_export(g_e, ENG_T_ITEMS, tfs_path("X.CSV", ""),
                     ENG_CSV_REPLACE, 0, after, &n, cursor), ENG_EFULL);
    tfs_fault_off();
    TF_STREQ(eng_errfield(g_e), "FILE");
    TF_EQ(tfs_size(tfs_path("X.&C0", "")), size);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", ENG_CSV_REPLACE, 0, after, &calls,
                     &rows), ENG_OK);
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
    /* A new export replaces a leftover scratch file. */
    put_file("X.&C0", "junk", 0);
    TF_EQ(export_all(ENG_T_ITEMS, "X.CSV", ENG_CSV_REPLACE, 0, "", &calls,
                     &rows), ENG_OK);
    TF_EQ(calls, 3);
    TF_EQ(rows, NFIXTURE + NPAGED - NDELETED);
    TF_CHECK(export_ok(ENG_T_ITEMS, "X.CSV"));
}

/* ---------------------------------------------------------------------
 * The checks of ADD.
 * --------------------------------------------------------------------- */

static void same_as_add(int t, const char *spec)
{
    char field[DBF_NAMELEN + 1];
    char msg[MSGMAX];
    u32 items;
    u32 locs;
    u32 audits;
    u32 id;
    u16 depth;
    u16 n;
    int rc;
    int rc2;

    items = eng_reccount(g_e, ENG_T_ITEMS);
    locs = eng_reccount(g_e, ENG_T_LOCS);
    audits = eng_reccount(g_e, ENG_T_AUDIT);
    n = te_kv(spec);
    rc = t == ENG_T_ITEMS ? eng_item_check(g_e, te_p, n)
                          : eng_loc_check(g_e, te_p, n, 0, &depth);
    strcpy(field, eng_errfield(g_e));
    strcpy(msg, eng_errmsg(g_e));
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), items);
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), locs);
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), audits);
    eng_clear_error(g_e);
    n = te_kv(spec);
    rc2 = t == ENG_T_ITEMS ? eng_item_add(g_e, te_p, n, &id)
                           : eng_loc_add(g_e, te_p, n, &id);
    if (!TF_EQ(rc, rc2))
        printf("  check \"%s\"\n", spec);
    if (rc2 != ENG_OK) {
        TF_STREQ(field, eng_errfield(g_e));
        TF_STREQ(msg, eng_errmsg(g_e));
        TF_EQ(eng_reccount(g_e, t), t == ENG_T_ITEMS ? items : locs);
    }
}

static void test_checks(void)
{
    same_as_add(ENG_T_ITEMS, "MFR=X");
    same_as_add(ENG_T_ITEMS, "CATEGORY=ZZZ");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|LOC_ID=Q9");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|LOC_ID=8");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|COND=BAD");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|STATUS=X");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|QTY=-1");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|ITEM_ID=5");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|BOGUS=1");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|ACQ_DATE=20261340");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|QTY=123456");
    same_as_add(ENG_T_ITEMS, "CATEGORY=SYS|LOC_ID=A1|QTY=2");
    same_as_add(ENG_T_LOCS, "");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|LOC_CODE=A B");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|LOC_CODE=TOOLONGCODE");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|LOC_CODE=a1");
    same_as_add(ENG_T_LOCS, "LOC_NAME=a1|LTYPE=BIN");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=ZZ");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|PARENT_ID=ZZ");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|CAPACITY=-2");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|LOC_ID=2");
    same_as_add(ENG_T_LOCS, "LOC_NAME=N|LTYPE=BIN|BOGUS=1");
    same_as_add(ENG_T_LOCS, "LOC_NAME=n1|LTYPE=bin|LOC_CODE=n1|CAPACITY=4|"
                            "PARENT_ID=a1");
}

int main(int argc, char **argv)
{
    tf_begin("T_CSV");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    te_remove(tfs_dir(), k_files);
    if (TF_EQ(te_open(&g_e, &g_now), ENG_OK)) {
        fixture();
        test_names();
        test_export();
        test_paging();
        test_concurrent();
        test_checks();
        test_parse();
        test_header();
        test_import();
        test_tree();
        test_order();
        test_cost();
        test_room();
        test_fail();
        TF_EQ(eng_close(g_e), ENG_OK);
        /* test_fail leaves an import stopped in its second pass, with
           its marker: the next open must repair it and succeed. */
        if (TF_EQ(te_open(&g_e, &g_now), ENG_OK))
            TF_EQ(eng_close(g_e), ENG_OK);
        test_trip(&g_now);
    }
    te_remove(tfs_dir(), k_files);
    return tf_done();
}
