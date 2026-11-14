/*
 * KEYEXPR.C - dBASE III Plus index key expressions.
 *
 * An expression is compiled once into a tree of nodes and then evaluated
 * on each record.  Every character node has a fixed length, so the key
 * length is known at compile time.  The results are those of dBASE III
 * Plus 1.1, measured on dBASE itself:
 *
 * Names.  Field names and function names are case-insensitive and may be
 * surrounded by spaces, also between a function name and its "(".  A
 * function name is either the full name or its first four or more
 * letters (SUBS, SUBST and SUBSTR); a longer name (SUBSTRING) or a
 * shorter one (UPP) is unknown, so STR and DAY have no abbreviation.
 *
 * Character values.  A C field gives its full width, trailing spaces
 * included.  Constants are delimited by "", '' or [].  "+" concatenates.
 * UPPER changes only a-z; the CP437 accented letters are left alone.
 * RIGHT(c,n) gives the last n characters and SUBSTR(c,s,n) the n
 * characters from position s (counted from 1); SUBSTR(c,1) is all of c.
 *
 * Key length.  dBASE's INDEX ON takes the key length from the text of
 * the expression, not from a value.  When STR has no width, a RIGHT or
 * SUBSTR count is 0, negative or runs past the end of c, or SUBSTR
 * without a count starts after position 1, that length differs from the
 * length of the value (STR(PRICE) gets 1-byte keys, SUBSTR(c,s) the
 * width of c), and the index holds the value, a NUL and leftover bytes.
 * Such expressions are refused here (DBF_EARG), as is any count that is
 * not an integer constant, so every accepted expression has one key
 * length, kx_len, and it is the length dBASE stores in the index.
 *
 * Numbers.  dBASE III Plus keeps every number as an IEEE double but
 * computes in an extended format with a KX_XBITS-bit significand: each
 * result is rounded to KX_XBITS bits and then to a double, both times to
 * nearest with ties to even (num_round).  That double rounding applies
 * to converting field text and constants, to "+" and "-", and to both
 * roundings inside STR below.  "+" and "-" also give 0 when the result's
 * binary exponent is KX_FLUSH or more below that of the larger operand,
 * so 0.1 + 0.2 - 0.3 is exactly 0.  A numeric constant has at most
 * KX_MAXDIG significant digits and decimals.  YEAR, MONTH and DAY give
 * the date's parts, 0 for a blank date.  No floating point is used here:
 * a double is a 53-bit mantissa and a binary exponent, and the exact
 * arithmetic is done on multi-word integers (kxbn).
 *
 * N fields.  Every text gives a value, so a damaged field never stops
 * an index from being built.  A field whose first byte is '*' (dBASE
 * fills a field with them when a value does not fit) is the overflow
 * value, which STR writes as asterisks at any width; a sum with it is
 * it, and it minus itself is 0.  Any other text is read from its start:
 * spaces and tabs, an optional sign, digits with an optional decimal
 * point, and an optional exponent, E, e, D or d with an optional sign
 * and digits, of which only KX_EXPDIG are read after their leading zeros
 * (1E0123 is 1E12).  The number ends at the first byte that cannot
 * continue it, so "  12  12" is 12, "1.5.3" is 1.5 and "1E 2" is 1;
 * text with no nonzero digit before its exponent ("abc", "- 1", ".E2")
 * is 0.  The digits are read exactly, so every width up to DBF_MAXNUM
 * works.  Two values that only an exponent writes differ from dBASE:
 * one of 10^DBF_MAXNUM or more is the overflow value here, where dBASE
 * writes it in exponent form below 10^99, and one with more than
 * KX_MAXSCALE decimals is 0 here, as STR shows it in dBASE.
 *
 * STR(n,w[,d]), w 1..KX_MAXW, d 0..KX_MAXD and below w (default 0),
 * works in three steps:
 *   1. r = floor(n * 10^d + 1/2 rounded as above), with n * 10^d + 1/2
 *      computed exactly.  This is round half up at the double's value,
 *      except that a result just below an integer is rounded up to it,
 *      so 1.15 gives 1.2 although the double 1.15 is below 1.15, while
 *      1.005 gives 1.00; -0.5 gives 0 and -0.05 gives -0.1.
 *   2. y = r / 10^d rounded as above.
 *   3. y is written with d decimals, correctly rounded with ties to even
 *      (for r below 2^52 the digits are those of r).  If that is wider
 *      than w, a leading "0" before the point is dropped; if it is still
 *      too wide, d is lowered one at a time and y written again, so
 *      STR(1.25,3,2) is "1.2" and STR(1234.5,5,1) is " 1234".  When even
 *      d = 0 does not fit, or the integer part of y alone is wider than
 *      w, the result is w asterisks for w below KX_EFORMW, and otherwise
 *      exponent form: sign or space, ".", the first w - 6 digits of the
 *      integer part (not rounded), "E+" and the two-digit digit count,
 *      e.g. " .9E+08" for STR(99999999.6,7,1).  When it is the rounding
 *      at d = 0 that makes the number too wide, the digits are those of
 *      the rounded integer: STR(9999999.5,7,1) is " .1E+08".
 * The result is right-justified in w characters; a value that rounds to
 * zero has no minus sign.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"

#define KX_MAXNEST      16      /* deepest nesting of (), signs, calls  */
#define KX_MAXDEPTH     64      /* highest expression tree accepted     */
#define KX_MAXW         19      /* widest STR result                    */
#define KX_MAXD         15      /* most STR decimals                    */
#define KX_EFORMW       7       /* narrowest STR in exponent form       */
#define KX_MAXDIG       18      /* significant digits of a constant     */
#define KX_MAXARG       9999    /* largest integer constant argument    */
#define KX_FNAMELEN     6       /* longest function name, SUBSTR        */
#define KX_FLUSH        45      /* exponent drop that makes a sum 0     */
#define KX_MBITS        53      /* bits in a double's mantissa          */
#define KX_XBITS        64      /* bits of an extended significand      */
#define KX_NL           14      /* 16-bit words in a kxbn               */
#define KX_NDIG         72      /* room for the decimal digits of any
                                   kxbn, written KX_DIGRUN at a time    */
