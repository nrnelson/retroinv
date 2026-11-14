/*
 * T_WCOMP.C - unit test for the Windows variants of COMPAT.H: far blocks
 * of global memory, the timer of GetTickCount, the handle table of
 * SetHandleCount, the error mode, the yield hook and the printer that
 * WIN.INI names; and for the schema data CSCHEMA.C reads from the
 * program's resource CSCH_RES.
 *
 * usage: WIN T_WCOMP [dir]
 *
 * dir is a scratch directory (default the current one), which gets the
 * files WC00.TMP to WC17.TMP, WCBAD.DRV and WCEPS.OUT for the length of
 * the run.  EPSON9.DRV, the Epson 9-pin printer driver of Windows 3.0,
 * is beside the program.
 * The test makes Windows calls of its own, which portable code may not,
 * so it lives in SRC/COMPAT/ and is built for Windows only (TEST/WTEST.C
 * runs it).
 * GlobalCompact with the most memory asked for makes Windows move every
 * moveable block that is not locked, as real-mode Windows does when it
 * needs room.
 *
 * Checks:
 * - a moveable block of CMEM_MAXBLK bytes has no lasting address
 *   (cmem_ptr NULL) and keeps what cmem_put and cmem_set wrote, and what
 *   cmem_move shifted both ways, across a compaction;
 * - a block of cmem_alloc_perm keeps its address across a compaction,
 *   and its bytes read through that address are those cmem_copy put in
 *   from the moveable block;
 * - a block of cmem_alloc_dos of NB_MSGMAX bytes, a NetBIOS buffer's,
 *   is fixed in every mode, lies wholly below 1 MB (its selector's base,
 *   GetSelectorBase in protected mode) and keeps its address and its
 *   bytes across a compaction;
 * - cf_writeblk and cf_readblk carry a moveable block's bytes through a
 *   file and back into another block;
 * - after dos_set_handles(WC_HANDLES) the files are open at once, more
 *   than DOS's 20 handles, and a smaller table is refused;
 * - after each cmem_ call a cmem_alloc block is unlocked, with the lock
 *   count 0 in real mode, and a cmem_alloc_perm block is fixed in real
 *   mode and moveable in standard and 386 enhanced mode (the count is
 *   exact in real mode only);
 * - cf_readblk and cf_writeblk on a closed handle return CF_EBADF;
 * - tmr_delay(TMR_SEC) waits at least TMR_SEC ticks of GetTickCount
 *   milliseconds and less than twice that, calling dos_yield; tmr_ticks
 *   stays below TMR_DAY and, read between two GetTickCount calls, is the
 *   tick count of their milliseconds at TMR_10SEC ticks per ten seconds,
 *   to a tick either way;
 * - with dos_crit_install in force, an open on the empty floppy drive
 *   of the test machines fails at once with error 53h (fail on INT 24h)
 *   rather than wait for an answer, and dos_crit_remove sets back the
 *   error mode dos_crit_install found;
 * - prn_open is PRN_ENODEV while WIN.INI's device line is empty, and
 *   PRN_EOFFLINE while it names as the driver WCBAD.DRV, a file that is
 *   none; either leaves no job open (prn_write PRN_ENODEV).  A job
 *   prints two double-width characters and two plain ones on a line,
 *   once in one write and once with a write ending between the two wide
 *   ones, and then such a line with the wide ones bold as well, the
 *   styles of a bin label's name.  On the printer the device line named
 *   at the start, the test machine's, which has no double-width font,
 *   all four print one column each, the bold ones struck again (the
 *   run's caller compares what was printed); then, that printer not
 *   remembered, on the Epson 9-pin driver of Windows 3.0, EPSON9.DRV
 *   beside the program, its port the file WCEPS.OUT, the wide ones are
 *   printed in the driver's expanded pitch, bold ones struck twice in
 *   it, and the plain ones after them, the file holding the bytes of an
 *   Epson printer for it; the device line is set back after;
 * - eng_schema_field, eng_index_expr, eng_seed_row and eng_tx_copy give
 *   the fields, key expressions and seed rows of SRC/ENGINE/SCHTAB.H and the
 *   texts of TXTAB.H, ENG_EINTERNAL (eng_seed_row table -1 and empty
 *   texts, eng_tx_copy "") one past the last, ENG_EINTERNAL for a key
 *   expression that does not fit and for a copy eng_schema_fault fails,
 *   and after each call, good or not, leave the resource unlocked (lock
 *   count 0 on the handle LoadResource gives back for it, the reader's);
 *   discarded, it gives the same values again (reloaded).  Before any of
 *   them has found the resource, with dos_inst naming USER, which has no
 *   CSCH_RES, each fails as it fails a copy: eng_schema_field and
 *   eng_index_expr ENG_EINTERNAL, eng_index_expr, eng_seed_row and
 *   eng_tx_copy giving empty texts, eng_seed_row still the seed's table
 *   and sort order; and the next call, dos_inst the program's again,
 *   finds it.
 *
 * Prints each failure and a summary line into T_WCOMP.OUT (WTEST.C);
 * status 0 when every check passes, 1 when one fails.
 */
