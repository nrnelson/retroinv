/*
 * ENGINE.H - the inventory engine: the tables of the data directory and
 * the business rules over them.
 *
 * One engine (an eng) owns every file of one data directory: each table
 * of the schema with its memo file and indexes.  It is opened by the
 * single writer process (the server, or the client in local mode); every
 * file is opened deny-read/write, so a second writer fails with
 * ENG_EINUSE when SHARE is loaded.  The protocol layer (PROTO.H) is the
 * only caller; screens never call the engine directly.
 *
 * Return codes.  Every function returning int returns ENG_OK (0) or one
 * of the ENG_E codes below, whose values are the protocol's error
 * numbers, so the protocol layer sends them unchanged.  A failing call
 * records the name of the offending field (eng_errfield, sent as FIELD=)
 * and a short numbered text (eng_errmsg, eng_errtx); they stay until
 * the next failure or eng_clear_error.  Data is unchanged when a
 * function fails with a code outside the 300s, except for the audit row
 * of a failed login; after a 300-range failure (I/O, index, disk full,
 * handles) the files are flushed as far as possible and the request may
 * be partly done.
 *
 * Every function that changes data leaves its files flushed on return
 * (dbf_flush, dbt_flush, ndx_flush), so a request either reached the disk
 * or reported an error.  A change made of several commits in one batch
 * flushes once, when the batch ends (eng_batch_begin).  Audit rows reach
 * the disk no later than the records they describe.
 *
 * Files, all in SRC/ENGINE/.  Besides SRC/DBF/, COMPAT.H and
 * SRC/COMPAT/CSCHEMA.C, each uses only modules listed before it
 * (DOC/ENGINE.md names which):
 *   SCHEMA.C    tables and indexes, field flags (rows in SCHTAB.H)
 *   TABLES.C    files, error state, actor, clock, record reads
 *   IDGEN.C     ID numbers from SEQ.DBF, ID keys
 *   AUDIT.C     audit rows
 *   CURSOR.C    paging cursors and bounded scans
 *   EDIT.C      record edits, index upkeep, field-change audit, memo lines
 *   USERS.C     password hash, logins, user accounts
 *   CODES.C     code tables: categories and location types
 *   LOCS.C      locations, their tree and its walks
 *   ITEMS.C     items, the repair of a split
 *   ASSY.C      assemblies: install, remove, move, walks, repair
 *   MAINT.C     maintenance records
 *   LOANS.C     loans, reservations, in-use rows and the counters
 *               of ITEMS, repair
 *   CSV.C       CSV export and import
 *   INITDATA.C  eng_open: seed rows, SEQ repair, open-time repairs
 *   ADMIN.C     the audit list, PACK, REINDEX, BACKUP and RESTORE
 * The one call back is eng_shut (and eng_close through it), which ends
 * the engine's open edit and audit reservation (eng_edit_cancel,
 * eng_audit_drop) before it closes the files.  Programs and tests link
 * all of them, and with them SRC/COMPAT/CSCHEMA.C and CSCHDAT.C: the
 * field rows, key expressions and seed rows that only an open reads, and
 * the numbered texts, kept outside DGROUP (they use SCHEMA.C).
 *
 * The headers beside this one are read only where they are needed, to
 * spare the compilers' memory: ENGFLD.H (field numbers, ENG_FIELD, and
 * on request field widths), ENGTX.H (the text numbers ENG_TX_, rows
 * in TXTAB.H), ENGSCH.H, ENGID.H, ENGEDIT.H, ENGUSER.H, ENGCODE.H,
 * ENGLOCS.H, ENGITEM.H, ENGASSY.H, ENGMAINT.H, ENGLOAN.H, ENGCSV.H and
 * ENGADMIN.H (the functions of CSCHEMA.C, IDGEN.C, EDIT.C, USERS.C,
 * CODES.C, LOCS.C, ITEMS.C, ASSY.C, MAINT.C, LOANS.C, CSV.C and
 * ADMIN.C).  Each is included after this one.
 *
 * The eng handle is passed without const (Watcom C 6.5 rejects a const
 * pointer parameter to a struct that is incomplete at the prototype).
 */
#ifndef ENGINE_H
#define ENGINE_H

#include "COMPAT.H"
#include "DBF.H"

/* ---------------------------------------------------------------------
 * Return codes: the protocol's error numbers.
 * --------------------------------------------------------------------- */
#define ENG_OK          0
#define ENG_ESYNTAX     100     /* syntax error                         */
#define ENG_EUNKNOWN    101     /* unknown command                      */
#define ENG_EARG        102     /* bad or missing argument; see errfield */
#define ENG_ETOOLONG    103     /* line too long                        */
#define ENG_ENOTAVAIL   104     /* command needs the server             */
#define ENG_ENOTFOUND   200     /* no such record                       */
#define ENG_EDUP        201     /* duplicate key                        */
#define ENG_ECONSTR     202     /* constraint violated                  */
#define ENG_EPERM       203     /* level too low or read-only station   */
#define ENG_ECURSOR     204     /* bad cursor                           */
#define ENG_EIO         300     /* database I/O error or damaged table  */
#define ENG_EINDEX      301     /* index corrupt or not as the schema   */
#define ENG_EFULL       302     /* disk full                            */
#define ENG_EHANDLES    303     /* file handles exhausted (FILES=)      */
#define ENG_EINUSE      304     /* data files in use by another program */
#define ENG_ENOLOGIN    400     /* HELLO or LOGIN required              */
#define ENG_EBADPW      401     /* bad user name or password            */
#define ENG_EBUSY       402     /* server busy                          */
#define ENG_EDISABLED   403     /* account disabled                     */
#define ENG_EMUSTCHG    404     /* password change required             */
#define ENG_EINTERNAL   500     /* internal error (also out of memory)  */
#define ENG_ESHUTDOWN   501     /* server shutting down                 */

