/*
 * TOOLSUP.H - support shared by the offline table tools.
 *
 * The exit statuses DBPACK, INVDUMP and the sample writer MKITEMS
 * document, and the text that names a DBF_E code in their messages.
 * Only the tools link TOOLSUP.C, so its texts stay out of INV.
 */
#ifndef TOOLSUP_H
#define TOOLSUP_H

#define EXIT_OK         0       /* the run did what was asked           */
#define EXIT_USAGE      1       /* bad command line or name             */
#define EXIT_FAIL       2       /* an operation failed, or no memory    */

/* The DBF_E code rc by name with a short explanation, such as
   "DBF_ENOENT (file or path not found)"; "unknown error" for any other
   value. */
const char *tl_err_name(int rc);

#endif
