/*
 * ADMIN.C - the audit list, and PACK, REINDEX, BACKUP and RESTORE
 * (ENGADMIN.H).
 *
 * The file commands run on an engine whose files are closed (eng_shut)
 * and open them again at the end, whatever happened (eng_reopen and
 * eng_init_data), with the actor of the request put back afterwards,
 * since eng_init_data leaves it blank.  BACKUP and RESTORE first make
 * the repairs of a marker left by a change cut off (repair).
 *
 * BACKUP and RESTORE copy each file under its scratch name in the
 * target directory, then write the commit mark ENG_COPYMARK there.
 * BACKUP then renames every copy into place (eng_put_copies), which
 * removes the mark last; RESTORE, whose target is the data directory,
 * leaves the renames to its reopen, which makes them from the mark as
 * the open after a stop does, so one code path finishes every RESTORE
 * and a failed rename is the reopen's error.  A stop before the mark
 * leaves the files in place as they were, and RESTORE refuses a backup
 * directory that still holds the mark as incomplete.  The copies go
 * through the engine's memo buffer, which no other work uses meanwhile,
 * in pieces of whole sectors, so DOS moves them straight between the
 * disk and the buffer.
 *
 * Whether two prefixes name one directory is told by the files, not by
 * their text, which may differ in case, in a drive letter or a relative
 * path: the scratch name of SEQ.DBF is made through one and looked for
 * through the other, seen there and gone again once it is removed.  A
 * stale file of that name in the other directory stays when the probe
 * goes, so it cannot pass for the probe; one in the data directory is
 * removed by the next open, as every scratch file there is.
 */
#define COMPAT_DISK
#include <stddef.h>
#include <string.h>

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGADMIN.H"

static const char k_dir[] = "DIR";
static const char k_user[] = "USER";
static const char k_dbf[] = ".DBF";
static const char k_dbt[] = ".DBT";

/* The command words, the FLD of their audit rows, in ENG_ADM_ order;
   the rows are as wide as the longest word and its NUL, so a longer
   word is refused by the compiler. */
static const char k_words[][sizeof "RESTORE"] = {
    "PACK", "REINDEX", "BACKUP", "RESTORE"
};

extern char fits_words[1];
extern char fits_words[(sizeof k_words / sizeof k_words[0] ==
                        ENG_ADM_RESTORE + 1) ? 1 : 2];

/* A copy moves at most AD_CHUNK bytes per call: the whole disk sectors
   that the memo buffer holds. */
#define AD_CHUNK        ((ENG_MAXMEMO / DOS_SECTOR) * DOS_SECTOR)

/* The filters of AUDIT LIST are kept in eng_scan.f: the station in
   text, the user in tag; eng_rec_is reads either field. */
extern char fits_actor[1];
extern char fits_actor[(ENG_STALEN <= ENG_MAXFIND &&
                        ENG_USERLEN <= ENG_TAGFILT &&
                        ENG_USERLEN <= ENG_MAXFIND) ? 1 : 2];

/* The keys of AUDIT1 and AUDIT2 start with a name of one width, the
   table's or the user's, which eng_audit_scan pads alike. */
#define AD_NAMELEN      ENG_TBLNAMELEN
extern char fits_names[1];
extern char fits_names[(ENG_USERLEN == AD_NAMELEN) ? 1 : 2];

/* ---------------------------------------------------------------------
 * The audit list.
 * --------------------------------------------------------------------- */

/* The station and user filters of AUDIT LIST, both kept; the user's
   is tested on the rows of an AUDIT2 scan as well, where it tells a
   name from the same name with spaces after it. */
static int actor_match(eng *e, eng_scan *s)
{
    return eng_rec_is(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, STATION),
                      s->f.text) &&
           eng_rec_is(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, USERNAME), s->f.tag);
}

