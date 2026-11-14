/*
 * TEWIPE.C - removing the engine's files from a data directory, for the
 * unit tests (TEWIPE.H).
 *
 * The extensions are kept as character arrays, not pointer tables, to
 * hold the module's near data to the texts themselves.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGADMIN.H"
#include "ENGCSV.H"
#include "TEWIPE.H"

#define WP_EXTSZ        5       /* ".XYZ" and NUL                       */

/* The extensions of a table's files and of an index's: the live ones,
   then the scratch names of a pack ($ new, % old) and of a create (&);
   a reindex adds # for its scratch. */
static const char k_tblext[][WP_EXTSZ] = {
    ".DBF", ".DBT", ".$DB", ".%DB", ".$DT", ".%DT", ".&DB", ".&DT"
};
static const char k_ndxext[][WP_EXTSZ] = {
    ".NDX", ".$NX", ".%NX", ".#NX", ".&NX"
};

/* The CSV import's scratch index of the codes it has met. */
static const char k_import[] = "IMPORT.&NX";

/* Remove dir + name + ext, built in path (DOS_PATHMAX bytes), unless
   it is too long. */
static void remove_one(char *path, const char *dir, const char *name,
                       const char *ext)
{
    size_t n;

    n = strlen(dir);
    if (n + strlen(name) + strlen(ext) >= DOS_PATHMAX)
        return;
    strcpy(path, dir);
    strcpy(path + n, name);
    strcat(path, ext);
    cf_remove(path);
}

void te_remove(const char *dir, const char *const *extra)
{
    char path[DOS_PATHMAX];
    char scr[DOS_PATHMAX];
    char ext[sizeof ENG_CSVSCRATCH + 1];
    size_t i;
    int t;

    for (t = 0; t < ENG_NTABLES; t++)
        for (i = 0; i < sizeof k_tblext / sizeof k_tblext[0]; i++)
            remove_one(path, dir, eng_table_def(t)->name, k_tblext[i]);
    for (t = 0; t < ENG_NINDEX; t++)
        for (i = 0; i < sizeof k_ndxext / sizeof k_ndxext[0]; i++)
            remove_one(path, dir, eng_index_def(t)->name, k_ndxext[i]);
    remove_one(path, dir, ENG_EDITMARK, "");
    remove_one(path, dir, ENG_COPYMARK, "");
    remove_one(path, dir, k_import, "");
    strcpy(ext, ENG_CSVSCRATCH);
    ext[sizeof ext - 1] = '\0';
    for (; extra != NULL && *extra != NULL; extra++) {
        path[0] = '\0';
        remove_one(path, dir, *extra, "");
        for (t = 0; path[0] != '\0' && t < ENG_EXPTAGS; t++) {
            ext[sizeof ext - 2] = txt_hexdig[t];
            if (txt_path_ext(scr, (u16)sizeof scr, path, ext, 0) == 0)
                cf_remove(scr);
        }
    }
}