/* ---------------------------------------------------------------------
 * Tables and indexes.  The numbers index the rows of SCHTAB.H.
 * --------------------------------------------------------------------- */
#define ENG_T_ITEMS     0
#define ENG_T_LOCS      1
#define ENG_T_CATS      2
#define ENG_T_LTYPES    3
#define ENG_T_MAINT     4
#define ENG_T_LOANS     5
#define ENG_T_AUDIT     6
#define ENG_T_USERS     7
#define ENG_T_SEQ       8
#define ENG_NTABLES     9

#define ENG_X_ITEMS1    0       /* STR(ITEM_ID,8)                       */
#define ENG_X_ITEMS2    1       /* STR(LOC_ID,8)+STR(ITEM_ID,8)         */
#define ENG_X_ITEMS3    2       /* STR(PARENT_ID,8)+STR(ITEM_ID,8)      */
#define ENG_X_ITEMS4    3       /* UPPER(MFR)+UPPER(MODEL)              */
#define ENG_X_ITEMS5    4       /* UPPER(SERIALNO)                      */
#define ENG_X_LOCS1     5       /* STR(LOC_ID,8)                        */
#define ENG_X_LOCS2     6       /* STR(PARENT_ID,8)+UPPER(LOC_NAME)+
                                   STR(LOC_ID,8): children by name      */
#define ENG_X_LOCS3     7       /* LOC_CODE                             */
#define ENG_X_MAINT1    8       /* item, then maintenance date          */
#define ENG_X_LOANS1    9       /* STR(ITEM_ID,8)+STR(LOAN_ID,8)        */
#define ENG_X_LOANS2    10      /* return year, due date, then loan:
                                   open loans first, by due date       */
#define ENG_X_AUDIT1    11      /* TBL+STR(REC_ID,8)+STR(AUD_ID,8)      */
#define ENG_X_AUDIT2    12      /* USERNAME+STR(99999999-AUD_ID,8): a
                                   user's rows, newest first            */
#define ENG_X_USERS1    13      /* USERNAME                             */
#define ENG_NINDEX      14

/* Memo files (ITEMS.DBT, LOCS.DBT, MAINT.DBT) plus tables plus indexes:
   the DOS handles an open engine holds.  While it works it briefly
   needs up to ENG_TMPHANDLES more (the edit marker, the duplicate a
   flush closes, a memo's stage, a reindex's scratch and new files, the
   files an ADMIN command copies or checks).  eng_open_files enlarges
   the handle table with dos_set_handles(ENG_PROCHANDLES): the engine's
   handles, its transient ones, the five standard ones and
   ENG_SPAREHANDLES for the program's own files (configuration, export
   and import files, the printer capture, the server's event log and
   the two files a mailbox session holds, its request and its
   response); and before it opens the tables it holds the engine's
   handles and its transient ones at once, so that a FILES= of
   CONFIG.SYS too small for them fails the open with ENG_EHANDLES. */
#define ENG_NMEMO       3
#define ENG_NHANDLES    (ENG_NTABLES + ENG_NMEMO + ENG_NINDEX)
#define ENG_TMPHANDLES  4
#define ENG_SPAREHANDLES 8
#define ENG_PROCHANDLES (ENG_NHANDLES + ENG_TMPHANDLES + 5 + \
                         ENG_SPAREHANDLES)

/* The FILES= of CONFIG.SYS the release asks for, which the texts of
   ENG_EHANDLES name: a full handle table takes ENG_PROCHANDLES - 2
   entries of it (the standard handles share three), and ENG_RESFILES
   remain for resident programs and redirected standard handles.  A
   literal, so that the texts can state it; TABLES.C checks the sum. */
#define ENG_RESFILES    4
#define ENG_FILES       45

/* ---------------------------------------------------------------------
 * Limits.
 * --------------------------------------------------------------------- */
#define ENG_MAXDIR      64      /* data directory prefix, without NUL   */
#define ENG_MAXPATH     (ENG_MAXDIR + 13)   /* prefix + 8.3 name + NUL  */
#define ENG_MAXREC      371     /* longest record (ITEMS), with flag    */
#define ENG_MAXKEYLEN   60      /* longest key of the schema (ITEMS4)   */
#define ENG_MAXKEYS     130     /* key bytes of one table's indexes     */
#define ENG_MAXMEMO     2000    /* memo text the engine reads or writes */
#define ENG_MAXVAL      (DBF_MAXCHAR + 1)   /* a field's text and NUL   */
#define ENG_AUDVAL      40      /* AUDIT.OLDVAL / NEWVAL width          */
#define ENG_MAXREADS    200     /* reads per scan request               */
#define ENG_MAXFLIST    24      /* fields in one FIELDS= list           */
#define ENG_MAXFSPEC    40      /* characters of a FIELDS= value        */
#define ENG_MAXRWIDTH   160     /* summed widths of a FIELDS= list      */
#define ENG_MAXFIND     30      /* ITEM FIND text                       */
#define ENG_FINDFLD     60      /* widest field ITEM FIND searches      */
#define ENG_ITAGLEN     12      /* characters of one tag of ITEMS.TAGS: a
                                   literal, as a text of TXTAB.H states
                                   it                                   */
