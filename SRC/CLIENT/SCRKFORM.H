/*
 * SCRKFORM.H - the code lists' kinds, their messages and the forms of
 * their entries (SCRKFORM.C), which each client's code list screens and
 * pick lists show.
 *
 * The code lists are the categories (CAT), the location types (LTYPE)
 * and the tags of the items (ITEM TAGS, CODETAB.H's ct_load_tags), by
 * kind SCR_CK_; the tags have codes alone, and no screen or form of
 * their own, only a pick list.  An entry's form holds its code, given once
 * as the positional word of "<table> ADD" and never changed (FF_KEY,
 * FF_NEWONLY), its name and its sort order (SORTORD), which orders the
 * list; the field of each is at its SCR_CF_ index.  INV draws the form
 * in a box, where row and column place each field; the Windows client
 * maps the fields to a dialog's controls by index.
 */
#ifndef SCRKFORM_H
#define SCRKFORM_H

#include "COMPAT.H"
#include "FMODEL.H"

#define SCR_CK_CAT      0
#define SCR_CK_LTYPE    1
#define SCR_CK_TAG      2

/* The messages (SCRCODE.MSG), the second of each pair the location
   types' (the first plus the kind). */
#define ME_CATS         656     /* Categories (the lists' titles)       */
#define ME_LTYPES       657     /* Location types                       */
#define ME_NEWCAT       658     /* New category (the forms' titles)     */
#define ME_NEWLT        659     /* New location type                    */
#define ME_FCAT         660     /* Category ^1                          */
#define ME_FLT          661     /* Location type ^1                     */
#define ME_HINT         662     /* INV's hints of the list              */
#define ME_HINTEDIT     663     /* INV's hints of the form              */
#define ME_DELQ         664     /* Delete ^1?                           */
#define ME_ADDED        665     /* ^1 added                             */
#define ME_SAVED        666     /* ^1 saved                             */
#define ME_DELETED      667     /* ^1 deleted                           */
#define ME_L_CODE       668     /* the form's labels                    */
#define ME_L_NAME       669
#define ME_L_ORDER      670
#define ME_HDR          671     /* the lists' column headings           */

/* The tag pick list's titles, whole and cut short (CODETAB.H's part),
   the text for no tags, the refusal of a tag the field has no room for
   (CODETAB.H's ct_tags_add), and INV's progress while the tags load,
   ^1 the tags found so far. */
#define ME_TAGS         672     /* Tags in use                          */
#define ME_TAGSPART     673     /* Tags in use (incomplete)             */
#define ME_NOTAGS       674     /* No tags in use                       */
#define ME_TAGFULL      675     /* No room for another tag              */
#define ME_TAGWAIT      676     /* Reading tags... found: ^1            */

#define SCR_CF_CODE     0
#define SCR_CF_NAME     1
#define SCR_CF_ORDER    2
#define SCR_CF_COUNT    3

/* The entry form of each kind, titled ME_NEWCAT + kind, with the help
   topic CATS or LTYPES. */
extern const form_def *const scr_code_defs[];

#endif