#define KX_DIGRUN       4       /* digits bn_digits takes per division  */
#define KX_DIGDIV       10000u  /* 10^KX_DIGRUN                         */
#define KX_FARBITS      120     /* an operand this many bits below the
                                   other cannot change the rounded sum  */
#define KX_INTBITS      32      /* STR of an integer below
                                   2^KX_INTBITS is written from a u32   */
#define KX_EXPDIG       2       /* exponent digits read after zeros     */
#define KX_MAXSCALE     38      /* most decimals of a field value       */
#define KX_OVERLOG      256     /* the overflow value is 2^KX_OVERLOG:
                                   more than KX_FARBITS bits above any
                                   other (below 2^72), so a sum with it
                                   is it                                */

/* bn_digits writes whole runs of KX_DIGRUN digits into KX_NDIG bytes. */
extern char kx_digruns[1];
extern char kx_digruns[(KX_NDIG % KX_DIGRUN == 0) ? 1 : 2];

/* num_from_dec scales a field value by up to KX_MAXSCALE decimals. */
extern char kx_scale[1];
extern char kx_scale[(4 * KX_MAXSCALE + KX_XBITS + 4 <= KX_NL * 16) ? 1 : 2];

/* A non-negative integer of KX_NL 16-bit words, least significant first.
   Every value used here is bounded well inside KX_NL * 16 bits: field
   values have at most DBF_MAXNUM digits (below 2^64) and constants at
   most KX_MAXDIG, so no operation carries out of the top word. */
typedef struct {
    u16 w[KX_NL];
} kxbn;

/* A double: (neg ? -1 : 1) * m * 2^e with 2^52 <= m < 2^53, or zero with
   m = 0 (and neg = 0).  m is stored as four 16-bit words. */
typedef struct {
    u16 m[4];
    int e;
    int neg;
} kxnum;

/* Node operations. */
#define KO_CFIELD       1       /* C field                              */
#define KO_CCONST       2       /* character constant                   */
#define KO_CAT          3       /* l + r, character                     */
#define KO_UPPER        4
#define KO_RIGHT        5
#define KO_SUBSTR       6       /* a = start (1-based)                  */
#define KO_STR          7       /* a = width, b = decimals              */
#define KO_NFIELD       8       /* N field                              */
#define KO_NCONST       9
#define KO_ADD          10
#define KO_SUB          11
#define KO_NEG          12
#define KO_YEAR         13
#define KO_MONTH        14
#define KO_DAY          15
#define KO_DFIELD       16      /* D field: only while parsing, then
                                   folded into its YEAR, MONTH or DAY   */

typedef struct kxnode kxnode;
struct kxnode {
    u8      op;
    char    type;           /* 'C', 'N' or 'D'                          */
    u8      depth;          /* height of the subtree                    */
    u16     len;            /* length of a character result             */
    u16     off;            /* field offset in the record; for YEAR,
                               MONTH and DAY that of their D field      */
    u16     a;              /* N field and STR: width; SUBSTR: start    */
    u16     b;              /* STR: decimals                            */
    kxnode *l;
    kxnode *r;
    u8     *buf;            /* constant text, the kxnum of a numeric
                               constant, or room for l's result         */
};

struct kx {
    kxnode *root;
    u16     reclen;         /* record length of the compile-time table  */
};

/* The scratch of one evaluation, on kx_eval's stack. */
typedef struct {
    kxbn    w1;             /* work numbers for the arithmetic          */
    kxbn    w2;
    char    dig[KX_NDIG];   /* decimal digits, most significant first   */
} kxwork;

/* ---------------------------------------------------------------------
 * Multi-word integers.
 * --------------------------------------------------------------------- */

static void bn_zero(kxbn *a)
{
    memset(a->w, 0, sizeof a->w);
}

/* Number of words up to the highest nonzero one (0 for zero); the
   words above it are zero, so the loops below stop there. */
static int bn_top(const kxbn *a)
{
    int n;

    n = KX_NL;
    while (n > 0 && a->w[n - 1] == 0)
        n--;
    return n;
}

/* Number of significant bits (0 for zero). */
static int bn_bitlen(const kxbn *a)
{
    int i;
    int n;
    u16 v;

    i = bn_top(a);
    if (i == 0)
        return 0;
    v = a->w[i - 1];
    n = 0;
    while (v != 0) {
        n++;
        v = (u16)(v >> 1);
    }
    return (i - 1) * 16 + n;
}

static int bn_bit(const kxbn *a, int i)
{
    if (i < 0 || i >= KX_NL * 16)
        return 0;
    return (a->w[i / 16] >> (i % 16)) & 1;
}

/* 1 when any of the n lowest bits is set. */
static int bn_low_any(const kxbn *a, int n)
{
    int i;

    for (i = 0; i < n / 16 && i < KX_NL; i++)
        if (a->w[i] != 0)
            return 1;
    if (n % 16 != 0 && i < KX_NL)
        if ((a->w[i] & (u16)((1u << (n % 16)) - 1u)) != 0)
            return 1;
    return 0;
}

static void bn_shl(kxbn *a, int k)
{
    int ws;
    int bs;
    int i;
    u16 lo;
    u16 hi;

    ws = k / 16;
    bs = k % 16;
    for (i = KX_NL - 1; i >= 0; i--) {
        hi = (u16)(i - ws >= 0 ? a->w[i - ws] : 0);
        lo = (u16)(i - ws - 1 >= 0 ? a->w[i - ws - 1] : 0);
        if (bs == 0)
            a->w[i] = hi;
        else
            a->w[i] = (u16)((hi << bs) | (lo >> (16 - bs)));
    }
}