#define ENG_TAGFILT     (2 * ENG_ITAGLEN + 1)   /* a TAG= filter: two
                                   tags and their comma                 */
#define ENG_NWORKS      4       /* a WORKS= filter: each of Y N I ? once */
#define ENG_TAGSET      1024    /* bytes of the tags one ITEM TAGS
                                   request gathers                      */
#define ENG_MAXPAIRS    24      /* KEY=value pairs of one request: equal
                                   to PROTO_MAXPAIRS; sizes the field
                                   numbers eng_edit_apply keeps         */
#define ENG_MAXCODES    50      /* records of a code table (CATS.DBF,
                                   LTYPES.DBF), deleted included        */
#define ENG_MAXDEPTH    16      /* containment tree depth: a top-level
                                   item is at level 1, its parts at 2  */
#define ENG_MAXLOCDEPTH 8       /* location tree depth: a top-level
                                   location is at depth 1              */
#define ENG_ITEMS3LEN   (2 * ENG_IDLEN)     /* ITEMS3 key length        */
#define ENG_DATELEN     DBF_DATELEN         /* a D field: YYYYMMDD      */
#define ENG_TIMELEN     8       /* AUDIT.ATIME: HH:MM:SS                */
#define ENG_STAMPLEN    14      /* a clock stamp: YYYYMMDDHHMMSS        */
#define ENG_TBLNAMELEN  8       /* table name: an 8.3 base name         */
#define ENG_STALEN      16      /* station name (AUDIT.STATION)         */
#define ENG_USERLEN     8       /* user name (USERS.USERNAME)           */
#define ENG_FULLNAMELEN 30      /* USERS.FULLNAME                       */
#define ENG_CATLEN      4       /* CATS.CAT_CODE                        */
#define ENG_LTYPELEN    8       /* LTYPES.LT_CODE, LOCS.LTYPE: the wider
                                   code of the two code tables          */
#define ENG_CODENAMELEN 20      /* CATS.CAT_NAME, LTYPES.LT_NAME        */
#define ENG_IDLEN       8       /* digits of an ID (ITEM_ID, LOC_ID...) */
#define ENG_MAXID       99999999UL          /* largest ENG_IDLEN digits */
#define ENG_LOCLEN      10      /* LOCS.LOC_CODE                        */
#define ENG_LOCNAMELEN  30      /* LOCS.LOC_NAME                        */
#define ENG_SERLEN      30      /* ITEMS.SERIALNO                       */
#define ENG_STATLEN     1       /* ITEMS.STATUS                         */
#define ENG_PWMIN       4       /* password length, characters          */
#define ENG_PWMAX       16
#define ENG_SALTLEN     4       /* USERS.SALT                           */
#define ENG_HASHLEN     32      /* USERS.PWHASH: hex digits             */

/* Cursor text: index name, ':', key in hex, ':', record number in hex
   (CURSOR.C).  ENG_CURSORLEN is the longest, without the NUL. */
#define ENG_IXNAMELEN   6
#define ENG_CURSORLEN   (ENG_IXNAMELEN + 1 + 2 * ENG_MAXKEYLEN + 1 + 8)
#define ENG_CURSORMAX   (ENG_CURSORLEN + 1)

/* Actor of the rows eng_open writes. */
#define ENG_SYSTEM      "SYSTEM"

/* ---------------------------------------------------------------------
 * Schema descriptions (SCHTAB.H, SCHEMA.C, SRC/COMPAT/CSCHEMA.C).
 *
 * SCHTAB.H holds the rows of the schema: tables, their fields in file
 * order with type, width, decimals and flags, the indexes, the seed rows
 * of the code tables.  What every request uses stays in near data
 * (SCHEMA.C): table and index names and numbers, and the flags of each
 * field.  What only an open uses (field names, types and widths, to
 * create and check the tables; key expressions; seed rows) lies outside
 * DGROUP, in SRC/COMPAT/CSCHDAT.C, and is copied out on request by
 * CSCHEMA.C (ENGSCH.H).  Once a table is open its fields are read from
 * its dbf handle (eng_field), which the open has checked against the
 * schema, so no request copies far data.
 *
 * Field numbers and widths are compile-time constants made from the
 * rows, in ENGFLD.H: ENG_FIELD(ITEMS, QTY) is the number of QTY in
 * ITEMS.DBF, ENG_FW_ITEMS_QTY its width.
 * --------------------------------------------------------------------- */
#define ENG_F_RO        0x0001  /* not settable through K=value pairs   */
#define ENG_F_UPPER     0x0002  /* C value is upper-cased when stored   */
#define ENG_F_HIDDEN    0x0004  /* never sent; edits audit no row for it */
#define ENG_F_LIST      0x0008  /* in the default F/R field list        */
#define ENG_F_NOAUDIT   0x0010  /* changes produce no audit row         */
#define ENG_F_AUDMOVE   0x0020  /* change audited as M (move)           */
#define ENG_F_AUDPAR    0x0040  /* change audited as P (parent change)  */
#define ENG_F_REQ       0x0080  /* must be given (non-blank) on ADD     */
#define ENG_F_NONNEG    0x0100  /* N value must not be negative         */
#define ENG_F_AUDLOAN   0x0200  /* change audited as O when the value
                                   rises, R when it falls (the counters
                                   of LOANS.C)                          */

