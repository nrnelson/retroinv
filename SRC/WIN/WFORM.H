/*
 * WFORM.H - the form dialog of the Windows programs: a form table of
 * FMODEL.H shown and edited in a modal dialog over the form model, with
 * the model's checks and its save through the link (WFORM.C).
 *
 * A form is the model's (FMODEL.H): wf_open makes one for a table and a
 * mode, the owner loads it (form_load, form_kline) and wf_run shows it.
 * The values stay in the form after the dialog, also when it was
 * cancelled, until form_close frees it.
 *
 * The dialog.  wf_run builds a dialog template in memory from the table
 * and runs it with DialogBoxIndirectParam over the owner's window, so
 * that every table, whatever its fields, needs no template of its own
 * and Windows' dialog manager gives the keys, the focus and the layout
 * in dialog units; its edit controls keep their text in memory of the
 * dialog's own, outside DGROUP.  It has the caption, the system menu
 * and the font of INVWIN.RC's dialogs (8 point Helv).
 *
 *   - The caption: in FORM_EDIT and FORM_VIEW the message ops->title
 *     with the record's name as ^1 (wf_name: its ID, unless WF_O_NOKEY,
 *     then the values of ops->names; the key alone on a form with an
 *     identity row, which shows the names), else the table's title
 *     message (when ops->title is 0, always the table's); in FORM_EDIT
 *     on a form that OK saves (ops->table) that caption in the string
 *     WF_S_EDITCAP ("Item 1 - Edit").
 *   - Pages.  A form's page table (ops->pages, wf_pages) gives each
 *     field a page, or WF_PG_TOP for the identity row above the pages;
 *     a form without one has a page alone, or, with an FT_MEMO field,
 *     two, the memo alone on the second, their tabs the string
 *     WF_S_PAGES.  A form of more than one page has a tab strip of
 *     WTAB.H, control WF_STRIPID, its tabs the page table's string, made
 *     as the dialog opens, in the dialog's font, under the identity row
 *     and before the pages' controls in the Z-order, so in the tab
 *     order.  Every control is in the dialog; those of the pages not
 *     shown lack WS_VISIBLE.  The strip's keys (WTAB.H) reach it through
 *     the message filter; a tab chosen shows its page, and the focus,
 *     when the control it was on is now hidden, goes to the first
 *     control after the strip that Tab stops on.  The focus put on a
 *     field (a failed check, a failed request, a field's access letter)
 *     shows the field's page first.  The dialog opens on its first page,
 *     and keeps the page shown when Edit builds it again.
 *   - Each field i is a label, a Static control of ID 65535 whose text
 *     is the field's label: the string WF_S_LABEL plus the label's
 *     message number where the program has one (the Windows client's
 *     own, WLABEL.RC), else the message itself, and a control of ID
 *     WF_ID0 + i, the field's; an FT_CODE field with a pick list (below)
 *     has its pick button too, a push button of ID WF_PICK0 + i whose
 *     text is the string WF_S_PICK, with no tab stop and no access
 *     letter, as F4 and ALT+Down are its keys.  The memo has no label:
 *     its tab names it.  The controls are written the identity row's
 *     first, then each page's, each in the table's order, a label before
 *     its control.
 *   - The layout, in dialog units, from WD_MARGIN of WDLG.H: the identity row
 *     on top, the strip under it, then the pages, as high as the tallest.  A
 *     page's lines are its fields' rows of the table, in order, one WF_ROWH
 *     each; the fields of a row stand in the order of their columns.  Each
 *     label is as wide as its text in the dialog's font, measured as the
 *     dialog is made.  A row's first field has its label at the margin and its
 *     control at the value column, one for every page: the widest label room
 *     of the table (vcol less col cells of WF_CELLW) or measured label of a
 *     first field; on the identity row its control follows its label.  Every
 *     later field's label follows the control before it by a gap and its
 *     control the label, a field without a label (its col its vcol) standing a
 *     cell after the control before it; a check box stands where its control
 *     would and its label right of it.  A control is as many cells wide as the
 *     field shows (dispw, else width, a number's with its separators,
 *     INTL_NUMW of INTL.H; FORM_DATEW for FT_DATE; a field with rows, below,
 *     its longest row when longer), yet, in every mode, never narrower than
 *     the widest text a value of a fixed format can show, measured as the
 *     labels are: a date or number its characters, each as wide as the widest
 *     digit or separator of the Control Panel's formats, and a choice's
 *     longest row while its rows keep their names; plus its border and a
 *     drop-down list's button, or the room of an FT_CODE field's pick button
 *     at its right.  Its least width is that, but an edit control's of more
 *     than a few cells, which is those few cells, or a fixed format's widest
 *     text when wider.  A row that would end past WF_MAXW, less the margin,
 *     with its controls at their least, shows its drop-down lists' rows
 *     without their names, the right-most list first, until it does not; a row
 *     that still would carries its fields from the first that would end past
 *     the margin on to a line of its own under it, from the value column, the
 *     lines after it moved down, and so on until every line fits.  Then a
 *     line's edit controls share what passes in proportion to their widths,
 *     then from the right, none below its least, an edit control scrolling
 *     what it cannot show, a drop-down list or a code field showing a cell
 *     beside its button.  The path field of a
 *     location field (below) and a field that only shows a value beside
 *     another, local, read-only and without a label (the name field, below, an
 *     item's held units, a loan's item and host), take no Tab stop.  The memo
 *     fills its page, lines of MEMO_COLS characters of its font across, the
 *     dialog widened as it opens when it would not hold them.
 *   - Under the pages, the record's buttons (wf_pages' acts), push
 *     buttons of ID WF_ACT0 + i, from the margin; under them the message
 *     line, a Static control of ID WF_MSGID two lines high, empty until
 *     a check or a request fails or a record button is refused, and
 *     right of it the buttons, each WD_BTNW by WD_BTNH: OK (IDOK, the
 *     default) and Cancel (IDCANCEL) in FORM_EDIT and FORM_NEW; Close
 *     (IDCANCEL, the default) in FORM_VIEW, then Edit, which CTRL+E
 *     gives too (below), disabled below TUI_LVL_OPER (TUI.H), on a form
 *     that OK saves (ops->table); then Help (WF_HELPID), which shows the
 *     help of the form's topic (form_def's) over the dialog, on a form
 *     that names one once the program has set its help (wf_help).
 *     Their texts are the strings WF_S_OK, WF_S_CANCEL, WF_S_CLOSE,
 *     WF_S_EDIT and WF_S_HELP (WFSTR.H) of the program's string table,
 *     which SRC/WIN/WFORM.RC holds, with WF_S_PICK, WF_S_PAGES and the
 *     pick list's template, for the program's resource script to
 *     include.
 *   - Access letters.  The shared and Windows label texts carry no '&' of
 *     their own, so the template gives them their letters, underlined, in the
 *     order the controls are written: each label of a field that can be
 *     changed in the mode, so none in FORM_VIEW, takes the first digit or
 *     ASCII letter starting one of its words that no label before it took
 *     (either case), else the first such anywhere in it, else none, but never
 *     a letter of the strip's tabs, of the record's buttons or of the Help
 *     button; the Edit button has none, CTRL+E being its key.  The other
 *     labels show no letter and take none, so no two controls of a dialog
 *     share a letter.  OK, Cancel and Close have no letter, as Enter and Esc
 *     are their keys.  A label's own
 *     '&' is shown as it is.  Alt and the letter, or the letter alone while
 *     the focus is on a control that takes no characters, moves the focus to
 *     the control after the label, the field's; for a field of a page not
 *     shown the message filter shows its page and moves the focus there, as
 *     Windows 3.0's dialog manager, which finds a hidden label's letter too,
 *     would move it to the first control shown after the label, or click it.
 *
 * The display floor is EGA, 640 x 350 pixels.  A dialog fits it when its
 * window, caption and frame included, is at most 640 x 350 at the dialog
 * base units of the EGA driver's system font (8 x 12 pixels, no smaller
 * than those of the dialog's own font), which is so for a table on INV's
 * grid of at most 14 rows, the item form's (SCRIFORM.H) on its pages: a
 * dialog's width is at most WF_MAXW and a row takes WF_ROWH.  Hercules,
 * 720 x 348, is wider but two lines shorter: whether a dialog of nearly
 * the floor's height fits it depends on its driver and system font.
 * CGA, 640 x 200, is below the floor: a dialog may pass the screen's
 * bottom and right.
 *
 * The controls, by type (the value in the model's wire form, FMODEL.H):
 *
 *   FT_CHAR, FT_NUM, FT_CODE
 *              an edit control holding the value, at most width
 *              characters; FF_UPPER makes it ES_UPPERCASE, so Windows
 *              folds what is typed.  An FT_CODE field of the
 *              categories or the location types whose code list the
 *              dialog fetched has rows instead (below); any other, and
 *              one whose list failed, may have a pick list (below).  An
 *              FT_NUM field that cannot be changed, but a record's ID
 *              (FF_ID), shows its number with the Control Panel's
 *              separators (wrow_intl of WROWS.H)
 *   FT_DATE    an edit control of at most FORM_DATEW characters; a
 *              value of FORM_DATELEN digits shows as YYYY-MM-DD, or in
 *              the Control Panel's short date when the field cannot be
 *              changed, any other as it is.  What is typed loses every
 *              '-' as it is read, and a text then longer than
 *              FORM_DATELEN is cut to FORM_DATELEN - 1 characters, never
 *              a date
 *   FT_LOGIC   a check box, checked for T.  It is read as T when
 *              checked, else as F, but an empty value stays empty while
 *              the box is not checked
 *   FT_CHOICE  a field with rows (below), its choices' words
 *   FT_SECRET  a password control of WEDIT.H (ES_PASSWORD), at most
 *              width characters, and no more than LINK_PWMAX
 *   FT_MEMO    an OEM text control of WEDIT.H in OEM_FIXED_FONT, multi-
 *              line, its words wrapped at its width, with a vertical
 *              scroll bar, holding at most LINK_MAXMEMO bytes, its lines
 *              broken by CR LF as stored; its text is the record's memo
 *              (MEMOIO.H, through the memo functions of wf_ops), never
 *              the model's value, which for FT_MEMO is only a line
 *              count.  Ctrl-Enter starts a new line, as Enter is OK.  A
 *              form has one FT_MEMO field at most, the record's NOTES
 *
 * Rows.  A field with rows, an FT_CHOICE field or an FT_CODE field whose code
 * list the dialog fetched, is a drop-down list (CBS_DROPDOWNLIST) of them, in
 * their order, after an empty row unless the field is FF_REQ: each the code or
 * word in the field's cells (LINK_LTYPELEN at most), two blanks and its name,
 * a code's from its list, a choice's from form_choice_name of FMODEL.H, or the
 * word alone when it has no name.  So a letter typed selects the next row
 * whose code starts with it, and F4 or ALT+Down drops the list, as in any
 * drop-down list of Windows.  Esc and Enter in the open list close it (the
 * message filter takes them, as it takes CTRL+E in FORM_VIEW, where Windows
 * 3.0 would also give them to the dialog as Cancel and OK); Esc restores the
 * row it had when it dropped.  The value's row, or the empty row for an empty
 * value, is selected; a value that is none of them (as loaded) selects no row
 * and stays as it is until a row is chosen.  A row chosen is read back as its
 * code, the row's text up to its first blank, or as its choice's word.  A
 * field with rows that cannot be changed shows its value's row, or the value
 * when no row holds it, as other fields that cannot be changed show theirs
 * (below).  The code lists (LIST of wp_tables, WPICK.H) are fetched before the
 * dialog is built, one request a kind of list, each between busy notes over
 * the owner's window; their rows then live in the controls.  A failed one
 * whose action leaves (rqe_class) is shown in a message box over the owner,
 * captioned as it is, and wf_run ends with WF_LOGIN, WF_QUIT or WF_LOST
 * without a dialog; any other leaves its fields edit controls with their pick
 * lists.
 *
 * Text crosses at each control but the memo, whose CP437 crosses as it
 * is: a value or label, CP437 as the data and the messages are, goes in
 * through OemToAnsi, and a text typed comes back through AnsiToOem.  As
 * the two lose much of CP437 between them, a field is read back only
 * when its control has changed: an edit control's text by EM_GETMODIFY,
 * a check box or drop-down list whenever it is read, its value being a
 * code or one of a few ASCII words.  Every text read but a password's and a
 * memo's loses its trailing blanks.
 *
 * Modes and read-only fields.  A field can be changed unless the form is in
 * FORM_VIEW, the field is FF_RO, or FF_NEWONLY in a form not in FORM_NEW; an
 * FT_MEMO field also needs ops->memo_save, and a memo that failed to load
 * stays read-only, as does one that a save would cut short, more than
 * MEMO_MAXLINES lines, past LINK_MAXMEMO bytes once loaded or truncated by the
 * server (mio_check), whose message (MM_TOOMANY, MM_TOOLONG or MM_TRUNC of
 * MEMOIO.H) the message line gives as the dialog opens.  In FORM_VIEW a
 * field's value but a check box's, a password's and the memo's is text, a
 * Static control of ID WF_ID0 + i (SS_LEFTNOWORDWRAP, SS_NOPREFIX) where its
 * control would be, on its label's line, with no Tab stop, when that holds it
 * whole: a value of a fixed format (a date, number or choice), as its control
 * is as wide as its widest text, or free text whose control holds as many
 * cells as the field's width, or as its longest row, and the value itself,
 * measured as the labels are; free text that may be longer than its
 * control shows, or whose value is wider than it, stays a read-only edit
 * control, which scrolls.  Any other field that cannot be changed is a
 * read-only edit control of WEDIT.H (it takes the focus, scrolls and
 * selects, but never changes), or, for a check box and a password control,
 * disabled (WS_DISABLED); a control that WEDIT cannot subclass is disabled
 * too.  The focus starts on the first
 * field of the page shown that can be changed, else on the first anywhere, in
 * FORM_VIEW on the tab strip on a form of pages, else on Close.
 *
 * OK.  Enter, Close and Esc in FORM_VIEW end the dialog with WF_CANCEL.
 * Otherwise OK reads the fields that can be changed into the model,
 * then:
 *
 *   0. a field of the owner's pick list (ops->pick) holding a value is
 *      looked up there (find 1), as INV's prompt for an item looks up
 *      what is typed: the owner takes it as it is or shows its list,
 *      and nothing chosen stops OK; a location typed that its path does
 *      not show yet is looked up (the location field, below), and one
 *      that names none stops OK.
 *   1. form_check, then ops->check: the first bad field gets the
 *      focus, and the message line its message, MF_ (FMODEL.H) for
 *      form_check's, none for ops->check's 0; the dialog stays.
 *   2. A memo whose text was changed is wrapped at MEMO_COLS, where
 *      the control, MEMO_COLS characters wide, wraps it on the screen:
 *      a word that would take its line past MEMO_COLS characters starts
 *      the next line, the blanks before it and those ending a line
 *      dropped.  It is then taken into the memo (mio_set_text) and must
 *      hold what INV's memo editor can: at most MEMO_MAXLINES lines
 *      (WF_S_LINES) of at most MEMO_COLS characters, none a TAB or a
 *      control byte (WF_S_LINE; a word longer than a line fails so), and
 *      at most LINK_MAXMEMO bytes, a line break counting two (WF_S_BYTES;
 *      the control holds as many, but the breaks of the wrap add one
 *      each), so that mio_check finds nothing to say; else the memo gets
 *      the focus and the string, given its limit.
 *   3. With ops->table, form_save through l, or ops->save in its place,
 *      a new record's location field (below) first made a change, so
 *      that the ADD carries a location its screen preset as loaded;
 *      without it, ops->send's request; either between busy notes.
 *      Then every FT_SECRET field is wiped, its control, its value and
 *      the original a save leaves, whatever came of the request, so that
 *      no password outlives the requests that sent it, and after a
 *      failure it is typed again.  After an ADD that succeeded, the form
 *      being FORM_EDIT now, whatever the rest of the save did: the new ID
 *      goes into ops->idfield's field (form_load; NULL when the key field
 *      already holds it), the caption becomes ops->title's, and
 *      FF_NEWONLY fields can no longer be changed.  A failure is shown
 *      as below, after a message box of the save's note, if it gave one.
 *   4. ops->memo_save for a memo whose text was changed, between busy
 *      notes; once it succeeds the text counts as unchanged.  A save
 *      that failed first says what became of the memo in a message box,
 *      the message of mio_failmsg (MEMOIO.H) if it gives one: MM_PARTIAL
 *      with the lines sent and the lines in all, MM_MAYBE, or
 *      MM_OLDSRV, which takes the failure's place; then the failure is
 *      shown as below, and OK sends the whole memo again.
 *   5. The dialog ends with WF_OK.
 *
 * A failed request (res) is shown, as INV's form does it, as follows.
 * When form_save set the model's partial, a message box first gives
 * MF_PARTIAL (the record was added; some fields may not have been
 * saved) and partial is cleared.  Then the text of rqe_class (RQERR.H)
 * for the user's level, a 200 naming one of the form's fields (a record
 * the field refers to) read as a 102, the server's text: a line
 * (RQE_LINE) goes to the message line, and the focus to the field the
 * response names (form_err_field), or, for a 201 naming none of the
 * form's fields, to its FF_KEY field; the dialog stays, so OK sends
 * again only what is not yet saved.  A box (RQE_BOX), or the line of
 * any other 200, whose record no longer exists, is a message box over
 * the dialog, captioned as the dialog is (wd_say of WDLG.H), with the
 * stop icon for a box and the exclamation icon for a line; then the
 * dialog stays, or ends with WF_GONE after that 200, or with WF_LOGIN,
 * WF_QUIT or WF_LOST when rqe_class's action is RQE_LOGIN, RQE_QUIT or
 * RQE_LOST.  A memo that fails to load as the dialog opens is shown the
 * same way.
 *
 * Cancel, Esc and the system menu's Close.  In FORM_EDIT and FORM_NEW
 * the fields are first read into the model; when form_dirty says so of
 * a form that OK saves (ops->table or ops->memo_save), or a memo's text
 * has changed, a message box captioned as the dialog asks MT_DISCARD
 * (Yes, No), and No keeps the dialog.  The dialog then ends with
 * WF_CANCEL.  So OK on a form nobody has touched sends nothing, Cancel
 * on it asks nothing, and nor does Cancel on a form that saves nothing,
 * such as a search's or a command's (send).
 *
 * CTRL+E in FORM_VIEW, on a form that OK saves (ops->table), and its Edit
 * button switch the dialog to FORM_EDIT, as INV's Edit does a form it shows:
 * the dialog is built again for FORM_EDIT, in its place, with the values it
 * showed and its memo as loaded, which is not loaded again, on the page it
 * showed and the focus on the first field of that page that can be changed;
 * from then on it is a dialog of FORM_EDIT.  Below TUI_LVL_OPER (TUI.H) Edit
 * shows INV's refusal instead (rqe_deny of RQERR.H), in a message box over the
 * dialog, captioned as the dialog is, with the exclamation icon, and the
 * dialog stays.  As the dialog manager gives a key to the control that has the
 * focus, CTRL+E reaches the dialog through a message filter (WH_MSGFILTER)
 * that wf_run sets while its dialog runs.
 *
 * Pick lists.  An FT_CODE field without rows has one when it can be changed,
 * the dialog has a link (wf_run's l) and the field names a list of WPICK.H, or
 * else the owner has one (ops->pick): the categories for a field whose name
 * starts with CAT (CATEGORY, CAT), the location types for LTYPE, the tags in
 * use for a field whose name starts with TAG (TAGS, TAG), the location tree
 * for the location field (below).  F4 or ALT+Down in the field, or a click on
 * its pick button, shows that pick list over the dialog, as INV's F4 does in a
 * code field, captioned ME_CATS, ME_LTYPES or ME_TAGS (WPICK.H) or MO_PICK
 * (SCRLFORM.H), with the dialog's link, level and busy bracket (ops->busy).  A
 * choice replaces the field's value as a change, but a tag, which is added to
 * the tags the field holds (ct_tags_add of CODETAB.H), clearing the message
 * line, or else, when it does not fit, leaves them as they are and puts
 * ME_TAGFULL there; nothing chosen leaves the value as it was.  The focus then
 * goes back to the control, but when a failure ends the dialog: a failed CAT
 * LIST, LTYPE LIST or ITEM TAGS is shown as a failed request is (above), and
 * after a failure of the location list, which that list shows, whose action
 * leaves, the dialog ends with WF_LOGIN, WF_QUIT or WF_LOST.  A pick list that
 * cannot be made says MT_NOMEM on the message line.  F4 and ALT+Down reach the
 * dialog through the message filter, as CTRL+E does.
 *
 * The location field.  A form whose ops->loc names a location field of
 * SCRLFORM.H (scr_locf: the field, the read-only field of its path, the
 * text of no location and whether the form is a filter), shown with a
 * link, keeps the path field showing where the location field's value
 * is, as INV's form does: none's text for "0", and for a blank value
 * but in a filter, where blank is any location and shows nothing; else
 * the path that LOC GET gives (scr_loc_info), cut to the path field's
 * cells from the left (msg_path of MSG.H).  It is shown as the dialog
 * opens, for the value loaded, after a choice from the pick list, and
 * when the field loses the focus, and as OK's first step, with a value
 * typed that the path does not show yet; each LOC GET between busy
 * notes.  A location loaded or chosen shows its code, where it has one,
 * in place of its LOC_ID, as loaded or as a change; one typed stays as
 * typed.  The pick list's lead row, none's text, chooses "0" in a
 * filter and blank in a record's form.  A value that names no location
 * (200) says MO_NOSUCH on the message line and shows no path, and OK
 * then stops there, the focus on the field; so in a location's own form
 * (WF_O_SELF) does one chosen or typed at the deepest level,
 * LINK_MAXLOCDEPTH, saying MO_TOODEEP, and one typed that is the
 * record or inside it, once saved (wp_within of WPICK.H), saying
 * MO_INSELF.  A path found clears the line.
 * Any other failure of LOC GET is shown as a failed request is
 * (above).
 *
 * The name field.  With ops->codes, a local read-only field (FF_LOCAL,
 * FF_RO) without a label that stands after an FT_CODE field on its row
 * shows the name that codes gives that code (ct_name of CODETAB.H), as
 * INV's location form shows its type's: as the dialog opens, after a
 * choice from the pick list, when the code field loses the focus and
 * when a row of its drop-down list is chosen.  When the code field has
 * rows, which name the code, the name field and its label are hidden,
 * the name kept all the same.
 *
 * PgUp and PgDn, as in INV's forms, move the focus to the first and the last
 * field shown whose control Tab stops on, its text selected, but in the memo,
 * whose text they scroll, and in an open drop-down list, which they page; they
 * too reach the dialog through the message filter.
 *
 * The record's buttons (wf_pages' acts), as INV's item form has its Parts,
 * Service and Loans tabs.  In FORM_VIEW, and in FORM_EDIT while no field and
 * not the memo has changed, the button ends the dialog with WF_ACT + i, for
 * the owner to open what it names; in FORM_NEW, or once something has changed,
 * the message line says WF_S_SAVEFIRST instead, and the dialog stays.
 *
 * Passwords.  An FT_SECRET value goes from the model's own slot to its
 * control through a copy on the stack, wiped at once, and from the
 * control straight into the slot; control and slot are wiped once a save
 * has sent it (OK's step 3), the control (we_wipe) as the dialog is
 * destroyed, however it ends, and both by wf_wipe, which the owner calls
 * when Windows ends while the dialog is open (Windows then destroys no
 * window), and then form_wipe: form_wipe frees the form, which wf_wipe
 * still reads.
 *
 * Real mode.  The template is a moveable global block, locked only
 * while bytes are written into it and freed when the dialog has ended,
 * as is the template of the dialog the labels are measured in, freed
 * once it is made;
 * the dialog's state is on wf_run's stack, reached by a near pointer;
 * the code lists are far blocks of CODETAB.H, held from their fetch
 * until the dialog has filled its controls; the memo's lines are a mio
 * of MEMOIO.H, in a far block reached only through its calls, and its
 * text passes to and from the control
 * through a near block of WF_MEMOSZ bytes; a dialog holds both only
 * while it shows a memo.
 */
