/*
 * WTDRV.H - the resource and control numbers that TEST/WTDRV.RC and
 * TEST/WTDRV.C share.
 */
#ifndef WTDRV_H
#define WTDRV_H

#define WTD_D_MAIN      1       /* the main window, a dialog            */
#define WTD_D_ABOUT     2       /* the modal dialog                     */
#define WTD_M_MAIN      1       /* the main window's menu               */
#define WTD_A_MAIN      1       /* its accelerators                     */

/* Controls of the main window and of the modal dialog. */
#define WTD_NAME        101     /* the name, an edit control            */
#define WTD_LOANED      102     /* a check box                          */
#define WTD_LIST        103     /* the rows, a list box                 */
#define WTD_ADD         104     /* adds the name as a row               */
#define WTD_ABOUT       105     /* opens the modal dialog               */
#define WTD_PASS        106     /* the dialog's password edit control   */

/* Menu commands. */
#define WTD_C_DIALOG    201     /* opens the modal dialog               */
#define WTD_C_WORK      202     /* holds the driver while it works      */
#define WTD_C_EXIT      203
#define WTD_C_MARK      204     /* checks or unchecks itself            */
#define WTD_C_GRAY      205     /* always grayed                        */

#endif