typedef struct {
    const char *name;           /* "ITEMS": file stem and AUDIT.TBL     */
    const char *seq;            /* SEQ_NAME of the ID field (the table's
                                   name), or NULL                       */
    u16  nfields;
    int  firstx;                /* first ENG_X_ of the table, or -1     */
    u8   memo;                  /* has a .DBT                           */
    u8   nindex;                /* its indexes: firstx .. firstx+n-1    */
} eng_tdef;

typedef struct {
    const char *name;           /* "ITEMS1": file stem and cursor name  */
    u16  keylen;                /* kx_len of the expression             */
    u8   table;                 /* ENG_T_                               */
    u8   unique;                /* not 0: no two live records share a
                                   key, which the modules check before
                                   every edit, and it is 1 + the number
                                   of the field a 201 duplicate error
                                   names                                */
} eng_xdef;

const eng_tdef *eng_table_def(int t);
const eng_xdef *eng_index_def(int x);

/* The ENG_F_ flags of field f of table t (0 out of range). */
u16 eng_field_flags(int t, u16 f);

/* ---------------------------------------------------------------------
 * Opening and closing (INITDATA.C, TABLES.C).
 * --------------------------------------------------------------------- */
typedef struct eng eng;         /* an open engine (opaque)              */

typedef struct {
    u16 year;                   /* 1980..2099 (DOS clock)               */
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
} eng_time;

/* Marker file in the data directory, present while a change is between its
   first write (its audit rows, then its tables) and its last index write,
   or while an open is creating a table or an index, and kept after either
   failed until the next open repairs it.  A marker written while AUDIT.DBF
   is open starts with a count line: the number of AUDIT records when the
   marker was created, in decimal (at most TXT_U32LEN digits), and CR LF;
   the audit rows after them may describe changes that never reached the
   disk.  Then come the lines of the tables to repair, each the table's 8.3
   base name (1..ENG_TBLNAMELEN upper-case characters) and CR LF.  A marker
   without a count line (the open's own, written before AUDIT.DBF is
   opened) covers no audit rows.  ENG_MARKHEAD is the longest count line and
   ENG_MARKMAX the longest marker, a count line and every table. */
#define ENG_EDITMARK    "EDITING.$$$"
#define ENG_MARKHEAD    (TXT_U32LEN + 2)
#define ENG_MARKMAX     (ENG_MARKHEAD + ENG_NTABLES * (ENG_TBLNAMELEN + 2))

/* TABLES.C.  Add the line naming table t to the marker ENG_EDITMARK,
   creating the file when it is missing or fresh is 1 (any old file then
   replaced), with its count line first when AUDIT.DBF is open, unless
   a line names t already or the marker is longer than ENG_MARKMAX (an
   open reads such a marker as naming every table).  Nothing in an
   existing file is rewritten: the line goes after its end, and is on
   the disk when the call returns.  The return is 0 when the line was
   added or nothing was to be added, ENG_MARKNEW when the marker was
   created, else a cf_ code. */
#define ENG_MARKNEW     (-1)
int eng_mark_line(eng *e, int t, int fresh);

/* INITDATA.C.  Open the data directory: eng_open_files, then
   eng_init_data.  now is the fixed clock (eng_set_now) to start with,
   or NULL for the DOS clock; tests pass one so the rows written while
   opening are reproducible.  On failure *out is NULL, nothing stays
   open, and why (if not NULL) receives a NUL-terminated text naming the
   file, at most whysz bytes; a buffer of ENG_WHYSZ bytes holds one that
   fits a screen line. */
#define ENG_WHYSZ       80
int eng_open(eng **out, const char *dir, const eng_time *now,
             char *why, u16 whysz);

/* INITDATA.C.  With the actor set to ENG_SYSTEM: append every missing
   SEQ.DBF row (NEXT_VAL 1), raise SEQ (eng_seq_heal), then write the
   seed rows into each empty code table (CATS.DBF, LTYPES.DBF) and the
   ADMIN user into an empty USERS.DBF (eng_user_seed), each audited as
   one I row, inside one batch (eng_batch_begin on CATS) when no marker
   was kept.  The repairs run whenever the marker was kept
   (eng_marker_kept is 1): eng_init_data first calls eng_mark_adopt,
   runs the steps above, after SEQ one ENG_A_CUT row flagging the audit
   rows the marker covers (eng_marker_from; TBL AUDIT, REC_ID the first
   one's AUD_ID, FLD AUD_ID, NEWVAL the last one's), and the repairs
   eng_item_heal, eng_assy_heal and eng_loan_heal in that batch, and
   closes it with eng_batch_end, which removes the marker only when
   every step and the flush succeeded; on a failure it returns the error
   and the marker stays for the next open.  The actor is left blank. */
int eng_init_data(eng *e);

