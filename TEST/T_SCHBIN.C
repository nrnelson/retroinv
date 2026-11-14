/*
 * T_SCHBIN.C - the test of the schema data a Windows program binds as a
 * resource (SRC/COMPAT/CSCHDAT.H), as the host tool CSCHBIN writes it.
 *
 * usage: T_SCHBIN file
 *
 * The header must give every part of CSCHDAT.H, back to back from the
 * header's end to the file's, with the element size and count the
 * schema has: the field rows of every table, the seed rows, the texts
 * and each key expression.  Each element must hold what the host build
 * of SRC/COMPAT/CSCHEMA.C copies out of the arrays of CSCHDAT.C: every
 * field's name, type, width and decimals, every seed's code and name,
 * every text with its size, every key expression.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGTX.H"
#include "ENGSCH.H"
#include "CSCHDAT.H"
#include "TESTFW.H"

#define BLOBMAX         16384   /* more than the data takes             */

static u8 g_blob[BLOBMAX];
static u16 g_size;

/* Field k (0 offset, 2 element size, 4 count) of part p's entry. */
static u16 entry(int p, int k)
{
    return get_le16(g_blob + p * CSCH_ENTRY + k);
}

/* Element i of part p, which must lie in the file. */
static const u8 *element(int p, u16 i)
{
    u32 at;

    at = entry(p, 0) + (u32)i * entry(p, 2);
    return at < g_size ? g_blob + at : g_blob;
}

/* Part p is count elements of size bytes. */
static void check_part(int p, u16 size, u16 count)
{
    TF_EQ(entry(p, 2), size);
    TF_EQ(entry(p, 4), count);
}

/* The parts lie back to back from the header's end to the file's. */
static void test_layout(void)
{
    u32 at;
    int p;

    at = CSCH_NPART * CSCH_ENTRY;
    for (p = 0; p < CSCH_NPART; p++) {
        TF_EQ(entry(p, 0), at);
        at += (u32)entry(p, 2) * entry(p, 4);
    }
    TF_EQ(g_size, at);
}

/* Every field row of every table. */
static void test_fields(void)
{
    dbf_field fd;
    u16 row;
    u16 f;
    int t;

    row = 0;
    for (t = 0; t < ENG_NTABLES; t++) {
        for (f = 0; f < eng_table_def(t)->nfields; f++, row++) {
            TF_EQ(eng_schema_field(t, f, &fd), ENG_OK);
            TF_STREQ((const char *)element(CSCH_FNAME, row), fd.name);
            TF_EQ(*element(CSCH_FTYPE, row), (u8)fd.type);
            TF_EQ(*element(CSCH_FLEN, row), fd.len);
            TF_EQ(*element(CSCH_FDEC, row), fd.dec);
        }
    }
    check_part(CSCH_FNAME, DBF_NAMELEN + 1, row);
    check_part(CSCH_FTYPE, 1, row);
    check_part(CSCH_FLEN, 1, row);
    check_part(CSCH_FDEC, 1, row);
}

/* Every seed row's code and name. */
static void test_seeds(void)
{
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    u16 sortord;
    u16 i;
    int t;

    for (i = 0; i < eng_seed_rows(); i++) {
        eng_seed_row(i, &t, code, name, &sortord);
        TF_STREQ((const char *)element(CSCH_SCODE, i), code);
        TF_STREQ((const char *)element(CSCH_SNAME, i), name);
    }
    check_part(CSCH_SCODE, ENG_LTYPELEN + 1, eng_seed_rows());
    check_part(CSCH_SNAME, ENG_CODENAMELEN + 1, eng_seed_rows());
}

/* Every text in number order, each the size its entry of CSCH_TXLEN
   gives. */
static void test_texts(void)
{
    char text[ENG_TXMAX + 1];
    const u8 *s;
    u16 total;
    u16 tx;

    s = element(CSCH_TX, 0);
    total = 0;
    for (tx = 1; tx <= ENG_NTX; tx++) {
        eng_tx_copy(tx, text);
        TF_STREQ((const char *)s, text);
        TF_EQ(*element(CSCH_TXLEN, (u16)(tx - 1)), strlen(text) + 1);
        total = (u16)(total + strlen(text) + 1);
        s += strlen(text) + 1;
    }
    check_part(CSCH_TX, total, 1);
    check_part(CSCH_TXLEN, 1, ENG_NTX);
}

/* Every key expression. */
static void test_keys(void)
{
    char expr[NDX_MAXEXPR + 1];
    int x;

    for (x = 0; x < ENG_NINDEX; x++) {
        TF_EQ(eng_index_expr(x, expr, (u16)sizeof expr), ENG_OK);
        TF_STREQ((const char *)element(CSCH_X0 + x, 0), expr);
        check_part(CSCH_X0 + x, 1, (u16)(strlen(expr) + 1));
    }
}

int main(int argc, char **argv)
{
    FILE *f;

    tf_begin("T_SCHBIN");
    if (argc != 2) {
        fputs("usage: T_SCHBIN file\n", stderr);
        return 1;
    }
    f = fopen(argv[1], "rb");
    TF_CHECK(f != NULL);
    if (f != NULL) {
        g_size = (u16)fread(g_blob, 1, sizeof g_blob, f);
        fclose(f);
        TF_CHECK(g_size > CSCH_NPART * CSCH_ENTRY && g_size < BLOBMAX);
        test_layout();
        test_fields();
        test_seeds();
        test_texts();
        test_keys();
    }
    return tf_done();
}
