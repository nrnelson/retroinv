/*
 * ENGEDIT.H - record edits (EDIT.C), the engine modules' one way of
 * writing a record.
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that edit records pay the compiler
 * memory for it (Watcom C 6.5 runs out of memory on the largest
 * programs otherwise).  Include it after ENGINE.H.
 *
 * eng_edit_begin saves the record's image and index keys, the put and
 * apply functions change the buffer, and eng_edit_commit writes it,
 * updates every index of the table whose key changed (the old entry
 * removed, the new one inserted), writes the audit rows and flushes.
 * One edit is open at a time.  Between begin and commit or cancel the
 * table's buffer belongs to the edit: lookups and scans of the same
 * table (eng_seek_live on its indexes, eng_rec_read, eng_scan_next) are
 * not allowed, so modules do their checks before eng_edit_begin;
 * lookups of other tables are allowed.  In particular the commit does
 * not check unique keys: every edit that sets one (a location's code
 * or name, a user's name, a code table's code) first looks it up and
 * gives ENG_EDUP; with a single writer nothing can come between that
 * check and the commit.
 */
#ifndef ENGEDIT_H
#define ENGEDIT_H

/* Start an edit of record recno of table t (0: a new, blank record). */
int eng_edit_begin(eng *e, int t, u32 recno);

/* Start a new record of table t holding a copy of record srcrecno
   (memo fields included: their text is written to new blocks at
   commit). */
int eng_edit_begin_copy(eng *e, int t, u32 srcrecno);

/* Start a new record of table t that will be written over the deleted
   record recno instead of being appended: the buffer starts blank and
   live, and the commit writes it at recno, maintains the indexes and
   audits it as an insert (one I row), exactly as for a new record.
   ENG_ECONSTR if recno is not a deleted record. */
int eng_edit_begin_reuse(eng *e, int t, u32 recno);

/* Store the text value of a field, converted and checked as the field's
   type requires: C at most len characters, no byte below 20 or equal to
   7F, upper-cased if ENG_F_UPPER; N ("" stores a blank) an optional
   '-', digits and at most dec decimals that fit the width, a value below
   zero refused with ENG_F_NONNEG; D "YYYYMMDD" of a valid date or "";
   L T, F, Y or N in either case, or ""; M up to ENG_MAXMEMO bytes
   (counted as stored) of bytes 20 and above, TAB and LF, each LF not
   preceded by CR stored as CR LF (a CR not followed by LF is refused),
   written to the .DBT at commit.  ENG_EARG with the field name
   otherwise. */
int eng_edit_put(eng *e, const char *field, const char *val);

/* The same for field number f of the edited table, for modules that
   name their fields by number (ENG_FIELD) and keep no field names; an
   f that is not a field of the table is ENG_EINTERNAL.  putnumf stores
   the scaled value of an N field: ENG_EARG naming the field if it is
   not N, is negative with ENG_F_NONNEG or does not fit the width. */
int eng_edit_putf(eng *e, u16 f, const char *val);
int eng_edit_putnumf(eng *e, u16 f, i32 scaled);

/* Set memo field f (a field number, as for eng_edit_putf) to the first
   len bytes of eng_memobuf, written at commit as they are: the bytes
   are not converted or checked, so the caller has built them as
   eng_edit_put would store them.  ENG_EARG naming the field if it is
   not M; a len over ENG_MAXMEMO is ENG_ECONSTR naming it; ENG_EINTERNAL
   if the edit already holds a value for another memo field. */
int eng_edit_putmemo(eng *e, u16 f, u16 len);

/* Start an edit of record recno of table t, loaded in its buffer, that
   appends text as a new line to its memo field f: CR LF separates it
   from the old text (none when that is empty), and the text is checked
   and converted as an M value of eng_edit_put (ENG_EARG naming f);
   ENG_ECONSTR naming f if the memo would exceed ENG_MAXMEMO.  The edit
   is left open, for the module to put more fields and end it with
   eng_edit_finish; on failure no edit is open.
   That is how ENG_NOTE_ADD (ENGNOTE.H) treats the old text.  With
   ENG_NOTE_NEW it is empty, and with ENG_NOTE_PART it is the stage in
   table t of how's tag and the actor, which must be one of record
   recno by the same tag, station and user (else ENG_ECURSOR naming f,
   ENG_TX_NOSTAGE).  Without ENG_NOTE_END these two open no edit: they
   write the new text as that stage (a file of ENG_NOTESTAGE: the
   record number, the tag, the station and the user, then the text), a
   stage not written whole being removed, and one refused before
   leaving it as it was.
   ENG_NOTE_PART with ENG_NOTE_END removes the actor's stage once it
   has read it, whatever the outcome; a stage not the actor's is left
   alone. */
int eng_edit_begin_note(eng *e, int t, u16 f, u32 recno,
                        const char *text, u8 how);

/* Apply pairs as eng_edit_put does.  A key that is not a field of the
   table, or names a field with any of the flags in deny, gives ENG_EARG
   naming the key; nothing is stored then.  n is at most ENG_MAXPAIRS;
   more gives ENG_EINTERNAL. */
int eng_edit_apply(eng *e, const eng_pair *p, u16 n, u16 deny);

/* Fields of ENG_F_REQ that are blank give ENG_EARG naming the first. */
int eng_edit_required(eng *e);

/* Mark the edited record deleted (1) or live (0). */
void eng_edit_delete(eng *e, int deleted);

/* Write the edit.  recid is the value written to AUDIT.REC_ID (0: the
   record number).  The audit rows: a new record gives one row with
   ACTION I whose FLD is the table's first field and NEWVAL that field's
   value; a change of the deleted mark one row with ACTION D, FLD blank
   and "DELETED" as NEWVAL (delete) or OLDVAL (undelete); every other
   changed field one row with ACTION U, M (ENG_F_AUDMOVE), P
   (ENG_F_AUDPAR), or O or R (ENG_F_AUDLOAN: the value rose or fell; a
   change that leaves the value equal, such as blank to 0, gives no row)
   and the old and new text cut to ENG_AUDVAL characters, or "MEMO" for
   a memo; fields flagged ENG_F_NOAUDIT or ENG_F_HIDDEN give no row. */
int eng_edit_commit(eng *e, u32 recid);

/* The end of an edit whose steps ended with rc: when rc is not ENG_OK
   the edit is cancelled and rc returned, else eng_edit_commit(e,
   recid). */
int eng_edit_finish(eng *e, int rc, u32 recid);

/* Make the open edit's commit write no audit rows; nothing else about
   it changes.  For the open-time repairs (ITEMS.C, ASSY.C, LOANS.C),
   which write a correction's row with eng_audit and flush AUDIT.DBF and
   its indexes before the commit, so that a stop leaves no correction
   without its row (at worst a row whose correction the next open makes
   again), and for ITEM SPLIT, which writes its rows before its new
   item. */
void eng_edit_quiet(eng *e);

/* Commit an open-time repair: on the open edit, whose values the module
   has put, write one audit row (ACTION action, FLD field f, OLDVAL was,
   NEWVAL f's text in the buffer now as eng_rec_str gives it), flush
   AUDIT.DBF and its indexes, then commit quietly (eng_edit_quiet) with
   recid; on any failure the edit is cancelled. */
int eng_edit_heal(eng *e, u16 f, const char *was, char action,
                  u32 recid);

/* Abandon the edit and restore the buffer. */
void eng_edit_cancel(eng *e);

#endif
