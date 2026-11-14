/*
 * T_MENU.C - the menu bar of the client as constant data
 * (SRC/CLIENT/INVMENU.C), checked against what INVMENU.H promises, with
 * the texts of the built INV.MSG.
 *
 * usage: T_MENU dir [msgdir]
 *
 * dir is unused (the runners pass a scratch directory); msgdir is the
 * directory prefix of the built INV.MSG (default: the program's own
 * directory).
 *
 * Covered:
 * - INVMENU_COUNT menus in the order of the bar, each with the entries,
 *   labels, commands and levels the MN_ constants of INVMENU.H name;
 * - no command twice, SCMD_ITEMS and SCMD_MUSTCHG in none;
 * - one '&' and a letter after it per text, no "&&", no blank at either
 *   end;
 * - the hot letters of the titles, and of the entries of each menu, all
 *   different;
 * - the key names (one word) at the end of the texts of MN_FIND and MN_QUIT
 *   alone, after two or more blanks, with no other double blank.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "SCR.H"
#include "INVMENU.H"
#include "TESTFW.H"

#define PATHMAX         80

/* The entries as INVMENU.H gives them, menu after menu in the order of
   the table. */
typedef struct {
    u16 label;
    u8  cmd;
    u8  level;
} want_item;

static const want_item g_all[] = {
    { MN_BYID, SCMD_ITEMS_ID, TUI_LVL_READ },
    { MN_BYLOC, SCMD_ITEMS_LOC, TUI_LVL_READ },
    { MN_BYMFR, SCMD_ITEMS_MFR, TUI_LVL_READ },
    { MN_BYSER, SCMD_ITEMS_SER, TUI_LVL_READ },
    { MN_NEWITEM, SCMD_ITEM_NEW, TUI_LVL_OPER },
    { MN_ASSY, SCMD_ASSY, TUI_LVL_READ },
    { MN_MAINT, SCMD_MAINT, TUI_LVL_READ },
    { MN_LOANS, SCMD_LOANS, TUI_LVL_READ },
    { MN_CATS, SCMD_CATS, TUI_LVL_READ },
    { MN_LOCS, SCMD_LOCS, TUI_LVL_READ },
    { MN_NEWLOC, SCMD_LOC_NEW, TUI_LVL_OPER },
    { MN_LTYPES, SCMD_LTYPES, TUI_LVL_READ },
    { MN_FIND, SCMD_FIND, TUI_LVL_READ },
    { MN_SERIAL, SCMD_SERIAL, TUI_LVL_READ },
    { MN_FILTER, SCMD_FILTER, TUI_LVL_READ },
    { MN_RLIST, SCMD_RPT_LIST, TUI_LVL_READ },
    { MN_RVALUE, SCMD_RPT_VALUE, TUI_LVL_READ },
    { MN_RLOANS, SCMD_RPT_LOANS, TUI_LVL_READ },
    { MN_RLABEL, SCMD_LBL_ITEM, TUI_LVL_READ },
    { MN_RBIN, SCMD_LBL_BIN, TUI_LVL_READ },
    { MN_USERS, SCMD_USERS, TUI_LVL_ADMIN },
    { MN_BACKUP, SCMD_BACKUP, TUI_LVL_ADMIN },
    { MN_RESTORE, SCMD_RESTORE, TUI_LVL_ADMIN },
    { MN_PACK, SCMD_PACK, TUI_LVL_ADMIN },
    { MN_REINDEX, SCMD_REINDEX, TUI_LVL_ADMIN },
    { MN_AUDIT, SCMD_AUDIT, TUI_LVL_ADMIN },
    { MN_SESSIONS, SCMD_SESSIONS, TUI_LVL_ADMIN },
    { MN_EXPORT, SCMD_EXPORT, TUI_LVL_ADMIN },
    { MN_IMPORT, SCMD_IMPORT, TUI_LVL_ADMIN },
    { MN_SETTINGS, SCMD_SETTINGS, TUI_LVL_READ },
    { MN_PASSWD, SCMD_PASSWD, TUI_LVL_READ },
    { MN_LOGOUT, TUI_CMD_LOGIN, TUI_LVL_READ },
    { MN_QUIT, TUI_CMD_QUIT, TUI_LVL_READ }
};
static const u16 g_titles[INVMENU_COUNT] = {
    MN_MITEMS, MN_MLOCS, MN_MSRCH, MN_MRPT, MN_MADMIN, MN_MOPT
};
static const u8 g_nwant[INVMENU_COUNT] = { 9, 3, 3, 5, 9, 4 };

static char g_txt[MSG_MAXLEN + 1];      /* a copy of the text under test */

/* Copy text n into g_txt; 0 when it is unknown (msg gives "?nnn"). */
static int load_text(u16 n)
{
    char unk[8];

    strcpy(g_txt, msg(n));
    sprintf(unk, "?%u", (unsigned)n);
    return TF_CHECK(strcmp(g_txt, unk) != 0);
}

/* The number of '&' in s, and 1 when two stand together. */
static u16 count_amp(const char *s, int *pair)
{
    u16 n;

    n = 0;
    *pair = 0;
    for (; *s; s++)
        if (*s == '&') {
            n++;
            if (s[1] == '&')
                *pair = 1;
        }
    return n;
}

