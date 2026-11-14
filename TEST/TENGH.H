/*
 * TENGH.H - engine scaffolding of the unit tests (TENGH.C): saving the
 * data directory, opening the engine, building pairs, reading and
 * writing records past the engine, and checking failed calls.
 *
 * The data directory is the scratch directory of T_FILES.H; te_remove
 * (TEWIPE.H) empties it.  te_open opens the engine there, and the
 * readers and writers below work on the engine of the last te_open
 * (none after a failed one), until the test closes it.  The readers
 * report a failure through their result ("?" or 0) and check nothing;
 * te_fail and te_kv record their own checks through TESTFW.H.
 */
#ifndef TENGH_H
#define TENGH_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "T_FILES.H"

#define TE_TEXTMAX      300     /* a te_kv spec or a te_aud row, NUL in */
#define TE_MAXP         ENG_MAXPAIRS    /* pairs of one te_kv spec      */
#define TE_BUF          0xFFFFFFFFUL    /* recno of te_rtext, te_rnum:
                                           the record in the buffer     */

/* ---------------------------------------------------------------------
 * Files and the engine.
 * --------------------------------------------------------------------- */

/* Save the engine's files of the data directory under other names
   (save 1), or restore them from those (save 0), with tfs_copy: every
   table's .DBF and .DBT as .SDF and .SDT, every index's .NDX as .SNX,
   and ENG_EDITMARK with the extension .S$$.  A file missing from the
   source leaves its target removed, so a state saved without it is
   restored without it; a test therefore cleans up at its end with
   te_remove(tfs_dir(), NULL) and then te_save(1), which removes every
   saved copy.  Returns 0 or the first cf_ code. */
int te_save(int save);

/* eng_open on the data directory with the clock now, then the actor
   STA1/TESTER.  On failure *e is NULL and the engine's text is printed
   as "eng_open: why", except while any fault hook is set (cf_fault not
   NULL, a counting-only tfs_fault recorder included).  Returns the
   eng_open code. */
int te_open(eng **e, const eng_time *now);

/* Write the marker ENG_EDITMARK naming the tables of tables, '|'
   separated ("ITEMS|LOANS"), one line each, as an interrupted change
   leaves it.  Returns 0 or a cf_ code (CF_EINVAL when it is longer
   than ENG_MARKMAX). */
int te_marker_put(const char *tables);

/* ---------------------------------------------------------------------
 * Pairs, records and audit rows.
 * --------------------------------------------------------------------- */

/* The pairs of the last te_kv; they point into its copy of the spec. */
extern eng_pair te_p[TE_MAXP];

/* Split spec "KEY=value|KEY=value" (values may hold spaces; an item
   without '=' has the value "") into te_p; returns the count, 0 for an
   empty spec.  A spec of TE_TEXTMAX characters or more, or of more than
   TE_MAXP pairs, records a failed check and returns 0. */
u16 te_kv(const char *spec);

/* LOC ADD on the engine of the last te_open: a location of type BIN in
   parent (0: at the top) whose LOC_NAME and LOC_CODE are code.  Its
   LOC_ID, or 0 after a failed check when the ADD fails.  Moves te_p. */
u32 te_loc(const char *code, u32 parent);

/* The text of field name of record recno of table t (TE_BUF: the
   record already in t's buffer), or "?" when the record (recno 0
   included) or the field cannot be read; in one static buffer. */
const char *te_rtext(int t, u32 recno, const char *name);

/* The same as a number (eng_rec_num of ENGINE.H), 0 when it cannot be
   read (recno 0 included). */
i32 te_rnum(int t, u32 recno, const char *name);

/* The ITEMS record of item id, live or deleted, or 0 when there is
   none. */
u32 te_irec(u32 id);

/* Store text (NULL: the number num) in field name of record recno of
   table t straight into the file, as a hand edit or a change stopped
   before its index writes leaves it, moving the record's entry in
   index x when x >= 0, then flush the engine.  Returns 0 or the
   ENG_E or DBF_E code of the step that failed (DBF_ERANGE for an
   unknown field). */
int te_raw_put(int t, u32 recno, int x, const char *name,
               const char *text, i32 num);

/* The number of AUDIT.DBF records. */
u32 te_naud(void);

/* The fields of audit record recno (counted from 1, absolute) named in
   the space-separated list fields ("TBL REC_ID ACTION"), their texts
   joined by '|' ("ITEMS|1|I"), or "?" when the record cannot be read (a
   field that cannot be read gives "?" in its place); in one static
   buffer of TE_TEXTMAX bytes.  Values may hold spaces, so '|' keeps the
   field boundaries plain; a test therefore stores no '|' in a value it
   reads back this way (te_kv cannot pass one anyway). */
const char *te_aud(u32 recno, const char *fields);

/* ---------------------------------------------------------------------
 * Checks.
 * --------------------------------------------------------------------- */

/* Note the record count of every table and whether ENG_EDITMARK is
   there, for te_fail. */
void te_snap(void);

/* Check that rc is code, that the engine names field (NULL: no field)
   and gives the text msg (NULL: any), and that the tables' counts and
   the marker are as te_snap left them.  On a mismatch a line naming
   the caller's line is printed.  The engine's error is cleared either
   way.  Records one check, as at file and line (file as TESTFW.H's
   TF_FILE gives it); returns 1 when it passed. */
int te_fail(int rc, int code, const char *field, const char *msg,
            const char *file, int line);

/* The snapshot, the call, and the check of its failure. */
#define TE_FAIL(call, code, field, msg) \
    (te_snap(), te_fail((call), (code), (field), (msg), TF_FILE, __LINE__))

/* 1 when index x of e holds exactly one entry per record of its table,
   each with the key the expression gives for that record.  On 0,
   te_index_rec is the record of the first entry that failed its check,
   or 0 when the entries and the records differ in number or the index
   cannot be walked. */
extern u32 te_index_rec;
int te_index_ok(eng *e, int x);

/* ---------------------------------------------------------------------
 * Flagged audit rows, declared only where TE_AUDCUT is defined before
 * this header is included (the compiler memory of Watcom C 6.5 is
 * tight in some tests).
 * --------------------------------------------------------------------- */
#ifdef TE_AUDCUT

/* The records counted in the header of table t's .DBF in the data
   directory, read straight from the file (no engine needed), or 0 when
   it cannot be read. */
u32 te_disk_count(int t);

/* The X rows (ENG_A_CUT) among the AUDIT records after record from:
   their number, or -1 when a record after from and before the last of
   them is neither an X row nor flagged by a later one (its AUD_ID from
   that row's REC_ID up to its NEWVAL), or when there are more than
   TE_MAXCUT. */
#define TE_MAXCUT       4
int te_cut(u32 from);

#endif

#endif
