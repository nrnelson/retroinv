/*
 * T_ENGIN3.C - the third part of the T_ENGINE test (see T_ENGINE.C):
 * the locations and walks phases, with the paging helper of the walks.
 * It is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_ENGINE.H holds what the parts share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGNOTE.H"
#include "ENGID.H"
#define ENGLOCS_WALKS
#include "ENGLOCS.H"
#include "ENGITEM.H"
#define TF_PART
#define TF_FILE "T_ENGIN3"
/* Checks keep no file name or expression text: Turbo C stores a
   separate copy of every string literal, which would cost this test
   kilobytes of near data. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#include "TESTFW.H"
#include "TENGH.H"
#include "T_ENGINE.H"

#define OUTMAX          600     /* the joined entries of one listing    */
#define PAGES_MAX       40      /* pages a listing may take, at most    */

static char g_out[OUTMAX];
static char g_path[ENG_LOCPATHLEN + 1];
static char g_first[ENG_CURSORMAX];
static char g_cur[ENG_CURSORMAX];
static u16 g_npages;

/* ---------------------------------------------------------------------
 * Paging a walk, as the protocol layer pages it.
 * --------------------------------------------------------------------- */
static int g_kind;              /* ENG_LW_                              */
static const char *g_loc;       /* its location argument                */
static const char *g_text;      /* item walks: the text of ITEM FIND,   */
static const char *g_cat;       /*   CAT= and STATUS= filters           */
static const char *g_stat;
static int g_flags;             /* item walks: ENG_IF_ switches         */

/* eng_loc_contents with the filters text, cat, stat and flags
   (eng_item_filters). */
static int contents(eng_lwalk *w, const char *loc, int all,
                    const char *text, const char *cat, const char *stat,
                    int flags, const char *after)
{
    eng_ifilt f;
    int rc;

    rc = eng_item_filters(g_e, &f, text, cat, stat, NULL, NULL, flags);
    if (rc == ENG_OK)
        rc = eng_loc_contents(g_e, w, loc, all, &f, after);
    return rc;
}

/* Open walk w of g_kind on g_loc at cursor after. */
static int wopen(eng_lwalk *w, const char *after)
{
    if (g_kind == ENG_LW_TREE)
        return eng_loc_list(g_e, w, g_loc, after);
    return contents(w, g_loc, g_kind == ENG_LW_ALL, g_text, g_cat, g_stat,
                    g_flags, after);
}

/* Run the walk page by page, pg entries a page, starting after the
   cursor from (NULL: the start), and join each entry as "name:depth"
   with ','; name is LOC_NAME of a location, ITEM_ID of an item.  After
   each entry it seeks location 1, item 1 and the first entries of
   LOCS2 and ITEMS2, as a caller may.  Sets
   g_npages, g_first (the cursor after the first page) and g_cur (after
   the last page that had one); "ERR n" when a call fails. */
static const char *wpages(u16 pg, const char *from)
{
    eng_lwalk w;
    char cur[ENG_CURSORMAX];
    char one[ENG_LOCNAMELEN + 8];
    u32 r;
    u16 depth;
    u16 got;
    int more;
    int rc;

    g_out[0] = '\0';
    strcpy(cur, from != NULL ? from : "");
    g_first[0] = '\0';
    g_npages = 0;
    do {
        rc = wopen(&w, cur);
        g_npages++;
        for (got = 0; rc == ENG_OK && got < pg; got++) {
            rc = eng_lwalk_next(g_e, &w, &r, &depth);
            if (rc != ENG_OK || r == 0)
                break;
            sprintf(one, "%s:%u", g_kind == ENG_LW_TREE ?
                    te_rtext(ENG_T_LOCS, TE_BUF, "LOC_NAME") :
                    te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID"),
                    (unsigned)depth);
            if (g_out[0] != '\0')
                strcat(g_out, ",");
            if (strlen(g_out) + strlen(one) < OUTMAX)
                strcat(g_out, one);
            TF_EQ(eng_loc_seek(g_e, 1, &r), ENG_OK);
            TF_EQ(eng_item_seek(g_e, 1, &r), ENG_OK);
            TF_EQ(ndx_first(eng_ndx(g_e, ENG_X_LOCS2), &r), DBF_OK);
            TF_EQ(ndx_first(eng_ndx(g_e, ENG_X_ITEMS2), &r), DBF_OK);
        }
        more = 0;
        if (rc == ENG_OK)
            rc = eng_lwalk_more(g_e, &w, &more);
        if (rc != ENG_OK) {
            sprintf(g_out, "ERR %d", rc);
            eng_clear_error(g_e);
            return g_out;
        }
        TF_CHECK(w.reads <= ENG_MAXREADS);
        cur[0] = '\0';
        if (more)
            eng_lwalk_cursor(&w, cur);
        if (more)
            strcpy(g_cur, cur);
        if (g_npages == 1)
            strcpy(g_first, cur);
    } while (cur[0] != '\0' && g_npages < PAGES_MAX);
    TF_CHECK(g_npages < PAGES_MAX);
    return g_out;
}

