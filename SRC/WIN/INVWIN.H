/*
 * INVWIN.H - the resource numbers that INVWIN.RC, INVWIN.C and the screens'
 * modules share, and the windows of INVWIN that a key script of the test
 * driver (TDRV.H) meets.
 *
 * The resource compiler reads it too, so it holds #define lines only.
 *
 * The test driver runs INVWIN with /KEYS:file and /SNAP:file, checking
 * where each dialog opens (td_place); /SNAP: without /KEYS: is a usage
 * error, which shows IW_S_USAGE.
 *
 * The frame, of class INVWIN, is titled IW_S_TITLE, or IW_S_LTITLE with the
 * list's title while a list is open: a message MI_T_ID to MI_T_SER (ITEMLS.H)
 * for the item list's order, or the title of the screen shown (WLOC.H, WSVC.H,
 * WLOAN.H, WUSER.H, WADMIN.H), of a search's results (wsrch_title of WSRCH.H),
 * of a location's contents (wloc_ctitle of WLOC.H, its path), followed as in
 * IW_S_CTITLE by MO_S_ALL or MO_S_TOP and MO_S_HERE or MO_S_BELOW (SCRLFORM.H)
 * naming the items they show, of the assemblies (MA_T_LIST of SCRAFORM.H) or
 * of an item's parts (MA_T_ITEM), a scoped screen's with its item's name as ^1
 * (WSCR.H: "1 IBM 5150").  It holds the menu bar and one list while a user is
 * logged in, two on a screen shown with its pane (the split view, below), the
 * tool bar along the top of its client area and the status bar along its
 * bottom.
 *
 * The tool bar, a WTOOL window (WTOOL.H) of control ID IW_C_TOOL, holds the
 * buttons of IW_S_TOOL, New, Open, Edit, Delete, Move, Install and Lend, each
 * giving the command of its Record entry (below), New's that of INS, Open's
 * ENTER, Edit's CTRL+E, Delete's DEL, Move's WS_K_MOVE and Install's
 * WS_K_INSTALL (WSCR.H), which have no key, and Lend's CTRL+L, then, after its
 * slot, Find, of command IW_M_FIND.  A button but Find is grayed while the
 * Record menu has no entry of its text not grayed, New standing for Install
 * part too, INS's on an item's parts, so Install is grayed on the loan lists
 * and an item's parts, whose WS_K_INSTALL does nothing.  The slot holds the
 * Find box, an edit control IW_C_FIND of LINK_MAXFIND characters.  CTRL+F in
 * any of the frame's windows gives it the focus, its text selected; there
 * ENTER, as Find, finds the items holding its text, its blanks at both ends
 * left out, as the search dialog's Text alone does (wsrch_find of WSRCH.H),
 * opening the results in place of the list shown, which takes the focus, the
 * box keeping the text, and with no text runs the search dialog as Find text
 * does; ESC and TAB give the focus back to the list.  While no user is logged
 * in every button is grayed and the box disabled.
 *
 * The status bar, a WSTAT window (WSTAT.H) of control ID IW_C_STATUS, has four
 * panels, each but the last as wide as its text, the count's as its widest
 * text for five digits: the connection and its target (IW_S_LOCAL, IW_S_NET,
 * IW_S_COM, IW_S_TCP or IW_S_MBOX); while logged in, the user and level
 * (IW_S_USER); once the list shown, the one with the focus in the split view,
 * has counted its rows (wl_count of WLIST.H, on a LOCAL, NET or TCP link),
 * their number in the noun of its screen, IW_S_NITEMS to IW_S_NAUDIT or
 * IW_S_NCATS, or for a pane of the split view that has one in its own string
 * with what it shows (ws_pane of WSCR.H), for the contents beside the tree
 * IW_S_NHERE with the location's name, cut to that width, the singular at the
 * next string for one, or
 * while it has several rows chosen IW_S_NSEL, or IW_S_NADD in add mode, on
 * any link; and
 * the help panel: while a menu entry is highlighted its help (below), else a
 * note after a change of a record (below) or a long request's text (ws_info of
 * WSCR.H).
 *
 * The menu bar holds File (IW_S_MFILE), the menus of invmenu_bar (INVMENU.H)
 * in their order, then Record (below), View (IW_S_MVIEW) and Help; the Items
 * menu has, after Browse by serial, an entry of INVWIN's own, Browse by
 * category (IW_M_BYCAT, IW_S_MBYCAT), which opens the categories with their
 * items (The categories, below) at every level.  File holds the entries of the
 * table whose commands are the main loop's, TUI_CMD_LOGIN and TUI_CMD_QUIT,
 * taken out of their menu: Log out, a separator and Exit, whose text is
 * IW_S_MEXIT, naming Alt+F4, the frame's Close, in place of Quit's.  The
 * command ID of an entry of the table is its command, an SCMD_ number of
 * SCR.H, TUI_CMD_LOGIN or TUI_CMD_QUIT, and its text its message of
 * INV.MSG with the '&' of its hot key, then "..." when the command always
 * opens a dialog box in which the user completes, cancels or closes it (a
 * form, a pick list, a code list, the sessions; not a question in a message
 * box alone, as Pack's, nor Log out, carried out before the login dialog
 * opens); the key a message names is INV's, so no entry of the table shows one
 * (INVWIN's CTRL+F gives the Find box the focus, above, not Find text's
 * dialog), and Exit names Alt+F4.  While the item list is shown, the Browse
 * entry of its order is checked, and Browse by category while the categories
 * are, no other one otherwise.  An entry above the user's level is grayed, so
 * that every level finds the same entries in the same places, as in INV, and
 * gives no command, by the menu or by its key; every menu still opens.
 * Windows' arrow keys stop on a grayed entry as on any other, and ENTER there
 * closes the menu without a command.  Browse by ID, by location, by maker and
 * by serial (SCMD_ITEMS_ID to SCMD_ITEMS_SER) open the item list in their
 * order, New item shows a new item in the item form, Assemblies opens the
 * assemblies, Categories the code list of the categories (WCODE.H), Browse
 * locations opens the location tree, once the names of the location types are
 * loaded (wloc_types of WLOC.H), New location shows a new location at the top
 * level in the location form, Location types the code list of the location
 * types, after which the list shown is fetched again, the tree with the names
 * of the types loaded again, Service opens the recent service list, Loans and
 * holds the open loans, reservations and uses of every kind (WLOAN.H), Find
 * text, Find serial number and Filter items run the search dialog of WSRCH.H,
 * which opens the results of its search in place of the list shown, the five
 * entries of Reports run their report dialogs, as WRPT.H says, Settings runs
 * the settings dialog of WSET.H, after which a new item list order opens the
 * item list shown again in it, Users opens the user list of WUSER.H, Backup,
 * Restore, Pack and Reindex run an operation on the data files, after which
 * the list shown is fetched again (PG_RELOAD), Audit log asks for the audit
 * log's filter and opens its list, Sessions shows the sessions dialog,
 * IW_R_SESS, as WADMIN.H says, Export CSV and Import CSV run the dialogs of
 * WCSV.H, after which the list shown is fetched again as after a form (below),
 * Change password runs the password dialog, Log out ends the login and shows
 * the login dialog again, and Exit ends the program.  Help holds Help on this
 * screen (IW_M_HELP), naming F1, the help's own key (WHELP.H), Getting started
 * (IW_M_START), Keyboard (IW_M_KEYS), a separator and About (IW_M_ABOUT), the
 * only one of these with "...", as the help window is a text to read.  View
 * holds Pane beside the list (IW_M_PANE, IW_S_VPANE), checked while the split
 * view shows (below) and grayed on a screen that has no pane, which opens the
 * screen shown again with its pane, or without it, in its order and on its
 * selected row, and keeps the choice as the screen's WINPANE. of the settings,
 * written to the settings file when it can be read; and Refresh
 * (IW_S_VREFRESH), naming F5, whose command is that key's, IW_M_KEY plus VK_F5
 * (below), grayed while no list is open.  The help of an entry, a line of at
 * most IW_H_MAXLEN bytes, is string IW_H_MENU plus the number of the text the
 * entry shows: its message of INV.MSG (MN_ of INVMENU.H) or its string here,
 * IW_S_MEXIT, IW_S_MBYCAT, IW_S_MHELPON, IW_S_MSTART, IW_S_MKEYS, IW_S_MABOUT,
 * IW_S_VPANE, IW_S_VREFRESH and the Record menu's.  The status bar's help
 * panel shows it while the entry is highlighted, in the menu bar or the popup
 * (below), grayed or not, and nothing while a menu's title, a separator or the
 * system menu is, or once the menus close.
 *
 * The Record menu gives the record commands of the list shown (below), the one
 * with the focus in the split view, as its entries, each with its key's name
 * after a TAB where it has one, so that a mouse finds them: an entry, of
 * command IW_M_KEY plus its record key (INVWIN.C: a virtual key, plus
 * IW_M_SHIFT for SHIFT, or a WS_K_ command of WSCR.H, which no key gives),
 * does what its key does; those that always open a dialog box end in "...", as
 * the menu bar's.  Open (ENTER), Edit (CTRL+E), New (INS) and Delete (DEL) are
 * always there; then Select all (CTRL+/), which has no access letter, each of
 * its letters being another entry's on the screens of items, on a list of
 * several rows (below); then the screen's own commands, on the screens of
 * items Move, Duplicate (CTRL+D), Install in, Take out and Split units, on an
 * item's parts Install part (INS) in New's place and Take out, on the loan
 * lists Open item
 * where the rows name their items and Return or release, or the location
 * tree's Move; then, on the screens of items and the loan lists that add rows,
 * Lend, Reserve and Use with (CTRL+L, CTRL+R, CTRL+U); then, on the screens of
 * items, Parts, Service records, Loans and holds and Used with it; then Next
 * list (CTRL+PGDN) on the loans and holds, Next order on a list with orders,
 * or Not installed only and Everything below on a location's contents, each
 * checked while on, Next pane (F6) in the split view, and Back (ESC) on every
 * screen but the item list; a separator between these groups.  An entry is
 * grayed where it does nothing: Open, Edit, New or Delete on a screen without
 * that command, an entry that needs a selected row while the list has none,
 * Edit, New, Delete, the screen's own commands, Duplicate and Move below
 * TUI_LVL_OPER, and every entry while no list is open.  The menu is filled as
 * each list opens or takes the focus from the other of the split view, and
 * again when a move leaves the list with rows where it had none, or none where
 * it had some.  It also opens as a popup menu over the list: a press of the
 * right mouse button on the list's rows selects the row under it as a left
 * click does, and its release there opens the menu at the pointer; SHIFT-F10
 * does the same at the selected row, the list's box clicked at the middle of
 * its rectangle (or near its top left corner without one).  The right button's
 * press, as the left's, ends a note on the status bar.
 *
 * The list, a WLIST window (WLIST.H) of control ID IW_C_LIST, is the item
 * list, ITEM LIST through itl_src (ITEMLS.H), an item's parts, ASSY BOM
 * through scr_assy_src (SCRAFORM.H), the location tree of WLOC.H, a service
 * list of WSVC.H, a loan list of WLOAN.H, the user list of WUSER.H, the audit
 * log of WADMIN.H, the categories or a category's items (The categories,
 * below), or an item list without orders: a search's results through
 * wsrch_src (WSRCH.H), a location's contents through wloc_csrc (WLOC.H) or the
 * assemblies through wassy_src (WASSY.H); each has INV's rows, column headers
 * and empty text.  A login opens the item list in the order of ITEMORD= of the
 * settings, by ID without one, a browse entry opens it again in place of the
 * list shown, and Assemblies and Browse locations open theirs in its place;
 * each opening loads the first page, its first row selected.  The list fills
 * the client area between the tool bar and the status bar, the split view's
 * two side by side, and has the focus whenever the frame has it.  Its pager's
 * window holds, from 1 to LV_MAXPAGE, as many rows as a list window of
 * WLIST.H's height and width for them fits there when it opens; once the frame
 * takes a size, or the splitter a place, where another number fits, the list
 * is opened again in the order in use, the list of a screen that has a pane
 * (the tree, its contents shown or not) seeking the row it had selected, while
 * minimizing and restoring the frame keep it as
 * it is.  Its header's headings (WLIST.H) name the item list's orders: a click
 * on ID, Maker, Location (the tree order) or Serial number switches the list
 * to that order as Next order does, and a drag of a divider between two
 * headings sets the width of the column left of it in every list of those
 * columns, writing WINCOLS. of the settings (CFG.H) when it can read them;
 * every list is laid out in the widths WINCOLS. gives as the program starts.
 * The keys of WLIST.H move it, and F5 fetches it again, counted anew.  Next
 * order takes the next order of a list with orders, CTRL+PGDN on the loans and
 * holds their next view and CTRL+PGUP the one before (WLOAN.H), and on a
 * location's contents Not installed only switches between its items not
 * installed in another and all (wloc_top of WLOC.H), and Everything below
 * between the location's own items and those of every location below it too
 * (wloc_all), each opening the list again; a location's contents start with
 * all its own items.  On every list the record commands are those of INV's
 * item list (INV.HLP): ENTER on a row, or a double click, shows its record in
 * its form in FORM_VIEW, on the location tree the location's contents instead,
 * as INV's tree does, CTRL+E in FORM_EDIT, INS shows a new record, on the tree
 * a new location inside the selected one (WS_TREE of WSCR.H), and DEL deletes
 * the selected one once a message box asking MI_DELQ, MO_DELQ, MV_DELQ or
 * MU_DELQ, its default button No, has been answered Yes, ^1 the record's name
 * (WSCR.H): an item's ID with its maker and model, a location's name or a
 * service record's ID with its summary, read first with ITEM GET, LOC GET or
 * MAINT GET, whose failure is shown and the list fetched again as after a
 * form's, or a user's name alone; on a loan list INS, CTRL+L, CTRL+R and
 * CTRL+U add a row of a kind, the item asked for first on the lists of every
 * item, as WLOAN.H says, DEL does nothing, Open item shows the row's item in
 * the item form, whose tabs come back to it, and Return or release ends the
 * selected row; on the screens of items CTRL+L, CTRL+R and CTRL+U show a new
 * loan, reservation or use of the selected item in its form (WLOAN.H).  On the
 * screens of items, the item list, a search's results, a location's contents
 * and the assemblies, Move, Install in and Take out move the selected item,
 * install it in another and take it out of its assembly, Split units splits
 * units off it into a new item, and CTRL+D duplicates it with ITEM COPY and
 * shows the copy, the ID of its +OK, in the item form in FORM_EDIT, whose
 * Cancel deletes the copy again with ITEM DEL, the list left as it was, unless
 * the form has gone to a tab (below) or its OK has sent a save (wf_sent of
 * WFORM.H); on an item's parts, items shown in the item form, INS (Install
 * part) installs a part, Take out takes the selected one out and DEL does
 * nothing (WASSY.H).  Edit, New, Delete, Duplicate, Lend, Reserve, Use with,
 * Move and the screens' own commands need level TUI_LVL_OPER of TUI.H: below
 * it CTRL+E, INS, DEL, CTRL+D, CTRL+L, CTRL+R and CTRL+U show INV's refusal
 * (rqe_deny of RQERR.H) in a message box, and the commands the Record menu
 * alone gives are grayed there; INS does nothing on the recent service list.
 * Move on the location tree moves the selected location into another, chosen
 * from a pick list (WLOC.H), after LOC GET has read its name for the note;
 * after an add, an edit or a move the tree is fetched again from its start,
 * the location selected.  ESC on the location tree, the assemblies, the
 * categories, the recent service list or the loans and holds opens the item
 * list as a login does, as INV's ESC leaves its lists for its home screen; on
 * the user list or the audit log it opens again the list shown when its entry
 * was chosen, in its order, on its row and with its scope or filter (an audit
 * log's being the one last asked for, which WADMIN.C keeps), or the item list
 * when none was; on a location's contents it opens the location tree again,
 * that location selected; on a search's results it runs the search dialog
 * again, with the search's values and the focus where its dialog started,
 * whose OK opens the new results and whose Cancel opens the item list so, as
 * INV's Esc leaves its search form.  Parts, Service records, Loans and holds
 * and Used with it on the screens of items, at every level, open the selected
 * item's parts, service records, loans and holds, and what is reserved for it
 * or used with it, once ITEM GET has read the item's name for their titles,
 * scoped screens (WSCR.H), whose ESC opens the screen they were opened from
 * again, a list in the order it had, on the row it had selected or on its
 * first row, never on another: on a LOCAL, NET or TCP link, and on the tree
 * on any, the row is sought from the list's start (wl_find of WLIST.H) over
 * LV_SEEKMAX rows at most (PAGER.H), a request for each page passed, and the
 * first page is fetched again when the row lies beyond them, is gone or an
 * Esc stopped the seek; on a serial line or a mailbox the first page's one
 * request is all, and the row is selected when that page holds it
 * (list_seek of INVWIN.C, which every list opened on a row goes through,
 * View > Pane beside the list's and a new size's too).  In the split view
 * the focus is in the pane that had it, on the row the screen was opened
 * from, found the same way, as the item form's record buttons do (below),
 * INV's Parts, Service and Loans tabs.  ENTER, CTRL+E, DEL, CTRL+D and the
 * screen's own commands on the empty list's text, but INS on an item's parts,
 * and the other keys the list leaves to its owner do nothing.  A move that
 * sends requests holds the frame busy as every request does (INVWIN.C), and a
 * failed one shows as WLIST.H says; one that ends the login (RQE_LOGIN of
 * RQERR.H) then logs out as Log out does, and one that ends the session ends
 * the program.  A list that does not open shows IW_S_NOLIST in a message box,
 * and the frame stays without one until a browse entry, Assemblies, Browse
 * locations, Service, Loans and holds, a search, Users, Audit log or the next
 * login.  Log out and Exit destroy the list.
 *
 * Several rows.  The lists of the screens that have commands of their own
 * (ws_act of WSCR.H) but the tree, the screens of items, an item's parts and
 * the loan lists, choose several rows (WL_MULTI of WLIST.H): SHIFT+click and
 * SHIFT with Up and Down extend the selection, CTRL+click toggles a row,
 * within the rows shown, which a move that fetches drops.  Their commands
 * that take several rows (WA_MULTI of WSCR.H: Move, Install in and Take out
 * of the screens of items, Take out of the parts and Return or release of the
 * loan lists), and DEL, CTRL+L, CTRL+R and CTRL+U on the screens of items,
 * act on the rows chosen (wl_chosen), in their order, and with one row as
 * above; ENTER, CTRL+E and every other command act
 * on the selected row.  With several, the command asks once: a command's own
 * dialog for the first row, its caption naming them with IW_S_MORE, which
 * sends the first row's request from the dialog, its refusal shown there;
 * the new loan, reservation or use of the first row in its form, the help
 * panel saying IW_S_EACH meanwhile, each later row a new record of the same
 * values; DEL the question IW_S_DELN, its default button No.  Then the frame
 * sends each other row's request in one busy bracket, the help panel
 * counting them in IW_G_DOING plus the command's Record entry, an Esc typed
 * meanwhile, during a row's request too, or the first failure stopping it
 * before the next row with a box, IW_S_HALTED or the failure's text and
 * IW_S_STOPPED; a DEL of a record already gone counts as made.  A key typed
 * before a command's rows, its dialog or a request began acts before it or
 * not at all: one that a list had taken and not yet carried out comes up
 * inside the loop that follows, and is dropped there, a list's key being a
 * record command only while the frame is enabled (idle of INVWIN.C), so that
 * none acts on other rows or another screen than those it was typed at; the
 * keys still waiting as such are a dialog's own, as Windows gives them, or
 * dropped by the request (INVWIN.C), an Esc among them stopping the rows as
 * one typed later does.  The note of all of them made is IW_G_DONE plus that
 * entry, and the list is fetched again once, as after one row.  A right
 * click on a selected row keeps the selection, the popup's entries acting
 * on it (wl_right of WLIST.H).  CTRL+/, or Select all of the Record menu,
 * which these lists alone show, after its four first entries, chooses
 * every row of the list, not only those shown (wl_select_all of WLIST.H),
 * at every level, as choosing changes nothing: the help panel counts the
 * rows while their keys are collected (IW_S_COLLECT), and the count panel
 * says IW_S_NALL until a key or click that WLIST.H names, or the list
 * fetched again, leaves the focus row alone chosen; a list of more than
 * KS_MAXROWS rows (KEYSET.H) is refused with IW_S_OVER, and after that, an
 * Esc while the keys are collected or a failed request, the rows chosen
 * before stay chosen.  The commands above then act on every row,
 * in the list's order, the first row's dialog captioned and the others'
 * requests counted as for several, DEL asking IW_S_DELALL instead, and a
 * command whose form is of the row's kind (Return or release) taking the
 * focus row's; below TUI_LVL_OPER each is refused once, before it asks.
 * CTRL+\ leaves the focus row alone chosen (wl_deselect of WLIST.H), from
 * the preview panel too.  Both keys are the virtual keys File Manager
 * tests (WL_VK_ALL, WL_VK_NONE), those of / and \ on a US keyboard.
 *
 * The split view.  A screen that has a pane (ws_pane of WSCR.H, the panes
 * those of WPANE.H) shows it beside its list, the master, while WINPANE. of
 * the settings (CFG.H) says 1 for the screen, or, without a line for it, on a
 * LOCAL, NET or TCP link, whose requests are fast; with 0, or without a line
 * on a serial line or a mailbox, the list shows alone.  The location tree's
 * pane is its selected location's contents: the tree, IW_C_LIST, on the left,
 * then a splitter (WSPLIT.H), IW_C_SPLIT, then the contents, IW_C_CLIST, a
 * list window over wloc_csrc (WLOC.H) as a location's contents' screen, from
 * which it takes its keys, its record commands and its Record menu; ENTER and
 * a double click on the tree still open them as that screen.  The master is
 * as wide as the settings say, in average characters of the list font, the
 * tree in WINSPLIT= and another screen in its WINPANE., or with no width as
 * the columns its pane names, the tree's Name and Type, at most half the
 * frame; each pane keeps at least IW_PANEMIN characters (INVWIN.C), and a
 * frame too narrow for both gives each half.  A drag of the splitter sets the
 * width, writing it to the settings file when it can read it: WINSPLIT= for
 * the tree, and for another screen its WINPANE. line, which then says 1.  The
 * pane follows the master's selection once it has stayed on a row for
 * IW_SETTLE ms (INVWIN.C), or IW_SETTLE_SLOW on a serial line or a mailbox,
 * where each load holds the program: its follow function points it at the
 * row, the contents' with LOC GET, and then its list is counted again and
 * fetched from its start, the contents with LOC
 * CONTENTS, all the location's items and, when it holds locations, everywhere
 * below it too, as wloc_enter says; it loads at once as the master opens, and
 * while the master has no row it is disabled and empty.  The test driver is
 * held while a load is to come (td_busy of TDRV.H), so each key of a script
 * that moves the master finds its pane loaded.  F6 or TAB, with or
 * without SHIFT, gives the focus to the other pane, as Next pane does, and
 * ESC on the contents gives it back to the tree; a click on either gives it
 * the focus.  The Record menu, the tool bar, the popup menu, the help
 * topic and the count on the status bar are those of the list with the focus;
 * a record command fetches that list again, and beside the tree the other
 * shows what it holds until its next load, but for the name of a location
 * saved whose contents show, which they take at once, while Backup, Restore,
 * Pack, Reindex, Export CSV, Import CSV and the code lists fetch both, but a
 * pane that follows no row yet.  New location gives the tree
 * the focus first.  ESC on the tree opens the item list.  From TUI_LVL_OPER
 * on, an item of the contents dragged onto a tree row moves there as Move
 * moves it, with that location in place of Move's dialog (Rows dragged of
 * WLIST.H):
 * the cursor IW_R_DRAG and IW_S_DRAG on the help panel while it is dragged,
 * the tree row under the pointer framed; a release elsewhere, ESC or a
 * dialog opening ends it with nothing done, and so does going busy.  A drop
 * on the location where the item is sends nothing more than its ITEM GET.
 * A drag from a row among several chosen carries them all, each moved as Move
 * moves several rows (Several rows, above), the server leaving one already
 * there as it is.
 *
 * The assemblies' pane (wpane_assy of WPANE.H, WINPANE.ASSY) is the selected
 * assembly's parts, a list as the parts' own screen, with its commands,
 * Install part installing in that assembly; ESC there gives the assemblies
 * the focus, and ENTER on an assembly still shows its form, whose Parts
 * button, as Parts of the Record menu, opens the parts as a screen of their
 * own.  Beside any master but the tree a record command in one list fetches
 * the other again too, counted again, as a part taken out may leave no
 * assembly and an assembly moved moves its parts: the pane after the master's
 * command while the master's row is still the one it shows, and the master
 * after its pane's.  The master then keeps the row its pane shows, sought
 * again when rows came or went above it, so that the pane, which keeps the
 * focus, stays that record's; once that row is gone the master stays on the
 * row number it had and takes the focus, its pane following.
 *
 * The loans' pane (wpane_loan of WPANE.H, WINPANE.LOAN), beside each view of
 * the loans and holds, is the history of the selected row's item: its loans
 * and holds, open and ended, a list as the item's own (wloan_item of WLOAN.H)
 * with its commands, INS and CTRL+L, CTRL+R and CTRL+U adding that item's;
 * CTRL+PGDN and CTRL+PGUP stay the master's.  It follows the row's item, read
 * from the row with no request, and is fetched again for each row followed,
 * another of the same item too.
 *
 * The users' pane (wpane_user of WPANE.H, WINPANE.USER) is what the selected
 * user did, the audit log's list (wadm_audit of WADMIN.H) for that user alone,
 * newest first, which has no record commands: its Record menu offers Next
 * pane and Back alone.  A user saved fetches it again, as the save by the
 * user selected adds an entry of that user's.
 *
 * The categories.  Browse by category opens the categories, CAT LIST, which
 * takes no PAGE= or AFTER=, paged by its list itself (wl_unpaged of WLIST.H),
 * so that a move re-reads the whole list, at most LINK_MAXCODES rows: each
 * category's code, name and sort order (wrow_code_cols of WROWS.H), titled
 * ME_CATS (SCRKFORM.H), its count IW_S_NCATS.  Their pane (wpane_cat of
 * WPANE.H, WINPANE.CAT) is the selected category's items, ITEM LIST CAT=
 * through itl_src in its orders, a screen of items with every key and command
 * of those, Next order and the headings among them, INS a new item of that
 * category; an item whose category a save changes leaves it, as it leaves a
 * search's results.  On the categories ENTER, or a double click, opens the
 * selected one's items as a screen of their own, titled ME_FCAT with the
 * category's code and name, the way to them while the pane is hidden; its ESC
 * opens the categories again, that category sought.  CTRL+E, INS and DEL on
 * the categories are the Categories dialog's Edit, New and Delete (wcode_form
 * and wcode_del of WCODE.H), from TUI_LVL_OPER as there: the entry's form
 * with the values of its row as listed, a new entry's, or ME_DELQ's question,
 * the server refusing the delete of a category an item has.  After a save, a
 * delete or a 200 the categories are fetched again and counted, the entry
 * saved sought, as its sort order may have moved it, the names held for the
 * rows of items are loaded again, and the pane follows again.  A command of
 * the pane leaves the categories as they are, as no item changes them; one
 * given on the categories that changes an item, as Items > New item, fetches
 * the pane again.  Once the Categories dialog closes, the categories keep the
 * one they had selected, sought again as rows may have come or gone above it,
 * and take the focus when it is gone; a category's items shown as a screen of
 * their own are titled again, with the name it has then.
 *
 * The preview.  The pane of the item list and of a search's results
 * (wpane_item of WPANE.H, WINPANE.ITEM) is no list but a preview
 * panel (WPREV.H), IW_C_PREVIEW, of the selected item, as WPANE.H fills it:
 * the list as wide as its columns, at most half the frame, without a width in
 * the settings.  It shows the item's location as the item form does, read with
 * LOC GET for an item that has one.  It follows the list as a pane does, on
 * the one row chosen, which add mode may leave off the focus row, and with
 * several chosen on the focus row, saying IW_S_PLOAD with the item's ID while
 * it loads, and it is read again, after the same wait, once a record command
 * has fetched the list again, as a save or a move may have changed the item,
 * and as the list is fetched again by F5, Backup, Restore, Pack, Reindex,
 * Export CSV, Import CSV or a code list.  An item deleted meanwhile shows
 * IW_S_PGONE.  A load that fails, its failure shown as a list's is, or that an
 * Esc typed ahead stops between two of its requests, leaves IW_S_PNONE in the
 * panel, which then shows no row's, so that a key or a click in the list, or
 * the list's next move, reads the row again.  The panel has no commands of its
 * own: the list stays the one whose Record menu, tool bar, popup menu, help
 * topic and count show, and its record keys work in the panel as they do in
 * the list, on the row it follows, the item shown: F6 or TAB gives the panel
 * the focus, which a click does too, UP, DOWN, PGUP, PGDN, HOME and END scroll
 * it, ENTER or a double click shows the item in its form in FORM_VIEW, or
 * loads a panel that says IW_S_PNONE, F5 reads the item again, an Esc typed
 * ahead stopping that too, and ESC, Back on the Record menu there, or F6 or
 * TAB gives the focus back to the list; a press of the right mouse button on
 * the panel gives it the focus, and its release, or SHIFT-F10 in the panel,
 * opens the list's Record menu as a popup there, at the pointer or near the
 * panel's top left corner.  While the list has no row the panel is empty and
 * disabled.  A size of the frame that changes the rows of the list makes the
 * panel anew, read again.
 *
 * The forms are the form dialog of WFORM.H, a record's caption naming it as
 * WSCR.H says: the item form over that of SCRIFORM.H, saved as ITEM with the
 * new ID in ITEM_ID and a record's caption MI_TITLE, read by scr_item_kline,
 * its held units worded by scr_item_loaded, its Parent field holding
 * after an installed item's parent's ID that item's maker and model, as INV's
 * does, read with ASSY PATH after ITEM GET (scr_item_parent), the ID alone
 * when that fails, its Location the form's location field (WFORM.H), blank for
 * no location (MO_NOLOC); the location form as WLOC.H gives it; and the
 * service record, loan, reservation, use and user forms as WSVC.H, WLOAN.H and
 * WUSER.H give them.  The first three show their NOTES in the dialog's memo,
 * loaded with ITEM NOTES, LOC NOTES or MAINT NOTES as a saved record's dialog
 * opens and saved after the record (mio_load and mio_save of MEMOIO.H); a
 * loan's NOTES is a field of the record.  A saved record is read with ITEM
 * GET, LOC GET, MAINT GET or LOAN GET first, from the frame (busy), its
 * failure shown as a list's is; a new item starts with scr_item_preset's
 * values (wsrch_preset's on a search's results, a filter's location with them,
 * and wloc_cpreset's in a location's contents, the location with them), a new
 * location as WLOC.H says, a new service record with its item, a new loan,
 * reservation or use with its item and quantity.  The item a new one is asked
 * for is found by its dialog's OK (WFORM.H's owner's pick list): its ID, read
 * with ITEM GET, else the list of the available items holding the text typed
 * (WPICK.H's wp_list), digits that name no item among them.  Once the dialog
 * ends, or the read failed, the list shown is fetched again (PG_RELOAD), its
 * selection kept on the same row, as INV fetches its list again after the
 * form, but the tree after a save from its start, the location saved selected,
 * and no list after a dialog cancelled having sent nothing, its rows kept;
 * a delete fetches it after ITEM DEL, LOC DEL or MAINT DEL succeeded or gave
 * 200, and after no other failure, the failure shown as a list's is, and the
 * end of a loan or hold, sent from its dialog (WSCR.H), after LOAN RETURN
 * succeeded or gave 200.  A failure whose action leaves (WF_LOGIN, WF_QUIT and
 * WF_LOST of the dialog, RQE_LOGIN to RQE_LOST of a request) logs out or ends
 * the program as the list's does, fetching nothing.  A form that cannot be
 * made shows IW_S_NOFORM in a message box.  When Windows ends while the dialog
 * is open, the frame wipes it (wf_wipe, then form_wipe).  CTRL+E in a form in
 * FORM_VIEW switches it to FORM_EDIT, as INV's Edit does, as WFORM.H says.
 * The item form's record buttons, at every level, open the item's parts,
 * service records, loans and holds, and what is used with it, as INV's Parts,
 * Service and Loans tabs: in FORM_VIEW, or in FORM_EDIT while nothing has
 * changed, else the dialog refuses them (WFORM.H).  Their ESC opens the screen
 * the form was shown on again, a list in the order and on the row it had, and
 * the item form over it, the item read again, in the mode it had then.  From a
 * part's form on an item's parts they go back through those parts first, as
 * INV's form goes back through the parts it showed; the frame keeps IW_NBACK
 * screens to go back to (INVWIN.C), dropping the oldest, and ESC past them
 * opens the item list.
 *
 * The notes.  On the screens of items, an item's parts and the location tree,
 * the status bar's help panel says what a command changed, as INV's hint row
 * does, until the next key or mouse button pressed in the program: an item
 * added or saved in the item form, MI_ADDED or MI_SAVED (SCRIFORM.H), an item
 * deleted, MI_DELETED, and a copy kept whose form was cancelled, or could not
 * be made, MI_COPIED, each with the item's ID; an item moved, installed or
 * taken out by Move, Install in or Take out, or a part installed or taken out
 * on an item's parts, MA_MOVED, MA_INSTALLED or MA_REMOVED (SCRAFORM.H), with
 * the item's ID and where it went; units split off an item by Split units,
 * MI_SPLIT, with the new
 * item's ID; a location added, saved, deleted or moved, MO_ADDED, MO_SAVED,
 * MO_DELETED or MO_MOVED (SCRLFORM.H), with its name; a service record or a
 * user added, saved or deleted, MV_ADDED, MV_SAVED, MV_DELETED (SCRMFORM.H) or
 * MU_ADDED, MU_SAVED, MU_DELETED (SCRZFORM.H), with its ID or name; a loan,
 * reservation or use added, saved or ended, MB_ADDED, MB_SAVED or MB_ENDED
 * plus its kind (SCRNFORM.H), with its ID; a category added, saved or
 * deleted on the categories' list, ME_ADDED, ME_SAVED or ME_DELETED
 * (SCRKFORM.H), with its name.
 *
 * The login dialog, IW_R_LOGIN, comes over the frame once the session
 * is open, and again after Log out; its caption is VER's text.  IDOK
 * (ENTER) sends LOGIN with IW_C_USER and IW_C_PASS, an ES_PASSWORD
 * control whose text is wiped as it is read; while either is empty it
 * only moves the focus there.  IDCANCEL (ESC) ends the program.  The
 * user name holds at most LINK_USERLEN characters (upper case), each
 * password control at most LINK_PWMAX: more typing is cut, with no
 * refusal text.  The user name is first USER= of the settings, then the
 * last one logged in; when there is one, the focus starts on the
 * password.  A failed login shows its text as rqe_class (RQERR.H) says:
 * on a line in IW_C_MSG (a wrong user name or password), or in a
 * message box; one that sends the user back to the login dialog (400,
 * 404) sends HELLO first, and one that ends the session ends the
 * program.  The LINK_MAXFAIL-th failed login shows IW_S_TOOMANY in a
 * message box and ends the program.  A login whose password must be
 * changed goes on to the forced change.
 *
 * The password dialog, IW_R_PASSWD, chosen by Change password, and
 * forced after such a login: IW_C_OLD, IW_C_NEW and IW_C_REPEAT, all
 * ES_PASSWORD.  IDOK sends PASSWD once the local checks pass, and a
 * changed password closes the dialog.  The outcomes SN_PW_NOMATCH to
 * SN_PW_OLDWRONG of sn_passwd show in IW_C_MSG (IW_S_PWNOMATCH to
 * IW_S_PWOLD), the focus on the field to mend; any other failed PASSWD
 * shows as a failed login does.  IDCANCEL closes
 * the dialog.  In the forced change IW_C_MSG first says that the
 * password must be changed, and IDCANCEL reads IW_S_LOGOUT and logs
 * out, back to the login dialog.
 *
 * In every password control BS, DEL and typing over a selection edit
 * the text as usual, and SHIFT-DEL, cut, deletes without the clipboard,
 * while paste, clear and undo (ALT+BS among them) and CTRL+BS do
 * nothing.  Each is wiped as its dialog is destroyed, and when Windows
 * ends while it is open.
 *
 * The About dialog, IW_R_ABOUT, shows VER's text in IW_C_VERSION.
 *
 * Each of these three dialogs that cannot be made shows IW_S_NOFORM in a
 * message box; the login dialog, or the forced change, then ends the
 * program.
 *
 * Help on this screen and F1 show help as WHELP.H does, in Windows Help or
 * in the help window IW_R_HELP, its text in IW_C_HELP, over the active
 * window: the section of the open form dialog's form, else of the screen
 * shown (ws_scr's topic) or of the dialog open, LOGIN for the login
 * dialog, PASSWD for the password dialog, ADMIN for Backup, Restore, Pack
 * and Reindex, SESSIONS for the sessions dialog, EXPORT and IMPORT for the
 * dialogs of Export CSV and Import CSV, CATS and LTYPES for the code
 * lists, and REPORTS, or LABELS for the labels, for those of a report;
 * Getting started shows section START, Keyboard section WH_HELPKEYS.
 *
 * The sessions dialog, IW_R_SESS, holds the column header IW_C_SHDR over
 * the list box IW_C_SROWS, the Refresh button IW_C_REFRESH and Close,
 * IDOK, as WADMIN.H gives them.
 *
 * A code list's dialog, IW_R_CODES, holds the column headings IW_C_CHDR
 * over the list box IW_C_CROWS beside the buttons Edit (IW_C_CEDIT, the
 * default), New (IW_C_CNEW), Delete (IW_C_CDEL) and Close, IDCANCEL, as
 * WCODE.H gives them.
 */
