/*
 * SCRCSV.C - export of a table to a CSV file and import of a CSV file
 * into ITEMS or LOCS (SCR.H).
 *
 * The form, its requests and the reading of their answers are those of
 * SCRCFORM.H.  The form leaves on Esc at once (form_noask), as its
 * values are not a record.  scr_csv checks the level with tui_allow
 * before opening the form; the server checks it again.  A file error
 * naming FILE (scr_csv_ferr) and a 102 put the cursor on their field
 * through form_error.
 *
 * Export.  Enter sends the export's requests, showing the records written
 * so far in the status line between them; an Esc typed meanwhile
 * (tui_esc_waiting) stops the export.  When the first answer is 201
 * naming FILE (the file exists), the user is asked whether to replace
 * it; on Y the export starts again, replacing it.
 *
 * Import.  Enter asks before anything is sent, says on the hint row that
 * the import is running (one request that may take minutes on a slow
 * machine), then sends the IMPORT: the records added are told on the
 * hint row, the refused lines in a box over the form, kept meanwhile in
 * a far block, and a failure after some records were added in a message
 * box.  402 (other sessions are open: an import needs the system to
 * itself) goes to tui_fail.
 *
 * The screen's requests are sent only through scr_csv_send.
 */
#define TUI_CLICKS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRCFORM.H"

#define MX_HINTEXP      1045    /* hints of the two forms               */
#define MX_HINTIMP      1046

#define CX_BOXW         (SCR_CX_REFW + 4)   /* the refused lines' box   */

/* Report a failed request: a file error naming FILE becomes a field
   error (scr_csv_ferr), so the cursor goes to the file name with the
   client's text for the code. */
static u8 csv_fail(form *f, rq_res *res)
{
    scr_csv_ferr(res);
    return form_error(f, res);
}

/* ---------------------------------------------------------------------
 * Export.
 * --------------------------------------------------------------------- */

/* The records written so far, on the status line. */
static void exp_progress(u32 rows)
{
    char num[SCR_CX_NUMSZ];

    txt_u32(num, rows);
    tui_info(msg_sub(MX_PROGRESS, num, NULL));
    vid_flush();
}

/* Run the export r asks for; the command the screen must return at
   once, or TUI_CMD_NONE to stay. */
static u8 run_export(form *f, scr_csv_req *r)
{
    char cursor[LINK_MAXCURSOR + 1];
    char num[SCR_CX_NUMSZ];
    rq_res res;
    u32 rows;
    int first;

    for (;;) {
        rows = 0;
        first = 1;
        exp_progress(rows);
        for (;;) {
            if (scr_csv_send(tui_s.l, r, 1, first ? NULL : cursor,
                             scr_csv_rows, &rows, &res) != RQ_OK)
                break;
            exp_progress(rows);
            if (res.more[0] == '\0')
                break;
            strcpy(cursor, res.more);
            first = 0;
            if (tui_esc_waiting()) {
                tui_info("");
                tui_msg(msg_sub(MX_STOPPED, r->file, NULL), 0);
                return TUI_CMD_NONE;
            }
        }
        tui_info("");
        if (res.code == RQ_OK) {
            txt_u32(num, rows);
            tui_msg(msg_sub(MX_EXPORTED, num, r->file), 0);
            return TUI_CMD_NONE;
        }
        if (first && !r->replace && !res.closed && res.code == RQ_EDUP &&
            strcmp(res.field, scr_csv_k_file) == 0) {
            if (!tui_confirm(msg_sub(MT_REPLACE, r->file, NULL)))
                return TUI_CMD_NONE;
            r->replace = 1;
            continue;
        }
        break;
    }
    if (!first && !res.closed && res.code == RQ_ECURSOR) {
        tui_msg(msg(MX_LOST), 1);
        return TUI_CMD_NONE;
    }
    return csv_fail(f, &res);
}

/* ---------------------------------------------------------------------
 * Import.
 * --------------------------------------------------------------------- */

/* The refused lines in a box over the form until Enter, Esc or a click
   anywhere, as tui_msgbox closes, then the form's hints hint again;
   code is the error that followed them, or RQ_OK. */
