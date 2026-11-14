/*
 * T_PRN.C - printer output through COMPAT.H (SRC/COMPAT/CPRN.C), as a
 * DOS program.
 *
 * usage: T_PRN dir [LPT]
 *
 * A job is the 256 byte values 00h..FFh in order followed by a tag and
 * CR LF: "LPT\r\n" for the job on PRN_LPT1, "PRN\r\n" for the one on
 * PRN_DOS.  dir is the scratch directory ("D:\").
 *
 * Checks:
 * - with dir alone, a write that the fault recorder fails in the
 *   partial mode (cf_fault_part) returns CF_EFULL and leaves exactly
 *   its first cf_fault_part bytes in the file.  Both jobs go to the
 *   capture file T_PRN.CAP in dir (not PRN.CAP: DOS takes PRN with any
 *   extension for the device), which must then hold them exactly, one
 *   after the other.  A write failed by the fault recorder part of the
 *   way through (the capture file is written in pieces), its failed
 *   piece reaching the file in part (cf_fault_part), must report the
 *   bytes taken before it, more than none and fewer than all, and cut
 *   that part off the file again, so that sending the rest from there
 *   gives the job without a gap or a repeat.  The capture keeps its own
 *   copy of the path.  A new capture starts an empty file; one of a
 *   device (NUL, whose handle is closed again) or of a path of
 *   DOS_PATHMAX bytes or more is refused with CF_EINVAL and ends the
 *   capture before it;
 * - with LPT, the two jobs go to the real devices instead: the BIOS
 *   port LPT1 must be present and ready and take every byte, and the
 *   DOS device PRN must take them in raw mode and be left in the mode
 *   it had.  The test machine's printer port writes what it receives to
 *   a host file, which the host compares with the expected bytes: the
 *   same as the capture file above.  A capture of PRN, a device on
 *   MS-DOS 3.10 (DOSBox-X opens PRN as a file), is refused;
 * - without capturing, PRN_LPT2 is absent (PRN_ENODEV), as on the test
 *   machines, and so is a port number above PRN_DOS.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 for a usage error.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define JOBLEN          261     /* 256 byte values, a tag and CR LF     */
#define PIECE           100     /* bytes per prn_write                  */
#define PART            5       /* bytes of a failed write that land    */

static char g_cap[DOS_PATHMAX];
static u8 g_job[JOBLEN];
static u8 g_back[2 * JOBLEN + 1];

static void make_job(const char *tag)
{
    u16 i;

    for (i = 0; i < 256; i++)
        g_job[i] = (u8)i;
    memcpy(g_job + 256, tag, 3);
    g_job[259] = '\r';
    g_job[260] = '\n';
}

/* Send g_job to port in pieces; PRN_OK when every byte was taken. */
static int send_job(u8 port)
{
    u16 off;
    u16 n;
    u16 done;
    int rc;

    rc = prn_open(port);
    if (rc != PRN_OK)
        return rc;
    off = 0;
    while (off < JOBLEN) {
        n = (u16)(JOBLEN - off < PIECE ? JOBLEN - off : PIECE);
        rc = prn_write(g_job + off, n, &done);
        if (rc == PRN_OK)
            TF_EQ(done, n);
        off = (u16)(off + done);
        if (rc != PRN_OK)
            break;
    }
    prn_close();
    return rc;
}