#ifndef WFORM_H
#define WFORM_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "WFSTR.H"

/* Control IDs and the layout, in dialog units. */
#define WF_ID0          100     /* field i's control is WF_ID0 + i      */
#define WF_PICK0        (WF_ID0 + FORM_MAXFIELDS)       /* and its pick
                                   button WF_PICK0 + i                  */
#define WF_ACT0         (WF_PICK0 + FORM_MAXFIELDS)     /* record
                                   button i is WF_ACT0 + i              */
#define WF_MSGID        99      /* the message line                     */
#define WF_STRIPID      95      /* the tab strip                        */
#define WF_HELPID       94      /* the Help button                      */
#define WF_CELLW        4       /* a text cell across                   */
#define WF_ROWH         12      /* a row down                           */
#define WF_MAXW         308     /* the widest dialog, frame left out    */
#define WF_PG_TOP       0xFF    /* wf_pages' page of the identity row   */

/* The memo's text as its control holds it: at most LINK_MAXMEMO bytes,
   CP437, lines broken by CR LF, and its NUL; a memo shown read-only past
   that is cut there (mio_text). */
#define WF_MEMOSZ       (LINK_MAXMEMO + 1)

/* A form's pages (above): tabs is the string of the strip's tabs, each
   letter marked by an '&', each text ended by WT_SEP of WTAB.H but the
   last; page[i] is field i's page, 0 the first tab's, or WF_PG_TOP for
   the identity row; acts is the string of the record's first button,
   button i's acts + i, nacts the buttons, 0 for none. */
