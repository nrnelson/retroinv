/*
 * CSCHBIN.C - the host tool that writes the schema data of CSCHDAT.C as
 * the file a Windows program binds as its resource CSCH_RES.
 *
 * usage: CSCHBIN out
 *
 * It compiles the arrays of CSCHDAT.C into itself, so that each part's
 * element size and count are those of its array, and writes them in the
 * layout of CSCHDAT.H, which SRC/COMPAT/CSCHEMA.C reads under Windows.
 *
 * Exit status: 0 when the file is written, 1 on a usage error, 2 when
 * it cannot be.
 */
#include "CSCHDAT.C"
#include <stdio.h>
#include "CSCHDAT.H"

/* A part: its first element, the size of one and their number. */
typedef struct {
    const void *p;
    u16 size;
    u16 count;
} part;

#define PART(a)     { a, (u16)sizeof a[0], (u16)(sizeof a / sizeof a[0]) }

#define SCH_TABLE(t, memo, seq, firstx, nindex)
#define SCH_FIELD(t, name, type, len, dec, flags)
#define SCH_END(t)
#define SCH_INDEX(x, t, expr, keylen, unique, dup) , PART(csch_x_##x)
#define SCH_SEED(t, code, name)

/* The parts in CSCHDAT.H's order. */
static const part parts[CSCH_NPART] = {
    PART(csch_fname), PART(csch_ftype), PART(csch_flen), PART(csch_fdec),
    PART(csch_scode), PART(csch_sname),
    { &csch_tx, (u16)sizeof csch_tx, 1 }, PART(csch_txlen)
#include "SCHTAB.H"
};

int main(int argc, char **argv)
{
    u8 head[CSCH_NPART * CSCH_ENTRY];
    u8 *e;
    u16 off;
    FILE *f;
    int ok;
    int k;

    if (argc != 2) {
        fputs("usage: CSCHBIN out\n", stderr);
        return 1;
    }
    off = (u16)sizeof head;
    for (k = 0; k < CSCH_NPART; k++) {
        e = head + k * CSCH_ENTRY;
        put_le16(e, off);
        put_le16(e + 2, parts[k].size);
        put_le16(e + 4, parts[k].count);
        off = (u16)(off + parts[k].size * parts[k].count);
    }
    f = fopen(argv[1], "wb");
    ok = f != NULL && fwrite(head, sizeof head, 1, f) == 1;
    for (k = 0; ok && k < CSCH_NPART; k++)
        ok = fwrite(parts[k].p, parts[k].size, parts[k].count, f) ==
             parts[k].count;
    if (f != NULL && fclose(f) != 0)
        ok = 0;
    if (!ok) {
        fprintf(stderr, "CSCHBIN: cannot write %s\n", argv[1]);
        return 2;
    }
    return 0;
}

#undef SCH_TABLE
#undef SCH_FIELD
#undef SCH_END
#undef SCH_INDEX
#undef SCH_SEED
