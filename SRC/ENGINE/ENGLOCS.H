/*
 * ENGLOCS.H - the locations of the engine (LOCS.C).
 *
 * Kept out of ENGINE.H, which every engine and protocol module reads,
 * so that only the modules and tests that use these functions pay the
 * compiler memory for them (Watcom C 6.5 runs out of memory on the
 * largest test programs otherwise).  For the same reason the walks of
 * the tree are declared only where ENGLOCS_WALKS is defined before this
 * header is included.  Include it after ENGINE.H.
 */
#ifndef ENGLOCS_H
#define ENGLOCS_H

/* ---------------------------------------------------------------------
 * Locations (LOCS.C).
 *
 * The locations form a tree through PARENT_ID: 0 for a top-level
 * location, else the LOC_ID of the live location holding it.  A
 * top-level location is at depth 1.  LOC_ID comes from SEQ and never
 * changes; an item names its location by it (ITEMS.LOC_ID, 0 for
 * unknown), and so does AUDIT.REC_ID.  Every ADD and SET keeps these
 * rules:
 *   - LOC_NAME is unique among the live locations of one parent, A-Z
 *     folded (ENG_EDUP naming LOC_NAME), so a path names one location;
 *   - LOC_CODE is blank or 1..ENG_LOCLEN characters, the first A-Z and
 *     the others A-Z, 0-9 or '-', upper-cased on input (else ENG_EARG
 *     naming LOC_CODE), and unique among the live locations (ENG_EDUP
 *     naming LOC_CODE);
 *   - LTYPE names a live location type (ENG_ECONSTR naming LTYPE);
 *   - PARENT_ID is 0 or a live location (ENG_ECONSTR naming PARENT_ID),
 *     never the location itself or one below it (ENG_TX_CYCLE), and no
 *     location ends up deeper than ENG_MAXLOCDEPTH (ENG_TX_LOCDEEP).
 * A location argument loc is a token: all digits is a LOC_ID, anything
 * else a code, upper-cased (a code starts with a letter, so the two
 * never meet).  The PARENT_ID value of ADD and SET, and an item's LOC_ID
 * value (ITEMS.C), is such a token, or "" or "0" for none; what is
 * stored is the LOC_ID.
 * Data written by another program may break the tree: a PARENT_ID that
 * names no live location, a loop, a chain longer than ENG_MAXLOCDEPTH.
 * Every walk of the tree is bounded by ENG_MAXLOCDEPTH and reports such
 * damage as ENG_EIO (ENG_TX_LOCTREE) instead of following it.
 * --------------------------------------------------------------------- */
#define ENG_LOCPATHLEN  78      /* a location's path text (eng_loc_path):
                                   one line of an 80-column screen in
                                   its frame                           */

/* Load the live location id into the LOCS buffer; *recno receives its
   record number, 0 (and no failure) when there is none. */
int eng_loc_seek(eng *e, u32 id, u32 *recno);

/* Load the live location loc into the LOCS buffer; ENG_ENOTFOUND
   (ENG_TX_NOSUCHLOC) when loc names none. */
int eng_loc_get(eng *e, const char *loc, u32 *recno);

/* The LOC_ID loc names into *id: 0 for NULL, "" or a token of zeros,
   else that of the live location eng_loc_get loads. */
int eng_loc_id(eng *e, const char *loc, u32 *id);

/* 1 when s is a LOC_ID token, one or more digits; *id receives their
   value, above ENG_MAXID (naming no location) when there are too
   many. */
int eng_loc_idtok(const char *s, u32 *id);

/* Copy the n pairs p into q (ENG_MAXPAIRS entries) for eng_edit_apply,
   with the value of every pair whose key is field (any case) taken as a
   location by eng_loc_id and replaced by the LOC_ID's digits in idtext
   (TXT_U32LEN + 1 bytes; the last such pair's, which is the one the edit
   keeps); *id receives that LOC_ID, and keeps its value when no pair
   names field.  ENG_ECONSTR naming field (ENG_TX_NOLOC) for a value
   that names no live location; ENG_EINTERNAL for n above ENG_MAXPAIRS.
   Moves the LOCS buffer. */