#define COMPAT_INTERNAL
#define COMPAT_FIND
#include "COMPAT.H"
#include <string.h>
#include "CWIN.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGSCH.H"
#include "ENGTX.H"
#include "CSCHDAT.H"
#include "TESTFW.H"

#define WC_PERMSZ       1024    /* bytes of the block for the run       */
#define WC_FILES        18      /* scratch files open at once           */
/* The table they need beside the five standard handles and T_WCOMP.OUT,
   and one more. */
#define WC_HANDLES      (WC_FILES + 7)
#define WC_STEP         4096u   /* the stride of the pattern checks     */
#define WC_10SECMS      10000UL /* milliseconds in TMR_10SEC ticks      */
#define WC_EFAIL24      0x53    /* CF_EFAIL24 of CDOS.C: INT 24h Fail   */
#define WC_DEVSZ        128     /* WIN.INI's device line and its NUL    */
#define WC_WIDECUT      4       /* bytes of k_wide up to its second
                                   double-width character               */

/* ESC W 1, "AB" in double width, ESC W 0, "CD" and the line's end. */
static const u8 k_wide[] = "\033W\001AB\033W\000CD\r\n";
/* The styles of a bin label's name: ESC E and ESC W 1, "AB" bold and in
   double width, ESC W 0 and ESC F, "CD" and the line's end. */
static const u8 k_label[] = "\033E\033W\001AB\033W\000\033FCD\r\n";
static const char *g_dir = "";
static u16 g_yields;
static HGLOBAL g_sch;           /* the schema resource, as CSCHEMA.C has it */

/* A byte pattern that differs at every position of a 256-byte run. */
static u8 pat(u16 i, u8 seed)
{
    return (u8)((i * 7u + seed) & 0xFFu);
}

/* The ticks of ms milliseconds at TMR_10SEC per ten seconds, whole, as
   the contract of TMR_10SEC gives them (no overflow for any u32). */
static u32 ms_ticks(u32 ms)
{
    return ms / WC_10SECMS * TMR_10SEC +
           ms % WC_10SECMS * TMR_10SEC / WC_10SECMS;
}

/* 1 when Windows runs in real mode. */
static int real_mode(void)
{
    return (GetWinFlags() & WF_PMODE) == 0;
}

/* 1 when b, a cmem_alloc block (perm 0) or a cmem_alloc_perm block
   (perm 1), is in the lock state it has between cmem_ calls. */
static int idle_ok(cmem_blk *b, int perm)
{
    int fixed;
    u16 n;

    n = cmem_locks(b, &fixed);
    if (perm)
        return fixed == real_mode();
    return !fixed && (!real_mode() || n == 0);
}

/* Check both blocks of test_blocks, or only b when p is NULL. */
static void idle(cmem_blk *b, cmem_blk *p)
{
    TF_CHECK(idle_ok(b, 0));
    if (p != NULL)
        TF_CHECK(idle_ok(p, 1));
}