typedef struct {
    UINT tabs;
    UINT acts;
    u8   nacts;
    const u8 *page;
} wf_pages;

/* wf_ops' flags. */
#define WF_O_NOKEY      0x01    /* the ID left out of a record's name   */
#define WF_O_SELF       0x02    /* the record is a location             */

/* wf_run's results. */
#define WF_EFAIL        (-1)    /* the dialog could not be made: no
                                   memory, Windows refused it, or another
                                   form dialog is open                  */
#define WF_CANCEL       0       /* left without saving                  */
#define WF_OK           1       /* checked and saved                    */
#define WF_GONE         2       /* the record no longer exists (200)    */
#define WF_LOGIN        3       /* a failure's action, as rqe_class's:  */
#define WF_QUIT         4       /* RQE_LOGIN, RQE_QUIT and RQE_LOST     */
#define WF_LOST         5
#define WF_EDIT         6       /* not a result: the end of a dialog
                                   that Edit switches to FORM_EDIT      */
#define WF_ACT          7       /* record button 0 (wf_pages); button i
                                   ends the dialog with WF_ACT + i      */

/* A record's name (wf_name): its key, an item's ID at most, then a maker
   and a model, each after a blank, at most; a longer one is cut. */
#define WF_NAMESZ       (LINK_IDLEN + 1 + 2 * (LINK_MFRLEN + 1))