static void bn_shr(kxbn *a, int k)
{
    int ws;
    int bs;
    int i;
    u16 lo;
    u16 hi;

    ws = k / 16;
    bs = k % 16;
    for (i = 0; i < KX_NL; i++) {
        lo = (u16)(i + ws < KX_NL ? a->w[i + ws] : 0);
        hi = (u16)(i + ws + 1 < KX_NL ? a->w[i + ws + 1] : 0);
        if (bs == 0)
            a->w[i] = lo;
        else
            a->w[i] = (u16)((lo >> bs) | (hi << (16 - bs)));
    }
}

static void bn_mul_small(kxbn *a, u16 m)
{
    int i;
    int n;
    u32 c;

    n = bn_top(a);
    c = 0;
    for (i = 0; i < n; i++) {
        c += (u32)a->w[i] * m;
        a->w[i] = (u16)(c & 0xFFFFu);
        c >>= 16;
    }
    if (n < KX_NL)
        a->w[n] = (u16)c;
}

static void bn_mul_pow10(kxbn *a, int d)
{
    while (d >= 4) {
        bn_mul_small(a, 10000u);
        d -= 4;
    }
    while (d > 0) {
        bn_mul_small(a, 10u);
        d--;
    }
}

/* a = a / d; returns the remainder. */
static u16 bn_div_small(kxbn *a, u16 d)
{
    int i;
    u32 r;

    r = 0;
    for (i = bn_top(a) - 1; i >= 0; i--) {
        r = (r << 16) | a->w[i];
        a->w[i] = (u16)(r / d);
        r %= d;
    }
    return (u16)r;
}

static void bn_add_small(kxbn *a, u16 v)
{
    int i;
    u32 c;

    c = v;
    for (i = 0; i < KX_NL && c != 0; i++) {
        c += a->w[i];
        a->w[i] = (u16)(c & 0xFFFFu);
        c >>= 16;
    }
}

static void bn_add(kxbn *a, const kxbn *b)
{
    int i;
    u32 c;

    c = 0;
    for (i = 0; i < KX_NL; i++) {
        c += (u32)a->w[i] + b->w[i];
        a->w[i] = (u16)(c & 0xFFFFu);
        c >>= 16;
    }
}

/* a = a - b, for a >= b. */
static void bn_sub(kxbn *a, const kxbn *b)
{
    int i;
    u32 x;
    u16 borrow;

    borrow = 0;
    for (i = 0; i < KX_NL; i++) {
        x = (u32)a->w[i] - b->w[i] - borrow;
        a->w[i] = (u16)(x & 0xFFFFu);
        borrow = (u16)((x >> 16) != 0 ? 1 : 0);
    }
}

static int bn_cmp(const kxbn *a, const kxbn *b)
{
    int i;

    for (i = KX_NL - 1; i >= 0; i--) {
        if (a->w[i] != b->w[i])
            return a->w[i] < b->w[i] ? -1 : 1;
    }
    return 0;
}

/* Write the decimal digits of a (destroyed) to dig, most significant
   first, without a NUL; returns their count (1 for zero). */
static int bn_digits(kxbn *a, char *dig)
{
    int n;
    int i;
    u16 r;
    char c;

    /* KX_DIGRUN digits per division, least significant first; the
       zeros above the leading digit are then dropped. */
    n = 0;
    do {
        r = bn_div_small(a, KX_DIGDIV);
        for (i = 0; i < KX_DIGRUN; i++) {
            dig[n++] = (char)('0' + r % 10u);
            r = (u16)(r / 10u);
        }
    } while (bn_top(a) != 0 && n + KX_DIGRUN <= KX_NDIG);
    while (n > 1 && dig[n - 1] == '0')
        n--;
    for (i = 0; i < n / 2; i++) {
        c = dig[i];
        dig[i] = dig[n - 1 - i];
        dig[n - 1 - i] = c;
    }
    return n;
}

/* ---------------------------------------------------------------------
 * Doubles.
 * --------------------------------------------------------------------- */

static int num_is_zero(const kxnum *x)
{
    return x->m[3] == 0 && x->m[2] == 0 && x->m[1] == 0 && x->m[0] == 0;
}

static void num_zero(kxnum *x)
{
    memset(x->m, 0, sizeof x->m);
    x->e = 0;
    x->neg = 0;
}

/* x = the overflow value 2^KX_OVERLOG, negative when neg. */
static void num_over(int neg, kxnum *x)
{
    num_zero(x);
    x->m[3] = (u16)(1u << (KX_MBITS - 1 - 3 * 16));
    x->e = KX_OVERLOG - (KX_MBITS - 1);
    x->neg = neg;
}

/* *x = *a (x may be a); Watcom C 6.5 refuses a struct assignment from
   a const object. */
static void num_copy(const kxnum *a, kxnum *x)
{
    if ((const kxnum *)x != a)
        memcpy(x, a, sizeof(kxnum));
}

/* Binary exponent: |x| is in [2^ilog, 2^(ilog+1)).  x must not be 0. */
static int num_ilog(const kxnum *x)
{
    return x->e + (KX_MBITS - 1);
}

static void num_to_bn(const kxnum *x, kxbn *b)
{
    bn_zero(b);
    b->w[0] = x->m[0];
    b->w[1] = x->m[1];
    b->w[2] = x->m[2];
    b->w[3] = x->m[3];
}

/* Round q * 2^exp to bits significant bits, ties to even; sticky is
   nonzero when the exact value is a little above q * 2^exp.  Returns the
   new exponent; q keeps at most bits bits. */
static int bn_round(kxbn *q, int exp, int sticky, int bits)
{
    int len;
    int sh;
    int half;

    len = bn_bitlen(q);
    if (len > bits) {
        sh = len - bits;
        half = bn_bit(q, sh - 1);
        if (bn_low_any(q, sh - 1))
            sticky = 1;
        bn_shr(q, sh);
        exp += sh;
        if (half && (sticky || (q->w[0] & 1u) != 0)) {
            bn_add_small(q, 1u);
            if (bn_bitlen(q) > bits) {
                bn_shr(q, 1);
                exp++;
            }
        }
    }
    return exp;
}

