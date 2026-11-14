/*
 * WRPT.H - the reports and labels of INVWIN (WRPT.C): INV's report and
 * label screens (SCRRPT.C) as dialogs, their runs those of RPTRUN.H.
 *
 * A Reports entry shows the form of its kind (SCRRFORM.H) in the form
 * dialog (WFORM.H), captioned with the kind's title, its values those
 * INV's form starts with.  OK checks them and runs the report from the
 * dialog (wf_ops' send), which is held busy meanwhile; once the run has
 * ended and its outcome has been shown, the dialog opens again with the
 * same values for another run, until Cancel, as INV's form stays open
 * until Esc.  FILE output without a file name, or with one that names a
 * data or scratch file or a device (scr_rpt_file_bad), is refused on
 * the dialog's message line (MQ_NEEDFILE, MQ_BADFILE), the focus on the
 * file; a file that exists is replaced only once a message box asking
 * MT_REPLACE has been answered Yes, No leaving the focus on the file
 * and the message line empty.  One label of an item or location that
 * does not exist is refused on the field that names it (MQ_NOTFOUND),
 * and any other failed request is shown as the dialog shows a failed
 * save.
 *
 * The run is INV's: the filters as SCRRFORM.H reads them, the date,
 * when none is typed, today, and the geometry of the settings.  While
 * it runs a dialog, WR_R_RUN, over the report dialog shows the progress
 * after each page (MQ_PROGRESS); its Cancel, or Esc, stops the run
 * there, as Esc stops INV's, and cancels a print job (prn_cancel).
 *
 * The output, as INV's: PRINTER, Windows' default printer (CPRNW.C),
 * through a job of an Epson printer, whose codes give the printer's
 * styles; FILE, the text a plain printer gets, into the file named; or
 * SCREEN, the station's scratch file (scr_rpt_screen), shown by the
 * preview once the run is complete and removed after it.  A run that
 * shows no preview ends with a message box over the frame, captioned
 * with the report's title: MQ_DONE with the records laid out, or why it
 * ended early, INV's text (MQ_STOPPED, MQ_CANCELLED, MQ_FILEERR,
 * MQ_SCRATCH, MT_NOMEM) or, for a printer, WR_S_NOPRN without a default
 * printer, WR_S_NODRV when its driver did not load and WR_S_PRNERR when
 * Windows did not print.  A SCREEN run of labels without records shows
 * MQ_DONE too.
 *
 * The preview, the dialog WR_R_VIEW, opens in the frame's place, as
 * much of it as the screen shows, and may be sized and maximized; its
 * caption is the report's title and its list box WR_C_LINES, in
 * OEM_FIXED_FONT, shows as many lines of the file as fit, a page end as
 * a rule, each row cut to RPT_WIDTH characters, and a bin label's code
 * twice as wide, as it prints.  Up and Down move the lines one, PgUp and
 * PgDn by the rows shown, Home and End to the first and the last rows,
 * and Left and Right the columns when the box is narrower than the
 * output, RPT_WIDTH characters of a report or the label width of
 * labels, as do the dialog's scroll bars; Esc, Enter and Close end it.
 * Of a file of more than SCR_RV_MAXLINES lines, or when memory is short,
 * the first lines show, after a message box saying how many (MQ_VSHOWN);
 * a file that cannot be read is said instead (MQ_FILEERR, MT_NOMEM).
 *
 * Real mode: every dialog's state is on wrpt_run's stack, reached by a
 * near pointer; the preview holds only the lines it shows.
 */
#ifndef WRPT_H
#define WRPT_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RPT.H"
#include "WRSTR.H"

/* Run the reports of cmd, SCMD_RPT_LIST to SCMD_LBL_BIN (SCR.H), over
   owner, the frame, on link l for a user of level (rqe_class), with the
   geometry g and the station's scratch file: the end of its last dialog,
   wf_run's WF_CANCEL, WF_EFAIL (the form could not be made), WF_LOGIN,
   WF_QUIT or WF_LOST. */
int wrpt_run(HWND owner, link *l, u8 level, u8 cmd, const rpt_geom *g,
             const char *station);

#endif