/* Move every moveable block that is not locked. */
static void compact(void)
{
    GlobalCompact((DWORD)-1L);
}

/* Fill n bytes of b at off with the pattern of seed. */
static void put_pat(cmem_blk *b, u16 off, u16 n, u8 seed)
{
    u8 buf[64];
    u16 i;
    u16 k;

    for (i = 0; i < n; i = (u16)(i + k)) {
        for (k = 0; k < sizeof buf && k < n - i; k++)
            buf[k] = pat((u16)(off + i + k), seed);
        cmem_put(b, (u16)(off + i), buf, k);
    }
}

/* 1 when the byte of b at off is the pattern of seed for at. */
static int byte_is(cmem_blk *b, u16 off, u16 at, u8 seed)
{
    u8 c;

    cmem_get(b, off, &c, 1);
    return c == pat(at, seed);
}

/* 1 when n bytes of b at off, sampled every WC_STEP bytes and at both
   ends, hold the pattern of seed for the offsets from at on. */
static int pat_ok(cmem_blk *b, u16 off, u16 n, u16 at, u8 seed)
{
    u16 i;

    for (i = 0; i < n; i = (u16)(i + WC_STEP))
        if (!byte_is(b, (u16)(off + i), (u16)(at + i), seed))
            return 0;
    return byte_is(b, (u16)(off + n - 1), (u16)(at + n - 1), seed);
}

static void test_blocks(void)
{
    cmem_blk *b;
    cmem_blk *p;
    FARP(u8) at;
    u8 c;
    u16 i;

    b = cmem_alloc(CMEM_MAXBLK);
    p = cmem_alloc_perm(WC_PERMSZ);
    if (!TF_CHECK(b != NULL && p != NULL)) {
        cmem_free(b);
        cmem_free(p);
        return;
    }
    idle(b, p);
    TF_CHECK(cmem_ptr(b) == NULL);
    at = cmem_ptr(p);
    TF_CHECK(at != NULL);
    put_pat(b, 0, CMEM_MAXBLK, 1);
    idle(b, p);
    compact();
    TF_CHECK(pat_ok(b, 0, CMEM_MAXBLK, 0, 1));
    idle(b, p);
    cmem_set(b, 0, 0x5A, 16);
    idle(b, p);
    cmem_get(b, 15, &c, 1);
    idle(b, p);
    TF_EQ(c, 0x5A);
    TF_CHECK(byte_is(b, 16, 16, 1));
    cmem_move(b, 100, 200, 1000);
    idle(b, p);
    cmem_move(b, 3000, 2000, 2000);
    idle(b, p);
    compact();
    TF_CHECK(pat_ok(b, 100, 1000, 200, 1));
    TF_CHECK(pat_ok(b, 3000, 2000, 2000, 1));
    cmem_copy(p, 0, b, 8192, WC_PERMSZ);
    idle(b, p);
    compact();
    TF_CHECK(cmem_ptr(p) == at);
    for (i = 0; i < WC_PERMSZ && at != NULL &&
         at[i] == pat((u16)(8192 + i), 1); i++)
        ;
    TF_EQ(i, WC_PERMSZ);
    cmem_free(p);
    cmem_free(b);
}

/* The linear address of the far pointer p. */
static u32 linear(FARP(u8) p)
{
    if (real_mode())
        return ((u32)SELECTOROF(p) << 4) + OFFSETOF(p);
    return GetSelectorBase(SELECTOROF(p)) + OFFSETOF(p);
}

static void test_dos(void)
{
    cmem_blk *d;
    FARP(u8) at;
    int fixed;
    u16 i;

    d = cmem_alloc_dos(NB_MSGMAX);
    if (!TF_CHECK(d != NULL))
        return;
    at = cmem_ptr(d);
    TF_CHECK(at != NULL);
    cmem_locks(d, &fixed);
    TF_CHECK(fixed);
    TF_CHECK(at != NULL && linear(at) + NB_MSGMAX <= 0x100000UL);
    put_pat(d, 0, NB_MSGMAX, 2);
    compact();
    TF_CHECK(cmem_ptr(d) == at);
    for (i = 0; i < NB_MSGMAX && at != NULL && at[i] == pat(i, 2); i++)
        ;
    TF_EQ(i, NB_MSGMAX);
    cmem_free(d);
}

