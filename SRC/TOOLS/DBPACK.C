/*
 * DBPACK.C - pack a dBASE III table and rebuild its indexes.
 *
 *   DBPACK table[.DBF] [/P] [/R index[.NDX] ...] [/C]
 *
 *   /P   pack: drop the records marked deleted and the memo blocks only
 *        they used; the default when no /R is given
 *   /R   rebuild each index named after it (until the next switch) from
 *        the table, after the pack when /P is given too; /R must name
 *        at least one index
 *   /C   only complete or undo an interrupted pack of the table, and an
 *        interrupted rebuild of each index listed with /R, then stop
 *
 * Every run first completes or undoes an interrupted pack of the table
 * (dbf_pack_recover), so a pack that was stopped part-way is never
 * built upon; each rebuild does the same for its index.  Packing
 * renumbers records, so every index of the table must be rebuilt
 * afterwards; list them with /R.  The count printed for an index is the
 * number of keys it holds.  A pack needs free disk space for a new copy
 * of the table and its memo file, a rebuild about three times the
 * index's size; when a failure leaves a replacement half done (a file
 * with a % extension), DBPACK says to run it again with /C.  A name
 * without an extension gets ".DBF" (table) or ".NDX" (index), lower-case
 * when the name ends in a lower-case letter.  The critical-error handler
 * is installed for the run, so a disk error is reported instead of
 * prompting "Abort, Retry, Fail?".
 *
 * Exit status: 0 success; 1 usage error, or a name that is empty or too
 * long; 2 an operation failed (the DBF_E code is shown by name), or
 * memory ran out.
 */
#define COMPAT_HEAP
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "COMPAT.H"
#include "DBF.H"
#include "TOOLSUP.H"

static void usage(void)
{
    printf("usage: DBPACK table[.DBF] [/P] [/R index[.NDX] ...] [/C]\n");
    printf("  /P  pack the table (default without /R)\n");
    printf("  /R  rebuild the listed indexes, after the pack\n");
    printf("  /C  only finish or undo an interrupted pack (and, with /R, "
           "rebuilds)\n");
}

/* 1 if name is not empty and, given extension ext by txt_path_ext,
   fits buf, DOS_PATHMAX bytes; otherwise prints why and returns 0. */
static int name_ok(char *buf, const char *name, const char *ext)
{
    if (name[0] == '\0') {
        printf("DBPACK: empty name\n");
        return 0;
    }
    if (txt_path_ext(buf, DOS_PATHMAX, name, ext, 1) != 0) {
        printf("DBPACK: name too long: %s\n", name);
        return 0;
    }
    return 1;
}

/* 1 if rc is DBF_OK; otherwise prints what failed on name, with the
   DBF_E code by name and, for a full disk, the space a run needs, and
   returns 0. */
static int report(const char *what, const char *name, int rc)
{
    if (rc == DBF_OK)
        return 1;
    printf("DBPACK: %s %s: %s\n", what, name, tl_err_name(rc));
    if (rc == DBF_EFULL)
        printf("DBPACK: a pack needs free space for a copy of the table "
               "and memo file,\n        a rebuild about three times "
               "the index size\n");
    return 0;
}

/* 1 if the file named like path but with extension ext (built in buf,
   DOS_PATHMAX bytes, by txt_path_ext) can be opened: a replacement was
   left half done. */
static int left_over(const char *path, const char *ext, char *buf)
{
    int h;

    if (txt_path_ext(buf, DOS_PATHMAX, path, ext, 0) != 0 ||
        cf_open(buf, (u8)(CF_READ | CF_DENYNONE), &h) != 0)
        return 0;
    cf_close(h);
    return 1;
}

/* After a refused recovery (DBF_EFORMAT: the files present are not a
   state an interrupted pack or rebuild leaves), list which of the n
   names ext[] made from path exist. */
static void list_refused(const char *path, const char *const *ext, int n,
                         char *buf)
{
    int k;

    printf("DBPACK: these files do not form a state a stopped run leaves; "
           "resolve them\n        by hand (keep one complete set, delete "
           "or rename the others):\n");
    for (k = 0; k < n; k++)
        if (left_over(path, ext[k], buf))
            printf("          %s\n", buf);
}

static const char *const table_exts[] = {
    "DBF", "DBT", "$DB", "$DT", "%DB", "%DT"
};
static const char *const index_exts[] = {
    "NDX", "$NX", "%NX", "#NX"
};

/* Rebuild the indexes argv[i] for which isidx[i] is set, or with
   recover_only complete or undo their interrupted rebuilds only. */
