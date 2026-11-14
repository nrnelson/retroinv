/*
 * T_PRINT.C - output jobs (SRC/CLIENT/PRINT.C) over the printer capture
 * file of COMPAT.H.
 *
 * usage: T_PRINT dir
 *
 * dir is the scratch directory ("OBJ/HOST/PRTT/", "D:\").  Printer jobs
 * go to the capture file PRT.CAP in it, file jobs to PRT.TXT, and every
 * job's bytes are compared with what it must send.
 *
 * Checks:
 * - an Epson job sends ESC @ first, ESC C for the form length, style
 *   codes only for the styles that change, bold and double width set and
 *   cleared independently, and control bytes as '?';
 * - a plain job sends no ESC, and a file job holds exactly a plain
 *   printer's bytes, whatever its type, and captures nothing;
 * - text longer than the job's buffer comes out whole;
 * - the wait function: a capture write made to fail calls it with
 *   PRN_EIO, a retry sends exactly the bytes not yet taken, and a cancel
 *   drops the rest of the job, pr_close then giving PR_ECANCEL (the
 *   capture file is written in pieces, so a write fails after some bytes
 *   of the job's buffer were taken, and the retry must start after
 *   them);
 * - a printer that is not there (PRN_LPT2 without capturing, absent on
 *   the test machines) calls the wait function before the job starts,
 *   and a cancel there, or no wait function, makes pr_open fail with
 *   PR_ECANCEL;
 * - a file that cannot be created gives PR_EFILE with the CF_E code, and
 *   so does a failed write, stopping the job;
 * - a PR_NEWFILE job creates a file that is not there, plain whatever
 *   its type, and over one that is gives PR_EFILE with CF_EEXIST,
 *   leaving its bytes as they were, also while another job holds it
 *   open; a close that fails gives PR_EFILE with the CF_E code;
 * - on the host, no memory for the job gives PR_ENOMEM before anything
 *   is created, and holds no memory.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 for a usage error.
 */
#include <stdio.h>
#include <string.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "PRINT.H"
#define TF_MALLOC
#include "TESTFW.H"
#include "T_FILES.H"

#define OUTMAX          1024

static char g_cap[DOS_PATHMAX];
static char g_txt[DOS_PATHMAX];
static u8 g_got[OUTMAX];        /* the bytes read_back read             */
static u16 g_ngot;              /* how many                             */
static int g_waits;             /* calls of the wait function           */
static int g_lastst;            /* the status of the latest             */
static int g_answer;            /* what it returns                      */

/* The contents of path into g_got, g_ngot bytes (none when it cannot be
   read). */
static void read_back(const char *path)
{
    tfs_read_all(path, g_got, OUTMAX, &g_ngot);
}

/* 1 when g_got holds exactly the n bytes of want. */
static int same(const char *want, u16 n)
{
    return g_ngot == n && memcmp(g_got, want, n) == 0;
}

static int wait_fn(void *arg, int status)
{
    TF_CHECK(arg == (void *)&g_waits);
    g_waits++;
    g_lastst = status;
    return g_answer;
}

static pr *start(u8 type)
{
    pr *p;
    int cferr;

    TF_EQ(prn_capture(g_cap), 0);
    g_waits = 0;
    TF_EQ(pr_open(&p, PR_PRINTER, type, PRN_LPT1, NULL, wait_fn, &g_waits,
                  &cferr), PR_OK);
    TF_CHECK(p != NULL);
    return p;
}

static void finish(pr *p, int want)
{
    int cferr;

    cferr = -1;
    TF_EQ(pr_close(p, &cferr), want);
    TF_EQ(cferr, 0);
    TF_EQ(prn_capture(NULL), 0);
    read_back(g_cap);
}

static void test_epson(void)
{
    static const char want[] =
        "\033@\033C\102"
        "\033EHead\r\n"
        "\033F\033W\001BIN\033E\033W\000x\033F\f"
        "a?b?c\344\r\n";
    pr *p;

    p = start(PR_EPSON);
    pr_form(p, 66);
    pr_style(p, PR_BOLD);
    pr_text(p, "Head", 4);
    pr_style(p, PR_BOLD);
    pr_eol(p);
    pr_style(p, PR_WIDE);
    pr_text(p, "BIN", 3);
    pr_style(p, PR_BOLD | PR_WIDE);
    pr_style(p, PR_BOLD);
    pr_text(p, "x", 1);
    pr_style(p, 0);
    pr_style(p, 0);
    pr_ff(p);
    pr_text(p, "a\tb\177c\344", 6);
    pr_eol(p);
    TF_EQ(pr_failed(p), PR_OK);
    finish(p, PR_OK);
    TF_CHECK(same(want, (u16)(sizeof want - 1)));
    TF_EQ(g_waits, 0);
}