#ifndef INVWIN_H
#define INVWIN_H

/* ---------------------------------------------------------------------
 * Resources: the dialogs, the About command and the controls.
 * --------------------------------------------------------------------- */
#define IW_R_ABOUT      1       /* the About dialog                     */
#define IW_R_LOGIN      2       /* the login dialog                     */
#define IW_R_PASSWD     3       /* the password dialog                  */
#define IW_R_SESS       4       /* the sessions dialog                  */
#define IW_R_HELP       5       /* the help window                      */
#define IW_R_CODES      6       /* a code list (WCODE.H)                */
#define IW_R_DRAG       7       /* the cursor of an item dragged        */

/* The entries of the Help menu: above every command of the menu table,
   whose commands are bytes. */
#define IW_M_ABOUT      256
#define IW_M_HELP       257     /* Help on this screen                  */
#define IW_M_KEYS       258     /* Keyboard                             */
#define IW_M_START      259     /* Getting started                      */
#define IW_M_FIND       260     /* the tool bar's Find button           */
#define IW_M_PANE       261     /* the View menu's Pane beside the list */
#define IW_M_BYCAT      262     /* the Items menu's Browse by category  */
/* The entries of the Record menu: IW_M_KEY plus the record key each
   stands for (INVWIN.C), a virtual key plus IW_M_SHIFT for the key with
   SHIFT, or a WS_K_ command of WSCR.H. */
