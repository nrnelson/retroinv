/*
 * WRSTR.H - the resource numbers of the report dialogs (WRPT.H), which
 * WRPT.RC gives to the resources of the program linking WRPT.C.
 *
 * The resource compiler reads it too, so it holds #define lines only.
 */
#ifndef WRSTR_H
#define WRSTR_H

#define WR_R_RUN        20      /* the dialog of a run under way        */
#define WR_R_VIEW       21      /* the preview                          */

#define WR_C_PROGRESS   300     /* the run's progress                   */
#define WR_C_LINES      301     /* the preview's lines                  */

/* The ends of a PRINTER run that Windows gives. */
#define WR_S_NOPRN      1056    /* no default printer                   */
#define WR_S_PRNERR     1057    /* the printer failed                   */
#define WR_S_NODRV      1058    /* its driver did not load              */

#endif
