/*
 * WFILE.H - the file name dialog of the Windows programs (WFILE.C): a
 * file of this machine, typed or chosen from a list of its directories,
 * as the Open and Save As dialogs of Windows 3.0's own programs take one;
 * COMMDLG is Windows 3.1's, for protected mode only.
 *
 * The dialog, WFILE_R_DLG of WFILE.RC, which the program's resource
 * script includes, is captioned by its owner and holds the name
 * (WFILE_C_NAME), the directory listed (WFILE_C_DIR), the list of that
 * directory's files matching the owner's pattern, its subdirectories and
 * the drives, as DlgDirList fills it (WFILE_C_LIST: [SUB], [..], [-C-]),
 * the message line (WFILE_C_MSG), OK and Cancel.  It opens on the
 * program's current directory, the name holding the pattern, selected.
 *
 * A row chosen in the list goes into the name (DlgDirSelect): a file's
 * name, or a directory's or a drive's path followed by the pattern; a
 * double click on a row is OK.  OK takes the name without its leading
 * and trailing blanks:
 *
 *   - nothing: the focus goes to the name;
 *   - a name holding * or ?, after a directory or not: it is listed, in
 *     that directory, and is the pattern from then on;
 *   - a directory or a drive, from the one listed: it is listed with the
 *     pattern, which goes into the name;
 *   - else a file: its directory part, when it has one, is listed as a
 *     directory is, the name keeping the file's own; the dialog ends
 *     with the file's path in the directory listed then.  The file need
 *     not exist, as for a file to be written.
 *
 * A name that the list cannot follow, a directory or a drive that does
 * not exist or holds no disk, gives WFILE_S_NODIR on the message line,
 * and a path longer than the owner takes WFILE_S_LONG, the focus on the
 * name.  A drive without a disk asks nothing: the program's error mode
 * (dos_crit_install of COMPAT.H, SEM_FAILCRITICALERRORS under Windows)
 * makes Windows fail the call.  Cancel, Esc and the system menu's Close
 * end the dialog with nothing.
 *
 * The program's current directory.  DlgDirList makes the directory it
 * lists the task's current drive and directory, and an engine running in
 * the program names its files from its data directory, which may be
 * relative.  So the dialog changes them only for each listing and gives
 * them back at once (DlgDirList without a list, which only changes them),
 * and never leaves another directory current between two messages; the
 * directory listed is its own, as OpenFile parses a name there
 * (OF_PARSE), CP437 and ending in its separator.
 *
 * Text: the name typed and the list are in the Windows set, the caption
 * and the path given in CP437 (OemToAnsi, AnsiToOem).
 */
#ifndef WFILE_H
#define WFILE_H

#include "COMPAT.H"
#include "CWIN.H"
#include "WFILESTR.H"

/* wfile_run's results. */
#define WFILE_EFAIL     (-1)    /* the dialog could not be made, or
                                   another file name dialog is open     */
#define WFILE_CANCEL    0       /* left without a file                  */
#define WFILE_OK        1       /* a file chosen                        */

/* Run the dialog over owner, a window of the program, captioned with
   message title, listing the files of spec, a name pattern of the 8.3
   form ("*.CSV").  On WFILE_OK, path (pathsz bytes, at most DOS_PATHMAX)
   holds the file's path, CP437, at most pathsz - 1 characters, which
   also bounds the name typed; else it is as it was. */
int wfile_run(HWND owner, u16 title, const char *spec, char *path,
              u16 pathsz);

#endif