#define IW_M_KEY        512
#define IW_M_SHIFT      256

/* Controls; the password controls, IW_C_PASS to IW_C_REPEAT, follow
   one another. */
#define IW_C_VERSION    200     /* About: the program and its version   */
#define IW_C_STATUS     201     /* the frame's status bar               */
#define IW_C_USER       202     /* login: the user name                 */
#define IW_C_MSG        203     /* login and password: the outcome      */
#define IW_C_PASS       204     /* login: the password                  */
#define IW_C_OLD        205     /* password: the old one                */
#define IW_C_NEW        206     /* the new one                          */
#define IW_C_REPEAT     207     /* the new one again                    */
#define IW_C_LIST       208     /* the frame's list                     */
#define IW_C_SHDR       209     /* sessions: the column header          */
#define IW_C_SROWS      210     /* their rows                           */
#define IW_C_REFRESH    211     /* the button fetching them again       */
#define IW_C_HELP       212     /* help: the section's text             */
/* A code list's rows, its buttons New, Edit and Delete, which follow
   one another, and its column headings, the last control. */
#define IW_C_CROWS      213
#define IW_C_CNEW       214
#define IW_C_CEDIT      215
#define IW_C_CDEL       216
#define IW_C_CHDR       217
#define IW_C_TOOL       218     /* the frame's tool bar                 */
#define IW_C_FIND       219     /* its Find box                         */
#define IW_C_SPLIT      220     /* the split view's splitter            */
#define IW_C_CLIST      221     /* and its contents                     */
#define IW_C_PREVIEW    222     /* or its preview panel                 */