/* The owner's part in a form dialog, constant for a screen.

   busy, never NULL, is the owner's bracket of every request (on 1,
   then 0), dlg the dialog, or the owner's window while the code lists
   are fetched before it opens: while it is on, dlg must take no input
   (EnableWindow) and a program under the test driver holds it (td_busy
   of TDRV.H); turned off, it gives dlg its input and its focus back.

   memo_load and memo_save are mio_load and mio_save of MEMOIO.H, or
   functions that do as they do without the link (a test's), given the
   dialog's link, table and the record's ID.  memo_load fills the memo,
   open and empty, as a saved record's dialog opens; a new record's memo
   starts empty.  memo_save sends the memo after the save of the form's
   fields (its record has an ID, also when it was new).  memo_load NULL
   leaves the memo empty and read-only, and then memo_save is NULL too;
   memo_save NULL alone shows the memo read-only.

   check, after form_check has passed, is the owner's check of the form
   as a whole: the index of the field to give the focus, with *mf the
   message (MSG.H) for the message line, 0 to leave it empty (after a
   question of the check's that the user declined), or -1 when the form
   passes; NULL, or left out of the initializer, checks nothing more.

   send, when table is NULL, is OK's request in place of a save: sent
   through l for the record id with the values of f, it returns RQ_OK
   or the failure's code, the failure in res.  NULL, or left out of the
   initializer, sends nothing.

   save saves f in form_save's place, with its arguments and its
   contract (OK's step 3), and may set *note, 0 before, to a message
   that a message box gives before its failure is shown (that a part of
   it was saved, say); NULL, or left out, is form_save itself.

   pages are the form's pages (above); NULL, or left out, for a page
   alone, or two with a memo.

   names are the fields whose values name the record after its ID in
   its caption (wf_name), ended by NULL; NULL, or left out, for the ID
   alone.

   flags, WF_O_, 0 or left out for none: WF_O_NOKEY leaves the ID out
   of the record's name, for records whose ID is a number the user never
   needs (a location's); WF_O_SELF says the record is a location, which
   its location field's pick list refuses as a choice, with every
   location inside it, once saved (wp_pick's outside, WPICK.H).

   loc is the form's location field (above), or NULL, or left out, for
   none.

   codes holds the names of the codes of the form's name field (above),
   or NULL, or left out, for none.

   pick is the owner's pick list of the FT_CODE fields that no list of
   WPICK.H serves (above): shown over dlg with the dialog's link and
   level on typed, the field's value, it returns WP_OK with the choice
   in code (WP_CODESZ bytes), else WP_NONE or WP_ENOMEM, and *act, as
   wp_list's.  With find 1, OK's look-up (step 0), a value the owner
   takes as it is gives WP_OK at once, the value it stands for in code,
   with no list shown.  NULL, or left out, for none. */