/* TABLES.C.  dir is a prefix to which the 8.3 file names are appended
   as they are, so it is empty (the current directory) or ends in '\',
   '/' or ':', at most ENG_MAXDIR characters.  The steps:
     1  finish or undo an interrupted pack or reindex of every file
        (dbf_pack_recover, ndx_reindex_recover) and remove leftover
        scratch files; then read the marker file ENG_EDITMARK, if any:
        every index of each table it names, and of AUDIT, is to be
        rebuilt (every index when the marker cannot be read);
     2  create every table that does not exist, each file under a
        scratch name renamed into place once complete (.DBT first, .DBF
        last); every new table and every table with a missing index is
        first added to the marker, and its indexes are to be rebuilt;
     3  open every file DBF_RDWR (deny-read/write), checking that each
        table's fields match the schema exactly (else ENG_EIO); a table
        named in the marker, SEQ, and AUDIT whenever a marker exists,
        lose any bytes after their last counted record (an append
        never committed); then build every index to be rebuilt afresh
        from its schema definition and its table (ndx_reindex, which
        an empty table does not need), check
        that each other index's expression (trailing blanks and case
        ignored) and key length match eng_xdef (else ENG_EINDEX;
        deleting the .NDX lets the next open rebuild it), and compile
        every index's key expression.  AUDIT2 is also built afresh
        when it is not the index of the AUDIT.DBF beside it, as far
        as one seek tells: AUDIT's last record must be the first
        entry of its user (files of two directories or releases
        copied together).
   The marker is deleted once the whole open has succeeded, so a failed
   open repeats the repair next time, except when it named ITEMS or
   LOANS, could not be read, or AUDIT.DBF holds records after the count
   of its count line: then the marker is kept (eng_marker_kept) for
   eng_init_data, which adopts it and removes it only after its repairs
   (so a stop during a repair repeats the whole repair at the next
   open).  A kept marker is first rewritten as its count line (0 when
   it could not be read) and one line per table it names (every table
   when it could not be read), so
   later changes can add their lines to it; a rewrite that fails fails
   the open, and a rewrite cut short leaves an empty marker (DOS sets
   the size at close), which reads as unreadable and repairs
   everything.
   Between steps 2 and 3 it holds ENG_NHANDLES + ENG_TMPHANDLES handles
   of SEQ.DBF at once and closes them: too few handles or entries of the
   system file table (FILES=) give ENG_EHANDLES there, before any table
   is opened.  A file held by another program gives ENG_EINUSE, a
   missing .DBT beside an existing table ENG_EIO. */
int eng_open_files(eng **out, const char *dir, const eng_time *now,
                   char *why, u16 whysz);

/* TABLES.C.  1 while the engine holds its files; 0 once an ADMIN
   command (ENGADMIN.H) has closed them and failed to open them again:
   the protocol layer then refuses every command that reads the data. */
int eng_is_open(eng *e);


/* TABLES.C.  The why text of eng_open and eng_open_files: what and
   text tx (ENG_TX_ of ENGTX.H), separated by ": " when both are not
   empty, cut to whysz bytes with the NUL (the text is left out when
   what leaves no room for the separator); NULL and ENG_TX_NONE count as
   empty.  Nothing is written when why is NULL or whysz 0. */
void eng_why(char *why, u16 whysz, const char *what, u16 tx);

/* TABLES.C.  The data directory prefix the engine was opened with. */
const char *eng_dir(eng *e);

/* Flush every file that has unwritten changes: SEQ, AUDIT, then the
   tables, none of whose record counts is written once SEQ or AUDIT has
   failed to flush.  The first error is returned. */
int eng_flush(eng *e);

/* Flush and close every file, in eng_flush's order and under its rule
   (dbf_drop closes the tables once SEQ or AUDIT failed), and free the
   engine, even on error.  An edit of e still open is cancelled and e's
   audit reservation dropped first, so no open edit or audit reservation
   outlives the engine. */
int eng_close(eng *e);

/* Records in table t, deleted ones included (dbf_reccount). */
u32 eng_reccount(eng *e, int t);

/* 1 when this open rebuilt the indexes of table t because the marker
   file ENG_EDITMARK named it (or could not be read), else 0; for the
   repairs eng_init_data makes after an interrupted change. */
int eng_repaired(eng *e, int t);

/* 1 when eng_open_files kept the marker for eng_init_data's repairs
   (it named ITEMS or LOANS, could not be read, or covers audit rows),
   else 0.  eng_marker_done, called by eng_init_data once the repairs'
   batch has removed it, makes it 0.  eng_marker_from gives the number
   of AUDIT records before those the kept marker covers (0 when it could
   not be read); the rows after them may describe changes that never
   reached the disk. */
int eng_marker_kept(eng *e);
void eng_marker_done(eng *e);
u32 eng_marker_from(eng *e);

/* Field name and text of the last failure: "" when there is none.  The
   text is copied out of far data into the engine's own buffer, and
   eng_errtx gives its number (ENG_TX_NONE when there is none). */
const char *eng_errfield(eng *e);
const char *eng_errmsg(eng *e);
u16 eng_errtx(eng *e);
void eng_clear_error(eng *e);

/* ---------------------------------------------------------------------
 * Actor and clock (TABLES.C).
 * --------------------------------------------------------------------- */

/* Station and user name written to the audit rows of the following
   calls; the protocol layer sets them before every request.  Longer
   names are cut to ENG_STALEN and ENG_USERLEN characters; the strings
   are copied. */
void eng_set_actor(eng *e, const char *station, const char *user);

/* Change only the user name of the actor (cut to ENG_USERLEN); user
   may be the string eng_actor gave. */
void eng_set_user(eng *e, const char *user);

/* The current actor; the strings belong to the engine. */
void eng_actor(eng *e, const char **station, const char **user);

/* Fix the clock at *t (also the date written in table headers, through
   dbf_set_today), or return to the DOS clock (dos_today, dos_time) with
   NULL. */
