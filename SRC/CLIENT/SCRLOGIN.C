/*
 * SCRLOGIN.C - the login screen and the home screen (SCR.H).
 *
 * The login screen opens the session with sn_hello (station from
 * tui_s.station, which INV.C sets), which fetches the program version
 * for its title once per run (it cannot change while INV runs); the
 * version text stays in scr_version for the settings screen.  A link
 * lost before or during the HELLO is opened again once, when INV can
 * (relink: a NET link), and the HELLO sent again; a HELLO that
 * still fails is told in tui_fatal's box, which ends the program with
 * SCR_EXIT_OPEN, or TUI_EXIT_LOST for a lost link.  The user name is
 * typed upper case, offered from USER= of the settings; the password is
 * typed without echo, and sn_login wipes it.
 *
 * sn_login's outcomes: SN_IN fills tui_s and goes home; SN_MUSTCHG runs
 * the forced password change (scr_passwd with SCMD_MUSTCHG) before
 * anything else, with the full name left empty; SN_REFUSED shows its
 * message, waits TMR_SEC ticks with the screen unchanged and asks for
 * the password again; SN_TOOMANY shows MG_TOOMANY in a message box and,
 * after any key or the end of a key script, ends the program with
 * SCR_EXIT_LOGIN through tui_fatal; SN_ENDED on a lost link sends the
 * HELLO again as above and asks for the password again.  A log-out from
 * the forced change, and a failure that asks for the login (the server
 * ended the session: IDLE=, a restart), bring the login screen back with
 * a new HELLO, the user name kept and the cursor on the password.
 */
#define RQERR_CLASS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "SESS.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"

#define MG_USER         400     /* label of the user name               */
#define MG_PASS         401     /* label of the password                */
#define MG_DATA         402     /* the data directory: ^1               */
#define MG_HINT         403     /* hints of the login screen            */
#define MG_TOOMANY      404     /* the third failed login               */
#define MG_HOMEHINT     405     /* hints of the home screen             */
#define MG_HUSER        407     /* user: ^1 the full name or user name  */
#define MG_HLEVEL       408     /* level: ^1 digit, ^2 its name         */
#define MG_LVL1         409     /* names of the levels 1..3             */
#define MG_LVL2         410
#define MG_LVL3         411
#define MG_HITEMS       412     /* record counts: ^1, in SN_C_ order    */
#define MG_HLOCS        413
#define MG_HCATS        414

/* scr_home draws the counts by MG_HITEMS + i (the build fails
   otherwise). */
extern char mg_counts_fit[1];
extern char mg_counts_fit[(MG_HLOCS == MG_HITEMS + 1 &&
                           MG_HCATS == MG_HITEMS + 2) ? 1 : 2];
#define MG_HMENU        415     /* how to reach the menus and help      */
#define MG_HDATE        416     /* today: ^1 YYYY-MM-DD                 */
#define MG_HEMPTY       417     /* how to add the first item            */
#define MG_HOLDDATE     418     /* the date is before ^1, HM_MINYEAR    */
#define MG_RELINKED     419     /* the lost link opened again           */
#define MG_GARBLED      420     /* HELLO garbled twice: check the line  */

/* tui_s.level takes sn_login's level, and scr_home names it by MG_LVL1
   + level - 1 (the build fails unless the ranges agree). */
extern char mg_levels_fit[1];
extern char mg_levels_fit[(SN_LVLMIN == TUI_LVL_READ &&
                           SN_LVLMAX == TUI_LVL_ADMIN &&
                           MG_LVL3 - MG_LVL1 == SN_LVLMAX - SN_LVLMIN) ?
                          1 : 2];

/* The login box. */
#define LG_TOP          8
#define LG_LEFT         10
#define LG_H            7
#define LG_W            67
#define LG_LCOL         (LG_LEFT + 4)
#define LG_VCOL         (LG_LCOL + 19)

/* The home box; a date before the year HM_MINYEAR is surely a PC
   clock never set (one without a clock starts at 1980-01-01). */
#define HM_TOP          5
#define HM_LEFT         10
#define HM_H            14
#define HM_W            60
#define HM_COL          (HM_LEFT + 4)
#define HM_MINYEAR      "1990"

char scr_version[RQ_TEXTLEN + 1];

