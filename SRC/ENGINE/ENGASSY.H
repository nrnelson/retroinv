/*
 * ENGASSY.H - the assemblies of the engine (ASSY.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  Include it after ENGINE.H.
 */
#ifndef ENGASSY_H
#define ENGASSY_H

/* ---------------------------------------------------------------------
 * Assemblies (ASSY.C).
 *
 * Containment is the PARENT_ID tree of ITEMS: 0 is a top-level item,
 * otherwise the ID of the item it is installed in.  ITEMS3
 * (STR(PARENT_ID,8)+STR(ITEM_ID,8)) lists the parts of an item in ID
 * order.  The rules kept here:
 *   - no cycle: an item is never installed in itself or in one of its
 *     own parts (the parent's ancestors are walked, at most
 *     ENG_MAXDEPTH of them);
 *   - no item lies deeper than level ENG_MAXDEPTH, a top-level item
 *     being at level 1;
 *   - an assembly (a top-level item and everything installed in it,
 *     at any depth) has at most ENG_MAXASSY ITEMS3 entries, deleted
 *     parts included until PACK removes them, so every walk of an
 *     assembly is bounded;
 *   - every installed item has the LOC_ID of its top-level item, so
 *     the contents of a location (ITEMS2) include installed parts;
 *   - an item with units lent, reserved or in use is not installed
 *     (LOANS.C holds top-level items only), so eng_item_held > 0
 *     implies PARENT_ID = 0.
 * Data written by another program may break the tree.  A PARENT_ID that
 * is negative or not a number names no item: the item counts as
 * installed, its chain as broken (REMOVE makes it top-level again).  A
 * chain is read up to ENG_MAXDEPTH items, so a PARENT_ID cycle reads as
 * a chain that passes ENG_MAXDEPTH; a walk down from an item that meets
 * the item again gives ENG_EIO.
 * A change of several items (the item named and its parts) is made as
 * one commit per changed item inside one ENG_EDITMARK bracket naming
 * ITEMS (eng_batch_begin), the parts first and the item named last, so
 * the request's visible effect on that item comes with its last
 * write.  An interrupted change leaves the marker; the next open
 * rebuilds the ITEMS indexes and eng_assy_heal gives every installed
 * item its top-level item's location again, which undoes the part
 * written.
 * Each change of an item stamps its UPDATED.  Audit: a PARENT_ID change
 * is one P row, a LOC_ID change one M row, of the item changed.
 * --------------------------------------------------------------------- */
#define ENG_MAXASSY     64      /* ITEMS3 entries of one assembly, its
                                   deleted parts included               */

/* ASSY INSTALL child parent.  Both live items (ENG_ENOTFOUND naming
   CHILD or PARENT).  ENG_ECONSTR naming CHILD when the child has units
   lent, reserved or in use (the reason of eng_item_hold); naming PARENT
   when the top-level item the child joins has an open row naming the
   child or one of its parts as host, or more rows of its own than
   ENG_MAXREADS lets it check (eng_loan_hosted), for a cycle (the parent
   is the child or one of its parts), for a part that would lie deeper
   than ENG_MAXDEPTH, or for an assembly that would pass ENG_MAXASSY
   entries (the top-level item's own entry counted with those of its
   parts).  The size and height counts go down into deleted parts and
   their own parts as well, so undeleting a part later can break neither
   limit; each stops once it passes ENG_MAXASSY.  A child installed
   elsewhere moves to the new parent; installing a child in its own
   parent again changes nothing.  The child and its parts take the
   location of the parent's top-level item; one that is negative or not
   a number (written by another program) is ENG_EIO naming PARENT. */
int eng_assy_install(eng *e, u32 child, u32 parent);

/* ASSY REMOVE child LOC=loc: the child becomes a top-level item at the
   location token loc (ENGLOCS.H; "0" for unknown), with its parts.
   ENG_ENOTFOUND naming CHILD, ENG_ECONSTR naming CHILD when it is not
   installed; ENG_EARG naming LOC when loc is empty or all spaces,
   ENG_ECONSTR naming LOC when it names no live location. */
int eng_assy_remove(eng *e, u32 child, const char *loc);

/* ASSY MOVE id LOC=loc: a top-level item and all its parts move to the
   location loc.  ENG_ENOTFOUND naming ID; ENG_ECONSTR naming ID for an
   installed item (its assembly moves instead); LOC as for REMOVE.
   Items already there are not written. */
int eng_assy_move(eng *e, u32 id, const char *loc);

