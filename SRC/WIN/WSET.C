/*
 * WSET.C - INVWIN's settings (WSET.H).
 *
 * The form's fields are local, never sent, named shortly as INV's are;
 * the dialog saves nothing itself (wf_ops without a table or a send), so
 * that OK, once the form's checks pass, ends it with WF_OK and the
 * values written here, the dialog shown again after a failed write.
 */
#define COMPAT_UART
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "MSG.H"
#include "RQERR.H"
#include "FMODEL.H"
#include "CFG.H"
#include "SCRSET.H"
#include "WFORM.H"
#include "WSCR.H"
#include "WSET.H"
#include "INVWIN.H"

/* The value column, after a label's slot (the W= of SCRSET.MSG's
   labels) and a blank; the cells the data directory, the settings
   file and the program show; the longest baud rate. */
#define WT_VCOL         17
#define WT_DIRW         40
#define WT_BAUDW        5

/* The rates of UART_BAUDOK, UART_MINBAUD doubled up to UART_MAXBAUD
   (the build fails unless those are the ends of the list). */
extern char wt_bauds_fit[1];
extern char wt_bauds_fit[(UART_MINBAUD == 1200u &&
                          UART_MAXBAUD == 38400u) ? 1 : 2];
static const char bauds[] = "1200 2400 4800 9600 19200 38400";

static const char k_data[] = "DIR";
static const char k_sta[] = "STA";
static const char k_user[] = "USR";
static const char k_ord[] = "ORD";
static const char k_baud[] = "BAU";
static const char k_rts[] = "RTS";
static const char k_cfg[] = "CFG";
static const char k_prog[] = "PRG";

static const form_field set_fields[] = {
    { k_data, MC_DATA, 0, 0, WT_VCOL, FT_CHAR, LINK_MAXDIR, 0, FF_LOCAL,
      WT_DIRW, NULL },
    { k_sta, MC_STATION, 1, 0, WT_VCOL, FT_CHAR, LINK_STALEN, 0, FF_LOCAL,
      0, NULL },
    { k_user, MC_USER, 2, 0, WT_VCOL, FT_CHAR, LINK_USERLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { k_ord, MC_ORDER, 3, 0, WT_VCOL, FT_CHOICE, CFG_ORDLEN, 0,
      FF_LOCAL | FF_REQ, 0, cfg_ordlist },
    { k_baud, MC_BAUD, 4, 0, WT_VCOL, FT_CHOICE, WT_BAUDW, 0,
      FF_LOCAL | FF_REQ, 0, bauds },
    { k_rts, MC_RTSCTS, 5, 0, WT_VCOL, FT_LOGIC, 1, 0, FF_LOCAL, 0, NULL },
    { k_cfg, MC_CFGFILE, 6, 0, WT_VCOL, FT_CHAR, LINK_MAXDIR, 0,
      FF_LOCAL | FF_RO, WT_DIRW, NULL },
    { k_prog, MC_PROGRAM, 7, 0, WT_VCOL, FT_CHAR, LINK_MAXDIR, 0,
      FF_LOCAL | FF_RO, WT_DIRW, NULL }
};

static const form_def set_def = {
    MC_TITLE, "SETTINGS", 0, 0, 0, 0,
    (u8)(sizeof set_fields / sizeof set_fields[0]), set_fields
};

static const wf_ops set_ops = { NULL, NULL, 0, ws_form_busy, NULL, NULL };

/* Load c, path and version into f, defdir for a data directory that c
   leaves to the default. */
static void load(form *f, const cfg *c, const char *path,
                 const char *version, const char *defdir)
{
    char w[TXT_U32LEN + 1];

    form_load(f, k_data, c->data[0] != '\0' ? c->data : defdir);
    form_load(f, k_sta, c->station);
    form_load(f, k_user, c->user);
    form_load(f, k_ord, c->itemord);
    txt_u32(w, c->baud);
    form_load(f, k_baud, w);
    form_load(f, k_rts, c->rtscts ? "T" : "F");
    form_load(f, k_cfg, path);
    form_load(f, k_prog, version);
}

/* Write the values of f, over the rest of c, to path, the data
   directory left to the default while f shows defdir: 0 or a CF_E
   code. */
static int save(form *f, cfg *c, const char *path, const char *defdir)
{
    if (c->data[0] != '\0' || strcmp(form_get(f, k_data), defdir) != 0)
        form_get_text(f, k_data, c->data, (u16)sizeof c->data);
    form_get_text(f, k_sta, c->station, (u16)sizeof c->station);
    form_get_text(f, k_user, c->user, (u16)sizeof c->user);
    form_get_text(f, k_ord, c->itemord, (u16)sizeof c->itemord);
    c->baud = txt_num(form_get(f, k_baud), UART_MAXBAUD);
    c->rtscts = (u8)(form_get(f, k_rts)[0] == 'T');
    return cfg_save(c, path);
}

int wset_run(HWND owner, const char *path, const char *version,
             const char *defdir, char *itemord)
{
    char fmt[MSG_MAXLEN + 1];
    char p[CFG_PATHMAX + 1];
    char why[MSG_MAXLEN + 1];
    char t[2 * MSG_MAXLEN + CFG_PATHMAX + 1];
    char id[1];
    cfg c;
    form *f;
    int rc;
    int r;

    rc = cfg_load(&c, path);
    if (rc != 0) {
        LoadString((HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE),
                   IW_S_NOCFG, fmt, sizeof fmt);
        OemToAnsi(path, p);
        OemToAnsi(rqe_doserr(rc), why);
        wsprintf(t, fmt, (LPCSTR)p, (LPCSTR)why);
        ws_say(owner, MB_ICONSTOP, t);
        return WF_CANCEL;
    }
    r = wf_open(&f, &set_def, FORM_EDIT);
    if (r != 0)
        return r;
    load(f, &c, path, version, defdir);
    id[0] = '\0';
    for (;;) {
        r = wf_run(f, owner, NULL, 0, &set_ops, NULL, id, (u16)sizeof id);
        if (r != WF_OK)
            break;
        rc = save(f, &c, path, defdir);
        if (rc == 0)
            break;
        ws_say_oem(owner, MB_ICONSTOP,
                   msg_sub(MC_SAVEFAIL, path, rqe_doserr(rc)));
    }
    form_close(f);
    if (r == WF_OK) {
        strcpy(itemord, c.itemord);
        ws_say_oem(owner, MB_ICONINFORMATION, msg(MC_WSAVED));
    }
    return r;
}
