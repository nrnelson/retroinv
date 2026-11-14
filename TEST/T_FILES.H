/*
 * T_FILES.H - scratch files and the disk fault recorder of the unit
 * tests (T_FILES.C).
 *
 * A test that writes files takes its scratch directory as an argument
 * and names every file through tfs_path; the whole-file, byte and copy
 * helpers below take full paths, so they also reach files outside it
 * (golden copies).  The fault recorder is the one cf_fault hook of the
 * tests: it logs every disk-changing call and makes chosen ones fail.
 * T_FILES.C links only CDOS.C, so the DBF and COMPAT tests use it as
 * well as the engine tests; it reports through return codes and checks
 * nothing itself.
 */
#ifndef T_FILES_H
#define T_FILES_H

#include "COMPAT.H"

/* ---------------------------------------------------------------------
 * The scratch directory.
 * --------------------------------------------------------------------- */
#define TFS_MAXDIR      64      /* scratch prefix, separator included,
                                   without NUL (TENGH.C: ENG_MAXDIR)   */
#define TFS_WHYSZ       80      /* buffer of an open's failure text     */

/* Take arg as the scratch directory: the prefix is arg followed by '\'
   when arg holds a '\' or a ':', else by '/', and by nothing when arg is
   empty or already ends in '\', '/' or ':'.  A prefix longer than
   TFS_MAXDIR is refused: a line is printed and the program exits with
   status 2. */
void tfs_set_dir(const char *arg);

/* The prefix tfs_set_dir made ("" before it is called). */
const char *tfs_dir(void);

/* The prefix, stem and ext (either may be "") in one of two static
   buffers, used in turn: at most two results are live at once, so
   tfs_copy(tfs_path(a, x), tfs_path(b, y)) works.  A path longer than
   DOS_PATHMAX - 1 is a test's own error: a line is printed and the
   program exits with status 2. */
const char *tfs_path(const char *stem, const char *ext);

/* The scratch path of name in the directory dir of the scratch
   directory, joined by DOS_DIRSEP, in one of tfs_path's buffers; the
   same limit applies. */
const char *tfs_subpath(const char *dir, const char *name);

/* ---------------------------------------------------------------------
 * Whole files.  Each returns 0 or the cf_ code of the first call that
 * failed, and leaves no file open.  None arms or disarms the fault
 * recorder: a test calls them with it off unless it means to fault them.
 * --------------------------------------------------------------------- */
#define TFS_NOFILE      0xFFFFFFFFUL    /* tfs_size of a missing file   */

/* Copy src to dst, replacing dst.  When src is missing (CF_ENOENT or
   CF_ENOPATH) dst is removed and that code returned. */
int tfs_copy(const char *src, const char *dst);

/* 1 when files a and b both exist and hold the same bytes, else 0. */
int tfs_same(const char *a, const char *b);

/* 1 when path names a file (one another open holds included); 0 when
   it is missing or cf_exists reports an error. */
int tfs_exists(const char *path);

/* The size of path in bytes, or TFS_NOFILE when it cannot be opened. */
u32 tfs_size(const char *path);

/* Read exactly n bytes of path at offset at into buf; fewer bytes there
   is CF_EINVAL. */
int tfs_peek(const char *path, u32 at, void *buf, u16 n);

/* Overwrite n bytes (n >= 1) of the existing file path at offset at. */
int tfs_poke(const char *path, u32 at, const void *buf, u16 n);

/* Read path from its start into buf until its end or max bytes; *got
   is the count read (compare it with tfs_size when the file may be
   longer). */
int tfs_read_all(const char *path, void *buf, u16 max, u16 *got);

/* Create path (replacing it) holding the n bytes of buf (none for 0). */
int tfs_write_all(const char *path, const void *buf, u16 n);

/* Remove every file of the NULL-ended list names, each a name in the
   scratch directory; a missing file is no error. */
void tfs_wipe(const char *const *names);

/* Make each directory of the NULL-ended list names in the scratch
   directory (make 1), or remove each with the files in it (make 0); a
   directory there already, or missing, is no error.  Returns 0 or the
   first cf_ code of a make. */
int tfs_dirs(const char *const *names, int make);

/* Append to stem.DBF of the scratch directory one uncounted record of
   spaces, as long as its header says a record is, and the end byte
   0x1A after it, in one write over the old end byte: what an append
   whose header update never reached the disk leaves.  A record length
   above DBF_MAXRECLEN is CF_EINVAL. */
int tfs_add_tail(const char *stem);

/* ---------------------------------------------------------------------
 * The fault recorder.  While armed it is cf_fault: every disk-changing
 * call is counted and its CF_OP_ code logged, and the calls chosen fail
 * without effect, except that a chosen close still closes the handle
 * (COMPAT.H).  A cf_close is seen only when the mask holds CF_OP_CLOSE,
 * so that the counts of every other op stay as they were.
 * --------------------------------------------------------------------- */
#define TFS_MAXOPS      64      /* calls whose CF_OP_ codes are logged  */
#define TFS_OP(op)      (1 << (op))     /* the bit of op in a mask      */
#define TFS_ALLOPS      (~TFS_OP(CF_OP_CLOSE)) /* every op but closes   */

/* Arm the recorder, clearing its counts and log.  Calls whose op is in
   the mask ops are numbered from 1; numbers at to at + count - 1 fail
   (count 0: at and every later one) with code, CF_EFULL when code is 0.
   at 0 fails nothing and only records. */
void tfs_fault(int ops, u32 at, u32 count, int code);

/* Disarm the recorder (cf_fault = NULL); its counts and log stay. */
void tfs_fault_off(void);

/* Disk-changing calls since tfs_fault, of every op. */
u32 tfs_calls(void);

/* Calls of op since tfs_fault. */
u16 tfs_count(int op);

/* The CF_OP_ code of the first call failed since tfs_fault, 0 when none
   has failed. */
int tfs_failop(void);

/* The CF_OP_ codes of the first TFS_MAXOPS calls since tfs_fault, in
   order. */
extern u8 tfs_ops[TFS_MAXOPS];

#endif
