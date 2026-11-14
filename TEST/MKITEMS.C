/*
 * MKITEMS.C - write a small sample database for compatibility checks.
 *
 * usage: MKITEMS dir
 *
 * Creates, in dir, ITEMS.DBF with ITEMS.DBT, LOCS.DBF with LOCS.DBT,
 * CATS.DBF, LTYPES.DBF and USERS.DBF with the field layouts of the
 * inventory schema, and fills them with a few retro-hardware records,
 * the locations a small tree.  Together the rows
 * use every field type (C, N, D, L, M) and the awkward cases a reader
 * must handle: a deleted record, zero and negative numbers, blank
 * numbers and dates, records without a memo, a zero-length memo, a memo
 * of several CR LF lines, one longer than a DBT_BLOCK and one longer
 * than 4096 bytes, and memo text with a line over 75 characters, a
 * dBASE soft line break (0x8D 0x0A), a bare CR and a bare LF.  The
 * header date is fixed to 2026-10-08 so the files are the same on every
 * run.
 *
 * Every sample user has blank SALT and PWHASH fields: the sample carries
 * no password material at all, so those accounts cannot log in.
 *
 * Existing files of the same names are replaced.  Exit status: 0
 * success, 1 usage error, 2 a file could not be written (the DBF_E code
 * is shown by name).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "TOOLSUP.H"

#define NAMEMAX      12         /* an 8.3 file name                     */
#define MAXCOLS      25         /* fields of the widest sample table    */

/* ---------------------------------------------------------------------
 * Table layouts.  Field order, widths and decimals are the on-disk
 * format that dBASE III Plus opens, so they must not be changed here
 * without changing the schema everywhere.
 * --------------------------------------------------------------------- */
static dbf_field items_fields[] = {
    { "ITEM_ID",   'N',  8, 0, 0 },
    { "CATEGORY",  'C',  4, 0, 0 },
    { "ITEMTYPE",  'C', 20, 0, 0 },
    { "MFR",       'C', 30, 0, 0 },
    { "MODEL",     'C', 30, 0, 0 },
    { "SERIALNO",  'C', 30, 0, 0 },
    { "DESCR",     'C', 60, 0, 0 },
    { "COND",      'C',  3, 0, 0 },
    { "WORKS",     'C',  1, 0, 0 },
    { "QTY",       'N',  5, 0, 0 },
    { "ON_LOAN",   'N',  5, 0, 0 },
    { "RESERVED",  'N',  5, 0, 0 },
    { "IN_USE",    'N',  5, 0, 0 },
    { "ATTACHED",  'N',  5, 0, 0 },
    { "LOC_ID",    'N',  8, 0, 0 },
    { "PARENT_ID", 'N',  8, 0, 0 },
    { "ACQ_DATE",  'D',  8, 0, 0 },
    { "ACQ_SRC",   'C', 30, 0, 0 },
    { "ACQ_PRICE", 'N',  9, 2, 0 },
    { "CUR_VALUE", 'N',  9, 2, 0 },
    { "TAGS",      'C', 60, 0, 0 },
    { "STATUS",    'C',  1, 0, 0 },
    { "CREATED",   'D',  8, 0, 0 },
    { "UPDATED",   'D',  8, 0, 0 },
    { "NOTES",     'M', 10, 0, 0 }
};

static dbf_field locs_fields[] = {
    { "LOC_ID",    'N',  8, 0, 0 },
    { "PARENT_ID", 'N',  8, 0, 0 },
    { "LOC_NAME",  'C', 30, 0, 0 },
    { "LOC_CODE",  'C', 10, 0, 0 },
    { "LTYPE",     'C',  8, 0, 0 },
    { "CAPACITY",  'N',  4, 0, 0 },
    { "NOTES",     'M', 10, 0, 0 }
};

static dbf_field cats_fields[] = {
    { "CAT_CODE",  'C',  4, 0, 0 },
    { "CAT_NAME",  'C', 20, 0, 0 },
    { "SORTORD",   'N',  2, 0, 0 }
};

static dbf_field ltypes_fields[] = {
    { "LT_CODE",   'C',  8, 0, 0 },
    { "LT_NAME",   'C', 20, 0, 0 },
    { "SORTORD",   'N',  2, 0, 0 }
};

