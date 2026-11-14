/*
 * T_KEYEX.C - unit tests for the index key expressions (KEYEXPR.C).
 *
 * usage: T_KEYEX [dir [golden]]
 *        T_KEYEX -f dir
 *
 * The test builds the fixture table KXFIX.DBF (T_KEYEX2.C) in the
 * scratch directory dir (default: the current directory) and compares
 * kx_eval with the key values dBASE III Plus 1.1 computed for the same
 * records, which TEST/DBASE/KXCHECK.PRG wrote to the file golden
 * (default: KEYEXPR.TXT in the directory of the program itself, as DOS
 * reports it in argv[0]).
 * The golden file is read line by line:
 *   X <expression>              starts an expression
 *   V <expression>              starts one that dBASE evaluates but
 *                               cannot index with that key length
 *   K <recno:4><length:4> [key] the key dBASE computed for that record,
 *                               and LEN() of it
 *   ERR <number> <message>      dBASE reported an error for it
 * An X expression with an error line, and every V expression, must fail
 * to compile; any other X expression must compile, with kx_len equal to
 * every length and kx_eval equal to every key byte for byte.  The other
 * checks cover what dBASE cannot show in that file: syntax and type
 * errors, field types, limits, memory, and N field text the fixture
 * cannot hold or dBASE reads otherwise.  The scratch files are
 * removed at the start and at the end.  Prints each failure and a
 * summary line; exit status 0 when every check passes, else 1 (2 when
 * dir is too long).
 *
 * "-f dir" only writes KXFIX.DBF there, for KXCHECK.PRG to read (see
 * TEST/DBASE/mkkx.sh); exit status 0, or 1 when it cannot (2 when dir
 * is too long).
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_KEYEX.H"

/* The files the tests make in the scratch directory, removed before
   and after them. */
static const char *const k_scratch[] = {
    "KXFIX.DBF", "KXMEMO.DBF", "KXOTHER.DBF", NULL
};

static char g_golden[200];

/* ---------------------------------------------------------------------
 * Compile errors and lengths.
 * --------------------------------------------------------------------- */

typedef struct {
    const char *expr;
    int rc;
} compile_case;