typedef struct {
    const char *table;          /* form_save's table ("ITEM"), or NULL:
                                   OK checks and sends send's request,
                                   saving nothing                       */
    const char *idfield;        /* the field shown the new ID of an ADD,
                                   or NULL                              */
    u16  title;                 /* message of a record's caption, ^1 its
                                   name; 0 for the table's title        */
    void (*busy)(void *arg, HWND dlg, int on);
    int  (*memo_load)(mio *m, link *l, const char *table, const char *id,
                      rq_res *res);
    int  (*memo_save)(mio *m, link *l, const char *table, const char *id,
                      u16 *done, u16 *total, rq_res *res);
    int  (*check)(void *arg, form *f, u16 *mf);
    int  (*send)(void *arg, form *f, link *l, const char *id, rq_res *res);
    int  (*save)(form *f, link *l, const char *table, char *id, u16 idsz,
                 rq_res *res, u16 *note);
    const wf_pages *pages;
    u8   flags;                 /* WF_O_                                */
    const char *const *names;
    const scr_locf *loc;
    ctab *codes;
    int  (*pick)(void *arg, HWND dlg, link *l, u8 level, const char *typed,
                 int find, char *code, u8 *act);
} wf_ops;

/* Make *out a form for def in mode (FORM_) with every value empty, a
   form of the model; 0, or WF_EFAIL with *out NULL when form_new
   refused (memory, or a fault of the table).  form_close frees it. */
