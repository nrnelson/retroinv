/*
 * WUSER.H - the users screen of INVWIN (WUSER.C), a screen of the frame
 * (WSCR.H), as INV's (SCRUSER.C): Admin > Users, at level 3 only.
 *
 * Its list shows USER LIST through scr_user_src (SCRZFORM.H), with INV's
 * rows, a disabled user's marked, column header and empty text, and
 * titles the frame MU_TITLE; beside it, in the split view, stands what
 * the selected user did (wpane_user of WPANE.H).  Its records show in
 * the user form of SCRZFORM.H, read with USER GET, the group through
 * scr_user_kline, and captioned MU_FTITLE with the user name; a new
 * user's form has no status (WS_NEWREST) and is captioned MU_NEWTITLE.
 * OK first refuses a password whose repetition differs, MU_NOMATCH with
 * the focus on the repetition.  A new user is then saved by USER ADD,
 * with the password; a saved one by USER RESET when a password is
 * typed, which makes the user change it at the next login, then by USER
 * SET for the other changes, a failure of which a message box first
 * says came after the password was saved (MU_PWONLY); left empty, the
 * password stays.  The dialog wipes both passwords, controls and slots,
 * once the save has been tried (WFORM.H).  DEL deletes the selected user
 * with USER DEL once MU_DELQ has been answered Yes.  A user added, saved
 * or deleted says so on the status bar, MU_ADDED, MU_SAVED or
 * MU_DELETED.  The server's refusals show as every failed request's:
 * deleting, disabling or demoting the last active administrator is 202,
 * with the server's reason, a password too short or too long 102 on the
 * password, a user name that exists 201.
 */
#ifndef WUSER_H
#define WUSER_H

#include "WSCR.H"

extern const ws_scr wuser_scr;

#endif