/* The password being typed: static, so that scr_wipe can reach it when
   the program ends while it is typed. */
static char lg_pw[SN_PWSZ];

/* The user name being typed, and 1 when the next login screen takes it
   up again with the cursor on the password: after a log-out from the
   forced password change. */
static char lg_user[LINK_USERLEN + 1];
static u8 lg_keep;

/* The login box; a message still up (a failure, MT_NOMEM of a forced
   password change, MG_RELINKED) stays until the next key. */
static void draw_login(void)
{
    tui_box(LG_TOP, LG_LEFT, LG_H, LG_W, TB_SINGLE, scr_version);
    tui_text(LG_TOP + 2, LG_LCOL, msg(MG_USER), TA_BOLD);
    tui_text(LG_TOP + 3, LG_LCOL, msg(MG_PASS), TA_BOLD);
    tui_textn(LG_TOP + 5, LG_LCOL, msg_sub(MG_DATA, scr_datadir, NULL),
              LG_LEFT + LG_W - 2 - LG_LCOL, TA_NORMAL);
    tui_hint_keep(msg(MG_HINT));
}

/* For a lost NET link, a new call (scr_reopen) as tui_s.l, and its
   station named again (sn_station) on the status line, as a NetBIOS
   name made up from the timer differs from call to call: 1 when it is
   open.  0 for another link, left as it was, or when the call failed,
   tui_s.l then NULL. */
static int relink(void)
{
    if (tui_s.conn != TUI_CONN_NET)
        return 0;
    tui_s.l = scr_reopen();
    if (tui_s.l == NULL)
        return 0;
    sn_station(tui_s.station, link_netname(tui_s.l), scr_cfg.station);
    tui_frame();
    return 1;
}

/* HELLO, on a link opened again (relink, MG_RELINKED) when it was lost;
   a failure, told as tui_fail tells it, ends the program with
   SCR_EXIT_OPEN, or TUI_EXIT_LOST for a lost link.  A HELLO still
   refused as unknown after sn_hello sent it again came garbled twice,
   which a bad cable or unequal speeds at the two ends do: MG_GARBLED
   says so in place of the internal error. */
static void hello(void)
{
    rq_res res;
    rqe e;
    int rc;

    rc = sn_hello(tui_s.l, tui_s.station, scr_version, &res);
    if (rc == RQ_ELOST && relink()) {
        tui_msg(msg(MG_RELINKED), 0);
        rc = sn_hello(tui_s.l, tui_s.station, scr_version, &res);
    }
    if (rc == RQ_OK)
        return;
    rqe_class(&e, &res, 0);
    tui_fatal(rc == RQ_EUNKNOWN ? msg(MG_GARBLED) : e.text,
              e.act == RQE_LOST ? TUI_EXIT_LOST : SCR_EXIT_OPEN);
}

void scr_wipe(void)
{
    txt_wipe(lg_pw, (u16)sizeof lg_pw);
    form_wipe();
}