/* x = q * 2^exp as dBASE III Plus stores a result: rounded first to the
   KX_XBITS-bit significand of its extended format, then to a double,
   both times to nearest with ties to even.  sticky is nonzero when the
   exact value is a little above q * 2^exp, which callers only pass when
   q has more than KX_XBITS + 1 bits.  q is destroyed. */
static void num_round(kxbn *q, int exp, int sticky, int neg, kxnum *x)
{
    int len;

    if (bn_top(q) == 0) {
        num_zero(x);
        return;
    }
    exp = bn_round(q, exp, sticky, KX_XBITS);
    exp = bn_round(q, exp, 0, KX_MBITS);
    len = bn_bitlen(q);
    if (len < KX_MBITS) {
        bn_shl(q, KX_MBITS - len);
        exp -= KX_MBITS - len;
    }
    x->m[0] = q->w[0];
    x->m[1] = q->w[1];
    x->m[2] = q->w[2];
    x->m[3] = q->w[3];
    x->e = exp;
    x->neg = neg;
}

/* x = the double nearest to n / 10^dec (n is destroyed). */
static void num_from_dec(kxbn *n, int dec, int neg, kxnum *x)
{
    int t;
    int i;
    int sticky;

    if (bn_top(n) == 0) {
        num_zero(x);
        return;
    }
    /* Scale up so the quotient keeps at least KX_XBITS + 3 bits;
       10^dec < 2^(4 * dec). */
    t = 4 * dec + KX_XBITS + 4 - bn_bitlen(n);
    if (t < 0)
        t = 0;
    bn_shl(n, t);
    sticky = 0;
    for (i = 0; i < dec; i++)
        if (bn_div_small(n, 10u) != 0)
            sticky = 1;
    num_round(n, -t, sticky, neg, x);
}

/* x = a + b, or a - b when sub is nonzero, as dBASE III Plus adds. */
static void num_add(kxwork *wk, const kxnum *a, const kxnum *b, int sub,
                    kxnum *x)
{
    kxbn *p;
    kxbn *q;
    int bneg;
    int emin;
    int top;
    int neg;
    int c;

    if (num_is_zero(b)) {
        num_copy(a, x);
        return;
    }
    bneg = sub ? !b->neg : b->neg;
    if (num_is_zero(a)) {
        num_copy(b, x);
        x->neg = bneg;
        return;
    }
    if (a->e - b->e > KX_FARBITS) {
        num_copy(a, x);
        return;
    }
    if (b->e - a->e > KX_FARBITS) {
        num_copy(b, x);
        x->neg = bneg;
        return;
    }
    top = num_ilog(a) > num_ilog(b) ? num_ilog(a) : num_ilog(b);
    emin = a->e < b->e ? a->e : b->e;
    p = &wk->w1;
    q = &wk->w2;
    num_to_bn(a, p);
    bn_shl(p, a->e - emin);
    num_to_bn(b, q);
    bn_shl(q, b->e - emin);
    if (a->neg == bneg) {
        bn_add(p, q);
        neg = a->neg;
    } else {
        c = bn_cmp(p, q);
        if (c == 0) {
            num_zero(x);
            return;
        }
        if (c > 0) {
            bn_sub(p, q);
            neg = a->neg;
        } else {
            bn_sub(q, p);
            p = q;
            neg = bneg;
        }
    }
    num_round(p, emin, 0, neg, x);
    if (num_ilog(x) <= top - KX_FLUSH)
        num_zero(x);
    else if (num_ilog(x) > KX_OVERLOG)
        num_over(neg, x);
}

/* ---------------------------------------------------------------------
 * STR().
 * --------------------------------------------------------------------- */

/* a = |floor(x)|, the magnitude of the integer at or below x. */
static void num_floor(const kxnum *x, kxbn *a)
{
    int frac;

    num_to_bn(x, a);
    if (x->e >= 0) {
        bn_shl(a, x->e);
        return;
    }
    frac = bn_low_any(a, -x->e);
    bn_shr(a, -x->e);
    if (x->neg && frac)
        bn_add_small(a, 1u);
}

/* The w-character overflow result for a number whose integer part has
   the nd digits in wk->dig. */
static void str_overflow(kxwork *wk, int nd, int neg, int w, u8 *out)
{
    int i;

    if (w < KX_EFORMW) {
        memset(out, '*', (size_t)w);
        return;
    }
    out[0] = (u8)(neg ? '-' : ' ');
    out[1] = '.';
    for (i = 0; i < w - 6; i++)
        out[2 + i] = (u8)(i < nd ? wk->dig[i] : '0');
    out[w - 4] = 'E';
    out[w - 3] = '+';
    out[w - 2] = (u8)('0' + nd / 10 % 10);
    out[w - 1] = (u8)('0' + nd % 10);
}

/* Write the nd digits of wk->dig right-justified in w characters, after
   a '-' when neg, or the overflow result when they do not fit. */
static void str_int(kxwork *wk, int nd, int neg, int w, u8 *out)
{
    int len;

    len = (neg ? 1 : 0) + nd;
    if (len > w) {
        str_overflow(wk, nd, neg, w, out);
        return;
    }
    memset(out, ' ', (size_t)(w - len));
    if (neg)
        out[w - len] = '-';
    memcpy(out + (w - nd), wk->dig, (size_t)nd);
}