static const compile_case g_bad[] = {
    { "", DBF_EARG },
    { "   ", DBF_EARG },
    { "MFR+", DBF_EARG },
    { "+MFR", DBF_EARG },
    { "(MFR", DBF_EARG },
    { "MFR)", DBF_EARG },
    { "MFR MODEL", DBF_EARG },
    { "UPPER(MFR", DBF_EARG },
    { "UPPER MFR", DBF_EARG },
    { "\"abc", DBF_EARG },
    { "[abc", DBF_EARG },
    { "MFR*2", DBF_EARG },
    { "MFR$MODEL", DBF_EARG },
    { "NOFIELD", DBF_EARG },
    { "NOFIELD+MFR", DBF_EARG },
    { "ITEM_IDX", DBF_EARG },
    { "VERYLONGFIELDNAME", DBF_EARG },
    { "UPPERX(MFR)", DBF_EARG },
    { "UPP(MFR)", DBF_EARG },
    { "SUBSTRING(MFR,1,2)", DBF_EARG },
    { "LEFT(MFR,2)", DBF_EARG },
    { "DTOS(MDATE)", DBF_EARG },
    { "ITEM_ID", DBF_EARG },
    { "ITEM_ID+1", DBF_EARG },
    { "YEAR(MDATE)", DBF_EARG },
    { "MDATE", DBF_EARG },
    { "ACTIVE", DBF_EARG },
    { "MFR+ACTIVE", DBF_EARG },
    { "MFR+ITEM_ID", DBF_EARG },
    { "ITEM_ID+MFR", DBF_EARG },
    { "MFR-MODEL", DBF_EARG },
    { "-MFR", DBF_EARG },
    { "STR(MFR)", DBF_EARG },
    { "STR(MDATE)", DBF_EARG },
    { "STR(ACTIVE)", DBF_EARG },
    { "UPPER(ITEM_ID)", DBF_EARG },
    { "UPPER(MDATE)", DBF_EARG },
    { "RIGHT(ITEM_ID,2)", DBF_EARG },
    { "SUBSTR(ITEM_ID,1,2)", DBF_EARG },
    { "YEAR(MFR)", DBF_EARG },
    { "STR(YEAR(ITEM_ID),4)", DBF_EARG },
    { "STR(YEAR(MDATE+1),4)", DBF_EARG },
    { "STR(MDATE+1,8)", DBF_EARG },
    { "STR(-MDATE,8)", DBF_EARG },
    { "MDATE+MFR", DBF_EARG },
    { "MONTH()", DBF_EARG },
    { "STR()", DBF_EARG },
    { "STR(ITEM_ID,)", DBF_EARG },
    { "STR(ITEM_ID,8,)", DBF_EARG },
    { "STR(ITEM_ID,8,2,1)", DBF_EARG },
    { "STR(ITEM_ID,0)", DBF_EARG },
    { "STR(ITEM_ID,-1)", DBF_EARG },
    { "STR(ITEM_ID,20)", DBF_EARG },
    { "STR(ITEM_ID,5,5)", DBF_EARG },
    { "STR(ITEM_ID,5,-1)", DBF_EARG },
    { "STR(ITEM_ID,19,16)", DBF_EARG },
    { "STR(ITEM_ID,8.5)", DBF_EARG },
    { "STR(ITEM_ID,QTY)", DBF_EARG },
    { "STR(ITEM_ID,99999)", DBF_EARG },
    { "RIGHT(MFR)", DBF_EARG },
    { "RIGHT(MFR,QTY)", DBF_EARG },
    { "STR(PRICE)", DBF_EARG },
    { "RIGHT(MFR,0)", DBF_EARG },
    { "RIGHT(MFR,-3)", DBF_EARG },
    { "RIGHT(MFR,31)", DBF_EARG },
    { "SUBSTR(MFR)", DBF_EARG },
    { "SUBSTR(MFR,31)", DBF_EARG },
    { "SUBSTR(MFR,30)", DBF_EARG },
    { "SUBSTR(MFR,2)", DBF_EARG },
    { "SUBSTR(MFR,-1,2)", DBF_EARG },
    { "SUBSTR(MFR,0)", DBF_EARG },
    { "SUBSTR(MFR,0,3)", DBF_EARG },
    { "SUBSTR(MFR,2,0)", DBF_EARG },
    { "SUBSTR(MFR,1,-1)", DBF_EARG },
    { "SUBSTR(MFR,28,4)", DBF_EARG },
    { "\"\"", DBF_EARG },
    { "STR(1.2.3,5)", DBF_EARG },
    { "STR(.,5)", DBF_EARG },
    { "STR(12A,5)", DBF_EARG },
    { "STR(1234567890123456789,19)", DBF_EARG },
    { "STR(0.0000000000000000001,19)", DBF_EARG },
    { "MFR+MODEL+SERIALNO+MFR", DBF_ERANGE },
    { "MFR+MODEL+SERIALNO+TBL+SUBSTR(LOC_CODE,1,3)", DBF_ERANGE },
    { "SUBSTR(MFR+MODEL+SERIALNO+MFR+MODEL+SERIALNO+MFR+MODEL+SERIALNO,1,5)",
      DBF_ERANGE }
};

typedef struct {
    const char *expr;
    u16 len;
} len_case;

