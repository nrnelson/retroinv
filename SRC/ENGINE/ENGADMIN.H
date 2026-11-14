/*
 * ENGADMIN.H - the administration of the data directory (ADMIN.C): the
 * audit list, and PACK, REINDEX, BACKUP and RESTORE.
 *
 * The four file commands need the data directory to themselves; the
 * protocol layer runs them only in local mode or for the one open
 * session of a server.  Each closes every file (eng_shut), works on the
 * files by name, and opens them again (eng_reopen, then eng_init_data),
 * so the work of one request is not bounded: it grows with the data.
 * PACK, REINDEX and RESTORE first name every table in the marker
 * ENG_EDITMARK, so the reopen, and the open after a stop, rebuild every
 * index from its table and make the open-time repairs.  BACKUP and
 * RESTORE, while a change cut off has left the marker, first open the
 * files again and close them, which makes its repairs: a backup holds
 * repaired tables (never the marker), and a RESTORE that fails after
 * writing a marker of its own loses none.  When a command succeeds it
 * writes one ENG_A_ADMIN audit row with the actor of the request: TBL
 * AUDIT, REC_ID 0, FLD the command's word, NEWVAL the directory of
 * BACKUP and RESTORE.  When the reopen fails the engine holds no file
 * (eng_is_open is 0) and the reopen's error is returned.
 *
 * The functions of TABLES.C that work on the files of a closed engine
 * by name are declared here too, apart from ENGINE.H, which every
 * module reads.  Only TABLES.C, ADMIN.C, DPADMIN.C and the tests include
 * this header, after ENGINE.H.
 */
#ifndef ENGADMIN_H
#define ENGADMIN_H

/* ---------------------------------------------------------------------
 * The files of a closed engine (TABLES.C).
 * --------------------------------------------------------------------- */

/* Scratch extensions: a new table, its memo file and a new index are
   written under these names, as are the copies ADMIN BACKUP and RESTORE
   make, and each is renamed into place once complete, so a stop never
   leaves a partial file under a name the engine opens.  They differ from
   the names PACK.C uses for its own replacements ($, % and #).  The
   copies of one BACKUP or RESTORE are renamed only once all of them are
   complete and the commit mark ENG_COPYMARK is written beside them
   (eng_put_copies). */
#define ENG_SCRDBF      ".&DB"
#define ENG_SCRDBT      ".&DT"
#define ENG_SCRNDX      ".&NX"
#define ENG_COPYMARK    "COPIED.$$$"

/* TABLES.C.  eng_shut cancels e's open edit, drops its audit reservation and
   flushes and closes every file under eng_close's rule, keeping e;
   eng_reopen, on e so shut, opens the files of its directory again with
   the steps of eng_open_files (the marker found decides what is rebuilt),
   recording the failure's text, and the caller then runs eng_init_data.
   Until a reopen succeeds e holds no file, and eng_is_open (ENGINE.H)
   gives 0. */
int eng_shut(eng *e);
int eng_reopen(eng *e);

/* TABLES.C.  Whether the marker ENG_EDITMARK is in the data directory of
   e, shut: cf_exists's answer. */
int eng_marker_on(eng *e);

/* TABLES.C.  The scratch copies of every table, memo file and index in
   the directory of prefix pre (ENG_SCRDBF, ENG_SCRDBT, ENG_SCRNDX): when
   the commit mark ENG_COPYMARK is there, each copy there is renamed
   into place over its file and the mark removed last, so a stop part-way
   leaves the mark and the copies still to rename; without the mark each
   copy is removed.  eng_open_files does this first in the data
   directory, which makes the renames of a RESTORE after its commit
   (its own reopen, or the open after a stop) and drops the copies of
   one cut off before it, or of a stopped creation.  eng_drop_copies
   removes every copy whatever the mark, as a BACKUP whose copying or
   commit mark failed does in its target.  0 or a cf_ code. */
int eng_put_copies(const char *pre);
int eng_drop_copies(const char *pre);

