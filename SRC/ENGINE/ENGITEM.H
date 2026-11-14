/*
 * ENGITEM.H - the items of the engine (ITEMS.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGITEM_H
#define ENGITEM_H

/* ---------------------------------------------------------------------
 * Items (ITEMS.C).
 *
 * Settable fields are those of ITEMS.DBF without ENG_F_RO: ITEM_ID, the
 * counters ON_LOAN, RESERVED, IN_USE and ATTACHED (ENGLOAN.H),
 * PARENT_ID, CREATED and UPDATED are maintained by the engine.
 * Every ADD and SET keeps these rules: CATEGORY names a live category
 * (else ENG_ECONSTR naming it), and a LOC_ID value, given as a location
 * token (ENGLOCS.H), is "", "0" or a live location (else ENG_ECONSTR
 * naming LOC_ID), the LOC_ID or 0 being stored; COND, the cosmetic
 * grade, is one of NEW EXC GD FR PR or blank (not graded), WORKS one of
 * Y (works), I (intermittently), N (does not work) and ? (not tested),
 * both stored upper case, STATUS A or D, and TAGS a list of tags
 * (ITEMS.C), stored normalized (else ENG_EARG naming the field, for TAGS
 * with ENG_TX_BADTAG); QTY, ACQ_PRICE and CUR_VALUE are not negative
 * (ENG_F_NONNEG) and QTY is not below the units held (eng_item_held;
 * ENG_ECONSTR naming QTY).  A COND or WORKS another program left in
 * lower case is upper-cased by the item's next commit, which audits the
 * change; any other value outside these rules fails every commit of the
 * item until it is set right.
 * An item that has a parent or children keeps its location: an assembly
 * moves as a whole, and an installed item always has its parent's
 * location.  Every change (SET, DEL, UNDEL, SPLIT, notes) sets UPDATED
 * to today.  Deleted items are not found by GET and are skipped by every
 * scan.
 * --------------------------------------------------------------------- */
#define ENG_ORD_ID      0       /* ITEMS1                               */
#define ENG_ORD_LOC     1       /* ITEMS2: by LOC_ID, then ITEM_ID      */
#define ENG_ORD_MFR     2       /* ITEMS4                               */
#define ENG_ORD_SERIAL  3       /* ITEMS5                               */

/* ITEM ADD: required CATEGORY; defaults QTY 1, COND blank, WORKS ?,
   STATUS A, PARENT_ID 0, counters 0, LOC_ID 0, CREATED and UPDATED
   today.  *id receives the new ITEM_ID. */
int eng_item_add(eng *e, const eng_pair *p, u16 n, u32 *id);

/* The checks of eng_item_add on the pairs p, in the same order and
   with the same results (location, defaults, settable fields, required
   fields, category, condition, works, tags, status, quantities),
   without reserving an ID or writing anything: the edit is cancelled.
   For the first pass of an import (CSV.C).  Like a refused ADD it moves
   the ITEMS, CATS and LOCS buffers, and a NOTES pair overwrites
   eng_memobuf, so its value must not point into that buffer. */
int eng_item_check(eng *e, const eng_pair *p, u16 n);

/* Load the live item id into the ITEMS buffer; *recno receives its
   record number, 0 (and no failure) when there is no live item id. */
int eng_item_seek(eng *e, u32 id, u32 *recno);

/* The same, ENG_ENOTFOUND "no such item" naming field (NULL for none)
   when there is no live item id. */
int eng_item_need(eng *e, u32 id, const char *field, u32 *recno);

/* eng_item_need naming no field. */
int eng_item_get(eng *e, u32 id, u32 *recno);

/* ITEM SET.  A LOC_ID change of an item that has a parent or children
   gives ENG_ECONSTR naming LOC_ID (assemblies move as a whole).  STATUS
   D (disposed) gives ENG_ECONSTR naming STATUS while a counter is above
   0 (the reason of eng_item_hold) or the item is installed
   (ENG_TX_INST). */