/* wpages of a walk of kind on loc from the start. */
static const char *walk(int kind, const char *loc, u16 pg)
{
    g_kind = kind;
    g_loc = loc;
    return wpages(pg, NULL);
}

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

/* LOC ADD of the pairs of spec, checked to succeed; the new LOC_ID, 0
   when the add failed. */
static u32 add_loc(const char *spec)
{
    u32 id;

    id = 0;
    if (!OK(eng_loc_add(g_e, te_p, te_kv(spec), &id)))
        printf("  LOC ADD %s\n", spec);
    return id;
}

/* The path of location id, "ERR n" when eng_loc_path fails; *depth
   receives its depth. */
static const char *path(u32 id, u16 *depth)
{
    int rc;

    rc = eng_loc_path(g_e, id, g_path, depth);
    if (rc != ENG_OK) {
        sprintf(g_path, "ERR %d", rc);
        eng_clear_error(g_e);
    }
    return g_path;
}

/* A location named c repeated n times, in parent, coded code; its
   LOC_ID. */
static u32 add_long(char c, u16 n, u32 parent, const char *code)
{
    char spec[TE_TEXTMAX];
    char name[ENG_LOCNAMELEN + 1];

    memset(name, c, n);
    name[n] = '\0';
    sprintf(spec, "LOC_NAME=%s|LTYPE=BOX|PARENT_ID=%lu|LOC_CODE=%s", name,
            (unsigned long)parent, code);
    return add_loc(spec);
}

/* ---------------------------------------------------------------------
 * Locations.
 * --------------------------------------------------------------------- */

