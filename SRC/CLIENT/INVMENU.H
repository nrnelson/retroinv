/*
 * INVMENU.H - the menu bar of the RETROINV client as constant data
 * (INVMENU.C): its menus, their entries, and the access level and
 * command of each.
 *
 * The table holds message numbers (MSG.H), levels and commands, and no
 * code.  The full-screen client hands it to its main loop (tui_app of
 * TUI.H, whose tui_menu and tui_item say what each field means), which
 * draws it as the menu bar and its pull-downs; a client with menus of
 * its own, such as a Windows menu bar built with AppendMenu, appends
 * the same entries in the same order and refuses or greys an entry by
 * the same level.  DOC/CLIENT.md lists the menus, their entries and the
 * level of each.  The table is constant near data, as TUI.C reads it
 * through near pointers.
 *
 * What a reader may rely on:
 *   - invmenu_bar holds INVMENU_COUNT menus in the order of the bar,
 *     each with at least one entry.  There are no separators: every
 *     entry gives a command.
 *   - Every title and entry text has one hot-key letter (A-Z or a-z),
 *     marked by '&' as tui_item says, and no "&&".  The hot letters of
 *     the titles differ from one another, as do those of the entries of
 *     each menu, compared without regard to case.  No text starts or
 *     ends with a blank.
 *   - An entry whose command also has a key of its own outside the
 *     menus names that key at the end of its text, in one word after two
 *     or more blanks: MN_FIND (Ctrl-F, which INV gives SCMD_FIND) and
 *     MN_QUIT (F3, which asks for TUI_CMD_QUIT).  The full-screen
 *     client draws the text as it stands, the blanks lining the key
 *     names up; a client that shows key names in a column of their own
 *     takes the entry's name to be the text before the first two blanks
 *     in a row, and the key's name the text after the blanks that
 *     follow.  No other text holds two blanks in a row.
 *   - level is the least access level that may choose the entry:
 *     TUI_LVL_READ, TUI_LVL_OPER or TUI_LVL_ADMIN.  A client only dims
 *     and refuses an entry by it; the server checks the level of every
 *     request regardless.
 *   - cmd is an SCMD_ command of SCR.H, TUI_CMD_LOGIN or TUI_CMD_QUIT.
 *     No command is in two entries, and SCMD_ITEMS and SCMD_MUSTCHG are
 *     in none.
 *
 * The messages of the table (INVMENU.MSG) are named here, so that a test
 * or another client can name an entry: the titles in the order of the
 * bar, then the entries of each menu in their order, each entry's define
 * with its command and level as the table gives them.
 */
#ifndef INVMENU_H
#define INVMENU_H

#include "COMPAT.H"
#include "TUI.H"

/* ---------------------------------------------------------------------
 * Messages.
 * --------------------------------------------------------------------- */

/* The titles of the bar. */
#define MN_MITEMS       100
#define MN_MLOCS        101
#define MN_MSRCH        102
#define MN_MRPT         103
#define MN_MADMIN       104
#define MN_MOPT         105

/* Items. */
#define MN_BYID         110     /* SCMD_ITEMS_ID, TUI_LVL_READ          */
#define MN_BYLOC        111     /* SCMD_ITEMS_LOC, TUI_LVL_READ         */
#define MN_BYMFR        112     /* SCMD_ITEMS_MFR, TUI_LVL_READ         */
#define MN_BYSER        113     /* SCMD_ITEMS_SER, TUI_LVL_READ         */
#define MN_NEWITEM      114     /* SCMD_ITEM_NEW, TUI_LVL_OPER          */
#define MN_ASSY         115     /* SCMD_ASSY, TUI_LVL_READ              */
#define MN_MAINT        116     /* SCMD_MAINT, TUI_LVL_READ             */
#define MN_LOANS        117     /* SCMD_LOANS, TUI_LVL_READ             */
#define MN_CATS         118     /* SCMD_CATS, TUI_LVL_READ              */

/* Locations. */
#define MN_LOCS         120     /* SCMD_LOCS, TUI_LVL_READ              */
#define MN_NEWLOC       121     /* SCMD_LOC_NEW, TUI_LVL_OPER           */
#define MN_LTYPES       122     /* SCMD_LTYPES, TUI_LVL_READ            */

/* Search. */
#define MN_FIND         130     /* SCMD_FIND, TUI_LVL_READ              */
#define MN_SERIAL       131     /* SCMD_SERIAL, TUI_LVL_READ            */
#define MN_FILTER       132     /* SCMD_FILTER, TUI_LVL_READ            */

/* Reports. */
#define MN_RLIST        140     /* SCMD_RPT_LIST, TUI_LVL_READ          */
#define MN_RVALUE       141     /* SCMD_RPT_VALUE, TUI_LVL_READ         */
#define MN_RLOANS       142     /* SCMD_RPT_LOANS, TUI_LVL_READ         */
#define MN_RLABEL       143     /* SCMD_LBL_ITEM, TUI_LVL_READ          */
#define MN_RBIN         144     /* SCMD_LBL_BIN, TUI_LVL_READ           */

/* Admin. */
#define MN_USERS        150     /* SCMD_USERS, TUI_LVL_ADMIN            */
#define MN_BACKUP       151     /* SCMD_BACKUP, TUI_LVL_ADMIN           */
#define MN_RESTORE      152     /* SCMD_RESTORE, TUI_LVL_ADMIN          */
#define MN_PACK         153     /* SCMD_PACK, TUI_LVL_ADMIN             */
#define MN_REINDEX      157     /* SCMD_REINDEX, TUI_LVL_ADMIN          */
#define MN_AUDIT        154     /* SCMD_AUDIT, TUI_LVL_ADMIN            */
#define MN_SESSIONS     158     /* SCMD_SESSIONS, TUI_LVL_ADMIN         */
#define MN_EXPORT       155     /* SCMD_EXPORT, TUI_LVL_ADMIN           */
#define MN_IMPORT       156     /* SCMD_IMPORT, TUI_LVL_ADMIN           */

/* Options. */
#define MN_SETTINGS     160     /* SCMD_SETTINGS, TUI_LVL_READ          */
#define MN_PASSWD       161     /* SCMD_PASSWD, TUI_LVL_READ            */
#define MN_LOGOUT       162     /* TUI_CMD_LOGIN, TUI_LVL_READ          */
#define MN_QUIT         163     /* TUI_CMD_QUIT, TUI_LVL_READ           */

/* ---------------------------------------------------------------------
 * The table.
 * --------------------------------------------------------------------- */
#define INVMENU_COUNT   6       /* menus of the bar                     */

extern const tui_menu invmenu_bar[];

#endif
