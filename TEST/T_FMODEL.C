/*
 * T_FMODEL.C - the form model (SRC/CLIENT/FMODEL.C), tested from the
 * contract of FMODEL.H alone.
 *
 * usage: T_FMODEL dir
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/FMODT/") for the
 * LOCAL link the test needs for form_save; its engine files are removed
 * first and last.  The test links the recording link of TRLINK.H instead
 * of LINK.C and answers every request itself through tl_hook, so the
 * engine is never asked anything and the request lines of a save are
 * checked exactly.  The form tables are the test's own.
 *
 * Covered:
 * - form_new (NULL for too many or too wide fields, for no memory, with no
 *   block kept; the form_model's members, the zeroed rest of the block,
 *   empty values, slots that do not overlap);
 * - form_valsz, form_load and form_kline (over a decoded +DATA response),
 *   form_put, form_get, form_get_text and the slot edited in place,
 *   form_dirty and form_saved;
 * - form_check for every MF_ code (required, number sign, width and
 *   decimals, non-negative, real dates, logic values, choices, the fields
 *   that always pass, the first failing field in table order);
 * - form_save (an ADD with its key and required fields, a SET, quoting, the
 *   fields never sent, nothing changed, the split of long changes into
 *   further SETs at exactly LINK_MAXLINE, an ADD that fails, a later SET
 *   that fails and the repeated save, a pair or a key too long for any
 *   request, partial set only after an ADD that went, an ADD without an
 *   ID);
 * - form_err_field, form_find and form_key, the choice helpers, form_close,
 *   form_wipe and the wiping of secret values;
 * - that the model reads neither labels, positions nor display widths.
 */
#define FMODEL_CALLS
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "FMODEL.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_MALLOC
#define TF_CLOCK
#include "TESTFW.H"

#define PATHMAX         96

struct form {
    form_model m;
    char tail[8];               /* the client's own part, after the model */
};

#define NEWF(def, mode) form_new(def, mode, sizeof(struct form))

/* A field table row, plain and with every drawing member set (label,
   row, col, vcol, dispw) to something else. */
#define PLAIN(n, t, w, d, fl, ch) { n, 0, 0, 0, 0, t, w, d, fl, 0, ch }
#define DRAWN(n, t, w, d, fl, ch) { n, 901, 7, 3, 11, t, w, d, fl, 1, ch }

/* An item-like form: a read-only ID, a required code first, then the
   other types, a memo, a local field and a money field. */
#define ITEM_FIELDS(F) \
    F("ID", FT_NUM, 8, 0, FF_RO, NULL), \
    F("CAT", FT_CODE, 4, 0, FF_REQ, NULL), \
    F("MFR", FT_CHAR, 30, 0, 0, NULL), \
    F("DESCR", FT_CHAR, 60, 0, 0, NULL), \
    F("QTY", FT_NUM, 5, 0, 0, NULL), \
    F("ACQ", FT_DATE, 8, 0, 0, NULL), \
    F("STATUS", FT_CHOICE, 1, 0, 0, "A D"), \
    F("OK", FT_LOGIC, 1, 0, 0, NULL), \
    F("NOTES", FT_MEMO, 3, 0, 0, NULL), \
    F("TMP", FT_CHAR, 10, 0, FF_LOCAL, NULL), \
    F("PRICE", FT_NUM, 9, 2, FF_NONNEG, NULL)

enum { I_ID, I_CAT, I_MFR, I_DESCR, I_QTY, I_ACQ, I_STATUS, I_OK, I_NOTES,
       I_TMP, I_PRICE, I_N };

static const form_field item_f[] = { ITEM_FIELDS(PLAIN) };
static const form_field item_g[] = { ITEM_FIELDS(DRAWN) };
static const form_def item_def = { 1, "a", 1, 2, 3, 4, I_N, item_f };
static const form_def item_gef = { 77, "zz", 9, 8, 7, 6, I_N, item_g };

/* A code list's entry: the key is not the first field. */
static const form_field code_f[] = {
    PLAIN("SORTORD", FT_CHAR, 10, 0, 0, NULL),
    PLAIN("CODE", FT_CHAR, 6, 0, FF_KEY | FF_REQ | FF_UPPER, NULL),
    PLAIN("NAME", FT_CHAR, 30, 0, 0, NULL)
};
static const form_def code_def = { 1, "a", 1, 2, 3, 4, 3, code_f };

/* A user: a secret and a key, the required fields first. */
static const form_field user_f[] = {
    PLAIN("NAME", FT_CHAR, 8, 0, FF_KEY | FF_REQ, NULL),
    PLAIN("PASSWORD", FT_SECRET, 16, 0, FF_REQ, NULL),
    PLAIN("LEVEL", FT_NUM, 1, 0, 0, NULL),
    PLAIN("FULLNAME", FT_CHAR, 30, 0, 0, NULL)
};
static const form_def user_def = { 1, "a", 1, 2, 3, 4, 4, user_f };
enum { U_NAME, U_PW, U_LEVEL, U_FULL };

/* The table of the long requests: its name, of BIG_LEN characters,
   makes three of the 60-character pairs below fill a request and a
   fourth pass LINK_MAXLINE, and a FORM_MAXVAL value of escapes alone
   too long for any request (test_save_toolong). */
#define BIG_LEN         (LINK_MAXLINE - 252)
static char g_big[BIG_LEN + 1];

/* Five 60-character fields after a required code: three of them fill an
   ADD, the other two (or one) follow in a SET. */
