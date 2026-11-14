/*
 * SCRSET.C - the settings screen (SCR.H).
 *
 * A form of local fields over the settings in scr_cfg, plus read-only
 * lines: the adapter and how the screen is written, the settings file,
 * the program version (the VER text the login screen kept) and the DOS
 * version.  No request is sent.  Enter first checks the printer numbers
 * against their ranges (RPT.H: page length, label width and label
 * pitch), putting the cursor on the first one outside its range, then
 * writes the settings file with cfg_save and, once that succeeded,
 * applies snow avoidance at once, the palette from the next screen
 * drawn and the printer settings from the next report; the data
 * directory, the station and BIOS output take effect at the next
 * start, as the message says.  A write failure shows its DOS error and
 * keeps the form as typed.  Every level may use it: the file is the
 * user's own.  A data directory the file leaves out shows as cfg_defdir,
 * and stays left out unless the field is changed.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "CFG.H"
#include "PRINT.H"
#include "RPT.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRSET.H"

#define MC_SNOW         685     /* field labels beside SCRSET.H's       */
#define MC_MONO         686
#define MC_BIOS         687
#define MC_ADAPTER      688
#define MC_DOS          691
#define MC_HINT         692     /* hints                                */
#define MC_MDA          693     /* adapter names                        */
#define MC_CGA          694
#define MC_EGA          695
#define MC_DIRECT       696     /* ^1 the adapter, written directly     */
#define MC_VIABIOS      697     /* ^1 the adapter, written by the BIOS  */
#define MC_SAVED        698     /* the file was written                 */
#define MC_PRINTER      700     /* printer row labels                   */
#define MC_PRTYPE       701
#define MC_PAGELEN      702
#define MC_LABELW       703
#define MC_LABELH       704     /* between label width and pitch        */
#define MC_RANGE        705     /* ^1 lowest, ^2 highest value          */

#define SET_H           10      /* rows of the form's box               */
#define SET_VCOL        18      /* value column of the left-hand fields */
#define SET_COL2        45      /* label and value columns of the       */
#define SET_VCOL2       60      /* settings file and the DOS version    */
#define SET_MONOC       28      /* label and value columns of the other */
#define SET_MONOV       40      /* fields of a row, in pairs            */
#define SET_BIOSC       50
#define SET_BIOSV       63
#define SET_TYPEC       25
#define SET_TYPEV       30
#define SET_PAGEC       37
#define SET_PAGEV       48
#define SET_LBLWC       53
#define SET_LBLWV       59
#define SET_LBLHC       62
#define SET_LBLHV       64
#define SET_DIRDISP     40      /* cells of the data directory field    */
#define SET_INFOW       26      /* cells of the adapter and program     */
#define SET_DOSW        5       /* "3.10"                               */
#define SET_PORTW       4       /* longest port name ("LPT1")           */
#define SET_TYPEW       5       /* longest printer type ("EPSON")       */
#define SET_NUMW        2       /* digits of the printer numbers        */
#define SET_NUMCAP      100     /* above every printer number           */

/* Every field fits a form value, and every printer number its field.
   The printer choices are the words of cfg_portlist, numbered from
   PRN_LPT1 to PRN_DOS, and of cfg_typelist, from PR_EPSON to PR_PLAIN. */
extern char set_dir_fits[1];
extern char set_dir_fits[(LINK_MAXDIR <= FORM_MAXVAL) ? 1 : 2];
extern char set_num_fits[1];
extern char set_num_fits[(RPT_MAXPAGE < SET_NUMCAP &&
                          RPT_MAXLABELW < SET_NUMCAP &&
                          RPT_MAXLABELH < SET_NUMCAP) ? 1 : 2];
extern char set_choice_order[1];
extern char set_choice_order[(PRN_LPT1 == 0 && PRN_DOS == 3 &&
                              PR_PLAIN == PR_EPSON + 1) ? 1 : 2];

/* Field names, local to this form (never sent), short to save near
   data; each literal is kept once. */
static const char k_data[] = "DIR";
static const char k_sta[] = "STA";
static const char k_user[] = "USR";
static const char k_ord[] = "ORD";
static const char k_snow[] = "SNO";
static const char k_mono[] = "MON";
static const char k_bios[] = "BIO";
static const char k_adp[] = "ADP";
static const char k_cfg[] = "CFG";
static const char k_prog[] = "PRG";
static const char k_dos[] = "DOS";
static const char k_port[] = "PRP";
static const char k_type[] = "PRT";
static const char k_page[] = "PGL";
static const char k_lblw[] = "LBW";
static const char k_lblh[] = "LBH";

