/*
 * WSET.H - INVWIN's settings (WSET.C): Options > Settings, at every
 * level, as INV's settings screen (SCRSET.C), the file being the
 * user's own.
 *
 * The settings dialog is the form dialog (WFORM.H), captioned MC_TITLE,
 * over the keys of the settings file (CFG.H) that INVWIN uses: the data
 * directory (DATA=; when the file sets none, the directory a local
 * session then takes, which a save leaves unset unless it was changed),
 * the station (STATION=), the default user (USER=), the item list's
 * order (ITEMORD=, a drop-down list of its orders), the serial link's
 * baud rate (BAUD=, a drop-down list of the rates of UART_BAUDOK) and
 * RTS/CTS flow control (RTSCTS=, a check box); then, read-only, the
 * settings file's path and VER's text.  It shows the file as it is when
 * the dialog opens, read with cfg_load, so that a save is shown again
 * by the next opening; a file that cannot be read is said in a message
 * box, IW_S_NOCFG with what its DOS error means, and no dialog opens.
 * No request is sent.
 *
 * OK writes the file with cfg_save, which keeps every other key, those
 * of INV's screen and printer among them, every comment and every line
 * it does not know; the paths it refuses (CFG.H), the scratch name's
 * among them, cfg_load refuses as well, so INVWIN's start, which reads
 * the same path, has ended on them already.  A save that succeeded is
 * said in a message box, MC_WSAVED: the data directory, the station, the
 * default user, the baud rate and RTS/CTS take effect at the next start,
 * the item list's order at once, as the frame applies it.  A write that
 * failed is said in a message box, MC_SAVEFAIL with the file and what
 * its DOS error means (rqe_doserr of RQERR.H), and
 * the dialog shows again with the values as typed.  Cancel, Esc and
 * Close leave the file as it is, asking nothing, as the dialog saves no
 * record.
 */
#ifndef WSET_H
#define WSET_H

#include "COMPAT.H"
#include "CWIN.H"
#include "CFG.H"

/* Run the settings dialog over owner for the settings file path, VER's
   text being version and defdir the data directory of a local session
   started without one.  Returns WF_OK once the file is written, the
   item list's order saved then in itemord (CFG_ORDLEN + 1 bytes), else
   WF_CANCEL, or WF_EFAIL when the dialog could not be made (WFORM.H),
   which the caller says. */
int wset_run(HWND owner, const char *path, const char *version,
             const char *defdir, char *itemord);

#endif