static const form_field big_f[] = {
    PLAIN("CAT", FT_CODE, 4, 0, FF_REQ, NULL),
    PLAIN("A", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("B", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("C", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("D", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("E", FT_CHAR, 60, 0, 0, NULL)
};
static const form_def big_def = { 1, "a", 1, 2, 3, 4, 6, big_f };

/* A short field after the long ones: it fits where a long one does not. */
static const form_field pass_f[] = {
    PLAIN("CAT", FT_CODE, 4, 0, FF_REQ, NULL),
    PLAIN("A", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("B", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("C", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("D", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("S", FT_CHAR, 10, 0, 0, NULL)
};
static const form_def pass_def = { 1, "a", 1, 2, 3, 4, 6, pass_f };

/* ... and one that is last, 44 characters of which make an ADD of exactly
   LINK_MAXLINE bytes. */
static const form_field edge_f[] = {
    PLAIN("CAT", FT_CODE, 4, 0, FF_REQ, NULL),
    PLAIN("A", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("B", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("C", FT_CHAR, 60, 0, 0, NULL),
    PLAIN("X", FT_CHAR, 60, 0, 0, NULL)
};
static const form_def edge_def = { 1, "a", 1, 2, 3, 4, 5, edge_f };

/* A field whose value as \xHH escapes cannot fit any request. */
static const form_field lng_f[] = {
    PLAIN("CAT", FT_CODE, 4, 0, FF_REQ, NULL),
    PLAIN("S", FT_CHAR, 10, 0, 0, NULL),
    PLAIN("W", FT_CHAR, FORM_MAXVAL, 0, 0, NULL)
};
static const form_def lng_def = { 1, "a", 1, 2, 3, 4, 3, lng_f };
static const form_field lngr_f[] = {
    PLAIN("W", FT_CHAR, FORM_MAXVAL, 0, FF_REQ, NULL)
};
static const form_def lngr_def = { 1, "a", 1, 2, 3, 4, 1, lngr_f };
static const form_field lngk_f[] = {
    PLAIN("W", FT_CHAR, FORM_MAXVAL, 0, FF_KEY, NULL)
};
static const form_def lngk_def = { 1, "a", 1, 2, 3, 4, 1, lngk_f };

/* No key. */
static const form_field nokey_f[] = {
    PLAIN("CAT", FT_CODE, 4, 0, FF_REQ, NULL),
    PLAIN("MFR", FT_CHAR, 30, 0, 0, NULL)
};
static const form_def nokey_def = { 1, "a", 1, 2, 3, 4, 2, nokey_f };

/* Two keys. */
static const form_field two_f[] = {
    PLAIN("A", FT_CHAR, 4, 0, 0, NULL),
    PLAIN("K1", FT_CHAR, 4, 0, FF_KEY, NULL),
    PLAIN("K2", FT_CHAR, 4, 0, FF_KEY, NULL)
};
static const form_def two_def = { 1, "a", 1, 2, 3, 4, 3, two_f };

/* One field of the test's choosing, for the type checks. */
static form_field one_f[1];
static const form_def one_def = { 1, "a", 1, 2, 3, 4, 1, one_f };

static link *g_lk;

/* The responses to the requests of a save, by request number (1 is the
   first); NULL is "+OK", RESP_END ends the session at that request. */
static const char *g_rs[4];
static const char resp_end[] = "";

static void script(const char *line, u16 len)
{
    u16 n;
    const char *r;

    if (len == 0 || line[0] == '\0')
        return;
    n = tl_count();
    r = n <= 4 ? g_rs[n - 1] : NULL;
    if (r == resp_end)
        r = NULL;
    else if (r == NULL)
        r = "+OK";
    tl_fail(n, r);
}

static void resp(const char *a, const char *b, const char *c)
{
    g_rs[0] = a;
    g_rs[1] = b;
    g_rs[2] = c;
    g_rs[3] = NULL;
    tl_clear();
}

static form *mk(const form_def *def, u8 mode)
{
    form *f;

    f = NEWF(def, mode);
    TF_CHECK(f != NULL);
    return f;
}

static int res_zero(const rq_res *r)
{
    return r->code == 0 && r->data == 0 && r->closed == 0 &&
           r->text[0] == '\0' && r->field[0] == '\0' && r->more[0] == '\0';
}

static int save(form *f, const char *table, char *id, u16 idsz, rq_res *res)
{
    memset(res, 0xFF, sizeof *res);
    return form_save(f, g_lk, table, id, idsz, res);
}

static void fill(char *d, char c, u16 n)
{
    memset(d, c, n);
    d[n] = '\0';
}

static void test_new(void)
{
    form_field many[FORM_MAXFIELDS + 1];
    char names[FORM_MAXFIELDS + 1][4];
    form_def md;
    form *f;
    struct form *s;
    u16 i;
    u16 held;
    u16 sum;
    char big[FORM_MAXVAL + 3];

    held = tm_held();
    f = NEWF(&item_def, FORM_NEW);
    if (!TF_CHECK(f != NULL))
        return;
    s = (struct form *)f;
    TF_CHECK(s->m.def == &item_def);
    TF_EQ(s->m.mode, FORM_NEW);
    TF_EQ(s->m.partial, 0);
    TF_EQ(form_mode_get(f), FORM_NEW);
    for (i = 0; i < sizeof s->tail; i++)
        TF_EQ(s->tail[i], 0);
    sum = 0;
    for (i = 0; i < I_N; i++) {
        TF_EQ(form_val(f, i)[0], '\0');
        TF_EQ(s->m.org[s->m.off[i]], '\0');
        TF_CHECK(form_val(f, i) == s->m.val + s->m.off[i]);
        TF_CHECK(s->m.off[i] + form_valsz(&item_f[i]) <= s->m.size);
        sum = (u16)(sum + form_valsz(&item_f[i]));
    }
    TF_CHECK(s->m.size >= sum);
    form_close(f);
    TM_HELD(held);

    /* The modes. */
    f = mk(&item_def, FORM_VIEW);
    if (f != NULL) {
        TF_EQ(form_mode_get(f), FORM_VIEW);
        form_mode_set(f, FORM_EDIT);
        TF_EQ(form_mode_get(f), FORM_EDIT);
        form_close(f);
    }
    f = mk(&item_def, FORM_EDIT);
    if (f != NULL) {
        TF_EQ(form_mode_get(f), FORM_EDIT);
        form_close(f);
    }

    /* The largest form and the largest value are fine, one more is not,
       and a refusal keeps no block. */
    memset(many, 0, sizeof many);
    for (i = 0; i <= FORM_MAXFIELDS; i++) {
        sprintf(names[i], "F%u", (unsigned)i);
        many[i].name = names[i];
        many[i].type = FT_CHAR;
        many[i].width = 1;
    }
    memset(&md, 0, sizeof md);
    md.fields = many;
    md.nfields = FORM_MAXFIELDS;
    f = NEWF(&md, FORM_NEW);
    if (TF_CHECK(f != NULL)) {
        TF_EQ(form_find(f, "F23"), FORM_MAXFIELDS - 1);
        form_close(f);
    }
    TM_HELD(held);
    md.nfields = FORM_MAXFIELDS + 1;
    TF_CHECK(NEWF(&md, FORM_NEW) == NULL);
    TM_HELD(held);

    md.nfields = 2;
    many[1].width = FORM_MAXVAL;
    f = NEWF(&md, FORM_NEW);
    if (TF_CHECK(f != NULL)) {
        fill(big, 'q', FORM_MAXVAL + 2);
        TF_EQ(form_put(f, "F1", big), 0);
        TF_EQ(strlen(form_get(f, "F1")), FORM_MAXVAL);
        form_close(f);
    }
    TM_HELD(held);
    many[1].width = FORM_MAXVAL + 1;
    TF_CHECK(NEWF(&md, FORM_NEW) == NULL);
    TM_HELD(held);

    /* A wide field after a secret one: nothing kept for form_wipe. */
    many[0].type = FT_SECRET;
    TF_CHECK(NEWF(&md, FORM_NEW) == NULL);
    form_wipe();
    TM_HELD(held);

    /* No memory. */
#ifdef COMPAT_HOST
    tm_fail(1);
    TF_CHECK(NEWF(&item_def, FORM_NEW) == NULL);
    tm_fail(0);
#endif
    TM_HELD(held);

    form_close(NULL);
    TM_HELD(held);
}

static void test_valsz(void)
{
    form_field d;

    memset(&d, 0, sizeof d);
    d.type = FT_CHAR;
    d.width = 30;
    TF_EQ(form_valsz(&d), 31);
    d.type = FT_NUM;
    d.width = 9;
    TF_EQ(form_valsz(&d), 10);
    d.type = FT_SECRET;
    d.width = 16;
    TF_EQ(form_valsz(&d), 17);
    d.type = FT_DATE;
    d.width = 3;
    TF_EQ(form_valsz(&d), FORM_DATELEN + 1);
    d.width = 20;
    TF_EQ(form_valsz(&d), FORM_DATELEN + 1);
    d.type = FT_MEMO;
    d.width = 2;
    TF_EQ(form_valsz(&d), FORM_MEMOW + 1);
    d.width = FORM_MEMOW;
    TF_EQ(form_valsz(&d), FORM_MEMOW + 1);
    d.width = FORM_MEMOW + 4;
    TF_EQ(form_valsz(&d), FORM_MEMOW + 5);
}

static void test_values(void)
{
    form *f;
    char buf[80];
    char c[2];
    u16 i;
    u16 k;
    u16 n;

    f = mk(&item_def, FORM_EDIT);
    if (f == NULL)
        return;

    /* Load: the original, cut to fit, and the current value with it. */
    TF_EQ(form_load(f, "MFR", "IBM"), 0);
    TF_STREQ(form_get(f, "MFR"), "IBM");
    TF_EQ(form_dirty(f), 0);
    TF_EQ(form_load(f, "NOPE", "x"), -1);
    TF_EQ(form_load(f, "mfr", "x"), -1);
    TF_EQ(form_dirty(f), 0);
    fill(buf, 'z', 70);
    TF_EQ(form_load(f, "DESCR", buf), 0);
    TF_EQ(strlen(form_get(f, "DESCR")), 60);
    TF_EQ(form_load(f, "ACQ", "202412311"), 0);
    TF_STREQ(form_get(f, "ACQ"), "20241231");
    TF_EQ(form_load(f, "NOTES", "123456789"), 0);
    TF_EQ(strlen(form_get(f, "NOTES")), FORM_MEMOW);
    TF_EQ(form_load(f, "NOTES", "42"), 0);
    TF_STREQ(form_get(f, "NOTES"), "42");
    TF_EQ(form_dirty(f), 0);

    /* Put: a change. */
    TF_EQ(form_put(f, "MFR", "Compaq"), 0);
    TF_STREQ(form_get(f, "MFR"), "Compaq");
    TF_EQ(form_dirty(f), 1);
    TF_EQ(form_put(f, "NOPE", "x"), -1);
    TF_EQ(form_put(f, "MFR", "IBM"), 0);
    TF_EQ(form_dirty(f), 0);
    TF_EQ(form_put(f, "MFR", ""), 0);
    TF_STREQ(form_get(f, "MFR"), "");
    TF_EQ(form_dirty(f), 1);
    TF_EQ(form_put(f, "MFR", "x"), 0);
    TF_EQ(form_load(f, "MFR", "y"), 0);
    TF_STREQ(form_get(f, "MFR"), "y");
    TF_EQ(form_dirty(f), 0);
    fill(buf, 'm', 40);
    TF_EQ(form_put(f, "MFR", buf), 0);
    TF_EQ(strlen(form_get(f, "MFR")), 30);
    TF_EQ(form_dirty(f), 1);
    form_saved(f);
    TF_EQ(form_dirty(f), 0);
    TF_EQ(strlen(form_get(f, "MFR")), 30);

    /* A memo's count is never a change. */
    TF_EQ(form_put(f, "NOTES", "7"), 0);
    TF_STREQ(form_get(f, "NOTES"), "7");
    TF_EQ(form_dirty(f), 0);

    /* The slot, edited in place. */
    strcpy(form_val(f, I_DESCR), "inplace");
    TF_STREQ(form_get(f, "DESCR"), "inplace");
    TF_EQ(form_dirty(f), 1);
    form_saved(f);
    TF_EQ(form_dirty(f), 0);

    /* A read-only and a local field are values like the others. */
    TF_EQ(form_put(f, "ID", "5"), 0);
    TF_STREQ(form_get(f, "ID"), "5");
    TF_EQ(form_dirty(f), 1);
    form_saved(f);
    TF_EQ(form_put(f, "TMP", "t"), 0);
    TF_EQ(form_dirty(f), 1);
    form_saved(f);

    /* get. */
    TF_CHECK(form_get(f, "NOPE") == NULL);
    TF_CHECK(form_get(f, "") == NULL);

    /* get_text, cut to fit. */
    TF_EQ(form_put(f, "MFR", "Hewlett"), 0);
    memset(buf, 'x', sizeof buf);
    form_get_text(f, "MFR", buf, sizeof buf);
    TF_STREQ(buf, "Hewlett");
    form_get_text(f, "MFR", buf, 4);
    TF_STREQ(buf, "Hew");
    form_get_text(f, "MFR", buf, 8);
    TF_STREQ(buf, "Hewlett");
    form_get_text(f, "MFR", buf, 7);
    TF_STREQ(buf, "Hewlet");
    c[0] = 'x';
    c[1] = 'y';
    form_get_text(f, "MFR", c, 1);
    TF_EQ(c[0], '\0');
    TF_EQ(c[1], 'y');
    form_get_text(f, "NOPE", buf, sizeof buf);
    TF_STREQ(buf, "");
    memset(buf, 'x', sizeof buf);
    form_get_text(f, "NOPE", buf, 3);
    TF_STREQ(buf, "");

    /* Every slot holds its own value: a full one in each, none touching
       the next. */
    for (i = 0; i < I_N; i++) {
        fill(buf, (char)('a' + i), 70);
        TF_EQ(form_put(f, item_f[i].name, buf), 0);
    }
    for (i = 0; i < I_N; i++) {
        const char *v;

        v = form_get(f, item_f[i].name);
        n = (u16)(form_valsz(&item_f[i]) - 1);
        TF_EQ(strlen(v), n);
        for (k = 0; k < n && v[k] == 'a' + i; k++)
            ;
        TF_EQ(k, n);
    }
    form_close(f);
}

static void test_kline(void)
{
    form *f;
    rq q;
    rq_res res;
    rq_line ln;

    f = mk(&item_def, FORM_EDIT);
    if (f == NULL)
        return;
    memset(&ln, 0, sizeof ln);
    ln.kind = 'K';
    ln.n = 2;
    ln.v[0] = (char *)"MFR";
    ln.v[1] = (char *)"Zenith";
    TF_EQ(form_kline(f, &ln), 0);
    TF_STREQ(form_get(f, "MFR"), "Zenith");
    TF_EQ(form_dirty(f), 0);
    ln.v[0] = (char *)"NOSUCH";
    TF_EQ(form_kline(f, &ln), 0);
    TF_EQ(form_dirty(f), 0);
    ln.kind = 'L';
    ln.n = 1;
    ln.v[0] = (char *)"MFR";
    ln.v[1] = (char *)"Other";
    TF_EQ(form_kline(f, &ln), 0);
    TF_STREQ(form_get(f, "MFR"), "Zenith");

    /* Only a K line of two values loads. */
    ln.kind = 'R';
    ln.n = 2;
    TF_EQ(form_kline(f, &ln), 0);
    ln.kind = 'F';
    TF_EQ(form_kline(f, &ln), 0);
    TF_STREQ(form_get(f, "MFR"), "Zenith");

    /* Over a decoded response. */
    resp(NULL, NULL, NULL);
    tl_hook = NULL;
    tl_fail(1, "+DATA\nK ID=17\nK MFR=\"Digital Equipment\"\n"
               "K DESCR=\"say \\\"hi\\\"\"\nK NOSUCH=1\nK QTY=\n.");
    rq_begin(&q, "ITEM GET 17");
    TF_EQ(rq_send(g_lk, &q, form_kline, f, &res), RQ_OK);
    TF_STREQ(form_get(f, "ID"), "17");
    TF_STREQ(form_get(f, "MFR"), "Digital Equipment");
    TF_STREQ(form_get(f, "DESCR"), "say \"hi\"");
    TF_STREQ(form_get(f, "QTY"), "");
    TF_EQ(form_dirty(f), 0);
    tl_hook = script;
    tl_clear();
    form_close(f);
}

static u16 mfof(u8 type, u8 width, u8 dec, u8 flags, const char *choices,
                const char *val)
{
    form *f;
    u16 mf;
    int i;

    mf = 9999;
    memset(one_f, 0, sizeof one_f);
    one_f[0].name = "V";
    one_f[0].type = type;
    one_f[0].width = width;
    one_f[0].dec = dec;
    one_f[0].flags = flags;
    one_f[0].choices = choices;
    f = mk(&one_def, FORM_NEW);
    if (f == NULL)
        return 0;
    form_put(f, "V", val);
    i = form_check(f, &mf);
    form_close(f);
    if (i == -1)
        return 0;
    TF_EQ(i, 0);
    return mf;
}

#define NUM(w, d, fl, val)  mfof(FT_NUM, w, d, fl, NULL, val)

static void test_check_types(void)
{
    u8 t;

    /* An empty value passes everywhere, unless required. */
    for (t = FT_CHAR; t <= FT_SECRET; t++) {
        TF_EQ(mfof(t, 8, 0, 0, "A B", ""), 0);
        TF_EQ(mfof(t, 8, 0, FF_RO, "A B", ""), 0);
        TF_EQ(mfof(t, 8, 0, FF_REQ | FF_RO, "A B", ""), 0);
        if (t == FT_MEMO)
            TF_EQ(mfof(t, 8, 0, FF_REQ, "A B", ""), 0);
        else
            TF_EQ(mfof(t, 8, 0, FF_REQ, "A B", ""), MF_REQUIRED);
    }
    TF_EQ(mfof(FT_CHAR, 8, 0, FF_REQ, NULL, "x"), 0);
    TF_EQ(mfof(FT_SECRET, 8, 0, FF_REQ, NULL, "x"), 0);
    TF_EQ(mfof(FT_CHAR, 8, 0, 0, NULL, "anything at all"), 0);
    TF_EQ(mfof(FT_CODE, 8, 0, 0, NULL, "anything"), 0);
    TF_EQ(mfof(FT_SECRET, 8, 0, 0, NULL, "pw"), 0);
    TF_EQ(mfof(FT_MEMO, 8, 0, 0, NULL, "abc"), 0);
    TF_EQ(mfof(FT_MEMO, 8, 0, 0, NULL, "12"), 0);

    /* Read-only fields are not checked. */
    TF_EQ(NUM(5, 0, FF_RO, "abc"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, FF_RO, NULL, "99999999"), 0);
    TF_EQ(mfof(FT_LOGIC, 1, 0, FF_RO, NULL, "x"), 0);
    TF_EQ(mfof(FT_CHOICE, 8, 0, FF_RO, "A B", "C"), 0);

    /* Numbers without decimals. */
    TF_EQ(NUM(3, 0, 0, "0"), 0);
    TF_EQ(NUM(3, 0, 0, "123"), 0);
    TF_EQ(NUM(3, 0, 0, "-12"), 0);
    TF_EQ(NUM(3, 0, 0, "999"), 0);
    TF_EQ(NUM(3, 0, 0, "-99"), 0);
    TF_EQ(NUM(5, 0, 0, "abc"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "12a"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "-"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "--1"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "1-2"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "+5"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, " 5"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "1.5"), MF_BADNUM);
    TF_EQ(NUM(5, 0, 0, "1.2.3"), MF_BADNUM);

    /* With decimals: sign, integer digits, point and decimals fit width. */
    TF_EQ(NUM(6, 2, 0, "123"), 0);
    TF_EQ(NUM(6, 2, 0, "123.45"), 0);
    TF_EQ(NUM(6, 2, 0, "123.4"), 0);
    TF_EQ(NUM(6, 2, 0, "-12.34"), 0);
    TF_EQ(NUM(6, 2, 0, "0.5"), 0);
    TF_EQ(NUM(6, 2, 0, "-0.5"), 0);
    TF_EQ(NUM(6, 2, 0, "1234.5"), MF_BADNUM);
    TF_EQ(NUM(6, 2, 0, "1234"), MF_BADNUM);
    TF_EQ(NUM(8, 2, 0, "123.456"), MF_BADNUM);
    TF_EQ(NUM(8, 2, 0, "-123.45"), 0);
    TF_EQ(NUM(6, 2, 0, "1.2.3"), MF_BADNUM);
    TF_EQ(NUM(6, 2, 0, "1,5"), MF_BADNUM);
    TF_EQ(NUM(6, 2, 0, "x.5"), MF_BADNUM);
    TF_EQ(NUM(6, 2, 0, "-123.4"), MF_BADNUM);
    TF_EQ(NUM(6, 2, 0, "-123"), MF_BADNUM);
    TF_EQ(NUM(5, 1, 0, "-123"), MF_BADNUM);
    TF_EQ(NUM(5, 1, 0, "-12"), 0);
    TF_EQ(NUM(6, 1, 0, "1.23"), MF_BADNUM);
    TF_EQ(NUM(6, 1, 0, "1.2"), 0);

    /* Non-negative. */
    TF_EQ(NUM(5, 0, FF_NONNEG, "5"), 0);
    TF_EQ(NUM(5, 0, FF_NONNEG, "0"), 0);
    TF_EQ(NUM(5, 0, FF_NONNEG, "-0"), 0);
    TF_EQ(NUM(5, 0, FF_NONNEG, "-5"), MF_NEGNUM);
    TF_EQ(NUM(5, 0, FF_NONNEG, "-10"), MF_NEGNUM);
    TF_EQ(NUM(5, 0, FF_NONNEG, "abc"), MF_BADNUM);
    TF_EQ(NUM(5, 0, FF_NONNEG, "-"), MF_BADNUM);
    TF_EQ(NUM(8, 2, FF_NONNEG, "-0.00"), 0);
    TF_EQ(NUM(8, 2, FF_NONNEG, "-0.0"), 0);
    TF_EQ(NUM(8, 2, FF_NONNEG, "-0.01"), MF_NEGNUM);
    TF_EQ(NUM(8, 2, FF_NONNEG, "12.50"), 0);
    TF_EQ(NUM(5, 0, 0, "-5"), 0);
    TF_EQ(NUM(5, 0, FF_NONNEG | FF_REQ, ""), MF_REQUIRED);

    /* Dates. */
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20241231"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "19800101"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240229"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20000229"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20230229"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "19000229"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20241301"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240001"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240100"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240132"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240431"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240131"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "20240430"), 0);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "2024123"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "2024123x"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, 0, NULL, "abcdefgh"), MF_BADDATE);
    TF_EQ(mfof(FT_DATE, 8, 0, FF_REQ, NULL, ""), MF_REQUIRED);

    /* Logical. */
    TF_EQ(mfof(FT_LOGIC, 1, 0, 0, NULL, "T"), 0);
    TF_EQ(mfof(FT_LOGIC, 1, 0, 0, NULL, "F"), 0);
    TF_EQ(mfof(FT_LOGIC, 1, 0, 0, NULL, "t"), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 1, 0, 0, NULL, "X"), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 1, 0, 0, NULL, "1"), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 2, 0, 0, NULL, "TX"), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 2, 0, 0, NULL, "FF"), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 2, 0, 0, NULL, "T "), MF_BADCHOICE);
    TF_EQ(mfof(FT_LOGIC, 1, 0, FF_REQ, NULL, ""), MF_REQUIRED);

    /* Choices: a whole word, case included. */
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "NEW"), 0);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "USED"), 0);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "BAD"), 0);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "new"), MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "NE"), MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "USE"), MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "ED"), MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "NEW USED"),
          MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, 0, "NEW USED BAD", "NEWS"), MF_BADCHOICE);
    TF_EQ(mfof(FT_CHOICE, 12, 0, FF_REQ, "NEW USED BAD", ""), MF_REQUIRED);
}