void eng_set_now(eng *e, const eng_time *t);
void eng_now(eng *e, eng_time *t);

/* The clock as text with a NUL: today as "YYYYMMDD" (ENG_DATELEN + 1
   bytes), today and the time "HH:MM:SS" (ENG_TIMELEN + 1) of one
   reading, so they never straddle midnight, and both as
   "YYYYMMDDHHMMSS" (ENG_STAMPLEN + 1). */
void eng_today(eng *e, char *out);
void eng_clock_text(eng *e, char *date, char *tm);
void eng_stamp(eng *e, char *out);

/* ---------------------------------------------------------------------
 * Record access (TABLES.C).
 *
 * Each table has one record buffer (that of its dbf).  Lookups and scans
 * load a record into it; eng_rec_text then reads its fields.
 * --------------------------------------------------------------------- */

/* A protocol KEY=value pair.  key is upper-case. */
typedef struct {
    const char *key;
    const char *val;
} eng_pair;

/* Load record recno of table t into its buffer. */
int eng_rec_read(eng *e, int t, u32 recno);

/* Field f of open table t (its dbf descriptor: name, type, len, dec,
   offset), NULL out of range; the pointer stays valid while the engine
   is open. */
const dbf_field *eng_field(eng *e, int t, u16 f);

/* Field number (0-based) of the named field of open table t,
   case-insensitive, or -1. */
int eng_field_num(eng *e, int t, const char *name);

/* The name of field f of open table t, "" out of range. */
const char *eng_field_name(eng *e, int t, u16 f);

/* Field f of the buffer as protocol text: C trimmed on the right; N
   trimmed ("" when blank); D "YYYYMMDD" or ""; L "T", "F" or ""; M "".
   out holds at least ENG_MAXVAL bytes. */
int eng_rec_text(eng *e, int t, u16 f, char *out);

/* Field f of table t's buffer as dbf_get_str gives it, into out (sz
   bytes, at least 1); "" when it does not fit or cannot be read. */
void eng_rec_str(eng *e, int t, u16 f, char *out, u16 sz);

/* 1 when want is "" or equals C field f of table t's buffer, ignoring
   the case of ASCII letters, else 0: a scan's filter on a field no
   wider than ENG_MAXFIND. */
int eng_rec_is(eng *e, int t, u16 f, const char *want);

/* ENG_OK when the filter value val is NULL or at most n characters,
   else ENG_EARG naming key (ENG_TX_FILTLONG). */
int eng_filter_len(eng *e, const char *val, u16 n, const char *key);

/* The value of N field f of table t's buffer, scaled as dbf_get_num
   gives it; 0 when blank or not a number. */
i32 eng_rec_num(eng *e, int t, u16 f);

/* Upper-case the letters a-z of s in place. */
void eng_upper(char *s);

/* Seek the exact key (padded with spaces to the index's key length) on
   index x and load the first live record whose key matches in full;
   *recno is 0 when there is none, as it is for a key longer than the
   index's key length.  Deleted records met on the way count toward no
   limit: their number for one key is bounded by the deletions since the
   last PACK. */
int eng_seek_live(eng *e, int x, const char *key, u32 *recno);

/* Parse a FIELDS= list ("A,B,C", case-insensitive, at most ENG_MAXFSPEC
   characters) of table t into field numbers; NULL or "" gives the
   fields flagged ENG_F_LIST.  ENG_EARG naming a field that is unknown,
   memo or hidden, or naming FIELDS when the text is too long, there are
   more than ENG_MAXFLIST fields or their widths add up to more than
   ENG_MAXRWIDTH.  idx holds ENG_MAXFLIST entries. */
int eng_fieldlist(eng *e, int t, const char *spec, u16 *idx, u16 *n);

/* Memo work buffer: ENG_MAXMEMO + 1 bytes. */
char *eng_memobuf(eng *e);

/* Read the memo of field f of table t's buffer into eng_memobuf, CR LF
   kept, NUL-terminated; *len receives its length.  A memo longer than
   ENG_MAXMEMO gives ENG_ECONSTR naming the field. */
int eng_memo_read(eng *e, int t, u16 f, u16 *len);

/* Module support, for the engine's own files only. */
dbf *eng_dbf(eng *e, int t);
dbt *eng_dbt(eng *e, int t);    /* NULL for a table without memo        */
ndx *eng_ndx(eng *e, int x);
kx  *eng_kx(eng *e, int x);     /* the index's compiled key expression  */

/* Record the failure and return code: field (NULL for none) is copied,
   at most DBF_NAMELEN characters, and tx is the number of its text
   (ENG_TX_ of ENGTX.H, ENG_TX_NONE for none). */
int eng_fail(eng *e, int code, const char *field, u16 tx);

/* Translate a DBF_E code (index: from an ndx_ call) into an ENG_E code,
   recording it as eng_fail does; DBF_OK gives ENG_OK. */
int eng_dbferr(eng *e, int rc, int index);

/* Translate a cf_ code (dbf_map_cf, then eng_dbferr) into an ENG_E
   code, recording it with text tx instead of the mapped one when tx is
   not ENG_TX_NONE; 0 gives ENG_OK. */
int eng_cferr(eng *e, int cfrc, u16 tx);

/* ---------------------------------------------------------------------
 * Audit (AUDIT.C).
 *
 * One AUDIT.DBF row per event or changed field, stamped with the clock
 * and the actor, with AUD_ID from SEQ and an entry in AUDIT1 and
 * AUDIT2.  tbl is the ENG_T_ table whose name goes into TBL.
 * --------------------------------------------------------------------- */
