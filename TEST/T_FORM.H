/*
 * T_FORM.H - what the two source files of the T_FORM test share: the
 * item and location forms, the screen copied at a key, the helpers of
 * T_FORM.C that run a form, and the tests of T_FORM2.C that main runs.
 *
 * The test is split in two compilation units, T_FORM.C (the local
 * checks, editing, saving, server errors, the keys kept, form_loop,
 * memo_field, main) and T_FORM2.C (the memo editor, values as text,
 * choice words, form_field_error, form_store), because Watcom C 6.5
 * runs out of compiler memory on it as one file.
 */
#ifndef T_FORM_H
#define T_FORM_H

/* Message numbers of the form model, whose constants are its own
   (FMODEL.H's FMODEL_CALLS section): the texts the test expects on the
   screen.  Kept equal to the defines there, which msgchk.py checks
   against FORM.MSG. */
#define MF_REQUIRED     250
#define MF_BADNUM       251
#define MF_NEGNUM       252
#define MF_BADDATE      253
#define MF_BADCHOICE    254
#define MF_YN           255
#define MF_MEMOLINES    256
#define MF_MEMONEW      257
#define MF_SAVEMEMO     258
#define MF_MEMOKEY      260

/* The fields of the item form, by number; their values start at column
   VCOL of a box whose top left corner is at BOXTOP, BOXLEFT. */
#define F_ID            0
#define F_CAT           1
#define F_TYPE          2
#define F_MFR           3
#define F_MODEL         4
#define F_SERIAL        5
#define F_DESCR         6
#define F_COND          7
#define F_QTY           8
#define F_LOC           9
#define F_DATE          10
#define F_SRC           11
#define F_PRICE         12
#define F_TAGS          13
#define F_STATUS        14
#define F_NOTES         15
#define F_N             16
#define VCOL            12
#define BOXTOP          1
#define BOXLEFT         0

extern const form_field item_fields[F_N];      /* the item form       */
extern const form_def cat_def;  /* a category form, keyed by its code    */

extern char g_snap[VID_COLS + 1];       /* the row feed copies          */
extern char *g_snapall;         /* the whole screen, on the heap        */
extern u16 *g_typed;            /* keys built by a test, on the heap    */
extern form *g_run_form;        /* the form run runs                    */

/* 1 when every logged request fits LINK_MAXLINE. */
int all_fit(void);

/* Keys ended by KEY_NONE; the screen row r is copied into g_snap and
   the whole screen into g_snapall (rows ended by '\n') at key at. */
void feed(const u16 *k, u16 at, u8 r);

/* An item form in mode, with the test's pick and memo functions. */
form *open_item(u8 mode);

/* form_run on g_run_form with the keys k; 1 when it returned. */
int run(const u16 *k, u16 at, u8 r);

/* The screen row of item field i, and the column of every value. */
u8 vrow(u16 i);
u8 vcol(void);

/* The fields a save may need to split. */
void fill_long(form *f);

/* Load record id into a fresh view form with ITEM GET. */
form *get_item(const char *id);

/* The tests of T_FORM2.C: the memo editor (saving, the split of a long
   save, reading back, read-only and empty memos); form_kline,
   form_get_text and the choice words; form_field_error; form_store's
   three results; the fields Tab passes and the value selected as a
   field is entered. */
void test_part2(void);

#endif