int eng_audit_scan(eng *e, eng_scan *s, const char *tbl, u32 rec,
                   const char *station, const char *user,
                   const char *after)
{
    char key[ENG_TBLNAMELEN + ENG_IDLEN + 1];
    const char *ftbl;
    const char *name;
    u16 n;
    int ix;
    int rc;

    ftbl = eng_field_name(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, TBL));
    n = (u16)(tbl != NULL ? strlen(tbl) : 0);
    if (n > ENG_TBLNAMELEN)
        return eng_fail(e, ENG_EARG, ftbl, ENG_TX_FILTLONG);
    rc = eng_filter_len(e, station, ENG_STALEN,
                        eng_field_name(e, ENG_T_AUDIT,
                                       ENG_FIELD(AUDIT, STATION)));
    if (rc == ENG_OK)
        rc = eng_filter_len(e, user, ENG_USERLEN, k_user);
    if (rc != ENG_OK)
        return rc;
    /* The prefix is the table's name, then the record's ID, or without
       a table the user's name: upper-cased and padded with spaces. */
    ix = ENG_X_AUDIT1;
    name = tbl;
    if (n == 0) {
        if (rec != 0)
            return eng_fail(e, ENG_EARG, ftbl, ENG_TX_REQUIRED);
        ix = ENG_X_AUDIT2;
        name = user;
        if (name == NULL || name[0] == '\0')
            ix = ENG_SCAN_RRECORDS;
    }
    if (ix != ENG_SCAN_RRECORDS) {
        memset(key, ' ', AD_NAMELEN);
        memcpy(key, name, strlen(name));
        key[AD_NAMELEN] = '\0';
        eng_upper(key);
        n = AD_NAMELEN;
        if (rec != 0) {
            eng_id_key(rec, key + n);
            n += ENG_IDLEN;
        }
    }
    rc = eng_scan_open(e, s, ENG_T_AUDIT, ix, (const u8 *)key, n, after);
    if (rc != ENG_OK)
        return rc;
    txt_copy(s->f.text, station, (u16)sizeof s->f.text);
    txt_copy(s->f.tag, user, (u16)sizeof s->f.tag);
    s->match = actor_match;
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * Files.
 * --------------------------------------------------------------------- */

/* out (ENG_MAXPATH bytes) = prefix pre + stem + ext. */
static void file_path(char *out, const char *pre, const char *stem,
                      const char *ext)
{
    strcpy(out, pre);
    strcat(out, stem);
    strcat(out, ext);
}

/* Record a failure of a file in the directory DIR names, path, whose
   DBF_E code is rc, naming DIR: a missing file is ENG_ENOTFOUND, its
   text telling a missing directory from a backup set that is not
   complete; any other code is mapped as eng_dbferr does.  Returns the
   ENG_E code. */
static int dir_fail(eng *e, int rc, const char *path)
{
    u16 tx;
    int h;

    if (rc == DBF_ENOENT) {
        rc = cf_open(path, CF_READ | CF_DENYNONE, &h);
        if (rc == 0)
            cf_close(h);
        tx = rc == CF_ENOPATH ? ENG_TX_NODIR : ENG_TX_SETMISS;
        rc = ENG_ENOTFOUND;
    } else {
        rc = eng_dbferr(e, rc, 0);
        tx = eng_errtx(e);
    }
    return eng_fail(e, rc, k_dir, tx);
}

/* The directory dir as a prefix into pre (ENG_MAXDIR + 1 bytes), as
   ENGADMIN.H describes. */
static int dir_prefix(eng *e, const char *dir, char *pre)
{
    u16 n;
    u16 i;
    int c;

    n = (u16)strlen(dir);
    if (n == 0)
        return eng_fail(e, ENG_EARG, k_dir, ENG_TX_DIRNAME);
    c = (u8)dir[n - 1];
    if (c != '\\' && c != '/' && c != ':')
        n++;
    if (n > ENG_MAXDIR)
        return eng_fail(e, ENG_EARG, k_dir, ENG_TX_DIRNAME);
    for (i = 0; dir[i] != '\0'; i++) {
        c = (u8)dir[i];
        if (c <= ' ' || c == 0x7F || c == '*' || c == '?')
            return eng_fail(e, ENG_EARG, k_dir, ENG_TX_DIRNAME);
        pre[i] = (char)c;
    }
    if (i < n)
        pre[i] = DOS_DIRSEP;
    pre[n] = '\0';
    return ENG_OK;
}

/* ENG_OK when the prefixes a and b name two directories, by the probe
   the header comment describes, made through a; ENG_EARG naming DIR when
   they name one; a probe that cannot be made or looked for is recorded
   by dir_fail, but one that cannot be made because the data directory
   is gone is ENG_EIO naming nothing, as the open reports it. */
