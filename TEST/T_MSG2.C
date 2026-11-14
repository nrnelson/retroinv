/*
 * T_MSG2.C - the second half of the T_MSG test (T_MSG.H): the far
 * blocks, the reads and the closes of msg_load, a load failing at each
 * of them, a file changed between its two reads, and the text helpers
 * msg_date and msg_dec.  The header comment of T_MSG.C lists the checks.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#define TF_PART
#define TF_FILE "T_MSG2"
#include "TESTFW.H"
#include "T_MSG.H"

/* The far blocks of a msg_load, as cmem_fault and cmem_freeing see
   them: the size of each one allocated, in order, and of those not yet
   freed, newest last.  g_holes counts the blocks freed while a newer
   one lived, told by their sizes: each would leave a hole under the
   newer one. */
static u16 g_asked[AL_MAX];
u16 g_live[AL_MAX];
static u16 g_nasked;
static u16 g_nlive;
static u16 g_holes;
static u16 g_refuse;            /* the allocation refused, from 1, or 0 */

/* COMPAT.H's free hook, declared here: the section it stands in is more
   than Watcom C 6.5 has memory for on top of this file. */
extern void (*cmem_freeing)(cmem_blk *b);

static int al_alloc(u16 size)
{
    if (g_nasked >= AL_MAX || g_nlive >= AL_MAX)
        return 1;
    g_asked[g_nasked++] = size;
    if (g_nasked == g_refuse)
        return 1;
    g_live[g_nlive++] = size;
    return 0;
}

static void al_free(cmem_blk *b)
{
    if (g_nlive == 0 || g_live[g_nlive - 1] != cmem_size(b))
        g_holes++;
    if (g_nlive != 0)
        g_nlive--;
}

/* Watch the far blocks from here on; nothing of MSG.C's is live. */
static void al_begin(u16 refuse)
{
    msg_free();
    g_nasked = 0;
    g_nlive = 0;
    g_holes = 0;
    g_refuse = refuse;
    cmem_fault = al_alloc;
    cmem_freeing = al_free;
}

static void al_end(void)
{
    cmem_fault = NULL;
    cmem_freeing = NULL;
}

/* The reads and closes of a msg_load, as cf_fault_rd and cf_fault see
   them; each pass takes one read, the second pass read RD_PASS2.  Read
   g_failread, from 1, fails; before read RD_PASS2, the file is
   overwritten in place, after its stamp line, with g_swap, a text as
   long as the one it had, when DOS lets the file be opened for that:
   msg_load's handle denies it where file sharing is in force. */
#define RD_PASS2        2
static u16 g_reads;
static u16 g_closes;
static u16 g_failread;
static const char *g_swap;
static int g_swapped;

static int count_close(int op)
{
    if (op == CF_OP_CLOSE)
        g_closes++;
    return 0;
}

static int rd_hook(int op)
{
    char path[PATHMAX + 16];
    int h;

    if (op != CF_OP_READ)
        return 0;
    g_reads++;
    if (g_reads == g_failread)
        return CF_EACCES;
    if (g_reads == RD_PASS2 && g_swap != NULL) {
        strcpy(path, g_tdir);
        strcat(path, MSG_FILE);
        cf_fault = NULL;
        if (cf_open(path, CF_RDWR, &h) == 0) {
            g_swapped = cf_seek(h, STAMPLEN) == 0 &&
                        cf_write(h, g_swap, (u16)strlen(g_swap)) == 0;
            cf_close(h);
        }
        cf_fault = count_close;
    }
    return 0;
}

int load_watched(const char *text, u16 refuse, u16 failread,
                 const char *swap)
{
    int rc;

    TF_EQ(write_file(MSG_FILE, g_stamp, text), 0);
    al_begin(refuse);
    g_reads = 0;
    g_closes = 0;
    g_failread = failread;
    g_swap = swap;
    g_swapped = 0;
    cf_fault_rd = rd_hook;
    cf_fault = count_close;
    rc = msg_load(g_tdir, MSG_FILE);
    cf_fault = NULL;
    cf_fault_rd = NULL;
    al_end();
    TF_EQ(g_closes, 1);
    TF_EQ(g_holes, 0);
    TF_EQ(g_nlive, rc == 0 ? 1 : 0);
    return rc;
}

#define IDX_RANGES      25      /* the MSG_R_ ranges of MSG.H           */
#define IDX_ENTRY       8       /* bytes of a range's index entry       */

/* msg_load frees its blocks last in first out: the block of the whole
   file before the block of the run is allocated, and the block of the
   second read, which ends with the last line of a range that stays
   loaded wherever the file has it, comes and goes after that.  Where
   the blocks lie in the far heap, which this test does not see, is
   T_MSGLO's to check. */