/* A bounded walk of ITEMS3 that is not one key range.  ENG_W_BOM visits
   the parts of root depth first, each part before its own parts and
   parts in ID order, skipping deleted parts (a deleted item has no live
   parts: ITEM DEL refuses an item with live parts); ENG_W_LIST visits
   the items that have at least one live part, in ID order,
   ENG_W_LISTTOP only those that are top-level.  As for a scan, every
   ITEMS3 entry examined counts one, its record read included, and the
   walk stops before examining an entry once ENG_MAXREADS have been
   counted; restoring the position from a cursor (for ENG_W_BOM, the
   ancestors of the cursor's item, at most ENG_MAXDEPTH reads) counts
   nothing.  Cursors are ITEMS3 cursors (eng_cursor_text) naming the
   last entry examined; for the list walks, once an item has been
   decided (returned, or found filtered out), the cursor's key is the
   item's ID followed by ENG_IDLEN '9' digits, so the walk resumes with
   the next item.  A list walk counts each ITEMS3 entry examined and,
   once per item decided, the read of that item's record; a cursor
   naming a deleted part (the walk stopped before a live part decided
   its item) resumes with the entries after it under the same, still
   undecided, item.  A list walk examines an entry only while at least
   two of its ENG_MAXREADS remain (the entry and the item it may
   decide), so one request counts at most ENG_MAXREADS; an entry whose
   parent is not an ID (negative) is examined, counted and passed over.
   A list walk takes any well-formed ITEMS3 cursor and continues after
   its entry.  A BOM cursor whose item is not a part of root, at any
   depth, is ENG_ECURSOR; a BOM that meets root again below itself (a
   PARENT_ID cycle) fails with ENG_EIO.  Between calls the caller moves
   no ITEMS index or buffer. */
#define ENG_W_BOM       0
#define ENG_W_LIST      1
#define ENG_W_LISTTOP   2

typedef struct {
    int  kind;                  /* ENG_W_                               */
    int  state;                 /* ASSY.C: not started, on, at end, or
                                   the ENG_E code of a failed walk      */
    u16  reads;                 /* entries counted so far               */
    u16  depth;                 /* ENG_W_BOM: levels of last[] in use   */
    u32  root;                  /* ENG_W_BOM: the item walked           */
    u32  last[ENG_MAXDEPTH];    /* ENG_W_BOM: the part reached at each
                                   level below root; last[i] is a part
                                   of last[i - 1], last[0] of root      */
    u32  recno;                 /* record of the last entry examined    */
    u8   key[ENG_ITEMS3LEN];    /* the cursor's ITEMS3 key              */
} eng_walk;

/* ASSY BOM id: a walk of the parts of the live item id (ENG_ENOTFOUND
   naming ID), continuing after the cursor text after (NULL or "").
   ASSY LIST: a list walk (top 1: ENG_W_LISTTOP). */
int eng_assy_bom(eng *e, eng_walk *w, u32 id, const char *after);
int eng_assy_list(eng *e, eng_walk *w, int top, const char *after);

/* Load the next item of the walk into the ITEMS buffer; *recno receives
   its record number, or 0 when the walk is over or ENG_MAXREADS is
   reached; *level its level below the root for ENG_W_BOM (1 for a part
   of root itself), 0 for the list walks. */
int eng_walk_next(eng *e, eng_walk *w, u32 *recno, u16 *level);

/* *more receives 1 when the walk has entries left after the last one
   examined, else 0; as eng_scan_more. */
int eng_walk_more(eng *e, eng_walk *w, int *more);

/* The walk's cursor; out holds ENG_CURSORMAX. */
void eng_walk_cursor(const eng_walk *w, char *out);

/* ASSY PATH id: the record numbers of the ancestors of the live item
   id, its top-level item first and its parent last, into recnos
   (ENG_MAXDEPTH entries); *n is 0 for a top-level item.  ENG_ENOTFOUND
   naming ID; ENG_EIO when the chain passes ENG_MAXDEPTH or reaches an
   item that is missing or deleted (data changed by another program). */
int eng_assy_path(eng *e, u32 id, u32 *recnos, u16 *n);

/* ASSY CHILDREN id: a scan of the ITEMS3 prefix of the live item id
   (ENG_ENOTFOUND naming ID): its direct parts in ID order. */
int eng_assy_children(eng *e, eng_scan *s, u32 id, const char *after);

/* Give every live installed item the LOC_ID of its top-level item
   where it differs (an M row each, with the actor in effect).  One pass
   over ITEMS3 from PARENT_ID 1 up, whose entries come grouped by parent:
   the top-level location is found once per parent (its chain read up
   to ENG_MAXDEPTH items) and kept while that parent's entries last, so
   the work is linear in the entries plus ENG_MAXDEPTH reads per parent.
   The location is compared as a number, blank being 0, so a second
   pass writes nothing.  Each item changed has its
   UPDATED stamped.  Its M row is written and AUDIT.DBF and its indexes
   flushed before the item's commit, which writes no row of its own
   (eng_edit_quiet), so even inside the marker's batch a stop leaves no
   corrected item without its row; one that leaves the row without the
   correction gets the same row again from the next open's pass.  An
   installed item whose chain is broken (a parent missing, deleted or
   not an ID, the group of a negative PARENT_ID included), passes
   ENG_MAXDEPTH (a cycle included) or ends at a top-level location that
   is negative or not a number is left as it is, and the pass goes on.
   Unbounded: called only by eng_init_data, inside the marker's
   bracket. */
int eng_assy_heal(eng *e);

#endif
