/*
 * T_ENGIN4.C - the fourth part of the T_ENGINE test (see T_ENGINE.C):
 * the texts phase, the one that reads the numbered texts (ENGTX.H), and
 * the tags phase.  It is a file of its own only to stay within the
 * compiler memory of Watcom C 6.5; T_ENGINE.H holds what the parts
 * share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGTX.H"
#include "ENGITEM.H"
#define TF_PART
#define TF_FILE "T_ENGIN4"
/* Checks keep no file name or expression text, which Turbo C and the
   Windows build would keep in near data. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#include "TESTFW.H"
#include "TENGH.H"
#include "T_ENGINE.H"

#define LONGEST         4       /* texts of ENG_TXMAX characters        */

/* The text of ENG_TX_BADTAG, which states ENG_ITAGLEN. */
static const char k_badtag[] = "each tag 1-12 of A-Z a-z 0-9 - _ . / +";

/* ---------------------------------------------------------------------
 * Texts.
 * --------------------------------------------------------------------- */

/* Numbered texts by the numbers a client may hold for them: the first,
   one that states ENG_MAXDEPTH, the edit marker's name, one that states
   ENG_FILES and is one of the longest, that of an unnamed error, another
   of the longest, those that state ENG_MAXLOCDEPTH and ENG_ITAGLEN, and
   the last. */
static const struct {
    u16 tx;
    const char *text;
} x_texts[] = {
    { 1, "cannot be put inside itself or its contents" },
    { 2, "parts nested more than 16 levels deep" },
    { 51, ENG_EDITMARK },
    { 130, "file handles exhausted: need FILES=45 or more" },
    { 137, "error" },
    { 148, "location holds locations: move or delete them" },
    { 150, "locations nested more than 8 levels deep" },
    { 155, "name who or which item it is for" },
    { 156, k_badtag },
    { 157, "works must be Y, N, I or ?" }
};

/* Every text whole and not empty, the samples by number, "" outside
   1..ENG_NTX; the text, number and field a failure records, until
   eng_clear_error.  Exactly LONGEST texts reach ENG_TXMAX: one longer,
   cut to ENG_TXMAX by eng_tx_copy, would make another. */
void test_texts(void)
{
    char buf[ENG_TXMAX + 2];
    int longest;
    u16 i;

    longest = 0;
    for (i = 1; i <= ENG_NTX; i++) {
        memset(buf, '#', sizeof buf);
        eng_tx_copy(i, buf);
        if (!TF_CHECK(buf[0] != '\0' && buf[ENG_TXMAX + 1] == '#'))
            printf("  text %u\n", i);
        if (strlen(buf) == ENG_TXMAX)
            longest++;
    }
    TF_EQ(longest, LONGEST);
    for (i = 0; i < sizeof x_texts / sizeof x_texts[0]; i++) {
        eng_tx_copy(x_texts[i].tx, buf);
        TF_STREQ(buf, x_texts[i].text);
    }
    eng_tx_copy(ENG_TX_NONE, buf);
    TF_STREQ(buf, "");
    eng_tx_copy(ENG_NTX + 1, buf);
    TF_STREQ(buf, "");

    if (!phase_start())
        return;
    TF_EQ(eng_fail(g_e, ENG_ECONSTR, "QTY", ENG_TX_NOSTOCK), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "QTY");
    TF_STREQ(eng_errmsg(g_e), "not enough on hand");
    TF_EQ(eng_errtx(g_e), ENG_TX_NOSTOCK);
    TF_EQ(eng_fail(g_e, ENG_EARG, NULL, ENG_NTX + 1), ENG_EARG);
    TF_STREQ(eng_errmsg(g_e), "");
    eng_clear_error(g_e);
    TF_EQ(eng_errtx(g_e), ENG_TX_NONE);
    TF_STREQ(eng_errmsg(g_e), "");
    phase_end();
}

/* ---------------------------------------------------------------------
 * Tags.
 * --------------------------------------------------------------------- */
#define NFULL           16      /* items of five tags (60 bytes of the
                                   set each) before the edge            */

