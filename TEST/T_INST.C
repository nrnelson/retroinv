/*
 * T_INST.C - unit test for INSTPLAN.C, the installer's decisions, on the
 * host.
 *
 * usage: T_INST dir
 *
 * dir is the scratch directory.  A set of two disks is a directory per
 * disk; the one in the drive is SRC, which the test's answer to a disk
 * question swaps for the disk it then means, setting dos_label_host,
 * the host's volume label, to that disk's.
 *
 * Checks:
 * - a list is read with its set, labels, disk and files, the data's
 *   count of files too, Z taken off the roles as the mark of a
 *   compressed file, and a list that breaks the format in any field,
 *   or is too long, is refused;
 * - the roles a set can install come from its programs, the installer
 *   itself aside, and the network-only client's name from its list;
 * - a target directory must be a whole path below a drive's root;
 * - a server with the Windows client and the sample data gets exactly
 *   the files of its roles, the disk in the drive first, then the second
 *   disk, asked for until the drive holds the disk of that label whose
 *   list names the same set;
 * - the room a plan needs counts whole clusters (dos_cluster_host), a
 *   cluster more per data file and one for the data directory, and
 *   none for the files the copy replaces; a drive one byte short
 *   (dos_free_host) is refused;
 * - leaving before the sample data's disk, or a failed copy of it,
 *   leaves no data directory, an empty one is no data, and a new run
 *   copies the sample;
 * - the further disks are asked for in the set's order;
 * - a settings file the target holds is kept unless the user says to
 *   replace it, and leaving at that question is INS_ECANCEL;
 * - a write that fails leaves no part of its file, and names it;
 * - leaving at the disk question keeps what was copied;
 * - nothing is copied when the source is the target;
 * - a file marked compressed and held under its compressed name, and
 *   the files of the sample data's compressed directory, are expanded,
 *   a plain file longer than the copy's buffer copied whole, a stale
 *   file under the own name and a stale data directory beside them
 *   passed over; from an installation, which holds them as they are,
 *   they are copied; a damaged one, or a disk that fills during an
 *   expansion, leaves no part of its file, the damaged one named by its
 *   source; installing into the directory that holds them compressed is
 *   INS_EPACKED;
 * - the removal asks about the data first, leaving there removes
 *   nothing, then removes the list's files, the data only when told to,
 *   and the directory only when it is empty.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_FIND
#define COMPAT_LABEL
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "INSTPLAN.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define TI_MAXQ         8       /* questions and notices recorded       */

/* The set's two disks, a disk of another set with the second's label,
   and each disk's files. */
static const char k_list1[] =
    "R 1\r\n360 LA1 LA2\r\n1\r\n"
    "SVNT 12 40 INSTALL.LST\r\nSVNT 12 5 README.TXT\r\nSVN 1 4 INV.EXE\r\n"
    "T 2 4 THIN.EXE\r\nV 2 4 SERV.EXE\r\nVK 2 3 SERV.CFG\r\nW 2 3 WIN.EXE\r\n"
    "D 1 6,2 DATA\r\n";
static const char k_list2[] =
    "R 1\r\n360 LA1 LA2\r\n2\r\n"
    "SVNT 12 40 INSTALL.LST\r\nSVNT 12 5 README.TXT\r\nSVN 1 4 INV.EXE\r\n"
    "T 2 4 THIN.EXE\r\nV 2 4 SERV.EXE\r\nVK 2 3 SERV.CFG\r\n"
    "W 2 3 WIN.EXE\r\nD 1 6,2 DATA\r\n";
static const char k_listx[] = "R 1\n720 LA1 LA2\n2\nSVNT 1 5 README.TXT\n";
static const char *const k_disk1[] = { "README.TXT", "INV.EXE", NULL };
static const char *const k_disk2[] = {
    "README.TXT", "THIN.EXE", "SERV.EXE", "SERV.CFG", "WIN.EXE", NULL
};
static const char *const k_data[] = { "A.DBF", "B.DBF", NULL };