/* Write STR(x, w, d) to out (w bytes). */
static void str_format(kxwork *wk, const kxnum *x, int w, int d, u8 *out)
{
    kxnum y;
    kxbn *a;
    kxbn *b;
    int s;
    int tneg;
    int neg;
    int nd;
    int dd;
    int half;
    int below;
    int sign;
    int ilen;
    int len;
    int i;
    int pos;

    if (!num_is_zero(x) && num_ilog(x) >= KX_OVERLOG) {
        memset(out, '*', (size_t)w);
        return;
    }
    a = &wk->w1;
    b = &wk->w2;

    /* An integer below 2^KX_INTBITS with no decimals is written from a
       u32: x + 1/2 is exact, so steps 1 and 2 below give x itself. */
    if (d == 0 && (num_is_zero(x) || num_ilog(x) < KX_INTBITS)) {
        num_to_bn(x, a);
        s = x->e < 0 ? -x->e : 0;
        if (!bn_low_any(a, s)) {
            bn_shr(a, s);
            nd = (int)txt_u32(wk->dig, (u32)a->w[0] | ((u32)a->w[1] << 16));
            str_int(wk, nd, x->neg, w, out);
            return;
        }
    }

    /* Step 1: r = floor(nearest double to x * 10^d + 1/2), into a. */
    tneg = 0;
    if (num_is_zero(x) || num_ilog(x) + 1 + 4 * d <= -58) {
        /* |x * 10^d| < 2^-58: the sum rounds to exactly 1/2. */
        bn_zero(a);
    } else {
        s = -x->e;
        if (s < 1)
            s = 1;
        /* a = |x| * 10^d * 2^s, b = 2^(s-1): the sum scaled by 2^s. */
        num_to_bn(x, a);
        bn_mul_pow10(a, d);
        bn_shl(a, x->e + s);
        bn_zero(b);
        b->w[(s - 1) / 16] = (u16)(1u << ((s - 1) % 16));
        if (!x->neg) {
            bn_add(a, b);
        } else if (bn_cmp(a, b) > 0) {
            bn_sub(a, b);
            tneg = 1;
        } else {
            bn_sub(b, a);
            *a = *b;
        }
        num_round(a, -s, 0, tneg, &y);
        num_floor(&y, a);
    }

    /* Step 2: y = nearest double to r / 10^d. */
    neg = tneg && bn_top(a) != 0;
    num_from_dec(a, d, neg, &y);

    /* Step 3: the integer part of |y| must fit. */
    y.neg = 0;
    num_floor(&y, a);
    nd = bn_digits(a, wk->dig);
    if ((neg ? 1 : 0) + nd > w) {
        str_overflow(wk, nd, neg, w, out);
        return;
    }

    for (dd = d; dd >= 0; dd--) {
        /* a = |y| * 10^dd rounded to an integer, ties to even. */
        num_to_bn(&y, a);
        bn_mul_pow10(a, dd);
        if (y.e >= 0) {
            bn_shl(a, y.e);
        } else {
            half = bn_bit(a, -y.e - 1);
            below = bn_low_any(a, -y.e - 1);
            bn_shr(a, -y.e);
            if (half && (below || (a->w[0] & 1u) != 0))
                bn_add_small(a, 1u);
        }
        sign = neg && bn_top(a) != 0;
        nd = bn_digits(a, wk->dig);
        ilen = nd > dd ? nd - dd : 1;   /* integer digits, "0" if none */
        len = sign + ilen + (dd > 0 ? 1 + dd : 0);
        if (len > w && dd > 0 && nd <= dd) {
            ilen = 0;                   /* drop the leading "0" */
            len--;
        }
        if (len <= w) {
            memset(out, ' ', (size_t)(w - len));
            pos = w - len;
            if (sign)
                out[pos++] = '-';
            /* Digits of the value padded with zeros to ilen + dd. */
            for (i = 0; i < ilen + dd; i++) {
                if (dd > 0 && i == ilen)
                    out[pos++] = '.';
                s = i - (ilen + dd - nd);
                out[pos++] = (u8)(s >= 0 ? wk->dig[s] : '0');
            }
            return;
        }
    }
    str_overflow(wk, nd, neg, w, out);
}

/* ---------------------------------------------------------------------
 * Evaluation.
 * --------------------------------------------------------------------- */

/* Year, month or day of a D field; 0 when the date is blank or not
   eight digits. */
static u16 date_part(const u8 *p, int op)
{
    int i;
    u16 v;

    for (i = 0; i < DBF_DATELEN; i++)
        if (!TXT_ISDIGIT(p[i]))
            return 0;
    if (op == KO_YEAR) {
        v = 0;
        for (i = 0; i < 4; i++)
            v = (u16)(v * 10u + (u16)(p[i] - '0'));
        return v;
    }
    i = op == KO_MONTH ? 4 : 6;
    return (u16)((p[i] - '0') * 10 + (p[i + 1] - '0'));
}

/* The exponent of the len-byte N field text at p whose number ends
   before p[i], as the header comment reads it; 0 when there is none. */
static int field_exp(const u8 *p, u16 i, u16 len)
{
    int neg;
    int ex;
    int n;
    u8 c;

    c = (u8)(i < len ? p[i] : 0);
    if (c != 'E' && c != 'e' && c != 'D' && c != 'd')
        return 0;
    i++;
    neg = i < len && p[i] == '-';
    if (i < len && (p[i] == '-' || p[i] == '+'))
        i++;
    while (i < len && p[i] == '0')
        i++;
    ex = 0;
    for (n = 0; n < KX_EXPDIG && i < len; n++, i++) {
        c = p[i];
        if (!TXT_ISDIGIT(c))
            break;
        ex = ex * 10 + (c - '0');
    }
    return neg ? -ex : ex;
}

/* x = the value of the len-byte N field text at p, read as dBASE III
   Plus reads it (see the header comment). */
static void field_num(kxwork *wk, const u8 *p, u16 len, kxnum *x)
{
    u16 i;
    int neg;
    int point;
    int dec;
    int sig;
    int ex;
    u8 c;

    if (len > 0 && p[0] == '*') {
        num_over(0, x);
        return;
    }
    bn_zero(&wk->w1);
    neg = 0;
    point = 0;
    dec = 0;
    sig = 0;
    i = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t'))
        i++;
    if (i < len && (p[i] == '-' || p[i] == '+')) {
        neg = p[i] == '-';
        i++;
    }
    for (; i < len; i++) {
        c = p[i];
        if (c == '.' && !point) {
            point = 1;
        } else if (TXT_ISDIGIT(c)) {
            if (point)
                dec++;
            if (sig > 0 || c != '0')
                sig++;
            bn_mul_small(&wk->w1, 10u);
            bn_add_small(&wk->w1, (u16)(c - '0'));
        } else {
            break;
        }
    }
    if (sig == 0) {
        num_zero(x);
        return;
    }
    ex = field_exp(p, i, len) - dec;    /* the value is w1 * 10^ex */
    if (sig + ex > DBF_MAXNUM) {
        num_over(neg, x);
    } else if (ex < -KX_MAXSCALE) {
        num_zero(x);
    } else {
        if (ex > 0)
            bn_mul_pow10(&wk->w1, ex);
        num_from_dec(&wk->w1, ex < 0 ? -ex : 0, neg, x);
    }
}