/* A TAGS of ENG_FW_ITEMS_TAGS characters whose tags take the most bytes
   of the set of ITEM TAGS: the width and a NUL. */
static const char k_worst[] =
    "b00000000001,b00000000002,b00000000003,b00000000004,b0000005";

static char g_tcur[ENG_CURSORMAX];

/* ITEM TAGS after the cursor after (which may be g_tcur): its tags
   joined by ',' in eng_memobuf, its cursor into g_tcur; "ERR" when it
   fails, "BAD" when the set is more than ENG_TAGSET bytes or a tag is
   not ended by a NUL. */
static const char *tags(const char *after)
{
    char from[ENG_CURSORMAX];
    char *b;
    u16 len;
    u16 i;

    strcpy(from, after != NULL ? after : "");
    if (eng_item_tags(g_e, from, &len, g_tcur) != ENG_OK) {
        eng_clear_error(g_e);
        return "ERR";
    }
    b = eng_memobuf(g_e);
    if (len > ENG_TAGSET || (len > 0 && b[len - 1] != '\0'))
        return "BAD";
    for (i = 0; i + 1 < len; i++)
        if (b[i] == '\0')
            b[i] = ',';
    b[len > 0 ? len - 1 : 0] = '\0';
    return b;
}

/* 1 when live item id passes the filters TAG= tag and WORKS= works. */
static int kept(u32 id, const char *tag, const char *works)
{
    eng_ifilt f;
    u32 r;

    return eng_item_filters(g_e, &f, NULL, NULL, NULL, tag, works, 0) ==
           ENG_OK && eng_item_get(g_e, id, &r) == ENG_OK &&
           eng_item_keep(g_e, &f);
}

/* The tags of items from to to of the edge test, joined by commas, into
   out: five tags of ENG_ITAGLEN - 1 characters an item, "a011bcdefgh"
   to "a015bcdefgh" for item 1, so that an item's tags take 60 bytes of
   the set. */
static void five_tags(char *out, u16 from, u16 to)
{
    u16 j;

    out[0] = '\0';
    for (; from <= to; from++)
        for (j = 1; j <= 5; j++)
            sprintf(out + strlen(out), "%sa%02u%ubcdefgh",
                    out[0] != '\0' ? "," : "", (unsigned)from, (unsigned)j);
}

/* ITEM TAGS: each tag once, A-Z folded, in its first spelling, in A-Z
   folded order; values another program left (spaces, any case, a word
   that is no tag) matched, folded or skipped as the rules say, deleted
   records passed over; ENG_MAXREADS records a request. */