static void test_check_order(void)
{
    form *f;
    u16 mf;

    f = mk(&item_def, FORM_NEW);
    if (f == NULL)
        return;

    /* Nothing to find in a form of empty values but the required one. */
    mf = 0;
    TF_EQ(form_check(f, &mf), I_CAT);
    TF_EQ(mf, MF_REQUIRED);
    form_put(f, "CAT", "C1");
    form_put(f, "ID", "abc");
    form_put(f, "NOTES", "zz");
    form_put(f, "TMP", "free text");
    TF_EQ(form_check(f, &mf), -1);

    /* The first failing one, in the table's order. */
    form_put(f, "PRICE", "-1");
    form_put(f, "ACQ", "20230230");
    form_put(f, "QTY", "x");
    form_put(f, "OK", "x");
    form_put(f, "STATUS", "Q");
    TF_EQ(form_check(f, &mf), I_QTY);
    TF_EQ(mf, MF_BADNUM);
    form_put(f, "QTY", "4");
    TF_EQ(form_check(f, &mf), I_ACQ);
    TF_EQ(mf, MF_BADDATE);
    form_put(f, "ACQ", "20230228");
    TF_EQ(form_check(f, &mf), I_STATUS);
    TF_EQ(mf, MF_BADCHOICE);
    form_put(f, "STATUS", "D");
    TF_EQ(form_check(f, &mf), I_OK);
    TF_EQ(mf, MF_BADCHOICE);
    form_put(f, "OK", "F");
    TF_EQ(form_check(f, &mf), I_PRICE);
    TF_EQ(mf, MF_NEGNUM);
    form_put(f, "PRICE", "1.25");
    TF_EQ(form_check(f, &mf), -1);

    /* A required field emptied again. */
    form_put(f, "CAT", "");
    TF_EQ(form_check(f, &mf), I_CAT);
    TF_EQ(mf, MF_REQUIRED);
    form_close(f);
}