void test_blocks(const char *mdir)
{
    static const char shared[] = "200 cd\n1 ab\n";
    static const char pages[] = "400 x\n450 yz\n";
    char want[8];
    u16 idx;
    u16 n;

    /* The block of the run: the index alone without a text; with the
       loaded ranges their two pages, each a table entry and a text of
       two bytes with its NUL; with a paged range the slots and the read
       area too, as long as the longer of the two ranges' lines. */
    TF_EQ(load_watched("# none\n", 0, 0, NULL), 0);
    TF_EQ(g_nasked, 2);
    TF_STREQ(msg(1), unknown(want, 1));
    idx = g_live[0];
    TF_EQ(idx, IDX_RANGES * IDX_ENTRY);
    TF_EQ(load_watched(shared, 0, 0, NULL), 0);
    TF_EQ(g_live[0], idx + 2 * (2 + 3));
    n = (u16)(STAMPLEN + sizeof shared - 1);
    strcpy(g_text, shared);
    strcat(g_text, pages);
    TF_EQ(load_watched(g_text, 0, 0, NULL), 0);
    TF_EQ(g_live[0], idx + MSG_NSLOT * MSG_PAGE + 7 + 2 * (2 + 3));
    TF_EQ(g_reads, RD_PASS2);
    TF_EQ(g_nasked, 3);
    TF_EQ(g_asked[0], n + sizeof pages - 1);
    TF_EQ(g_asked[1], g_live[0]);
    TF_EQ(g_asked[2], n);
    TF_STREQ(msg(1), "ab");
    TF_STREQ(msg(200), "cd");
    TF_STREQ(msg(450), "yz");
    /* A loaded range after the pages' lines takes the read up to it. */
    strcpy(g_text, pages);
    strcat(g_text, shared);
    TF_EQ(load_watched(g_text, 0, 0, NULL), 0);
    TF_EQ(g_asked[2], g_asked[0]);
    TF_STREQ(msg(1), "ab");
    TF_STREQ(msg(400), "x");
    /* Without a loaded range there is no second read. */
    TF_EQ(load_watched(pages, 0, 0, NULL), 0);
    TF_EQ(g_live[0], idx + MSG_NSLOT * MSG_PAGE + 7);
    TF_EQ(g_reads, RD_PASS2 - 1);
    TF_EQ(g_nasked, 2);
    TF_STREQ(msg(450), "yz");
    TF_STREQ(msg(1), unknown(want, 1));
    /* The program's own file. */
    al_begin(0);
    TF_EQ(msg_load(mdir, MSG_FILE), 0);
    al_end();
    TF_EQ(g_holes, 0);
    TF_EQ(g_nlive, 1);
    TF_EQ(g_nasked, 3);
    TF_CHECK(g_asked[2] < g_asked[0]);
    msg_free();
}

/* A load that fails at either read or at any of its three blocks says
   why, has closed the file once and leaves no block and no text. */
void test_load_fails(void)
{
    static const char text[] = "1 ab\n200 cd\n400 x\n";
    char want[8];
    u16 i;

    for (i = 1; i <= RD_PASS2; i++) {
        TF_EQ(load_watched(text, 0, i, NULL), CF_EACCES);
        TF_EQ(g_reads, i);
        TF_STREQ(msg_why(), "cannot read the file");
        TF_STREQ(msg(1), unknown(want, 1));
    }
    for (i = 1; i <= 3; i++) {
        TF_EQ(load_watched(text, i, 0, NULL), CF_ENOMEM);
        TF_EQ(g_nasked, i);
        TF_STREQ(msg_why(), "not enough memory");
        TF_STREQ(msg(200), unknown(want, 200));
    }
    TF_EQ(load_watched(text, 0, 0, NULL), 0);
    TF_STREQ(msg_why(), "");
    TF_STREQ(msg(200), "cd");
    msg_free();
}

/* The lines, after the stamp line, of the file whose second pass finds
   others. */
static const char k_before[] = "1 ab\n200 cd\n400 x\n";

/* The file holding swap, as long as k_before, from msg_load's second
   pass on: a page that swap's lines do not fit as the first read sized
   it refuses the load, and nothing is loaded. */
static void refused(const char *swap)
{
    char want[8];

    TF_EQ(strlen(swap), sizeof k_before - 1);
    TF_EQ(load_watched(k_before, 0, 0, swap), CF_EINVAL);
    TF_CHECK(g_swapped);
    TF_STREQ(msg_why(), "cannot read the file");
    TF_STREQ(msg(1), unknown(want, 1));
    TF_STREQ(msg(200), unknown(want, 200));
}

/* A file changed between msg_load's two passes never writes outside the
   pages the first read sized: other texts of the same sizes load, a
   shorter one too; a text one byte longer, a number past the range's
   table, below the range and of another range, and a malformed line
   refuse the load.  The first case shows whether msg_load's handle
   reads what another has written since its first read; the rest run
   only where it does: not where file sharing refuses the write, nor in
   an emulator that keeps each handle's bytes apart. */