/* The path of scratch file i into path (DOS_PATHMAX bytes). */
static void file_name(char *path, u16 i)
{
    static const char k_name[] = "WC00.TMP";
    size_t n;

    txt_copy(path, g_dir, (u16)(DOS_PATHMAX - sizeof k_name + 1));
    n = strlen(path);
    strcpy(path + n, k_name);
    txt_u32w(path + n + 2, i, 2, '0');
}

static void test_files(void)
{
    char path[DOS_PATHMAX];
    int h[WC_FILES];
    cmem_blk *a;
    cmem_blk *b;
    u16 got;
    u16 i;
    u16 n;

    TF_EQ(dos_set_handles(WC_HANDLES), 0);
    TF_EQ(dos_set_handles(WC_HANDLES - 1), CF_EINVAL);
    for (i = 0; i < WC_FILES; i++) {
        file_name(path, i);
        if (!TF_EQ(cf_create(path, &h[i]), 0))
            break;
    }
    n = i;
    a = cmem_alloc(CMEM_MAXBLK);
    b = cmem_alloc(CMEM_MAXBLK);
    if (!TF_CHECK(a != NULL && b != NULL))
        n = 0;
    if (n > 0) {
        put_pat(a, 0, CMEM_MAXBLK, 2);
        idle(a, NULL);
        TF_EQ(cf_writeblk(h[0], a, 0, CMEM_MAXBLK), 0);
        idle(a, NULL);
        TF_EQ(cf_seek(h[0], 0), 0);
        TF_EQ(cf_readblk(h[0], b, 0, CMEM_MAXBLK, &got), 0);
        idle(b, NULL);
        TF_EQ(got, CMEM_MAXBLK);
        compact();
        TF_CHECK(pat_ok(b, 0, CMEM_MAXBLK, 0, 2));
    }
    while (i-- > 0) {
        TF_EQ(cf_close(h[i]), 0);
        file_name(path, i);
        TF_EQ(cf_remove(path), 0);
    }
    if (n > 0) {
        TF_EQ(cf_readblk(h[0], b, 0, 1, &got), CF_EBADF);
        TF_EQ(cf_writeblk(h[0], a, 0, 1), CF_EBADF);
        idle(a, NULL);
        idle(b, NULL);
    }
    cmem_free(b);
    cmem_free(a);
}

static void count_yield(void)
{
    g_yields++;
}

static void test_timer(void)
{
    u32 ms;
    u32 lo;
    u32 hi;
    u32 t;
    u32 span;

    lo = ms_ticks(GetTickCount());
    t = tmr_ticks();
    hi = ms_ticks(GetTickCount());
    TF_CHECK(t < TMR_DAY);
    /* The ticks since lo - 1, taken from midnight to midnight, are at
       most those of the bracket and a tick either way. */
    lo = (lo + TMR_DAY - 1) % TMR_DAY;
    hi = (hi + 1) % TMR_DAY;
    span = (hi + TMR_DAY - lo) % TMR_DAY;
    TF_CHECK((t + TMR_DAY - lo) % TMR_DAY <= span);
    dos_yield = count_yield;
    ms = GetTickCount();
    tmr_delay(TMR_SEC);
    ms = GetTickCount() - ms;
    dos_yield = NULL;
    TF_CHECK(ms >= (u32)TMR_SEC * WC_10SECMS / TMR_10SEC);
    TF_CHECK(ms < (u32)TMR_SEC * 2 * WC_10SECMS / TMR_10SEC);
    TF_CHECK(g_yields > 0);
    TF_CHECK(tmr_ticks() < TMR_DAY);
}