static void test_save_new(void)
{
    form *f;
    rq_res res;
    char id[16];

    /* An ADD: the required code, then the changed fields in the table's
       order; the read-only, memo and local ones and the empty ones are
       not sent. */
    resp("+OK ID=17", NULL, NULL);
    f = mk(&item_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "TMP", "t");
    form_put(f, "ID", "99");
    form_put(f, "NOTES", "7");
    form_put(f, "PRICE", "12.50");
    form_put(f, "OK", "T");
    form_put(f, "STATUS", "D");
    form_put(f, "ACQ", "20240229");
    form_put(f, "QTY", "3");
    form_put(f, "MFR", "IBM \"PC\"");
    form_put(f, "CAT", "C1");
    id[0] = '\0';
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0),
             "ITEM ADD CAT=C1 MFR=\"IBM \\\"PC\\\"\" QTY=3 ACQ=20240229 "
             "STATUS=D OK=T PRICE=12.50");
    TF_STREQ(id, "17");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(res.code, RQ_OK);
    form_close(f);

    /* Only the required code changed. */
    resp("+OK ID=3", NULL, NULL);
    f = mk(&item_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C3");
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM ADD CAT=C3");
    TF_STREQ(id, "3");
    TF_EQ(form_dirty(f), 0);
    form_close(f);
}