int eng_item_set(eng *e, u32 id, const eng_pair *p, u16 n);

/* ITEM DEL: ENG_ECONSTR while the item has live children (ITEMS3), when
   ENG_MAXREADS entries of deleted children leave that unsettled, or
   while a counter is above 0 (naming it, with the reason of
   eng_item_hold).  ITEM UNDEL: ENG_ENOTFOUND for an unknown item,
   ENG_ECONSTR for a live one; ENG_ECONSTR if its category or parent is
   no longer live, or (without a parent) its location.  An item with a
   parent takes the parent's current LOC_ID when it differs (an M audit
   row). */
int eng_item_del(eng *e, u32 id);
int eng_item_undel(eng *e, u32 id);

/* ITEM COPY: a new top-level item with the fields and notes of id
   (eng_edit_begin_copy), SERIALNO and COND blank, WORKS ?, counters 0,
   new dates: another unit, neither graded nor tested. */
int eng_item_copy(eng *e, u32 id, u32 *newid);

/* ITEM SPLIT: take qty units (the text of an N value; NULL as "") off
   item id into a copy as ITEM COPY makes it, but keeping COND and
   WORKS (the lot was graded and tested as a whole), holding QTY qty; id
   keeps the rest and gets UPDATED today.  A qty that is not a number
   above 0 is ENG_EARG naming QTY, one that would leave id fewer than one
   unit or than its held units ENG_ECONSTR naming QTY (ENG_TX_NOSTOCK).
   One batch naming ITEMS: the new item's I row and the U row of id's
   QTY are written, in that order, then the new item is committed
   without rows and flushed, and only then is id rewritten (no row of
   its own), so a stop leaves either nothing, or the new item and its
   rows, which eng_item_heal completes, or the whole split. */
int eng_item_split(eng *e, u32 id, const char *qty, u32 *newid);

/* The repair of a split cut off, for eng_init_data when the marker was
   kept.  Among the audit rows the marker covers it takes the last row
   of an ITEMS QTY by a user other than ENG_SYSTEM; only when the row
   just before it is the ITEMS I row of a live item, the item of the QTY
   row is live, its QTY still holds the row's old value, and that less
   the new item's QTY is the row's new value and no less than the units
   its open rows hold (eng_item_held), does the QTY get the new value,
   with UPDATED today and a U row by ENG_SYSTEM, flushed before the
   record.
   A later change of any QTY thus leaves an earlier split as it is. */
int eng_item_heal(eng *e);

/* ITEM NOTES: load the item and read its memo into eng_memobuf. */
int eng_item_notes(eng *e, u32 id, u16 *len);

/* ITEM NOTES id ADD=: append text as a new line of the memo (CR LF
   between the old text and the new), checked and converted as an M
   value of eng_edit_put; ENG_ECONSTR naming NOTES if the result exceeds
   ENG_MAXMEMO.  how ENG_NOTE_ADD; the other hows (ENGNOTE.H) stage the
   memo, and the one with ENG_NOTE_END writes it. */
int eng_item_addnote(eng *e, u32 id, const char *text, u8 how);

/* The units of the item in the ITEMS buffer that its open rows hold:
   ON_LOAN + RESERVED + IN_USE. */
i32 eng_item_held(eng *e);

/* How the item in the ITEMS buffer stands, one letter: ENG_AV_YES when
   it is available (STATUS A, top level and QTY above its held units),
   else why not: ENG_AV_GONE for a STATUS other than A, ENG_AV_PART when
   it is installed, the kind (ENG_K_ of ENGLOAN.H) of the first of
   ON_LOAN, RESERVED and IN_USE above 0, or ENG_AV_NONE when it holds no
   unit at all. */
#define ENG_AV_YES      'Y'     /* available                            */
#define ENG_AV_GONE     'D'     /* disposed of                          */
#define ENG_AV_PART     'I'     /* installed in another item            */
#define ENG_AV_NONE     'N'     /* QTY 0                                */
char eng_item_avail(eng *e);

