/*
 * WFSTR.H - the numbers of the form dialog's strings (WFORM.H) and of
 * its pick list's template (WPICK.H), which WFORM.RC gives to the
 * resources of each program linking WFORM.C.
 *
 * The resource compiler reads it too, so it holds #define lines only.
 */
#ifndef WFSTR_H
#define WFSTR_H

#define WF_S_OK         1024    /* the OK button                        */
#define WF_S_CANCEL     1025    /* the Cancel button                    */
/* The memo limits, each given its number for %d. */
#define WF_S_LINE       1026    /* a line longer than MEMO_COLS, a TAB
                                   or a control byte                    */
#define WF_S_LINES      1027    /* more lines than MEMO_MAXLINES        */
#define WF_S_BYTES      1029    /* more bytes than LINK_MAXMEMO stored,
                                   a line break counting two            */
#define WF_S_SAVEFIRST  1028    /* a record button on a changed record  */
#define WF_S_PICK       1030    /* a code field's pick button           */
#define WF_S_EDIT       1031    /* the Edit button of FORM_VIEW         */
#define WF_S_CLOSE      1032    /* the Close button of FORM_VIEW        */
#define WF_S_HELP       1033    /* the Help button                      */
#define WF_S_PAGES      1034    /* the tabs of a form with a memo and no
                                   page table of its own                */
#define WF_S_EDITCAP    1035    /* the caption in FORM_EDIT, the record's
                                   caption as %s                        */
/* A field's label of the Windows client's own, where INV's would read
   short in a dialog: the string WF_S_LABEL + the label's message number
   (WLABEL.RC), above every message number (MSG_MAXNUM of MSG.H, which
   WFORM.C checks); every other string number of a program that links
   WFORM.C stays below WF_S_LABEL. */
#define WF_S_LABEL      2048

/* The pick list's dialog and its list. */
#define WF_R_PICK       1024    /* the template                         */
#define WF_C_PICK       1025    /* the list                             */

#endif