static void test_plain(void)
{
    static const char want[] = "Head?\r\nBIN\f";
    pr *p;
    int cferr;

    p = start(PR_PLAIN);
    pr_form(p, 66);
    pr_style(p, PR_BOLD | PR_WIDE);
    pr_text(p, "Head\033", 5);
    pr_eol(p);
    pr_style(p, 0);
    pr_text(p, "BIN", 3);
    pr_ff(p);
    finish(p, PR_OK);
    TF_CHECK(same(want, (u16)(sizeof want - 1)));

    /* A file job is plain whatever its type, and captures nothing. */
    TF_EQ(prn_capture(g_cap), 0);
    TF_EQ(pr_open(&p, PR_FILE, PR_EPSON, PRN_LPT1, g_txt, wait_fn, &g_waits,
                  &cferr), PR_OK);
    TF_CHECK(p != NULL);
    pr_form(p, 66);
    pr_style(p, PR_BOLD);
    pr_text(p, "Head\033", 5);
    pr_eol(p);
    pr_style(p, 0);
    pr_text(p, "BIN", 3);
    pr_ff(p);
    TF_EQ(pr_close(p, &cferr), PR_OK);
    TF_EQ(cferr, 0);
    TF_EQ(prn_capture(NULL), 0);
    read_back(g_txt);
    TF_CHECK(same(want, (u16)(sizeof want - 1)));
    read_back(g_cap);
    TF_EQ(g_ngot, 0);
    cf_remove(g_txt);
}

/* Text of n bytes 'A'.. in turn, longer than the buffer. */
static void fill(char *t, u16 n)
{
    u16 i;

    for (i = 0; i < n; i++)
        t[i] = (char)('A' + i % 26);
}

static void test_long_text(void)
{
    char t[3 * PR_BUFLEN + 7];
    char want[3 * PR_BUFLEN + 16];
    pr *p;

    fill(t, (u16)sizeof t);
    p = start(PR_EPSON);
    pr_text(p, t, (u16)sizeof t);
    pr_eol(p);
    finish(p, PR_OK);
    memcpy(want, "\033@", 2);
    memcpy(want + 2, t, sizeof t);
    memcpy(want + 2 + sizeof t, "\r\n", 2);
    TF_CHECK(same(want, (u16)(sizeof t + 4)));
}

static void test_retry_cancel(void)
{
    char t[2 * PR_BUFLEN + 10];
    char want[2 * PR_BUFLEN + 16];
    pr *p;

    /* The capture file is written in pieces: its second write fails
       twice, after the bytes of the first were taken; both retries are
       taken, each sending from the first byte not taken, and the job
       comes out whole, without a gap or a repeat. */
    fill(t, (u16)sizeof t);
    p = start(PR_EPSON);
    g_answer = 1;
    tfs_fault(TFS_OP(CF_OP_WRITE), 2, 2, CF_EFULL);
    pr_text(p, t, (u16)sizeof t);
    pr_eol(p);
    TF_EQ(pr_failed(p), PR_OK);
    TF_EQ(g_waits, 2);
    TF_EQ(g_lastst, PRN_EIO);
    finish(p, PR_OK);
    tfs_fault_off();
    memcpy(want, "\033@", 2);
    memcpy(want + 2, t, sizeof t);
    memcpy(want + 2 + sizeof t, "\r\n", 2);
    TF_CHECK(same(want, (u16)(sizeof t + 4)));

    /* Cancelled at the second write: the bytes taken before it are
       out, and the rest of the job is dropped. */
    p = start(PR_EPSON);
    g_answer = 0;
    tfs_fault(TFS_OP(CF_OP_WRITE), 2, 1, CF_EFULL);
    pr_text(p, t, (u16)sizeof t);
    TF_EQ(pr_failed(p), PR_ECANCEL);
    TF_EQ(g_waits, 1);
    pr_text(p, "more", 4);
    pr_eol(p);
    pr_ff(p);
    finish(p, PR_ECANCEL);
    tfs_fault_off();
    TF_EQ(g_waits, 1);
    TF_CHECK(g_ngot > 0 && g_ngot < PR_BUFLEN);
    TF_CHECK(memcmp(g_got, want, g_ngot) == 0);

    /* Cancelled in the last send, at pr_close. */
    p = start(PR_PLAIN);
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, CF_EFULL);
    pr_text(p, "x", 1);
    finish(p, PR_ECANCEL);
    tfs_fault_off();
    TF_EQ(g_waits, 1);
    TF_EQ(g_ngot, 0);
}