int wf_open(form **out, const form_def *def, u8 mode);

/* Show f in its dialog over owner, a window of the program, until it
   ends; the result, WF_. l is the link of the save and of the pick
   lists, NULL for a dialog that has neither, and level the user's
   access level (rqe_class); id (idsz bytes) is the record's ID
   or code, shown in the caption, which an ADD replaces by the new one
   (form_save).  ops and arg, passed to its functions, are the owner's.
   One form dialog is open at a time; wf_run called while one is gives
   WF_EFAIL at once. */
int wf_run(form *f, HWND owner, link *l, u8 level, const wf_ops *ops,
           void *arg, char *id, u16 idsz);

/* Make show the Help button's (above), the help of the topic over the
   dialog owner; NULL, as at first, for no Help button. */
void wf_help(void (*show)(HWND owner, const char *topic));

/* Wipe the passwords of the open form dialog, if any, its controls and
   its slots, when Windows ends while it is open; form_wipe comes after,
   as it frees the form this reads. */
void wf_wipe(void);

/* 1 once OK in a dialog of f has sent a request, a save or a command,
   whatever came of it, so that some of it may have been saved; else 0.
   wf_open makes it 0. */
int wf_sent(form *f);

/* The help topic of the open form dialog's table (form_def's topic), or
   NULL when no form dialog is open or its table names none. */
