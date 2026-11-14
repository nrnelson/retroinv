/*
 * INVDUMP.C - print the structure and records of a dBASE III table.
 *
 *   INVDUMP [/H] [/S] [/D] [/M] [/F:FIELD,FIELD...] [/R:from-to] FILE[.DBF]
 *
 * Without switches the program prints a header summary (version byte,
 * last-update date, record count, header and record lengths, whether the
 * table has a memo file), the field table (number, name, type, width,
 * decimals, byte offset in the record) and then every live record, one
 * per line:
 *
 *   recno: value|value|...
 *
 * Each value is the field's text with trailing spaces removed; numbers
 * also lose their leading spaces, dates are YYYYMMDD and memo fields show
 * their .DBT block number (empty when the record has no memo).
 *
 *   /H         header summary and field table only, no records
 *   /S         field table only
 *   /D         include deleted records, printed as "*recno: ..."
 *   /M         print the text of memo fields below each record, read from
 *              the .DBT file beside the table, indented MEMO_INDENT spaces
 *              and wrapped to LINE_WIDTH columns
 *   /F:A,B     print only the named fields, in the order given
 *   /R:n-m     print only records n to m; "n-" runs to the last record,
 *              "-m" starts at the first and "n" alone is one record; the
 *              last /R given counts, as the last /F does
 *
 * A file name without an extension gets ".DBF", or ".dbf" when the name
 * ends in a lower-case letter; the memo file's extension follows the
 * case of the table's.  Output is plain ASCII: bytes outside the
 * printable range are shown as '.'.  Memo text longer than
 * MEMO_BUFSZ - 1 bytes is cut and marked.  When /M is given but the memo
 * file cannot be opened, the error is reported and the records are
 * printed without memo text.
 *
 * Exit status: 0 success; 1 usage error; 2 the table or memo file cannot
 * be opened or read (the DBF_E code is shown by name), or memory ran
 * out.
 *
 * The memo buffer, MEMO_BUFSZ bytes, is allocated with malloc so the
 * program runs with a small stack; the other buffers, under 1 KB in all,
 * are locals of main.
 */
#define COMPAT_HEAP
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "COMPAT.H"
#include "DBF.H"
#include "TOOLSUP.H"

#define MEMO_BUFSZ      4096    /* bytes of memo text read per field    */
#define LINE_WIDTH      79      /* columns used by wrapped memo lines   */
#define MEMO_INDENT     4       /* spaces before each memo line         */
#define VALUE_BUFSZ     (DBF_MAXCHAR + 1)

/* dBASE III marks a soft line break in memo text as 0x8D 0x0A. */
#define SOFT_CR         0x8D

typedef struct {
    int header;                 /* print the header summary             */
    int records;                /* print the records                    */
    int deleted;                /* include deleted records              */
    int memo;                   /* print memo text                      */
    const char *fieldlist;      /* /F argument, or NULL for all fields  */
    const char *range;          /* the last /R argument, or NULL        */
    u32 from;                   /* first record number                  */
    u32 to;                     /* last record number                   */
    const char *file;
} options;

/* The exit status, raised to EXIT_FAIL by a memo or record that cannot be
   read, or a memo file that cannot be opened. */
static int dump_status = EXIT_OK;

static void usage(void)
{
    printf("usage: INVDUMP [/H] [/S] [/D] [/M] [/F:FIELD,FIELD...] "
           "[/R:from-to] FILE[.DBF]\n");
    printf("  /H  header and fields only     /S  fields only\n");
    printf("  /D  include deleted records    /M  print memo text\n");
    printf("  /F  print only these fields    /R  record range, e.g. "
           "/R:10-20\n");
}

/* Print one byte, replacing anything outside printable ASCII by '.'. */
static void put_byte(u8 c)
{
    if (c < 0x20 || c > 0x7E)
        c = '.';
    putchar((int)c);
}

/* Parse decimal digits at *p into *v and advance *p; 0 if there are no
   digits or the value exceeds 32 bits. */
static int parse_u32(const char **p, u32 *v)
{
    const char *s;
    u32 n;
    u32 d;

    s = *p;
    n = 0;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        d = (u32)(*s - '0');
        if (n > (u32)((0xFFFFFFFFUL - d) / 10UL))
            return 0;
        n = (u32)(n * 10U + d);
        s++;
    }
    *p = s;
    *v = n;
    return 1;
}

