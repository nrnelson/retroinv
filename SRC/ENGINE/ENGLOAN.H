/*
 * ENGLOAN.H - the loans, reservations and in-use assignments of the
 * engine (LOANS.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGLOAN_H
#define ENGLOAN_H

/* ---------------------------------------------------------------------
 * Loans, reservations and in-use assignments (LOANS.C).
 *
 * One LOANS.DBF record (a row) takes QTY units of an item out of stock
 * from OUT_DATE until its RET_DATE is set (a return, or a release in the
 * clients); it is open while RET_DATE is blank.  Its KIND says how:
 *   L  lent to HOLDER (the borrower), due back on DUE_DATE; both given,
 *      HOST_ID 0;
 *   R  reserved for HOLDER (a purpose or a person) or for the item
 *      HOST_ID, one of them given, until DUE_DATE if one is given;
 *   U  in use with HOLDER or with the item HOST_ID, one of them given,
 *      DUE_DATE blank.
 * A HOST_ID is 0 or a live item with STATUS A that is neither the row's
 * item nor one of its parts.  Only a live, top-level item with STATUS A
 * is held, and a new row's QTY is at most the item's QTY less its held
 * units (ON_LOAN + RESERVED + IN_USE, eng_item_held).  ITEMS.ON_LOAN,
 * RESERVED and IN_USE of every item are the sums of the QTY of its open
 * L, R and U rows, and ATTACHED the number of open R and U rows naming
 * it as HOST_ID.  A change raises or lowers them in their items' own
 * commits (audited as O or R, ENG_F_AUDLOAN, with UPDATED stamped)
 * after the row's commit, all in one batch naming LOANS
 * (eng_batch_begin), so an interrupted change leaves the marker and the
 * next open recomputes them (eng_loan_heal).  LOAN_ID comes from SEQ;
 * rows are found by ID with eng_id_find.  The protocol keys DUE, DATE
 * and HOST may be given for DUE_DATE, OUT_DATE and HOST_ID (RET_DATE
 * for RETURN); errors name the field.  Audit: TBL LOANS, REC_ID the
 * LOAN_ID; a new row is one I row, RETURN a U row for RET_DATE, SET a U
 * row per changed field.
 * --------------------------------------------------------------------- */
#define ENG_K_LOAN      'L'     /* KIND of a loan                       */
#define ENG_K_RESERVE   'R'     /* of a reservation                     */
#define ENG_K_USE       'U'     /* of an in-use assignment              */

/* LOAN OUT item (also LOAN ADD), LOAN RESERVE item and LOAN USE item: a
   row of kind (ENG_K_) of the live, top-level item with STATUS A
   (ENG_ENOTFOUND naming ITEM; ENG_ECONSTR naming ITEM when it is
   installed or disposed).  Pairs QTY (default 1, at least 1, ENG_EARG
   naming QTY), HOLDER, CONTACT, OUT_DATE (default today; given empty it
   is today too, so every row has one, since LOAN SET cannot set it),
   NOTES, and by kind DUE_DATE (L and R) and HOST_ID (R and U); any
   other key is ENG_EARG naming it (ENG_TX_NOSET).  A loan without
   HOLDER or DUE_DATE is ENG_EARG naming the field (ENG_TX_REQUIRED), a
   reservation or assignment without HOLDER or HOST_ID ENG_EARG naming
   HOLDER (ENG_TX_NOHOLDER), a DUE_DATE before OUT_DATE ENG_EARG naming
   DUE_DATE.  ENG_ECONSTR naming QTY when QTY passes the item's QTY less
   its held units (ENG_TX_NOSTOCK).  A HOST_ID that is no live item is
   ENG_ENOTFOUND naming HOST_ID; one that is disposed, the item itself
   or one of its parts ENG_ECONSTR naming HOST_ID.  *id receives the new
   LOAN_ID. */
int eng_loan_out(eng *e, u32 item, int kind, const eng_pair *p, u16 n,
                 u32 *id);

/* The check ASSY INSTALL makes of the top-level item that the child
   joins (ITEMS record top) and the child (record child): ENG_ECONSTR
   naming field (ENG_TX_HOSTSELF) when an open R or U row of top names
   the child or one of its parts as HOST_ID: top would then be used with
   one of its own parts.  Only when top's RESERVED + IN_USE is above 0
   are top's LOANS1 entries read, ended rows skipped, until its open R
   and U rows hold that many units, and each host named on the way up
   from it (eng_assy_path).  A walk that would read more than
   ENG_MAXREADS of top's entries is refused, naming field, as top's
   counter gives the reason (ENG_TX_HELDRES, else ENG_TX_HELDUSE).
   Moves the ITEMS and LOANS buffers. */