static void show_refused(scr_csv_imp *x, int code, u16 hint)
{
    char t[SCR_CX_REFW + 1];
    cmem_blk *save;
    u8 h;
    u8 top;
    u8 left;
    u8 row;
    u16 i;
    u16 k;

    h = (u8)(x->n + 5 + (code != RQ_OK));
    top = (u8)(TUI_TOP + (TUI_WORKROWS - h) / 2);
    left = (u8)((VID_COLS - CX_BOXW) / 2);
    save = vid_save(top, left, h, CX_BOXW);
    tui_box(top, left, h, CX_BOXW, TB_DOUBLE, msg(MX_T_REFUSED));
    row = (u8)(top + 2);
    for (i = 0; i < x->n; i++) {
        cmem_get(x->lines, (u16)(i * sizeof t), t, (u16)sizeof t);
        tui_textn(row++, (u8)(left + 2), t, SCR_CX_REFW, TA_NORMAL);
    }
    row++;
    if (x->errors[0] == '\0')
        txt_u32(x->errors, x->nr);
    tui_textn(row++, (u8)(left + 2), msg_sub(MX_REFCOUNT, x->errors, NULL),
              SCR_CX_REFW, TA_BOLD);
    if (code != RQ_OK)
        tui_textn(row, (u8)(left + 2), msg_sub(MX_REFERR, rqe_text(code),
                                               NULL), SCR_CX_REFW, TA_ERROR);
    tui_hint(msg(MT_HINTOK));
    do
        k = tui_key();
    while (k != KEY_ENTER && k != KEY_ESC &&
           tui_hit(k, 0, 0, VID_ROWS, VID_COLS) < 0);
    if (save != NULL)
        vid_restore(save);
    else
        tui_clear(TUI_TOP, TUI_BOTTOM);
    tui_hint(msg(hint));
}

/* Run the import r asks for, after asking; the command the screen must
   return at once, or TUI_CMD_NONE to stay. */
static u8 run_import(form *f, scr_csv_req *r)
{
    char added[SCR_CX_NUMSZ];
    rq_res res;
    scr_csv_imp x;
    u8 cmd;

    if (!tui_confirm(msg_sub(MX_IMPORTQ, r->file, r->table)))
        return TUI_CMD_NONE;
    memset(&x, 0, sizeof x);
    x.lines = cmem_alloc((u16)(LINK_IMPERRS * (SCR_CX_REFW + 1)));
    if (x.lines == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    tui_msg(msg(MX_WAIT), 0);
    vid_flush();
    scr_csv_send(tui_s.l, r, 0, NULL, scr_csv_imp_line, &x, &res);
    cmd = TUI_CMD_NONE;
    if (!res.closed && x.refused) {
        show_refused(&x, res.code, MX_HINTIMP);
    } else if (!res.closed && x.partial && res.code != RQ_OK) {
        tui_msgbox(msg(MX_T_IMPORT),
                   msg_sub(MX_PARTIAL, x.added, rqe_text(res.code)));
    } else if (res.code == RQ_OK) {
        rq_okval(&res, scr_csv_k_added, added, (u16)sizeof added);
        tui_msg(msg_sub(MX_ADDED, added, r->table), 0);
    } else {
        cmd = csv_fail(f, &res);
    }
    cmem_free(x.lines);
    return cmd;
}

/* ---------------------------------------------------------------------
 * The screen.
 * --------------------------------------------------------------------- */

u8 scr_csv(u8 cmd)
{
    form_field fl[SCR_CX_NFIELDS];
    form_def def;
    scr_csv_req r;
    form *f;
    int isexp;
    int used;
    u16 hint;
    u16 k;

    isexp = cmd == SCMD_EXPORT;
    if (cmd != SCMD_EXPORT && cmd != SCMD_IMPORT)
        return TUI_CMD_NONE;
    if (!tui_allow(TUI_LVL_ADMIN))
        return TUI_CMD_NONE;
    scr_csv_form(&def, fl, isexp);
    hint = isexp ? MX_HINTEXP : MX_HINTIMP;
    f = form_open(&def, FORM_EDIT, NULL, NULL, NULL);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    scr_csv_preset(f);
    form_noask(f);
    tui_hint(msg(hint));
    cmd = TUI_CMD_NONE;
    for (;;) {
        k = form_run(f);
        if (k == KEY_ESC)
            break;
        if (k == KEY_ENTER) {
            scr_csv_get(&r, f);
            cmd = isexp ? run_export(f, &r) : run_import(f, &r);
            if (cmd != TUI_CMD_NONE)
                break;
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
