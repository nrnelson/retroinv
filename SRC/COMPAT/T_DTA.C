/*
 * T_DTA.C - unit test for cf_exists, dos_find_first and dos_find_next
 * of COMPAT.H under DOS: a find-first walk held in the program's own
 * disk transfer area (DTA) comes through them unchanged, and a
 * directory counts as absent; and for the answers of the handler
 * dos_crit_install sets.
 *
 * usage: T_DTA [dir]
 *
 * dir is the scratch directory (default the current one; a drive root
 * in the DOS runs).  The test makes DOS calls of its own (set and get
 * the DTA, find first and find next) and issues INT 24h, which portable
 * code may not, so it lives in SRC/COMPAT/ and is built for DOS only.
 *
 * Checks:
 * - cf_exists of a directory made in dir gives 0, of a file in it 1;
 * - a find-first/find-next walk (functions 4Eh and 4Fh, DOS 2.0) in the
 *   program's DTA (function 1Ah) over DT_NFILES files finds each file
 *   once and ends with "no more files";
 * - between the walk's steps, cf_exists gives 1 for one of the files
 *   and 0 for a missing file, a name in a missing directory and the
 *   directory, and a whole dos_find search of the same pattern gives
 *   each file once; after those calls the DTA (function 2Fh) is still
 *   the program's own and holds every byte it held before;
 * - dos_volume_label reads the current drive's label; when it has none,
 *   one made by an extended FCB (functions 16h and 10h, DOS 1.0), as
 *   DOS 3's LABEL makes it, ten characters with a space, comes back as
 *   it was made, and "" once it is deleted (function 13h); the DTA is
 *   still the program's own and unchanged afterwards;
 * - INT 24h, issued directly after dos_crit_install, answers Fail
 *   (AL = 3) when AH allows Fail (bit 3) and Retry (AL = 1) when it
 *   does not, and returns AH, BX, CX, DX, SI and DI unchanged.
 *
 * The files and the directory are removed at the start and at the end.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when the directory name is too long.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include "COMPAT.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "TESTFW.H"
#include "T_FILES.H"

#define DT_DTASIZE      43      /* bytes of a find-first DTA            */
#define DT_NAME         30      /* offset of the found name in it       */
#define DT_NFILES       3       /* files the walk finds                 */
#define DT_ENOMORE      18      /* DOS error: no more files             */
#define DT_FAILOK       0x08    /* INT 24h AH bit: Fail allowed         */
#define DT_XFCB         44      /* bytes of an extended FCB             */
#define DT_XFCBATTR     6       /* offset of its attribute              */
#define DT_XFCBNAME     8       /* offset of its 11-character name      */

/* The walk's files, found by the pattern "DTA?.TMP", the directory and
   the file in it.  The directory name matches no file pattern. */
static const char *const k_files[] = {
    "DTA1.TMP", "DTA2.TMP", "DTA3.TMP", NULL
};
static const char k_dir[] = "DTADIR";
static const char k_inner[] = "DTADIR\\IN.TMP";

/* What cf_exists must give for each name between the walk's steps. */
static const struct {
    const char *name;
    int want;
} k_probes[] = {
    { "DTA2.TMP", 1 },
    { "DTANO.TMP", 0 },
    { "DTANODIR\\X.TMP", 0 },
    { "DTADIR", 0 }
};
#define DT_NPROBES      (sizeof k_probes / sizeof k_probes[0])

static u8 g_dta[DT_DTASIZE];

/* DOS function ah with DS:DX the near address dx; 0 or the DOS error
   code.  r and s are left as DOS returned them. */
static int int21(u8 ah, const void *dx, union REGS *r, struct SREGS *s)
{
    memset(r, 0, sizeof *r);
    r->h.ah = ah;
    r->x.dx = (unsigned)dx;
    segread(s);
    intdosx(r, r, s);
    return r->x.cflag ? (int)r->x.ax : 0;
}

/* 1 when the DTA (function 2Fh, ES:BX) is g_dta. */
static int dta_is_mine(void)
{
    union REGS r;
    struct SREGS s;
    u16 ds;

    segread(&s);
    ds = (u16)s.ds;
    int21(0x2F, NULL, &r, &s);
    return (u16)s.es == ds && r.x.bx == (unsigned)g_dta;
}

/* Create a 0-byte file at path. */
static void make_file(const char *path)
{
    int h;

    if (TF_EQ(cf_create(path, &h), 0))
        TF_EQ(cf_close(h), 0);
}

/* Remove the files, the file in the directory and the directory, any of
   them missing. */
static void clean(void)
{
    tfs_wipe(k_files);
    cf_remove(tfs_path(k_inner, ""));
    cf_rmdir(tfs_path(k_dir, ""));
}

/* The files of k_files a dos_find search of pat gives, a bit each, or
   -1 when it gives one twice or another name. */