static dbf_field users_fields[] = {
    { "USER_ID",   'N',  4, 0, 0 },
    { "USERNAME",  'C',  8, 0, 0 },
    { "FULLNAME",  'C', 30, 0, 0 },
    { "GROUPNAME", 'C',  8, 0, 0 },
    { "LEVEL",     'N',  1, 0, 0 },
    { "SALT",      'C',  4, 0, 0 },
    { "PWHASH",    'C', 32, 0, 0 },
    { "STATUS",    'C',  1, 0, 0 },
    { "MUSTCHG",   'L',  1, 0, 0 },
    { "LASTLOGIN", 'D',  8, 0, 0 },
    { "CREATED",   'D',  8, 0, 0 }
};

/* The number of elements of array a. */
#define NITEMS(a) ((u16)(sizeof(a) / sizeof((a)[0])))

/* ---------------------------------------------------------------------
 * Memo texts, one string per line; the lines are joined with CR LF.
 * --------------------------------------------------------------------- */
static const char *const memo_5150[] = {
    "Original IBM PC, type 5150, 64K-256K motherboard (rev B BIOS",
    "dated 10/27/82), fully populated with 256 KB.",
    "",
    "Two full-height Tandon TM100-2A 5.25in drives.  Case has a",
    "small dent on the left side; PSU fan replaced.",
    NULL
};

static const char *const memo_st225[] = {
    "Seagate ST-225, 20 MB, MFM, 615 cylinders, 4 heads, 17 sectors",
    "per track, half-height 5.25in, stepper-motor head positioner.",
    "",
    "Arrived stuck (stiction).  Freed by turning the spindle by hand",
    "through the bottom access hole with the drive powered off, then",
    "it spun up normally.  Low-level formatted with the WD 1002-WX2",
    "controller BIOS at C800:5, interleave 3, which suits a 4.77 MHz",
    "8088.  Defect list from the label: cyl 212 head 1, cyl 488",
    "head 3; both entered in the format program before formatting.",
    "",
    "SpinRite-style surface scan found no further bad sectors.  Keep",
    "parked (SHIPDISK) before moving; this model does not auto-park.",
    "Jumpers: drive select 2 with the twisted cable, terminator on.",
    NULL
};

static const char *const memo_kbd[] = {
    "Keys sticky after a spill; disposed of.",
    NULL
};

static const char *const memo_rtc[] = {
    "Battery inside the module is dead.  Kept only as a footprint",
    "sample for the replacement socket.",
    NULL
};

static const char *const memo_compaq[] = {
    "Burn-in pass: POST OK, 640 KB RAM test OK, both drives seek OK.",
    "Left running 8 hours at room temperature; no lockups.",
    NULL
};

/* Line breaks of every kind a memo can hold: CR LF between the strings,
   the dBASE soft break 0x8D 0x0A, a bare CR and a bare LF. */
static const char *const memo_cable[] = {
    "Flat 34-wire ribbon with the twist between the drive A: and drive B: "
    "connectors, two card-edge plugs and one IDC header for the controller.",
    "Typed in the dBASE memo editor, which wraps\215\nwith a soft break.",
    "A bare CR ends here\rand a bare LF ends here\nas well.",
    NULL
};

static const char *const memo_empty[] = {
    NULL
};

static const char *const memo_shelfa[] = {
    "Heavy steel shelving, top level holds complete systems.",
    "Keep away from the window.",
    NULL
};

/* ---------------------------------------------------------------------
 * Rows.  Values are given as text, one per field: C, D and L fields are
 * stored as written, N fields are decimal numbers ("" stores a blank
 * field), and M fields are ignored in favour of the row's memo (NULL
 * means no memo, which leaves the field blank).
 * --------------------------------------------------------------------- */
typedef struct {
    int deleted;
    const char *const *memo;    /* memo lines, NULL for no memo         */
    u16 repeat;                 /* times the memo lines are repeated    */
    const char *v[MAXCOLS];
} row;