void test_swapped(void)
{
    int sees;

    TF_EQ(load_watched(k_before, 0, 0, "1 xy\n200 cd\n400 x\n"), 0);
    sees = g_swapped && strcmp(msg(1), "xy") == 0;
#ifdef COMPAT_HOST
    TF_CHECK(sees);
#endif
    TF_STREQ(msg(1), sees ? "xy" : "ab");
    TF_STREQ(msg(400), "x");
    if (sees) {
        TF_EQ(load_watched(k_before, 0, 0, "1 a\n\n200 cd\n400 x\n"), 0);
        TF_STREQ(msg(1), "a");
        TF_STREQ(msg(200), "cd");
        refused("1 abc\n200 d\n400 x\n");
        refused("2 ab\n200 cd\n400 x\n");
        refused("1 ab\n199 cd\n400 x\n");
        refused("1 ab\n250 cd\n400 x\n");
        refused("1 ^b\n200 cd\n400 x\n");
    }
    msg_free();
}

/* A wire date is exactly MSG_DATELEN digits, shown as YYYY-MM-DD in at
   most MSG_DATEW + 1 bytes; anything else gives 0 and an empty text. */
static void date_is(const char *wire, int ok, const char *want)
{
    char out[MSG_DATEW + 4];

    memset(out, '#', sizeof out);
    out[sizeof out - 1] = '\0';
    TF_EQ(msg_date(out, wire), ok);
    TF_STREQ(out, want);
    TF_EQ(out[MSG_DATEW + 1], '#');
}

void test_dates(void)
{
    TF_EQ(MSG_DATEW, 10);
    TF_EQ(MSG_DATELEN, 8);
    date_is("20260131", 1, "2026-01-31");
    date_is("19991231", 1, "1999-12-31");
    date_is("00000101", 1, "0000-01-01");
    date_is("2026013", 0, "");
    date_is("202601311", 0, "");
    date_is("2026-1-3", 0, "");
    date_is("2026013x", 0, "");
    date_is("x0260131", 0, "");
    date_is("2026 131", 0, "");
    date_is("        ", 0, "");
    date_is("2026-01-31", 0, "");
    date_is("", 0, "");
}

/* What msg_dec should give: digits are taken while the value is below
   cap; *used the digits taken. */
static u16 ref_dec(const char *s, u16 cap, u16 *used)
{
    u32 v;
    u16 k;

    v = 0;
    k = 0;
    while (s[k] >= '0' && s[k] <= '9' && v < cap) {
        v = v * 10 + (u32)(s[k] - '0');
        k++;
    }
    *used = k;
    return (u16)v;
}

static void dec_is(const char *s, u16 cap, u16 want, u16 used)
{
    const char *p;
    int ok;

    p = s;
    ok = TF_EQ(msg_dec(&p, cap), want);
    if (!TF_EQ(p - s, used) || !ok)
        printf("  msg_dec(\"%s\", %u)\n", s, (unsigned)cap);
}

void test_decimals(void)
{
    static const char *const strs[] = {
        "", "x", "-5", "0", "007", "9", "10", "99x", "100", "101", "255",
        "256", "1000", "1234x", "6553", "6554", "65535", "65536",
        "99999999", "0000000000000000000099"
    };
    static const u16 caps[] = { 1, 9, 10, 99, 100, 255, 256, 1000, 6000 };
    u16 i;
    u16 j;
    u16 used;
    u16 want;

    /* With cap up to MSG_DECCAP the value always fits a u16. */
    TF_CHECK((u32)MSG_DECCAP * 10 - 1 <= 0xFFFFUL);
    TF_CHECK(MSG_DECCAP >= 1000);
    dec_is("", 100, 0, 0);
    dec_is("abc", 100, 0, 0);
    dec_is("-5", 100, 0, 0);
    dec_is("42", 100, 42, 2);
    dec_is("42 x", 100, 42, 2);
    dec_is("99x", 100, 99, 2);
    dec_is("100", 100, 100, 3);
    dec_is("1234x", 100, 123, 3);
    dec_is("0005", 10, 5, 4);
    dec_is("57", 1, 5, 1);
    dec_is("07", 1, 7, 2);
    dec_is("65535", 6000, 6553, 4);
    for (i = 0; i < sizeof strs / sizeof strs[0]; i++) {
        for (j = 0; j < sizeof caps / sizeof caps[0]; j++) {
            want = ref_dec(strs[i], caps[j], &used);
            dec_is(strs[i], caps[j], want, used);
        }
        want = ref_dec(strs[i], MSG_DECCAP, &used);
        dec_is(strs[i], MSG_DECCAP, want, used);
    }
}
