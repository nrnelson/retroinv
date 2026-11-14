/*
 * WEDIT.H - the edit controls of the Windows programs that Windows 3.0
 * cannot make alone: password controls that keep no copy of what is
 * deleted, read-only ones, and controls that hold CP437 text (WEDIT.C).
 *
 * All are edit controls subclassed by one procedure of WEDIT.C, which
 * tells them apart by the style: a control of style ES_PASSWORD is a
 * password control, one of style WE_READONLY a read-only one, any other
 * an OEM text control.
 *
 * A password control.  Windows' edit control keeps the text it deletes
 * in an undo buffer outside DGROUP, which it frees without clearing,
 * so each deletion, by BS, DEL (SHIFT-DEL, cut, too, without the
 * clipboard) or a character typed over a selection, is made on a copy
 * on the stack that goes back whole by SetWindowText, which keeps no
 * undo copy, and is wiped; another character goes to the control,
 * which inserts it without an undo copy.  Paste, clear and undo, by
 * their messages, by the INS key or by ALT+BS, which the control runs
 * itself, do nothing, and so does CTRL+BS, which the control would type
 * as the character DEL.  The control holds at most LINK_PWMAX
 * characters, as the copy is sized so.  we_wipe overwrites its text
 * with as many blanks before it empties it, as an edit control writes a
 * text no longer than the one it holds in the same place, so that its
 * buffer keeps none of the password.
 *
 * A read-only control.  ES_READONLY came with Windows 3.1, and Windows
 * 3.0 edits whatever it holds, so the procedure drops every character,
 * DEL, and what a password control drops but CTRL-INS, which copies the
 * selection: the text stays as set, while the caret, the selection and
 * the scroll bars move as usual and the control takes the focus, unlike
 * a disabled one, so a long text can still be read and copied.
 * WE_READONLY is ES_READONLY's bit, which Windows 3.0 ignores.
 *
 * An OEM text control holds CP437 bytes as they are, for a control in
 * OEM_FIXED_FONT (which its owner sets), so that a text crosses with no
 * conversion and loses no character: a character typed above 7Fh,
 * which Windows gives in its own set, goes in through AnsiToOemBuff,
 * and copy, cut and paste, by their messages or by CTRL-INS, SHIFT-DEL
 * and SHIFT-INS, carry the clipboard's CF_OEMTEXT, which Windows turns
 * into CF_TEXT for other programs; a copy or cut of no selection leaves
 * the clipboard as it was.
 */
#ifndef WEDIT_H
#define WEDIT_H

#include "COMPAT.H"
#include "CWIN.H"

#define WE_READONLY     0x0800L /* the style of a read-only control     */

/* Subclass the edit control e, a control of a window of the program:
   with readonly 1 as a read-only control (given style WE_READONLY, also
   when it was subclassed before), else as a password control (style
   ES_PASSWORD, then limited to LINK_PWMAX characters) or an OEM text
   control.  1 when it is subclassed, also when it already was, or 0
   when Windows gave no procedure thunk, leaving e unsubclassed, which
   its caller then disables.  The thunk, one for every control, is made
   for the first control and freed after the last. */
int we_attach(HWND e, int readonly);

/* Wipe the password control e (any edit control of style ES_PASSWORD,
   subclassed or not): its text overwritten with blanks, then emptied. */
void we_wipe(HWND e);

/* Before e is destroyed: wipe it when it is a password control, and
   give it back its own procedure when we_attach subclassed it. */
void we_detach(HWND e);

#endif