static int neval(kxwork *wk, kxnode *n, dbf *t, kxnum *x)
{
    kxnum v;
    int rc;

    switch (n->op) {
    case KO_NFIELD:
        field_num(wk, dbf_recbuf(t) + n->off, n->a, x);
        return DBF_OK;
    case KO_NCONST:
        num_copy((const kxnum *)n->buf, x);
        return DBF_OK;
    case KO_ADD:
    case KO_SUB:
        rc = neval(wk, n->l, t, x);
        if (rc == DBF_OK)
            rc = neval(wk, n->r, t, &v);
        if (rc == DBF_OK)
            num_add(wk, x, &v, n->op == KO_SUB, x);
        return rc;
    case KO_NEG:
        rc = neval(wk, n->l, t, x);
        if (rc == DBF_OK && !num_is_zero(x))
            x->neg = !x->neg;
        return rc;
    case KO_YEAR:
    case KO_MONTH:
    case KO_DAY:
        bn_zero(&wk->w1);
        wk->w1.w[0] = date_part(dbf_recbuf(t) + n->off, n->op);
        num_from_dec(&wk->w1, 0, 0, x);
        return DBF_OK;
    default:
        return DBF_EARG;
    }
}

static int ceval(kxwork *wk, kxnode *n, dbf *t, u8 *out)
{
    kxnum v;
    u16 i;
    u8 c;
    int rc;

    switch (n->op) {
    case KO_CFIELD:
        memcpy(out, dbf_recbuf(t) + n->off, n->len);
        return DBF_OK;
    case KO_CCONST:
        memcpy(out, n->buf, n->len);
        return DBF_OK;
    case KO_CAT:
        rc = ceval(wk, n->l, t, out);
        if (rc == DBF_OK)
            rc = ceval(wk, n->r, t, out + n->l->len);
        return rc;
    case KO_UPPER:
        rc = ceval(wk, n->l, t, out);
        for (i = 0; i < n->len; i++) {
            c = out[i];
            out[i] = (u8)TXT_UPPER(c);
        }
        return rc;
    case KO_RIGHT:
    case KO_SUBSTR:
        rc = ceval(wk, n->l, t, n->buf);
        if (n->op == KO_RIGHT)
            memcpy(out, n->buf + (n->l->len - n->len), n->len);
        else
            memcpy(out, n->buf + (n->a - 1), n->len);
        return rc;
    case KO_STR:
        rc = neval(wk, n->l, t, &v);
        if (rc == DBF_OK)
            str_format(wk, &v, n->a, n->b, out);
        return rc;
    default:
        return DBF_EARG;
    }
}

/* ---------------------------------------------------------------------
 * Compilation.
 * --------------------------------------------------------------------- */

typedef struct {
    const char *p;          /* next character of the expression         */
    dbf        *t;
} kxparse;

/* The functions: name, node operation and argument type. */
typedef struct {
    char name[KX_FNAMELEN + 1];
    u8   op;
    char arg;
} kxfunc;

static const kxfunc kx_funcs[] = {
    { "STR",    KO_STR,    'N' },
    { "UPPER",  KO_UPPER,  'C' },
    { "RIGHT",  KO_RIGHT,  'C' },
    { "SUBSTR", KO_SUBSTR, 'C' },
    { "YEAR",   KO_YEAR,   'D' },
    { "MONTH",  KO_MONTH,  'D' },
    { "DAY",    KO_DAY,    'D' }
};

static void free_node(kxnode *n)
{
    if (n == NULL)
        return;
    free_node(n->l);
    free_node(n->r);
    free(n->buf);
    free(n);
}

static kxnode *new_node(u8 op, char type)
{
    kxnode *n;

    n = (kxnode *)malloc(sizeof(kxnode));
    if (n == NULL)
        return NULL;
    memset(n, 0, sizeof(kxnode));
    n->op = op;
    n->type = type;
    n->depth = 1;
    return n;
}

/* Attach children and check the tree height, which counts n's own
   depth as set (2 for YEAR, MONTH and DAY, whose D field argument is
   folded into them); frees everything on failure. */
static int link_node(kxnode *n, kxnode *l, kxnode *r, kxnode **out)
{
    u16 d;

    n->l = l;
    n->r = r;
    d = (u16)(n->depth - 1u);
    if (l != NULL && l->depth > d)
        d = l->depth;
    if (r != NULL && r->depth > d)
        d = r->depth;
    if (d + 1u > KX_MAXDEPTH) {
        free_node(n);
        return DBF_EARG;
    }
    n->depth = (u8)(d + 1u);
    *out = n;
    return DBF_OK;
}

static void skip_space(kxparse *ps)
{
    while (*ps->p == ' ')
        ps->p++;
}

/* 1 when c may start a name: a letter or '_'. */
static int is_name(char c)
{
    return TXT_ISALPHA(c) || c == '_';
}

/* Expect character c after optional spaces. */
static int expect(kxparse *ps, char c)
{
    skip_space(ps);
    if (*ps->p != c)
        return DBF_EARG;
    ps->p++;
    return DBF_OK;
}