static const row items_rows[] = {
    { 0, memo_5150, 1,
      { "1", "SYS", "Desktop PC", "IBM", "5150 Personal Computer", "0123456",
        "IBM PC with 256 KB, two 360 KB floppy drives, CGA", "GD", "Y", "1",
        "0", "0", "0", "1", "1", "0", "19960315", "Estate sale", "75.00",
        "1200.00", "IBM,8088,5150,1981", "A", "20260901", "20261008", "" } },
    { 0, NULL, 0,
      { "2", "CARD", "ISA VGA", "Paradise", "PVGA1A-JK", "PV8-04411",
        "8-bit ISA VGA card, 256 KB, works in an XT bus", "EXC", "Y", "1", "0",
        "0", "0", "0", "1", "1", "20240611", "VCF swap meet", "40.00", "85.50",
        "VGA,8-BIT,ISA", "A", "20260901", "20260901", "" } },
    { 0, memo_st225, 1,
      { "3", "DRV", "MFM HDD", "Seagate", "ST-225", "1234567-X",
        "20 MB half-height MFM hard disk", "GD", "I", "1", "1", "0", "0", "0",
        "2", "0", "20250120", "eBay", "25.00", "60.00", "MFM,ST-506,20MB", "A",
        "20260902", "20261001", "" } },
    { 1, memo_kbd, 1,
      { "4", "PERI", "Keyboard", "IBM", "Model F XT 83-key", "",
        "XT keyboard, stuck keys", "FR", "N", "1", "0", "0", "0", "0", "0",
        "0", "19980704", "Flea market", "5.00", "0.00", "KEYBOARD,XT", "D",
        "20260903", "20260910", "" } },
    { 0, memo_rtc, 1,
      { "5", "PART", "RTC module", "Dallas", "DS12887", "",
        "Real-time clock with built-in battery, dead", "", "N", "0", "0", "0",
        "0", "0", "2", "0", "", "", "0.00", "-5.00", "RTC,BATTERY", "A",
        "20260904", "20260904", "" } },
    { 0, NULL, 0,
      { "6", "SW", "Operating system", "Microsoft", "MS-DOS 3.10", "",
        "Original 5.25in disks with manual", "", "?", "1", "0", "0", "0", "0",
        "", "0", "", "", "", "", "DOS,FLOPPY", "A", "20260905", "", "" } },
    { 0, memo_empty, 1,
      { "7", "CPU", "8088 upgrade", "NEC", "V20 UPD70108C-8", "",
        "NEC V20 fitted in place of the 8088", "GD", "Y", "1", "0", "0", "0",
        "0", "1", "1", "20250301", "Parts bin", "3.50", "12.00", "CPU,V20",
        "A", "20260906", "20260906", "" } },
    { 0, memo_compaq, 36,
      { "8", "SYS", "Luggable PC", "Compaq", "Portable", "1075321",
        "Compaq Portable with 9in amber CRT, 640 KB, two FDD", "EXC", "Y", "1",
        "0", "0", "0", "0", "1", "0", "20230812", "Club auction", "150.00",
        "650.00", "COMPAQ,8088,PORTABLE,1983", "A", "20260907", "20260907",
        "" } },
    { 0, memo_cable, 1,
      { "9", "CBL", "Floppy cable", "", "34-pin twisted", "",
        "Floppy drive cable with twist, 2 drives", "GD", "?", "3", "0", "1",
        "1", "0", "2", "0", "", "Parts bin", "0.00", "2.00", "FDD,CABLE", "A",
        "20260908", "20260908", "" } }
};

static const row locs_rows[] = {
    { 0, memo_shelfa, 1,
      { "1", "4", "Shelf A, top level", "A1", "SHELF", "6", "" } },
    { 0, NULL, 0,
      { "2", "3", "Small parts bin 12", "B12", "BIN", "40", "" } },
    { 0, NULL, 0,
      { "3", "0", "Workbench", "", "RACK", "", "" } },
    { 0, NULL, 0,
      { "4", "0", "Garage", "GAR", "ROOM", "", "" } },
    { 1, NULL, 0,
      { "5", "4", "Closet shelf 3, software boxes", "C3", "SHELF", "",
        "" } }
};

static const row cats_rows[] = {
    { 0, NULL, 0, { "SYS",  "Systems",            "1"  } },
    { 0, NULL, 0, { "MOBO", "Motherboards",       "2"  } },
    { 0, NULL, 0, { "CPU",  "Processors",         "3"  } },
    { 0, NULL, 0, { "CARD", "Expansion cards",    "4"  } },
    { 0, NULL, 0, { "DRV",  "Drives",             "5"  } },
    { 0, NULL, 0, { "PERI", "Peripherals",        "6"  } },
    { 0, NULL, 0, { "SW",   "Software",           "7"  } },
    { 0, NULL, 0, { "MAN",  "Manuals",            "8"  } },
    { 0, NULL, 0, { "CBL",  "Cables",             "9"  } },
    { 0, NULL, 0, { "PART", "Parts",              "10" } }
};

