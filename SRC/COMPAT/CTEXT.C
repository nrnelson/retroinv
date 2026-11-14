/*
 * CTEXT.C - the portable text helpers every layer shares: decimal and
 * hex digits, case-insensitive compares, bounded copies, word lists,
 * secret wipes, the calendar check, the NetBIOS name check, the checks
 * of device names and of the names of files written for the user,
 * extension replacement and directory parts of paths.
 *
 * The module is strict C89 and uses no compiler extension and no DOS
 * call, although it lives in SRC/COMPAT/, so that the engine, the
 * protocol, the client and the tools link one copy of each helper
 * without any of them crossing a layer.  It formats numbers itself and
 * includes neither <stdio.h> nor <ctype.h>.  Its only static data are
 * txt_hexdig, the device names of txt_dosdev and the refused bytes and
 * extensions of txt_outfile_ok.
 */
#define COMPAT_FIND
#include "COMPAT.H"
/* Turbo C 2.01 defines NULL in <stddef.h>, not in <string.h>. */
#include <stddef.h>
#include <string.h>

#define TX_DATELEN      8       /* digits of a YYYYMMDD date            */
#define TX_BASELEN      8       /* characters of a base name DOS keeps  */
#define TX_DEVLEN       3       /* letters of a device name's stem      */
#define TX_EXTLEN       3       /* characters of an extension DOS keeps */

const char txt_hexdig[] = "0123456789ABCDEF";

/* The device names: those of k_dev3, COM1..COM4 and LPT1..LPT3
   (k_devn, each with its highest digit), and k_clock. */
static const char k_dev3[] = "CONAUXPRNNUL";
static const char k_devn[] = "COM4LPT3";
static const char k_clock[] = "CLOCK$";

/* What txt_outfile_ok refuses: the bytes DOS refuses in names besides
   control bytes and spaces; the extensions of the data files; the
   first characters of the extensions of scratch files. */
static const char k_badch[] = "\"*+,;<=>?[]|";
static const char k_badext[] = "DBFDBTNDX";
static const char k_scrch[] = "&$%#";

