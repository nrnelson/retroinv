/*
 * WSVC.H - the service screens of INVWIN (WSVC.C), screens of the frame
 * (WSCR.H).
 *
 * wsvc_recent, Items > Service, shows MAINT RECENT through scr_maint_src
 * (SCRMFORM.H), the newest record added first, titled MV_T_RECENT; INS
 * does nothing there, as in INV's recent list.  wsvc_item, a scoped
 * screen opened from a record of the item screen, shows the item's
 * records, MAINT LIST through scr_maint_isrc, the oldest service date
 * first, titled MV_T_ITEM for the item, and a new record takes the
 * item's ID in ITEM_ID.  Both have INV's rows, column headers and empty
 * text, and show their records in the service record form of SCRMFORM.H,
 * read with MAINT GET, saved as MAINT with the new ID in MAINT_ID and a
 * record's caption MV_TITLE, with their notes (mio_load and mio_save of
 * MEMOIO.H), and deleted with MAINT DEL once MV_DELQ has been answered.
 * A record added, saved or deleted says so on the status bar, MV_ADDED,
 * MV_SAVED or MV_DELETED.
 */
#ifndef WSVC_H
#define WSVC_H

#include "WSCR.H"

extern const ws_scr wsvc_recent;
extern const ws_scr wsvc_item;

#endif
