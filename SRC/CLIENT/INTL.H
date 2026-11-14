/*
 * INTL.H - dates and numbers in the forms of the Windows Control
 * Panel's International settings, for INVWIN to show (INTL.C).
 *
 * The settings are those of WIN.INI's [intl] section that Windows 3.0
 * keeps: sShortDate, the short date picture, which holds the date's
 * separators too (3.0 has no sDate; iDate is 2.x's and unused), and
 * sDecimal, sThousand and iLZero for numbers.  The caller reads them
 * (INVWIN once at start) and gives them to intl_set, their characters in
 * CP437 as the rows and the data are.  Only the display uses them: a
 * value typed, and an edit control being typed into, stay ISO dates and
 * plain digits, as the form model and the wire expect.
 *
 * Short date pictures.  A picture is supported when it holds the day,
 * the month and the year once each, each one of
 *   d    the day, 1-31          dd    the day, 01-31
 *   M    the month, 1-12        MM    the month, 01-12
 *   yy   the year, 00-99        yyyy  the year, 0000-9999
 * and between them any characters but letters, copied as they are, and
 * when its longest date, two digits for d and M, is at most MSG_DATEW
 * characters: every picture Windows 3.0's Control Panel writes (order
 * MDY, DMY or YMD, a separator of one character, leading zeros or not,
 * the century or not).  Any other picture (the names MMM, MMMM, ddd and
 * dddd, a y or yyy, a field twice or missing, another letter, too long)
 * is not supported, and dates are then shown as ISO, YYYY-MM-DD.  So a
 * date never takes more than MSG_DATEW characters, the width of INV's.
 *
 * A zeroed intl_fmt shows dates as ISO and numbers as the wire gives
 * them, their leading blanks dropped.
 */
#ifndef INTL_H
#define INTL_H

#include "COMPAT.H"
#include "MSG.H"

typedef struct {
    char date[MSG_DATEW + 1];   /* supported short date picture; "" ISO */
    char dec;                   /* decimal separator; '\0' as '.'       */
    char thou;                  /* thousands separator; '\0' none       */
    u8   nolz;                  /* 1: no 0 before the decimal separator
                                   of a number below 1 (iLZero=0)       */
} intl_fmt;

/* The cells a number of w characters (sign, digits and, with dec
   decimals, the point and those digits) takes with a thousands
   separator in every group of three integer digits but the first. */
#define INTL_NUMW(w, dec) ((w) + ((w) - ((dec) ? (dec) + 1 : 0) - 1) / 3)

/* Make f from [intl]'s values: pic sShortDate's (kept when supported,
   else ""), dec and thou the first characters of sDecimal and sThousand
   (an empty sDecimal as '.', an empty sThousand none), lzero iLZero's
   (0 sets nolz). */
void intl_set(intl_fmt *f, const char *pic, const char *dec,
              const char *thou, int lzero);

/* A wire date (MSG_DATELEN digits, YYYYMMDD) in f's short date into out
   (MSG_DATEW + 1 bytes); 1, or 0 with out "" when wire is not such a
   date, as msg_date of MSG.H. */
int intl_date(const intl_fmt *f, char *out, const char *wire);

/* The number v as the wire gives it (leading blanks, an optional '-',
   digits, and an optional '.' with digits) with f's separators into out
   (n + 1 bytes): its integer digits in groups of three from the right
   joined by thou, dec in place of the point, and an integer part of
   "0" before it dropped when nolz is set and kept, or "0" added before
   the separator when the wire has none, when it is not.  v of any other
   form, and an empty one, is copied as it is, leading blanks dropped;
   either is cut to n characters (INTL_NUMW gives the n a field of the
   form model needs). */
void intl_num(const intl_fmt *f, char *out, const char *v, u16 n);

#endif