/* An integer constant argument with an optional sign. */
static int parse_int(kxparse *ps, int *v)
{
    int neg;
    int n;

    skip_space(ps);
    neg = 0;
    if (*ps->p == '-' || *ps->p == '+') {
        neg = *ps->p == '-';
        ps->p++;
        skip_space(ps);
    }
    if (!TXT_ISDIGIT(*ps->p))
        return DBF_EARG;
    n = 0;
    while (TXT_ISDIGIT(*ps->p)) {
        if (n > KX_MAXARG / 10)
            return DBF_EARG;
        n = n * 10 + (*ps->p - '0');
        ps->p++;
    }
    if (n > KX_MAXARG || *ps->p == '.')
        return DBF_EARG;
    *v = neg ? -n : n;
    return DBF_OK;
}

/* A numeric constant: digits with an optional decimal part. */
static int parse_number(kxparse *ps, kxnode **out)
{
    kxbn n;
    kxnode *x;
    int digits;
    int dec;
    int point;
    int any;

    bn_zero(&n);
    any = 0;
    digits = 0;
    dec = 0;
    point = 0;
    while (TXT_ISDIGIT(*ps->p) || (*ps->p == '.' && !point)) {
        if (*ps->p == '.') {
            point = 1;
        } else {
            any = 1;
            if (point)
                dec++;
            if (digits > 0 || *ps->p != '0')
                digits++;
            if (digits > KX_MAXDIG || dec > KX_MAXDIG)
                return DBF_EARG;
            bn_mul_small(&n, 10u);
            bn_add_small(&n, (u16)(*ps->p - '0'));
        }
        ps->p++;
    }
    if (!any || is_name(*ps->p) || *ps->p == '.')
        return DBF_EARG;
    x = new_node(KO_NCONST, 'N');
    if (x == NULL)
        return DBF_ENOMEM;
    x->buf = (u8 *)malloc(sizeof(kxnum));
    if (x->buf == NULL) {
        free_node(x);
        return DBF_ENOMEM;
    }
    num_from_dec(&n, dec, 0, (kxnum *)x->buf);
    *out = x;
    return DBF_OK;
}

static int parse_string(kxparse *ps, kxnode **out)
{
    kxnode *x;
    const char *s;
    char close;
    u16 len;

    close = *ps->p == '[' ? ']' : *ps->p;
    s = ++ps->p;
    while (*ps->p != close) {
        if (*ps->p == '\0')
            return DBF_EARG;
        ps->p++;
    }
    if (ps->p - s > DBF_MAXCHAR)
        return DBF_ERANGE;
    len = (u16)(ps->p - s);
    ps->p++;
    x = new_node(KO_CCONST, 'C');
    if (x == NULL)
        return DBF_ENOMEM;
    x->len = len;
    x->buf = (u8 *)malloc(len > 0 ? len : 1u);
    if (x->buf == NULL) {
        free_node(x);
        return DBF_ENOMEM;
    }
    memcpy(x->buf, s, len);
    *out = x;
    return DBF_OK;
}

static int parse_expr(kxparse *ps, kxnode **out, int depth);

/* The function named by the n characters at name, or NULL. */
static const kxfunc *find_func(const char *name, int n)
{
    const kxfunc *f;
    int fl;

    for (f = kx_funcs; f < kx_funcs + sizeof kx_funcs / sizeof kx_funcs[0];
         f++) {
        fl = (int)strlen(f->name);
        if (n != fl && (n < 4 || n > fl))
            continue;
        if (txt_ieqn(name, f->name, (u16)n))
            return f;
    }
    return NULL;
}

/* The arguments and ")" of function f, after its "(". */
static int parse_call(kxparse *ps, const kxfunc *f, kxnode **out, int depth)
{
    kxnode *arg;
    kxnode *x;
    int rc;
    int w;              /* STR width or SUBSTR start                    */
    int d;              /* STR decimals                                 */
    int n;              /* length of the result                         */

    rc = parse_expr(ps, &arg, depth);
    if (rc != DBF_OK)
        return rc;
    if (arg->type != f->arg) {
        free_node(arg);
        return DBF_EARG;
    }
    w = 0;
    d = 0;
    n = 0;
    switch (f->op) {
    case KO_STR:
        rc = expect(ps, ',');
        if (rc == DBF_OK)
            rc = parse_int(ps, &w);
        skip_space(ps);
        if (rc == DBF_OK && *ps->p == ',') {
            ps->p++;
            rc = parse_int(ps, &d);
        }
        if (rc == DBF_OK && (w < 1 || w > KX_MAXW || d < 0 || d > KX_MAXD
                             || d >= w))
            rc = DBF_EARG;
        n = w;
        break;
    case KO_UPPER:
        n = (int)arg->len;
        break;
    case KO_RIGHT:
        rc = expect(ps, ',');
        if (rc == DBF_OK)
            rc = parse_int(ps, &n);
        if (rc == DBF_OK && (n < 1 || n > (int)arg->len))
            rc = DBF_EARG;
        break;
    case KO_SUBSTR:
        rc = expect(ps, ',');
        if (rc == DBF_OK)
            rc = parse_int(ps, &w);
        skip_space(ps);
        n = -1;
        if (rc == DBF_OK && *ps->p == ',') {
            ps->p++;
            rc = parse_int(ps, &n);
            if (rc == DBF_OK && n < 1)
                rc = DBF_EARG;
        } else if (w == 1) {
            n = (int)arg->len;
        }
        if (rc == DBF_OK && (w < 1 || n < 1 || w + n - 1 > (int)arg->len))
            rc = DBF_EARG;
        break;
    default:
        break;
    }
    if (rc == DBF_OK)
        rc = expect(ps, ')');
    if (rc != DBF_OK) {
        free_node(arg);
        return rc;
    }

    x = new_node(f->op, (char)(f->arg == 'D' ? 'N' : 'C'));
    if (x == NULL) {
        free_node(arg);
        return DBF_ENOMEM;
    }
    x->len = (u16)n;
    x->a = (u16)w;
    x->b = (u16)d;
    if (f->arg == 'D') {
        /* The D field is read through the function's own node. */
        x->off = arg->off;
        x->depth = 2;
        free_node(arg);
        arg = NULL;
    } else if (f->op == KO_RIGHT || f->op == KO_SUBSTR) {
        x->buf = (u8 *)malloc(arg->len);
        if (x->buf == NULL) {
            free_node(x);
            free_node(arg);
            return DBF_ENOMEM;
        }
    }
    return link_node(x, arg, NULL, out);
}