/* A set of three disks, the list's disk number filled in, whose third
   disk's file comes before its second's. */
static const char k_list3[] =
    "R 1\r\n360 LA1 LA2 LA3\r\n%d\r\nSVNT 123 1 INSTALL.LST\r\n"
    "SVNT 123 1 README.TXT\r\nV 3 1 C.EXE\r\nV 2 1 B.EXE\r\n";
static const char *const k_disk3[][2] = {
    { "README.TXT", NULL }, { "B.EXE", NULL }, { "C.EXE", NULL }
};

/* A set of one disk whose program and sample data are compressed, and
   the bytes of its files. */
static const char k_listz[] =
    "R 1\r\n360 LA1\r\n1\r\nSVNT 1 99 INSTALL.LST\r\nSZ 1 5401 INV.EXE\r\n"
    "S 1 5000 README.TXT\r\nDZ 1 18,2 DATA\r\n";
static const u8 k_abc[] = { 0x07, 'A', 'B', 'C', 0xF0, 0xF3 };
#define TI_ABC          "ABCABCABC"     /* what k_abc expands to        */
#define TI_RUNS         300     /* matches of 18 of the long file       */
#define TI_RUNLEN       (1 + 18 * TI_RUNS)      /* its bytes, all 'x'   */
#define TI_BIG          5000    /* bytes of the plain file              */
#define TI_HDRSZ        14      /* bytes of a compressed file's header  */

/* What a server with the Windows client and the data gets. */
static const char *const k_server[] = {
    "INSTALL.LST", "README.TXT", "INV.EXE", "SERV.EXE", "SERV.CFG",
    "WIN.EXE", NULL
};

static char g_src[DOS_PATHMAX];         /* the drive's directory, SRC/  */
static char g_dst[DOS_PATHMAX];         /* the target, DST              */
static char g_in[DOS_PATHMAX];          /* the name of the disk in SRC  */
static const char *g_swaps;             /* disks the next asks insert   */
static int g_replace;                   /* the answer to INS_Q_REPLACE  */
static int g_data;                      /* the answer to INS_Q_DATA     */
static int g_nq;                        /* questions and notices seen   */
static int g_q[TI_MAXQ];
static char g_s[TI_MAXQ][DOS_PATHMAX];

/* Write the file name holding text into the scratch directory dir. */
static void put_file(const char *dir, const char *name, const char *text)
{
    TF_EQ(tfs_write_all(tfs_subpath(dir, name), text, (u16)strlen(text)),
          0);
}

/* Make the disk directory dir with its list and files, each holding
   its name, and with data the data directory. */
static void make_disk(const char *dir, const char *list,
                      const char *const *files, int data)
{
    char sub[DOS_PATHMAX];
    int i;

    TF_EQ(cf_mkdir(tfs_path(dir, "")), 0);
    put_file(dir, INS_LIST, list);
    for (; *files != NULL; files++)
        put_file(dir, *files, *files);
    if (data) {
        sprintf(sub, "%s%c%s", dir, DOS_DIRSEP, INS_DATA);
        TF_EQ(cf_mkdir(tfs_path(sub, "")), 0);
        for (i = 0; k_data[i] != NULL; i++)
            put_file(sub, k_data[i], k_data[i]);
    }
}

static const char *const k_dirs[] = {
    "SRC", "D1", "D2", "D3", "DX", "DST", "DS2", NULL
};
static const char *const k_subs[] = {
    "SRC/DATA", "SRC/DATA._", "D1/DATA", "D2/DATA", "D3/DATA", "DX/DATA",
    "DST/DATA", "DS2/DATA", NULL
};

/* Remove the disks and the target. */
static void clean(void)
{
    tfs_dirs(k_subs, 0);
    tfs_dirs(k_dirs, 0);
}

/* Make the set's disks, disk 1 in the drive, and no target. */
static void setup(void)
{
    clean();
    make_disk("SRC", k_list1, k_disk1, 1);
    make_disk("D2", k_list2, k_disk2, 0);
    make_disk("DX", k_listx, k_disk1 + 1, 0);
    strcpy(g_in, "D1");
    dos_label_host = "LA1";
    g_nq = 0;
}