int eng_loc_pairs(eng *e, const eng_pair *p, u16 n, const char *field,
                  eng_pair *q, char *idtext, u32 *id);

/* LOC ADD LOC_NAME= LTYPE= [PARENT_ID=][LOC_CODE=][CAPACITY=][NOTES=]:
   *id receives the new LOC_ID. */
int eng_loc_add(eng *e, const eng_pair *p, u16 n, u32 *id);

/* The checks of eng_loc_add with the same results, without reserving an
   ID or writing anything: the edit is cancelled.  For the first pass of
   an import (CSV.C).  pdepth 0 checks as ADD does; pdepth 1 ..
   ENG_MAXLOCDEPTH stands for a parent at that depth that is not written
   yet (an earlier line of the import), left out of p: no live location
   can share the name under it, so only the depth is checked.  *depth
   receives the new location's depth when the checks pass.  Like a
   refused ADD it moves the LOCS and LTYPES buffers, and a NOTES pair
   overwrites eng_memobuf, so its value must not point into that
   buffer. */
int eng_loc_check(eng *e, const eng_pair *p, u16 n, u16 pdepth,
                  u16 *depth);

/* LOC SET loc k=v...: any field but LOC_ID; a PARENT_ID that differs
   moves the location with everything below it. */
int eng_loc_set(eng *e, const char *loc, const eng_pair *p, u16 n);

/* LOC DEL loc: ENG_ECONSTR while a live item is at the location
   (ITEMS2; ENG_TX_LOCFULL) or a live location is in it (LOCS2;
   ENG_TX_LOCKIDS), or when ENG_MAXREADS reads of deleted ones leave
   either unsettled (ENG_TX_LOCSMANY, ENG_TX_KIDSMANY). */
int eng_loc_del(eng *e, const char *loc);

/* LOC NOTES (memo into eng_memobuf) and LOC NOTES loc ADD= (append a
   line, or stage the memo, as eng_item_addnote). */
int eng_loc_notes(eng *e, const char *loc, u16 *len);
int eng_loc_addnote(eng *e, const char *loc, const char *text, u8 how);

/* The path of the live location id into path (ENG_LOCPATHLEN + 1 bytes;
   NULL for none): the names from the top down joined by " > ", the
   leading ones given up for "... > " when the whole is longer than
   ENG_LOCPATHLEN, so the nearest always show; *depth receives its
   depth.  ENG_ENOTFOUND (ENG_TX_NOSUCHLOC) when id is no live location,
   ENG_EIO for a damaged chain.  Leaves id in the LOCS buffer. */
int eng_loc_path(eng *e, u32 id, char *path, u16 *depth);

#ifdef ENGLOCS_WALKS
/* Walks of the tree in tree order: a location, then the locations in
   it, depth first, the locations of one parent by name (LOCS2), each
   step seeking on the key of the last entry, so no index position is
   kept between steps.  ENG_LW_TREE gives the locations below root (LOC
   LIST); ENG_LW_HERE the items at root (LOC CONTENTS), ENG_LW_ALL those
   at root and then at each location below it (LOC CONTENTS ALL=1), the
   items of one location by ITEM_ID (ITEMS2).  Root 0 stands for the
   whole tree, whose items of unknown location come first.
   Every entry examined counts one, its record read included, deleted
   ones too, and the walk stops before examining an entry once
   ENG_MAXREADS have been counted; restoring the position from a cursor
   (its location and the ancestors, at most ENG_MAXLOCDEPTH + 1 reads)
   counts nothing.  The items and children of a deleted location are
   passed over (LOC DEL refuses one that holds any).  A deleted location
   may lie one level below the deepest, its entry staying until PACK, and
   is examined as any other; a live one there is damage.  A
   cursor names the last entry examined: for ENG_LW_TREE a LOCS2 cursor,
   for the item walks an ITEMS2 cursor whose key is STR(location,8) and
   then the item's ID, 0 on arriving at a location; its record number is
   that of the last entry examined, of either table.  A cursor whose
   location is not root or below it (one of another walk, or of a
   location moved out since) is ENG_ECURSOR.  A location deleted between
   two requests ends its part of the walk, repeating or hiding no other;
   the cursor's own location moved since is passed over with what is in
   it.  The cursor holds no more than that location and its parent, so
   a move or rename of one of their ancestors between two requests
   resumes the walk at the ancestor's new place, which may skip or repeat
   other rows: a client that changes the tree lists it again from the
   start.  The walk keeps its place in its own keys and seeks LOCS2 and
   ITEMS2 afresh at every step, so between calls the caller may read
   and seek any table or index; the entry eng_lwalk_next loads stays in
   its buffer only until the caller moves that. */