#define ENG_A_INSERT    'I'
#define ENG_A_UPDATE    'U'
#define ENG_A_DELETE    'D'     /* delete and undelete                  */
#define ENG_A_MOVE      'M'
#define ENG_A_PARENT    'P'
#define ENG_A_LOANOUT   'O'
#define ENG_A_RETURN    'R'
#define ENG_A_ADMIN     'K'
#define ENG_A_LOGIN     'L'
#define ENG_A_FAILED    'F'
#define ENG_A_PASSWORD  'W'
#define ENG_A_CUT       'X'     /* audit rows of a change cut off       */

typedef struct {
    char action;                /* ENG_A_                               */
    const char *fld;            /* field name or NULL                   */
    const char *oldval;         /* NULL as blank; cut to ENG_AUDVAL     */
    const char *newval;
} eng_audrow;

/* AUDIT.C.  Bracket a change of table t with the ENG_EDITMARK file:
   eng_mark creates it naming t (eng_mark_line), or, while an earlier
   bracket is still open, adds t's line, so the line is on disk before
   the first write it covers.  eng_unmark deletes it
   when the last open bracket closes; a missing marker is no error, nor
   is one that cannot be removed once the change is on disk: its bracket
   stays counted as open and later changes add to it.  Commits and audit
   rows written outside a commit are bracketed by the two.  A failed
   change leaves its bracket open (and the marker on disk) for the rest
   of the session, so later commits never take an open bracket as a
   reason to skip their flushes; only a batch does. */
int eng_mark(eng *e, int t);
void eng_unmark(eng *e);

/* AUDIT.C.  A batch: a change made of several commits (ITEMS.C,
   ASSY.C, LOANS.C) or the open-time repairs (eng_init_data).
   eng_batch_begin marks t (eng_mark) and sets the batch flag; while it
   is set, eng_edit_commit and eng_audit_end skip their flushes.
   eng_batch_end ends a batch whose steps ended with rc: it always calls
   eng_flush (AUDIT.DBF before the tables its rows describe) and clears
   the flag, and calls eng_unmark only when rc is ENG_OK and the flush
   succeeded.  It returns rc when that is not ENG_OK, else the flush's
   code; a failure keeps the marker, so a stop or a failure inside a
   batch is repaired by the next open's rebuild and repairs.  Batches do
   not nest. */
int eng_batch_begin(eng *e, int t);
int eng_batch_end(eng *e, int rc);

/* TABLES.C.  The batch flag, kept in the engine so that it ends with
   eng_close: eng_batch_on is 1 while a batch of e is open (EDIT.C and
   AUDIT.C ask it), else 0; only AUDIT.C sets it. */
int eng_batch_on(eng *e);
void eng_set_batch(eng *e, int on);

/* AUDIT.C.  Take over the bracket of a marker eng_open_files kept
   (eng_marker_kept): the bracket count becomes 1 on e whatever it was,
   the marker counts as already written for this session, and a batch
   is begun, so the repairs' own marks append to the kept marker and
   eng_batch_end(e, ENG_OK) removes it. */
void eng_mark_adopt(eng *e);

/* Reserve the IDs of n rows with one eng_seq_next; then append each with
   eng_audit_add (at most n), and end with eng_audit_end, which flushes
   AUDIT.DBF and its indexes (inside a batch it leaves them to
   eng_batch_end).  The row's strings are copied at once. */
int eng_audit_begin(eng *e, u16 n);
int eng_audit_add(eng *e, int tbl, u32 recid, const eng_audrow *row);
int eng_audit_end(eng *e);

/* eng_audit_begin, eng_audit_add and eng_audit_end for one row. */
int eng_audit(eng *e, int tbl, u32 recid, const eng_audrow *row);

/* Forget e's audit reservation and bracket count; for eng_close. */
void eng_audit_drop(eng *e);

/* ---------------------------------------------------------------------
 * Paging cursors and bounded scans (CURSOR.C).
 *
 * A cursor names the last entry a request examined:
 *   index:hexkey:recno
 * index is an index name (eng_xdef.name, e.g. ITEMS4) or, for a scan in
 * record order, a table name (ITEMS); hexkey is the entry's key in hex,
 * two digits per byte, with trailing spaces left off (empty in record
 * order); recno is the record number in hex.  Cursors are written in
 * upper case without leading zeros; on input names and hex digits are
 * case-insensitive, leading zeros are accepted, recno has 1..8 digits
 * and is not 0, and hexkey has an even number of digits, no more than
 * twice the index's key length.  A cursor that breaks these rules, names
 * another index or table than the scan's, or whose key does not start
 * with the scan's prefix gives ENG_ECURSOR.  The cursor carries no
 * filter: a repeated request repeats its filters, and the scan continues
 * strictly after the named entry.
 *
 * A scan steps through one index within a key prefix (or through the
 * records in order) and loads each live record that passes its filter
 * into the table's buffer.  Counting: positioning on the cursor, a
 * descent of the index to the first entry after (key, recno) that
 * binary-searches the child pages of a run of equal keys, counts
 * nothing.  After that every entry examined counts one, whether it is
 * returned, deleted or filtered out, its record read included.  The scan
 * stops before examining an entry once ENG_MAXREADS have been counted,
 * so each request moves the cursor forward by at least one entry.
 * --------------------------------------------------------------------- */