void test_tags(void)
{
    char spec[TE_TEXTMAX];
    u32 id;
    u16 len;

    if (!phase_start())
        return;
    add_item("CATEGORY=SYS|TAGS=Alpha beta|WORKS=Y");
    add_item("CATEGORY=SYS|TAGS=ALPHA,gamma");
    id = add_item("CATEGORY=SYS");
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, "TAGS",
                     " Delta , bad*tag,EPSILON  ", 0), ENG_OK);
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, "WORKS", "y", 0),
          ENG_OK);
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, "COND", "gd", 0),
          ENG_OK);
    id = add_item("CATEGORY=SYS|TAGS=zeta");
    raw_delete(id);
    TF_STREQ(tags(NULL), "Alpha,beta,Delta,EPSILON,gamma");
    TF_STREQ(g_tcur, "");
    TF_STREQ(tags("ITEMS::2"), "Delta,EPSILON");
    TF_STREQ(tags("ITEMS::4"), "");
    TF_STREQ(g_tcur, "");
    TE_FAIL(eng_item_tags(g_e, "ITEMS:41:1", &len, g_tcur), ENG_ECURSOR,
            NULL, NULL);
    TF_CHECK(kept(3, "delta,epsilon", "Y"));
    TF_CHECK(kept(1, "alpha", "y?"));
    TF_CHECK(!kept(3, "bad", NULL));
    TF_CHECK(!kept(2, "alpha,beta", NULL));
    TF_CHECK(!kept(2, NULL, "Y"));
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(2), -1, "WORKS", "", 0), ENG_OK);
    TF_CHECK(!kept(2, NULL, "YIN?"));
    /* The next commit folds a lower-case COND or WORKS, with its U row; a
       word that is no tag, or another COND or WORKS, blocks it until the
       field is set right. */
    TE_FAIL(eng_item_set(g_e, 3, te_p, te_kv("MFR=X")), ENG_EARG, "TAGS",
            k_badtag);
    TE_FAIL(eng_item_set(g_e, 3, te_p, te_kv("TAGS=Delta,bad*tag")),
            ENG_EARG, "TAGS", NULL);
    OK(eng_item_set(g_e, 3, te_p, te_kv("TAGS=Delta,,bad")));
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(3), "COND"), "GD");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(3), "WORKS"), "Y");
    TF_STREQ(te_rtext(ENG_T_ITEMS, te_irec(3), "TAGS"), "Delta,bad");
    TF_STREQ(aud(3), "ITEMS|3|U|COND|gd|GD");
    TF_STREQ(aud(2), "ITEMS|3|U|WORKS|y|Y");
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(3), -1, "COND", "ut", 0), ENG_OK);
    TE_FAIL(eng_item_set(g_e, 3, te_p, te_kv("MFR=X")), ENG_EARG, "COND",
            NULL);
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(3), -1, "WORKS", "x", 0), ENG_OK);
    TE_FAIL(eng_item_set(g_e, 3, te_p, te_kv("COND=FR")), ENG_EARG, "WORKS",
            NULL);
    OK(eng_item_set(g_e, 3, te_p, te_kv("COND=FR|WORKS=n")));

    /* ENG_MAXREADS records a request, and the rest in the next. */
    raw_copies(1, ENG_MAXREADS);
    add_item("CATEGORY=SYS|TAGS=omega");
    TF_STREQ(tags(NULL), "Alpha,bad,beta,Delta,gamma");
    TF_STREQ(g_tcur, "ITEMS::C8");
    TF_STREQ(tags(g_tcur), "Alpha,beta,omega");
    TF_STREQ(g_tcur, "");
    phase_end();

    /* ENG_MAXREADS untagged records give a request of no tags but a
       cursor.  The next reads the following record while the set has
       room for a record's worst case, here that of k_worst, which fills
       the set to ENG_TAGSET bytes; one byte less room ends the request
       before that record, which the next request resumes at.  The pages
       are compared in g_big, which holds ENG_TAGSET bytes. */
    if (!phase_start())
        return;
    TF_EQ(NFULL * 60 + sizeof "zz" + sizeof k_worst, ENG_TAGSET);
    raw_copies(add_item("CATEGORY=SYS"), ENG_MAXREADS - 1);
    for (len = 1; len <= NFULL; len++) {
        strcpy(spec, "CATEGORY=SYS|TAGS=");
        five_tags(spec + strlen(spec), len, len);
        add_item(spec);
    }
    id = add_item("CATEGORY=SYS|TAGS=zz");
    strcpy(spec, "CATEGORY=SYS|TAGS=");
    strcat(spec, k_worst);
    add_item(spec);
    add_item("CATEGORY=SYS|TAGS=omega");
    TF_STREQ(tags(NULL), "");
    TF_STREQ(g_tcur, "ITEMS::C8");
    five_tags(g_big, 1, NFULL);
    strcat(g_big, ",");
    strcat(g_big, k_worst);
    strcat(g_big, ",zz");
    TF_STREQ(tags(g_tcur), g_big);
    TF_STREQ(g_tcur, "ITEMS::DA");
    TF_STREQ(tags(g_tcur), "omega");
    TF_STREQ(g_tcur, "");
    TF_EQ(te_raw_put(ENG_T_ITEMS, te_irec(id), -1, "TAGS", "zzz", 0),
          ENG_OK);
    five_tags(g_big, 1, NFULL);
    strcat(g_big, ",zzz");
    TF_STREQ(tags("ITEMS::C8"), g_big);
    TF_STREQ(g_tcur, "ITEMS::D9");
    strcpy(spec, k_worst);
    strcat(spec, ",omega");
    TF_STREQ(tags(g_tcur), spec);
    TF_STREQ(g_tcur, "");
    phase_end();
}