/* ---------------------------------------------------------------------
 * Strings, formatted by wsprintf: each string's %s before its number.
 * --------------------------------------------------------------------- */
#define IW_S_TITLE      1       /* the frame's title                    */
#define IW_S_USAGE      2       /* the command line                     */
#define IW_S_LOCAL      3       /* status bar: %s the data directory    */
#define IW_S_NET        4       /* status bar: %s the server            */
#define IW_S_COM        5       /* status bar: %s the serial port       */
#define IW_S_NOCALL     6       /* NET: %s the server, %s why           */
#define IW_S_NOCFG      7       /* %s the settings file, %s what its DOS
                                   error means                          */
#define IW_S_NOOPEN     8       /* LOCAL: %s the data directory, %s why,
                                   %d the error                         */
#define IW_S_NOCLOSE    9       /* %d the error of closing the link     */
#define IW_S_REFUSED    10      /* %s the data directory, server or
                                   port, %s the reason HELLO or VER was
                                   refused, %d the error                */
#define IW_S_NOFRAME    11      /* the frame could not be made          */
#define IW_S_NOMSG      12      /* %s the message file, %s why          */
#define IW_S_USER       13      /* status bar: %s the user, %d the level */
#define IW_S_TOOMANY    14      /* the LINK_MAXFAIL-th failed login     */
#define IW_S_NOANSWER   15      /* %s the server, port or mailbox, whose
                                   HELLO or VER got no answer           */