u8 scr_login(u8 cmd)
{
    rq_res res;
    int field;
    int st;
    u8 level;
    u8 r;
    u16 k;

    if (cmd != TUI_CMD_LOGIN)
        return cmd;
    field = lg_keep;
    lg_keep = 0;
    tui_topic("LOGIN");
    hello();
    if (!field)
        strcpy(lg_user, scr_cfg.user);
    draw_login();
    for (;;) {
        tui_textn(LG_TOP + 2, LG_VCOL, lg_user, LINK_USERLEN, TA_INPUT);
        tui_textn(LG_TOP + 3, LG_VCOL, "", LINK_PWMAX, TA_INPUT);
        if (field == 0) {
            k = tui_input(LG_TOP + 2, LG_VCOL, LINK_USERLEN, lg_user,
                          LINK_USERLEN, TI_UPPER);
            if (k == KEY_ESC || k == KEY_F3)
                return TUI_CMD_QUIT;
            if (k == KEY_ENTER || k == KEY_TAB || k == KEY_DOWN)
                field = lg_user[0] != '\0';
            continue;
        }
        lg_pw[0] = '\0';
        /* A stray Ctrl key keeps the password typed so far. */
        do
            k = tui_input(LG_TOP + 3, LG_VCOL, LINK_PWMAX, lg_pw,
                          LINK_PWMAX, TI_NOECHO | TI_PRINT);
        while (k < 0x20 && k != KEY_ENTER && k != KEY_TAB && k != KEY_ESC);
        if (k != KEY_ENTER && k != KEY_TAB) {
            txt_wipe(lg_pw, (u16)sizeof lg_pw);
            if (k == KEY_ESC || k == KEY_F3)
                return TUI_CMD_QUIT;
            if (k == KEY_UP || k == KEY_STAB)
                field = 0;
            continue;
        }
        st = sn_login(tui_s.l, lg_user, lg_pw, &level, tui_s.name, &res);
        if (st == SN_TOOMANY)
            tui_fatal(msg(MG_TOOMANY), SCR_EXIT_LOGIN);
        if (st == SN_ENDED && res.code == RQ_ELOST) {
            hello();
            continue;
        }
        if (st == SN_ENDED) {
            tui_fail(&res);
            return TUI_CMD_QUIT;
        }
        if (st == SN_REFUSED) {
            tui_fail(&res);
            vid_flush();
            tmr_delay(TMR_SEC);
            continue;
        }
        r = TUI_CMD_NONE;
        if (st == SN_FAILED) {
            r = tui_fail(&res);
            if (r == TUI_CMD_NONE)
                continue;
        } else {
            strcpy(tui_s.user, lg_user);
            if (st == SN_MUSTCHG) {
                tui_clear(TUI_TOP, TUI_BOTTOM);
                r = scr_passwd(SCMD_MUSTCHG);
            }
            if (r == TUI_CMD_NONE) {
                tui_s.level = level;
                return TUI_CMD_NONE;
            }
        }
        if (r != TUI_CMD_LOGIN)
            return TUI_CMD_QUIT;
        lg_keep = 1;
        return TUI_CMD_LOGIN;
    }
}

/* ---------------------------------------------------------------------
 * The home screen.
 * --------------------------------------------------------------------- */

u8 scr_home(u8 cmd)
{
    sn_counts c;
    rq_res res;
    const char *who;
    char today[SCR_DATESZ];
    char shown[MSG_DATEW + 1];
    char lvl[2];
    u16 lvname;
    int used;
    int i;
    u8 r;
    u16 k;

    if (cmd != TUI_CMD_NONE)
        return cmd;
    tui_topic("HOME");
    if (sn_status(tui_s.l, &c, &res) != RQ_OK) {
        r = tui_fail(&res);
        if (r != TUI_CMD_NONE)
            return r;
    }
    tui_level_text(lvl);
    tui_box(HM_TOP, HM_LEFT, HM_H, HM_W, TB_SINGLE, msg(MT_PROG));
    who = tui_s.name[0] != '\0' ? tui_s.name : tui_s.user;
    lvname = (u16)(MG_LVL1 + (tui_s.level > 0 ? tui_s.level - 1 : 0));
    tui_text(HM_TOP + 2, HM_COL, msg_sub(MG_HUSER, who, NULL), TA_BOLD);
    tui_text(HM_TOP + 3, HM_COL, msg_sub(MG_HLEVEL, lvl, msg(lvname)),
             TA_NORMAL);
    scr_date(today);
    msg_date(shown, today);
    tui_text(HM_TOP + 4, HM_COL, msg_sub(MG_HDATE, shown, NULL), TA_NORMAL);
    for (i = 0; i < SN_NCOUNTS; i++)
        tui_text((u8)(HM_TOP + 6 + i), HM_COL,
                 msg_sub((u16)(MG_HITEMS + i), c.n[i], NULL), TA_NORMAL);
    tui_text(HM_TOP + 10, HM_COL, msg(MG_HMENU), TA_NORMAL);
    if (strcmp(c.n[SN_C_ITEMS], "0") == 0)
        tui_text(HM_TOP + 11, HM_COL, msg(MG_HEMPTY), TA_NORMAL);
    if (memcmp(today, HM_MINYEAR, sizeof HM_MINYEAR - 1) < 0)
        tui_text(HM_TOP + 12, HM_COL, msg_sub(MG_HOLDDATE, HM_MINYEAR, NULL),
                 TA_ERROR);
    tui_hint_keep(msg(MG_HOMEHINT));
    for (;;) {
        k = tui_key();
        r = tui_global(k, &used);
        if (used)
            return r;
    }
}
