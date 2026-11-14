/*
 * FMODEL.H - the model of a data-entry form (FMODEL.C): the values over
 * a form table, their checks, the save and the field a failed save
 * names.
 *
 * A form is a table of typed fields (form_def, form_field).  The model
 * holds each field's value as text in the protocol's wire form, and the
 * value it was loaded with, so a save sends only the fields that
 * changed.  It reads a field's name, type, width, decimals, flags and
 * choices, never its label or position but to name a field's choices
 * (form_choice_name): a client draws the form as it likes and may
 * reach the fields by index (0 to nfields - 1, the order of the table),
 * as a dialog maps them to its controls.  The model shows nothing and
 * reads no key; FORM.H is the full-screen client's form over it.
 *
 * Values by type (every one may also be empty):
 *   FT_CHAR    text of up to width characters
 *   FT_NUM     digits, at most dec of them after a '.', an optional
 *              leading '-'
 *   FT_DATE    YYYYMMDD
 *   FT_LOGIC   T or F
 *   FT_CHOICE  one of the space-separated words of choices
 *   FT_CODE    a code the server checks (category, location), or
 *              tags, chosen from a pick list on F4; a category field's
 *              name starts with CAT
 *   FT_MEMO    the line count of the record's memo in decimal, for
 *              showing: never sent and never a change
 *   FT_SECRET  as FT_CHAR: a password, whose slots are overwritten
 *              before the form is freed
 *
 * A form is the client's own struct form, opaque outside the client's
 * form module, whose first member is a form_model; the functions here
 * take the form and reach that member only.  The client module reads
 * form_model's def, mode and partial (and clears partial once it has
 * reported it); the other members belong to FMODEL.C.  form_model and
 * the calls only a client's form module makes are in the last section,
 * which a file sees by defining FMODEL_CALLS.
 */
#ifndef FMODEL_H
#define FMODEL_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"

#define FORM_MAXFIELDS  24      /* fields of one form                   */
#define FORM_MAXVAL     LINK_MAXDIR /* characters of one value: the widest
                                   field of any form (the settings
                                   screen's data directory,
                                   LINK_MAXDIR); form_new refuses a
                                   wider field, and the screens check
                                   their widths against it at compile
                                   time                                 */
#define FORM_DATELEN    MSG_DATELEN /* YYYYMMDD: an FT_DATE value       */
#define FORM_MEMOW      5       /* digits of a memo line count: the
                                   fewest characters an FT_MEMO slot
                                   holds                                */

/* The memo of an FT_MEMO field as every client holds and edits it
   (MEMOIO.H). */
#define MEMO_COLS       60      /* characters of a line                 */
#define MEMO_MAXLINES   100     /* lines of a memo                      */
#define MEMO_TAB        8       /* the TAB stops a load expands to      */

/* Field types. */
#define FT_CHAR         1
#define FT_NUM          2
#define FT_DATE         3
#define FT_LOGIC        4
#define FT_CHOICE       5
#define FT_CODE         6
#define FT_MEMO         7
#define FT_SECRET       8

/* Field flags. */
#define FF_REQ          0x01    /* must not be empty                    */
#define FF_RO           0x02    /* shown, never edited (IDs, dates the
                                   server sets)                         */
#define FF_NEWONLY      0x04    /* editable only in a new record        */
#define FF_KEY          0x08    /* the record's key: sent as the
                                   positional word of ADD (LOC ADD
                                   code), never as a pair               */
#define FF_UPPER        0x10    /* folded to upper case as typed        */
#define FF_NONNEG       0x20    /* FT_NUM: not below zero               */
#define FF_LOCAL        0x40    /* not a protocol field; never sent     */
/* FT_NUM: a record's ID, which INVWIN shows without the Control Panel's
   separators (INTL.H).  The host build has it too, for the host tests
   that read the form tables; the DOS clients never read it, so it is 0
   there and their tables keep their bytes. */
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
#define FF_ID           0x80
#else
#define FF_ID           0
#endif

/* Modes. */
#define FORM_VIEW       0
#define FORM_EDIT       1
#define FORM_NEW        2

/* label, row, col, vcol and dispw are the drawing's: the model never
   reads them. */
typedef struct {
    const char *name;           /* protocol field name (or a local one) */
    u16  label;                 /* message number                       */
    u8   row;                   /* label position inside the box        */
    u8   col;
    u8   vcol;                  /* value column inside the box          */
    u8   type;                  /* FT_                                  */
    u8   width;                 /* characters of the value              */
    u8   dec;                   /* FT_NUM decimals; FT_CHOICE: 0, or
                                   the distance from label to the
                                   choices' names (form_choice_name)    */
    u8   flags;                 /* FF_                                  */
    u8   dispw;                 /* cells shown, 0 = width               */
    const char *choices;        /* FT_CHOICE wire values, else NULL     */
} form_field;