#define IW_S_MHELP      16      /* the Help menu                        */
#define IW_S_MABOUT     17      /* its About entry                      */
#define IW_S_LOGOUT     18      /* IDCANCEL of the forced change        */
/* The local refusals of the password dialog, in the order of
   sn_passwd's outcomes SN_PW_NOMATCH to SN_PW_OLDWRONG (SESS.H); each
   is given LINK_PWMIN and LINK_PWMAX, for two %d. */
#define IW_S_PWNOMATCH  19
#define IW_S_PWINVALID  20
#define IW_S_PWSAME     21
#define IW_S_PWOLD      22
#define IW_S_NOLIST     23      /* %d the error of opening a list
                                   (wl_open)                            */
#define IW_S_LTITLE     24      /* the frame's title with the list's: %s
                                   IW_S_TITLE, %s the list's            */
#define IW_S_NOFORM     25      /* a form could not be made             */
#define IW_S_NOCALLNB   26      /* IW_S_NOCALL, %X the NetBIOS return
                                   code                                 */
#define IW_S_NOPORT     27      /* SERIAL: %s the port, %s why          */
/* The link's reasons a remote link does not open, the why of
   IW_S_NOCALL, IW_S_NOCALLNB, IW_S_NOCALLWS, IW_S_NOPORT and
   IW_S_NOMBOX, in the order of the codes LINK_ENONB to LINK_EMBXARG
   (LINK.H). */
