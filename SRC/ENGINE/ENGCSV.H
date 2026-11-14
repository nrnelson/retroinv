/*
 * ENGCSV.H - text files of comma-separated values (CSV.C): export of a
 * table to a file and import of a file into ITEMS or LOCS.
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only CSV.C, DPCSV.C and their tests pay the compiler memory
 * for these declarations (Watcom C 6.5 runs out of memory on the
 * largest programs otherwise).  Include it after ENGINE.H.
 *
 * The files are those dBASE III Plus writes with COPY TO file DELIMITED
 * and reads with APPEND FROM file DELIMITED: one line per record, ended
 * by CR LF, the non-memo fields in file order separated by commas; a C
 * value in double quotes with its trailing spaces removed, N values
 * without their leading spaces, D values as YYYYMMDD.  The byte forms
 * of blank values and of the file's end are dBASE's own, recorded in
 * DOC/ENGINE.md ("Text files (CSV)") and checked against dBASE by
 * TEST/DBASE/csvchk.sh.
 * Unlike dBASE, a '"' inside a C value is written doubled ("") and read
 * back as one.  Bytes 80h..FFh (code page 437) pass unchanged; a byte
 * below 20h or 7Fh in a stored value (only another program can store
 * one) is written as a space.  Memo fields are never written or read.
 *
 * Paths name a file on the machine running the engine (the server, or
 * the client in local mode), relative to that program's current
 * directory unless they hold a drive or a directory.
 */
#ifndef ENGCSV_H
#define ENGCSV_H

#define ENG_CSV_HEADER  0x01    /* the first line holds the field names  */
#define ENG_CSV_REPLACE 0x02    /* export: an existing file is replaced */

#define ENG_CSVPATH     ENG_MAXDIR  /* characters of a FILE= path       */
#define ENG_CSVLINE     ENG_MAXMEMO /* bytes of one line, CR LF excluded:
                                       lines are built and read in
                                       eng_memobuf, which no export or
                                       import touches otherwise        */
#define ENG_EXPROWS     50      /* records an export request examines   */
#define ENG_IMPERRS     10      /* refused rows an import reports: the
                                   IMPORT answer's PROTO_IMPERRS        */
#define ENG_IMPROWCOST  1024    /* disk bytes an imported row may take
                                   (record, index entries, audit row,
                                   scratch index entries)               */
#define ENG_IMPSLACK    16384   /* disk bytes an import keeps free      */
#define ENG_CSVSCRATCH  "&C"    /* extension of an export being written,
                                   without its dot and its last
                                   character, the tag's hex digit       */
#define ENG_EXPTAGS     16      /* tags of an export: one hex digit     */

/* The table named name ("ITEMS", case-insensitive) into *t when it may
   be exported (ITEMS LOCS CATS LTYPES MAINT LOANS AUDIT) or, with
   import 1, imported (ITEMS LOCS); else ENG_EARG naming TABLE
   (ENG_TX_NOCSV). */
int eng_csv_table(eng *e, const char *name, int import, int *t);

/* ENG_OK when path may name an export or import file, as txt_outfile_ok
   (COMPAT.H) judges a name of at most ENG_CSVPATH characters: among the
   extensions it refuses are those of ENG_CSVSCRATCH and ENG_EDITMARK;
   else ENG_EARG naming FILE (ENG_TX_FNAME). */
int eng_csv_path(eng *e, const char *path);

/* EXPORT: write the live records of table t, in record order, to path.
   The file is written under a scratch name and renamed to path once
   complete, so path never holds a partial export.  The scratch name is
   path with its extension replaced by, or given, ENG_CSVSCRATCH and the
   hex digit of tag (0 .. ENG_EXPTAGS - 1, else ENG_EINTERNAL), the
   export's own: the protocol gives its session's slot.  One call
   examines at most ENG_EXPROWS records (deleted ones counted) and
   appends the live ones; *rows receives how many it wrote and cursor
   (ENG_CURSORMAX bytes) the cursor to continue with, or "" when the
   export is complete and renamed.  after NULL or "" starts the export:
   it creates the scratch file (replacing a leftover one) and, with
   ENG_CSV_HEADER, writes the field names first; ENG_EDUP naming FILE
   (ENG_TX_EXISTS) when path exists and flags lack ENG_CSV_REPLACE.
   With after, the call appends to the scratch file, which must exist
   (else ENG_ECURSOR, ENG_TX_NOEXPORT); the caller repeats path, flags
   and tag.  The last call removes an existing path (ENG_CSV_REPLACE)
   and renames the scratch file to it.  A call after the first that
   fails cuts the scratch file back to its size before the call, so it
   can be repeated with the same cursor.  Exports with different tags
   never share a scratch file; those of one tag to paths that differ
   only in their extension share one (on DOS; the scratch extension's
   case follows the path's), so they must not run at the same time.
   Two exports to one path both write their whole file: the last call
   of the second to end finds path there, ENG_EDUP naming FILE unless
   it replaces it.  Writes no audit row.  A file or directory not found
   is ENG_ENOTFOUND naming FILE; the other file errors as eng_dbferr
   maps them. */