static void test_save_edit(void)
{
    form *f;
    rq_res res;
    char id[16];

    resp(NULL, NULL, NULL);
    f = mk(&item_def, FORM_EDIT);
    if (f == NULL)
        return;
    form_load(f, "ID", "17");
    form_load(f, "CAT", "C1");
    form_load(f, "MFR", "IBM");
    form_load(f, "DESCR", "old");
    form_load(f, "NOTES", "5");
    strcpy(id, "17");

    /* Nothing changed: nothing sent, an all-zero result. */
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 0);
    TF_CHECK(res_zero(&res));
    TF_STREQ(id, "17");

    /* Only memo, read-only and local changes are not sent either. */
    form_put(f, "NOTES", "6");
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 0);
    TF_CHECK(res_zero(&res));

    /* A SET carries the changed fields only, not the required code. */
    form_put(f, "DESCR", "new text");
    form_put(f, "QTY", "2");
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM SET 17 DESCR=\"new text\" QTY=2");
    TF_STREQ(id, "17");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(res.code, RQ_OK);

    /* Saved: a save again sends nothing. */
    tl_clear();
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 0);

    /* A failed SET leaves its fields changed, and a save again sends
       them. */
    form_put(f, "MFR", "HP");
    resp("-ERR 202 no such code FIELD=CAT", "+OK", NULL);
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_ECONSTR);
    TF_EQ(res.code, RQ_ECONSTR);
    TF_STREQ(res.field, "CAT");
    TF_EQ(tl_count(), 1);
    TF_EQ(form_err_field(f, &res), I_CAT);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(form_dirty(f), 1);
    TF_STREQ(form_get(f, "MFR"), "HP");
    resp(NULL, NULL, NULL);
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 17 MFR=HP");
    TF_EQ(tl_count(), 1);
    TF_EQ(form_dirty(f), 0);

    /* Read-only, local and memo changes go with a change, never alone. */
    form_put(f, "ID", "99");
    form_put(f, "TMP", "t");
    form_put(f, "NOTES", "9");
    form_put(f, "CAT", "C9");
    tl_clear();
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 17 CAT=C9");
    TF_EQ(tl_count(), 1);
    form_close(f);
}

