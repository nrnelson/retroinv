/*
 * PRINT.H - report output to the printer or to a text file (PRINT.C).
 *
 * A pr is one output job.  Text, line ends and page ends pass through a
 * buffer of PR_BUFLEN bytes to the device: the printer through the prn_
 * functions of COMPAT.H (a BIOS parallel port or the DOS device PRN),
 * or a text file through cf_.  Type PR_EPSON adds Epson ESC/P control
 * codes for the styles and the form length; PR_PLAIN, and every file,
 * gets only the text, CR LF line ends and FF page ends, so a file holds
 * exactly what a plain printer receives.
 *
 * The ESC/P codes sent, all of the Epson FX-80 set and kept by the LX
 * and later models (an IBM Proprinter lacks ESC @: use PR_PLAIN):
 *   ESC @     1B 40      initialise; first in every printer job
 *   ESC C n   1B 43 n    form length of n lines (pr_form)
 *   ESC E     1B 45      emphasised (bold) on
 *   ESC F     1B 46      emphasised off
 *   ESC W 1   1B 57 01   double width on
 *   ESC W 0   1B 57 00   double width off
 * and CR LF (0D 0A) after each line, FF (0C) at each page end.  Text
 * bytes 20h..7Eh and 80h..FFh pass unchanged (code page 437 on the
 * printer's IBM character table); bytes below 20h and 7Fh are sent as
 * '?'.
 *
 * When the printer does not take a byte (not ready, out of paper,
 * timeout), the job calls its wait function with the PRN_E status; the
 * function may ask the user, and returns 1 to send the same bytes
 * again or 0 to cancel.  A cancelled job, or one whose file could not
 * be written, drops all further output; pr_failed tells, and pr_close
 * returns the reason.
 */
#ifndef PRINT_H
#define PRINT_H

#include "COMPAT.H"

/* Destinations and printer types. */
#define PR_PRINTER      1
#define PR_FILE         2
#define PR_NEWFILE      3       /* a file that must not exist yet       */
#define PR_EPSON        1
#define PR_PLAIN        2

/* Styles (pr_style). */
#define PR_BOLD         0x01
#define PR_WIDE         0x02    /* double width: each character takes
                                   two columns on the paper            */

/* Results. */
#define PR_OK           0
#define PR_ECANCEL      1       /* cancelled after a printer error      */
#define PR_EFILE        2       /* the file could not be created or
                                   written (*cferr holds the CF_E code) */
#define PR_ENOMEM       3       /* no memory; the last code: RPT.H's
                                   RPT_EKIND follows it                 */

#define PR_BUFLEN       128     /* bytes buffered before the device     */

typedef struct pr pr;           /* an output job (opaque)               */

/* The printer did not take a byte: status is a PRN_E code; 1 retries,
   0 cancels the job. */
typedef int (*pr_wait_fn)(void *arg, int status);

/* Start a job: dest PR_PRINTER on port (PRN_LPT1..PRN_DOS) of type
   (PR_EPSON or PR_PLAIN), the printer's state checked first (through
   wait, as for a byte), and for PR_EPSON ESC @ sent; or dest PR_FILE
   creating (truncating) path, or PR_NEWFILE creating it only when it
   is not there yet (cf_create_new: CF_EEXIST otherwise), type and port
   ignored.  Returns PR_OK, or the PR_ code with *out NULL and *cferr
   the CF_E code of a file error (0 for another). */
int pr_open(pr **out, u8 dest, u8 type, u8 port, const char *path,
            pr_wait_fn wait, void *arg, int *cferr);

/* Form length: lines per page (PR_EPSON: ESC C lines; otherwise only
   the page ends mark pages). */
void pr_form(pr *p, u8 lines);

/* The styles in effect from here on (0 for none); PR_EPSON sends the
   codes of those that change. */
void pr_style(pr *p, u8 style);

/* n bytes of text; pr_eol ends the line, pr_ff the page. */
void pr_text(pr *p, const char *s, u16 n);
void pr_eol(pr *p);
void pr_ff(pr *p);

/* PR_OK while output still goes out, else the PR_ code that stopped it. */
int pr_failed(pr *p);

/* The lines given so far, as a reader of the file finds them: a line
   starts at the first byte of the job and after each line end or page
   end, and each page end is a line of its own.  Exact for a file job,
   PR_FILE or PR_NEWFILE (a printer job also sends ESC/P codes). */
u32 pr_lines(pr *p);

/* Send what is buffered, end the job (closing the file or the printer
   port) and free p; returns PR_OK or the PR_ code that stopped it, with
   *cferr the CF_E code of a file error.  p may be NULL. */
int pr_close(pr *p, int *cferr);

#endif