/* ADD, GET and the rules of a new location. */
static void locs_add(void)
{
    u32 r;
    u32 a0;
    u32 id;

    a0 = te_naud();
    TF_EQ(add_loc("LOC_NAME=Office|LTYPE=room|LOC_CODE=off|CAPACITY=12|"
                  "NOTES=first"), 1);
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "LOCS|1|I|LOC_ID||1");
    if (OK(eng_loc_get(g_e, "off", &r))) {
        TF_EQ(r, 1);
        TF_STREQ(te_rtext(ENG_T_LOCS, TE_BUF, "LTYPE"), "ROOM");
        TF_STREQ(te_rtext(ENG_T_LOCS, TE_BUF, "LOC_CODE"), "OFF");
        TF_STREQ(te_rtext(ENG_T_LOCS, TE_BUF, "PARENT_ID"), "0");
        TF_STREQ(te_rtext(ENG_T_LOCS, TE_BUF, "CAPACITY"), "12");
    }
    if (OK(eng_loc_get(g_e, "0001", &r)))
        TF_EQ(r, 1);
    TF_EQ(add_loc("LOC_NAME=Shelf A|LTYPE=SHELF|LOC_CODE=SA|PARENT_ID=off"),
          2);
    TF_EQ(add_loc("LOC_NAME=Shelf B|LTYPE=SHELF|PARENT_ID=1"), 3);
    TF_EQ(add_loc("LOC_NAME=Bin 1|LTYPE=BIN|LOC_CODE=B-1|PARENT_ID=SA"), 4);
    TF_EQ(add_loc("LOC_NAME=Garage|LTYPE=ROOM|LOC_CODE=GAR|PARENT_ID=0"), 5);
    TF_EQ(add_loc("LOC_NAME=shelf a|LTYPE=SHELF|PARENT_ID=GAR"), 6);
    TF_STREQ(te_rtext(ENG_T_LOCS, 4, "PARENT_ID"), "2");
    OK(eng_loc_id(g_e, "sa", &id));
    TF_EQ(id, 2);
    OK(eng_loc_id(g_e, "000", &id));
    TF_EQ(id, 0);
    OK(eng_loc_id(g_e, "", &id));
    TF_EQ(id, 0);

    /* The rules: a name once among its siblings, codes once and well
       formed, the required fields, a known type and parent. */
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=SHELF A|LTYPE=SHELF|"
                                         "PARENT_ID=1"), &id), ENG_EDUP,
            "LOC_NAME", "name already used at this level");
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=Garage|LTYPE=ROOM"), &id),
            ENG_EDUP, "LOC_NAME", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|LOC_CODE=sa"),
                        &id), ENG_EDUP, "LOC_CODE",
            "location code already used");
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|LOC_CODE=1A"),
                        &id), ENG_EARG, "LOC_CODE", "bad location code");
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|LOC_CODE=-A"),
                        &id), ENG_EARG, "LOC_CODE", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|LOC_CODE=A_1"),
                        &id), ENG_EARG, "LOC_CODE", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|"
                                         "LOC_CODE=ABCDEFGHIJK"), &id),
            ENG_EARG, "LOC_CODE", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LTYPE=BIN"), &id), ENG_EARG,
            "LOC_NAME", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X"), &id), ENG_EARG,
            "LTYPE", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=TOTE"), &id),
            ENG_ECONSTR, "LTYPE", "unknown location type");
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|PARENT_ID=99"),
                        &id), ENG_ECONSTR, "PARENT_ID", "unknown location");
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|PARENT_ID=ZZ"),
                        &id), ENG_ECONSTR, "PARENT_ID", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|LOC_ID=7"),
                        &id), ENG_EARG, "LOC_ID", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|CAPACITY=-1"),
                        &id), ENG_EARG, "CAPACITY", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|COLOR=red"),
                        &id), ENG_EARG, "COLOR", NULL);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=0123456789012345678901234"
                                         "567890|LTYPE=BIN"), &id),
            ENG_EARG, "LOC_NAME", NULL);
    TE_FAIL(eng_loc_get(g_e, "C1", &r), ENG_ENOTFOUND, "",
            "no such location");
    TE_FAIL(eng_loc_get(g_e, "BAD CODE", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_get(g_e, "0", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_get(g_e, "", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_get(g_e, "100000001", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_id(g_e, "7", &id), ENG_ENOTFOUND, "", NULL);
    TF_EQ(id, 0);
}

/* Paths and the depth limit. */
static void locs_paths(void)
{
    char want[ENG_LOCPATHLEN + 1];
    u32 r;
    u32 id;
    u16 d;
    u16 i;

    TF_STREQ(path(4, &d), "Office > Shelf A > Bin 1");
    TF_EQ(d, 3);
    TF_STREQ(te_rtext(ENG_T_LOCS, TE_BUF, "LOC_ID"), "4");
    TF_STREQ(path(5, &d), "Garage");
    TF_EQ(d, 1);
    d = 9;
    OK(eng_loc_path(g_e, 2, NULL, &d));
    TF_EQ(d, 2);
    TF_STREQ(path(99, &d), "ERR 200");
    TF_STREQ(path(0, &d), "ERR 200");

    /* A path of exactly ENG_LOCPATHLEN, then one character more: the
       leading names give way to "... > ". */
    id = add_long('p', 24, 0, "P1");
    id = add_long('q', 24, id, "P2");
    id = add_long('r', 24, id, "P3");
    memset(want, 'p', 24);
    strcpy(want + 24, " > ");
    memset(want + 27, 'q', 24);
    strcpy(want + 51, " > ");
    memset(want + 54, 'r', 24);
    want[78] = '\0';
    TF_STREQ(path(id, &d), want);
    TF_EQ(strlen(g_path), ENG_LOCPATHLEN);
    OK(eng_loc_set(g_e, "P1", te_p,
                   te_kv("LOC_NAME=ppppppppppppppppppppppppp")));
    strcpy(want, "... > ");
    memset(want + 6, 'q', 24);
    strcpy(want + 30, " > ");
    memset(want + 33, 'r', 24);
    want[57] = '\0';
    TF_STREQ(path(id, &d), want);
    TF_EQ(d, 3);
    /* A name that fits with too little room left for "... > " is given
       up too. */
    id = add_long('w', 1, 0, "W1");
    id = add_long('x', 10, id, "W2");
    id = add_long('y', 30, id, "W3");
    id = add_long('z', 30, id, "W4");
    strcpy(want, "... > ");
    memset(want + 6, 'y', 30);
    strcpy(want + 36, " > ");
    memset(want + 39, 'z', 30);
    want[69] = '\0';
    TF_STREQ(path(id, &d), want);
    TF_EQ(d, 4);

    /* A chain down to ENG_MAXLOCDEPTH with the longest names: the cut
       keeps the two nearest; nothing goes below it. */
    id = 0;
    for (i = 0; i < ENG_MAXLOCDEPTH; i++) {
        sprintf(want, "D%u", (unsigned)(i + 1));
        id = add_long((char)('a' + i), ENG_LOCNAMELEN, id, want);
    }
    strcpy(want, "... > ");
    memset(want + 6, 'g', 30);
    strcpy(want + 36, " > ");
    memset(want + 39, 'h', 30);
    want[69] = '\0';
    TF_STREQ(path(id, &d), want);
    TF_EQ(d, ENG_MAXLOCDEPTH);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|PARENT_ID=D8"),
                        &r), ENG_ECONSTR, "PARENT_ID",
            "locations nested more than 8 levels deep");
    OK(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|PARENT_ID=D7"),
                   &r));
}