const char *wf_topic(void);

/* The name (form_field's) of the field whose control, or pick button,
   has the focus in the open form dialog, or NULL when no form dialog is
   open or the focus is elsewhere. */
const char *wf_field(void);

/* The path field of location field d of f, loaded with what the form
   shows for the location field's value without a request: none's text
   for no location, "" for any (a filter's); 0 then, or 1, with nothing
   loaded, when the value names a location, which wf_loc_get reads. */
int wf_loc_asks(form *f, const scr_locf *d);

/* Read the location that the value of location field d of f names, with
   LOC GET over l, into *x, and load f's path field with its path as the
   form shows it, cut to the field (msg_path), "" after a failure: RQ_OK,
   or the failure's code with res its response. */
int wf_loc_get(form *f, link *l, const scr_locf *d, scr_locinfo *x,
               rq_res *res);

/* Field d's label into t (MSG_MAXLEN + 1 bytes), in the Windows set: the
   string WF_S_LABEL plus its message number of inst, else the
   message. */
void wf_label(HINSTANCE inst, const form_field *d, char *t);

/* The value of field i of f as a view shows it, into t (FORM_MAXVAL + 1
   bytes), CP437: a date and a number, but a record's ID (FF_ID), in
   wrow_intl's formats, a choice by its name where its field names its
   choices, any other value, and one of no such form, as it is. */
void wf_view_text(form *f, u16 i, char *t);

/* The name of the record of key in f into t (WF_NAMESZ bytes), as ops
   gives it: key, unless WF_O_NOKEY, then the value of each field of
   ops->names that is not empty, after a blank, cut to fit ("1 IBM
   5150"). */
void wf_name(form *f, const wf_ops *ops, const char *key, char *t);

#endif