static void test_save_key(void)
{
    form *f;
    rq_res res;
    char id[16];

    /* The key is the positional word, wherever it is in the table and
       never a pair; no ID= in the answer, so the key is the ID. */
    resp(NULL, NULL, NULL);
    f = mk(&code_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "SORTORD", "10");
    form_put(f, "NAME", "case one");
    form_put(f, "CODE", "A1");
    id[0] = '\0';
    TF_EQ(save(f, "CAT", id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "CAT ADD A1 SORTORD=10 NAME=\"case one\"");
    TF_STREQ(id, "A1");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(form_dirty(f), 0);

    /* Then a SET names the record by id, and the key is no pair. */
    form_put(f, "SORTORD", "20");
    TF_EQ(save(f, "CAT", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(1), "CAT SET A1 SORTORD=20");
    TF_EQ(tl_count(), 2);
    form_close(f);

    /* Only the key and nothing else: the ADD still goes. */
    resp(NULL, NULL, NULL);
    f = mk(&code_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CODE", "B2");
    TF_EQ(save(f, "CAT", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "CAT ADD B2");
    TF_STREQ(id, "B2");
    form_close(f);

    /* A key and a secret with the required fields before the others. */
    resp(NULL, NULL, NULL);
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "FULLNAME", "Bob B");
    form_put(f, "LEVEL", "2");
    form_put(f, "PASSWORD", "pw123");
    form_put(f, "NAME", "bob");
    TF_EQ(save(f, "USER", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0),
             "USER ADD bob PASSWORD=pw123 LEVEL=2 FULLNAME=\"Bob B\"");
    TF_STREQ(id, "bob");
    form_close(f);
}

static void test_save_split(void)
{
    form *f;
    rq_res res;
    char id[16];
    char a[61];
    char b[61];
    char c[61];
    char d[61];
    char e[61];
    char x[LINK_MAXLINE];
    char exp[2 * LINK_MAXLINE];
    u16 i;

    fill(a, 'a', 60);
    fill(b, 'b', 60);
    fill(c, 'c', 60);
    fill(d, 'd', 60);
    fill(e, 'e', 60);

    /* What does not fit the ADD goes to a SET of the new ID, and the
       fields of both are saved. */
    resp("+OK ID=9", NULL, NULL);
    f = mk(&big_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "E", e);
    form_put(f, "D", d);
    form_put(f, "C", c);
    form_put(f, "B", b);
    form_put(f, "A", a);
    form_put(f, "CAT", "C1");
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 2);
    sprintf(exp, "%s ADD CAT=C1 A=%s B=%s C=%s", g_big, a, b, c);
    TF_STREQ(tl_req(0), exp);
    sprintf(exp, "%s SET 9 D=%s E=%s", g_big, d, e);
    TF_STREQ(tl_req(1), exp);
    TF_STREQ(id, "9");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(form_dirty(f), 0);
    form_close(f);

    /* The later SET fails: the record exists, the ADD's fields are saved,
       the others stay changed, and a save again sends just those. */
    resp("+OK ID=9", "-ERR 202 refused FIELD=D", NULL);
    f = mk(&big_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    form_put(f, "E", e);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_ECONSTR);
    TF_EQ(tl_count(), 2);
    TF_EQ(res.code, RQ_ECONSTR);
    TF_STREQ(res.field, "D");
    TF_STREQ(id, "9");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(((struct form *)f)->m.partial, 1);
    TF_EQ(form_dirty(f), 1);
    TF_EQ(form_err_field(f, &res), 4);
    resp(NULL, NULL, NULL);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    sprintf(exp, "%s SET 9 D=%s E=%s", g_big, d, e);
    TF_STREQ(tl_req(0), exp);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(form_dirty(f), 0);
    form_close(f);

    /* The partial flag is cleared by a save that does not leave it. */
    resp("+OK ID=9", "-ERR 500 oops", NULL);
    f = mk(&big_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_EINTERNAL);
    TF_EQ(((struct form *)f)->m.partial, 1);
    resp("-ERR 500 oops", NULL, NULL);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_EINTERNAL);
    TF_EQ(((struct form *)f)->m.partial, 0);
    form_close(f);

    /* The ADD fails: nothing is saved, the form stays new, and the
       field is named. */
    resp("-ERR 201 duplicate FIELD=CAT", NULL, NULL);
    f = mk(&big_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "A", a);
    form_put(f, "E", e);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_EDUP);
    TF_EQ(tl_count(), 1);
    TF_EQ(res.code, RQ_EDUP);
    TF_STREQ(res.field, "CAT");
    TF_EQ(form_mode_get(f), FORM_NEW);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(form_dirty(f), 1);
    TF_EQ(form_err_field(f, &res), 0);
    resp("+OK ID=4", NULL, NULL);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    sprintf(exp, "%s ADD CAT=C1 A=%s E=%s", g_big, a, e);
    TF_STREQ(tl_req(0), exp);
    TF_EQ(form_mode_get(f), FORM_EDIT);
    form_close(f);

    /* A request of exactly LINK_MAXLINE bytes fits (BIG_LEN + 203 of
       them are the ADD and the pairs before X=); one byte more moves its
       last pair to a SET. */
    for (i = 0; i < 2; i++) {
        resp("+OK ID=9", NULL, NULL);
        f = mk(&edge_def, FORM_NEW);
        if (f == NULL)
            return;
        fill(x, 'x', (u16)(LINK_MAXLINE - BIG_LEN - 203 + i));
        form_put(f, "CAT", "C1");
        form_put(f, "A", a);
        form_put(f, "B", b);
        form_put(f, "C", c);
        form_put(f, "X", x);
        TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
        if (i == 0) {
            TF_EQ(tl_count(), 1);
            TF_EQ(strlen(tl_req(0)), LINK_MAXLINE);
            sprintf(exp, "%s ADD CAT=C1 A=%s B=%s C=%s X=%s", g_big, a, b, c,
                    x);
            TF_STREQ(tl_req(0), exp);
        } else {
            TF_EQ(tl_count(), 2);
            sprintf(exp, "%s ADD CAT=C1 A=%s B=%s C=%s", g_big, a, b, c);
            TF_STREQ(tl_req(0), exp);
            sprintf(exp, "%s SET 9 X=%s", g_big, x);
            TF_STREQ(tl_req(1), exp);
        }
        form_close(f);
    }

    /* A SET splits too, without an ADD. */
    resp(NULL, NULL, NULL);
    f = mk(&big_def, FORM_EDIT);
    if (f == NULL)
        return;
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    form_put(f, "E", e);
    strcpy(id, "5");
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 2);
    sprintf(exp, "%s SET 5 A=%s B=%s C=%s D=%s", g_big, a, b, c, d);
    TF_CHECK(strlen(exp) > LINK_MAXLINE);
    sprintf(exp, "%s SET 5 A=%s B=%s C=%s", g_big, a, b, c);
    TF_STREQ(tl_req(0), exp);
    sprintf(exp, "%s SET 5 D=%s E=%s", g_big, d, e);
    TF_STREQ(tl_req(1), exp);
    TF_EQ(form_dirty(f), 0);

    /* The second SET fails: the first one's fields are saved. */
    form_put(f, "A", e);
    form_put(f, "B", d);
    form_put(f, "C", d);
    form_put(f, "E", a);
    resp(NULL, "-ERR 300 disk", NULL);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_EIO);
    TF_EQ(tl_count(), 2);
    sprintf(exp, "%s SET 5 A=%s B=%s C=%s", g_big, e, d, d);
    TF_STREQ(tl_req(0), exp);
    TF_EQ(((struct form *)f)->m.partial, 0);
    TF_EQ(form_dirty(f), 1);
    resp(NULL, NULL, NULL);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 1);
    sprintf(exp, "%s SET 5 E=%s", g_big, a);
    TF_STREQ(tl_req(0), exp);
    TF_EQ(form_dirty(f), 0);
    form_close(f);
}

static void test_save_passover(void)
{
    form *f;
    rq_res res;
    char id[16];
    char a[61];
    char b[61];
    char c[61];
    char d[61];
    char exp[2 * LINK_MAXLINE];

    fill(a, 'a', 60);
    fill(b, 'b', 60);
    fill(c, 'c', 60);
    fill(d, 'd', 60);

    /* The ID= of the answer is the new ID, also with a key. */
    resp("+OK ID=7", NULL, NULL);
    f = mk(&code_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CODE", "A1");
    TF_EQ(save(f, "CAT", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "CAT ADD A1");
    TF_STREQ(id, "7");
    form_close(f);

    /* form_saved makes every change the original: the read-only, local
       and key ones, which a save does not, too. */
    resp(NULL, NULL, NULL);
    f = mk(&item_def, FORM_EDIT);
    if (f == NULL)
        return;
    strcpy(id, "5");
    form_put(f, "MFR", "m1");
    form_put(f, "ID", "99");
    form_put(f, "TMP", "t1");
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM SET 5 MFR=m1");
    TF_EQ(form_dirty(f), 1);
    form_saved(f);
    TF_EQ(form_dirty(f), 0);
    TF_STREQ(form_get(f, "ID"), "99");
    TF_STREQ(form_get(f, "TMP"), "t1");
    TF_STREQ(form_get(f, "MFR"), "m1");
    form_put(f, "ID", "");
    TF_EQ(form_dirty(f), 1);
    form_put(f, "ID", "99");
    TF_EQ(form_dirty(f), 0);
    form_put(f, "TMP", "");
    TF_EQ(form_dirty(f), 1);
    form_put(f, "TMP", "t1");
    TF_EQ(form_dirty(f), 0);
    form_put(f, "MFR", "");
    TF_EQ(form_dirty(f), 1);
    form_close(f);

    f = mk(&code_def, FORM_EDIT);
    if (f == NULL)
        return;
    form_load(f, "CODE", "A1");
    form_put(f, "CODE", "A2");
    TF_EQ(form_dirty(f), 1);
    form_saved(f);
    TF_EQ(form_dirty(f), 0);
    form_put(f, "CODE", "A1");
    TF_EQ(form_dirty(f), 1);
    form_close(f);

    /* An ADD passes over a pair that does not fit and takes a later
       one that does; the SET has the pass-over pair. */
    resp("+OK ID=9", NULL, NULL);
    f = mk(&pass_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    form_put(f, "S", "s");
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 2);
    sprintf(exp, "%s ADD CAT=C1 A=%s B=%s C=%s S=s", g_big, a, b, c);
    TF_STREQ(tl_req(0), exp);
    sprintf(exp, "%s SET 9 D=%s", g_big, d);
    TF_STREQ(tl_req(1), exp);
    form_close(f);

    /* So does a SET, and the next one starts at the first pair left. */
    resp(NULL, NULL, NULL);
    f = mk(&pass_def, FORM_EDIT);
    if (f == NULL)
        return;
    strcpy(id, "5");
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    form_put(f, "S", "s");
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_OK);
    TF_EQ(tl_count(), 2);
    sprintf(exp, "%s SET 5 A=%s B=%s C=%s S=s", g_big, a, b, c);
    TF_STREQ(tl_req(0), exp);
    sprintf(exp, "%s SET 5 D=%s", g_big, d);
    TF_STREQ(tl_req(1), exp);
    TF_EQ(form_dirty(f), 0);
    form_close(f);
}

static void test_save_toolong(void)
{
    form *f;
    rq_res res;
    char id[16];
    char w[FORM_MAXVAL + 1];

    fill(w, '\1', FORM_MAXVAL);
    TF_CHECK(BIG_LEN + strlen(" SET 5 W=") + rq_cost(w, FORM_MAXVAL) >
             LINK_MAXLINE);

    /* Edit: refused unsent, the field stays changed. */
    resp(NULL, NULL, NULL);
    f = mk(&lng_def, FORM_EDIT);
    if (f == NULL)
        return;
    strcpy(id, "5");
    form_put(f, "W", w);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_ETOOLONG);
    TF_EQ(tl_count(), 0);
    TF_EQ(form_dirty(f), 1);
    TF_STREQ(id, "5");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(((struct form *)f)->m.partial, 0);
    form_close(f);

    /* A required field too long: no ADD can be made. */
    resp(NULL, NULL, NULL);
    f = mk(&lngr_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "W", w);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_ETOOLONG);
    TF_EQ(tl_count(), 0);
    TF_EQ(form_mode_get(f), FORM_NEW);
    TF_EQ(form_dirty(f), 1);
    TF_EQ(((struct form *)f)->m.partial, 0);
    form_close(f);

    /* A key too long: likewise. */
    resp(NULL, NULL, NULL);
    f = mk(&lngk_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "W", w);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_ETOOLONG);
    TF_EQ(tl_count(), 0);
    TF_EQ(form_mode_get(f), FORM_NEW);
    TF_EQ(((struct form *)f)->m.partial, 0);
    form_close(f);

    /* A long optional pair is left out of the ADD, which goes: the form
       is an edit form for its record, the long field still a change
       whose SET is refused unsent, and the save partial. */
    resp("+OK ID=8", NULL, NULL);
    f = mk(&lng_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "S", "s");
    form_put(f, "W", w);
    TF_EQ(save(f, g_big, id, sizeof id, &res), RQ_ETOOLONG);
    TF_EQ(tl_count(), 1);
    TF_STREQ(id, "8");
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_EQ(form_dirty(f), 1);
    TF_EQ(((struct form *)f)->m.partial, 1);
    form_close(f);

    /* An ADD with no key and no ID in its answer. */
    resp("+OK", NULL, NULL);
    f = mk(&nokey_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    TF_EQ(save(f, "ITEM", id, sizeof id, &res), RQ_EINTERNAL);
    TF_STREQ(tl_req(0), "ITEM ADD CAT=C1");
    form_close(f);
}