/* SET: names, codes and moves with their checks. */
static void locs_set(void)
{
    u32 a0;
    u32 r;
    u16 d;

    a0 = te_naud();
    OK(eng_loc_set(g_e, "4", te_p, te_kv("LOC_NAME=Bin One|CAPACITY=3")));
    TF_EQ(te_naud(), a0 + 2);
    TF_STREQ(aud(2), "LOCS|4|U|LOC_NAME|Bin 1|Bin One");
    OK(eng_loc_set(g_e, "B-1", te_p, te_kv("LOC_CODE=")));
    TF_STREQ(aud(1), "LOCS|4|U|LOC_CODE|B-1|");
    TE_FAIL(eng_loc_get(g_e, "B-1", &r), ENG_ENOTFOUND, "", NULL);
    OK(eng_loc_set(g_e, "4", te_p, te_kv("LOC_CODE=b-1")));
    OK(eng_loc_set(g_e, "B-1", te_p, te_kv("LOC_CODE=b-1|LOC_NAME=bin one")));
    TE_FAIL(eng_loc_set(g_e, "4", te_p, te_kv("LOC_CODE=OFF")), ENG_EDUP,
            "LOC_CODE", NULL);
    TE_FAIL(eng_loc_set(g_e, "SA", te_p, te_kv("LOC_NAME=Shelf B")), ENG_EDUP,
            "LOC_NAME", NULL);
    TE_FAIL(eng_loc_set(g_e, "SA", te_p, te_kv("LOC_ID=9")), ENG_EARG,
            "LOC_ID", NULL);
    TE_FAIL(eng_loc_set(g_e, "SA", te_p, te_kv("LTYPE=")), ENG_EARG, "LTYPE",
            NULL);
    TE_FAIL(eng_loc_set(g_e, "NONE", te_p, te_kv("CAPACITY=1")),
            ENG_ENOTFOUND, "", NULL);

    /* Moves: a name taken under the new parent, cycles, room below. */
    TE_FAIL(eng_loc_set(g_e, "SA", te_p, te_kv("PARENT_ID=GAR")), ENG_EDUP,
            "LOC_NAME", NULL);
    a0 = te_naud();
    OK(eng_loc_set(g_e, "3", te_p, te_kv("PARENT_ID=gar")));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "LOCS|3|U|PARENT_ID|1|5");
    TF_STREQ(path(3, &d), "Garage > Shelf B");
    OK(eng_loc_set(g_e, "3", te_p, te_kv("PARENT_ID=5")));
    TF_EQ(te_naud(), a0 + 1);
    TE_FAIL(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=B-1")),
            ENG_ECONSTR, "PARENT_ID",
            "cannot be put inside itself or its contents");
    TE_FAIL(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=1")), ENG_ECONSTR,
            "PARENT_ID", NULL);
    TE_FAIL(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=ZZ")), ENG_ECONSTR,
            "PARENT_ID", "unknown location");
    /* Office holds two levels below it: under D6 Bin One would lie at
       depth 9; under D5 it reaches ENG_MAXLOCDEPTH. */
    TE_FAIL(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=D6")), ENG_ECONSTR,
            "PARENT_ID", "locations nested more than 8 levels deep");
    OK(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=D5")));
    TF_STREQ(path(4, &d), "... > eeeeeeeeeeeeeeeeeeeeeeeeeeeeee > Office > "
                          "Shelf A > bin one");
    TF_EQ(d, ENG_MAXLOCDEPTH);
    TE_FAIL(eng_loc_add(g_e, te_p, te_kv("LOC_NAME=X|LTYPE=BIN|PARENT_ID=4"),
                        &r), ENG_ECONSTR, "PARENT_ID", NULL);
    OK(eng_loc_set(g_e, "OFF", te_p, te_kv("PARENT_ID=")));
    TF_STREQ(path(4, &d), "Office > Shelf A > bin one");
}

/* A deleted location below ENG_MAXLOCDEPTH takes no room: moves and
   walks, of the whole tree too, pass it over, and a cursor on it holds. */
static void locs_room(void)
{
    u8 key[ENG_LOCS2LEN];
    char at[ENG_CURSORMAX];
    u32 pb;
    u32 r;
    u16 d;

    pb = te_loc("PB", te_loc("PA", 0));
    te_loc("PC", pb);
    if (!OK(eng_loc_get(g_e, "PC", &r)) ||
        !TF_EQ(kx_eval(eng_kx(g_e, ENG_X_LOCS2), eng_dbf(g_e, ENG_T_LOCS),
                       key), DBF_OK))
        return;
    OK(eng_loc_del(g_e, "PC"));
    OK(eng_loc_set(g_e, "PA", te_p, te_kv("PARENT_ID=D6")));
    path(pb, &d);
    TF_EQ(d, ENG_MAXLOCDEPTH);
    TF_STREQ(walk(ENG_LW_TREE, "PA", 9), "PB:8");
    TF_STREQ(walk(ENG_LW_ALL, "PA", 9), "");
    TF_CHECK(strncmp(walk(ENG_LW_TREE, NULL, 50), "ERR", 3) != 0);
    eng_cursor_text(ENG_X_LOCS2, key, r, at);
    g_loc = "PA";
    TF_STREQ(wpages(9, at), "");
    TF_EQ(g_npages, 1);
}