static void test_no_printer(void)
{
    pr *p;
    int err;
    int cferr;

    cferr = -1;
    g_waits = 0;
    g_answer = 0;
    err = pr_open(&p, PR_PRINTER, PR_EPSON, PRN_LPT2, NULL, wait_fn, &g_waits,
                  &cferr);
    TF_CHECK(p == NULL);
    TF_EQ(err, PR_ECANCEL);
    TF_EQ(cferr, 0);
    TF_EQ(g_waits, 1);
    TF_EQ(g_lastst, PRN_ENODEV);

    /* No wait function: cancelled at once. */
    err = pr_open(&p, PR_PRINTER, PR_EPSON, PRN_LPT2, NULL, NULL, NULL,
                  &cferr);
    TF_CHECK(p == NULL);
    TF_EQ(err, PR_ECANCEL);
    TF_EQ(pr_close(NULL, &cferr), PR_OK);
    TF_EQ(cferr, 0);
}

static void test_file_errors(void)
{
    char t[PR_BUFLEN];
    pr *p;
    int err;
    int cferr;

    cferr = -1;
    tfs_fault(TFS_OP(CF_OP_CREATE), 1, 0, CF_EACCES);
    err = pr_open(&p, PR_FILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr);
    tfs_fault_off();
    TF_CHECK(p == NULL);
    TF_EQ(err, PR_EFILE);
    TF_EQ(cferr, CF_EACCES);

    /* A write that fails stops the job with the file's error. */
    TF_EQ(pr_open(&p, PR_FILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr),
          PR_OK);
    TF_CHECK(p != NULL);
    pr_text(p, "abc", 3);
    pr_eol(p);
    fill(t, PR_BUFLEN);
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, CF_EFULL);
    pr_text(p, t, PR_BUFLEN);
    tfs_fault_off();
    TF_EQ(pr_failed(p), PR_EFILE);
    TF_EQ(pr_close(p, &cferr), PR_EFILE);
    TF_EQ(cferr, CF_EFULL);
    cf_remove(g_txt);
}

static void test_new_file(void)
{
    static const char old[] = "OLD\r\n";
    static const char want[] = "NEW\r\n";
    pr *p;
    pr *q;
    int cferr;

    /* A file there already is refused, its bytes untouched. */
    TF_EQ(tfs_write_all(g_txt, old, (u16)(sizeof old - 1)), 0);
    cferr = -1;
    TF_EQ(pr_open(&p, PR_NEWFILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr),
          PR_EFILE);
    TF_CHECK(p == NULL);
    TF_EQ(cferr, CF_EEXIST);
    read_back(g_txt);
    TF_CHECK(same(old, (u16)(sizeof old - 1)));
    cf_remove(g_txt);

    /* A missing file is created, plain though the type is Epson; a
       second job on the same name, while the first is open, is refused
       and leaves the first's bytes. */
    TF_EQ(pr_open(&p, PR_NEWFILE, PR_EPSON, PRN_LPT1, g_txt, NULL, NULL,
                  &cferr), PR_OK);
    TF_CHECK(p != NULL);
    pr_style(p, PR_BOLD);
    pr_text(p, "NEW", 3);
    pr_eol(p);
    cferr = -1;
    TF_EQ(pr_open(&q, PR_NEWFILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr),
          PR_EFILE);
    TF_CHECK(q == NULL);
    TF_EQ(cferr, CF_EEXIST);
    TF_EQ(pr_close(p, &cferr), PR_OK);
    TF_EQ(cferr, 0);
    read_back(g_txt);
    TF_CHECK(same(want, (u16)(sizeof want - 1)));
    cf_remove(g_txt);

    /* The file is closed at the end, and a close that fails says so. */
    TF_EQ(pr_open(&p, PR_NEWFILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr),
          PR_OK);
    tfs_fault(TFS_OP(CF_OP_CLOSE), 1, 1, CF_EACCES);
    TF_EQ(pr_close(p, &cferr), PR_EFILE);
    tfs_fault_off();
    TF_EQ(cferr, CF_EACCES);
    cf_remove(g_txt);
}

#ifdef COMPAT_HOST
static void test_no_memory(void)
{
    pr *p;
    int cferr;
    u16 held;

    held = tm_held();
    cferr = -1;
    tm_fail(1);
    TF_EQ(pr_open(&p, PR_FILE, PR_PLAIN, 0, g_txt, NULL, NULL, &cferr),
          PR_ENOMEM);
    tm_fail(0);
    TF_CHECK(p == NULL);
    TF_EQ(cferr, 0);
    TF_EQ(cf_exists(g_txt), 0);
    TM_HELD(held);
}
#endif

int main(int argc, char **argv)
{
    tf_begin("T_PRINT");
    if (argc < 2) {
        printf("usage: T_PRINT dir\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    strcpy(g_cap, tfs_path("PRT", ".CAP"));
    strcpy(g_txt, tfs_path("PRT", ".TXT"));
    test_epson();
    test_plain();
    test_long_text();
    test_retry_cancel();
    test_no_printer();
    test_file_errors();
    test_new_file();
#ifdef COMPAT_HOST
    test_no_memory();
#endif
    cf_remove(g_cap);
    return tf_done();
}
