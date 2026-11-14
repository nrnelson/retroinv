/*
 * KEYSCR.H - key scripts and screen snapshots (KEYSCR.C), the test
 * hook of the full-screen client.
 *
 * INV's test switches /KEYS:file and /SNAP:file replace the keyboard
 * and the mouse by a key script and write screen snapshots to a file,
 * so that a run is the same on every machine: the host build, DOSBox-X
 * with a CGA or a Hercules (MDA) adapter, and MS-DOS 3.10 under QEMU.
 * The script is read through the cf_ functions and handed to the
 * keyboard layer as its key source (key_set_source).  Keys are taken
 * one at a time, exactly when the program asks for a key, so a snapshot
 * shows the screen as it stands at that point, after everything before
 * it has been drawn and flushed.  Only the keys of a P line are typed
 * ahead: the program sees them waiting (key_peek) before it reads them,
 * as it sees keys typed during a long operation.
 *
 * Script format: text lines ended by LF (a CR before it is dropped), at
 * most KS_MAXLINE bytes and without NUL bytes; a Ctrl-Z (1Ah) ends the
 * script, also inside a T line, whose bytes before it are still typed;
 * a word is a run of characters other than space and TAB; command
 * letters and key names are matched without regard to case.
 *
 *   # text         a comment; blank lines are ignored too
 *   T text         type the bytes after "T " as they stand (CP437)
 *   K name ...     the named keys in order, separated by spaces:
 *                  F1..F10 ENTER ESC TAB STAB BS SPACE UP DOWN LEFT
 *                  RIGHT PGUP PGDN HOME END INS DEL CLEFT CRIGHT CHOME
 *                  CEND CPGUP CPGDN BREAK, ALT-A..ALT-Z, CTRL-A..CTRL-Z,
 *                  xHHHH for any key code, or a single character, which
 *                  stands for itself (tui_keyname)
 *   P name ...     keys named as in K lines, typed ahead: from the first
 *                  look for a waiting key after the keys before them
 *                  have been read, they are seen waiting, one by one,
 *                  and they are read in order, as K keys are.  It may
 *                  not follow an S or M line (comments, blank lines and
 *                  lines without keys aside), which holds back every
 *                  look until the next key is read
 *   S name         write a snapshot titled name (1..KS_NAMELEN of A-Z,
 *                  0-9, _) before the next key is given
 *   M row col len  from the next snapshot on, write the len cells from
 *                  row, col (cut at the row's end) as '?' in the text
 *                  and as attribute 00h, for cells that differ between
 *                  machines (the adapter, DOS version and flags on the
 *                  settings screen); at most KS_MAXMASK masks
 *   M CLEAR        drop every mask
 *   C row col      a click of the mouse's left button on the cell at
 *                  row, col (KEY_CLICK), given as a K line's key is
 *   D row col      the second press of a double click there
 *                  (KEY_DCLICK): a double click on the screen is a C
 *                  line followed by a D line on the same cell.  The
 *                  right button is Esc (K ESC)
 *
 * The key names of K lines are messages of the message file (MSG.H),
 * which tui_keyname matches, so their table takes no near data.
 * ks_open reads the whole script once to check it; a line it does not
 * understand is refused before anything runs.  After the last line the
 * source returns KEY_EOS, and the program ends (tui_key).
 *
 * Snapshot format: every line ends CR LF, on every build.
 *
 *   =SNAP name PALETTE=COLOR|MONO|MONOCGA CURSOR=row,col,s VRAM=OK|row,col
 *                  (s the cursor shape: LINE, BLOCK or HIDE)
 *   |80 characters of row 0|
 *   ... (VID_ROWS lines)
 *   =ATTR A=hh B=hh ...
 *   |80 letters of row 0|
 *   ... (VID_ROWS lines)
 *
 * A hidden cursor is always written CURSOR=0,0,HIDE, wherever it was
 * parked.  VRAM= ignores masked cells (vid_verify gives only the first
 * differing cell; when that cell is masked, VRAM=OK).
 *
 * When the program ends, ks_close appends one more line,
 *
 *   =STACK UNUSED=n
 *
 * with the stack bytes never touched (cstk_unused), left out when that
 * is unknown (CSTK_UNKNOWN, the host build).  Golden comparisons ignore
 * this line; the test harness checks n against its margin.
 *
 * The text lines hold the screen's character bytes unchanged, except
 * 00h, 0Ah, 0Dh and 1Ah, written as '?' (the screens never draw them).
 * The attribute lines give each cell a letter, A..Z then a..z, in the
 * order the attributes first occur reading the screen row by row; the
 * =ATTR line lists each letter's adapter attribute in hex.  VRAM= is
 * the result of vid_verify: OK when the adapter shows exactly the
 * shadow, else the first cell that differs.
 */
#ifndef KEYSCR_H
#define KEYSCR_H

#include "COMPAT.H"

#define KS_MAXLINE      128     /* bytes of a script line, LF excluded  */
#define KS_NAMELEN      8       /* characters of a snapshot name        */
#define KS_MAXMASK      16      /* masks in force at once               */

/* Check the script keys and install it as the key source; create (or
   empty) the snapshot file snap, which may be NULL for none.  The key
   names are messages, so msg_load must have run.  Returns 0; or a CF_E
   code when a file cannot be opened or created (why then ""), or when
   the script cannot be read (why then its file name); or -1 when the
   script has a bad line, why (whysz bytes) then giving "file:line".
   A read error or a failed snapshot write while the program runs ends
   the script, as its end does, and ks_close reports it. */
int ks_open(const char *keys, const char *snap, char *why, u16 whysz);

/* Write the =STACK line, remove the key source and close both files.
   Returns 0; or the CF_E code of the first failure since ks_open: a
   script read or snapshot write that ended the run, then the =STACK
   write and the closes.  0 when not open. */
int ks_close(void);

#endif