/* DEL, the items at a location, notes. */
static void locs_del(void)
{
    char want[48];
    u32 r;
    u32 a0;
    u32 id;
    u16 len;
    u16 was;

    id = add_item("CATEGORY=CPU|LOC_ID=b-1");
    TE_FAIL(eng_loc_del(g_e, "B-1"), ENG_ECONSTR, "",
            "location holds items: move or delete them");
    TE_FAIL(eng_loc_del(g_e, "SA"), ENG_ECONSTR, "",
            "location holds locations: move or delete them");
    OK(eng_item_del(g_e, id));
    a0 = te_naud();
    OK(eng_loc_del(g_e, "B-1"));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "LOCS|4|D|||DELETED");
    OK(eng_loc_del(g_e, "2"));
    TE_FAIL(eng_loc_get(g_e, "4", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_del(g_e, "4"), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=CPU|LOC_ID=4"), &id),
            ENG_ECONSTR, "LOC_ID", NULL);
    TE_FAIL(eng_item_undel(g_e, 1), ENG_ECONSTR, "LOC_ID",
            "location not live");
    /* A deleted location's code and name may be taken again.  IDs
       taken from SEQ unused set LOC_ID apart from the record number,
       and the audit names LOC_ID. */
    OK(eng_seq_next(g_e, ENG_T_LOCS, 5, &r));
    id = add_loc("LOC_NAME=Shelf A|LTYPE=SHELF|LOC_CODE=SA|PARENT_ID=1");
    TF_EQ(id, r + 5);
    sprintf(want, "LOCS|%lu|I|LOC_ID||%lu", (unsigned long)id,
            (unsigned long)id);
    TF_STREQ(aud(1), want);
    OK(eng_loc_set(g_e, "SA", te_p, te_kv("LOC_NAME=Shelf Z")));
    sprintf(want, "LOCS|%lu|U|LOC_NAME|Shelf A|Shelf Z", (unsigned long)id);
    TF_STREQ(aud(1), want);
    if (OK(eng_loc_get(g_e, "SA", &r)))
        TF_CHECK(r != id);

    /* Notes. */
    if (OK(eng_loc_notes(g_e, "OFF", &len)))
        TF_STREQ(eng_memobuf(g_e), "first");
    OK(eng_loc_addnote(g_e, "off", "second", ENG_NOTE_ADD));
    TF_STREQ(aud(1), "LOCS|1|U|NOTES|MEMO|MEMO");
    if (OK(eng_loc_notes(g_e, "1", &len))) {
        TF_STREQ(eng_memobuf(g_e), "first\r\nsecond");
        TF_EQ(len, 13);
    }
    OK(eng_loc_addnote(g_e, "GAR", "only", ENG_NOTE_ADD));
    was = 0;
    if (OK(eng_loc_notes(g_e, "GAR", &was)))
        TF_STREQ(eng_memobuf(g_e), "only");
    TE_FAIL(eng_loc_addnote(g_e, "GAR", "a\rb", ENG_NOTE_ADD),
            ENG_EARG, "NOTES", NULL);
    TE_FAIL(eng_loc_addnote(g_e, "NONE", "a", ENG_NOTE_ADD),
            ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_loc_notes(g_e, "NONE", &len), ENG_ENOTFOUND, "", NULL);
    /* One byte over ENG_MAXMEMO with the line break, then exactly it. */
    memset(g_big, 'z', (size_t)(ENG_MAXMEMO - was - 1));
    g_big[ENG_MAXMEMO - was - 1] = '\0';
    TE_FAIL(eng_loc_addnote(g_e, "GAR", g_big, ENG_NOTE_ADD),
            ENG_ECONSTR, "NOTES", NULL);
    g_big[ENG_MAXMEMO - was - 2] = '\0';
    OK(eng_loc_addnote(g_e, "GAR", g_big, ENG_NOTE_ADD));
    if (OK(eng_loc_notes(g_e, "GAR", &len)))
        TF_EQ(len, ENG_MAXMEMO);
}

void test_locs(void)
{
    if (!phase_start())
        return;
    locs_add();
    locs_paths();
    locs_set();
    locs_room();
    locs_del();
    phase_end();
}

/* ---------------------------------------------------------------------
 * Walks: LOC LIST and LOC CONTENTS.
 * --------------------------------------------------------------------- */

/* The tree of the walks and its items:
     Garage (6)  Rack (7)     Slot 1 (8), Slot 2 (9)
     Office (1)  Shelf A (3)  bin 1 (5), Bin 2 (4)
                 Shelf B (2)
   items 1 at Office, 2 and its part 3 at bin 1, 4 at Garage, 5 at
   Shelf B, 6 of unknown location, 7 at Bin 2. */