static int other_dir(eng *e, const char *a, const char *b)
{
    char pa[ENG_MAXPATH];
    char pb[ENG_MAXPATH];
    const char *stem;
    int seen;
    int rc;
    int h;

    stem = eng_table_def(ENG_T_SEQ)->name;
    file_path(pa, a, stem, ENG_SCRDBF);
    file_path(pb, b, stem, ENG_SCRDBF);
    rc = cf_create(pa, &h);
    if (rc == 0)
        rc = cf_close(h);
    rc = dbf_map_cf(rc);
    if (rc == DBF_ENOENT && a == eng_dir(e))
        return eng_fail(e, ENG_EIO, NULL, ENG_TX_NODIR);
    if (rc != DBF_OK)
        return dir_fail(e, rc, pa);
    seen = cf_exists(pb);
    cf_remove(pa);
    rc = seen == 1 ? cf_exists(pb) : seen;
    if (rc < 0)
        return dir_fail(e, dbf_map_cf(-rc), pb);
    if (seen == 1 && rc == 0)
        return eng_fail(e, ENG_EARG, k_dir, ENG_TX_DATADIR);
    return ENG_OK;
}

/* ENG_OK when the backup in the directory of prefix pre is complete:
   no commit mark of a BACKUP cut off, every table opening read-only
   with the schema's fields, and every memo file opening read-only; else
   the first failure, recorded by dir_fail. */
static int check_set(eng *e, const char *pre)
{
    char path[ENG_MAXPATH];
    const eng_tdef *td;
    dbf *d;
    dbt *m;
    int rc;
    int t;

    file_path(path, pre, ENG_COPYMARK, "");
    if (cf_exists(path) == 1)
        return eng_fail(e, ENG_ENOTFOUND, k_dir, ENG_TX_SETMISS);
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        file_path(path, pre, td->name, k_dbf);
        rc = dbf_open(&d, path, DBF_RDONLY);
        if (rc == DBF_OK) {
            rc = eng_layout(d, t);
            dbf_close(d);
        }
        if (rc == DBF_OK && td->memo) {
            file_path(path, pre, td->name, k_dbt);
            rc = dbt_open(&m, path, DBF_RDONLY);
            if (rc == DBF_OK)
                dbt_close(m);
        }
        if (rc != DBF_OK)
            return dir_fail(e, rc, path);
    }
    return ENG_OK;
}

/* Copy file stem+ext of the directory of prefix src to stem+scr in that
   of prefix dst, made afresh.  A DBF_E code; a failed copy may leave
   part of the file under the scratch name. */
static int copy_file(eng *e, const char *src, const char *dst,
                     const char *stem, const char *ext, const char *scr)
{
    char path[ENG_MAXPATH];
    char *b;
    u16 got;
    int rc;
    int rc2;
    int hi;
    int ho;

    file_path(path, src, stem, ext);
    rc = cf_open(path, CF_READ | CF_DENYW, &hi);
    if (rc != 0)
        return dbf_map_cf(rc);
    file_path(path, dst, stem, scr);
    rc = cf_create(path, &ho);
    if (rc == 0) {
        b = eng_memobuf(e);
        do {
            rc = cf_read(hi, b, AD_CHUNK, &got);
            if (rc == 0 && got != 0)
                rc = cf_write(ho, b, got);
        } while (rc == 0 && got != 0);
        rc2 = cf_close(ho);
        if (rc == 0)
            rc = rc2;
    }
    cf_close(hi);
    return dbf_map_cf(rc);
}

/* Copy every table and memo file, and with ndx 1 every index, of the
   directory of prefix src to their scratch names in that of prefix
   dst.  A DBF_E code. */
static int copy_set(eng *e, const char *src, const char *dst, int ndx)
{
    const eng_tdef *td;
    int rc;
    int i;

    rc = DBF_OK;
    for (i = 0; rc == DBF_OK && i < ENG_NTABLES; i++) {
        td = eng_table_def(i);
        rc = copy_file(e, src, dst, td->name, k_dbf, ENG_SCRDBF);
        if (rc == DBF_OK && td->memo)
            rc = copy_file(e, src, dst, td->name, k_dbt, ENG_SCRDBT);
    }
    for (i = 0; ndx && rc == DBF_OK && i < ENG_NINDEX; i++)
        rc = copy_file(e, src, dst, eng_index_def(i)->name, ".NDX",
                       ENG_SCRNDX);
    return rc;
}

/* Write the commit mark in the directory of prefix pre.  A DBF_E code. */
static int commit(const char *pre)
{
    char path[ENG_MAXPATH];
    int rc;
    int h;

    file_path(path, pre, ENG_COPYMARK, "");
    rc = cf_create(path, &h);
    /* The mark's existence is the commit; a failed close frees h too. */
    if (rc == 0)
        cf_close(h);
    dos_disk_reset();
    return dbf_map_cf(rc);
}