static const row ltypes_rows[] = {
    { 0, NULL, 0, { "ROOM",    "Room",    "1" } },
    { 0, NULL, 0, { "SHELF",   "Shelf",   "2" } },
    { 0, NULL, 0, { "RACK",    "Rack",    "3" } },
    { 0, NULL, 0, { "CABINET", "Cabinet", "4" } },
    { 0, NULL, 0, { "DRAWER",  "Drawer",  "5" } },
    { 0, NULL, 0, { "BOX",     "Box",     "6" } },
    { 0, NULL, 0, { "BIN",     "Bin",     "7" } },
    { 0, NULL, 0, { "BUCKET",  "Bucket",  "8" } }
};

static const row users_rows[] = {
    { 0, NULL, 0,
      { "1", "ADMIN", "Administrator", "STAFF", "3", "", "", "A", "T",
        "", "20261008" } },
    { 0, NULL, 0,
      { "2", "GUEST", "Visitor account", "VISIT", "1", "", "", "D", "F",
        "20261001", "20260915" } },
    { 0, NULL, 0,
      { "3", "BENCH", "Workbench terminal", "STAFF", "2", "", "", "A",
        "", "", "20260920" } }
};

/* ---------------------------------------------------------------------
 * Writing.
 * --------------------------------------------------------------------- */
/* The output directory as a prefix, separator included (set_dir). */
static char g_dir[DOS_PATHMAX - NAMEMAX];

/* Stop the program when rc is an error. */
static void check(int rc, const char *what, const char *name)
{
    if (rc != DBF_OK) {
        printf("MKITEMS: %s %s: %s\n", what, name, tl_err_name(rc));
        exit(EXIT_FAIL);
    }
}

/* Take arg as the output directory, by the rule of the tests' scratch
   directories: g_dir is arg followed by '\' when arg holds a '\' or a
   ':', else by '/' (which DOS accepts in paths as well), and by nothing
   when arg is empty or already ends in '\', '/' or ':'. */
static void set_dir(const char *arg)
{
    size_t n;
    char sep;

    n = strlen(arg);
    sep = '\0';
    if (n > 0 && arg[n - 1] != '\\' && arg[n - 1] != '/' &&
        arg[n - 1] != ':') {
        if (strchr(arg, '\\') != NULL || strchr(arg, ':') != NULL)
            sep = '\\';
        else
            sep = '/';
    }
    if (n + 1 >= sizeof g_dir) {
        printf("MKITEMS: directory name too long\n");
        exit(EXIT_USAGE);
    }
    strcpy(g_dir, arg);
    g_dir[n] = sep;
    g_dir[n + 1] = '\0';
}

/* The output directory and name (at most NAMEMAX bytes) in out. */
static void make_path(char *out, const char *name)
{
    strcpy(out, g_dir);
    strcat(out, name);
}

/* Parse decimal text such as "-5.00" into a value scaled by 10^dec. */
static int parse_scaled(const char *s, u8 dec, i32 *out)
{
    i32 v;
    int neg;
    u8 frac;
    int seen_dot;
    int digits;

    v = 0;
    neg = 0;
    frac = 0;
    seen_dot = 0;
    digits = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    for (; *s != '\0'; s++) {
        if (*s == '.' && !seen_dot) {
            seen_dot = 1;
            continue;
        }
        if (*s < '0' || *s > '9')
            return 0;
        if (seen_dot) {
            if (frac >= dec)
                return 0;
            frac++;
        }
        if (v > 99999999L)
            return 0;
        v = (i32)(v * 10 + (i32)(*s - '0'));
        digits++;
    }
    if (digits == 0)
        return 0;
    for (; frac < dec; frac++)
        v = (i32)(v * 10);
    *out = neg ? -v : v;
    return 1;
}