static const form_field set_fields[] = {
    { k_data, MC_DATA, 0, 1, SET_VCOL, FT_CHAR, LINK_MAXDIR, 0, FF_LOCAL,
      SET_DIRDISP, NULL },
    { k_sta, MC_STATION, 1, 1, SET_VCOL, FT_CHAR, LINK_STALEN, 0,
      FF_LOCAL, 0, NULL },
    { k_user, MC_USER, 2, 1, SET_VCOL, FT_CHAR, LINK_USERLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { k_ord, MC_ORDER, 3, 1, SET_VCOL, FT_CHOICE, CFG_ORDLEN, 0,
      FF_LOCAL | FF_REQ, 0, cfg_ordlist },
    { k_snow, MC_SNOW, 4, 1, SET_VCOL, FT_LOGIC, 1, 0, FF_LOCAL, 0, NULL },
    { k_mono, MC_MONO, 4, SET_MONOC, SET_MONOV, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { k_bios, MC_BIOS, 4, SET_BIOSC, SET_BIOSV, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { k_port, MC_PRINTER, 5, 1, SET_VCOL, FT_CHOICE, SET_PORTW,
      0, FF_LOCAL | FF_REQ, 0, cfg_portlist },
    { k_type, MC_PRTYPE, 5, SET_TYPEC, SET_TYPEV, FT_CHOICE, SET_TYPEW, 0,
      FF_LOCAL | FF_REQ, 0, cfg_typelist },
    { k_page, MC_PAGELEN, 5, SET_PAGEC, SET_PAGEV, FT_NUM, SET_NUMW, 0,
      FF_LOCAL | FF_REQ | FF_NONNEG, 0, NULL },
    { k_lblw, MC_LABELW, 5, SET_LBLWC, SET_LBLWV, FT_NUM, SET_NUMW, 0,
      FF_LOCAL | FF_REQ | FF_NONNEG, 0, NULL },
    { k_lblh, MC_LABELH, 5, SET_LBLHC, SET_LBLHV, FT_NUM, SET_NUMW, 0,
      FF_LOCAL | FF_REQ | FF_NONNEG, 0, NULL },
    { k_adp, MC_ADAPTER, 6, 1, SET_VCOL, FT_CHAR, SET_INFOW, 0,
      FF_LOCAL | FF_RO, 0, NULL },
    { k_cfg, MC_CFGFILE, 6, SET_COL2, SET_VCOL2, FT_CHAR, LINK_MAXDIR, 0,
      FF_LOCAL | FF_RO, (u8)(VID_COLS - 2 - SET_VCOL2), NULL },
    { k_prog, MC_PROGRAM, 7, 1, SET_VCOL, FT_CHAR, SET_INFOW, 0,
      FF_LOCAL | FF_RO, 0, NULL },
    { k_dos, MC_DOS, 7, SET_COL2, SET_VCOL2, FT_CHAR, SET_DOSW, 0,
      FF_LOCAL | FF_RO, 0, NULL }
};

static const form_def set_form = {
    MC_TITLE, "SETTINGS", TUI_TOP, 0, SET_H, VID_COLS,
    (u8)(sizeof set_fields / sizeof set_fields[0]), set_fields
};

static const char k_true[] = "T";
static const char k_false[] = "F";

/* The logical field's value of flag v. */
static const char *yn(u8 v)
{
    return v ? k_true : k_false;
}

/* Load the settings and the read-only lines into the form. */
static void load(form *f)
{
    char w[TXT_U32LEN + 1];     /* choice word, number, DOS version     */
    u8 major;
    u8 minor;
    u16 a;
    u16 n;
    u8 adp;

    form_load(f, k_data,
              scr_cfg.data[0] != '\0' ? scr_cfg.data : cfg_defdir);
    form_load(f, k_sta, scr_cfg.station);
    form_load(f, k_user, scr_cfg.user);
    form_load(f, k_ord, scr_cfg.itemord);
    form_load(f, k_snow, yn(scr_cfg.snow));
    form_load(f, k_mono, yn(scr_cfg.mono));
    form_load(f, k_bios, yn(scr_cfg.bios));
    form_choice_word(cfg_portlist, scr_cfg.prport, w, (u16)sizeof w);
    form_load(f, k_port, w);
    form_choice_word(cfg_typelist, (u16)(scr_cfg.prtype - PR_EPSON), w,
                     (u16)sizeof w);
    form_load(f, k_type, w);
    txt_u32(w, scr_cfg.pagelen);
    form_load(f, k_page, w);
    txt_u32(w, scr_cfg.labelw);
    form_load(f, k_lblw, w);
    txt_u32(w, scr_cfg.labelh);
    form_load(f, k_lblh, w);
    adp = vid_adapter();
    a = adp == VID_A_MDA ? MC_MDA : (adp == VID_A_CGA ? MC_CGA : MC_EGA);
    form_load(f, k_adp, msg_sub((scr_vidflags & VID_F_BIOS) ? MC_VIABIOS
                                                               : MC_DIRECT,
                                   msg(a), NULL));
    form_load(f, k_cfg, scr_cfgpath);
    form_load(f, k_prog, scr_version);
    /* At most three digits, '.' and the two of the minor version. */
    dos_version(&major, &minor);
    n = txt_u32(w, major);
    w[n] = '.';
    txt_u32w(w + n + 1, (u32)(minor % 100), 2, '0');
    w[n + 3] = '\0';
    form_load(f, k_dos, w);
}

/* 1 when the number in field name lies in lo..hi, *v receiving it;
   else 0, the cursor then on the field and its range said. */
static int num_in(form *f, const char *name, u8 lo, u8 hi, u8 *v)
{
    char a[TXT_U32LEN + 1];
    char b[TXT_U32LEN + 1];
    const char *s;
    u16 n;

    s = form_get(f, name);
    n = msg_dec(&s, SET_NUMCAP);
    if (n >= lo && n <= hi) {
        *v = (u8)n;
        return 1;
    }
    txt_u32(a, lo);
    txt_u32(b, hi);
    form_field_error(f, name, msg_sub(MC_RANGE, a, b));
    return 0;
}

/* Write the form's values to the settings file: 0, a CF_E code, or -1
   when a printer number is outside its range (said on the form). */
static int save(form *f)
{
    cfg c;
    int rc;

    c = scr_cfg;
    if (!num_in(f, k_page, RPT_MINPAGE, RPT_MAXPAGE, &c.pagelen) ||
        !num_in(f, k_lblw, RPT_MINLABELW, RPT_MAXLABELW, &c.labelw) ||
        !num_in(f, k_lblh, RPT_MINLABELH, RPT_MAXLABELH, &c.labelh))
        return -1;
    c.prport = (u8)form_choice_num(cfg_portlist, form_get(f, k_port));
    c.prtype = (u8)(PR_EPSON + form_choice_num(cfg_typelist,
                                               form_get(f, k_type)));
    if (c.data[0] != '\0' || strcmp(form_get(f, k_data), cfg_defdir) != 0)
        form_get_text(f, k_data, c.data, (u16)sizeof c.data);
    form_get_text(f, k_sta, c.station, (u16)sizeof c.station);
    form_get_text(f, k_user, c.user, (u16)sizeof c.user);
    form_get_text(f, k_ord, c.itemord, (u16)sizeof c.itemord);
    c.snow = (u8)(form_get(f, k_snow)[0] == 'T');
    c.mono = (u8)(form_get(f, k_mono)[0] == 'T');
    c.bios = (u8)(form_get(f, k_bios)[0] == 'T');
    rc = cfg_save(&c, scr_cfgpath);
    if (rc != 0)
        return rc;
    scr_cfg = c;
    vid_snow(c.snow);
    if (!vid_mono())
        tui_palette((u8)(c.mono ? TP_MONOCGA : TP_COLOR));
    return 0;
}

u8 scr_settings(u8 cmd)
{
    form *f;
    int used;
    int rc;
    u16 k;

    if (cmd != SCMD_SETTINGS)
        return TUI_CMD_NONE;
    f = form_open(&set_form, FORM_EDIT, NULL, NULL, NULL);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    load(f);
    tui_hint(msg(MC_HINT));
    cmd = TUI_CMD_NONE;
    for (;;) {
        k = form_run(f);
        if (k == KEY_ESC)
            break;
        if (k == KEY_ENTER) {
            rc = save(f);
            if (rc < 0)
                continue;
            if (rc != 0) {
                tui_msg(msg_sub(MC_SAVEFAIL, scr_cfgpath, rqe_doserr(rc)),
                        1);
            } else {
                form_saved(f);
                tui_msg(msg(MC_SAVED), 0);
            }
            continue;
        }
        cmd = tui_global(k, &used);
        if (used)
            break;
        cmd = TUI_CMD_NONE;
    }
    form_close(f);
    return cmd;
}
