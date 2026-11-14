/*
 * CODETAB.H - a code list, the categories, the location types or the
 * tags of the items, held whole for the screens of each client
 * (CODETAB.C).
 *
 * CAT LIST and LTYPE LIST answer unpaged with at most LINK_MAXCODES rows,
 * each the code, its name and its sort order, in that order of columns
 * and of rows (by sort order, then code).  A ctab keeps the rows of one
 * such list in a far block, in the order they came, for a pick list, a
 * code list screen or a name looked up for every row of another list.
 *
 * ITEM TAGS answers in pages, each the distinct tags of the items it
 * read, A-Z folded (the case of ASCII letters ignored), a tag of two
 * pages on both.  ct_load_tags follows the pages to the end and merges
 * them into the rows, kept in the same order, a tag once in the
 * spelling it came in first, at most the LINK_MAXCODES first: a code
 * without a name or sort order.
 */
#ifndef CODETAB_H
#define CODETAB_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"

#define CT_ORDERW       2       /* digits of a sort order               */

/* One row: the name, sort order and code (a tag's being the widest) as
   the list gives them.  The far block holds a row of a code list as the
   first CT_ROWSZ bytes of a ct_row, as its code is at most
   LINK_LTYPELEN characters, and a tag as its code alone, CT_TAGSZ
   bytes. */
typedef struct {
    char name[LINK_CODENAMELEN + 1];
    char order[CT_ORDERW + 1];
    char code[LINK_ITAGLEN + 1];
} ct_row;

#define CT_ROWSZ        (LINK_CODENAMELEN + 1 + CT_ORDERW + 1 + \
                         LINK_LTYPELEN + 1)
#define CT_TAGSZ        (LINK_ITAGLEN + 1)

/* A code list.  n is the caller's to read. */
typedef struct {
    cmem_blk *b;                /* LINK_MAXCODES rows of CT_ROWSZ bytes */
    u16  n;                     /* rows held                            */
    u16  last;                  /* the row ct_name found last           */
    u8   rowsz;                 /* bytes of a row in b: CT_TAGSZ after
                                   ct_load_tags, else CT_ROWSZ          */
    u8   part;                  /* ct_load_tags: the rows are not every
                                   tag, as the load stopped or there
                                   were more than LINK_MAXCODES         */
} ctab;

/* ct_load_tags' wait function, called with its arg after each page
   that has another after it, the ctab holding the tags so far: nonzero
   stops the load there. */
typedef int (*ct_wait_fn)(void *arg);

/* Open t without rows: 1, or 0 when memory has run out (nothing held). */
int ct_open(ctab *t);

/* Free the far block of t; a ctab ct_open refused may be closed too. */
void ct_close(ctab *t);

/* Send "<table> LIST" (table "CAT" or "LTYPE") through l and keep its
   rows in t, in place of those it held.  RQ_OK, or the failed request's
   code with res holding its response and t without rows. */
int ct_load(ctab *t, link *l, const char *table, rq_res *res);

/* Send ITEM TAGS through l, then ITEM TAGS AFTER= the cursor of each
   page until one comes without, wait called between them, and keep the
   tags in t, in place of those it held, merged as this header says.
   RQ_OK, also when wait stopped the load (t->part set); else the failed
   request's code with res holding its response and t without rows. */
int ct_load_tags(ctab *t, link *l, ct_wait_fn wait, void *arg,
                 rq_res *res);

/* Append tag to t, the value of a tags field width characters wide
   (width + 1 bytes), after a comma, the blanks and commas t ends in
   dropped, unless t holds it already: a whole tag between blanks or
   commas, A-Z folded, t then unchanged.  1 when t holds tag after; 0,
   t unchanged, when it does not fit.  The item form's tags and the tag
   filters take the tag chosen from their pick list so. */
int ct_tags_add(char *t, u16 width, const char *tag);

/* The blanks of the tags t made commas, so that a TAG= filter of them,
   the search's or a report's, is sent bare, and the commas it then ends
   in dropped, as a report's subtitle shows it. */
void ct_tags_bare(char *t);

/* Row i (below t->n) into *r. */
void ct_get(ctab *t, u16 i, ct_row *r);

/* The name of code into name (LINK_CODENAMELEN + 1 bytes), or the code
   itself when t has no such row.  The row found last is tried first, as
   the rows of a list often repeat a code. */
void ct_name(ctab *t, const char *code, char *name);

#endif
