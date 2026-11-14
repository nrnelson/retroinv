/*
 * ENGMAINT.H - the maintenance records of the engine (MAINT.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGMAINT_H
#define ENGMAINT_H

/* ---------------------------------------------------------------------
 * Maintenance records (MAINT.C).
 *
 * One MAINT.DBF record per repair, cleaning, recap, test, modification
 * or inspection of an item (MTYPE RP CL RC TS MD IN), with its MDATE,
 * SUMMARY, COST, DONE_BY and the DETAIL memo.  MAINT_ID comes from SEQ;
 * records are found by ID with eng_id_find.  Settable fields are those
 * without ENG_F_RO: MDATE, MTYPE, SUMMARY, COST, DONE_BY and DETAIL;
 * ITEM_ID is fixed when the record is added.  After ADD and every SET,
 * MTYPE is one of the codes and SUMMARY is not blank (else ENG_EARG
 * naming the field).  The protocol key NOTES stands for DETAIL in every
 * command: NOTES and DETAIL together are ENG_EARG naming NOTES, and a
 * failure of a NOTES value or of the memo names NOTES.  Audit: TBL
 * MAINT, REC_ID the MAINT_ID; ADD is one I row, SET a U row per changed
 * field (DETAIL as MEMO), DEL one D row.  Deleted records are not found
 * and are skipped by every scan.  Only ADD and LIST check that the item
 * is live: GET, SET, DEL, NOTES and RECENT also reach the records of an
 * item that has been deleted.
 * --------------------------------------------------------------------- */

/* MAINT ADD item: the live item (ENG_ENOTFOUND naming ITEM); MTYPE one
   of the codes and SUMMARY non-blank (ENG_EARG naming the field); MDATE
   defaults to today when not given (an empty MDATE= stores a blank).
   *id receives the new MAINT_ID. */
int eng_maint_add(eng *e, u32 item, const eng_pair *p, u16 n, u32 *id);

/* Load the live record id into the MAINT buffer; ENG_ENOTFOUND naming
   ID.  MAINT SET, DEL and NOTES fail the same way for a record that is
   not live. */
int eng_maint_get(eng *e, u32 id, u32 *recno);

/* MAINT SET id, MAINT DEL id. */
int eng_maint_set(eng *e, u32 id, const eng_pair *p, u16 n);
int eng_maint_del(eng *e, u32 id);

/* MAINT NOTES id (the DETAIL memo into eng_memobuf) and MAINT NOTES id
   ADD= (append a line, or stage the memo, as eng_item_addnote; failures
   name NOTES). */
int eng_maint_notes(eng *e, u32 id, u16 *len);
int eng_maint_addnote(eng *e, u32 id, const char *text, u8 how);

/* MAINT LIST item: the records of the live item (ENG_ENOTFOUND naming
   ITEM), oldest MDATE first (the MAINT1 prefix).  MAINT RECENT: every
   record, the most recently added first (ENG_SCAN_RRECORDS). */
int eng_maint_scan(eng *e, eng_scan *s, u32 item, const char *after);
int eng_maint_recent(eng *e, eng_scan *s, const char *after);

#endif