int eng_loan_hosted(eng *e, u32 top, u32 child, const char *field);

/* LOAN RETURN id, a row of any kind: RET_DATE is date ("YYYYMMDD"; NULL
   or "" today, not before OUT_DATE, else ENG_EARG naming RET_DATE); the
   item's counter of the row's kind falls by the row's QTY and its
   host's ATTACHED by 1, neither below 0, and an item or host that has
   been deleted is left alone.  ENG_ENOTFOUND naming ID, ENG_ECONSTR
   naming ID for a row already ended. */
int eng_loan_return(eng *e, u32 id, const char *date);

/* Load the live row id into the LOANS buffer; *recno receives its
   record number (0 on failure).  ENG_ENOTFOUND naming ID when there is
   no live row id. */
int eng_loan_get(eng *e, u32 id, u32 *recno);

/* LOAN SET id: pairs HOLDER, CONTACT, NOTES, and by the row's kind
   DUE_DATE (L and R) and HOST_ID (R and U), under the rules of
   eng_loan_out; any other field is ENG_EARG naming the key
   (ENG_TX_NOSET).  An open row given another host moves one from the
   old host's ATTACHED to the new one's, in a batch as eng_loan_out's. */
int eng_loan_set(eng *e, u32 id, const eng_pair *p, u16 n);

/* LOAN LIST: with item non-zero the rows of that live item (ENG_ENOTFOUND
   naming ITEM) in LOAN_ID order (the LOANS1 prefix; open 1 filters the
   open ones), else LOANS2 order: the open rows by DUE_DATE (the LOANS2
   prefix "   0", the year of a blank RET_DATE), then the ended ones by
   year of return and DUE_DATE; open 1 scans that prefix only.  host
   non-zero keeps the open rows naming that live item as HOST_ID
   (ENG_ENOTFOUND naming HOST), kind non-zero (ENG_K_) the rows of that
   kind, and holder, when not NULL or "", the rows whose HOLDER equals
   it ignoring case (ENG_EARG naming HOLDER when it is longer than the
   field); all of them must match, and they filter the scan, whose rows
   they pass over still count towards ENG_MAXREADS.  LOAN OVERDUE: the
   open rows whose DUE_DATE is before asof ("YYYYMMDD"; NULL or ""
   today, else a valid date or ENG_EARG naming ASOF), by DUE_DATE, so
   loans and reservations: the same prefix, ended by the bound "   0"
   followed by asof (eng_scan.bound), rows with a blank DUE_DATE (which
   sort first) skipped. */
int eng_loan_scan(eng *e, eng_scan *s, u32 item, u32 host, int kind,
                  const char *holder, int open, const char *after);
int eng_loan_overdue(eng *e, eng_scan *s, const char *asof,
                     const char *after);

/* The check of asof that eng_loan_overdue makes, alone: ENG_OK for NULL,
   "" or a valid date, else ENG_EARG naming ASOF. */
int eng_loan_asof_ok(eng *e, const char *asof);

/* 1 when the row in the LOANS buffer is open and its DUE_DATE is before
   asof (NULL or "": today), else 0; a blank DUE_DATE is not late. */
int eng_loan_late(eng *e, const char *asof);

/* Load the item of the row in the LOANS buffer into the ITEMS buffer
   (one ITEMS1 lookup); *recno is 0 when it is not a live item. */
int eng_loan_item(eng *e, u32 *recno);

/* Set the counters of every live item to what the open rows give where
   they differ (an O or R row each, with the actor in effect, and
   UPDATED stamped): ON_LOAN, RESERVED and IN_USE the QTY of its open
   rows of their kinds, each capped at what the item's QTY leaves after
   the ones before it, ATTACHED the number of open R and U rows naming
   it.  One pass over LOANS1, whose entries come grouped by item, sums
   the open rows of each item and corrects it when the group ends, and
   counts the hosts of its open R and U rows whose ATTACHED is 0; a
   second pass over the ITEMS records sets ON_LOAN, RESERVED and IN_USE
   to 0 on every item with one of them other than 0 that has no LOANS1
   entry (one LOANS1 seek each) and counts every item whose ATTACHED is
   other than 0.  A host
   is counted on a walk of the open prefix of LOANS2, so the repair reads
   the open rows once per host: quadratic in the worst case, linear in
   the usual one of few hosts.  Each correction's row is written and
   AUDIT.DBF and its indexes flushed before the item's commit, which
   writes no row of its own (eng_edit_quiet), so even inside the
   marker's batch a stop leaves no corrected item without its row; one
   that leaves the row without the correction gets the same row again
   from the next open's pass.  Unbounded: called only by eng_init_data,
   inside the marker's bracket. */
int eng_loan_heal(eng *e);

#endif