/* Put the disk dir in the drive, the one there back to its name; DX
   has the label of D2. */
static void swap_in(const char *dir)
{
    char a[DOS_PATHMAX];

    strcpy(a, tfs_path("SRC", ""));
    TF_EQ(rename(a, tfs_path(g_in, "")), 0);
    TF_EQ(rename(tfs_path(dir, ""), a), 0);
    strcpy(g_in, dir);
    dos_label_host = dir[1] == '1' ? "LA1" : dir[1] == '3' ? "LA3" : "LA2";
}

/* The user: records each question and notice; at a disk question puts
   in the disk of the next letter of g_swaps (1, 2, X for DX, '-' none),
   leaving when there is none. */
static int ui(int q, const char *s)
{
    char d[3];

    if (q == INS_N_COPY)
        return 0;
    if (g_nq < TI_MAXQ) {
        g_q[g_nq] = q;
        strcpy(g_s[g_nq++], s);
    }
    switch (q) {
    case INS_Q_DISK:
        if (*g_swaps == '\0')
            return -1;
        if (*g_swaps != '-') {
            d[0] = 'D';
            d[1] = *g_swaps;
            d[2] = '\0';
            swap_in(d);
        }
        g_swaps++;
        return 1;
    case INS_Q_REPLACE:
        return g_replace;
    case INS_Q_DATA:
        return g_data;
    default:
        return 0;
    }
}

/* 1 when the target holds exactly the files want (and DATA's, when
   data), each with the bytes of its source. */
static int holds(const char *const *want, int data)
{
    dos_find f;
    char name[DOS_FINDNAME];
    char pat[DOS_PATHMAX];
    int n;
    int i;
    int rc;

    n = 0;
    strcpy(pat, tfs_subpath("DST", "*.*"));
    for (rc = dos_find_first(&f, pat, name); rc == 0;
         rc = dos_find_next(&f, name))
        n++;
    for (i = 0; want[i] != NULL; i++) {
        if (!tfs_exists(tfs_subpath("DST", want[i])))
            return 0;
        if (strcmp(want[i], INS_LIST) != 0 &&
            tfs_size(tfs_subpath("DST", want[i])) != strlen(want[i]))
            return 0;
    }
    return n == i && tfs_exists(tfs_subpath("DST", "DATA/A.DBF")) == data &&
           tfs_exists(tfs_subpath("DST", "DATA/B.DBF")) == data;
}