/* Of the table, the model reads only nfields and fields. */
typedef struct {
    u16  title;                 /* message number                       */
    const char *topic;          /* help topic of the form               */
    u8   top;                   /* the box                              */
    u8   left;
    u8   h;
    u8   w;
    u8   nfields;
    const form_field *fields;
} form_def;

typedef struct form form;       /* a form being shown (opaque)          */

/* Free the form (NULL is taken); the values of FT_SECRET fields, current
   and loaded, are overwritten first, through a volatile pointer so the
   stores are not left out. */
void form_close(form *f);

/* Close the open form that has FT_SECRET fields, if any, so that no
   password being typed outlives the program: its exit path calls this
   (SCR.H's scr_wipe), as the program can end while the form is open. */
void form_wipe(void);

/* The mode now (a FORM_NEW form becomes FORM_EDIT once its ADD has
   succeeded), and changing it, e.g. from view to edit. */
u8 form_mode_get(form *f);
void form_mode_set(form *f, u8 mode);

/* The command word after the table in the first request a FORM_NEW
   form's save sends, word a static string (LOAN RESERVE, LOAN USE);
   ADD until set. */
void form_set_add(form *f, const char *word);

/* Load a value in wire form, cut to fit, as the field's original
   (unchanged) value; 0, or -1 when the form has no field name.
   form_kline is an rq_fn that loads every K line of two values of a GET
   response this way (arg is the form); other lines, and names the form
   lacks, are skipped, and it returns 0, never stopping the response. */
int form_load(form *f, const char *name, const char *val);
int form_kline(void *arg, const rq_line *ln);

/* Set a value, cut to fit, as a change (a default, a picked code); 0 or
   -1.  Any field takes one, FF_RO and FF_LOCAL included: such a change
   counts in form_dirty, is never sent, and stays a change after a save
   until form_saved. */
int form_put(form *f, const char *name, const char *val);

/* The current value of field name in wire form, or NULL without it. */
const char *form_get(form *f, const char *name);

/* The current value of field name into d (size bytes, at least 1), cut
   to fit; "" when the form has no field name. */
void form_get_text(form *f, const char *name, char *d, u16 size);

/* 1 when any field but an FT_MEMO one differs from its original value. */
int form_dirty(form *f);

/* Make every current value the original (after a save). */
void form_saved(form *f);

/* Save through l, sending every changed field that is not FF_RO, FF_LOCAL,
   FF_KEY or FT_MEMO as a pair.  For FORM_NEW the first request is "<table>
   ADD" (or form_set_add's word): the FF_KEY field's value as its positional
   word (e.g. "LOC ADD code"), then every FF_REQ pair in the table's order,
   changed or not, so the server can create the record from it, then each other
   changed pair, in the table's order, that fits in what is left of the ADD,
   one that does not being passed over.  The pairs not yet sent then go into
   "<table> SET <id>" requests, which is all a save of a FORM_EDIT form sends.
   Each SET is filled in the table's order with every pair not yet sent that
   fits in what is left of it, one that does not being passed over for a later
   SET, and the next SET starts again from the first pair not yet sent; when
   that first pair does not fit a SET even alone, the SET is refused unsent
   (RQ_ETOOLONG) and the save ends.  A key or required pair that does not fit
   fails the ADD itself the same way: nothing is sent and the form stays
   FORM_NEW.

   id (idsz bytes) holds the record's ID or code.  Once an ADD answers
   +OK, id holds the new ID from "+OK ID=n" (or the FF_KEY value), the
   form is FORM_EDIT for that record, and the ADD's fields are marked
   saved, whatever the later requests do.  Each later SET that answers
   +OK marks its fields saved too.  Returns RQ_OK when every request
   succeeded, with res all zero when no field to send changed and
   nothing was sent; otherwise res holds the failed response and the
   fields not yet saved stay changed, so a save again sends only those,
   as a SET.  partial is set when the ADD succeeded and a later SET
   failed (a lost link may have taken its fields, or a pair was too long
   for any request), else cleared; an ADD without an FF_KEY field whose
   +OK has no ID= gives RQ_EINTERNAL. */
int form_save(form *f, link *l, const char *table, char *id, u16 idsz,
              rq_res *res);

/* The number (from 0) of word in the space-separated list choices, or
   0 when it is none of them (an FT_CHOICE field holds only words of its
   list); the match is exact, case included. */
