/*
 * SETUPL.H - the portable part of SETUP, the installer of RETROINV's
 * Windows client (SETUPL.C): the release's list of files, the copy of a
 * file, the directory typed, and the command line and commands that make
 * Program Manager's group.
 *
 * The list.  Each release disk carries INSTALL's list INS_LIST, in the
 * format of SRC/TOOLS/INSTPLAN.H: three lines, then one per file of the
 * set, "ROLES DISKS SIZE NAME".  SETUP reads ROLES and NAME alone: the
 * role INS_R_WIN marks a file of the Windows client, those of INS_R_DOS
 * one of a DOS installation, and INS_R_LZ one the disks hold compressed.
 *
 * Program Manager's commands.  A command's arguments are separated by
 * commas and closed by a parenthesis, so the directory and the switches
 * of the item may hold neither, nor a bracket or a double quote.
 *
 * Its sizes and INSTPLAN.H's take DOS_FINDNAME and DOS_LABELLEN, so a
 * file that includes it defines COMPAT_FIND and COMPAT_LABEL before
 * COMPAT.H.
 */
#ifndef SETUPL_H
#define SETUPL_H

#include "COMPAT.H"
#include "INSTPLAN.H"

#define SU_PROG         "INVWIN.EXE"
#define SU_TEMP         "SETUP.$$$"     /* a copy until it is whole     */
/* The name of the group and of its item, INVWIN's own title. */
#define SU_NAME         "RETROINV"
/* Characters of a directory: room for '\', a file's name and a NUL. */
#define SU_DIRMAX       (DOS_PATHMAX - DOS_FINDNAME - 1)
#define SU_CMDMAX       DOS_PATHMAX     /* bytes of the item's command  */
/* Bytes of the commands that make the group and its item (su_dde_add),
   the NUL counted in SU_CMDMAX. */
#define SU_DDEMAX       (SU_CMDMAX + sizeof "[CreateGroup()][AddItem(,)]" - \
                         1 + 2 * (sizeof SU_NAME - 1))

/* A reader of the list, over the text the caller keeps. */
typedef struct {
    const char *p;
    const char *end;
} su_list;

/* Start reading the n bytes of a list's text at buf, past its first
   three lines: 1, or 0 when it has fewer or n is INS_LISTMAX or more, a
   read of INS_LISTMAX bytes having cut a longer list. */
int su_list_open(su_list *l, const char *buf, u16 n);

/* The next file of the list: its name into name (DOS_FINDNAME bytes)
   and its roles, the INS_R_ bits of its ROLES, into *roles.  1 when one
   is read, 0 at the end of the list, -1 for a line that is not "ROLES
   DISKS SIZE NAME" or whose NAME holds a character of INS_SEPS, which
   would reach outside the directory. */
int su_list_next(su_list *l, char *name, u8 *roles);

/* Take the blanks off both ends of the directory dir, upper-case it in
   place and check it: "X:\NAME" with further "\NAME"s, each an 8.3 name
   of letters, digits and the punctuation DOS and Program Manager both
   take, at most SU_DIRMAX characters.  1 when it is such a directory,
   else 0. */
int su_dir_ok(char *dir);

/* The item's command line into cmd (SU_CMDMAX bytes): dir, a '\' and
   SU_PROG, then a blank and opts, its blanks at both ends taken off,
   when it holds more.  1, or 0 when opts holds a character that
   Program Manager's commands cannot carry or the line is too long. */
int su_cmd(char *cmd, const char *dir, const char *opts);

/* The commands that make the group SU_NAME with the item SU_NAME of
   command line cmd (su_cmd), into out (SU_DDEMAX bytes). */
void su_dde_add(char *out, const char *cmd);

/* The command that deletes the group SU_NAME. */
extern const char su_k_delete[];

/* The path of the file name in dir into out (DOS_PATHMAX bytes); dir
   may end in its separator. */
void su_join(char *out, const char *dir, const char *name);

/* Copy the file name from the directory src into dir through the
   temporary file SU_TEMP there, which replaces an earlier copy only once
   it is whole; when lz is not 0 and src holds the file under its
   compressed name (lz_name), as a release disk does, that is expanded,
   else the file is copied as it is.  0, LZ_EFORMAT for a compressed file
   that breaks its format, or the CF_E code of the step that failed, the
   temporary file then removed and an earlier copy kept. */
int su_copy(const char *src, const char *dir, const char *name, int lz);

/* 1 when the directory src holds a file of the list's W lines that it
   marks compressed under its compressed name, as a copy of a release
   disk does: SETUP started there cannot install into it. */
int su_packed(const su_list *files, const char *src);

#endif