static void test_parse(void)
{
    static const char *const bad[] = {
        "R 1\n360 LA1 LA2\n1\n",
        "R 1\n360 LA1 LA2\n3\nSVNT 1 5 README.TXT\n",
        "R 1\n360 LA1 LA2\n1\nSVNT 3 5 README.TXT\n",
        "R 1\n360 LA1 LA2\n1\nSQ 1 5 README.TXT\n",
        "R 1\n360 LA1 LA2\n1\nZ 1 5 README.TXT\n",
        "R 1\n360 LA1 LA2\n1\nS 1 5x README.TXT\n",
        "R 1\n360 LA1 LA2\n1\nS 1 5 A\\B.TXT\n",
        "R 1\n360 LA1 LA2\n1\nS 1 5 README.TXT X\n",
        "R 1\n360 LA1 LA2\n1\nS 1 5 README.TXT",
        "R 1\n360 A B C D E\n1\nS 1 5 README.TXT\n",
        "R 1\n360\n1\nS 1 5 README.TXT\n",
        "R 1\n360 LA1\n1\nD 1 6 DATA\n",
        "R 1\n360 LA1\n1\nD 1 6,0 DATA\n",
        "R 1\n360 LA1\n1\nS 1 6,2 README.TXT\n",
        "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT\r\n"
        "99999 AAAAAAAAAAA BBBBBBBBBBB CCCCCCCCCCC DDDDDDDDDDD\r\n1\n"
        "S 1 5 README.TXT\n",
        NULL
    };
    char big[INS_LISTMAX + 64];
    ins_list l;
    char thin[DOS_FINDNAME];
    int i;

    put_file("SRC", "BAD.LST", "");
    TF_EQ(ins_load(&l, tfs_subpath("SRC", "BAD.LST")), INS_EFORMAT);
    strcpy(big, "R 1\n360 LA1\n1\n");
    while (strlen(big) + 10 < sizeof big)
        strcat(big, "S 1 5 A.B\n");
    put_file("SRC", "BAD.LST", big);
    TF_EQ(ins_load(&l, tfs_subpath("SRC", "BAD.LST")), INS_EFORMAT);
    put_file("SRC", "BAD.LST",
             "R 1\n360 LA1\n1\nSVNT 1 5 INSTALL.EXE\nSVN 1 5 INV.EXE\n");
    TF_EQ(ins_load(&l, tfs_subpath("SRC", "BAD.LST")), INS_OK);
    TF_EQ(ins_roles(&l), INS_R_ALONE | INS_R_SERVER | INS_R_CLIENT);
    put_file("SRC", "BAD.LST", "R 1\n360 LA1\n1\nDZ 1 6,2 DATA\n");
    TF_EQ(ins_load(&l, tfs_subpath("SRC", "BAD.LST")), INS_OK);
    TF_EQ(l.f[0].files, 2);
    TF_CHECK(l.f[0].lz && l.f[0].roles == INS_R_DATA);
    for (i = 0; bad[i] != NULL; i++) {
        put_file("SRC", "BAD.LST", bad[i]);
        TF_EQ(ins_load(&l, tfs_subpath("SRC", "BAD.LST")), INS_EFORMAT);
    }
    TF_EQ(ins_load(&l, tfs_subpath("SRC", "NONE.LST")), CF_ENOENT);
    TF_EQ(ins_load(&l, tfs_subpath("D2", INS_LIST)), INS_OK);
    TF_STREQ(l.title, "R 1");
    TF_STREQ(l.set, "R 1\r\n360 LA1 LA2\r\n");
    TF_EQ(l.ndisks, 2);
    TF_STREQ(l.label[1], "LA2");
    TF_EQ(l.here, 1);
    TF_EQ(l.nfiles, 8);
    TF_STREQ(l.f[5].name, "SERV.CFG");
    TF_EQ(l.f[5].roles, INS_R_SERVER | INS_R_KEEP);
    TF_EQ(l.f[5].disks, 2);
    TF_EQ(l.f[0].size, 40);
    TF_EQ(ins_roles(&l), INS_R_DOS | INS_R_WIN);
    ins_prog(&l, INS_R_THIN, thin);
    TF_STREQ(thin, "THIN");
    TF_EQ(ins_load(&l, tfs_subpath("DX", INS_LIST)), INS_OK);
    TF_EQ(ins_roles(&l), 0);
    ins_prog(&l, INS_R_THIN, thin);
    TF_STREQ(thin, "");
}

static void test_dir_ok(void)
{
    static const char *const bad[] = {
        "C:\\", "C:", "RETROINV", "\\RETROINV", "C:\\A\\\\B", "C:\\A\\",
        "C:\\A B", "C:\\A*", "C:\\A/B", "1:\\A", NULL
    };
    char d[DOS_PATHMAX];
    int i;

    strcpy(d, "c:\\retroinv\\x");
    TF_EQ(ins_dir_ok(d), 1);
    TF_STREQ(d, "C:\\RETROINV\\X");
    for (i = 0; bad[i] != NULL; i++) {
        strcpy(d, bad[i]);
        TF_EQ(ins_dir_ok(d), 0);
    }
    memset(d, 'A', DOS_PATHMAX - 1);
    d[DOS_PATHMAX - 1] = '\0';
    memcpy(d, "C:\\", 3);
    d[DOS_PATHMAX - 5] = '\0';
    TF_EQ(ins_dir_ok(d), 0);
}

