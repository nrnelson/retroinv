/*
 * T_LISTV.H - what the two source files of the T_LISTV test share: the
 * item list source, the helpers that read a view, the keys typed ahead,
 * and the tests of T_LISTV2.C that main runs.
 *
 * The columns and the windows taller than a protocol page are a file of
 * their own only to stay within the compiler memory of Watcom C 6.5.
 */
#ifndef T_LISTV_H
#define T_LISTV_H

extern u16 g_nitems;            /* items in the engine's list           */
extern const lv_src items_src;  /* ITEM LIST over them, ordered by ID   */
extern char g_seen[];           /* status information at the look       */

/* The selected row's item number, or 0. */
u32 sel_id(lv *v);

/* The AFTER= cursor of request line s, or "". */
const char *after_of(const char *s);

/* 1 when the request line of len bytes is an ITEM LIST. */
int is_list(const char *line, u16 len);

/* Type keys ahead (up to KEY_NONE) from the look-th look for one. */
void ahead(const u16 *keys, u16 look);

/* The tests of list columns and date columns. */
void test_columns(void);

/* The tests of windows taller than LINK_PAGEMAX rows. */
void test_tall(void);

#endif