#define ENG_LW_TREE     0
#define ENG_LW_HERE     1
#define ENG_LW_ALL      2

#define ENG_LOCS2LEN    (2 * ENG_IDLEN + ENG_LOCNAMELEN)  /* LOCS2 key  */
#define ENG_ITEMS2LEN   (2 * ENG_IDLEN)     /* ITEMS2 key               */

typedef struct {
    int  state;                 /* LOCS.C: not started, on, at end, or
                                   the ENG_E code of a failed walk      */
    u8   kind;                  /* ENG_LW_                              */
    eng_ifilt f;                /* item walks: the filters of
                                   eng_item_filters (ENGITEM.H)         */
    u8   down;                  /* the current location is live (or the
                                   root): its items and children next   */
    u16  reads;                 /* entries counted so far               */
    u16  base;                  /* depth of root, 0 for the whole tree  */
    u16  depth;                 /* levels of path in use below root     */
    u32  root;                  /* LOC_ID walked, 0 for the whole tree  */
    u32  recno;                 /* record of the last entry examined    */
    u8   path[ENG_MAXLOCDEPTH + 1][ENG_LOCS2LEN];
                                /* the LOCS2 keys from the level below
                                   root down to the current location,
                                   one level below the deepest for a
                                   deleted one                          */
    u8   ikey[ENG_ITEMS2LEN];   /* item walks: the position among the
                                   current location's items             */
} eng_lwalk;

/* LOC LIST [UNDER=under]: an ENG_LW_TREE walk below the live location
   under, or of the whole tree for NULL, "" or "0", continuing after the
   cursor text after (NULL or "" to start).  ENG_ENOTFOUND for an under
   that names no live location. */
int eng_loc_list(eng *e, eng_lwalk *w, const char *under,
                 const char *after);

/* LOC CONTENTS loc, and the item lists in tree order (ITEM LIST, ITEM
   FIND and REPORT ITEMS with ALL=1): an ENG_LW_HERE walk of the items at
   loc (a live location, or NULL, "" or "0" for unknown; ENG_ENOTFOUND
   for any other), with all 1 ENG_LW_ALL.  It keeps the items that pass
   the filters f of eng_item_filters (ENGITEM.H); an item passed over
   still counts towards ENG_MAXREADS, so a page may end with fewer rows,
   or none, and a cursor. */
int eng_loc_contents(eng *e, eng_lwalk *w, const char *loc, int all,
                     const eng_ifilt *f, const char *after);

/* Load the next entry of the walk, the location into the LOCS buffer
   (ENG_LW_TREE) or the item into the ITEMS buffer; *recno receives its
   record number, or 0 when the walk is over or ENG_MAXREADS is reached,
   and *depth the depth of the location, or of the item's location (0
   for unknown). */
int eng_lwalk_next(eng *e, eng_lwalk *w, u32 *recno, u16 *depth);

/* *more receives 1 when the walk has entries left after the last one
   examined, else 0; as eng_scan_more. */
int eng_lwalk_more(eng *e, eng_lwalk *w, int *more);

/* The walk's cursor; out holds ENG_CURSORMAX. */
void eng_lwalk_cursor(const eng_lwalk *w, char *out);
#endif

#endif
