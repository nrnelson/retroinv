/*
 * WCHRT.H - the resource, control and command numbers that
 * TEST/WCHRT.RC and TEST/WCHRT.C share.
 */
#ifndef WCHRT_H
#define WCHRT_H

#define WCH_S_TOOL      1       /* the tool bar's pieces, a string      */
#define WCH_S_LOAD      2       /* the preview panel's text of CTRL T   */

/* Children of the frame, in the order the program makes them, and the
   tool bar's slot control. */
#define WCH_TOOL        100     /* the tool bar                         */
#define WCH_FIND        101     /* an edit control in the bar's slot    */
#define WCH_EDIT        102     /* an edit control, the focus's         */
#define WCH_SEEN        103     /* what the frame saw, a static         */
#define WCH_ANS         104     /* the last answer, a static           */
#define WCH_STAT        105     /* the status bar                       */
#define WCH_SPLIT       106     /* the splitter (SRC/WIN/WSPLIT.H)      */
#define WCH_PREV        107     /* the preview panel (SRC/WIN/WPREV.H)  */

/* The tool bar's commands, button k's WCH_CMD + k. */
#define WCH_CMD         201

/* The splitter's drags (WCHRT.C's header), in pixels. */
#define WCH_SPX         100     /* its left edge at first               */
#define WCH_SPLO        60      /* the lowest left edge SHIFT F1 allows */
#define WCH_SPHI        200     /* the highest                          */
#define WCH_GRAB        2       /* a press's x within the splitter      */
#define WCH_STEP        50      /* F1's move right                      */
#define WCH_FAR         300     /* SHIFT and CTRL F1's moves, each way  */
#define WCH_GX1         120     /* CTRL G's guide first                 */
#define WCH_GX2         196     /* then                                 */

/* The preview panel's keys (WCHRT.C's header). */
#define WCH_NLINES      40      /* CTRL P's numbered lines of notes     */
#define WCH_THUMB       3       /* the row CTRL H scrolls to            */
#define WCH_RECSZ       64      /* a field of CTRL L, its label and its
                                   value with their NULs: so many fill
                                   the panel's block exactly            */

#endif