static const char k_all[] = "Garage:1,Rack:2,Slot 1:3,Slot 2:3,Office:1,"
                            "Shelf A:2,bin 1:3,Bin 2:3,Shelf B:2";

static void walks_tree(void)
{
    te_loc("OFF", 0);
    add_loc("LOC_NAME=Shelf B|LTYPE=SHELF|PARENT_ID=1");
    add_loc("LOC_NAME=Shelf A|LTYPE=SHELF|PARENT_ID=1|LOC_CODE=SA");
    add_loc("LOC_NAME=Bin 2|LTYPE=BIN|PARENT_ID=3");
    add_loc("LOC_NAME=bin 1|LTYPE=BIN|PARENT_ID=3");
    te_loc("GAR", 0);
    add_loc("LOC_NAME=Rack|LTYPE=RACK|PARENT_ID=6");
    add_loc("LOC_NAME=Slot 1|LTYPE=BOX|PARENT_ID=7");
    add_loc("LOC_NAME=Slot 2|LTYPE=BOX|PARENT_ID=7");
    OK(eng_loc_set(g_e, "OFF", te_p, te_kv("LOC_NAME=Office")));
    OK(eng_loc_set(g_e, "GAR", te_p, te_kv("LOC_NAME=Garage")));
    add_item("CATEGORY=SYS|LOC_ID=1");
    add_item("CATEGORY=SYS|LOC_ID=5");
    set_parent(add_item("CATEGORY=CARD|LOC_ID=5"), 2);
    add_item("CATEGORY=SYS|LOC_ID=GAR");
    add_item("CATEGORY=SYS|LOC_ID=2");
    add_item("CATEGORY=SYS");
    add_item("CATEGORY=SYS|LOC_ID=4");
}

/* LOC LIST: the whole tree and a part of it. */
static void walks_list(void)
{
    TF_STREQ(walk(ENG_LW_TREE, NULL, 1), k_all);
    TF_EQ(g_npages, 9);
    TF_CHECK(strncmp(g_first, "LOCS2:20202020202020304741524147452020",
                     38) == 0);
    TF_STREQ(walk(ENG_LW_TREE, "0", 2), k_all);
    TF_EQ(g_npages, 5);
    TF_STREQ(walk(ENG_LW_TREE, "", 9), k_all);
    TF_EQ(g_npages, 1);
    TF_STREQ(walk(ENG_LW_TREE, "off", 1),
             "Shelf A:2,bin 1:3,Bin 2:3,Shelf B:2");
    TF_STREQ(walk(ENG_LW_TREE, "SA", 3), "bin 1:3,Bin 2:3");
    TF_STREQ(walk(ENG_LW_TREE, "5", 3), "");
    TF_STREQ(walk(ENG_LW_TREE, "ZZ", 3), "ERR 200");
}

/* LOC CONTENTS: one location, everything below it, the top filter, the
   cursors. */
static void walks_contents(void)
{
    eng_lwalk w;
    char at[ENG_CURSORMAX];

    TF_STREQ(walk(ENG_LW_HERE, "OFF", 1), "1:1");
    TF_STREQ(g_first, "");
    TF_STREQ(walk(ENG_LW_HERE, "5", 1), "2:3,3:3");
    TF_STREQ(g_first, "ITEMS2:20202020202020352020202020202032:2");
    g_flags = ENG_IF_TOP;
    TF_STREQ(walk(ENG_LW_HERE, "5", 1), "2:3");
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 2), "1:1,2:3,7:3,5:2");
    g_flags = 0;
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 1), "1:1,2:3,3:3,7:3,5:2");
    TF_STREQ(walk(ENG_LW_ALL, "1", 3), "1:1,2:3,3:3,7:3,5:2");
    TF_STREQ(walk(ENG_LW_ALL, "0", 2), "6:0,4:1,1:1,2:3,3:3,7:3,5:2");
    TF_STREQ(walk(ENG_LW_HERE, "0", 2), "6:0");
    TF_STREQ(walk(ENG_LW_ALL, "SA", 2), "2:3,3:3,7:3");
    TF_STREQ(walk(ENG_LW_ALL, "4", 2), "7:3");
    TF_STREQ(walk(ENG_LW_HERE, "ZZ", 2), "ERR 200");

    /* An item deleted between pages is passed over; a cursor of another
       location or walk is refused. */
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 2), "1:1,2:3,3:3,7:3,5:2");
    strcpy(at, g_first);
    OK(eng_item_del(g_e, 3));
    TF_STREQ(wpages(2, at), "7:3,5:2");
    TE_FAIL(contents(&w, "OFF", 0, NULL, NULL, NULL, 0, at), ENG_ECURSOR,
            NULL, NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, NULL, NULL, 0,
                     "ITEMS2:20202020202020362020202020202034:4"),
            ENG_ECURSOR, NULL, NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, NULL, NULL, 0,
                     "ITEMS2:20202020202039392020202020202031:1"),
            ENG_ECURSOR, NULL, NULL);
    OK(eng_item_undel(g_e, 3));
}