static void test_err_field(void)
{
    form *f;
    rq_res res;
    static const int same[] = { RQ_EARG, RQ_ENOTFOUND, RQ_EDUP,
                                RQ_ECONSTR };
    static const int others[] = { RQ_OK, RQ_ESYNTAX, RQ_EUNKNOWN, RQ_ETOOLONG,
        RQ_ENOTAVAIL, RQ_EPERM, RQ_ECURSOR, RQ_EIO, RQ_EFULL,
        RQ_ENOLOGIN, RQ_EBUSY, RQ_EINTERNAL, RQ_ESHUTDOWN, RQ_ECLOSED,
        RQ_ESTOP, RQ_ELOST };
    u16 i;

    f = mk(&item_def, FORM_EDIT);
    if (f == NULL)
        return;
    memset(&res, 0, sizeof res);
    strcpy(res.field, "MFR");
    res.code = RQ_EARG;
    TF_EQ(form_err_field(f, &res), I_MFR);
    res.code = RQ_ENOTFOUND;
    strcpy(res.field, "CAT");
    TF_EQ(form_err_field(f, &res), I_CAT);
    res.code = RQ_EDUP;
    strcpy(res.field, "DESCR");
    TF_EQ(form_err_field(f, &res), I_DESCR);
    res.code = RQ_ECONSTR;
    strcpy(res.field, "PRICE");
    TF_EQ(form_err_field(f, &res), I_PRICE);
    strcpy(res.field, "ID");
    TF_EQ(form_err_field(f, &res), I_ID);

    /* A field the form does not have, or none. */
    for (i = 0; i < sizeof same / sizeof same[0]; i++) {
        res.code = same[i];
        strcpy(res.field, "NOSUCH");
        TF_EQ(form_err_field(f, &res), -1);
        res.field[0] = '\0';
        TF_EQ(form_err_field(f, &res), -1);
    }

    /* Other codes name no field of the form, whatever FIELD= holds. */
    strcpy(res.field, "MFR");
    for (i = 0; i < sizeof others / sizeof others[0]; i++) {
        res.code = others[i];
        TF_EQ(form_err_field(f, &res), -1);
    }
    form_close(f);
}

static void test_find_key(void)
{
    form *f;
    u16 i;

    f = mk(&item_def, FORM_VIEW);
    if (f != NULL) {
        for (i = 0; i < I_N; i++)
            TF_EQ(form_find(f, item_f[i].name), i);
        TF_EQ(form_find(f, "NOSUCH"), -1);
        TF_EQ(form_find(f, ""), -1);
        TF_EQ(form_find(f, "MF"), -1);
        TF_EQ(form_find(f, "MFRX"), -1);
        TF_EQ(form_key(f), -1);
        form_close(f);
    }
    f = mk(&code_def, FORM_VIEW);
    if (f != NULL) {
        TF_EQ(form_key(f), 1);
        form_close(f);
    }
    f = mk(&user_def, FORM_VIEW);
    if (f != NULL) {
        TF_EQ(form_key(f), U_NAME);
        form_close(f);
    }
    f = mk(&two_def, FORM_VIEW);
    if (f != NULL) {
        TF_EQ(form_key(f), 1);
        form_close(f);
    }
    f = mk(&nokey_def, FORM_VIEW);
    if (f != NULL) {
        TF_EQ(form_key(f), -1);
        form_close(f);
    }
}

static void test_choices(void)
{
    char out[8];

    TF_EQ(form_choice_count(""), 0);
    TF_EQ(form_choice_count("A"), 1);
    TF_EQ(form_choice_count("A D"), 2);
    TF_EQ(form_choice_count("NEW USED BAD"), 3);

    TF_EQ(form_choice_find("NEW USED BAD", "NEW"), 0);
    TF_EQ(form_choice_find("NEW USED BAD", "USED"), 1);
    TF_EQ(form_choice_find("NEW USED BAD", "BAD"), 2);
    TF_EQ(form_choice_find("NEW USED BAD", "new"), -1);
    TF_EQ(form_choice_find("NEW USED BAD", "US"), -1);
    TF_EQ(form_choice_find("NEW USED BAD", "USEDX"), -1);
    TF_EQ(form_choice_find("NEW USED BAD", "ED BAD"), -1);
    TF_EQ(form_choice_find("A", "A"), 0);
    TF_EQ(form_choice_find("AB A", "A"), 1);
    TF_EQ(form_choice_find("A AB", "AB"), 1);
    TF_EQ(form_choice_find("", "A"), -1);

    TF_EQ(form_choice_num("NEW USED BAD", "NEW"), 0);
    TF_EQ(form_choice_num("NEW USED BAD", "USED"), 1);
    TF_EQ(form_choice_num("NEW USED BAD", "BAD"), 2);
    TF_EQ(form_choice_num("NEW USED BAD", "bad"), 0);
    TF_EQ(form_choice_num("NEW USED BAD", "OTHER"), 0);
    TF_EQ(form_choice_num("NEW USED BAD", ""), 0);

    memset(out, 'x', sizeof out);
    form_choice_word("NEW USED BAD", 0, out, sizeof out);
    TF_STREQ(out, "NEW");
    form_choice_word("NEW USED BAD", 1, out, sizeof out);
    TF_STREQ(out, "USED");
    form_choice_word("NEW USED BAD", 2, out, sizeof out);
    TF_STREQ(out, "BAD");
    form_choice_word("NEW USED BAD", 3, out, sizeof out);
    TF_STREQ(out, "");
    form_choice_word("NEW USED BAD", 40, out, sizeof out);
    TF_STREQ(out, "");
    form_choice_word("", 0, out, sizeof out);
    TF_STREQ(out, "");
    form_choice_word("NEW USED BAD", 1, out, 3);
    TF_STREQ(out, "US");
    form_choice_word("NEW USED BAD", 1, out, 5);
    TF_STREQ(out, "USED");
    form_choice_word("NEW USED BAD", 1, out, 4);
    TF_STREQ(out, "USE");
    memset(out, 'x', sizeof out);
    form_choice_word("NEW USED BAD", 1, out, 1);
    TF_EQ(out[0], '\0');
    TF_EQ(out[1], 'x');
    memset(out, 'x', sizeof out);
    form_choice_word("NEW", 5, out, 1);
    TF_EQ(out[0], '\0');
}

