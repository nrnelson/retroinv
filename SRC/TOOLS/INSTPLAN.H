/*
 * INSTPLAN.H - what INSTALL copies, keeps and removes.
 *
 * A set of release disks carries on each disk the list INS_LIST, which
 * REL/mkdist.sh writes from REL/DISKS.TXT: lines ending in LF (a CR
 * before it ignored), words between single spaces.
 *
 *   TITLE                    the release's name, at most INS_TITLEMAX
 *                            characters
 *   KB LABEL...              the set: the disks' size in KB and the
 *                            volume label of each disk, in order, at
 *                            most INS_MAXDISKS
 *   N                        the disk this copy is on, 1 for the first
 *   ROLES DISKS SIZE NAME    one per file, at most INS_MAXFILES: the
 *                            letters of INS_ROLES whose roles need it,
 *                            with Z when the disks hold it compressed,
 *                            the digits of the disks that hold it, its
 *                            bytes once installed, and its 8.3 name;
 *                            INS_DATA, of role D, is the sample data
 *                            directory, SIZE "BYTES,FILES", the bytes
 *                            of its files and how many there are
 *
 * The first two lines name the release and the set, so the list of a
 * disk inserted tells whether it belongs to the same set of the same
 * release, as labels repeat across sets.
 * A file marked Z is on the disks in Microsoft's COMPRESS format
 * (LZEXP.H) under the name lz_name gives it, INV.EX_ for INV.EXE, and
 * the sample data's files in the directory DATA._, each under its own
 * name, as a table and its memo file would share one; the installer
 * expands them.  Where the compressed name is missing, as in an
 * installation or the zip's folder, which hold every file as it is, the
 * file is copied as it is from its own name, so one list serves them
 * all.
 * The installer copies the files of the roles chosen, the disk in the
 * drive first and then each further disk asked for by its label, and
 * puts the list beside them for the removal, which takes away every
 * file the list names and asks before the data directory.
 */
#ifndef INSTPLAN_H
#define INSTPLAN_H

#include "LZEXP.H"

#define INS_LIST        "INSTALL.LST"
#define INS_SELF        "INSTALL.EXE"
#define INS_DATA        "DATA"
#define INS_MAXDISKS    4       /* disks of one set                     */
#define INS_MAXFILES    24      /* files of one set's list              */
#define INS_LISTMAX     2048    /* bytes of the largest list            */
#define INS_TITLEMAX    40      /* characters of its title              */
#define INS_SETMAX      96      /* bytes of its first two lines         */
#define INS_SEPS        "\\/:"  /* what a list's file name may not hold */

/* The role letters of a list, in the order of their bits, and Z. */
#define INS_ROLES       "SVNTWDKZ"
#define INS_R_ALONE     0x01    /* S: one PC, INV and its data          */
#define INS_R_SERVER    0x02    /* V: INVSERV, the bridge, tools, INV   */
#define INS_R_CLIENT    0x04    /* N: a network client, INV             */
#define INS_R_THIN      0x08    /* T: the network-only client           */
#define INS_R_WIN       0x10    /* W: the Windows client                */
#define INS_R_DATA      0x20    /* D: the sample data                   */
#define INS_R_KEEP      0x40    /* K: settings, kept unless replaced    */
#define INS_R_LZ        0x80    /* Z: no role, compressed on the disks  */
#define INS_R_DOS       0x0F    /* the roles of one DOS PC              */

/* Codes of the calls below beside the CF_E codes, which are positive. */
#define INS_OK          0
/* A list, or a compressed file, that breaks its format. */
#define INS_EFORMAT     LZ_EFORMAT
#define INS_ECANCEL     (-2)    /* the user left at a question          */
#define INS_EROOM       (-3)    /* not enough room on the target        */
#define INS_EPACKED     (-4)    /* the target holds compressed files    */

/* The questions and notices of ins_ui_fn, with the text each names. */
#define INS_Q_DISK      1       /* insert the disk of this label        */
#define INS_Q_REPLACE   2       /* replace this settings file?          */
#define INS_Q_DATA      3       /* delete the data directory too?       */
#define INS_N_COPY      4       /* copying this file                    */
#define INS_N_WRONG     5       /* not that disk; the label found       */
#define INS_N_LEFT      6       /* this directory keeps other files     */

/* The program's side of a question: 1 yes (for INS_Q_DISK, the disk is
   in), 0 no, or -1 when the user leaves; a notice's answer is unused. */
typedef int (*ins_ui_fn)(int q, const char *s);

