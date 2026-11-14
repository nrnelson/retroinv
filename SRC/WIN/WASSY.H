/*
 * WASSY.H - the assembly commands and the split of INVWIN (WASSY.C), the
 * own commands (ws_act of WSCR.H) of the screens of items (the item
 * screen, a search's results, a location's contents, the assemblies)
 * and of an item's parts, and the source of the assemblies.
 *
 * wassy_item_acts act on the selected item as INV's item lists do: Move
 * (WS_K_MOVE of WSCR.H) moves it with its parts to a location (ASSY MOVE id
 * LOC=), Install in (WS_K_INSTALL) installs it in an item whose ID is asked
 * for (ASSY INSTALL id parent), Take out (WS_K_REMOVE) takes it out of its
 * assembly to a location (ASSY REMOVE id LOC=) and Split units (WS_K_SPLIT)
 * splits units off it into a new item (ITEM SPLIT id QTY=).  wassy_part_acts
 * are those of the parts screen, as INV's Parts tab: INS, Install part,
 * installs an item whose ID is asked for in the screen's item, its scope (ASSY
 * INSTALL child item), and Take out takes the selected part out of its
 * assembly to a location.  Move, Install in and Take out of the items and Take
 * out of the parts act on each row chosen, asked once (WA_MULTI of WSCR.H).
 *
 * Each asks in a dialog of one field: the location, LOC, labelled
 * MI_L_LOC and titled MA_Q_MOVE or MA_Q_OUT, a location field of
 * WFORM.H with its path below it, "0" for no location (MO_NOLOC, the
 * pick list's lead row), refused blank, which starts where the item is
 * (a part where its assembly is), Move sending nothing when that location
 * is chosen again (WA_KEEP); the item to install in, PARENT, titled
 * MA_Q_PARENT, and the part to install, CHILD, titled MA_Q_CHILD, each
 * labelled MI_L_ITEMID, its ID or text to find it in the pick list of
 * every item, which digits that name no item also search for (the
 * frame's ask, INVWIN.C); the units to split off, QTY, labelled
 * MI_L_QTY and titled MI_SPLITQ.  An assembly command's request is
 * scr_assy_send's (SCRAFORM.H), so an item gone meanwhile is refused on
 * its field as MI_NOITEM; the split's refusals are the server's, on
 * QTY.  A command made notes it, as INV does: MA_MOVED, MA_INSTALLED or
 * MA_REMOVED, the item moved, installed or taken out and where to, or
 * MI_SPLIT, the new item.
 *
 * The assemblies are an item list, as INV's: ASSY LIST TOP=1, the items
 * with parts that are installed in no other, through wassy_src.
 */
#ifndef WASSY_H
#define WASSY_H

#include "WSCR.H"

extern const ws_act wassy_item_acts[];
extern const ws_act wassy_part_acts[];

/* The assemblies' source, a template as itl_src of ITEMLS.H is, which
   the frame copies, giving it fmt: itl_src's columns without its
   orders, ASSY LIST TOP=1, and MA_EMPTY for no rows. */
extern const lv_src wassy_src;

#endif
