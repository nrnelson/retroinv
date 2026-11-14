/*
 * INTL.C - dates and numbers in the forms of the Windows Control
 * Panel's International settings (INTL.H).
 *
 * A wire date is YYYYMMDD: the year at 0, the month at 4, the day at 6.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "INTL.H"

/* The characters of the run of p[0] that p starts with. */
static u16 run(const char *p)
{
    u16 n;

    for (n = 1; p[n] == p[0]; n++)
        ;
    return n;
}

/* The longest text of the run of n letters c of a picture, or 0 when
   the run is none of d, dd, M, MM, yy, yyyy. */
static u16 field_w(char c, u16 n)
{
    if (c == 'y')
        return (u16)(n == 2 || n == 4 ? n : 0);
    return (u16)((c == 'd' || c == 'M') && n <= 2 ? 2 : 0);
}

/* 1 when pic is a supported short date picture (INTL.H). */
static int supported(const char *pic)
{
    u16 w;
    u16 n;
    u16 fw;
    u8 seen;
    u8 bit;

    w = 0;
    seen = 0;
    while (*pic != '\0') {
        n = 1;
        fw = 1;
        if (TXT_ISALPHA(*pic)) {
            n = run(pic);
            fw = field_w(*pic, n);
            bit = (u8)(*pic == 'y' ? 1 : *pic == 'M' ? 2 : 4);
            if (fw == 0 || (seen & bit))
                return 0;
            seen |= bit;
        }
        w = (u16)(w + fw);
        if (w > MSG_DATEW)
            return 0;
        pic += n;
    }
    return seen == 7;
}

void intl_set(intl_fmt *f, const char *pic, const char *dec,
              const char *thou, int lzero)
{
    if (supported(pic))
        strcpy(f->date, pic);
    else
        f->date[0] = '\0';
    f->dec = dec[0] != '\0' ? dec[0] : '.';
    f->thou = thou[0];
    f->nolz = (u8)(lzero == 0);
}

int intl_date(const intl_fmt *f, char *out, const char *wire)
{
    const char *p;
    u16 n;
    u16 at;
    u16 len;

    if (!msg_date(out, wire) || f->date[0] == '\0')
        return out[0] != '\0';
    for (p = f->date; *p != '\0'; p += n) {
        n = 1;
        if (!TXT_ISALPHA(*p)) {
            *out++ = *p;
            continue;
        }
        n = run(p);
        /* yy is the year's last two digits; d and M drop a leading 0. */
        at = (u16)(*p == 'y' ? 4 - n : *p == 'M' ? 4 : 6);
        len = (u16)(*p == 'y' ? n : 2);
        if (n == 1 && wire[at] == '0') {
            at++;
            len--;
        }
        memcpy(out, wire + at, len);
        out += len;
    }
    *out = '\0';
    return 1;
}

/* Append c to out, of k characters, unless it holds n. */
static void put(char *out, u16 *k, u16 n, char c)
{
    if (*k < n)
        out[(*k)++] = c;
}

void intl_num(const intl_fmt *f, char *out, const char *v, u16 n)
{
    const char *s;
    const char *frac;
    u16 ni;
    u16 nf;
    u16 i;
    u16 k;

    while (*v == ' ')
        v++;
    s = v + (*v == '-');
    for (ni = 0; TXT_ISDIGIT(s[ni]); ni++)
        ;
    frac = s[ni] == '.' ? s + ni + 1 : NULL;
    nf = 0;
    if (frac != NULL)
        while (TXT_ISDIGIT(frac[nf]))
            nf++;
    if (ni + nf == 0 || (frac != NULL ? frac[nf] : s[ni]) != '\0') {
        txt_copy(out, v, (u16)(n + 1));
        return;
    }
    k = 0;
    if (s != v)
        put(out, &k, n, '-');
    if (frac != NULL && f->nolz && ni == 1 && s[0] == '0')
        ni = 0;
    else if (frac != NULL && !f->nolz && ni == 0)
        put(out, &k, n, '0');
    for (i = 0; i < ni; i++) {
        if (i > 0 && (ni - i) % 3 == 0 && f->thou != '\0')
            put(out, &k, n, f->thou);
        put(out, &k, n, s[i]);
    }
    if (frac != NULL) {
        put(out, &k, n, f->dec != '\0' ? f->dec : '.');
        for (i = 0; i < nf; i++)
            put(out, &k, n, frac[i]);
    }
    out[k] = '\0';
}