#define ENG_SCAN_RECORDS (-1)   /* eng_scan.ix: record order            */
#define ENG_SCAN_RRECORDS (-2)  /* eng_scan.ix: record order, last record
                                   first (the cursor format is that of
                                   ENG_SCAN_RECORDS; the scan continues
                                   with the records before the named one) */
#define ENG_BOUNDLEN    12      /* bytes of eng_scan.bound: a LOANS2 key
                                   up to its due date (year of return 4,
                                   due date ENG_DATELEN)                */

typedef struct eng_scan eng_scan;
typedef int (*eng_match_fn)(eng *e, eng_scan *s);

/* The filters of a scan or walk, "" for none: those of the item lists,
   as eng_item_filters (ENGITEM.H) fills them; other scans use them as
   their module says. */
typedef struct {
    char text[ENG_MAXFIND + 1]; /* ITEM FIND's text                     */
    char cat[ENG_CATLEN + 1];   /* CATEGORY                             */
    char stat[ENG_STATLEN + 1]; /* STATUS                               */
    char tag[ENG_TAGFILT + 1];  /* tags, joined by commas               */
    char works[ENG_NWORKS + 1]; /* WORKS letters                        */
    u8   flags;                 /* the module's switches                */
} eng_ifilt;

struct eng_scan {
    int  table;                 /* ENG_T_                               */
    int  ix;                    /* ENG_X_, ENG_SCAN_RECORDS or _RRECORDS */
    u16  plen;                  /* prefix length, 0 = whole index       */
    u16  reads;                 /* reads so far                         */
    int  state;                 /* CURSOR.C: not started, on, at end, or
                                   the ENG_E code of a failed scan      */
    u32  recno;                 /* last entry examined                  */
    u8   key[ENG_MAXKEYLEN];    /* its key                              */
    u8   prefix[ENG_MAXKEYLEN];
    eng_match_fn match;         /* NULL keeps every live record; else
                                   called on the loaded record, 1 keeps
                                   it and 0 skips it                   */
    eng_ifilt f;                /* filters, set by the module that
                                   opened the scan                      */
    u32  floc;
    u8   blen;                  /* 0, or 1..ENG_BOUNDLEN and at most the
                                   index's key length (index scans
                                   only; else eng_scan_next and
                                   eng_scan_more give ENG_EINTERNAL):
                                   the scan ends before the first entry
                                   whose first blen key bytes are not
                                   below bound, and eng_scan_more counts
                                   no entry from there on               */
    u8   bound[ENG_BOUNDLEN];
};

/* Start a scan of table t over index ix (or ENG_SCAN_RECORDS or
   ENG_SCAN_RRECORDS) within the plen-byte key prefix, continuing after
   the cursor text after (NULL or "" to start at the beginning).  The
   filters, match and blen are cleared; the caller sets them
   afterwards. */
int eng_scan_open(eng *e, eng_scan *s, int t, int ix,
                  const u8 *prefix, u16 plen, const char *after);

/* Load the next matching live record; *recno receives its number, or 0
   when the range is exhausted or ENG_MAXREADS is reached.  Between
   calls the caller moves no index or buffer of the scan's table. */
int eng_scan_next(eng *e, eng_scan *s, u32 *recno);

/* *more receives 1 if entries remain in the range after the last one
   examined (looked up in the index without reading a record), else 0.
   Returns ENG_OK, or the error of a failed index read, in which case
   *more is 0 and the caller must not treat the range as exhausted. */
int eng_scan_more(eng *e, eng_scan *s, int *more);

/* The cursor of the last entry examined; out holds ENG_CURSORMAX. */
void eng_scan_cursor(const eng_scan *s, char *out);

/* Whether a live record of table t has the len-byte key prefix key on
   index ix, by one scan: *found is 1 when one was found, 2 when
   ENG_MAXREADS entries of deleted records were read and more remain
   (unsettled), else 0.  Moves t's buffer. */
int eng_scan_any(eng *e, int t, int ix, const u8 *key, u16 len,
                 int *found);

/* Cursors for walks that are not key ranges (the assembly walks of
   ASSY.C).  eng_cursor_text writes the cursor of entry (key, recno) of
   index ix (key of the index's key length; trailing spaces are left
   off) into out (ENG_CURSORMAX bytes).  eng_cursor_parse reads after
   for index ix by the rules above, except the prefix rule, which the
   walk applies itself; key (the index's key length) receives the key
   padded with spaces.  ENG_ECURSOR when after breaks the rules. */
void eng_cursor_text(int ix, const u8 *key, u32 recno, char *out);
int eng_cursor_parse(eng *e, int ix, const char *after, u8 *key,
                     u32 *recno);

/* ---------------------------------------------------------------------
 * Users: levels and the login record (USERS.C; the functions are in
 * ENGUSER.H).  Levels: 1 read-only, 2 operator, 3 admin.
 * --------------------------------------------------------------------- */
#define ENG_LVL_READ    1
#define ENG_LVL_OPER    2
#define ENG_LVL_ADMIN   3

/* A login (eng_user_login, eng_user_resume): every member after a
   success, and the user that eng_user_login found after a failure. */
typedef struct {
    u8   level;                 /* USERS.LEVEL                          */
    int  mustchg;               /* 1: only a password change is allowed */
    char user[ENG_USERLEN + 1]; /* the stored, upper-case name          */
    char fullname[ENG_FULLNAMELEN + 1];
} eng_login;

#endif