/* Parse "n-m", "n-", "-m" or "n" into o->from and o->to; a bound left
   out keeps the default parse_args set. */
static int parse_range(const char *s, options *o)
{
    if (*s == '-') {
        s++;
        if (!parse_u32(&s, &o->to))
            return 0;
    } else {
        if (!parse_u32(&s, &o->from))
            return 0;
        if (*s == '-') {
            s++;
            if (*s != '\0' && !parse_u32(&s, &o->to))
                return 0;
        } else {
            o->to = o->from;
        }
    }
    return *s == '\0' && o->from >= 1 && o->from <= o->to;
}

/* 1 when argv sets the options in o; otherwise prints why, except when
   no file is named, and returns 0 (a usage error). */
static int parse_args(int argc, char **argv, options *o)
{
    const char *a;
    int i;
    int h;
    int s;
    int arg;
    int known;
    char c;

    h = 0;
    s = 0;
    o->deleted = 0;
    o->memo = 0;
    o->fieldlist = NULL;
    o->range = NULL;
    o->file = NULL;
    for (i = 1; i < argc; i++) {
        a = argv[i];
        /* A switch is one letter, optionally followed by ':' and an
           argument; anything else, such as a host path "/tmp/X.DBF",
           is a file name.  /F and /R take the argument, the others
           stand alone. */
        if ((a[0] == '/' || a[0] == '-') && a[1] != '\0' &&
            (a[2] == '\0' || a[2] == ':')) {
            arg = a[2] == ':';
            known = !arg;
            c = a[1];
            switch (TXT_UPPER(c)) {
            case 'H':
                h = 1;
                break;
            case 'S':
                s = 1;
                break;
            case 'D':
                o->deleted = 1;
                break;
            case 'M':
                o->memo = 1;
                break;
            case 'F':
                known = arg;
                if (arg)
                    o->fieldlist = a + 3;
                break;
            case 'R':
                known = arg;
                if (arg)
                    o->range = a + 3;
                break;
            default:
                known = 0;
                break;
            }
            if (!known) {
                printf("INVDUMP: unknown switch %s\n", a);
                return 0;
            }
        } else if (o->file == NULL) {
            o->file = a;
        } else {
            printf("INVDUMP: more than one file name\n");
            return 0;
        }
    }
    if (o->file == NULL)
        return 0;
    o->from = 1;
    o->to = 0xFFFFFFFFUL;
    if (o->range != NULL && !parse_range(o->range, o)) {
        printf("INVDUMP: bad record range %s\n", o->range);
        return 0;
    }
    /* /H wins over /S: it prints the summary as well as the fields. */
    o->header = h || !s;
    o->records = !h && !s;
    return 1;
}

static void print_header(dbf *t, const char *path, const char *dbtpath)
{
    u16 year;
    u8 month;
    u8 day;

    year = 0;
    month = 0;
    day = 0;
    dbf_lastupdate(t, &year, &month, &day);
    printf("Table        %s\n", path);
    printf("Version      %02Xh (dBASE III%s)\n", (unsigned)dbf_version(t),
           dbf_has_memo(t) ? ", with memo" : "");
    printf("Last update  %04u-%02u-%02u\n", (unsigned)year,
           (unsigned)month, (unsigned)day);
    printf("Records      %lu\n", (unsigned long)dbf_reccount(t));
    printf("Header       %u bytes\n", (unsigned)dbf_hdrlen(t));
    printf("Record       %u bytes\n", (unsigned)dbf_reclen(t));
    printf("Fields       %u\n", (unsigned)dbf_nfields(t));
    if (dbf_has_memo(t))
        printf("Memo         yes (%s)\n", dbtpath);
    else
        printf("Memo         no\n");
    printf("\n");
}

static const char *type_name(char type)
{
    switch (type) {
    case 'C':
        return "Character";
    case 'N':
        return "Numeric";
    case 'D':
        return "Date";
    case 'L':
        return "Logical";
    case 'M':
        return "Memo";
    default:
        return "Unknown";
    }
}