/* Install roles from the set into a target that may hold settings,
   the disk questions answered by swaps: the code of ins_copy. */
static int install(ins_list *l, u8 roles, const char *swaps)
{
    char failed[DOS_PATHMAX];
    int rc;

    TF_EQ(ins_load(l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(l, roles);
    rc = ins_keep(l, g_dst, ui);
    if (rc == INS_OK) {
        g_swaps = swaps;
        rc = ins_copy(l, g_src, 0, g_dst, ui, failed);
    }
    return rc;
}

static void test_install(void)
{
    ins_list l;

    setup();
    TF_EQ(install(&l, INS_R_SERVER | INS_R_WIN | INS_R_DATA, "-X2"),
          INS_OK);
    TF_CHECK(holds(k_server, 1));
    TF_EQ(g_nq, 5);
    TF_EQ(g_q[0], INS_Q_DISK);
    TF_STREQ(g_s[0], "LA2");
    TF_EQ(g_q[1], INS_N_WRONG);
    TF_STREQ(g_s[1], "LA1");
    TF_EQ(g_q[3], INS_N_WRONG);
    TF_STREQ(g_s[3], "LA2");
    TF_EQ(g_q[4], INS_Q_DISK);

    /* Again, the second disk in the drive: one swap back to the first,
       the settings file kept, then replaced. */
    put_file("DST", "SERV.CFG", "MINE");
    g_nq = 0;
    g_replace = 0;
    TF_EQ(install(&l, INS_R_SERVER, "1"), INS_OK);
    TF_EQ(tfs_size(tfs_subpath("DST", "SERV.CFG")), 4);
    TF_EQ(g_q[0], INS_Q_REPLACE);
    TF_STREQ(g_s[0], "SERV.CFG");
    TF_EQ(g_q[1], INS_Q_DISK);
    TF_STREQ(g_s[1], "LA1");
    TF_EQ(g_nq, 2);
    g_replace = 1;
    TF_EQ(install(&l, INS_R_SERVER, "2"), INS_OK);
    TF_EQ(tfs_size(tfs_subpath("DST", "SERV.CFG")), 8);
    g_replace = -1;
    TF_EQ(install(&l, INS_R_SERVER, ""), INS_ECANCEL);
}

/* The room a plan needs, in clusters of 512 and 4096 bytes, a drive
   with one byte too few refused, and a new installation over an old
   one needing none for the files it replaces. */
static void test_room(void)
{
    ins_list l;
    u32 need;
    u32 have;

    setup();
    TF_EQ(ins_load(&l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_SERVER | INS_R_WIN | INS_R_DATA);
    TF_EQ(ins_room(&l, g_dst, &need, &have), INS_OK);
    TF_EQ(need, 6 * 512 + 512 + 6 + 2 * 511);
    dos_cluster_host = 4096;
    dos_free_host = 6 * 4096 + 4096 + 6 + 2 * 4095;
    TF_EQ(ins_room(&l, g_dst, &need, &have), INS_OK);
    TF_EQ(need, have);
    dos_free_host--;
    TF_EQ(ins_room(&l, g_dst, &need, &have), INS_EROOM);
    g_replace = 1;
    TF_EQ(install(&l, INS_R_SERVER | INS_R_WIN | INS_R_DATA, "2"),
          INS_OK);
    dos_free_host = 0;
    ins_plan(&l, INS_R_SERVER | INS_R_WIN);
    TF_EQ(ins_room(&l, g_dst, &need, &have), INS_OK);
    TF_EQ(need, 0);
    ins_plan(&l, INS_R_THIN);
    TF_EQ(ins_room(&l, g_dst, &need, &have), INS_EROOM);
    TF_EQ(need, 4096);
    dos_free_host = 0xFFFFFFFFUL;
    dos_cluster_host = 512;
}

/* Leaving before the sample data's disk, or a copy of the sample that
   fails, leaves no data directory, nor does an empty one count as
   data, so the next run offers the sample again and copies it. */
static void test_resume(void)
{
    char failed[DOS_PATHMAX];
    ins_list l;

    setup();
    swap_in("D2");
    TF_EQ(install(&l, INS_R_SERVER | INS_R_DATA, ""), INS_ECANCEL);
    TF_CHECK(tfs_exists(tfs_subpath("DST", "SERV.EXE")));
    TF_CHECK(ins_has_dir(tfs_subpath("DST", INS_DATA)) == 0);
    TF_CHECK(ins_has_data(g_dst) == 0);
    TF_EQ(cf_mkdir(tfs_subpath("DST", INS_DATA)), 0);
    TF_CHECK(ins_has_data(g_dst) == 0);
    TF_EQ(install(&l, INS_R_SERVER | INS_R_DATA, "1"), INS_OK);
    TF_CHECK(ins_has_data(g_dst));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "DATA/B.DBF")));

    /* The disk in the drive is the first: its other files take three
       writes, the sample's second file the fifth. */
    setup();
    TF_EQ(ins_load(&l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_ALONE | INS_R_DATA);
    tfs_fault(TFS_OP(CF_OP_WRITE), 5, 1, 0);
    TF_EQ(ins_copy(&l, g_src, 0, g_dst, ui, failed), CF_EFULL);
    tfs_fault_off();
    TF_STREQ(failed, tfs_subpath("DST", "DATA/B.DBF"));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "INV.EXE")));
    TF_CHECK(ins_has_dir(tfs_subpath("DST", INS_DATA)) == 0);

    /* An empty inventory still gets its data directory: INV makes none
       of its own. */
    setup();
    TF_EQ(install(&l, INS_R_ALONE, ""), INS_OK);
    TF_CHECK(ins_has_dir(tfs_subpath("DST", INS_DATA)));
    TF_CHECK(ins_has_data(g_dst) == 0);
}