static int search(const char *pat)
{
    dos_find f;
    char name[DOS_FINDNAME];
    int seen;
    int i;
    int rc;

    seen = 0;
    for (rc = dos_find_first(&f, pat, name); rc == 0;
         rc = dos_find_next(&f, name)) {
        for (i = 0; k_files[i] != NULL; i++)
            if (strcmp(name, k_files[i]) == 0)
                break;
        if (k_files[i] == NULL || (seen & (1 << i)) != 0)
            return -1;
        seen |= 1 << i;
    }
    TF_EQ(rc, CF_ENOENT);
    return seen;
}

static void test_dir(void)
{
    TF_EQ(cf_mkdir(tfs_path(k_dir, "")), 0);
    make_file(tfs_path(k_inner, ""));
    TF_EQ(cf_exists(tfs_path(k_dir, "")), 0);
    TF_EQ(cf_exists(tfs_path(k_inner, "")), 1);
}

static void test_walk(void)
{
    union REGS r;
    struct SREGS s;
    char pat[DOS_PATHMAX];
    char probe[DT_NPROBES][DOS_PATHMAX];
    u8 before[DT_DTASIZE];
    int seen;
    int n;
    int i;
    int rc;

    for (i = 0; k_files[i] != NULL; i++)
        make_file(tfs_path(k_files[i], ""));
    strcpy(pat, tfs_path("DTA?", ".TMP"));
    for (i = 0; i < (int)DT_NPROBES; i++)
        strcpy(probe[i], tfs_path(k_probes[i].name, ""));
    int21(0x1A, g_dta, &r, &s);
    seen = 0;
    n = 0;
    rc = int21(0x4E, pat, &r, &s);
    while (rc == 0 && n <= DT_NFILES) {
        n++;
        for (i = 0; k_files[i] != NULL; i++) {
            if (strcmp((const char *)g_dta + DT_NAME, k_files[i]) == 0)
                seen |= 1 << i;
        }
        memcpy(before, g_dta, sizeof before);
        for (i = 0; i < (int)DT_NPROBES; i++)
            TF_EQ(cf_exists(probe[i]), k_probes[i].want);
        TF_EQ(search(pat), (1 << DT_NFILES) - 1);
        TF_CHECK(dta_is_mine());
        TF_CHECK(memcmp(g_dta, before, sizeof before) == 0);
        rc = int21(0x4F, NULL, &r, &s);
    }
    TF_EQ(rc, DT_ENOMORE);
    TF_EQ(n, DT_NFILES);
    TF_EQ(seen, (1 << DT_NFILES) - 1);
}

/* The label test_label makes, as the 11 bytes of a directory entry,
   and as dos_volume_label gives it back. */
static const char k_label11[] = "RETRO INV1 ";
static const char k_label[] = "RETRO INV1";

/* DOS function ah on an extended FCB for the volume label of the
   current drive named name (11 bytes): AL, 0 when DOS did it. */
static int label_fcb(u8 ah, const char *name)
{
    union REGS r;
    struct SREGS s;
    u8 x[DT_XFCB];

    memset(x, 0, sizeof x);
    x[0] = 0xFF;
    x[DT_XFCBATTR] = 0x08;
    memcpy(x + DT_XFCBNAME, name, 11);
    int21(ah, x, &r, &s);
    if (ah == 0x16 && r.h.al == 0)
        int21(0x10, x, &r, &s);
    return r.h.al;
}

static void test_label(void)
{
    union REGS r;
    struct SREGS s;
    char label[DOS_LABELLEN + 1];
    u8 before[DT_DTASIZE];

    int21(0x1A, g_dta, &r, &s);
    memcpy(before, g_dta, sizeof before);
    TF_EQ(dos_volume_label(0, label), 0);
    if (label[0] == '\0') {
        TF_EQ(label_fcb(0x16, k_label11), 0);
        TF_EQ(dos_volume_label(0, label), 0);
        TF_STREQ(label, k_label);
        TF_EQ(label_fcb(0x13, "???????????"), 0);
        TF_EQ(dos_volume_label(0, label), 0);
        TF_STREQ(label, "");
    }
    TF_CHECK(dta_is_mine());
    TF_CHECK(memcmp(g_dta, before, sizeof before) == 0);
}

/* The critical-error handler's answer to INT 24h with AH = ah, the
   other registers given patterns that must come back unchanged. */
static int crit_answer(u8 ah)
{
    union REGS r;
    union REGS o;

    r.h.ah = ah;
    r.h.al = 0;
    r.x.bx = 0x1234;
    r.x.cx = 0x5678;
    r.x.dx = 0x9ABC;
    r.x.si = 0xDEF0;
    r.x.di = 0x0FED;
    int86(0x24, &r, &o);
    TF_EQ(o.h.ah, ah);
    TF_CHECK(o.x.bx == r.x.bx && o.x.cx == r.x.cx && o.x.dx == r.x.dx &&
             o.x.si == r.x.si && o.x.di == r.x.di);
    return o.h.al;
}

static void test_crit(void)
{
    dos_crit_install();
    TF_EQ(crit_answer(DT_FAILOK), 3);
    TF_EQ(crit_answer(0), 1);
    dos_crit_remove();
}

int main(int argc, char **argv)
{
    tf_begin("T_DTA");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    clean();
    test_dir();
    test_walk();
    clean();
    test_label();
    test_crit();
    return tf_done();
}
