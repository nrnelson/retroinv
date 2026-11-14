/*
 * WTDRV2.H - the resource and control numbers that TEST/WTDRV2.RC and
 * TEST/WTDRV2.C share.
 */
#ifndef WTDRV2_H
#define WTDRV2_H

#define WT2_S_TITLE     1       /* the window's title, a string */
#define WT2_MENU        1       /* the menu bar, a menu */
#define WT2_ACCEL       1       /* the accelerator table */

/* Commands: menu items and accelerators. */
#define WT2_C_LIST      101     /* focus the single-selection list box */
#define WT2_C_MARKS     102     /* focus the extended-selection list box */
#define WT2_C_COMBO     103     /* focus the combo box */
#define WT2_C_EDIT      104     /* focus the multi-line edit control */
#define WT2_C_PASS      105     /* focus the password edit control */
#define WT2_C_MSG       201     /* ask a question in a message box */
#define WT2_C_BUSY      202     /* hold the script for a while */
#define WT2_C_CHECK     203     /* toggle the mark of this item */
#define WT2_C_GONE      204     /* a disabled item, never sent */
#define WT2_C_EXIT      205     /* end the program */
#define WT2_C_DEEP      301     /* the item of the submenu */
#define WT2_C_STALL     401     /* hold the script for good (accelerator) */
#define WT2_C_HIDE      402     /* hide the window (accelerator) */

/* Controls: the window IDs of the snapshots. */
#define WT2_ID_LABEL    10      /* static text with an OEM character */
#define WT2_ID_LIST     20      /* single-selection list box */
#define WT2_ID_MARKS    21      /* extended-selection list box */
#define WT2_ID_COMBO    30      /* drop-down combo box */
#define WT2_ID_EDIT     40      /* multi-line edit control */
#define WT2_ID_PASS     41      /* password edit control */
#define WT2_ID_OFF      50      /* disabled push button */
#define WT2_ID_HID      51      /* hidden push button */
#define WT2_ID_GO       52      /* default push button */
#define WT2_ID_BOX      53      /* check box, checked at the start */
#define WT2_ID_TRI      54      /* 3-state check box, indeterminate */
#define WT2_ID_STATUS   60      /* static text the commands write to */

/* The mouse pads of /MOUSE, each reporting in its window text the mouse
   messages it gets, and their places in pixels: pads A and B in the
   window's client area, pad C in pad A's (TEST/mousechk.py reads them). */
#define WT2_ID_PADA     70      /* pad of a class with CS_DBLCLKS */
#define WT2_ID_PADB     71      /* pad of a class without */
#define WT2_ID_PADC     1       /* pad of B's class inside pad A */
#define WT2_PADX        4       /* pad A's left edge */
#define WT2_PADBX       108     /* pad B's left edge */
#define WT2_PADY        190     /* both pads' top edge */
#define WT2_PADW        96      /* both pads' width */
#define WT2_PADH        40      /* and height */
#define WT2_PADCX       50      /* pad C's left edge */
#define WT2_PADCY       6       /* its top edge */
#define WT2_PADCW       40      /* its width */
#define WT2_PADCH       28      /* and height */

#endif