static void test_wipe(void)
{
    form *f;
    form *g;
    struct form *s;
    u16 held;
    u16 pw;

    held = tm_held();
    pw = form_valsz(&user_f[U_PW]);

    /* form_close overwrites the current value of a secret... */
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    form_load(f, "PASSWORD", "loaded1");
    form_put(f, "PASSWORD", "typed pw");
    form_put(f, "NAME", "bob");
#ifdef COMPAT_HOST
    tm_expect_wiped(form_val(f, U_PW), pw);
#endif
    form_close(f);
#ifdef COMPAT_HOST
    TF_CHECK(tm_wiped());
#endif
    TM_HELD(held);

    /* ... and the value it was loaded with. */
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    s = (struct form *)f;
    form_load(f, "PASSWORD", "loaded1");
    form_put(f, "PASSWORD", "typed pw");
#ifdef COMPAT_HOST
    tm_expect_wiped(s->m.org + s->m.off[U_PW], pw);
#endif
    form_close(f);
#ifdef COMPAT_HOST
    TF_CHECK(tm_wiped());
#endif
    TM_HELD(held);

    /* Saved, the original is the typed value: wiped as well. */
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    s = (struct form *)f;
    form_put(f, "PASSWORD", "typed pw");
    form_saved(f);
#ifdef COMPAT_HOST
    tm_expect_wiped(s->m.org + s->m.off[U_PW], pw);
#endif
    form_close(f);
#ifdef COMPAT_HOST
    TF_CHECK(tm_wiped());
#endif
    TM_HELD(held);

    /* form_wipe closes the open form with a secret, once. */
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "PASSWORD", "typed pw");
#ifdef COMPAT_HOST
    tm_expect_wiped(form_val(f, U_PW), pw);
#endif
    form_wipe();
#ifdef COMPAT_HOST
    TF_CHECK(tm_wiped());
#endif
    TM_HELD(held);
    form_wipe();
    TM_HELD(held);

    /* A form closed by its owner is not closed again by form_wipe. */
    f = mk(&user_def, FORM_NEW);
    if (f == NULL)
        return;
    form_close(f);
    form_wipe();
    TM_HELD(held);

    /* A form with no secret is left alone, and may be closed after. */
    g = mk(&item_def, FORM_NEW);
    if (g == NULL)
        return;
    form_put(g, "MFR", "kept");
    form_wipe();
    TM_HELD(held + 1);
    TF_STREQ(form_get(g, "MFR"), "kept");
    form_close(g);
    TM_HELD(held);

    /* ... also beside a secret one, which is the one wiped. */
    g = mk(&item_def, FORM_NEW);
    f = mk(&user_def, FORM_NEW);
    if (g == NULL || f == NULL)
        return;
    form_put(g, "MFR", "kept");
    form_put(f, "PASSWORD", "typed pw");
    form_wipe();
    TM_HELD(held + 1);
    TF_STREQ(form_get(g, "MFR"), "kept");
    form_close(g);
    TM_HELD(held);
}

/* One run over a form with every drawing member set one way or another:
   what the model gives, as text. */
static void run_model(const form_def *def, char *out)
{
    form *f;
    rq_res res;
    char id[16];
    u16 mf;
    int i;

    resp("+OK ID=31", "+OK", NULL);
    f = mk(def, FORM_NEW);
    out[0] = '\0';
    if (f == NULL)
        return;
    sprintf(out + strlen(out), "%u %d %d|", ((struct form *)f)->m.size,
            form_find(f, "QTY"), form_key(f));
    form_put(f, "QTY", "12345678");
    form_put(f, "DESCR", "d");
    form_put(f, "ACQ", "202402301");
    form_put(f, "STATUS", "DD");
    i = form_check(f, &mf);
    sprintf(out + strlen(out), "%d %u|", i, i < 0 ? 0 : mf);
    form_put(f, "CAT", "C1");
    form_put(f, "QTY", "50");
    i = form_check(f, &mf);
    sprintf(out + strlen(out), "%d %u|", i, i < 0 ? 0 : mf);
    form_put(f, "ACQ", "20240230");
    i = form_check(f, &mf);
    sprintf(out + strlen(out), "%d %u|", i, i < 0 ? 0 : mf);
    form_put(f, "ACQ", "20240228");
    form_put(f, "STATUS", "D");
    form_put(f, "MFR", "m");
    i = form_check(f, &mf);
    sprintf(out + strlen(out), "%d|", i);
    i = save(f, "ITEM", id, sizeof id, &res);
    sprintf(out + strlen(out), "%d %s %d|%s", i, id, form_dirty(f), tl_log());
    form_close(f);
}

static void test_independence(void)
{
    char a[400];
    char b[400];

    run_model(&item_def, a);
    run_model(&item_gef, b);
    TF_STREQ(b, a);
    TF_CHECK(strstr(a, "ITEM ADD CAT=C1 MFR=m DESCR=d QTY=50 ACQ=20240228 "
                       "STATUS=D") != NULL);
}

/* A lost link ends the session: last. */
static void test_lost(void)
{
    form *f;
    rq_res res;
    char id[16];
    char a[61];
    char b[61];
    char c[61];
    char d[61];

    fill(a, 'a', 60);
    fill(b, 'b', 60);
    fill(c, 'c', 60);
    fill(d, 'd', 60);
    resp("+OK ID=9", resp_end, NULL);
    f = mk(&big_def, FORM_NEW);
    if (f == NULL)
        return;
    form_put(f, "CAT", "C1");
    form_put(f, "A", a);
    form_put(f, "B", b);
    form_put(f, "C", c);
    form_put(f, "D", d);
    TF_CHECK(save(f, g_big, id, sizeof id, &res) != RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_EQ(((struct form *)f)->m.partial, 1);
    TF_EQ(form_mode_get(f), FORM_EDIT);
    TF_STREQ(id, "9");
    TF_EQ(form_dirty(f), 1);
    form_close(f);
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_FMODEL");
    fill(g_big, 'T', BIG_LEN);
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_FMODEL dir\n");
        return 2;
    }
    te_remove(argv[1], NULL);
    tc_clock(&clk);
    rc = link_open_local(&g_lk, argv[1], &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_FMODEL: %s\n", why);
        return tf_done();
    }
    tl_hook = script;
    test_new();
    test_valsz();
    test_values();
    test_kline();
    test_check_types();
    test_check_order();
    test_save_new();
    test_save_edit();
    test_save_key();
    test_save_split();
    test_save_passover();
    test_save_toolong();
    test_err_field();
    test_find_key();
    test_choices();
    test_wipe();
    test_independence();
    test_lost();
    tl_hook = NULL;
    TF_EQ(link_close(g_lk), 0);
    te_remove(argv[1], NULL);
    return tf_done();
}