#define IW_S_ENONB      28
#define IW_S_ENAME      29
#define IW_S_ECALL      30
#define IW_S_ENOMEM     31
#define IW_S_EREFUSED   32
#define IW_S_ENOPORT    33
#define IW_S_EBUSY      34
#define IW_S_ESETUP     35
#define IW_S_ENOTCP     36
#define IW_S_ECANCEL    37
#define IW_S_EMBXARG    38
#define IW_S_NBBUSY     39      /* %d the NetBIOS commands a closed link
                                   left with the provider               */
#define IW_S_TCP        40      /* status bar: %s the bridge's address  */
#define IW_S_NOCALLWS   41      /* IW_S_NOCALL, %d the Winsock error    */
#define IW_S_MHELPON    42      /* the Help menu's Help on this screen  */
#define IW_S_MKEYS      43      /* its Keyboard entry                   */
#define IW_S_MBOX       44      /* status bar: %s the mailbox           */
#define IW_S_NOMBOX     45      /* MBOX: %s the mailbox, %s why         */
#define IW_S_NOSTA      46      /* MBOX: %s the mailbox, %s settings    */
#define IW_S_MREC       47      /* the Record menu                      */
/* Its entries, each with its key, if any, after a TAB: the four always there,
   Open, Edit, New and Delete, in this order; then the others. */