static int reindex_all(const char *table, int argc, char **argv,
                       const char *isidx, int recover_only)
{
    char path[DOS_PATHMAX];
    char tmp[DOS_PATHMAX];
    dbf *t;
    u32 n;
    int i;
    int ok;
    int rc;
    int failed;

    t = NULL;
    failed = 0;
    if (!recover_only &&
        !report("cannot open", table, dbf_open(&t, table, DBF_RDONLY)))
        return 0;
    ok = 1;
    for (i = 1; i < argc && ok; i++) {
        if (!isidx[i])
            continue;
        /* The caller (run) checked the name.  The recovery runs here as
           well as inside ndx_reindex, so that its result is reported on
           its own; with recover_only every index is tried, and the run
           fails if any is refused. */
        txt_path_ext(path, DOS_PATHMAX, argv[i], "NDX", 1);
        rc = ndx_reindex_recover(path);
        if (!report("cannot recover", path, rc)) {
            if (rc == DBF_EFORMAT)
                list_refused(path, index_exts,
                             sizeof index_exts / sizeof index_exts[0], tmp);
            failed = 1;
            ok = recover_only;
            continue;
        }
        if (recover_only) {
            printf("%s: no interrupted rebuild left\n", path);
            continue;
        }
        rc = ndx_reindex(t, path, &n);
        if (rc == DBF_EFORMAT) {
            printf("DBPACK: cannot reindex %s: not a character index "
                   "whose key length\n        matches its expression "
                   "on this table (DBF_EFORMAT)\n", path);
            ok = 0;
        } else if (rc == DBF_EARG) {
            printf("DBPACK: cannot reindex %s: its key expression does not "
                   "fit this table\n        (unknown field or type "
                   "mismatch, DBF_EARG)\n", path);
            ok = 0;
        } else {
            ok = report("cannot reindex", path, rc);
        }
        if (!ok && left_over(path, "%NX", tmp))
            printf("DBPACK: the rebuild of %s is half done; run "
                   "DBPACK %s /C /R %s\n", path, table, path);
        if (ok)
            printf("%s: %lu keys\n", path, (unsigned long)n);
    }
    if (t != NULL && !report("cannot close", table, dbf_close(t)))
        ok = 0;
    return ok && !failed;
}

/* Parse the command line and do the run; returns the exit status.  isidx
   (argc bytes) marks the index arguments. */
static int run(int argc, char **argv, char *isidx)
{
    char table[DOS_PATHMAX];
    char path[DOS_PATHMAX];
    dbf_packinfo info;
    const char *tname;
    int pack;
    int nidx;
    int recover_only;
    int want_idx;
    int inidx;
    int i;
    int rc;
    char *a;
    char c;

    tname = NULL;
    pack = 0;
    nidx = 0;
    recover_only = 0;
    want_idx = 0;
    inidx = 0;
    for (i = 1; i < argc; i++) {
        a = argv[i];
        isidx[i] = 0;
        if (a[0] == '/' || a[0] == '-') {
            if (a[1] == '\0' || a[2] != '\0') {
                usage();
                return EXIT_USAGE;
            }
            inidx = 0;
            c = a[1];
            switch (TXT_UPPER(c)) {
            case 'P':
                pack = 1;
                break;
            case 'R':
                inidx = 1;
                want_idx = 1;
                break;
            case 'C':
                recover_only = 1;
                break;
            default:
                usage();
                return EXIT_USAGE;
            }
        } else if (inidx) {
            if (!name_ok(path, a, "NDX"))
                return EXIT_USAGE;
            isidx[i] = 1;
            nidx++;
        } else if (tname == NULL) {
            tname = a;
        } else {
            usage();
            return EXIT_USAGE;
        }
    }
    /* /R without a name would fall back to a pack that renumbers the
       records and leaves every index stale. */
    if (tname == NULL || (recover_only && pack) ||
        (want_idx && nidx == 0)) {
        usage();
        return EXIT_USAGE;
    }
    if (!name_ok(table, tname, "DBF"))
        return EXIT_USAGE;
    if (nidx == 0 && !recover_only)
        pack = 1;

    /* Run here as well as inside dbf_pack, so that its result is
       reported on its own. */
    rc = dbf_pack_recover(table);
    if (report("cannot recover", table, rc)) {
        if (recover_only)
            printf("%s: no interrupted pack left\n", table);
    } else if (rc == DBF_EFORMAT) {
        list_refused(table, table_exts,
                     sizeof table_exts / sizeof table_exts[0], path);
    }
    if (recover_only) {
        /* The indexes are recovered even when the table is not. */
        if (nidx > 0 && !reindex_all(table, argc, argv, isidx, 1))
            return EXIT_FAIL;
        return rc == DBF_OK ? EXIT_OK : EXIT_FAIL;
    }
    if (rc != DBF_OK)
        return EXIT_FAIL;
    if (pack) {
        if (!report("cannot pack", table, dbf_pack(table, &info))) {
            if (left_over(table, "%DB", path) ||
                left_over(table, "%DT", path))
                printf("DBPACK: the pack of %s is half done; run "
                       "DBPACK %s /C\n", table, table);
            return EXIT_FAIL;
        }
        printf("%s: %lu records kept, %lu deleted records removed\n",
               table, (unsigned long)info.kept,
               (unsigned long)info.removed);
        if (info.memo_blocks_before != 0)
            printf("memo file: %lu blocks before, %lu after\n",
                   (unsigned long)info.memo_blocks_before,
                   (unsigned long)info.memo_blocks_after);
    }
    if (nidx > 0 && !reindex_all(table, argc, argv, isidx, 0))
        return EXIT_FAIL;
    return EXIT_OK;
}

int main(int argc, char **argv)
{
    char *isidx;
    int rc;

    dos_heap_top();
    if (argc < 2) {
        usage();
        return EXIT_USAGE;
    }
    isidx = (char *)malloc((size_t)argc);
    if (isidx == NULL) {
        printf("DBPACK: %s\n", tl_err_name(DBF_ENOMEM));
        return EXIT_FAIL;
    }
    dos_crit_install();
    rc = run(argc, argv, isidx);
    dos_crit_remove();
    free(isidx);
    return rc;
}