/* The item filters inside the walk: a page of one row passes over the
   items that fail them, so every page still holds the next kept item in
   tree order; the filters are checked as eng_item_scan checks them. */
static void walks_filters(void)
{
    eng_lwalk w;

    g_cat = "card";
    TF_STREQ(walk(ENG_LW_ALL, "0", 1), "3:3");
    g_cat = "SYS";
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 1), "1:1,2:3,7:3,5:2");
    g_cat = NULL;
    OK(eng_item_set(g_e, 7, te_p, te_kv("STATUS=D")));
    g_stat = "d";
    TF_STREQ(walk(ENG_LW_ALL, "0", 1), "7:3");
    g_stat = NULL;
    g_flags = ENG_IF_AVAIL;
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 1), "1:1,2:3,5:2");
    g_flags = ENG_IF_UNAVAIL;
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 1), "3:3,7:3");
    g_flags = 0;
    OK(eng_item_set(g_e, 7, te_p, te_kv("STATUS=A")));
    OK(eng_item_set(g_e, 2, te_p, te_kv("MFR=Compaq")));
    OK(eng_item_set(g_e, 5, te_p, te_kv("DESCR=a compaq clone")));
    g_text = "COMPAQ";
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 1), "2:3,5:2");
    TF_STREQ(walk(ENG_LW_HERE, "OFF", 1), "");
    g_text = "paq c";
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 2), "5:2");
    g_text = NULL;
    TE_FAIL(contents(&w, "OFF", 1, "", NULL, NULL, 0, NULL), ENG_EARG,
            "TEXT", NULL);
    TE_FAIL(contents(&w, "OFF", 1, "0123456789012345678901234567890", NULL,
                     NULL, 0, NULL), ENG_EARG, "TEXT", NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, "TOOLONG", NULL, 0, NULL), ENG_EARG,
            "CAT", NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, NULL, "AB", 0, NULL), ENG_EARG,
            "STATUS", NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, NULL, "A", ENG_IF_AVAIL, NULL),
            ENG_EARG, "STATUS", NULL);
    TE_FAIL(contents(&w, "OFF", 1, NULL, NULL, "D", ENG_IF_UNAVAIL, NULL),
            ENG_EARG, "STATUS", NULL);
}

/* A walk through more deleted items than one request may read: each
   page stops at the limit, and the cursor moves on. */
static void walks_bounded(void)
{
    char want[OUTMAX];
    u32 id;
    u32 last;

    id = add_item("CATEGORY=PART|LOC_ID=SA");
    raw_copies(id, ENG_MAXREADS);
    last = add_item("CATEGORY=PART|LOC_ID=SA");
    for (; id < last; id++)
        raw_delete(id);
    TF_EQ(dbf_flush(eng_dbf(g_e, ENG_T_ITEMS)), DBF_OK);
    sprintf(want, "%lu:2", (unsigned long)last);
    TF_STREQ(walk(ENG_LW_HERE, "SA", 5), want);
    TF_EQ(g_npages, 2);
    sprintf(want, "1:1,%lu:2,2:3,3:3,7:3,5:2", (unsigned long)last);
    TF_STREQ(walk(ENG_LW_ALL, "OFF", 50), want);
    TF_EQ(g_npages, 2);
}

/* A cursor stays good while the location it names, and those after it,
   are moved or deleted; one of another walk, or malformed, is
   refused. */
static void walks_cursor(void)
{
    eng_lwalk w;
    char at[ENG_CURSORMAX];
    u32 r;
    u16 d;

    /* The cursor's own location moved since is passed over. */
    TF_STREQ(walk(ENG_LW_TREE, NULL, 6), k_all);
    strcpy(at, g_first);
    OK(eng_loc_set(g_e, "SA", te_p, te_kv("PARENT_ID=GAR")));
    TF_STREQ(wpages(9, at), "Shelf B:2");
    OK(eng_loc_set(g_e, "SA", te_p, te_kv("PARENT_ID=OFF")));

    TF_STREQ(walk(ENG_LW_TREE, NULL, 3), k_all);
    strcpy(at, g_first);
    OK(eng_loc_del(g_e, "8"));
    TF_STREQ(wpages(1, at), "Slot 2:3,Office:1,Shelf A:2,bin 1:3,Bin 2:3,"
                            "Shelf B:2");
    OK(eng_loc_del(g_e, "9"));
    OK(eng_loc_del(g_e, "7"));
    TF_STREQ(wpages(2, at), "Office:1,Shelf A:2,bin 1:3,Bin 2:3,Shelf B:2");
    TF_STREQ(walk(ENG_LW_TREE, NULL, 1), "Garage:1,Office:1,Shelf A:2,"
                                         "bin 1:3,Bin 2:3,Shelf B:2");
    TE_FAIL(eng_loc_list(g_e, &w, "OFF", at), ENG_ECURSOR, NULL, NULL);
    TE_FAIL(eng_lwalk_next(g_e, &w, &r, &d), ENG_EINTERNAL, NULL, NULL);
    TE_FAIL(eng_loc_list(g_e, &w, "1", "LOCS2:zz:1"), ENG_ECURSOR, NULL,
            NULL);
    TE_FAIL(eng_loc_list(g_e, &w, NULL, "LOCS1:2020202020202031:1"),
            ENG_ECURSOR, NULL, NULL);
}