u16 form_choice_num(const char *choices, const char *word);

/* Word i (from 0) of the space-separated list choices into out (size
   bytes, at least 1), cut to fit; "" when the list has fewer words. */
void form_choice_word(const char *choices, u16 i, char *out, u16 size);

/* The message naming choice i (from 0) of the FT_CHOICE field d, which a
   list of its choices shows after the word: label + dec + i, or 0 when
   d names none (dec 0). */
u16 form_choice_name(const form_field *d, u16 i);

/* ---------------------------------------------------------------------
 * The model's own calls.
 *
 * Only a file that defines FMODEL_CALLS before it includes this header
 * sees this section: FMODEL.C, the form modules of the clients (FORM.C,
 * and WFORM.C of the Windows programs) and T_FMODEL.  The screens see
 * forms through FORM.H, as the tests of the forms and of the screens
 * (T_FORM.C, T_SCRR.C) would have little Watcom C 6.5 compiler memory
 * left if they saw more.
 * --------------------------------------------------------------------- */
#ifdef FMODEL_CALLS
/* The messages (MSG.H, FORM.MSG) form_check gives, and the one a client
   shows when form_save has set partial. */
#define MF_REQUIRED     250     /* a required field is empty            */
#define MF_BADNUM       251     /* not a number of the field's form     */
#define MF_NEGNUM       252     /* a negative number where none is      */
#define MF_BADDATE      253     /* not a real date                      */
#define MF_BADCHOICE    254     /* not one of the choices               */
#define MF_PARTIAL      259     /* a new record, fields maybe not saved */

typedef struct {
    const form_def *def;
    char *val;                  /* current values                       */
    char *org;                  /* values as loaded or saved            */
    u16  off[FORM_MAXFIELDS];   /* each field's slot in val and org     */
    const char *add;            /* ADD, or form_set_add's word          */
    u16  size;                  /* bytes of val (and of org)            */
    u8   mode;                  /* FORM_                                */
    u8   partial;               /* the last form_save's ADD succeeded
                                   and a later SET failed               */
} form_model;

/* A form of size bytes (the client's struct form, at least
   sizeof(form_model)) for def in mode (FORM_), with every value empty
   and every byte after the form_model zero; one near-heap block.  NULL,
   keeping nothing, when memory has run out or def has more than
   FORM_MAXFIELDS fields or one wider than FORM_MAXVAL.  At most one
   form with FT_SECRET fields is open at a time: form_wipe closes the
   last one opened. */
form *form_new(const form_def *def, u8 mode, u16 size);

/* The index of the field named name, or -1. */
int form_find(form *f, const char *name);

/* The index of the first FF_KEY field, or -1. */
int form_key(form *f);

/* The bytes of a value slot of field d, its NUL included: FORM_DATELEN
   + 1 for FT_DATE, the larger of width and FORM_MEMOW, plus 1, for
   FT_MEMO, else width + 1. */
u16 form_valsz(const form_field *d);

/* The current value of field i (below nfields): its slot of
   form_valsz bytes, which the client may edit in place, so a password
   typed into it is never copied, keeping a NUL-terminated value. */
char *form_val(form *f, u16 i);

/* The index of the first field, in the table's order, whose value fails
   its check, with *mf its message (MF_); -1 when every one passes.
   FF_RO and FT_MEMO fields pass, and so does an empty value, but in an
   FF_REQ field (MF_REQUIRED).  FT_NUM: MF_BADNUM unless a number of
   the field's form of at most width characters whose sign, integer
   digits and, with dec, point and dec digits (dec counted in full,
   however many are typed) fit width, as a dBASE N field holds them;
   MF_NEGNUM below zero with FF_NONNEG, where '-' before zero alone is
   zero.  FT_DATE: MF_BADDATE unless a real date.  FT_LOGIC:
   MF_BADCHOICE unless T or F.  FT_CHOICE: MF_BADCHOICE unless a word
   of choices, case included.  Other types pass. */
int form_check(form *f, u16 *mf);

/* The index of the field a failed response names: for RQ_EARG (102),
   RQ_ENOTFOUND (200), RQ_EDUP (201) and RQ_ECONSTR (202) the field of
   its FIELD=, when the form has it; else -1. */
int form_err_field(form *f, const rq_res *res);

/* The number of words in the space-separated list choices. */
u16 form_choice_count(const char *choices);

/* The number (from 0) of word in the space-separated list choices, or
   -1 when it is none of them.  The match is exact, case included. */
int form_choice_find(const char *choices, const char *word);
#endif

#endif