/* Join the memo lines with CR LF and append them to the memo file. */
static u32 add_memo(dbt *m, const row *r, const char *name)
{
    size_t total;
    size_t len;
    size_t n;
    u16 i;
    u16 k;
    char *text;
    u32 block;

    total = 0;
    len = 0;
    block = 0;
    for (i = 0; r->memo[i] != NULL; i++)
        total += strlen(r->memo[i]) + 2;
    if (r->repeat > 0 && total > DBT_MAXMEMO / r->repeat)
        check(DBF_ERANGE, "memo for", name);
    total *= r->repeat;
    text = (char *)malloc(total + 1);
    if (text == NULL)
        check(DBF_ENOMEM, "memo for", name);
    for (k = 0; k < r->repeat; k++) {
        for (i = 0; r->memo[i] != NULL; i++) {
            n = strlen(r->memo[i]);
            if (len > 0) {
                memcpy(text + len, "\r\n", 2);
                len += 2;
            }
            memcpy(text + len, r->memo[i], n);
            len += n;
        }
    }
    text[len] = '\0';
    check(dbt_append(m, text, (u16)len, &block), "memo for", name);
    /* The memo header must name the new end before a record points at
       the memo. */
    check(dbt_flush(m), "flush memo of", name);
    free(text);
    return block;
}

static void put_row(dbf *t, dbt *m, const row *r, const char *name)
{
    const dbf_field *f;
    const char *v;
    i32 scaled;
    u16 i;
    u16 n;

    n = dbf_nfields(t);
    dbf_blank(t);
    for (i = 0; i < n; i++) {
        f = dbf_field_at(t, i);
        v = r->v[i];
        if (v == NULL)
            v = "";
        if (f->type == 'M') {
            if (r->memo != NULL)
                check(dbf_put_memo(t, i, add_memo(m, r, name)),
                      "memo field of", name);
        } else if (f->type == 'N') {
            if (*v == '\0') {
                check(dbf_put_blank(t, i), "field of", name);
            } else {
                if (!parse_scaled(v, f->dec, &scaled))
                    check(DBF_EARG, "number in", name);
                check(dbf_put_num(t, i, scaled), "number in", name);
            }
        } else {
            check(dbf_put_str(t, i, v), "field of", name);
        }
    }
    if (r->deleted)
        dbf_set_deleted(t, 1);
    check(dbf_append(t), "append to", name);
}

/* Create one table (and its memo file when memo is not NULL) and write
   the rows. */
static void make_table(const char *name, const char *memo,
                       dbf_field *fields, u16 nfields,
                       const row *rows, u16 nrows)
{
    char dbfpath[DOS_PATHMAX];
    char dbtpath[DOS_PATHMAX];
    dbf *t;
    dbt *m;
    u16 i;

    t = NULL;
    m = NULL;
    if (nfields > MAXCOLS) {
        printf("MKITEMS: %s has more than %u fields\n", name,
               (unsigned)MAXCOLS);
        exit(EXIT_FAIL);
    }
    make_path(dbfpath, name);
    check(dbf_create(dbfpath, fields, nfields), "create", dbfpath);
    if (memo != NULL) {
        make_path(dbtpath, memo);
        check(dbt_create(dbtpath), "create", dbtpath);
        check(dbt_open(&m, dbtpath, DBF_RDWR), "open", dbtpath);
    }
    check(dbf_open(&t, dbfpath, DBF_RDWR), "open", dbfpath);
    for (i = 0; i < nrows; i++)
        put_row(t, m, &rows[i], dbfpath);
    if (m != NULL)
        check(dbt_close(m), "close", dbtpath);
    check(dbf_close(t), "close", dbfpath);
    printf("MKITEMS: %s, %u records\n", name, (unsigned)nrows);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: MKITEMS dir\n");
        return EXIT_USAGE;
    }
    set_dir(argv[1]);

    dbf_set_today(2026, 10, 8);
    make_table("ITEMS.DBF", "ITEMS.DBT", items_fields, NITEMS(items_fields),
               items_rows, NITEMS(items_rows));
    make_table("LOCS.DBF", "LOCS.DBT", locs_fields, NITEMS(locs_fields),
               locs_rows, NITEMS(locs_rows));
    make_table("CATS.DBF", NULL, cats_fields, NITEMS(cats_fields),
               cats_rows, NITEMS(cats_rows));
    make_table("LTYPES.DBF", NULL, ltypes_fields, NITEMS(ltypes_fields),
               ltypes_rows, NITEMS(ltypes_rows));
    make_table("USERS.DBF", NULL, users_fields, NITEMS(users_fields),
               users_rows, NITEMS(users_rows));
    return EXIT_OK;
}