static void test_crit(void)
{
    UINT before;
    int h;

    before = SetErrorMode(0);
    SetErrorMode(before);
    dos_crit_install();
    TF_EQ(cf_open("A:\\T_WCOMP.TMP", CF_READ, &h), WC_EFAIL24);
    dos_crit_remove();
    TF_EQ(SetErrorMode(0), before);
    SetErrorMode(before);
}

/* Set WIN.INI's device line to dev, then check that prn_open gives
   want and leaves no job open. */
static void prn_with(const char *dev, int want)
{
    u16 done;

    WriteProfileString("windows", "device", dev);
    TF_EQ(prn_open(PRN_LPT1), want);
    TF_EQ(prn_write((const u8 *)"\r", 1, &done), PRN_ENODEV);
}

/* Send the n bytes at p to the job open. */
static void prn_part(const u8 *p, u16 n)
{
    u16 done;

    TF_EQ(prn_write(p, n, &done), PRN_OK);
    TF_EQ(done, n);
}

/* Print, on the printer of WIN.INI's device line dev, the line k_wide
   twice, in one write and with a write ending between its two
   double-width characters, and then the line k_label. */
static void prn_job(const char *dev)
{
    WriteProfileString("windows", "device", dev);
    if (TF_EQ(prn_open(PRN_LPT1), PRN_OK)) {
        prn_part(k_wide, (u16)(sizeof k_wide - 1));
        prn_part(k_wide, WC_WIDECUT);
        prn_part(k_wide + WC_WIDECUT,
                 (u16)(sizeof k_wide - 1 - WC_WIDECUT));
        prn_part(k_label, (u16)(sizeof k_label - 1));
        prn_close();
    }
}

static void test_prn(void)
{
    static const char k_drv[] = "WCBAD";
    static const char k_port[] = ",LPT1:";
    static const char k_text[] = "not a driver";
    static const char k_eps[] = "Epson 9 pin,EPSON9,";
    static const char k_out[] = "WCEPS.OUT";
    /* prn_job as the Epson driver writes it: the job's start, each line
       with the expanded pitch (ESC ! 20h) on for "AB" and off for "CD",
       which follows it at once, the bold "AB" of the third struck twice
       in that pitch, the feed to the next line (ESC J), the page's end
       and the job's. */
    static const u8 k_job[] =
        "\033r\0\0336\r\033U\0\033\031\0\0332\033CB\033!\0\033p\0\033QP"
        "\033! \033p\0AB\033!\0\033p\0CD\r\033J$"
        "\033! \033p\0AB\033!\0\033p\0CD\r\033J$"
        "\033! \033p\0AB\rAB\033!\0\033p\0CD\f\0332\033P\022";
    char old[WC_DEVSZ];
    char dev[WC_DEVSZ];
    char path[WC_DEVSZ];
    u8 got[sizeof k_job];
    u16 n;
    int h;

    GetProfileString("windows", "device", "", old, (int)sizeof old);
    prn_with("", PRN_ENODEV);
    /* The device line "WCBAD,<dir>WCBAD,LPT1:", GDI adding .DRV. */
    txt_copy(path, g_dir, (u16)(WC_DEVSZ - 2 * sizeof k_drv - sizeof k_port));
    strcat(path, k_drv);
    strcpy(dev, k_drv);
    strcat(dev, ",");
    strcat(dev, path);
    strcat(dev, k_port);
    strcat(path, ".DRV");
    if (TF_EQ(cf_create(path, &h), 0)) {
        TF_EQ(cf_write(h, k_text, (u16)(sizeof k_text - 1)), 0);
        TF_EQ(cf_close(h), 0);
        prn_with(dev, PRN_EOFFLINE);
        TF_EQ(cf_remove(path), 0);
    }
    /* The device line "Epson 9 pin,EPSON9,<dir>WCEPS.OUT": Windows finds
       the driver beside the program, and its port is a file.  Its job
       comes after one on a printer without a double-width font, which
       must not be remembered. */
    txt_copy(path, g_dir, (u16)(WC_DEVSZ - sizeof k_eps - sizeof k_out));
    strcat(path, k_out);
    strcpy(dev, k_eps);
    strcat(dev, path);
    prn_job(old);
    prn_job(dev);
    if (TF_EQ(cf_open(path, CF_READ, &h), 0)) {
        TF_EQ(cf_read(h, got, (u16)sizeof got, &n), 0);
        TF_EQ(n, sizeof k_job - 1);
        TF_CHECK(memcmp(got, k_job, sizeof k_job - 1) == 0);
        TF_EQ(cf_close(h), 0);
        TF_EQ(cf_remove(path), 0);
    }
    WriteProfileString("windows", "device", old);
}