/* The walk of LOC_ID g_path at a cursor on location record r fails
   with rc. */
static void deep_cursor(u32 r, int rc)
{
    eng_lwalk w;
    u8 key[ENG_LOCS2LEN];

    if (TF_EQ(eng_rec_read(g_e, ENG_T_LOCS, r), ENG_OK) &&
        TF_EQ(kx_eval(eng_kx(g_e, ENG_X_LOCS2), eng_dbf(g_e, ENG_T_LOCS),
                      key), DBF_OK)) {
        eng_cursor_text(ENG_X_LOCS2, key, r, g_out);
        TE_FAIL(eng_loc_list(g_e, &w, g_path, g_out), rc, NULL, NULL);
    }
}

/* Damage: a loop of parents, a location of LOC_ID 0 and a chain past
   ENG_MAXLOCDEPTH, which no walk or cursor follows. */
static void walks_damage(void)
{
    eng_lwalk w;
    u32 first;
    u32 id;
    u32 r;
    u16 d;
    u16 i;

    TF_EQ(te_raw_put(ENG_T_LOCS, 1, ENG_X_LOCS2, "PARENT_ID", NULL, 2),
          ENG_OK);
    TF_STREQ(path(2, &d), "ERR 300");
    TE_FAIL(eng_loc_list(g_e, &w, "OFF", NULL), ENG_EIO, "",
            "location tree damaged");
    TE_FAIL(eng_loc_set(g_e, "SA", te_p, te_kv("PARENT_ID=GAR")), ENG_EIO, "",
            NULL);
    TF_STREQ(walk(ENG_LW_TREE, NULL, 9), "Garage:1");
    TF_EQ(te_raw_put(ENG_T_LOCS, 1, ENG_X_LOCS2, "PARENT_ID", NULL, 0),
          ENG_OK);
    /* Found on the page that reaches it, before the depth limit would
       stop the walk through the tree again below it. */
    TF_EQ(te_raw_put(ENG_T_LOCS, 2, ENG_X_LOCS2, "LOC_ID", NULL, 0), ENG_OK);
    TF_STREQ(walk(ENG_LW_TREE, NULL, 20), "ERR 300");
    TF_EQ(g_npages, 1);
    TF_EQ(te_raw_put(ENG_T_LOCS, 2, ENG_X_LOCS2, "LOC_ID", NULL, 2), ENG_OK);

    first = add_long('a', 1, 0, "");
    id = first;
    for (i = 1; i < ENG_MAXLOCDEPTH; i++)
        id = add_long((char)('a' + i), 1, id, "");
    te_loc("DEEP", 0);
    if (OK(eng_loc_get(g_e, "DEEP", &r)))
        TF_EQ(te_raw_put(ENG_T_LOCS, r, ENG_X_LOCS2, "PARENT_ID", NULL,
                         (i32)id), ENG_OK);
    TF_STREQ(path(id + 1, &d), "ERR 300");
    sprintf(g_cur, "%lu", (unsigned long)first);
    TF_STREQ(walk(ENG_LW_TREE, g_cur, 20), "ERR 300");
    TF_STREQ(walk(ENG_LW_ALL, "0", 50), "ERR 300");
    /* From a walk of 'a', a cursor on it is damage; one on a location
       below it climbs past the levels the walk may reach. */
    sprintf(g_path, "%lu", (unsigned long)first);
    deep_cursor(r, ENG_EIO);
    te_loc("DEEP2", 0);
    if (OK(eng_loc_get(g_e, "DEEP2", &r)) &&
        TF_EQ(te_raw_put(ENG_T_LOCS, r, ENG_X_LOCS2, "PARENT_ID", NULL,
                         (i32)(id + 1)), ENG_OK))
        deep_cursor(r, ENG_ECURSOR);
}

void test_walks(void)
{
    if (!phase_start())
        return;
    walks_tree();
    walks_list();
    walks_contents();
    walks_filters();
    walks_bounded();
    walks_cursor();
    walks_damage();
    phase_end();
}