static void print_fields(dbf *t)
{
    const dbf_field *f;
    u16 i;
    u16 n;

    n = dbf_nfields(t);
    printf("Field  Name        Type       Width  Dec  Offset\n");
    for (i = 0; i < n; i++) {
        f = dbf_field_at(t, i);
        printf("%5u  %-10s  %c %-9s %4u", (unsigned)(i + 1), f->name,
               f->type, type_name(f->type), (unsigned)f->len);
        if (f->type == 'N')
            printf("  %3u", (unsigned)f->dec);
        else
            printf("     ");
        printf("  %6u\n", (unsigned)f->offset);
    }
    printf("** Total **                   %5u\n", (unsigned)dbf_reclen(t));
}

/* Fill sel with the field indexes to print; returns the count, or 0 after
   reporting an unknown field name. */
static u16 select_fields(dbf *t, const char *list, u16 *sel)
{
    char name[DBF_NAMELEN + 1];
    u16 n;
    u16 len;
    int idx;
    int toolong;

    n = 0;
    if (list == NULL) {
        for (n = 0; n < dbf_nfields(t); n++)
            sel[n] = n;
        return n;
    }
    while (*list != '\0') {
        len = 0;
        toolong = 0;
        while (*list != '\0' && *list != ',') {
            if (len < DBF_NAMELEN)
                name[len++] = *list;
            else
                toolong = 1;
            list++;
        }
        name[len] = '\0';
        if (*list == ',')
            list++;
        if (len == 0)
            continue;
        idx = toolong ? -1 : dbf_field_index(t, name);
        if (idx < 0) {
            printf("INVDUMP: no field %s%s in the table\n", name,
                   toolong ? "..." : "");
            return 0;
        }
        if (n >= DBF_MAXFIELDS) {
            printf("INVDUMP: too many fields in /F\n");
            return 0;
        }
        sel[n++] = (u16)idx;
    }
    if (n == 0)
        printf("INVDUMP: /F names no fields\n");
    return n;
}

/* Record lines are written through out_byte so that the space after the
   colon is only printed when something follows it. */
static int dump_space;

static void out_byte(u8 c)
{
    if (dump_space) {
        putchar(' ');
        dump_space = 0;
    }
    put_byte(c);
}

static void out_text(const char *s)
{
    while (*s != '\0')
        out_byte((u8)*s++);
}

/* Print the indent that starts every non-empty memo line. */
static void memo_indent(void)
{
    int i;

    for (i = 0; i < MEMO_INDENT; i++)
        putchar(' ');
}

/* Print memo text: CR LF, LF, CR and the dBASE soft break end a line;
   long lines are wrapped. */
static void print_memo_text(const char *text, u16 len)
{
    u16 i;
    u16 col;
    int open;
    u8 c;

    col = 0;
    open = 0;
    for (i = 0; i < len; i++) {
        c = (u8)text[i];
        if (c == '\r' || c == SOFT_CR || c == '\n') {
            if (c != '\n' && i + 1 < len && text[i + 1] == '\n')
                i++;
            putchar('\n');
            open = 0;
            col = 0;
            continue;
        }
        if (open && col >= LINE_WIDTH - MEMO_INDENT) {
            putchar('\n');
            open = 0;
            col = 0;
        }
        if (!open) {
            memo_indent();
            open = 1;
        }
        put_byte(c);
        col++;
    }
    if (open)
        putchar('\n');
}

/* Print the text of memo field f, stored at block of m, read into buf
   (MEMO_BUFSZ bytes); a read error is reported and sets dump_status to
   EXIT_FAIL. */
static void print_memo(dbt *m, const dbf_field *f, u32 block, char *buf)
{
    u16 len;
    int rc;

    len = 0;
    rc = dbt_read(m, block, buf, (u16)MEMO_BUFSZ, &len);
    if (rc == DBF_ERANGE && len > 0) {
        printf("  %s (block %lu, first %u bytes):\n", f->name,
               (unsigned long)block, (unsigned)len);
        print_memo_text(buf, len);
        memo_indent();
        printf("[memo cut at %u bytes]\n", (unsigned)len);
    } else if (rc != DBF_OK) {
        printf("  %s (block %lu): %s\n", f->name, (unsigned long)block,
               tl_err_name(rc));
        dump_status = EXIT_FAIL;
    } else {
        printf("  %s (block %lu, %u bytes):\n", f->name,
               (unsigned long)block, (unsigned)len);
        print_memo_text(buf, len);
    }
}

/* Print the records o selects, the nsel fields sel names and, when m is
   not NULL, their memo text; value (VALUE_BUFSZ bytes) and memobuf
   (MEMO_BUFSZ bytes) are work buffers.  1, or 0 after reporting a record
   that cannot be read. */