/* The hot letter of g_txt, upper case, after checking that it has one
   '&' followed by a letter; 0 when it fails. */
static int hot_letter(void)
{
    const char *p;
    int pair;
    int c;

    if (!TF_EQ(count_amp(g_txt, &pair), 1) || !TF_EQ(pair, 0))
        return 0;
    p = strchr(g_txt, '&');
    c = (unsigned char)p[1];
    if (!TF_CHECK(TXT_ISALPHA(c)))
        return 0;
    return TXT_UPPER(c);
}

/* Where the key name of g_txt starts, or NULL when no two blanks stand
   together.  The name is the text before the first two blanks. */
static const char *key_of(void)
{
    const char *p;

    p = strstr(g_txt, "  ");
    if (p == NULL)
        return NULL;
    while (*p == ' ')
        p++;
    return p;
}

static void test_shape(void)
{
    u16 m;
    u16 i;
    u16 at;

    at = 0;
    for (m = 0; m < INVMENU_COUNT; m++) {
        TF_EQ(invmenu_bar[m].label, g_titles[m]);
        TF_EQ(invmenu_bar[m].nitems, g_nwant[m]);
        if (invmenu_bar[m].nitems != g_nwant[m])
            return;
        for (i = 0; i < g_nwant[m]; i++, at++) {
            TF_EQ(invmenu_bar[m].items[i].label, g_all[at].label);
            TF_EQ(invmenu_bar[m].items[i].cmd, g_all[at].cmd);
            TF_EQ(invmenu_bar[m].items[i].level, g_all[at].level);
        }
    }
}

static void test_hot(void)
{
    int seen[256];
    u16 m;
    u16 i;
    int c;

    memset(seen, 0, sizeof seen);
    for (m = 0; m < INVMENU_COUNT; m++) {
        if (!load_text(invmenu_bar[m].label))
            continue;
        c = hot_letter();
        if (c) {
            TF_EQ(seen[c], 0);
            seen[c] = 1;
        }
    }
    for (m = 0; m < INVMENU_COUNT; m++) {
        memset(seen, 0, sizeof seen);
        for (i = 0; i < invmenu_bar[m].nitems; i++) {
            if (!load_text(invmenu_bar[m].items[i].label))
                continue;
            c = hot_letter();
            if (c) {
                TF_EQ(seen[c], 0);
                seen[c] = 1;
            }
        }
    }
}

/* No command is in two entries, and SCMD_ITEMS and SCMD_MUSTCHG are in
   none. */
static void test_cmds(void)
{
    u16 m;
    u16 i;
    u16 m2;
    u16 i2;
    u8 c;

    for (m = 0; m < INVMENU_COUNT; m++)
        for (i = 0; i < invmenu_bar[m].nitems; i++) {
            c = invmenu_bar[m].items[i].cmd;
            TF_CHECK(c != SCMD_ITEMS);
            TF_CHECK(c != SCMD_MUSTCHG);
            for (m2 = m; m2 < INVMENU_COUNT; m2++)
                for (i2 = (u16)(m2 == m ? i + 1 : 0);
                     i2 < invmenu_bar[m2].nitems; i2++)
                    TF_CHECK(invmenu_bar[m2].items[i2].cmd != c);
        }
}

/* g_txt holds a key name key after its entry name (non-empty, no blank
   in front of the two) and no hot letter in the key. */
static void check_keyed(const char *key, const char *want)
{
    const char *two;

    two = strstr(g_txt, "  ");
    TF_CHECK(two != NULL && two > g_txt && two[-1] != ' ');
    TF_STREQ(key, want);
    TF_CHECK(strchr(key, '&') == NULL);
    TF_CHECK(strchr(key, ' ') == NULL);
}

/* Neither edge of g_txt is blank. */
static void check_edges(void)
{
    TF_CHECK(g_txt[0] != ' ');
    TF_CHECK(g_txt[0] == '\0' || g_txt[strlen(g_txt) - 1] != ' ');
}

static void test_keys(void)
{
    u16 m;
    u16 i;
    u16 label;
    const char *key;

    for (m = 0; m < INVMENU_COUNT; m++) {
        if (load_text(invmenu_bar[m].label)) {
            TF_CHECK(key_of() == NULL);
            check_edges();
        }
        for (i = 0; i < invmenu_bar[m].nitems; i++) {
            label = invmenu_bar[m].items[i].label;
            if (!load_text(label))
                continue;
            key = key_of();
            if (label == MN_FIND)
                check_keyed(key != NULL ? key : "", "Ctrl-F");
            else if (label == MN_QUIT)
                check_keyed(key != NULL ? key : "", "F3");
            else
                TF_CHECK(key == NULL);
            check_edges();
        }
    }
}

int main(int argc, char **argv)
{
    char mdir[PATHMAX];

    tf_begin("T_MENU");
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_MENU: %s\n", msg_why());
        return tf_done();
    }
    test_shape();
    test_hot();
    test_cmds();
    test_keys();
    msg_free();
    return tf_done();
}
