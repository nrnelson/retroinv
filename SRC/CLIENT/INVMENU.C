/*
 * INVMENU.C - the menu bar of the RETROINV client (INVMENU.H).
 *
 * Constant tables only: the entries of each menu, then the bar over
 * them.
 */
#include "COMPAT.H"
#include "TUI.H"
#include "SCR.H"
#include "INVMENU.H"

#define NITEMS(a)       ((u8)(sizeof a / sizeof a[0]))

static const tui_item mi_items[] = {
    { MN_BYID, TUI_LVL_READ, SCMD_ITEMS_ID },
    { MN_BYLOC, TUI_LVL_READ, SCMD_ITEMS_LOC },
    { MN_BYMFR, TUI_LVL_READ, SCMD_ITEMS_MFR },
    { MN_BYSER, TUI_LVL_READ, SCMD_ITEMS_SER },
    { MN_NEWITEM, TUI_LVL_OPER, SCMD_ITEM_NEW },
    { MN_ASSY, TUI_LVL_READ, SCMD_ASSY },
    { MN_MAINT, TUI_LVL_READ, SCMD_MAINT },
    { MN_LOANS, TUI_LVL_READ, SCMD_LOANS },
    { MN_CATS, TUI_LVL_READ, SCMD_CATS }
};
static const tui_item mi_locs[] = {
    { MN_LOCS, TUI_LVL_READ, SCMD_LOCS },
    { MN_NEWLOC, TUI_LVL_OPER, SCMD_LOC_NEW },
    { MN_LTYPES, TUI_LVL_READ, SCMD_LTYPES }
};
static const tui_item mi_srch[] = {
    { MN_FIND, TUI_LVL_READ, SCMD_FIND },
    { MN_SERIAL, TUI_LVL_READ, SCMD_SERIAL },
    { MN_FILTER, TUI_LVL_READ, SCMD_FILTER }
};
static const tui_item mi_rpt[] = {
    { MN_RLIST, TUI_LVL_READ, SCMD_RPT_LIST },
    { MN_RVALUE, TUI_LVL_READ, SCMD_RPT_VALUE },
    { MN_RLOANS, TUI_LVL_READ, SCMD_RPT_LOANS },
    { MN_RLABEL, TUI_LVL_READ, SCMD_LBL_ITEM },
    { MN_RBIN, TUI_LVL_READ, SCMD_LBL_BIN }
};
static const tui_item mi_admin[] = {
    { MN_USERS, TUI_LVL_ADMIN, SCMD_USERS },
    { MN_BACKUP, TUI_LVL_ADMIN, SCMD_BACKUP },
    { MN_RESTORE, TUI_LVL_ADMIN, SCMD_RESTORE },
    { MN_PACK, TUI_LVL_ADMIN, SCMD_PACK },
    { MN_REINDEX, TUI_LVL_ADMIN, SCMD_REINDEX },
    { MN_AUDIT, TUI_LVL_ADMIN, SCMD_AUDIT },
    { MN_SESSIONS, TUI_LVL_ADMIN, SCMD_SESSIONS },
    { MN_EXPORT, TUI_LVL_ADMIN, SCMD_EXPORT },
    { MN_IMPORT, TUI_LVL_ADMIN, SCMD_IMPORT }
};
static const tui_item mi_opt[] = {
    { MN_SETTINGS, TUI_LVL_READ, SCMD_SETTINGS },
    { MN_PASSWD, TUI_LVL_READ, SCMD_PASSWD },
    { MN_LOGOUT, TUI_LVL_READ, TUI_CMD_LOGIN },
    { MN_QUIT, TUI_LVL_READ, TUI_CMD_QUIT }
};

const tui_menu invmenu_bar[] = {
    { MN_MITEMS, NITEMS(mi_items), mi_items },
    { MN_MLOCS, NITEMS(mi_locs), mi_locs },
    { MN_MSRCH, NITEMS(mi_srch), mi_srch },
    { MN_MRPT, NITEMS(mi_rpt), mi_rpt },
    { MN_MADMIN, NITEMS(mi_admin), mi_admin },
    { MN_MOPT, NITEMS(mi_opt), mi_opt }
};

/* The bar holds INVMENU_COUNT menus. */
extern char invmenu_count_ok[1];
extern char invmenu_count_ok[(NITEMS(invmenu_bar) == INVMENU_COUNT) ? 1 : 2];