/* Why the item in the ITEMS buffer cannot be disposed of or deleted:
   ENG_TX_ONLOAN, ENG_TX_HELDRES, ENG_TX_HELDUSE or ENG_TX_ATTACHED for
   the first of ON_LOAN, RESERVED, IN_USE and ATTACHED above 0, *f
   receiving its field number, else ENG_TX_NONE.  When eng_item_held is
   above 0 it is one of the first three, the reason an item is not
   installed. */
u16 eng_item_hold(eng *e, u16 *f);

/* The switches of an item list (eng_item_filters' flags).  ENG_IF_AVAIL
   keeps the available items (eng_item_avail), ENG_IF_UNAVAIL every
   other live item. */
#define ENG_IF_TOP      1       /* top-level items only (PARENT_ID 0)   */
#define ENG_IF_AVAIL    2       /* available items only                 */
#define ENG_IF_UNAVAIL  4       /* unavailable items only               */

/* The filters of an item list into *f, as eng_item_scan and
   eng_loc_contents apply them; cat, status, tag and works are NULL or ""
   for none:
   - text, NULL for none, else 1 .. ENG_MAXFIND characters (or ENG_EARG
     naming TEXT): items holding it in MFR, MODEL, DESCR, TAGS, SERIALNO
     or ITEMTYPE, A-Z folded;
   - cat and status: the items of that CATEGORY or STATUS, compared
     ignoring case (ENG_EARG naming CAT or STATUS when longer than the
     field; a status beside ENG_IF_AVAIL or ENG_IF_UNAVAIL in flags is
     ENG_EARG naming STATUS);
   - tag: a list of tags, at most ENG_TAGFILT characters (else ENG_EARG
     naming TAG, ENG_TX_FILTLONG): the items whose TAGS hold every one,
     whole and A-Z folded; a word that is no tag, or a list of
     separators only, is ENG_EARG naming TAG (ENG_TX_BADTAG);
   - works: 1 .. ENG_NWORKS of the letters Y I N ?, any case, each at
     most once (else ENG_EARG naming WORKS): the items whose WORKS is one
     of them;
   - flags: the switches ENG_IF_. */
int eng_item_filters(eng *e, eng_ifilt *f, const char *text,
                     const char *cat, const char *status, const char *tag,
                     const char *works, int flags);

/* 1 when the item in the ITEMS buffer passes the filters f. */
int eng_item_keep(eng *e, const eng_ifilt *f);

/* ITEM LIST, ITEM FIND and REPORT ITEMS with the filters f.  Without a
   text filter, order is ENG_ORD_ (else ENG_EARG naming ORDER); with loc
   set and ENG_ORD_LOC the scan is the STR(LOC_ID,8) prefix of ITEMS2.
   With one (ITEM FIND) the scan is in record order and order is not
   used.  loc is a location token (ENGLOCS.H) keeping the items at that
   location, "0" those of unknown location; one that names no live
   location is ENG_ENOTFOUND naming LOC.  A filter value that names
   nothing matches nothing. */
int eng_item_scan(eng *e, eng_scan *s, int order, const eng_ifilt *f,
                  const char *loc, const char *after);

/* ITEM TAGS: the tags of the live items, any STATUS, from at most
   ENG_MAXREADS records in record order after the cursor after (NULL or
   "" to start), each tag once (A-Z folded, the first spelling met) in
   A-Z folded order, words that are no tag skipped.  They go into
   eng_memobuf, each ended by a NUL, *len bytes in all (0 for none), at
   most ENG_TAGSET: the request also ends before a record whose tags
   might not fit.  cursor (ENG_CURSORMAX bytes) receives the scan's
   cursor while records remain, else "". */
int eng_item_tags(eng *e, const char *after, u16 *len, char *cursor);

/* ITEM SERIAL: items whose SERIALNO equals sn ignoring case (ITEMS5);
   sn longer than the field gives ENG_EARG naming SERIAL. */
int eng_item_serial(eng *e, eng_scan *s, const char *sn,
                    const char *after);

#endif