/* A field reference or a function call. */
static int parse_name(kxparse *ps, kxnode **out, int depth)
{
    char name[DBF_NAMELEN + 1];
    const char *s;
    const dbf_field *fd;
    const kxfunc *f;
    kxnode *x;
    int n;
    int i;

    s = ps->p;
    while (is_name(*ps->p) || TXT_ISDIGIT(*ps->p))
        ps->p++;
    n = (int)(ps->p - s);
    skip_space(ps);
    if (*ps->p == '(') {
        f = find_func(s, n);
        if (f == NULL)
            return DBF_EARG;
        ps->p++;
        return parse_call(ps, f, out, depth);
    }
    if (n > DBF_NAMELEN)
        return DBF_EARG;
    memcpy(name, s, (size_t)n);
    name[n] = '\0';
    i = dbf_field_index(ps->t, name);
    if (i < 0)
        return DBF_EARG;
    fd = dbf_field_at(ps->t, (u16)i);
    switch (fd->type) {
    case 'C':
        x = new_node(KO_CFIELD, 'C');
        break;
    case 'N':
        x = new_node(KO_NFIELD, 'N');
        break;
    case 'D':
        x = new_node(KO_DFIELD, 'D');
        break;
    default:
        return DBF_EARG;
    }
    if (x == NULL)
        return DBF_ENOMEM;
    x->off = fd->offset;
    x->len = fd->type == 'C' ? fd->len : 0;
    x->a = fd->len;
    *out = x;
    return DBF_OK;
}

/* Unary sign, parentheses, constants and names. */
static int parse_unary(kxparse *ps, kxnode **out, int depth)
{
    kxnode *x;
    kxnode *n;
    int rc;
    char c;

    if (depth > KX_MAXNEST)
        return DBF_EARG;
    skip_space(ps);
    c = *ps->p;
    if (c == '-' || c == '+') {
        ps->p++;
        rc = parse_unary(ps, &x, depth + 1);
        if (rc != DBF_OK)
            return rc;
        if (x->type != 'N') {
            free_node(x);
            return DBF_EARG;
        }
        if (c == '+') {
            *out = x;
            return DBF_OK;
        }
        n = new_node(KO_NEG, 'N');
        if (n == NULL) {
            free_node(x);
            return DBF_ENOMEM;
        }
        return link_node(n, x, NULL, out);
    }
    if (c == '(') {
        ps->p++;
        rc = parse_expr(ps, out, depth + 1);
        if (rc == DBF_OK) {
            rc = expect(ps, ')');
            if (rc != DBF_OK)
                free_node(*out);
        }
        return rc;
    }
    if (c == '"' || c == '\'' || c == '[')
        return parse_string(ps, out);
    if (TXT_ISDIGIT(c) || c == '.')
        return parse_number(ps, out);
    if (is_name(c))
        return parse_name(ps, out, depth + 1);
    return DBF_EARG;
}

/* Terms joined by "+" and "-", left to right. */
static int parse_expr(kxparse *ps, kxnode **out, int depth)
{
    kxnode *l;
    kxnode *r;
    kxnode *n;
    int rc;
    char c;

    rc = parse_unary(ps, &l, depth);
    if (rc != DBF_OK)
        return rc;
    for (;;) {
        skip_space(ps);
        c = *ps->p;
        if (c != '+' && c != '-')
            break;
        ps->p++;
        rc = parse_unary(ps, &r, depth);
        if (rc != DBF_OK) {
            free_node(l);
            return rc;
        }
        if (l->type != r->type || l->type == 'D'
            || (l->type == 'C' && c == '-')) {
            rc = DBF_EARG;
        } else if (l->type == 'C' && l->len + r->len > DBF_MAXCHAR) {
            rc = DBF_ERANGE;
        }
        n = NULL;
        if (rc == DBF_OK) {
            n = new_node((u8)(l->type == 'C' ? KO_CAT
                                             : (c == '+' ? KO_ADD : KO_SUB)),
                         l->type);
            if (n == NULL)
                rc = DBF_ENOMEM;
        }
        if (rc != DBF_OK) {
            free_node(l);
            free_node(r);
            return rc;
        }
        n->len = (u16)(l->len + r->len);
        rc = link_node(n, l, r, &l);
        if (rc != DBF_OK)
            return rc;
    }
    *out = l;
    return DBF_OK;
}

int kx_compile(kx **out, dbf *t, const char *expr)
{
    kxparse ps;
    kxnode *root;
    kx *k;
    int rc;

    *out = NULL;
    ps.p = expr;
    ps.t = t;
    rc = parse_expr(&ps, &root, 0);
    if (rc != DBF_OK)
        return rc;
    skip_space(&ps);
    if (*ps.p != '\0' || root->type != 'C' || root->len == 0)
        rc = DBF_EARG;
    else if (root->len > NDX_MAXKEY)
        rc = DBF_ERANGE;
    if (rc == DBF_OK) {
        k = (kx *)malloc(sizeof(kx));
        if (k == NULL)
            rc = DBF_ENOMEM;
    }
    if (rc != DBF_OK) {
        free_node(root);
        return rc;
    }
    k->root = root;
    k->reclen = dbf_reclen(t);
    *out = k;
    return DBF_OK;
}

u16 kx_len(kx *k)
{
    return k->root->len;
}

int kx_eval(kx *k, dbf *t, u8 *out)
{
    kxwork wk;

    if (dbf_reclen(t) != k->reclen)
        return DBF_EARG;
    return ceval(&wk, k->root, t, out);
}

void kx_free(kx *k)
{
    if (k == NULL)
        return;
    free_node(k->root);
    free(k);
}
