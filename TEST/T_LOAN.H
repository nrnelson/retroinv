/*
 * T_LOAN.H - what the two source files of the T_LOAN test share: the
 * engine under test, the clock of the repairs, the helpers of T_LOAN.C
 * (fields read back, new rows, hand edits, the counters' check, listed
 * scans) and the phases of T_LOAN2.C.
 *
 * The test is split in two compilation units, T_LOAN.C (the loans,
 * their lists and repair, main) and T_LOAN2.C (reservations and in-use
 * equipment), because Watcom C 6.5 runs out of compiler memory on one.
 */
#ifndef T_LOAN_H
#define T_LOAN_H

#include "COMPAT.H"
#include "ENGINE.H"

#define LISTMAX         200     /* a list of LOAN_IDs, NUL included     */

extern eng *g_e;
extern u32 g_id;                /* the LOAN_ID row gave                 */
extern const eng_time g_next;   /* the clock of the repairs             */
extern const char *g_holder;    /* the HOLDER filter of ids, NULL none  */

/* The helpers of T_LOAN.C, each described where it is defined. */
const char *ifield(u32 id, const char *name);
const char *lfield(u32 id, const char *name);
int marked(void);
u32 add_item(const char *spec);
int row(int kind, u32 item, const char *spec);
u32 hold(int kind, u32 item, const char *spec);
u32 lend(u32 item, const char *spec);
void raw_num(int t, u32 recno, const char *name, i32 num);
void check_counts(const char *file, int line);
const char *ids(int over, u32 item, u32 host, int kind, int open,
                const char *asof, u16 page, int *pages);

/* Check every live item's counters against the open rows, reporting a
   failure at the line of the check. */
#define COUNTS_OK() check_counts(TF_FILE, __LINE__)

/* The phases of T_LOAN2.C. */
void test_holds(void);
void test_hset(void);
void test_hinstall(void);
void test_hlists(void);
void test_hheal(void);
void test_hmany(void);

#endif
