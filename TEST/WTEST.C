/*
 * WTEST.C - the Windows side of the unit tests built as Windows programs:
 * the WinMain that runs a test's main, and the printf and sprintf the
 * test calls.
 *
 * usage: WIN NAME [ARG...]   (from the directory the report is to be
 *                            written in; NAME.EXE the program)
 *
 * TESTFW.H renames, under Windows, the test's main to tf_main and its
 * printf and sprintf to tf_printf and tf_sprintf.  WinMain runs tf_main
 * with argv[0] the program's path, from GetModuleFileName, and the
 * command line's blank-separated words after it, then ends the Windows
 * session with ExitWindows, so that WIN.COM returns to DOS; should
 * another program refuse to end, it returns alone.  WIN gives DOS no
 * exit status, so the program writes NAME.OUT in its current directory
 * instead, lines ending CR LF: every line the test prints, each written
 * as it is printed, then
 *
 *   NAME EXIT 0          tf_main's status
 *   NAME MODE STANDARD   REAL, STANDARD or ENHANCED, from GetWinFlags
 *   NAME FREE 412160     the bytes free in the global heap, GetFreeSpace
 *   NAME DGROUP 40960    the bytes of the data segment, static data,
 *                        stack and the local heap grown as the test went
 *   NAME STACK 3012      the bytes of the stack the test never reached
 *   NAME TICKS 182       how long tf_main ran (tmr_ticks)
 *
 * A test that leaves through exit() writes none of these and leaves
 * Windows running.
 *
 * The formatter takes the conversions the tests use: d, u, X and s, with
 * the flag 0, a width, and l for a long argument; any other character
 * after % is written as it is.  Its strings are near pointers, as the
 * medium model passes them, where USER's wsprintf reads far ones.
 */
#define COMPAT_FIND
#include <stdarg.h>
#include "COMPAT.H"
#include "CWIN.H"
#define TF_PART
#include "TESTFW.H"

#define WT_MAXARGS      16      /* entries of argv, the path included   */
#define WT_CMDMAX       128     /* a DOS command tail and its NUL       */
#define WT_EXTLEN       4       /* ".OUT"                               */
#define WT_BUFSZ        128     /* bytes of NAME.OUT gathered at once   */

static int wt_out = -1;         /* NAME.OUT, -1 when it did not open    */
static char wt_buf[WT_BUFSZ];
static u16 wt_n;

/* Write what wt_buf gathered to NAME.OUT. */
static void flush_out(void)
{
    if (wt_n != 0 && wt_out >= 0)
        cf_write(wt_out, wt_buf, wt_n);
    wt_n = 0;
}

/* A character of tf_printf into wt_buf, '\n' written as CR LF. */
static void put_out(void *arg, char c)
{
    if (c == '\n')
        put_out(arg, '\r');
    if (wt_n == WT_BUFSZ)
        flush_out();
    wt_buf[wt_n++] = c;
}

/* A character of tf_sprintf at *(char **)arg, which moves on. */
static void put_str(void *arg, char c)
{
    *(*(char **)arg)++ = c;
}

/* Format fmt and its arguments ap through put, as above; returns the
   characters given to put. */
static int format(void (*put)(void *arg, char c), void *arg,
                  const char *fmt, va_list ap)
{
    char dig[TXT_U32LEN];
    const char *s;
    char *d;
    u32 v;
    int n;
    int len;
    int width;
    int zero;
    int neg;

    n = 0;
    for (; *fmt != '\0'; fmt++) {
        if (*fmt != '%') {
            (*put)(arg, *fmt);
            n++;
            continue;
        }
        zero = *++fmt == '0';
        for (width = 0; TXT_ISDIGIT(*fmt); fmt++)
            width = width * 10 + (*fmt - '0');
        if (*fmt == '\0')
            break;
        v = 0;
        if (*fmt == 'l') {
            v = va_arg(ap, u32);
            fmt++;
        } else if (*fmt == 'd') {
            v = (u32)(long)va_arg(ap, int);
        } else if (*fmt == 'u' || *fmt == 'X') {
            v = va_arg(ap, unsigned);
        }
        neg = *fmt == 'd' && (v & 0x80000000UL) != 0;
        if (neg)
            v = 0UL - v;
        if (*fmt == 's') {
            s = va_arg(ap, const char *);
            for (len = 0; s[len] != '\0'; len++)
                ;
        } else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'X') {
            d = dig + sizeof dig;
            do {
                *--d = txt_hexdig[v % (*fmt == 'X' ? 16u : 10u)];
                v /= *fmt == 'X' ? 16u : 10u;
            } while (v != 0);
            s = d;
            len = (int)(dig + sizeof dig - d);
        } else {
            s = fmt;
            len = 1;
        }
        width -= len + neg;
        if (neg && zero)
            (*put)(arg, '-');
        for (; width > 0; width--, n++)
            (*put)(arg, zero ? '0' : ' ');
        if (neg && !zero)
            (*put)(arg, '-');
        for (n += len + neg; len > 0; len--)
            (*put)(arg, *s++);
    }
    return n;
}

int tf_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = format(put_out, NULL, fmt, ap);
    va_end(ap);
    flush_out();
    return n;
}

int tf_sprintf(char *out, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = format(put_str, &out, fmt, ap);
    va_end(ap);
    *out = '\0';
    return n;
}

/* The name of the mode Windows runs in, from GetWinFlags. */
static const char *mode_name(u32 flags)
{
    if (!(flags & WF_PMODE))
        return "REAL";
    return (flags & WF_ENHANCED) ? "ENHANCED" : "STANDARD";
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char path[DOS_PATHMAX];
    char words[WT_CMDMAX];
    char name[DOS_FINDNAME];
    char out[DOS_FINDNAME + WT_EXTLEN];
    char *argv[WT_MAXARGS + 1];
    char *base;
    char *p;
    u32 start;
    int argc;
    int rc;

    (void)prev;
    (void)show;
    cstk_fill();
    dos_inst = (u16)inst;
    GetModuleFileName(inst, path, (int)sizeof path);
    base = path;
    for (p = path; *p != '\0'; p++)
        if (*p == '\\' || *p == ':')
            base = p + 1;
    txt_copy(name, base, (u16)sizeof name);
    for (p = name; *p != '\0' && *p != '.'; p++)
        ;
    *p = '\0';
    if (txt_path_ext(out, (u16)sizeof out, name, "OUT", 0) != 0 ||
        cf_create(out, &wt_out) != 0)
        wt_out = -1;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    argv[0] = path;
    argc = 1;
    for (p = words; argc < WT_MAXARGS; ) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ')
            p++;
        if (*p != '\0')
            *p++ = '\0';
    }
    argv[argc] = NULL;
    start = tmr_ticks();
    rc = tf_main(argc, argv);
    tf_printf("%s EXIT %d\n%s MODE %s\n%s FREE %lu\n", name, rc, name,
              mode_name(GetWinFlags()), name,
              (unsigned long)GetFreeSpace(0));
    tf_printf("%s DGROUP %lu\n%s STACK %u\n%s TICKS %lu\n", name,
              (unsigned long)GlobalSize((HGLOBAL)LOWORD(
                  GlobalHandle(SELECTOROF((void FAR *)&wt_n)))),
              name, cstk_unused(), name, (unsigned long)tmr_since(start));
    if (wt_out >= 0)
        cf_close(wt_out);
    ExitWindows(0L, 0);
    return rc;
}