static void test_capture(void)
{
    char path[DOS_PATHMAX];
    u16 got;
    u16 done;
    u16 off;

    strcpy(path, g_cap);
    TF_EQ(prn_capture(path), 0);
    path[0] = '\0';
    make_job("LPT");
    TF_EQ(send_job(PRN_LPT1), PRN_OK);
    make_job("PRN");
    TF_EQ(prn_open(PRN_DOS), PRN_OK);
    TF_EQ(prn_status(), PRN_OK);
    /* The third write fails after its first PART bytes reached the
       file; a truncation must cut them off again. */
    tfs_fault(TFS_OP(CF_OP_WRITE), 3, 1, CF_EFULL);
    cf_fault_part = PART;
    TF_EQ(prn_write(g_job, 10, &done), PRN_OK);
    TF_EQ(done, 10);
    TF_EQ(tfs_count(CF_OP_TRUNC), 0);
    TF_EQ(prn_write(g_job + 10, 50, &done), PRN_EIO);
    TF_CHECK(done > 0 && done < 50);
    TF_EQ(tfs_count(CF_OP_TRUNC), 1);
    off = (u16)(10 + done);
    TF_EQ(prn_write(g_job + off, (u16)(JOBLEN - off), &done), PRN_OK);
    tfs_fault_off();
    cf_fault_part = 0;
    TF_EQ(done, JOBLEN - off);
    prn_close();
    TF_EQ(prn_status(), PRN_ENODEV);
    TF_EQ(prn_write(g_job, 1, &done), PRN_ENODEV);
    TF_EQ(done, 0);
    TF_EQ(prn_capture(NULL), 0);

    TF_EQ(tfs_read_all(g_cap, g_back, (u16)sizeof g_back, &got), 0);
    TF_EQ(got, 2 * JOBLEN);
    TF_CHECK(memcmp(g_back + JOBLEN, g_job, JOBLEN) == 0);
    make_job("LPT");
    TF_CHECK(memcmp(g_back, g_job, JOBLEN) == 0);

    /* A new capture starts an empty file. */
    TF_EQ(prn_capture(g_cap), 0);
    TF_EQ(prn_capture(NULL), 0);
    TF_EQ(tfs_read_all(g_cap, g_back, (u16)sizeof g_back, &got), 0);
    TF_EQ(got, 0);
    cf_remove(g_cap);
}

/* Refused captures: a device, whose handle must be closed again (the
   next one created gets the same number), and a path too long; each
   ends the capture before it. */
static void test_refused(void)
{
    char t[DOS_PATHMAX + 1];
    int h;
    int h2;

    if (!TF_EQ(cf_create(g_cap, &h), 0))
        return;
    cf_close(h);
    TF_EQ(prn_capture(g_cap), 0);
    TF_EQ(prn_capture("NUL"), CF_EINVAL);
    TF_EQ(prn_open(PRN_LPT2), PRN_ENODEV);
    TF_EQ(cf_create(g_cap, &h2), 0);
    cf_close(h2);
    TF_EQ(h2, h);
    memset(t, 'A', DOS_PATHMAX);
    t[DOS_PATHMAX] = '\0';
    TF_EQ(prn_capture(g_cap), 0);
    TF_EQ(prn_capture(t), CF_EINVAL);
    TF_EQ(prn_open(PRN_LPT2), PRN_ENODEV);
    cf_remove(g_cap);
}

/* A write failed in the partial mode returns CF_EFULL with its first
   PART bytes in the file. */
static void test_part(void)
{
    char path[DOS_PATHMAX];
    u32 size;
    int h;

    strcpy(path, tfs_path("T_PRN", ".PRT"));
    if (!TF_EQ(cf_create(path, &h), 0))
        return;
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, CF_EFULL);
    cf_fault_part = PART;
    TF_EQ(cf_write(h, g_job, 10), CF_EFULL);
    tfs_fault_off();
    cf_fault_part = 0;
    TF_EQ(cf_size(h, &size), 0);
    TF_EQ(size, PART);
    cf_close(h);
    cf_remove(path);
}

static void test_absent(void)
{
    TF_EQ(prn_open(PRN_LPT2), PRN_ENODEV);
    TF_EQ(prn_status(), PRN_ENODEV);
    TF_EQ(prn_open(PRN_DOS + 1), PRN_ENODEV);
}

/* PRN's mode now (1 raw), left unchanged. */
static int prn_mode(void)
{
    int was;
    int x;

    was = -1;
    TF_EQ(dos_dev_raw(4, 0, &was), 0);
    TF_EQ(dos_dev_raw(4, was, &x), 0);
    return was;
}

static void test_devices(void)
{
    int mode;

    dos_crit_install();
    make_job("LPT");
    TF_EQ(send_job(PRN_LPT1), PRN_OK);
    make_job("PRN");
    mode = prn_mode();
    TF_EQ(send_job(PRN_DOS), PRN_OK);
    TF_EQ(prn_mode(), mode);
    TF_EQ(prn_capture("PRN"), CF_EINVAL);
    TF_EQ(prn_mode(), mode);
    dos_crit_remove();
}

int main(int argc, char **argv)
{
    tf_begin("T_PRN");
    if (argc < 2) {
        printf("usage: T_PRN dir [LPT]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    strcpy(g_cap, tfs_path("T_PRN", ".CAP"));
    if (argc > 2 && strcmp(argv[2], "LPT") == 0) {
        test_devices();
    } else {
        test_part();
        test_capture();
        test_refused();
    }
    test_absent();
    return tf_done();
}