u16 txt_u32(char *out, u32 v)
{
    char tmp[TXT_U32LEN];
    u16 n;
    int i;
    u16 w;

    n = 0;
    while (v > 0xFFFFUL) {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    w = (u16)v;
    do {
        tmp[n++] = (char)('0' + w % 10);
        w /= 10;
    } while (w != 0);
    for (i = 0; i < (int)n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

void txt_u32w(char *out, u32 v, u16 w, char pad)
{
    u16 s;

    while (v > 0xFFFFUL && w > 0) {
        out[--w] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    s = (u16)v;
    while (w > 0) {
        out[--w] = (char)('0' + s % 10);
        s /= 10;
        if (s == 0)
            break;
    }
    while (w > 0)
        out[--w] = pad;
}

u16 txt_num(const char *s, u16 max)
{
    u32 v;
    char c;

    v = 0;
    while ((c = *s++) != '\0' && v <= max) {
        if (!TXT_ISDIGIT(c))
            return 0;
        v = v * 10 + (u32)(c - '0');
    }
    return v <= max ? (u16)v : 0;
}

int txt_hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

int txt_ieq(const char *a, const char *b)
{
    while (TXT_UPPER(*a) == TXT_UPPER(*b)) {
        if (*a == '\0')
            return 1;
        a++;
        b++;
    }
    return 0;
}

int txt_ieqn(const char *a, const char *b, u16 n)
{
    for (; n > 0; n--) {
        if (TXT_UPPER(*a) != TXT_UPPER(*b))
            return 0;
        if (*a == '\0')
            break;
        a++;
        b++;
    }
    return 1;
}

void txt_copy(char *d, const char *s, u16 size)
{
    u16 n;

    if (size == 0)
        return;
    n = 0;
    if (s != NULL) {
        while (n < size - 1 && s[n] != '\0')
            n++;
        memcpy(d, s, n);
    }
    d[n] = '\0';
}

void txt_ucopy(char *d, const char *s, u16 size)
{
    u16 n;
    char c;

    for (n = 0; n < size - 1 && s[n] != '\0'; n++) {
        c = s[n];
        d[n] = (char)TXT_UPPER(c);
    }
    d[n] = '\0';
}

int txt_word_num(const char *list, const char *w)
{
    u16 n;
    u16 k;
    int i;

    n = (u16)strlen(w);
    for (i = 0;; i++) {
        while (*list == ' ')
            list++;
        if (*list == '\0')
            return -1;
        for (k = 0; list[k] != ' ' && list[k] != '\0'; k++)
            ;
        if (k == n && txt_ieqn(list, w, n))
            return i;
        list += k;
    }
}

int txt_dosdev(const char *s, u16 n)
{
    u16 i;

    if (n > TX_BASELEN)
        n = TX_BASELEN;
    if (n == TX_DEVLEN)
        for (i = 0; k_dev3[i] != '\0'; i += TX_DEVLEN)
            if (txt_ieqn(s, k_dev3 + i, TX_DEVLEN))
                return 1;
    if (n == TX_DEVLEN + 1)
        for (i = 0; k_devn[i] != '\0'; i += TX_DEVLEN + 1)
            if (txt_ieqn(s, k_devn + i, TX_DEVLEN) && s[TX_DEVLEN] >= '1' &&
                s[TX_DEVLEN] <= k_devn[i + TX_DEVLEN])
                return 1;
    return n == sizeof k_clock - 1 &&
           txt_ieqn(s, k_clock, (u16)(sizeof k_clock - 1));
}

int txt_outfile_ok(const char *path, u16 max)
{
    const u8 *p;
    u16 n;
    u16 i;
    u16 last;
    u16 dot;
    u16 ext;
    u8 c;

    p = (const u8 *)path;
    n = path != NULL ? (u16)strlen(path) : 0;
    if (n == 0 || n > max)
        return 0;
    last = 0;
    for (i = 0; i < n; i++) {
        c = p[i];
        if (c <= ' ' || c == 0x7F || strchr(k_badch, c) != NULL)
            return 0;
        if (c == '\\' || c == '/' || c == ':')
            last = (u16)(i + 1);
    }
    for (dot = last; dot < n && p[dot] != '.'; dot++)
        ;
    if (dot == last || txt_dosdev(path + last, (u16)(dot - last)))
        return 0;
    if (dot < n) {
        ext = (u16)(dot + 1);
        if (strchr(path + ext, '.') != NULL ||
            (ext < n && strchr(k_scrch, p[ext]) != NULL))
            return 0;
        if (n - ext >= TX_EXTLEN)
            for (i = 0; k_badext[i] != '\0'; i += TX_EXTLEN)
                if (txt_ieqn(path + ext, k_badext + i, TX_EXTLEN))
                    return 0;
    }
    return 1;
}

void txt_word_at(const char *list, u16 i, char *out, u16 size)
{
    u16 n;

    if (size == 0)
        return;
    for (;;) {
        while (*list == ' ')
            list++;
        if (*list == '\0' || i == 0)
            break;
        while (*list != ' ' && *list != '\0')
            list++;
        i--;
    }
    for (n = 0; n < size - 1 && list[n] != ' ' && list[n] != '\0'; n++)
        out[n] = list[n];
    out[n] = '\0';
}

void txt_wipe(void *p, u16 n)
{
    volatile u8 *q;

    for (q = (volatile u8 *)p; n > 0; n--)
        *q++ = 0;
}

int txt_date_ok(const char *s)
{
    int i;
    u16 y;
    u16 m;
    u16 d;
    u16 last;

    if (s == NULL)
        return 0;
    for (i = 0; i < TX_DATELEN; i++)
        if (!TXT_ISDIGIT(s[i]))
            return 0;
    if (s[TX_DATELEN] != '\0')
        return 0;
    y = 0;
    for (i = 0; i < 4; i++)
        y = (u16)(y * 10 + (u16)(s[i] - '0'));
    m = (u16)((s[4] - '0') * 10 + (s[5] - '0'));
    d = (u16)((s[6] - '0') * 10 + (s[7] - '0'));
    if (y == 0 || m < 1 || m > 12 || d < 1)
        return 0;
    /* February has 29 days in a leap year; the other months up to July
       have 31 days when odd, those from August when even. */
    if (m == 2)
        last = (u16)((y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 29 : 28);
    else
        last = (u16)(30 + ((m ^ (m >> 3)) & 1));
    return d <= last;
}

int txt_nbname_ok(const char *s)
{
    int n;

    if (s[0] == '*')
        return 0;
    for (n = 0; s[n] != '\0'; n++)
        if ((unsigned char)s[n] <= ' ' || s[n] == 0x7F ||
            n == NB_NAMELEN - 1)
            return 0;
    return n != 0;
}

/* CF_EINVAL for a result too long for out, which is left empty when it
   has room for the NUL. */
static int path_long(char *out, u16 outsz)
{
    if (outsz > 0)
        out[0] = '\0';
    return CF_EINVAL;
}

int txt_path_ext(char *out, u16 outsz, const char *path, const char *ext,
                 int keep)
{
    u16 n;
    u16 base;
    u16 i;
    int lower;
    char c;

    n = (u16)strlen(path);
    base = n;
    for (i = n; i > 0; i--) {
        c = path[i - 1];
        if (c == '.') {
            base = (u16)(i - 1);
            break;
        }
        if (c == '\\' || c == '/' || c == ':')
            break;
    }
    if (keep && base < n) {
        if (n >= outsz)
            return path_long(out, outsz);
        memcpy(out, path, (size_t)n + 1);
        return 0;
    }
    if ((u32)base + 1 + (u16)strlen(ext) >= outsz)
        return path_long(out, outsz);
    c = base < n ? path[base + 1] : (base > 0 ? path[base - 1] : 'A');
    lower = c >= 'a' && c <= 'z';
    memcpy(out, path, base);
    out += base;
    *out++ = '.';
    for (; *ext != '\0'; ext++) {
        c = *ext;
        if (lower && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        *out++ = c;
    }
    *out = '\0';
    return 0;
}

void txt_path_dir(char *out, u16 outsz, const char *path)
{
    u16 n;

    n = (u16)strlen(path);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/' &&
           path[n - 1] != ':')
        n--;
    if (n >= outsz)
        n = 0;
    memcpy(out, path, n);
    out[n] = '\0';
}