/* Name every table in the marker (eng_mark_line), the first line making
   it afresh when fresh is 1, else added to a marker left by a change
   that failed; the files are closed, so the marker has no count line
   unless it is that old one.  A DBF_E code. */
static int mark_all(eng *e, int fresh)
{
    int rc;
    int t;

    rc = 0;
    for (t = 0; t < ENG_NTABLES && (rc == 0 || rc == ENG_MARKNEW); t++)
        rc = eng_mark_line(e, t, fresh && t == 0);
    return dbf_map_cf(rc == ENG_MARKNEW ? 0 : rc);
}

/* Before BACKUP or RESTORE works on the closed files: when a change cut
   off left the marker, open them again, which makes its repairs, and
   close them.  A BACKUP then copies repaired tables, never the marker,
   and a RESTORE that fails after replacing the marker with its own
   loses no repair.  An ENG_E code. */
static int repair(eng *e)
{
    int rc;
    int rc2;

    if (eng_marker_on(e) == 0)
        return ENG_OK;
    rc = eng_reopen(e);
    if (rc != ENG_OK)
        return rc;
    rc = eng_init_data(e);
    rc2 = eng_shut(e);
    return rc != ENG_OK ? rc : rc2;
}

/* The work of op on the closed files, as ENGADMIN.H describes; pre is
   the prefix of the backup directory.  An ENG_E code. */
static int run_op(eng *e, int op, const char *pre, u32 *removed)
{
    char path[ENG_MAXPATH];
    const char *data;
    dbf_packinfo pi;
    int rc;
    int t;

    data = eng_dir(e);
    switch (op) {
    case ENG_ADM_PACK:
        rc = mark_all(e, 0);
        for (t = 0; rc == DBF_OK && t < ENG_NTABLES; t++) {
            file_path(path, data, eng_table_def(t)->name, k_dbf);
            rc = dbf_pack(path, &pi);
            if (rc == DBF_OK)
                *removed += pi.removed;
        }
        break;
    case ENG_ADM_REINDEX:
        rc = mark_all(e, 0);
        break;
    case ENG_ADM_BACKUP:
        rc = copy_set(e, data, pre, 1);
        if (rc == DBF_OK)
            rc = commit(pre);
        if (rc == DBF_OK)
            rc = dbf_map_cf(eng_put_copies(pre));
        else
            eng_drop_copies(pre);       /* the copy or the mark failed  */
        break;
    default:
        rc = copy_set(e, pre, data, 0);
        if (rc == DBF_OK)
            rc = mark_all(e, 1);
        if (rc == DBF_OK)
            rc = commit(data);
        break;
    }
    return eng_dbferr(e, rc, 0);
}

int eng_admin(eng *e, int op, const char *dir, u32 *removed)
{
    char pre[ENG_MAXDIR + 1];
    char sta[ENG_STALEN + 1];
    char user[ENG_USERLEN + 1];
    const char *s;
    const char *u;
    eng_audrow row;
    int rc;
    int rc2;

    *removed = 0;
    pre[0] = '\0';
    if (op >= ENG_ADM_BACKUP) {
        rc = dir_prefix(e, dir, pre);
        if (rc == ENG_OK)
            rc = op == ENG_ADM_BACKUP ? other_dir(e, pre, eng_dir(e)) :
                                        other_dir(e, eng_dir(e), pre);
        if (rc == ENG_OK && op == ENG_ADM_RESTORE)
            rc = check_set(e, pre);
        if (rc != ENG_OK)
            return rc;
    }
    eng_actor(e, &s, &u);
    txt_copy(sta, s, (u16)sizeof sta);
    txt_copy(user, u, (u16)sizeof user);
    rc = eng_shut(e);
    if (rc == ENG_OK && op >= ENG_ADM_BACKUP)
        rc = repair(e);
    if (rc == ENG_OK)
        rc = run_op(e, op, pre, removed);
    rc2 = eng_reopen(e);
    if (rc2 == ENG_OK)
        rc2 = eng_init_data(e);
    eng_set_actor(e, sta, user);
    if (rc2 != ENG_OK) {
        eng_shut(e);
        return rc2;
    }
    if (rc != ENG_OK)
        return rc;
    row.action = ENG_A_ADMIN;
    row.fld = k_words[op];
    row.oldval = NULL;
    row.newval = op >= ENG_ADM_BACKUP ? dir : NULL;
    return eng_audit(e, ENG_T_AUDIT, 0, &row);
}