typedef struct {
    char name[DOS_FINDNAME];
    u32 size;
    u16 files;                  /* files of the data directory, else 0  */
    u8 roles;                   /* INS_R_ bits but INS_R_LZ             */
    u8 lz;                      /* 1 when the disks hold it compressed  */
    u8 disks;                   /* bit i: on disk i + 1                 */
    u8 todo;                    /* 1 while it is still to be copied     */
} ins_file;

typedef struct {
    char title[INS_TITLEMAX + 1];
    char set[INS_SETMAX + 1];   /* the first two lines, as in the file  */
    char label[INS_MAXDISKS][DOS_LABELLEN + 1];
    ins_file f[INS_MAXFILES];
    u8 ndisks;
    u8 nfiles;
    u8 here;                    /* this copy's disk, from 0             */
    u8 roles;                   /* the roles of ins_plan                */
} ins_list;

/* Read the list at path into l: INS_OK, a CF_E code, or INS_EFORMAT. */
int ins_load(ins_list *l, const char *path);

/* The roles of INS_R_DOS and INS_R_WIN this set can install: those of
   a program on it other than INS_SELF. */
u8 ins_roles(const ins_list *l);

/* The base name, without ".EXE", of the program of role r alone (the
   network-only client for INS_R_THIN) into name (DOS_FINDNAME bytes),
   "" when the set has none. */
void ins_prog(const ins_list *l, u8 r, char *name);

/* Upper-case dir in place and check that it is a whole path below a
   drive's root, "X:\NAME...", with no final '\', none doubled, no
   wildcard or blank, and room under it for the data directory's files:
   1 when it is, else 0. */
int ins_dir_ok(char *dir);

/* 1 when the directory dir exists, or cannot be told not to. */
int ins_has_dir(const char *dir);

/* 1 when the data directory of dst holds a file, or cannot be told not
   to: data the installer keeps.  An empty or missing one is not, so
   that the sample data is offered for it. */
int ins_has_data(const char *dst);

/* Mark the files of roles to be copied (INS_R_DATA for the sample
   data) and keep roles for ins_copy. */
void ins_plan(ins_list *l, u8 roles);

/* Ask through ui whether each settings file marked that dst holds
   already is to be replaced, and leave the ones it is not to: INS_OK or
   INS_ECANCEL. */
int ins_keep(ins_list *l, const char *dst, ins_ui_fn ui);

/* The bytes the marked files will take on dst's drive, into *need, and
   those free there, into *have: each file rounded up to the drive's
   cluster, the data directory with a cluster of its own and up to one
   more per file, less the clusters of the files dst holds that the
   copy replaces.  INS_OK, INS_EROOM when they do not fit, or the CF_E
   code of the free space call. */
int ins_room(const ins_list *l, const char *dst, u32 *need, u32 *have);

/* Copy the files ins_plan chose from src (a directory ending in its
   separator, or "" for the current one) of drive (0 the default, 1 A:,
   ...) into dst, which is made when missing, expanding those found
   compressed: the files on the disk in the drive first, then those of
   each further disk in the set's order, which ui is asked for, its label
   and its list's release and set checked; on each disk the sample data
   last, into the data directory, made when it starts.  The roles that
   keep data get the data directory, empty, when they have no sample.
   Nothing is copied when src is dst; when dst then holds one of the
   files compressed, as a copy of a disk does, the code is INS_EPACKED,
   as it needs another target.  INS_OK, INS_ECANCEL, or the code of the
   step that failed, INS_EFORMAT for a compressed file that breaks its
   format or a CF_E code, whose path goes into failed (DOS_PATHMAX
   bytes): the source's when it could not be opened or is damaged, else
   the target's (dst for INS_EPACKED).  A file whose copy failed is
   removed, the files copied before it stay, but for the sample data,
   which is whole or not there: when its copy fails, what it copied and
   the data directory go, and when the user leaves before it, there is
   no data directory yet, so that a new run offers it again. */
int ins_copy(ins_list *l, const char *src, u8 drive, const char *dst,
             ins_ui_fn ui, char *failed);

/* Remove from dst every file the list names, the list last, after
   asking through ui whether the data directory goes too, and then dst
   when nothing is left in it, or say through ui that it stays.  INS_OK,
   INS_ECANCEL with nothing removed, or the CF_E code of the removal
   that failed, whose path goes into failed, the files after it kept. */
int ins_remove(const ins_list *l, const char *dst, ins_ui_fn ui,
               char *failed);

#endif