static void test_order(void)
{
    static const char *const dirs[] = { "SRC", "D2", "D3" };
    ins_list l;
    char list[sizeof k_list3];
    int i;

    clean();
    for (i = 0; i < 3; i++) {
        sprintf(list, k_list3, i + 1);
        make_disk(dirs[i], list, k_disk3[i], 0);
    }
    strcpy(g_in, "D1");
    dos_label_host = "LA1";
    g_nq = 0;
    TF_EQ(install(&l, INS_R_SERVER, "23"), INS_OK);
    TF_EQ(g_nq, 2);
    TF_STREQ(g_s[0], "LA2");
    TF_STREQ(g_s[1], "LA3");
    TF_CHECK(tfs_exists(tfs_subpath("DST", "B.EXE")));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "C.EXE")));
}

static void test_fail(void)
{
    static const char *const first[] = { "INSTALL.LST", "README.TXT", NULL };
    ins_list l;
    char failed[DOS_PATHMAX];

    setup();
    TF_EQ(ins_load(&l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_CLIENT);
    tfs_fault(TFS_OP(CF_OP_WRITE), 3, 1, 0);
    TF_EQ(ins_copy(&l, g_src, 0, g_dst, ui, failed), CF_EFULL);
    tfs_fault_off();
    TF_STREQ(failed, tfs_subpath("DST", "INV.EXE"));
    TF_CHECK(holds(first, 0));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "DATA/A.DBF")) == 0);

    /* Leaving at the second disk keeps the first disk's files. */
    TF_EQ(install(&l, INS_R_THIN, ""), INS_ECANCEL);
    TF_CHECK(holds(first, 0));

    /* A source that is the target copies nothing. */
    TF_EQ(ins_load(&l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_THIN);
    TF_EQ(ins_copy(&l, tfs_subpath("DST", ""), 0, g_dst, ui, failed),
          INS_OK);
    TF_CHECK(holds(first, 0));
}

/* Write into out the header of a compressed file of n bytes: its
   length. */
static u16 szdd_head(u8 *out, u32 n)
{
    memcpy(out, "SZDD\x88\xF0\x27\x33" "A_", 10);
    put_le32(out + 10, n);
    return TI_HDRSZ;
}