#define IW_S_ROPEN      48
#define IW_S_REDIT      49
#define IW_S_RNEW       50
#define IW_S_RDEL       51
#define IW_S_RCOPY      52      /* the item duplicated (CTRL+D)         */
#define IW_S_RMOVE      53      /* the screens' own commands (ws_act)   */
#define IW_S_RINTO      54
#define IW_S_ROUT       55
#define IW_S_RPART      56
#define IW_S_RRET       57
/* An item's parts, service records, loans and holds and what is used
   with it, in the order of WS_K_PARTS to WS_K_USED (WSCR.H). */
#define IW_S_RPARTS     58
#define IW_S_RSVC       59
#define IW_S_RLOANS     60
#define IW_S_RUSED      61
#define IW_S_RBELOW     62      /* a location's items below it too      */
#define IW_S_RNEXT      63      /* the loans and holds' next view       */
#define IW_S_RORDER     64      /* the list's next order                */
#define IW_S_RBACK      65      /* ESC                                  */
#define IW_S_RSPLIT     66      /* the split (ws_act)                   */
#define IW_S_RTOP       67      /* the items not installed              */
#define IW_S_MSTART     68      /* the Help menu's Getting started      */
#define IW_S_CTITLE     69      /* a filtered list's title: %s its own,
                                   %s and %s the filters                */