/* 1 when the schema resource is not locked. */
static int sch_idle(void)
{
    return (GlobalFlags(g_sch) & GMEM_LOCKCOUNT) == 0;
}

static void field_is(int t, u16 f, const char *name, char type, u8 len,
                     u8 dec)
{
    dbf_field d;

    if (TF_EQ(eng_schema_field(t, f, &d), ENG_OK)) {
        TF_STREQ(d.name, name);
        TF_EQ(d.type, type);
        TF_EQ(d.len, len);
        TF_EQ(d.dec, dec);
    }
    TF_CHECK(sch_idle());
}

static void expr_is(int x, const char *want)
{
    char e[NDX_MAXEXPR + 1];

    if (TF_EQ(eng_index_expr(x, e, (u16)sizeof e), ENG_OK))
        TF_STREQ(e, want);
    TF_CHECK(sch_idle());
}

static void seed_is(u16 i, int t, const char *code, const char *name,
                    u16 sortord)
{
    char c[ENG_LTYPELEN + 1];
    char n[ENG_CODENAMELEN + 1];
    int gt;
    u16 gs;

    c[0] = n[0] = '?';
    c[1] = n[1] = '\0';
    eng_seed_row(i, &gt, c, n, &gs);
    TF_EQ(gt, t);
    TF_STREQ(c, code);
    TF_STREQ(n, name);
    TF_EQ(gs, sortord);
    TF_CHECK(sch_idle());
}

/* Text tx into out (ENG_TXMAX + 1 bytes), checked against want unless
   want is NULL. */
static void tx_is(u16 tx, char *out, const char *want)
{
    out[0] = '?';
    out[1] = '\0';
    eng_tx_copy(tx, out);
    if (want != NULL)
        TF_STREQ(out, want);
    TF_CHECK(sch_idle());
}

static int fail_copy(void)
{
    return 1;
}