/* The index key expressions of the inventory schema, and others. */
static const len_case g_lens[] = {
    { "STR(ITEM_ID,8)", 8 },
    { "STR(LOC_ID,8)+STR(ITEM_ID,8)", 16 },
    { "STR(PARENT_ID,8)+STR(ITEM_ID,8)", 16 },
    { "UPPER(MFR)+UPPER(MODEL)", 60 },
    { "UPPER(SERIALNO)", 30 },
    { "STR(LOC_ID,8)", 8 },
    { "STR(PARENT_ID,8)+UPPER(LOC_NAME)+STR(LOC_ID,8)", 46 },
    { "LOC_CODE", 6 },
    { "STR(ITEM_ID,8)+STR(YEAR(MDATE),4)+RIGHT(STR(MONTH(MDATE)+100,3),2)"
      "+RIGHT(STR(DAY(MDATE)+100,3),2)", 16 },
    { "STR(ITEM_ID,8)+STR(LOAN_ID,8)", 16 },
    { "STR(YEAR(RET_DATE),4)+STR(YEAR(DUE_DATE),4)"
      "+RIGHT(STR(MONTH(DUE_DATE)+100,3),2)"
      "+RIGHT(STR(DAY(DUE_DATE)+100,3),2)+STR(LOAN_ID,8)", 20 },
    { "TBL+STR(REC_ID,8)+STR(AUD_ID,8)", 24 },
    { "USERNAME", 8 },
    { "STR(PRICE,10)", 10 },
    { "STR(PRICE,19,15)", 19 },
    { "RIGHT(MFR,30)", 30 },
    { "RIGHT(MFR,1)", 1 },
    { "SUBSTR(MFR,1)", 30 },
    { "SUBSTR(MFR,30,1)", 1 },
    { "SUBSTR(MFR,28,3)", 3 },
    { "SUBSTR(MFR,5,26)", 26 },
    { "SUBS(MFR,2,5)", 5 },
    { "MFR+MODEL+SERIALNO+TBL+SUBSTR(LOC_CODE,1,2)", NDX_MAXKEY },
    { "\"\"+[x]+''", 1 },
    { "UPPER( 'abc' )", 3 }
};

static void test_compile(dbf *t)
{
    static char s[400];         /* the longest expression built here    */
    kx *k;
    int i;
    int rc;

    for (i = 0; i < (int)(sizeof g_bad / sizeof g_bad[0]); i++) {
        k = (kx *)&k;
        rc = kx_compile(&k, t, g_bad[i].expr);
        if (!TF_EQ(rc, g_bad[i].rc))
            printf("  expression: %s\n", g_bad[i].expr);
        TF_CHECK(k == NULL);
        kx_free(k);
    }
    for (i = 0; i < (int)(sizeof g_lens / sizeof g_lens[0]); i++) {
        rc = kx_compile(&k, t, g_lens[i].expr);
        if (!TF_EQ(rc, DBF_OK)) {
            printf("  expression: %s\n", g_lens[i].expr);
            continue;
        }
        if (!TF_EQ(kx_len(k), g_lens[i].len))
            printf("  expression: %s\n", g_lens[i].expr);
        kx_free(k);
    }

    /* A character constant of the longest length dBASE allows, and one
       more; then nesting and tree height. */
    strcpy(s, "SUBSTR(\"");
    memset(s + 8, 'a', 254);
    strcpy(s + 8 + 254, "\",1,5)");
    TF_EQ(kx_compile(&k, t, s), DBF_OK);
    kx_free(k);
    strcpy(s, "SUBSTR(\"");
    memset(s + 8, 'a', 255);
    strcpy(s + 8 + 255, "\",1,5)");
    TF_EQ(kx_compile(&k, t, s), DBF_ERANGE);

    /* 16 levels of parentheses are accepted, 17 are not. */
    memset(s, '(', 16);
    strcpy(s + 16, "MFR");
    memset(s + 19, ')', 16);
    s[35] = '\0';
    TF_EQ(kx_compile(&k, t, s), DBF_OK);
    kx_free(k);
    memset(s, '(', 17);
    strcpy(s + 17, "MFR");
    memset(s + 20, ')', 17);
    s[37] = '\0';
    TF_EQ(kx_compile(&k, t, s), DBF_EARG);

    /* A sum of 50 terms is accepted, one of 70 is too high a tree. */
    strcpy(s, "STR(1");
    for (i = 1; i < 50; i++)
        strcat(s, "+1");
    strcat(s, ",3)");
    TF_EQ(kx_compile(&k, t, s), DBF_OK);
    kx_free(k);
    strcpy(s, "STR(1");
    for (i = 1; i < 70; i++)
        strcat(s, "+1");
    strcat(s, ",3)");
    TF_EQ(kx_compile(&k, t, s), DBF_EARG);

    kx_free(NULL);
}