int eng_export(eng *e, int t, const char *path, u8 flags, u8 tag,
               const char *after, u16 *rows, char *cursor);

/* A refused row of an import: its line number in the file, counted from
   the file's first line with blank lines and the field-name line
   included; the field (LINE for the whole line, or for the field-name
   line a column it does not know); the error code and text, as
   eng_errfield, eng_errmsg and the code of the failing check give them.
   The strings last until the next check. */
typedef void (*eng_csverr_fn)(void *arg, u32 line, const char *field,
                              int code, const char *msg);

/* IMPORT: add every line of path to table t (ENG_T_ITEMS or ENG_T_LOCS)
   as a new record, as ITEM ADD or LOC ADD would; a path that does not
   exist gives ENG_ENOTFOUND naming FILE.  With ENG_CSV_HEADER the first
   non-blank line names the columns (field names, any case, each once,
   no memo field); otherwise the columns are the table's non-memo fields
   in file order, and a line may hold fewer.  An empty value gives no
   pair, so the defaults of ADD apply; columns naming a field flagged
   ENG_F_RO (the IDs) are not stored.  A location value (an item's
   LOC_ID, a location's PARENT_ID) is a location token (ENGLOCS.H),
   with two additions for a PARENT_ID, which may name a line before or
   after its own.  When the file has a LOC_ID column (a file without a
   header always has one), a LOC_ID value numbers its line (1 to
   ENG_MAXID, else ENG_EARG naming LOC_ID, ENG_TX_BADVALUE; once in the
   file, else ENG_EDUP) and a PARENT_ID that is a number names the line
   of that number (else ENG_ECONSTR naming PARENT_ID, ENG_TX_NOLOC), or
   0 the top level: never a live location, so an export imports back
   with its tree into any data.  In any file a PARENT_ID that is a code
   and no live location's names the line of that LOC_CODE.  Lines whose
   parents form a loop are refused (ENG_ECONSTR naming PARENT_ID,
   ENG_TX_CYCLE), as are those that would lie deeper than
   ENG_MAXLOCDEPTH (ENG_TX_LOCDEEP) and those whose parent line is refused
   (ENG_TX_NOLOC).  Blank lines are skipped and a Ctrl-Z (1Ah) ends the
   file.
   Two passes.  The first reads the whole file and checks every line
   (eng_item_check, eng_loc_check; for LOCS also each LOC_ID in the
   file and each code against those of the other lines, and each name
   against those the other lines add under the same parent, ENG_EDUP
   naming the field, ENG_TX_FILEDUP), so it refuses exactly the lines
   the second pass would, without writing anything; each refused line
   is reported through err (at most ENG_IMPERRS times, LOCS lines in the
   order they are found) and counted in *errors; a failure from 300 up
   ends the import with its code.  For LOCS the first pass reads the
   file at most ENG_MAXLOCDEPTH + 2 times (once for the LOC_IDs and
   codes, then once per sweep, which takes each line whose parent is no
   line or a line already taken) and the second once per sweep that
   took a line, adding parents before children: a file that lists
   parents first is read twice by the first pass and once by the
   second.  When any line was refused, or
   the disk of the data directory has less than ENG_IMPSLACK + lines x
   ENG_IMPROWCOST bytes free (ENG_EFULL naming FILE, ENG_TX_NOSPACE),
   nothing is written.  Otherwise the second pass
   adds the lines in one batch naming t (eng_batch_begin, one commit per
   record, eng_batch_end), preceded by one audit row (ACTION K, TBL the
   table, REC_ID 0, FLD IMPORT, NEWVAL the path), which is written also
   when the file holds no line; *added counts the records written.  A
   stop before the batch has ended leaves none of them after the next
   open, which cuts the uncounted records; a failure of the second pass
   (300-range) returns its code with *added the records already written,
   which the batch's end has flushed.  Unbounded work: the protocol
   allows it only while no other session is open.  Uses two DOS handles
   besides the engine's (the file, and for LOCS a scratch index in the
   data directory). */
int eng_import(eng *e, int t, const char *path, u8 flags,
               eng_csverr_fn err, void *arg, u16 *errors, u32 *added);

#endif
