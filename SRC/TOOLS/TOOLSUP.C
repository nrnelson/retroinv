/*
 * TOOLSUP.C - support shared by the offline table tools (TOOLSUP.H).
 */
#include "DBF.H"
#include "TOOLSUP.H"

/* Indexed by -rc - 1: the DBF_E codes run from DBF_EIO (-1) down to
   DBF_ERDONLY without a gap. */
static const char *const tl_err[] = {
    "DBF_EIO (read, write or seek failed)",
    "DBF_ENOENT (file or path not found)",
    "DBF_EINUSE (access denied or file in use)",
    "DBF_EFORMAT (not a valid dBASE III file)",
    "DBF_ENOMEM (out of memory)",
    "DBF_ERANGE (out of range)",
    "DBF_EARG (invalid argument)",
    "DBF_EFULL (disk full)",
    "DBF_EHANDLES (no free file handles, raise FILES=)",
    "DBF_ERDONLY (file is read-only)"
};

/* One text per DBF_E code (the build fails otherwise). */
extern char tl_err_fits[1];
extern char tl_err_fits[(sizeof tl_err / sizeof tl_err[0] ==
                         (unsigned)-DBF_ERDONLY) ? 1 : 2];

const char *tl_err_name(int rc)
{
    if (rc > DBF_EIO || rc < DBF_ERDONLY)
        return "unknown error";
    return tl_err[-rc - 1];
}
