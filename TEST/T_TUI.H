/*
 * T_TUI.H - what the two files of the T_TUI test (TEST/T_TUI.C,
 * TEST/T_TUI2.C) share.
 */
#ifndef T_TUI_H
#define T_TUI_H

#define PATHMAX         96

/* The scratch directory prefix of the test's files (main's dir). */
extern char g_tdir[PATHMAX];

/* T_TUI2.C: key scripts read and refused (their line limits, one that
   cannot be read), the keys of a script seen waiting, and the snapshot
   file a script writes. */
void test_script_peeks(void);
void test_script_limits(void);
void test_keyscripts(void);

/* T_TUI2.C: the clicks of the mouse, as tui_hit, the key scripts and the
   hint row take them, and the key names. */
void test_clicks(void);

#endif