static int print_records(dbf *t, dbt *m, const options *o, const u16 *sel,
                         u16 nsel, char *value, char *memobuf)
{
    const dbf_field *f;
    u32 last;
    u32 r;
    u32 block;
    u16 k;
    int rc;

    last = dbf_reccount(t);
    if (o->to < last)
        last = o->to;
    if (o->from > last)
        return 1;
    for (r = o->from; ; r++) {
        rc = dbf_read(t, r);
        if (rc != DBF_OK) {
            printf("INVDUMP: cannot read record %lu: %s\n",
                   (unsigned long)r, tl_err_name(rc));
            return 0;
        }
        if (!dbf_is_deleted(t) || o->deleted) {
            printf("%s%lu:", dbf_is_deleted(t) ? "*" : "",
                   (unsigned long)r);
            dump_space = 1;
            for (k = 0; k < nsel; k++) {
                f = dbf_field_at(t, sel[k]);
                if (k > 0)
                    out_byte('|');
                if (f->type == 'M') {
                    block = 0;
                    if (dbf_get_memo(t, sel[k], &block) == DBF_OK &&
                        block != 0) {
                        sprintf(value, "%lu", (unsigned long)block);
                        out_text(value);
                    }
                } else {
                    /* A value too long for the buffer cannot occur:
                       fields are at most DBF_MAXCHAR wide. */
                    dbf_get_str(t, sel[k], value, (u16)VALUE_BUFSZ);
                    out_text(value);
                }
            }
            dump_space = 0;
            putchar('\n');
            if (m != NULL) {
                for (k = 0; k < nsel; k++) {
                    f = dbf_field_at(t, sel[k]);
                    block = 0;
                    if (f->type == 'M' &&
                        dbf_get_memo(t, sel[k], &block) == DBF_OK &&
                        block != 0)
                        print_memo(m, f, block, memobuf);
                }
            }
        }
        if (r == last)
            break;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options o;
    char dbfpath[DOS_PATHMAX];
    char dbtpath[DOS_PATHMAX];
    char value[VALUE_BUFSZ];
    u16 sel[DBF_MAXFIELDS];
    char *memobuf;
    u16 nsel;
    dbf *t;
    dbt *m;
    int rc;

    dos_heap_top();
    if (!parse_args(argc, argv, &o)) {
        usage();
        return EXIT_USAGE;
    }
    if (txt_path_ext(dbfpath, DOS_PATHMAX, o.file, "DBF", 1) != 0 ||
        txt_path_ext(dbtpath, DOS_PATHMAX, dbfpath, "DBT", 0) != 0) {
        printf("INVDUMP: file name too long\n");
        return EXIT_USAGE;
    }

    memobuf = NULL;
    nsel = 0;
    m = NULL;
    rc = dbf_open(&t, dbfpath, DBF_RDONLY);
    if (rc != DBF_OK) {
        printf("INVDUMP: cannot open %s: %s\n", dbfpath, tl_err_name(rc));
        return EXIT_FAIL;
    }
    if (o.records) {
        nsel = select_fields(t, o.fieldlist, sel);
        if (nsel == 0) {
            dbf_close(t);
            return EXIT_USAGE;
        }
    }
    if (o.records && o.memo && dbf_has_memo(t)) {
        memobuf = (char *)malloc(MEMO_BUFSZ);
        if (memobuf == NULL) {
            printf("INVDUMP: %s\n", tl_err_name(DBF_ENOMEM));
            dbf_close(t);
            return EXIT_FAIL;
        }
        rc = dbt_open(&m, dbtpath, DBF_RDONLY);
        if (rc != DBF_OK) {
            printf("INVDUMP: cannot open %s: %s; memo text not shown\n",
                   dbtpath, tl_err_name(rc));
            m = NULL;
            dump_status = EXIT_FAIL;
        }
    }

    if (o.header)
        print_header(t, dbfpath, dbtpath);
    print_fields(t);
    if (o.records) {
        printf("\n");
        if (!print_records(t, m, &o, sel, nsel, value, memobuf))
            dump_status = EXIT_FAIL;
    }

    if (m != NULL)
        dbt_close(m);
    dbf_close(t);
    free(memobuf);
    return dump_status;
}