/* Memo fields are refused like logical ones. */
static void test_memo_field(void)
{
    static dbf_field fields[2] = {
        { "NAME",  'C', 10, 0, 0 },
        { "NOTES", 'M', 10, 0, 0 }
    };
    dbf *t;
    kx *k;

    if (!TF_EQ(dbf_create(tfs_path("KXMEMO.DBF", ""), fields, 2), DBF_OK))
        return;
    if (!TF_EQ(dbf_open(&t, tfs_path("KXMEMO.DBF", ""), DBF_RDONLY), DBF_OK))
        return;
    TF_EQ(kx_compile(&k, t, "NOTES"), DBF_EARG);
    TF_EQ(kx_compile(&k, t, "NAME+NOTES"), DBF_EARG);
    TF_EQ(kx_compile(&k, t, "UPPER(NOTES)"), DBF_EARG);
    TF_EQ(kx_compile(&k, t, "name"), DBF_OK);
    TF_EQ(kx_len(k), 10);
    kx_free(k);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("KXMEMO.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Evaluation.
 * --------------------------------------------------------------------- */

typedef struct {
    const char *expr;
    u16 rec;
    const char *key;
} eval_case;

/* Values dBASE III Plus 1.1 gives for these expressions. */
static const eval_case g_evals[] = {
    { "STR(ITEM_ID,8)", 1, "       1" },
    { "STR(ITEM_ID,8)", 8, "       0" },
    { "STR(ITEM_ID,8)", 6, "-9999999" },
    { "UPPER(MFR)", 3, "APPLE COMPUTER                " },
    { "STR(YEAR(RET_DATE),4)+STR(YEAR(DUE_DATE),4)"
      "+RIGHT(STR(MONTH(DUE_DATE)+100,3),2)"
      "+RIGHT(STR(DAY(DUE_DATE)+100,3),2)+STR(LOAN_ID,8)", 2,
      "   0   00000       2" },
    { "STR(YEAR(RET_DATE),4)+STR(YEAR(DUE_DATE),4)"
      "+RIGHT(STR(MONTH(DUE_DATE)+100,3),2)"
      "+RIGHT(STR(DAY(DUE_DATE)+100,3),2)+STR(LOAN_ID,8)", 1,
      "199920791231       1" },
    { "STR(YEAR(MDATE),4)+RIGHT(STR(MONTH(MDATE)+100,3),2)"
      "+RIGHT(STR(DAY(MDATE)+100,3),2)", 3, "20000229" },
    { "STR(PRICE,4,1)", 1, " 1.2" },
    { "STR(PRICE,3,2)", 3, "1.2" },
    { "STR(PRICE,4,1)", 6, "-0.1" },
    { "STR(PRICE,3)", 14, "  0" },
    { "STR(QTY,3)", 4, "***" },
    { "STR(ITEM_ID,7)", 5, " .9E+08" },
    { "STR(1.005,5,2)", 1, " 1.00" },
    { "STR(-0.5,10)", 1, "         0" },
    { "STR(2.5,10)", 1, "         3" },
    { "STR(1234.5,5,1)", 1, " 1234" },
    { "STR(-12.345,6,3)", 1, "-12.35" },
    { "STR(0.05,3,2)", 1, ".05" },
    { "STR(9999999.5,7,1)", 1, " .1E+08" },
    { "STR(99999999.99,19,9)", 1, " 99999999.989999995" },
    { "STR(-0.0000000000000005,19,15)", 1, " -0.000000000000001" },
    { "STR(-9.4,2,1)", 1, "-9" },
    { "STR(-99.4,3,1)", 1, "-99" },
    { "UPPER(SUBS(MODEL,1,4))+RIGH(TBL,6)", 7, "\216\231\232 EMS   " }
};

static void test_eval(dbf *t)
{
    u8 key[NDX_MAXKEY];
    kx *k;
    int i;
    u16 n;

    for (i = 0; i < (int)(sizeof g_evals / sizeof g_evals[0]); i++) {
        if (!TF_EQ(kx_compile(&k, t, g_evals[i].expr), DBF_OK))
            continue;
        n = kx_len(k);
        TF_EQ(n, strlen(g_evals[i].key));
        TF_EQ(dbf_read(t, g_evals[i].rec), DBF_OK);
        memset(key, 0, sizeof key);
        TF_EQ(kx_eval(k, t, key), DBF_OK);
        if (!TF_CHECK(memcmp(key, g_evals[i].key, n) == 0))
            printf("  %s on record %u: [%.*s]\n", g_evals[i].expr,
                   (unsigned)g_evals[i].rec, (int)n, (char *)key);
        kx_free(k);
    }
}

/* N field text that the fixture cannot hold, or that dBASE reads
   otherwise (KEYEXPR.C's header comment): NUL bytes, and values only an
   exponent writes, of 10^DBF_MAXNUM or more (just below, a number,
   leading zeros not counting) or with more than KX_MAXSCALE decimals.
   The text is put at the start of the field in record 1's buffer, the
   rest of the field blank. */
typedef struct {
    const char *field;
    const char *text;
    u16 len;
    const char *expr;
    const char *key;
} raw_case;

static const raw_case g_raws[] = {
    { "QTY", "\0\0\0\0\0", 5, "STR(QTY,5)", "    0" },
    { "QTY", " 9\0" "9", 4, "STR(QTY,5)", "    9" },
    { "HUGE", "1E19", 4, "STR(HUGE,19)", "*******************" },
    { "HUGE", "-1E19", 5, "STR(HUGE,8)+STR(HUGE-HUGE,2)", "******** 0" },
    { "HUGE", "9999999999999999E3", 18, "STR(HUGE,7)", " .1E+20" },
    { "HUGE", "0000000001E18", 13, "STR(HUGE,19)", "1000000000000000000" },
    { "TINY", "-1E-39", 6, "STR(TINY,19,15)", "  0.000000000000000" }
};

static void test_raw(dbf *t)
{
    u8 key[NDX_MAXKEY];
    const dbf_field *fd;
    kx *k;
    int i;
    u16 n;

    for (i = 0; i < (int)(sizeof g_raws / sizeof g_raws[0]); i++) {
        if (!TF_EQ(kx_compile(&k, t, g_raws[i].expr), DBF_OK))
            continue;
        n = kx_len(k);
        TF_EQ(n, strlen(g_raws[i].key));
        TF_EQ(dbf_read(t, 1), DBF_OK);
        fd = dbf_field_at(t, (u16)dbf_field_index(t, g_raws[i].field));
        memset(dbf_recbuf(t) + fd->offset, ' ', fd->len);
        memcpy(dbf_recbuf(t) + fd->offset, g_raws[i].text, g_raws[i].len);
        memset(key, 0, sizeof key);
        TF_EQ(kx_eval(k, t, key), DBF_OK);
        if (!TF_CHECK(memcmp(key, g_raws[i].key, n) == 0))
            printf("  %s with %s [%.*s]: [%.*s]\n", g_raws[i].expr,
                   g_raws[i].field, (int)g_raws[i].len, g_raws[i].text,
                   (int)n, (char *)key);
        kx_free(k);
    }
}

/* Values around 2^32, where a 32-bit integer would wrap or lose its
   sign; dBASE keeps them as doubles.  A value too wide for w is w
   asterisks below KX_EFORMW characters and exponent form from there up,
   as dBASE writes STR(BIG,9) for 2147483647 (" .214E+10"). */
static const eval_case g_evals32[] = {
    { "STR(4294967295,10)", 1, "4294967295" },
    { "STR(4294967296,10)", 1, "4294967296" },
    { "STR(4294967297,11)", 1, " 4294967297" },
    { "STR(-4294967295,11)", 1, "-4294967295" },
    { "STR(-4294967296,11)", 1, "-4294967296" },
    { "STR(8589934591,10)", 1, "8589934591" },
    { "STR(2147483648,10)", 1, "2147483648" },
    { "STR(-2147483648,11)", 1, "-2147483648" },
    { "STR(4294967296,9)", 1, " .429E+10" },
    { "STR(-4294967296,10)", 1, "-.4294E+10" },
    { "STR(4294967296,6)", 1, "******" },
    { "STR(4294967296.5,12,1)", 1, "4294967296.5" },
    { "STR(4294967296.5,11,1)", 1, " 4294967296" },
    { "STR(4294967295+1,10)", 1, "4294967296" },
    { "STR(4294967296-1,10)", 1, "4294967295" },
    { "STR(HUGE,10)", 3, "2147483648" },
    { "STR(HUGE,11)", 4, "-2147483649" }
};

/* The N field BIG holding 4294967296, and expressions on it. */
static const eval_case g_big32[] = {
    { "STR(BIG,12)", 1, "  4294967296" },
    { "STR(BIG,10)", 1, "4294967296" },
    { "STR(BIG,9)", 1, " .429E+10" },
    { "STR(BIG+1,11)", 1, " 4294967297" },
    { "STR(BIG-1,10)", 1, "4294967295" },
    { "STR(-BIG,11)", 1, "-4294967296" },
    { "STR(BIG,12,1)", 1, "4294967296.0" }
};

static void test_eval32(dbf *t)
{
    u8 key[NDX_MAXKEY];
    kx *k;
    int i;
    u16 n;

    for (i = 0; i < (int)(sizeof g_evals32 / sizeof g_evals32[0]); i++) {
        if (!TF_EQ(kx_compile(&k, t, g_evals32[i].expr), DBF_OK))
            continue;
        n = kx_len(k);
        TF_EQ(n, strlen(g_evals32[i].key));
        TF_EQ(dbf_read(t, g_evals32[i].rec), DBF_OK);
        memset(key, 0, sizeof key);
        TF_EQ(kx_eval(k, t, key), DBF_OK);
        if (!TF_CHECK(memcmp(key, g_evals32[i].key, n) == 0))
            printf("  %s on record %u: [%.*s]\n", g_evals32[i].expr,
                   (unsigned)g_evals32[i].rec, (int)n, (char *)key);
        kx_free(k);
    }

    /* BIG (field 8, N 12) is set in the record buffer only; the record
       on disk keeps its value. */
    for (i = 0; i < (int)(sizeof g_big32 / sizeof g_big32[0]); i++) {
        if (!TF_EQ(kx_compile(&k, t, g_big32[i].expr), DBF_OK))
            continue;
        n = kx_len(k);
        TF_EQ(n, strlen(g_big32[i].key));
        TF_EQ(dbf_read(t, g_big32[i].rec), DBF_OK);
        TF_EQ(put_num_text(t, 8, "4294967296"), DBF_OK);
        memset(key, 0, sizeof key);
        TF_EQ(kx_eval(k, t, key), DBF_OK);
        if (!TF_CHECK(memcmp(key, g_big32[i].key, n) == 0))
            printf("  %s with BIG 4294967296: [%.*s]\n", g_big32[i].expr,
                   (int)n, (char *)key);
        kx_free(k);
    }
}

/* An expression compiled for one table is refused on a table with a
   different record layout. */
static void test_other_table(dbf *t)
{
    static dbf_field fields[1] = {
        { "MFR", 'C', 12, 0, 0 }
    };
    dbf *o;
    kx *k;
    u8 key[NDX_MAXKEY];

    if (!TF_EQ(dbf_create(tfs_path("KXOTHER.DBF", ""), fields, 1), DBF_OK))
        return;
    if (!TF_EQ(dbf_open(&o, tfs_path("KXOTHER.DBF", ""), DBF_RDONLY), DBF_OK))
        return;
    dbf_blank(o);
    TF_EQ(kx_compile(&k, t, "MFR"), DBF_OK);
    TF_EQ(kx_eval(k, o, key), DBF_EARG);
    kx_free(k);
    TF_EQ(dbf_close(o), DBF_OK);
    TF_EQ(cf_remove(tfs_path("KXOTHER.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * The golden file.
 * --------------------------------------------------------------------- */

#define GLINE_MAX   320

static int g_gh;                /* golden file handle                   */
static u8 g_gbuf[512];
static u16 g_gpos;
static u16 g_glen;
static int g_geof;

/* Read the next line (CR and LF removed) into line; returns its length,
   or -1 at the end of the file or at a 0x1A.  Longer lines are cut. */
static int read_line(char *line)
{
    int n;
    u8 c;

    n = 0;
    for (;;) {
        if (g_gpos == g_glen) {
            if (g_geof || cf_read(g_gh, g_gbuf, sizeof g_gbuf, &g_glen) != 0
                || g_glen == 0) {
                g_geof = 1;
                g_glen = 0;
                g_gpos = 0;
                line[n] = '\0';
                return n > 0 ? n : -1;
            }
            g_gpos = 0;
        }
        c = g_gbuf[g_gpos++];
        if (c == 0x1A) {
            g_geof = 1;
            g_glen = g_gpos;
            line[n] = '\0';
            return n > 0 ? n : -1;
        }
        if (c == '\n')
            break;
        if (c != '\r' && n < GLINE_MAX - 1)
            line[n++] = (char)c;
    }
    line[n] = '\0';
    return n;
}

/* Whole-number value of n characters at s (spaces skipped). */
static int field_num(const char *s, int n)
{
    int v;
    int i;

    v = 0;
    for (i = 0; i < n; i++)
        if (s[i] >= '0' && s[i] <= '9')
            v = v * 10 + (s[i] - '0');
    return v;
}

/* One expression of the golden file: its compile result and what the
   golden file says about it. */
typedef struct {
    char expr[GLINE_MAX];
    kx *k;
    int rc;
    int refuse;             /* a V expression                           */
    int errs;
    int keys;
} golden_expr;

static void golden_finish(golden_expr *g, u32 *nexpr)
{
    if (g->expr[0] == '\0')
        return;
    (*nexpr)++;
    if (g->refuse) {
        if (!TF_EQ(g->rc, DBF_EARG))
            printf("  dBASE cannot index: %s\n", g->expr);
    } else if (g->errs > 0) {
        if (!TF_CHECK(g->rc != DBF_OK))
            printf("  dBASE refuses: %s\n", g->expr);
    } else {
        if (!TF_EQ(g->rc, DBF_OK))
            printf("  dBASE accepts: %s\n", g->expr);
        TF_CHECK(g->keys > 0);
    }
    kx_free(g->k);
    g->k = NULL;
    g->expr[0] = '\0';
}

static void golden_key(golden_expr *g, dbf *t, const char *line, int n,
                       u32 *nkeys)
{
    u8 key[NDX_MAXKEY];
    const char *open;
    const char *close;
    int rec;
    int len;
    int ok;

    g->keys++;
    if (g->refuse || g->errs > 0 || g->rc != DBF_OK)
        return;
    (*nkeys)++;
    /* "K" recno(4) length(4) " [" key "]" */
    open = n >= 12 ? line + 11 : NULL;
    close = line + n - 1;
    if (!TF_CHECK(open != NULL && line[10] == '[' && *close == ']')) {
        printf("  bad golden line: %s\n", line);
        return;
    }
    rec = field_num(line + 1, 4);
    len = field_num(line + 5, 4);
    TF_EQ(close - open, len);
    if (!TF_EQ(kx_len(g->k), len)) {
        printf("  length of %s\n", g->expr);
        return;
    }
    ok = dbf_read(t, (u32)rec) == DBF_OK;
    memset(key, 0, sizeof key);
    ok = ok && kx_eval(g->k, t, key) == DBF_OK;
    ok = ok && memcmp(key, open, (size_t)len) == 0;
    if (!TF_CHECK(ok))
        printf("  %s on record %d:\n    got  [%.*s]\n    want [%.*s]\n",
               g->expr, rec, len, (char *)key, len, open);
}

static void test_golden(dbf *t)
{
    static golden_expr g;
    static char line[GLINE_MAX];
    u32 nexpr;
    u32 nkeys;
    int n;

    if (!TF_EQ(cf_open(g_golden, CF_READ, &g_gh), 0)) {
        printf("  cannot open the golden file %s\n", g_golden);
        return;
    }
    g_gpos = 0;
    g_glen = 0;
    g_geof = 0;
    g.expr[0] = '\0';
    g.k = NULL;
    nexpr = 0;
    nkeys = 0;
    while ((n = read_line(line)) >= 0) {
        if (n >= 2 && (line[0] == 'X' || line[0] == 'V') && line[1] == ' ') {
            golden_finish(&g, &nexpr);
            strcpy(g.expr, line + 2);
            g.rc = kx_compile(&g.k, t, g.expr);
            g.refuse = line[0] == 'V';
            g.errs = 0;
            g.keys = 0;
        } else if (n >= 3 && memcmp(line, "ERR", 3) == 0) {
            g.errs++;
        } else if (n >= 1 && line[0] == 'K' && g.expr[0] != '\0') {
            golden_key(&g, t, line, n, &nkeys);
        }
    }
    golden_finish(&g, &nexpr);
    TF_EQ(cf_close(g_gh), 0);
    printf("golden: %lu expressions, %lu keys compared\n",
           (unsigned long)nexpr, (unsigned long)nkeys);
    TF_CHECK(nexpr >= 100);
    TF_CHECK(nkeys >= 1000);
}

/* The directory part of path (up to its last \, / or :), plus name. */
static void beside(char *out, const char *path, const char *name)
{
    size_t n;

    n = strlen(path);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/'
           && path[n - 1] != ':')
        n--;
    if (n + strlen(name) >= sizeof g_golden)
        n = 0;
    memcpy(out, path, n);
    strcpy(out + n, name);
}

int main(int argc, char **argv)
{
    dbf *t;

    if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        tfs_set_dir(argv[2]);
        if (make_fixture(&t) != DBF_OK || dbf_close(t) != DBF_OK) {
            printf("T_KEYEX: cannot write %s\n", tfs_path("KXFIX.DBF", ""));
            return 1;
        }
        return 0;
    }

    tf_begin("T_KEYEX");
    if (argc > 1)
        tfs_set_dir(argv[1]);
    if (argc > 2 && strlen(argv[2]) < sizeof g_golden)
        strcpy(g_golden, argv[2]);
    else
        beside(g_golden, argc > 0 ? argv[0] : "", "KEYEXPR.TXT");
    tfs_wipe(k_scratch);

    if (TF_EQ(make_fixture(&t), DBF_OK)) {
        test_compile(t);
        test_memo_field();
        test_eval(t);
        test_eval32(t);
        test_raw(t);
        test_other_table(t);
        test_golden(t);
        TF_EQ(dbf_close(t), DBF_OK);
        TF_EQ(cf_remove(tfs_path("KXFIX.DBF", "")), 0);
    }
    tfs_wipe(k_scratch);
    return tf_done();
}