/* Write the compressed file path: the header for n bytes, then the
   len bytes of tokens at tok. */
static void put_szdd(const char *path, u32 n, const u8 *tok, u16 len)
{
    u8 buf[TI_HDRSZ + sizeof k_abc];
    u16 h;

    h = szdd_head(buf, n);
    memcpy(buf + h, tok, len);
    TF_EQ(tfs_write_all(path, buf, (u16)(h + len)), 0);
}

/* Write into out the compressed form of TI_RUNLEN bytes 'x': a literal,
   then TI_RUNS matches of 18 from its window position, each repeating
   it; its length. */
static u16 make_run(u8 *out)
{
    u16 n;
    int t;

    n = szdd_head(out, TI_RUNLEN);
    for (t = 0; t <= TI_RUNS; t++) {
        if (t % 8 == 0)
            out[n++] = (u8)(t == 0);
        if (t == 0) {
            out[n++] = 'x';
        } else {
            out[n++] = 0xF0;
            out[n++] = 0xFF;
        }
    }
    return n;
}

/* 1 when path holds TI_RUNLEN bytes 'x'. */
static int is_run(const char *path)
{
    static u8 buf[TI_RUNLEN + 1];
    u16 got;
    u16 i;

    if (tfs_read_all(path, buf, sizeof buf, &got) != 0 || got != TI_RUNLEN)
        return 0;
    for (i = 0; i < got && buf[i] == 'x'; i++)
        ;
    return i == got;
}

/* 1 when path holds the bytes of text. */
static int holds_text(const char *path, const char *text)
{
    char buf[16];
    u16 got;

    return tfs_read_all(path, buf, sizeof buf, &got) == 0 &&
           got == strlen(text) && memcmp(buf, text, got) == 0;
}

static void test_lz(void)
{
    static u8 big[TI_BIG];
    static u8 run[TI_RUNS * 3];
    ins_list l;
    char failed[DOS_PATHMAX];
    char src2[DOS_PATHMAX];
    u16 n;
    u16 i;

    clean();
    TF_EQ(cf_mkdir(tfs_path("SRC", "")), 0);
    TF_EQ(cf_mkdir(tfs_subpath("SRC", "DATA._")), 0);
    put_file("SRC", INS_LIST, k_listz);
    n = make_run(run);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INV.EX_"), run, n), 0);
    for (i = 0; i < TI_BIG; i++)
        big[i] = (u8)(i * 7);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "README.TXT"), big, TI_BIG), 0);
    put_szdd(tfs_subpath("SRC", "DATA._/A.DBF"), 9, k_abc, sizeof k_abc);
    put_szdd(tfs_subpath("SRC", "DATA._/A.DBT"), 9, k_abc, sizeof k_abc);
    put_file("SRC", "INV.EXE", "STALE");
    TF_EQ(cf_mkdir(tfs_subpath("SRC", INS_DATA)), 0);
    put_file("SRC", "DATA/A.DBF", "STALE");
    dos_label_host = "LA1";
    TF_EQ(install(&l, INS_R_ALONE | INS_R_DATA, ""), INS_OK);
    TF_CHECK(l.f[1].lz && l.f[1].roles == INS_R_ALONE && !l.f[2].lz &&
             l.f[3].lz && l.f[3].roles == INS_R_DATA);
    TF_CHECK(is_run(tfs_subpath("DST", "INV.EXE")));
    TF_CHECK(tfs_same(tfs_subpath("SRC", "README.TXT"),
                      tfs_subpath("DST", "README.TXT")));
    TF_CHECK(holds_text(tfs_subpath("DST", "DATA/A.DBF"), TI_ABC));
    TF_CHECK(holds_text(tfs_subpath("DST", "DATA/A.DBT"), TI_ABC));

    /* From the installation, as from the zip's folder: the same list,
       every file as it is. */
    strcpy(src2, tfs_subpath("DST", ""));
    TF_EQ(ins_load(&l, tfs_subpath("DST", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_ALONE | INS_R_DATA);
    TF_EQ(ins_copy(&l, src2, 0, tfs_path("DS2", ""), ui, failed), INS_OK);
    TF_CHECK(is_run(tfs_subpath("DS2", "INV.EXE")));
    TF_CHECK(holds_text(tfs_subpath("DS2", "DATA/A.DBT"), TI_ABC));

    /* A disk that fills at the expansion's second write, then a cut
       file: no part of INV.EXE either time. */
    TF_EQ(cf_remove(tfs_subpath("DST", "INV.EXE")), 0);
    TF_EQ(ins_load(&l, tfs_subpath("SRC", INS_LIST)), INS_OK);
    ins_plan(&l, INS_R_ALONE);
    tfs_fault(TFS_OP(CF_OP_WRITE), 3, 1, 0);
    TF_EQ(ins_copy(&l, g_src, 0, g_dst, ui, failed), CF_EFULL);
    tfs_fault_off();
    TF_STREQ(failed, tfs_subpath("DST", "INV.EXE"));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "INV.EXE")) == 0);
    TF_EQ(tfs_write_all(tfs_subpath("SRC", "INV.EX_"), run, (u16)(n / 2)),
          0);
    ins_plan(&l, INS_R_ALONE);
    TF_EQ(ins_copy(&l, g_src, 0, g_dst, ui, failed), INS_EFORMAT);
    TF_STREQ(failed, tfs_subpath("SRC", "INV.EX_"));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "INV.EXE")) == 0);

    /* Into the disk's own directory, as after a copy of the disk. */
    ins_plan(&l, INS_R_ALONE);
    TF_EQ(ins_copy(&l, g_src, 0, tfs_path("SRC", ""), ui, failed),
          INS_EPACKED);
    TF_STREQ(failed, tfs_path("SRC", ""));
}

