/*
 * T_ENGINE.H - what the two source files of the T_ENGINE test share:
 * the engine under test, the memo buffer, the helpers of T_ENGINE.C (a
 * fresh engine for each phase, expected successes, items added from
 * KEY=value specs, and fields written straight into ITEMS.DBF) and the
 * phases of T_ENGIN2.C, T_ENGIN3.C and T_ENGIN4.C.  The engine
 * scaffolding of TENGH.H (pairs, reading back, expected failures)
 * serves them all.
 *
 * The test is split in four compilation units, T_ENGINE.C (helpers, the
 * items phase, main), T_ENGIN2.C (the lists, categories and bounded
 * phases), T_ENGIN3.C (the locations and walks phases) and T_ENGIN4.C
 * (the texts and tags phases), because Watcom C 6.5 runs out of
 * compiler memory on larger ones.
 */
#ifndef T_ENGINE_H
#define T_ENGINE_H

#include "COMPAT.H"
#include "ENGINE.H"

extern eng *g_e;

/* A heap buffer of ENG_MAXMEMO + 2 bytes for note texts at the memo
   limit and the expected pages of the tags phase, allocated by main. */
extern char *g_big;

/* The helpers of T_ENGINE.C, each described where it is defined. */
int phase_start(void);
void phase_end(void);
int expect_ok(int rc, const char *file, int line);
u32 add_item(const char *spec);
const char *aud(u32 k);
void set_parent(u32 child, u32 parent);
void raw_copies(u32 src, u16 count);
void raw_delete(u32 id);

/* Check that call returns ENG_OK, reporting a failure at the file and
   line of the OK. */
#define OK(call) expect_ok((call), TF_FILE, __LINE__)

/* The phases of T_ENGIN2.C, T_ENGIN3.C and T_ENGIN4.C. */
void test_lists(void);
void test_cats(void);
void test_bounded(void);
void test_locs(void);
void test_walks(void);
void test_texts(void);
void test_tags(void);

#endif