/* TABLES.C.  DBF_OK when the open table d has exactly the fields of
   table t in the schema, else DBF_EFORMAT, or, when a schema row cannot
   be copied out of far data, a code eng_dbferr maps to ENG_EINTERNAL. */
int eng_layout(dbf *d, int t);

/* ---------------------------------------------------------------------
 * The commands (ADMIN.C).
 * --------------------------------------------------------------------- */

/* AUDIT LIST: open s over the audit rows.  With tbl (a table name, at
   most ENG_TBLNAMELEN characters, compared upper-cased) the scan is the
   AUDIT1 prefix of that table, or of that table and record when rec is
   not 0: by record, then oldest first.  Without tbl it runs from the
   last row backwards (ENG_SCAN_RRECORDS), newest first, and rec must be
   0.  station and user, when not NULL or "", keep the rows whose
   STATION and USERNAME equal them, compared without regard to case;
   given together, a row must match both.  With user and without tbl
   the scan is the AUDIT2 prefix of that name, the user's rows alone,
   newest first.  Any other filter is tested on the rows the scan reads,
   and those it passes over still count towards ENG_MAXREADS.  after is
   the cursor of the scan (NULL or "" to start).  ENG_EARG naming TBL
   when tbl is too long or rec is given without it, naming STATION when
   station is longer than ENG_STALEN, naming USER when user is longer
   than ENG_USERLEN; a cursor of another order, or of another user's
   rows, is ENG_ECURSOR. */
int eng_audit_scan(eng *e, eng_scan *s, const char *tbl, u32 rec,
                   const char *station, const char *user,
                   const char *after);

/* The file commands, by op:
     ENG_ADM_PACK     dbf_pack every table, then rebuild every index;
                      *removed receives the deleted records dropped
     ENG_ADM_REINDEX  rebuild every index from its table
     ENG_ADM_BACKUP   copy every file of the data directory, tables,
                      memo files and indexes, into the existing
                      directory dir, each written under its scratch
                      name (ENG_SCRDBF ...) there; when all are, the
                      commit mark is written there and every copy
                      renamed over the file of its name, so a stop
                      before the mark leaves the files there as they
                      were, and one after it leaves the mark, for
                      which RESTORE refuses the set until a BACKUP
                      there succeeds
     ENG_ADM_RESTORE  copy the tables and memo files of a backup in dir
                      back, once every table there has opened read-only
                      with the schema's fields and every memo file has
                      opened, then rebuild every index: each file is
                      copied under its scratch name and, when all are,
                      the commit mark is written and the reopen renames
                      them into place, so a failure before the mark
                      changes no data, and a stop after it is finished
                      by the next open (eng_open_files, step 1).
   dir (BACKUP and RESTORE; NULL otherwise) is a directory to which
   DOS_DIRSEP is added unless it ends in '\', '/' or ':': ENG_EARG naming
   DIR when it is empty, longer than ENG_MAXDIR with the separator,
   holds a space, a control byte, '*' or '?', or is the data directory
   (as a probe file made in one is seen in the other); ENG_ENOTFOUND
   naming DIR when it does not exist, a file of the backup is missing
   or the backup holds the commit mark.  Any other failure (a missing
   file aside) of a table or memo file of the backup, opened as
   required, or of the probe in either directory is the code eng_dbferr
   maps it to, naming DIR: ENG_EIO for a table with other fields (the
   probe is made in BACKUP's dir, in RESTORE's data directory, and
   looked for from the other); a data directory gone, so that RESTORE
   cannot make the probe there, is ENG_EIO naming nothing, as the open
   reports it.  Those are found before anything is changed.
   *removed is 0 for every op but PACK. */
#define ENG_ADM_PACK    0
#define ENG_ADM_REINDEX 1
#define ENG_ADM_BACKUP  2
#define ENG_ADM_RESTORE 3
int eng_admin(eng *e, int op, const char *dir, u32 *removed);

#endif