/* Every read of test_schema, values from SCHTAB.H and TXTAB.H. */
static void sch_reads(void)
{
    char e[NDX_MAXEXPR + 1];
    char s[ENG_TXMAX + 1];
    dbf_field d;
    u16 n;

    n = eng_table_def(ENG_T_ITEMS)->nfields;
    field_is(ENG_T_ITEMS, ENG_FIELD(ITEMS, ITEM_ID), "ITEM_ID", 'N', 8, 0);
    field_is(ENG_T_ITEMS, ENG_FIELD(ITEMS, ACQ_PRICE), "ACQ_PRICE", 'N', 9,
             2);
    field_is(ENG_T_ITEMS, (u16)(n - 1), "NOTES", 'M', DBF_MEMOLEN, 0);
    field_is(ENG_T_CATS, ENG_FIELD(CATS, CAT_NAME), "CAT_NAME", 'C',
             ENG_CODENAMELEN, 0);
    TF_EQ(eng_schema_field(ENG_T_ITEMS, n, &d), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    TF_EQ(eng_schema_field(ENG_T_SEQ, eng_table_def(ENG_T_SEQ)->nfields,
                           &d), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    TF_EQ(eng_schema_field(ENG_NTABLES, 0, &d), ENG_EINTERNAL);
    TF_CHECK(sch_idle());

    expr_is(ENG_X_ITEMS1, "STR(ITEM_ID,8)");
    expr_is(ENG_X_USERS1, "USERNAME");
    TF_EQ(ENG_X_USERS1, ENG_NINDEX - 1);
    TF_EQ(eng_index_expr(ENG_NINDEX, e, (u16)sizeof e), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    TF_EQ(eng_index_expr(ENG_X_LOCS3, e, 8), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    if (TF_EQ(eng_index_expr(ENG_X_LOCS3, e, 9), ENG_OK))
        TF_STREQ(e, "LOC_CODE");
    TF_CHECK(sch_idle());

    n = eng_seed_rows();
    TF_EQ(n, 18);
    seed_is(0, ENG_T_CATS, "SYS", "Systems", 1);
    seed_is(9, ENG_T_CATS, "PART", "Parts", 10);
    seed_is((u16)(n - 1), ENG_T_LTYPES, "BUCKET", "Bucket", 8);
    seed_is(n, -1, "", "", 0);

    tx_is(ENG_TX_CYCLE, s, "cannot be put inside itself or its contents");
    tx_is(ENG_TX_LONGLINE, s, ENG_TXT_LONGLINE);
    tx_is(ENG_TX_WORKS, s, "works must be Y, N, I or ?");
    tx_is(ENG_NTX, s, NULL);
    TF_CHECK(s[0] != '\0' && s[0] != '?');
    tx_is(ENG_NTX + 1, s, "");
    tx_is(ENG_TX_NONE, s, "");

    eng_schema_fault = fail_copy;
    TF_EQ(eng_index_expr(ENG_X_ITEMS1, e, (u16)sizeof e), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    TF_EQ(eng_schema_field(ENG_T_ITEMS, 0, &d), ENG_EINTERNAL);
    TF_CHECK(sch_idle());
    eng_schema_fault = NULL;
}

/* Each schema read while dos_inst is an instance whose module has no
   CSCH_RES. */
static void sch_unfound(void)
{
    char e[NDX_MAXEXPR + 1];
    char c[ENG_LTYPELEN + 1];
    char n[ENG_CODENAMELEN + 1];
    char s[ENG_TXMAX + 1];
    dbf_field d;
    u16 inst;
    int t;
    u16 ord;

    inst = dos_inst;
    dos_inst = (u16)GetModuleHandle("USER");
    TF_EQ(eng_schema_field(ENG_T_ITEMS, 0, &d), ENG_EINTERNAL);
    e[0] = c[0] = n[0] = s[0] = '?';
    e[1] = c[1] = n[1] = s[1] = '\0';
    TF_EQ(eng_index_expr(ENG_X_ITEMS1, e, (u16)sizeof e), ENG_EINTERNAL);
    TF_STREQ(e, "");
    eng_seed_row(0, &t, c, n, &ord);
    TF_EQ(t, ENG_T_CATS);
    TF_STREQ(c, "");
    TF_STREQ(n, "");
    TF_EQ(ord, 1);
    eng_tx_copy(ENG_TX_CYCLE, s);
    TF_STREQ(s, "");
    dos_inst = inst;
}

static void test_schema(void)
{
    HINSTANCE inst;
    HRSRC r;
    char s[ENG_TXMAX + 1];

    sch_unfound();
    eng_tx_copy(ENG_TX_CYCLE, s);
    TF_STREQ(s, "cannot be put inside itself or its contents");
    inst = (HINSTANCE)dos_inst;
    r = FindResource(inst, MAKEINTRESOURCE(CSCH_RES),
                     MAKEINTRESOURCE(CSCH_TYPE));
    g_sch = r != NULL ? LoadResource(inst, r) : NULL;
    if (!TF_CHECK(g_sch != NULL))
        return;
    sch_reads();
    TF_CHECK(GlobalReAlloc(g_sch, 0, GMEM_MOVEABLE) == g_sch);
    TF_CHECK((GlobalFlags(g_sch) & GMEM_DISCARDED) != 0);
    sch_reads();
    FreeResource(g_sch);
}

int main(int argc, char **argv)
{
    tf_begin("T_WCOMP");
    if (argc > 1)
        g_dir = argv[1];
    test_blocks();
    test_dos();
    test_files();
    test_timer();
    test_crit();
    test_prn();
    test_schema();
    return tf_done();
}