/* The why of IW_S_NOMBOX for a mailbox directory that is not there, and
   for one that lacks a folder, %s its name. */
#define IW_S_ENODIR     70
#define IW_S_ENOSUB     71
#define IW_S_MOVETO     72      /* the caption of a location's move: %s
                                   its name (WLOC.H)                    */
/* A new loan, reservation and use, in the order of SCR_K_LOAN to
   SCR_K_USE (SCRNFORM.H). */
#define IW_S_RLEND      73
#define IW_S_RRES       74
#define IW_S_RUSE       75
#define IW_S_RITEM      76      /* the row's item                       */
#define IW_S_TOOL       77      /* the tool bar's pieces (WTOOL.H)      */
#define IW_S_MFILE      78      /* the File menu                        */
#define IW_S_MEXIT      79      /* its Exit entry                       */
/* The count of the list shown, %lu its rows, in the noun of its screen:
   each plural, the singular at the next number. */
#define IW_S_NITEMS     80
#define IW_S_NASSY      82
#define IW_S_NPARTS     84
#define IW_S_NLOCS      86
#define IW_S_NSVC       88
#define IW_S_NHOLDS     90
#define IW_S_NUSERS     92
#define IW_S_NAUDIT     94
#define IW_S_RPANE      96      /* the other pane of the split view     */
/* The count of the contents beside the tree: %lu its rows, %s the
   location; the singular at the next number. */
#define IW_S_NHERE      97
#define IW_S_DRAG       99      /* help panel: an item dragged          */
/* The rows chosen in the list shown (Several rows, above), %u them. */
#define IW_S_NSEL       100
/* A dialog's caption asking once for several rows: %s the first row's key,
   %u the other rows. */
#define IW_S_MORE       101
/* The help panel while a new loan, reservation or use asks once for
   several rows, %u them. */
#define IW_S_EACH       102
/* DEL's question for several rows, %u them. */
#define IW_S_DELN       103
/* A command on several rows stopped: by its failure, %s the failure's
   text, %s the row's key, %u the rows made and %u all of them; by an Esc,
   %u and %u the same. */
#define IW_S_STOPPED    104
#define IW_S_HALTED     105
/* The rows chosen while the list shown is in add mode (wl_adding of
   WLIST.H), %u them. */
#define IW_S_NADD       106
/* The preview panel's texts (WPREV.H): while it loads, %s the row's key;
   when the load failed or an Esc stopped it; for a record that is
   gone. */
#define IW_S_PLOAD      107
#define IW_S_PNONE      108
#define IW_S_PGONE      109
/* Every row of the list shown chosen (Several rows, above): the count
   panel, %u them; the help panel while their keys are collected, %u
   those so far; DEL's question, %u them; and the refusal of a list too
   long, %u the most rows (KS_MAXROWS of KEYSET.H). */
#define IW_S_NALL       110
#define IW_S_COLLECT    111
#define IW_S_DELALL     112
#define IW_S_OVER       113
/* The View menu, its Pane beside the list and its Refresh, whose help
   is numbered as the Record entries' is (IW_H_MENU), so above the menu
   messages. */
#define IW_S_MVIEW      200
#define IW_S_VPANE      201
#define IW_S_VREFRESH   202
#define IW_S_RALL       203     /* the Record menu's Select all, its
                                   help numbered so too                 */
/* The count of the categories shown, %lu its rows, never more than
   LINK_MAXCODES, so that it is no wider than the count panel, which is
   as wide as five digits in the nouns above; the singular at the next
   number.  It and Browse by category are numbered in the View menu's
   block: a resource holds its strings in blocks of sixteen numbers, and
   a number in a block of its own adds that block to the program. */
#define IW_S_NCATS      204
/* The Items menu's Browse by category, INVWIN's own, its help numbered
   as the View entries' is. */
#define IW_S_MBYCAT     206
/* A command on several rows (Several rows, above): its progress, IW_G_DOING
   plus its Record entry's string, %u the row and %u the rows, and its note
   once made, IW_G_DONE plus that string, %u the rows; those strings are
   below IW_G_DONE - IW_G_DOING (INVWIN.C). */
#define IW_G_DOING      8192
#define IW_G_DONE       8320

/* The help of a menu entry: IW_H_MENU plus the number of its text, an
   MN_ message (MSG_R_INV to MSG_R_LISTV of MSG.H) or an entry's IW_S_
   string (below and above them), at most IW_H_MAXLEN bytes, the room of
   the status bar's help panel. */
#define IW_H_MENU       4096
#define IW_H_MAXLEN     60

#endif