static void test_remove(void)
{
    ins_list l;
    char failed[DOS_PATHMAX];

    setup();
    TF_EQ(install(&l, INS_R_SERVER | INS_R_WIN | INS_R_DATA, "2"),
          INS_OK);
    put_file("DST", "MY.TXT", "mine");
    TF_EQ(ins_load(&l, tfs_subpath("DST", INS_LIST)), INS_OK);
    g_nq = 0;
    g_data = -1;
    TF_EQ(ins_remove(&l, g_dst, ui, failed), INS_ECANCEL);
    TF_CHECK(tfs_exists(tfs_subpath("DST", "SERV.EXE")));
    TF_EQ(g_q[0], INS_Q_DATA);
    TF_STREQ(g_s[0], tfs_subpath("DST", INS_DATA));
    g_nq = 0;
    g_data = 0;
    TF_EQ(ins_remove(&l, g_dst, ui, failed), INS_OK);
    TF_CHECK(tfs_exists(tfs_subpath("DST", "SERV.EXE")) == 0);
    TF_CHECK(tfs_exists(tfs_subpath("DST", INS_LIST)) == 0);
    TF_CHECK(tfs_exists(tfs_subpath("DST", "MY.TXT")));
    TF_CHECK(tfs_exists(tfs_subpath("DST", "DATA/A.DBF")));
    TF_EQ(g_q[1], INS_N_LEFT);
    TF_EQ(g_nq, 2);
    TF_EQ(cf_remove(tfs_subpath("DST", "MY.TXT")), 0);
    g_nq = 0;
    g_data = 1;
    TF_EQ(ins_remove(&l, g_dst, ui, failed), INS_OK);
    TF_CHECK(ins_has_dir(g_dst) == 0);
    TF_EQ(g_nq, 1);
}

int main(int argc, char **argv)
{
    tf_begin("T_INST");
    if (argc < 2)
        return 2;
    tfs_set_dir(argv[1]);
    strcpy(g_src, tfs_subpath("SRC", ""));
    strcpy(g_dst, tfs_path("DST", ""));
    setup();
    test_parse();
    test_dir_ok();
    test_install();
    test_room();
    test_resume();
    test_order();
    test_fail();
    test_lz();
    test_remove();
    clean();
    return tf_done();
}
