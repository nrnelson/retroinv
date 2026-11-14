/*
 * COMPAT.H - the only interface between portable code and the compiler,
 * the DOS machine and the host test build.
 *
 * Portable modules include this header and use only what it declares; the
 * implementations live in SRC/COMPAT/ and are the only place where
 * compiler-specific keywords, interrupts and port I/O appear.  Supported
 * builds: Open Watcom and Watcom C 6.5 (both __WATCOMC__) and Turbo C
 * 2.01 (__TURBOC__) for 16-bit real-mode DOS, Open Watcom and Watcom C
 * 9.5 for 16-bit Windows (__WINDOWS__), and gcc on Linux (HOST_LINUX)
 * for host tests.
 */
#ifndef COMPAT_H
#define COMPAT_H

/* ---------------------------------------------------------------------
 * Build selection.
 *
 * Exactly one of these is defined, for the SRC/COMPAT/ implementations:
 *   COMPAT_HOST  gcc host test build (HOST_LINUX)
 *   COMPAT_TC    Turbo C 2.01 (__TURBOC__)
 *   COMPAT_OW    Open Watcom, or Watcom C 11.0 and later; __WATCOMC__ is
 *                1100 or above (Watcom C 11.0 1100, Open Watcom 1.x 1200
 *                and above, Open Watcom 2.0 1300); and Watcom C 9.5
 *                building for Windows (below)
 *   COMPAT_WC6   Watcom C 6.5 (1988), which predefines __WATCOMC__ with
 *                an empty value; it has no <i86.h>, and its far/near
 *                keywords have no leading underscores
 *
 * Watcom C 7.0 to 10.6 predefine values between these two and are
 * refused, as is any other compiler, but for Watcom C 9.5 building for
 * Windows.
 *
 * Beside COMPAT_OW, COMPAT_WIN16 marks a 16-bit Windows program (Open
 * Watcom's -bt=windows: __WINDOWS__ and __I86__).  It implies COMPAT_OW:
 * the compiler, its keywords, pragmas and C library are those of the DOS
 * build, so a COMPAT_OW branch that chooses compiler code serves Windows
 * too, and COMPAT_WIN16 marks only where the machine differs: global
 * memory for far blocks, GetTickCount for the timer, and Windows calls
 * where a DOS program patches its PSP or hooks interrupts.  Windows runs
 * the program in protected mode, where a segment is a selector, so those
 * use no BIOS data area, interrupt vector or segment arithmetic.
 *
 * Watcom C 9.5 (1993; __WATCOMC__ 950) builds 16-bit Windows programs
 * only (/bt=windows: __WINDOWS__, and M_I86, as it has no __I86__), a
 * period-compiler check of the Open Watcom build.  It defines
 * COMPAT_WC95 and with it COMPAT_OW and COMPAT_WIN16: Open Watcom
 * descends from it and kept its keywords, pragmas and C library, so
 * COMPAT_WC95 marks only what 9.5 lacks: winsock.h, which postdates it
 * (CWSOCK.C).
 *
 * The Watcom C 6.5 preprocessor mishandles #elif: after a taken group it
 * skips the next #elif or #else group but enables the one after that.
 * This selection and the compiler branches in SRC/COMPAT/ are therefore
 * written with nested #if/#else and separate #ifdef blocks only.
 * --------------------------------------------------------------------- */
#ifdef HOST_LINUX
#define COMPAT_HOST     1
#else
#ifdef __TURBOC__
#define COMPAT_TC       1
#else
#ifdef __WATCOMC__
#if (__WATCOMC__ + 0) == 0
#define COMPAT_WC6      1
#else
#if (__WATCOMC__ + 0) >= 1100
#define COMPAT_OW       1
#ifdef __WINDOWS__
#ifdef __I86__
#define COMPAT_WIN16    1
#else
#error COMPAT.H supports 16-bit Windows only
#endif
#endif
#else
#if (__WATCOMC__ + 0) == 950
#ifdef __WINDOWS__
#ifdef M_I86
#define COMPAT_WC95     1
#define COMPAT_OW       1
#define COMPAT_WIN16    1
#endif
#endif
#endif
#ifndef COMPAT_WC95
#error COMPAT.H supports Watcom C 6.5, 9.5 for Windows and 11.0 or later
#endif
#endif
#endif
#else
#error COMPAT.H: unsupported compiler
#endif
#endif
#endif

/* ---------------------------------------------------------------------
 * Fixed-width integer types.
 *
 * On the 16-bit DOS compilers int and short are 16 bits and long is 32.
 * On the 64-bit Linux host long is 64 bits, so int carries 32 there.
 * --------------------------------------------------------------------- */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef short          i16;
#ifdef HOST_LINUX
typedef unsigned int   u32;
typedef int            i32;
#else
typedef unsigned long  u32;
typedef long           i32;
#endif

/* Compile-time size checks.  Each pair declares one array twice, the
   second time with size 2 when the width is wrong: the declarations then
   disagree, which every compiler refuses (a negative array size is
   accepted by some period compilers).  Repeating a matching pair in
   every file is allowed, and an array never referenced costs no data. */
extern char compat_u16_is_2[1];
extern char compat_u16_is_2[(sizeof(u16) == 2) ? 1 : 2];
extern char compat_u32_is_4[1];
extern char compat_u32_is_4[(sizeof(u32) == 4) ? 1 : 2];

/* The separator the file system puts between directory names: '\' under
   DOS, '/' on the Linux host, where a '\' would be part of a file
   name. */
#ifdef COMPAT_HOST
#define DOS_DIRSEP      '/'
#else
#define DOS_DIRSEP      '\\'
#endif

/* ---------------------------------------------------------------------
 * Little-endian serialization.
 *
 * p must point to u8 (unsigned char) bytes.  v may be of any integer type;
 * put_le16 stores (u16)v and put_le32 stores (u32)v.  These are macros, so
 * p and v are evaluated more than once: never pass expressions with side
 * effects such as p++ or a function call.  put_le16/put_le32 are
 * expressions of type void and are used as statements.
 * --------------------------------------------------------------------- */
#define get_le16(p) \
    ((u16)((u16)(p)[0] | (u16)((u16)(p)[1] << 8)))
#define get_le32(p) \
    ((u32)((u32)(p)[0] | ((u32)(p)[1] << 8) | \
           ((u32)(p)[2] << 16) | ((u32)(p)[3] << 24)))
#define put_le16(p, v) \
    ((void)((p)[0] = (u8)((u16)(v) & 0xFFu), \
            (p)[1] = (u8)((u16)(v) >> 8)))
#define put_le32(p, v) \
    ((void)((p)[0] = (u8)((u32)(v) & 0xFFu), \
            (p)[1] = (u8)(((u32)(v) >> 8) & 0xFFu), \
            (p)[2] = (u8)(((u32)(v) >> 16) & 0xFFu), \
            (p)[3] = (u8)((u32)(v) >> 24)))

/* ---------------------------------------------------------------------
 * Text helpers, CTEXT.C.
 *
 * Portable C89 helpers for the text jobs every layer has: decimal and
 * hex digits, case-insensitive compares, bounded copies, wiping
 * secrets, the YYYYMMDD calendar check, extension replacement and the
 * directory part of a path.
 * Letters are ASCII.  Portable code tests and folds characters with
 * the macros below, never with <ctype.h>: its tests, and Turbo C 2.01's
 * toupper, link a 257-byte character table into DGROUP.
 * --------------------------------------------------------------------- */
#define TXT_U32LEN      10      /* digits of the largest u32            */

/* 1 when c is a decimal digit (TXT_ISDIGIT) or an ASCII letter
   (TXT_ISALPHA), tested on (unsigned char)(c), else 0.  TXT_UPPER is
   (unsigned char)(c) with 'a'..'z' turned into 'A'..'Z', an int in
   0..255.  The argument is evaluated more than once: pass a plain
   variable. */
#define TXT_ISDIGIT(c) \
    ((unsigned char)(c) >= '0' && (unsigned char)(c) <= '9')
#define TXT_ISALPHA(c) \
    (((unsigned char)(c) >= 'A' && (unsigned char)(c) <= 'Z') || \
     ((unsigned char)(c) >= 'a' && (unsigned char)(c) <= 'z'))
#define TXT_UPPER(c) \
    ((unsigned char)(c) >= 'a' && (unsigned char)(c) <= 'z' ? \
     (unsigned char)(c) - 'a' + 'A' : (int)(unsigned char)(c))

/* Write the decimal digits of v and a NUL into out, which holds at
   least TXT_U32LEN + 1 bytes; returns the number of digits. */
u16 txt_u32(char *out, u32 v);

/* Write v as exactly w characters, right-aligned and filled on the left
   with pad (' ' or '0'), with no NUL; the caller makes sure v fits in w
   digits.  v 0 is written as one '0' digit. */
void txt_u32w(char *out, u32 v, u16 w, char pad);

/* The value of the decimal number s, digits only, when it is 1..max;
   0 when s is empty, holds another character or is above max. */
u16 txt_num(const char *s, u16 max);

/* The hex digits, "0123456789ABCDEF" with its NUL. */
extern const char txt_hexdig[];

/* The value 0..15 of the hex digit c (0-9, A-F, a-f), or -1. */
int txt_hexval(char c);

/* 1 when a and b are equal ignoring the case of ASCII letters, else 0.
   txt_ieqn compares at most n bytes, so 1 also when the first n bytes
   are equal (n 0 always gives 1). */
int txt_ieq(const char *a, const char *b);
int txt_ieqn(const char *a, const char *b, u16 n);

/* Copy at most size - 1 bytes of s into d and end it with a NUL; a NULL
   s copies "".  size 0 writes nothing.  The rest of d is left as it
   was, not padded. */
void txt_copy(char *d, const char *s, u16 size);

/* Copy at most size - 1 (size at least 1) bytes of s into d, the ASCII
   letters upper-cased, and end it with a NUL; d may be s. */
void txt_ucopy(char *d, const char *s, u16 size);

/* The words of a list are its runs of characters other than ' ',
   separated by one or more spaces ("A D", "LPT1 LPT2 LPT3 PRN").
   txt_word_num gives the number (from 0) of the word of list equal to
   w as a whole word, ignoring the case of ASCII letters, or -1 when
   none is (also for an empty w).  txt_word_at copies word i (from 0)
   of list into out, at most size - 1 characters and a NUL; "" when list
   has fewer words; size 0 writes nothing. */
int txt_word_num(const char *list, const char *w);
void txt_word_at(const char *list, u16 i, char *out, u16 size);

/* Zero n bytes at p through a volatile pointer, so that the compiler
   cannot drop the stores to a buffer about to be freed or left; for
   passwords and other secrets. */
void txt_wipe(void *p, u16 n);

/* 1 when s is exactly eight digits YYYYMMDD naming a real date of the
   Gregorian calendar in the years 0001..9999, else 0 (also for a NULL
   s). */
int txt_date_ok(const char *s);

/* Write into out (outsz bytes, not overlapping path) path with its
   extension replaced by '.' and ext; ext is given without the dot.  The
   extension starts at the last '.' after every '\', '/' and ':' (a dot
   in a directory name does not count), and a trailing '.' is an empty
   extension.  With keep 1 a path that has an extension is copied
   unchanged, and ext is added only to one that has none.  The letters of
   ext are lower-cased when path's extension starts with a lower-case
   letter or, when path has no extension, its last character is one, so
   that names made on the host, where case matters, follow the name
   given.  Returns 0, or CF_EINVAL when the result and its NUL do not fit
   in outsz; out is then the empty string when outsz is at least 1. */
int txt_path_ext(char *out, u16 outsz, const char *path, const char *ext,
                 int keep);

/* Write into out (outsz bytes, at least 1, not overlapping path) the
   directory part of path: up to and including its last '\', '/' or
   ':', so that a file name can be appended; "" when path has none or
   the part and its NUL do not fit in outsz. */
void txt_path_dir(char *out, u16 outsz, const char *path);

/* ---------------------------------------------------------------------
 * DOS services, CDOS.C.
 *
 * CDOS.C is the only program module that issues INT 21h (the DOS-only
 * tests in SRC/COMPAT/ make their own calls), and it uses only
 * functions present in MS-DOS 3.1 or earlier.  The host build serves
 * the file access and the commit through POSIX calls and provides stubs
 * that succeed for the rest.  The Windows build issues the same calls,
 * which Windows passes to DOS, translating the addresses in protected
 * mode, but for the handle table, the critical errors, the break and
 * the idle call, which Windows owns: each says what it does there.
 * --------------------------------------------------------------------- */

/* Write the directory entry (size, time) of the file open on handle, as
   a close would, while leaving the handle open: the handle is duplicated
   (function 45h) and the duplicate closed (function 3Eh).  The DOS 3.3
   commit function 68h is never used.  Returns 0 or the error code as the
   cf_ functions give it, e.g. CF_EBADF for a handle that is not open.
   The host duplicates and closes the descriptor the same way. */
int dos_commit_handle(int handle);

/* Write every modified DOS disk buffer to its disk (function 0Dh, DOS
   1.0), so that directory changes made by rename, remove and create
   are on the disk before the next step begins; DOS otherwise may keep
   a changed directory sector in its buffers.  Open files stay open. */
void dos_disk_reset(void);

/* Issue INT 28h, the DOS idle interrupt, during which resident programs
   such as PRINT run; a program calls it while it polls for something.
   A no-op on the host.  Under Windows, which switches to another program
   only when the running one yields, it calls dos_yield instead, unless
   that is NULL. */
void dos_idle(void);

/* The yield hook of dos_idle, Windows only, NULL at start: the program
   points it at a function that lets the other programs run, a loop
   that dispatches the messages PeekMessage takes. */
#ifdef COMPAT_WIN16
extern void (*dos_yield)(void);

/* The program's instance (its HINSTANCE), Windows only, which it sets
   before it calls a module here that makes a window or a procedure
   instance of its own, a TCP connection's window (sk_open) and a
   printer job's abort procedure (prn_open), or reads a resource of its
   own, the engine's schema data (CSCHEMA.C). */
extern u16 dos_inst;
#endif

/* Critical-error (INT 24h) handler that answers Fail (AL = 3, DOS 3.0),
   so that a disk, sharing or lock error makes the failing DOS call
   return with carry set and an error code, instead of prompting "Abort,
   Retry, Fail?" on the console, which would stall a server or a session
   whose console is redirected.  The cf_ functions then return the
   extended error of the failed call, e.g. CF_ESHARE.  A program installs
   it at startup and removes it before it exits; dos_crit_install saves
   the previous vector (function 35h) and sets the new one (function
   25h), dos_crit_remove restores the saved vector if INT 24h still
   points at this handler and otherwise leaves it alone.  Calling either
   twice in a row has no further effect.  The handler's only DOS call is
   function 59h (get extended error), which DOS allows inside INT 24h.
   An error DOS does not allow to Fail (bit 3 of AH clear on entry) is
   answered Retry, since DOS would take Fail as Abort and end the program
   without its exit path; the call then waits until the error clears.
   Programs started through EXEC inherit the handler.  No-ops on the
   host.  Windows owns INT 24h: there dos_crit_install sets its error
   mode to SEM_FAILCRITICALERRORS (SetErrorMode), so that Windows answers
   Fail without asking, and dos_crit_remove sets back the mode it found;
   the failed call then returns error 53h (fail on INT 24h), as nothing
   reads the finer extended error while the error is reported. */
void dos_crit_install(void);
void dos_crit_remove(void);

/* The vector of interrupt n, read with function 35h into *seg:*off,
   for a test that checks a handler was put back: such a file defines
   COMPAT_VECT first (Watcom C 6.5 runs out of memory on some tests if
   every file reads it).  The modules under SRC/COMPAT/ get it from the
   internal section below.  DOS builds only. */
#ifdef COMPAT_VECT
#ifndef COMPAT_INTERNAL
#ifndef COMPAT_HOST
void dos_getvect(u8 n, u16 *seg, u16 *off);
#endif
#endif
#endif

/* File access through DOS handles.  Every function returns 0 or a DOS
   error code (CF_E...); buffers are ordinary pointers.  Handles are the
   DOS handle numbers, so dos_commit_handle applies to them.  On the host
   the sharing modes are accepted and ignored. */
#define CF_READ         0x00    /* access: read only                    */
#define CF_RDWR         0x02    /* access: read and write               */
#define CF_DENYRW       0x10    /* sharing (DOS 3.0): deny all access   */
#define CF_DENYW        0x20    /* sharing: deny others write access    */
#define CF_DENYNONE     0x40    /* sharing: allow others everything     */

#define CF_ENOENT       2       /* file not found                       */
#define CF_ENOPATH      3       /* path not found                       */
#define CF_EMFILE       4       /* no free handles (raise FILES=)       */
#define CF_EACCES       5       /* access denied                        */
#define CF_EBADF        6       /* invalid handle                       */
#define CF_ENOMEM       8       /* not enough memory                    */
#define CF_EINVACC      12      /* invalid access value in an open mode */
#define CF_EINVAL       13      /* invalid data (argument out of range) */
#define CF_ESHARE       32      /* sharing violation (file in use)      */
#define CF_ELOCK        33      /* lock violation                       */
#define CF_EFULL        39      /* disk full: a short write             */

/* Open an existing file (function 3Dh); mode is an access value ORed
   with a sharing value, e.g. CF_RDWR | CF_DENYRW. */
int cf_open(const char *path, u8 mode, int *h);

/* Create or truncate a file (function 3Ch) and leave it open for reading
   and writing in compatibility sharing mode. */
int cf_create(const char *path, int *h);

int cf_close(int h);

/* Duplicate handles, CDUP.C, for a test that points a standard handle
   at a file for a while and puts it back; no program links them.  Such
   a test defines COMPAT_DUP first, which spares Watcom C 6.5's memory
   as COMPAT_VECT does.  dos_dup (function 45h, DOS 2.0) gives in *nh
   the lowest free handle, open on the file h is open on; dos_dup2
   (function 46h, DOS 2.0) makes handle to, which differs from h, open on
   that file, closing it first if it is open.  The two handles then share
   one file position and access mode, and each is closed on its own.
   Each returns 0, CF_EBADF when h is not open (dos_dup2 also when to is
   outside the handle table), CF_EMFILE when dos_dup finds no free
   handle, or another CF_E code; *nh is -1 on failure.  Neither is
   offered to cf_fault.  The host build uses dup and dup2. */
#ifdef COMPAT_DUP
int dos_dup(int h, int *nh);
int dos_dup2(int h, int to);
#endif

/* Read up to n bytes; *got receives the count, 0 at end of file. */
int cf_read(int h, void *buf, u16 n, u16 *got);

/* Write exactly n bytes, n >= 1 (a zero-length DOS write truncates; use
   cf_truncate for that).  A short write returns CF_EFULL. */
int cf_write(int h, const void *buf, u16 n);

/* Write n zero bytes at the current position (n may be 0) from a
   static block of zeros (CZERO.C), one cf_write per block, so cf_fault
   sees each block as a CF_OP_WRITE; 0 or the first CF_E code, the
   zeros before it written.  DOS_SECTOR is the bytes of a disk sector,
   the unit in which DOS writes the disk.  A file that uses either
   defines COMPAT_DISK first (Watcom C 6.5 runs out of memory on some
   tests if every file reads them). */
#ifdef COMPAT_DISK
int cf_zero(int h, u32 n);
#define DOS_SECTOR      512
#endif

/* Set the file position (function 42h, from the start). */
int cf_seek(int h, u32 pos);

/* File size in bytes; the file position is left unchanged. */
int cf_size(int h, u32 *size);

/* Cut the file to size bytes; the position is left at size. */
int cf_truncate(int h, u32 size);

int cf_remove(const char *path);                   /* function 41h */
int cf_rename(const char *from, const char *to);   /* function 56h */

/* Whether path names a file or a device: 1 when it exists, 0 when it
   or its directory is not found (a directory itself counts as not
   found), and for any other error the negated CF_E code, e.g.
   -CF_EACCES, whose meaning the caller decides.  path holds no
   wildcards.  DOS function 4Eh (find first, DOS 2.0) with the normal,
   read-only, hidden and system attributes; the program's DTA is left
   as it was.  The host build uses stat. */
int cf_exists(const char *path);

/* Fault injection, for tests only; programs leave cf_fault at its
   initial null value.  When it is set, every call that changes the
   disk (cf_create, cf_create_new, cf_write, cf_truncate, cf_remove,
   cf_rename, dos_commit_handle and cf_close) first calls it with the
   call's CF_OP_ code; a nonzero result is returned as the call's error
   without anything being done (but see cf_fault_part), which lets a
   test stop all the modules of a program at the same instant, as a
   machine that stops would.  A cf_close that the hook fails still
   closes the handle, and the close inside dos_commit_handle is not
   offered to the hook. */
#define CF_OP_CREATE    1
#define CF_OP_WRITE     2
#define CF_OP_TRUNC     3
#define CF_OP_RENAME    4
#define CF_OP_REMOVE    5
#define CF_OP_COMMIT    6
#define CF_OP_CLOSE     7

typedef int (*cf_fault_fn)(int op);
extern cf_fault_fn cf_fault;

/* The read hook, for tests only; programs leave it at its initial null
   value.  When it is set, cf_open, cf_read and cf_readblk first call it
   with CF_OP_OPEN or CF_OP_READ, and a nonzero result is returned as
   the call's error without anything being done (an open then leaves *h
   -1, a read *got 0).  cf_exists calls it with CF_OP_EXISTS, and a
   nonzero result stands for the search's error: CF_ENOENT or
   CF_ENOPATH makes it report 0, any other code its negation.  It is
   apart from cf_fault, whose hooks count only the calls that change
   the disk. */
#define CF_OP_OPEN      8
#define CF_OP_READ      9
#define CF_OP_EXISTS    10

extern cf_fault_fn cf_fault_rd;

/* The partial mode of the fault hook, for tests only; programs leave it
   0.  A CF_OP_WRITE that cf_fault fails, of more than cf_fault_part
   bytes, writes its first cf_fault_part of them (the DOS build issues
   function 40h with that count) and returns CF_EFULL, as a write that
   fills the disk does; a test can then see what the caller does with
   the bytes that reached the file. */
extern u16 cf_fault_part;

/* The DOS version (function 30h, DOS 2.0): *major and *minor, the minor
   number in hundredths, so MS-DOS 3.10 gives 3 and 10 and MS-DOS 3.30
   gives 3 and 30.  DOS 1.x leaves AL 0 and so reports major 0.  The host
   build reports 3.10, the oldest version the programs accept.  Returns
   1 when the version is 3.10 or later, the floor of the programs, else
   0. */
int dos_version(u8 *major, u8 *minor);

/* Grow the near heap to the top of DGROUP in one step, where the steps
   of Watcom C 6.5's malloc could damage it (CDOS.C); nothing under the
   other compilers.  A program calls it first in main, before anything
   allocates; cmem_init does for the programs that start with it.  Its
   callers define COMPAT_HEAP first, which spares Watcom C 6.5's memory
   as COMPAT_VECT does. */
#ifdef COMPAT_HEAP
#ifdef COMPAT_WC6
void dos_heap_top(void);
#else
#define dos_heap_top()
#endif
#endif

/* Today's date from DOS (function 2Ah): year 1980..2099, month 1..12,
   day 1..31.  The host build reads the local clock. */
void dos_today(u16 *year, u8 *month, u8 *day);

/* The time of day from DOS (function 2Ch, DOS 1.0): hour 0..23, minute
   and second 0..59 (the second 60 MS-DOS 3.10 reports at some ticks is
   given as 59).  The host build reads the local clock. */
void dos_time(u8 *hour, u8 *min, u8 *sec);

#define DOS_STAMPLEN    19      /* characters of a dos_stamp            */

/* Write the date and time now (CRUN.C), "YYYY-MM-DD HH:MM:SS" and a NUL,
   into d (DOS_STAMPLEN + 1 bytes).  The date is read again after the
   time, and both once more when the day changed between the two
   reads. */
void dos_stamp(char *d);

/* Largest per-process handle table dos_set_handles accepts. */
#define DOS_MAXHANDLES  255

/* Let this process hold up to n DOS handles at once, n <= DOS_MAXHANDLES.
   DOS starts a program with a 20-entry handle table in its PSP, five of
   them taken by the standard handles; the function that enlarges it
   (67h) first appeared in DOS 3.3 and is not used.  Instead a table of n
   bytes is allocated from the near heap, the current entries are copied
   into it, the rest marked free (0FFh), and the PSP's table size (offset
   32h) and far table pointer (offset 34h) are pointed at it, which DOS
   3.0 and later honour.  The PSP is found with function 51h.
   Returns 0 (also when n equals the current size), CF_EINVAL when n is
   smaller than the current size or above DOS_MAXHANDLES, or CF_ENOMEM.
   A later call with a larger n moves the table again and frees the
   previous one.
   The table in use is never freed: it must outlive every open file,
   and DOS closes the program's handles through it when the program
   ends.
   Nothing restores the PSP fields at exit, and nothing needs to, since
   the table lives in the program's own memory and the PSP goes with it.
   The parent (COMMAND.COM) keeps its own PSP and table.  A program
   started through EXEC gets a fresh PSP with a 20-entry table, which
   inherits only handles 0..19 of this process.
   This raises only the per-process limit: every open file also takes
   one of the system-wide entries set by FILES= in CONFIG.SYS, so FILES=
   must cover all the files the program opens, plus about 3 for the
   standard devices and any held by resident programs.
   Handles above 19 must be used only through the cf_ functions: the
   compilers' own run-time file calls (open, fopen, close and the like)
   keep arrays of 20 entries per process and do not accept them.
   Interrupts are disabled while the table is moved and enabled again
   afterwards, so the function must not be called with interrupts
   disabled.  The host build checks n the same way, keeps the size, and
   does nothing else.  Under Windows, which keeps the handle tables of
   its tasks, the table is enlarged by SetHandleCount, after the same
   checks, and CF_ENOMEM is returned when it gives fewer than n. */
int dos_set_handles(u16 n);

/* Bytes of a buffer that holds a DOS path, NUL included. */
#define DOS_PATHMAX     128

/* Longest DOS machine name, NUL excluded. */
#define DOS_MACHLEN     15

/* The machine name set by the network software (function 5E00h, DOS
   3.1), into name, which holds DOS_MACHLEN + 1 bytes, trailing spaces
   removed.  Returns 0, or 1 when no name is defined: no network
   software is loaded, DOS refuses the function, or the name is empty
   or all spaces.  The host build always returns 1. */
int dos_machine_name(char *name);

/* Make Ctrl-C and Ctrl-Break harmless.  dos_break_off saves the BREAK
   setting (function 33h, AL 00h) and turns it off (33h, AL 01h, DL 0),
   so DOS checks for a break only during its console, printer and serial
   calls; and it points INT 23h (function 25h) at a handler that returns
   with IRET, so a break DOS still sees there makes it carry on with the
   call instead of ending the program.  dos_break_restore sets the
   saved BREAK setting back; DOS itself restores the program's INT 23h
   vector from the PSP when the program ends.  A second dos_break_off,
   and a dos_break_restore with none in force, do nothing.  No-ops on
   the host and under Windows, which owns the keyboard and Ctrl-Break. */
void dos_break_off(void);
void dos_break_restore(void);

/* The BREAK setting now (function 33h, AL 00h): 1 on, 0 off; for tests.
   The host build reports 0. */
int dos_break_get(void);

/* Stack use, for tests and the server's event log.  cstk_fill writes
   CSTK_PAT over the unused part of the stack, from just below the
   current stack pointer down to the stack's lowest usable address:
   _STACKLOW under Open Watcom and Watcom C 6.5; under Turbo C 2.01,
   whose medium-model stack lies right above the bytes reserved for the
   near heap (the _heaplen its near-heap module sets), the end of those.
   cstk_unused later counts the bytes at the bottom that still hold the
   pattern, so a run can report how close it came to overflowing; under
   Turbo C it reports 0 once the near heap has grown past its
   reservation into the stack.  The host build does nothing and reports
   CSTK_UNKNOWN. */
#define CSTK_UNKNOWN    0xFFFFu
#define CSTK_PAT        0x5Au   /* the fill byte                        */
void cstk_fill(void);
u16 cstk_unused(void);

/* Free bytes on drive (0 the default drive, 1 A:, 2 B:, ...): function
   36h (DOS 2.0), free clusters x sectors per cluster x bytes per
   sector, capped at 0xFFFFFFFF.  Returns 0; CF_EINVAL for a drive that
   does not exist; or, when DOS raised a critical error for the drive
   (an empty floppy drive, for one) and the handler of dos_crit_install
   answered Fail, which MS-DOS 3.10 allows here, the error the handler
   recorded, as the cf_ functions return it.  The host build reports
   dos_free_host, 0xFFFFFFFF unless a test sets it. */
int dos_disk_free(u8 drive, u32 *bytes);
#ifdef COMPAT_HOST
extern u32 dos_free_host;
#endif

/* Put the character device open on handle into raw mode (raw 1: DOS
   passes every byte, Ctrl-Z included, unchanged) or cooked mode (raw
   0), through IOCTL functions 4400h and 4401h (DOS 2.0); *was receives
   the mode it had (1 raw), for restoring.  CF_EINVAL when the handle is
   a file.  The host build does nothing and reports raw. */
int dos_dev_raw(int handle, int raw, int *was);

/* ---------------------------------------------------------------------
 * Printer, CPRN.C.
 *
 * A job goes to one port: a parallel port through the BIOS (INT 17h:
 * function 00h sends a byte, 02h reads the status), or the DOS device
 * PRN (handle 4, in raw mode for the job through dos_dev_raw, so a
 * redirected printer -- MODE LPT1:=COM1, a network printer -- gets the
 * bytes unchanged; DOS errors arrive through dos_crit_install's
 * handler).  A BIOS port exists when the BIOS data area holds an I/O
 * address for it (0040:0008 + 2 x port); its status is read before the
 * first byte, so an absent, switched-off or paper-less printer is
 * reported at once instead of after the BIOS timeout of each byte,
 * which bounds every byte sent (0040:0078 + port).
 *
 * prn_capture sends every later job to a file instead, on every DOS
 * build and the host, for tests (the host build has no printer
 * otherwise).
 *
 * Under Windows, CPRNW.C, a job goes to the default printer through
 * GDI, whatever its port, the job's bytes laid out on its pages as a
 * printer lays them out, and a write fails with PRN_EIO once the job
 * has failed or has been cancelled (prn_cancel); there is no capture
 * and no prn_status.
 * --------------------------------------------------------------------- */
#define PRN_LPT1        0
#define PRN_LPT2        1
#define PRN_LPT3        2
#define PRN_DOS         3       /* the DOS device PRN (handle 4)        */

#define PRN_OK          0
#define PRN_ENODEV      1       /* no such port                         */
#define PRN_EOFFLINE    2       /* not selected, or I/O error           */
#define PRN_EPAPER      3       /* out of paper                         */
#define PRN_ETIMEOUT    4       /* the printer did not take a byte      */
#define PRN_EIO         5       /* DOS device or capture file: the write
                                   failed                               */

/* Start a job on port (PRN_LPT1..PRN_DOS): PRN_OK, or the status that
   keeps the printer from printing.  At most one job is open. */
int prn_open(u8 port);

/* The printer's state now: PRN_OK or a PRN_E code. */
int prn_status(void);

/* Send n bytes, stopping at the first one not taken: *done receives how
   many were taken; returns PRN_OK or the PRN_E code of the byte that
   failed, which a later call may send again. */
int prn_write(const u8 *buf, u16 n, u16 *done);

/* End the job; the DOS device gets back the mode it had. */
void prn_close(void);

/* Windows: cancel the job open, as the user asked to: GDI's abort
   procedure ends it while a page prints, or the next prn_write or
   prn_close does. */
#ifdef COMPAT_WIN16
void prn_cancel(void);
#endif

/* Create (truncate) path and append every later job to it, whatever its
   port; NULL ends capturing.  Every call, a refused one included, first
   ends the capture before it.  The path is copied.  Returns 0 or a CF_E
   code: CF_EINVAL when path holds DOS_PATHMAX bytes or more, or when DOS
   opens it as a device (PRN, LPT1, NUL), which is not captured into. */
int prn_capture(const char *path);

/* ---------------------------------------------------------------------
 * Test-VM exit, CEXIT.C.
 *
 * Flushes stdout and stderr and commits DOS handles 1 and 2 with
 * dos_commit_handle, so that redirected output reaches its file before
 * the machine stops, then writes code to I/O port 501h, where QEMU's
 * isa-debug-exit device ends the virtual machine with host exit status
 * code * 2 + 1.  On a machine without that device the port is unused,
 * nothing happens, and the function returns.  The host build only
 * flushes.
 * --------------------------------------------------------------------- */
void cexit_vm(u8 code);

/* ---------------------------------------------------------------------
 * Far memory, CMEM.C.
 *
 * Buffers too large for the near data segment of the medium model live
 * outside it, in blocks of at most CMEM_MAXBLK bytes.  Portable code
 * never holds a far pointer: it holds a cmem_blk handle (an ordinary
 * pointer to a small descriptor in the near heap) and moves bytes
 * between the block and its own near buffers with cmem_get and
 * cmem_put.  Offsets and counts are bytes; every access must lie inside
 * the block (off + n <= the size given to cmem_alloc).  An access
 * outside it is a program error: the DOS builds then copy nothing, and
 * the host build calls abort(), so tests find it.  A count of 0 does
 * nothing.  On the host a block is ordinary malloc memory.
 *
 * Under Windows a block is a moveable block of global memory
 * (GlobalAlloc), locked only inside the one cmem_ call that reaches it,
 * so that real-mode Windows, which moves blocks to make room, can move
 * it between calls: a block left locked stays where it is and splits
 * the free memory around it.  The few blocks a program keeps for the
 * whole run come from cmem_alloc_perm (COMPAT_FIND below).
 * --------------------------------------------------------------------- */
#define CMEM_MAXBLK     32768u  /* largest block and largest buffer     */

/* The near heap of a program built by Turbo C or Open Watcom, in bytes
   (Watcom C 6.5 grows its near heap to all of DGROUP, as its far heap
   takes blocks out of the near heap's free space once DOS has none
   left: CMEM.C): what DGROUP's
   64 KB leave beside the program's static near data, its stack and this
   heap goes to the far heap.  Every DOS program that links CMEM.C links
   exactly one near-heap module too, which sets the size its compiler
   reserves (CMEM.C says how for each): CMEMHT.C gives CMEM_TCPHEAP to the
   programs on the TCP stack (INVTCP, and any other that links WATTCP),
   CMEMHI.C and CMEMHR.C give INV and the network-only client (THIN of
   SRCS.MK) a smaller heap under Turbo C (CMEM_INVHEAP and CMEM_REMHEAP,
   which those modules define and explain) and CMEM_NEARHEAP under Open
   Watcom, and CMEMHN.C gives CMEM_NEARHEAP to every other program.  Under
   Open Watcom a near allocation beyond it fails; under Turbo C it is a
   reservation below the stack, which the allocator does not enforce (a
   heap that outgrows it takes stack space, which the stack checks of the
   tests report).  Each program's heap must fit in DGROUP beside the most
   static near data and the stack its builds allow, which TEST/mapchk.sh
   checks program by program.

   CMEM_NEARHEAP is the most near heap a DOS test run of such a program
   was measured to take under Turbo C, whose allocator adds the largest
   headers (the engine with every table and index open, a data
   directory created and seeded, the deepest screens of T_SCRE), rounded
   up to whole kilobytes, plus four kilobytes for growth; it is measured
   again whenever the engine's or the client's near allocations grow.
   CMEM_TCPHEAP is the heap of the programs on the TCP stack, which keeps
   its sockets and packet buffers in DGROUP: room for the descriptors of
   their far blocks and NCB set and the C library's stream buffers.  It
   is chosen, not measured: those allocations are few and small, and it
   is the four kilobytes of room for growth that CMEM_NEARHEAP keeps. */
#define CMEM_NEARHEAP   28672u
#define CMEM_TCPHEAP    4096u

typedef struct cmem_blk cmem_blk;   /* a far block (opaque)             */

/* Grow the near heap before anything takes far memory; a program calls
   it first thing in main.  With Open Watcom and Watcom C 6.5 the near
   heap grows by enlarging the program's DOS memory block, which a far
   block allocated right after it would stop, so a later near allocation
   (the engine's tables and indexes) would fail; the heap is claimed by
   allocating and freeing near blocks, the program's near heap of them
   under Open Watcom and all of DGROUP under Watcom C 6.5.  Turbo C
   sizes its near heap at start-up (the near-heap module sets the size),
   the host build has none, and under Windows the near heap is the local
   heap, which Windows grows inside DGROUP on demand whatever the far
   blocks: there it does nothing, and the program links no near-heap
   module.  A later call, as when a test runs a program's main again,
   does no harm: it only allocates near blocks and frees them again, so
   every block taken before stays as it was. */
void cmem_init(void);

/* A new block of size bytes (1..CMEM_MAXBLK), its contents undefined;
   NULL when size is out of range or memory has run out. */
cmem_blk *cmem_alloc(u16 size);

/* Fault injection, for tests only; programs leave cmem_fault at its
   initial null value.  When it is set, cmem_alloc first calls it with
   the size asked for, and a nonzero result makes the allocation fail
   (NULL) as if memory had run out. */
typedef int (*cmem_fault_fn)(u16 size);
extern cmem_fault_fn cmem_fault;

/* Free the block and its descriptor; b may be NULL. */
void cmem_free(cmem_blk *b);

/* cmem_alloc of size bytes for a block the program keeps while others
   come and go, in the place of a block of was bytes that it has just
   freed: the new block starts where that one did when it fits there,
   so that what is left of the place stays one free piece with the
   memory above and nothing free lies under the new block.  Open
   Watcom's and Turbo C's far heaps give the lowest free address by
   themselves, so there this is cmem_alloc; Watcom C 6.5's does not
   (CMEM.C).  Only a file that defines COMPAT_REPLACE before it includes
   this header sees the call, which spares Watcom C 6.5's memory as
   COMPAT_FIND does. */
#ifdef COMPAT_REPLACE
#ifdef COMPAT_WC6
cmem_blk *cmem_replace(u16 size, u16 was);
#else
#define cmem_replace(size, was) cmem_alloc(size)
#endif
#endif

/* The size given to cmem_alloc. */
u16 cmem_size(cmem_blk *b);

/* Copy n bytes at off out of the block into dst (cmem_get), or from src
   into the block (cmem_put). */
void cmem_get(cmem_blk *b, u16 off, void *dst, u16 n);
void cmem_put(cmem_blk *b, u16 off, const void *src, u16 n);

/* Set n bytes at off to v. */
void cmem_set(cmem_blk *b, u16 off, u8 v, u16 n);

/* Move n bytes from offset from to offset to inside one block; the two
   ranges may overlap. */
void cmem_move(cmem_blk *b, u16 to, u16 from, u16 n);

/* Copy n bytes from offset soff of block src to offset doff of block
   dst; dst and src must be different blocks. */
void cmem_copy(cmem_blk *dst, u16 doff, cmem_blk *src, u16 soff, u16 n);

/* Read up to n bytes from the open handle h into the block at off, as
   cf_read does into a near buffer, with one DOS call (function 3Fh) that
   moves whole sectors straight into the block: 0 or a CF_E code, *got
   the count read, 0 at end of file.  A range outside the block reads
   nothing and returns CF_EINVAL (the host aborts). */
int cf_readblk(int h, cmem_blk *b, u16 off, u16 n, u16 *got);

/* ---------------------------------------------------------------------
 * File search, directories and new files, CDOS.C; far writes, blocks
 * for the whole run and the free hook, CMEM.C; device and NetBIOS
 * names, CTEXT.C.
 *
 * Only a file that defines COMPAT_FIND before it includes this header
 * sees this section, which spares Watcom C 6.5's memory as
 * COMPAT_NETBIOS does: the programs that scan a directory, name files,
 * make a file only while it is new, write from far blocks or keep a
 * block for the whole run are few.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_FIND
/* The files whose names match pattern, a path of at most DOS_PATHMAX -
   1 bytes whose last part may hold the wildcards '*' and '?' (functions
   4Eh and 4Fh, DOS 2.0, with the normal attribute: hidden and system
   files, directories and the volume label are not found).
   dos_find_first starts a search in f and gives the first name,
   dos_find_next each later one, into name (DOS_FINDNAME bytes): the 8.3
   name without its directory, upper-case as DOS stores it.  Under DOS
   the order is the directory's, which a caller must not rely on; the
   host build gives name order.  Each returns 0;
   CF_ENOENT when no file (or no further file) matches, and on every
   later call; dos_find_first CF_ENOPATH when the directory does not
   exist and CF_EINVAL for a longer pattern; or another CF_E code; name
   is "" on failure.  f holds the search between calls: the caller
   neither changes nor copies it, and may run several searches at once,
   each in its own f, and abandon one at any time.  The DTA is f for the
   call (function 1Ah) and the program's own (function 2Fh) again
   before it returns.  The host build keeps the pattern and the last
   name given in f, reads the directory again on every call and gives
   the files in name order, the next after the last name, matching as
   DOS does, ignoring case, the names that are 8.3 names; a name keeps
   the case it has on the disk. */
#define DOS_FINDNAME    13      /* bytes of a found name: 8.3 and NUL   */
#ifdef COMPAT_HOST
#define DOS_FINDSZ      (DOS_PATHMAX + DOS_FINDNAME)
#else
#define DOS_FINDSZ      44      /* DOS's 43-byte search record, even    */
#endif

typedef struct {
    u8 b[DOS_FINDSZ];
} dos_find;

int dos_find_first(dos_find *f, const char *pattern, char *name);
int dos_find_next(dos_find *f, char *name);

/* Make (cf_mkdir, function 39h, DOS 2.0) or remove (cf_rmdir, function
   3Ah) the directory path, given without a final separator; for tests,
   which make the directories a mailbox holds.  0; CF_ENOPATH when the
   directory above path, or for cf_rmdir path itself, does not exist;
   CF_EACCES when path exists already, or for cf_rmdir is not empty; or
   another CF_E code.  Neither is offered to cf_fault. */
int cf_mkdir(const char *path);
int cf_rmdir(const char *path);

/* The volume label of drive (0 the default drive, 1 A:, 2 B:, ...)
   into label (DOS_LABELLEN + 1 bytes), its trailing spaces removed, ""
   when the disk has none: function 4Eh (DOS 2.0) on "X:\*.*" with the
   volume-label attribute alone, which DOS 3 answers with the label
   only, given as a file name with a '.' after its eighth character,
   which is taken out again.  Modified DOS buffers are written first
   (dos_disk_reset), and the search makes DOS check the disk in the
   drive, so the label read is that of the floppy in the drive now: a
   drive whose change line reports a swap, or with none any access two
   seconds or more after the last, makes DOS read the disk again.
   Returns 0, or the CF_E code of the search, such as the critical
   error of an empty drive (CF_ENOTREADY), which the handler of
   dos_crit_install must answer for the call to return; label is then
   "".  The host build has no disks and gives the label dos_label_host,
   which a test sets, "" while it is NULL, whatever the drive.  It is
   in CLABEL.C, which only the programs that read a label link.  Only a
   file that defines COMPAT_LABEL first sees this, which spares Watcom C
   6.5's memory as COMPAT_VECT does. */
#ifdef COMPAT_LABEL
#define DOS_LABELLEN    11      /* characters of a volume label         */
#define CF_EWRPROT      19      /* the disk is write-protected          */
#define CF_ENOTREADY    21      /* the drive is not ready (no disk)     */
int dos_volume_label(u8 drive, char *label);

/* The bytes of a cluster, the unit in which DOS gives a file room, on
   drive (as for dos_volume_label), into *bytes: function 36h (DOS 2.0),
   sectors per cluster x bytes per sector.  Returns 0, or CF_EINVAL for
   a drive DOS cannot answer for, *bytes then 0; the installer asks
   dos_disk_free first, which tells the errors apart.  The host build
   gives dos_cluster_host, 512 unless a test sets it.  In CLABEL.C. */
int dos_disk_cluster(u8 drive, u32 *bytes);
#ifdef COMPAT_HOST
extern const char *dos_label_host;
extern u32 dos_cluster_host;
#endif
#endif

/* cf_create of a file that does not exist yet (function 5Bh, DOS 3.0):
   CF_EEXIST, with nothing changed, when path is already there, so that
   a file another program has just made is never truncated. */
#define CF_EEXIST       80      /* file exists                          */
int cf_create_new(const char *path, int *h);

/* Write exactly n bytes (n >= 1) at off of the block to the open handle
   h, as cf_write does from a near buffer, with one DOS call (function
   40h) straight from the block: 0, CF_EFULL for a short write, or
   another CF_E code.  cf_fault sees it as a CF_OP_WRITE, with
   cf_fault_part as for cf_write.  A range outside the block writes
   nothing and returns CF_EINVAL (the host aborts). */
int cf_writeblk(int h, cmem_blk *b, u16 off, u16 n);

/* cmem_alloc of a block the program keeps for the whole run, allocated
   once at start, before the blocks it allocates and frees as it goes.
   Under Windows its address never changes, so that a COMPAT module may
   hand it to Windows or a driver (cmem_ptr): it is fixed (GMEM_FIXED)
   in real mode, where Windows puts fixed blocks at the bottom of the
   heap, out of the way of the blocks it moves, and in protected mode a
   moveable block locked until cmem_free, which there costs nothing, as
   Windows moves a locked block's memory behind its selector.  The
   other builds give an ordinary block. */
#ifdef COMPAT_WIN16
cmem_blk *cmem_alloc_perm(u16 size);

/* The lock state of b, for tests only, Windows only: the lock count
   Windows keeps for b's global memory block (GlobalFlags & GMEM_LOCKCOUNT)
   and in *fixed 1 when the block is fixed (GMEM_FIXED, known by its
   handle being its selector), else 0.  The call locks the block and
   unlocks it again before it reads the count, so it leaves the count
   as it found it.  The count is exact in real mode only: in standard
   and 386 enhanced mode Windows reports 0 for a block that cannot be
   discarded, locked or not.  Between cmem_ calls, a block of cmem_alloc
   is moveable and in real mode has the count 0; one of cmem_alloc_perm
   is fixed in real mode, and in protected mode moveable. */
u16 cmem_locks(cmem_blk *b, int *fixed);
#else
#define cmem_alloc_perm(size) cmem_alloc(size)
#endif

/* The free hook, for tests only; programs leave it at its initial null
   value.  When it is set, cmem_free first calls it with the block (not
   NULL), which it may read: an observation point for the blocks a
   module zeroes before it frees them. */
extern void (*cmem_freeing)(cmem_blk *b);

/* 1 when the base name of n characters at s, cut to eight as DOS cuts
   it, names a DOS device, which DOS finds in every directory and with
   any extension: CON, AUX, PRN, NUL, COM1..COM4, LPT1..LPT3 or CLOCK$,
   letters of either case; else 0 (CTEXT.C). */
int txt_dosdev(const char *s, u16 n);

/* 1 when path may name a file written for the user, an export or a
   report: 1..max characters, none of them a control byte, a space, DEL
   or one of " * + , ; < = > ? [ ] | (which DOS refuses in names, so
   such a path never needs quotes on the wire); its last component, read
   as DOS reads it, with a base name of at least one character that is
   no DOS device (txt_dosdev) and at most one dot, and no extension
   whose first three characters (all DOS keeps of a longer one) are DBF,
   DBT or NDX, the data files', or that starts with &, $, % or #, the
   scratch files' of the engine and of the client; else 0 (CTEXT.C). */
int txt_outfile_ok(const char *path, u16 max);

/* 1 when s is a NetBIOS name a program may take or call: 1 to
   NB_NAMELEN - 1 characters, none of them a control character, a space
   or DEL, the first not the wildcard '*'; else 0 (CTEXT.C). */
int txt_nbname_ok(const char *s);
#endif

/* ---------------------------------------------------------------------
 * NetBIOS (INT 5Ch), CNETB.C.
 *
 * Every file sees NB_NAMELEN and NB_MSGMAX, which other headers size
 * buffers with; only a file that defines COMPAT_NETBIOS before it
 * includes this header sees the rest of this section and the next:
 * Watcom C 6.5 runs out of memory compiling the largest tests when
 * every file reads them.
 *
 * Every function of this section except nb_listen_wait returns only
 * when its command has completed, and returns the NetBIOS return code
 * (NB_OK on success).  Names are C strings of at most NB_NAMELEN - 1
 * characters; longer names are truncated, shorter ones are padded with
 * spaces, and the last byte of the 16-byte NetBIOS name is 0x00.  The
 * remote name "*" passed to nb_listen accepts a call from any name and
 * is sent as '*' followed by spaces.  rto and sto are the receive and
 * send timeouts of the new session in 500 ms units, 0 meaning no
 * timeout.  Buffers are ordinary pointers.  The no-wait commands of the
 * next section share the return codes.
 *
 * Under Windows the provider is reached through KERNEL's NetBIOSCall,
 * which Windows maps in protected mode, the NCB and its buffer given by
 * their protected-mode addresses.  A command of this section then runs
 * as its no-wait form, polled with dos_idle, as a wait-form command
 * would stop every program until it ends, but CANCEL, which has no
 * no-wait form; the program never calls an nb_ or nba_ function from
 * its dos_yield, nor from a message the yield dispatches.
 * nb_listen_wait, nb_last_cancel_rc, nb_send and nb_recv are left out:
 * their NCB or buffer lies in DGROUP, which Windows may move while the
 * program yields.
 * --------------------------------------------------------------------- */
#define NB_NAMELEN      16
#define NB_MSGMAX       2048    /* longest message a program sends      */

#ifdef COMPAT_NETBIOS
#define NB_OK             0x00  /* command completed successfully       */
#define NB_RC_ILLCMD      0x03  /* invalid command or argument          */
#define NB_RC_TIMEOUT     0x05  /* command timed out                    */
#define NB_RC_INCOMPLETE  0x06  /* receive buffer smaller than message  */
#define NB_RC_BADLSN      0x08  /* no session has that LSN              */
#define NB_RC_SESSCLOSED  0x0A  /* session closed                       */
#define NB_RC_CANCELLED   0x0B  /* command was cancelled                */
#define NB_RC_DUPNAME     0x0D  /* name already in the local name table */
#define NB_RC_NAMEFULL    0x0E  /* ADD NAME: local name table full      */
#define NB_RC_DELPEND     0x0F  /* DELETE NAME: deferred, sessions open */
#define NB_RC_SESSFULL    0x11  /* local session table full             */
#define NB_RC_REJECTED    0x12  /* CALL: the name has no LISTEN posted  */
#define NB_RC_NOANSWER    0x14  /* CALL: no station has the name        */
#define NB_RC_NONAME      0x15  /* name not in the local name table     */
#define NB_RC_NAMEINUSE   0x16  /* ADD NAME: another station has it     */
#define NB_RC_NAMEDEL     0x17  /* LISTEN: its name was deleted         */
#define NB_RC_ABORTED     0x18  /* session ended abnormally             */
#define NB_RC_TOOMANY     0x22  /* too many commands in progress        */
#define NB_RC_NOCANCEL    0x26  /* CANCEL: the command cannot be ended  */
#define NB_RC_NOT_PRESENT 0xFB  /* no NetBIOS provider detected         */
#define NB_RC_PENDING     0xFF  /* command still in progress            */

/* 1 if a NetBIOS provider answers on INT 5Ch, else 0; or, in a Windows
   program only, NB_PRES_NOMEM when the conventional-memory block its
   commands go through cannot be allocated, so no provider was asked.
   Until a call of nb_present() has returned 1, every other nb_ function
   returns NB_RC_NOT_PRESENT without issuing INT 5Ch. */
#define NB_PRES_NOMEM   2
int nb_present(void);

/* Add name to the local name table; *num receives its name number. */
u8 nb_add_name(const char *name, u8 *num);
u8 nb_delete_name(const char *name);

#define NB_NAMETRIES    8       /* generated local names tried          */

/* Add the program's local name (CNBNAME.C) into name (NB_NAMELEN
   bytes): local, or for NULL the DOS machine name (dos_machine_name)
   when there is one, else prefix and four hex digits of the timer, a
   different one up to NB_NAMETRIES times while the name is taken
   (NB_RC_NAMEINUSE or NB_RC_DUPNAME).  Names are upper-cased (ASCII
   letters) and cut to NB_NAMELEN - 1 characters.  An ADD NAME of a
   name given or of the machine name that gives NB_RC_DUPNAME finds it
   already in the local name table: it is used as it is.  Returns NB_OK
   when the name is ready, else the code of the last ADD NAME; *added
   is 1 when the name was added, for the program to delete at its end,
   else 0. */
u8 nb_local_name(char *name, const char *local, const char *prefix,
                 u8 *added);

/* Open a session from local to remote (nb_call) or wait for remote to
   call local (nb_listen); *lsn receives the local session number.
   nb_listen waits for ever, with the machine blocked inside INT 5Ch: it
   suits only a program with nothing else to do until the call arrives,
   and no program calls it yet (NBTEST's server waits through
   nb_listen_wait). */
u8 nb_call(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto);
u8 nb_listen(const char *local, const char *remote, u8 *lsn, u8 rto, u8 sto);
#ifndef COMPAT_WIN16

/* nb_listen that gives up after the given number of seconds (0 = wait
   for ever); the machine idles (INT 28h) while waiting.  On expiry the
   LISTEN is cancelled and NB_RC_TIMEOUT is returned.  If the provider
   does not confirm the cancel, the LISTEN stays pending inside the
   provider and NB_RC_PENDING is returned; until it finishes, later calls
   return NB_RC_PENDING at once without starting a new LISTEN, and the
   program must not exit, because the provider may still write into the
   LISTEN's control block.  Deleting the name usually makes the provider
   end such a LISTEN.  *lsn is 0 from entry until a call arrives. */
u8 nb_listen_wait(const char *local, const char *remote, u8 *lsn,
                  u8 rto, u8 sto, u16 seconds);

/* Return code of the most recent CANCEL issued by nb_listen_wait on
   expiry; NB_OK if none has been issued. */
u8 nb_last_cancel_rc(void);

/* Send len bytes as one message.  nb_recv: *len is the buffer size on
   entry and the number of bytes received on return. */
u8 nb_send(u8 lsn, const void *buf, u16 len);
u8 nb_recv(u8 lsn, void *buf, u16 *len);
#endif
u8 nb_hangup(u8 lsn);

/* ---------------------------------------------------------------------
 * NetBIOS no-wait commands, CNETB.C.
 *
 * A set of no-wait NCBs lets a program keep several NetBIOS commands in
 * progress while it does other work, and poll each one for completion
 * (there are no post routines).  nba_open allocates the n NCBs of a set
 * as one far block of 64-byte NCBs, named by the handles 0 .. n - 1.
 * Several sets may be open at once, one per user (a test runs a client
 * and a server in one program).
 *
 * A post issues one command on a free NCB and returns at once: NB_OK
 * when the provider took the command, which is then in progress (it may
 * already have completed: poll it), or a final code when it did not,
 * the NCB staying free: NB_RC_NOT_PRESENT until nb_present() has
 * returned 1, NB_RC_TOOMANY when the provider has no room for another
 * command (post again later), NB_RC_ILLCMD for a handle out of range or
 * not free, a len of 0 or above NB_MSGMAX, or an off + len beyond the end
 * of b, or another code of the provider; nba_poll, nba_lsn, nba_len and
 * nba_callname then still give what they gave before the post.  While a
 * command is in progress the provider owns its NCB and its buffer and
 * writes into them: neither may be changed, reused or freed until
 * nba_poll has returned a final code or nba_cancel NB_OK; the NCB is
 * then free again.  A buffer is len bytes at offset off of a far block
 * b of nba_alloc; portable code never sees a far pointer.  The
 * wait-form nb_ commands of the section above use an NCB of their own
 * and may be issued while no-wait commands are in progress.
 *
 * Under Windows a set's NCBs and the blocks of nba_alloc are fixed, so
 * that the provider may write into them while the program yields, and
 * a buffer in a block that moves between calls (cmem_alloc's) is
 * NB_RC_ILLCMD.  One RECEIVE and one SEND of NB_MSGMAX bytes may be in
 * progress at once: in protected mode Windows copies the buffers of the
 * commands in progress into a pool of its own (NetHeapSize of
 * SYSTEM.INI, 8 KB in standard mode and 12 KB in 386 enhanced mode by
 * default), and a command that finds no room there fails.
 *
 * The host build allocates sets as the DOS builds do, and every post
 * returns NB_RC_NOT_PRESENT.
 * --------------------------------------------------------------------- */
#define NBA_MAX         16      /* NCBs in one set                      */

typedef struct nba nba;         /* a set of no-wait NCBs (opaque)       */

/* A far block for the buffers of nba_recv and nba_send, as cmem_alloc
   gives one and freed by cmem_free; under Windows a block of
   cmem_alloc_dos. */
#ifdef COMPAT_WIN16
cmem_blk *nba_alloc(u16 size);
#else
#define nba_alloc(size) cmem_alloc(size)
#endif

/* Open a set of n free NCBs in *out.  Returns 0, CF_EINVAL when n is
   not 1..NBA_MAX, or CF_ENOMEM; *out is NULL then. */
int nba_open(nba **out, u16 n);

/* Cancel every command of the set still in progress, wait a bounded
   time for each to complete, and free the set.  Returns the number of
   NCBs the provider still owned when the wait gave up, 0 normally; the
   far block is then not freed, since the provider may still write into
   it, and stays allocated for the rest of the run, but the set is gone
   all the same.  Hanging up the sessions and deleting the name the
   commands use before the close makes the provider end them.  With a
   nonzero count the program must not end while the provider may still
   write into a kept block: it reports the count, then waits in
   nba_drain, and only then exits.  s may be NULL (returns 0). */
u16 nba_close(nba *s);

/* The number of NCBs in the blocks nba_close kept that the provider
   still owns, read afresh from their completion bytes on every call; a
   block whose NCBs have all completed is freed.  0 when none is kept. */
u16 nba_stuck(void);

/* Wait, calling dos_idle, until nba_stuck returns 0 or a key is
   pressed; a key, which means the operator accepts the risk of ending
   while the provider may still write into a kept block, is read.
   Returns the NCBs still owned, 0 when none is kept.  CNBDRAIN.C,
   over key_peek and key_read (CKEY.C). */
u16 nba_drain(void);

/* Post a LISTEN on NCB h for a call to the name local from remote ("*"
   for any name), with the timeouts rto and sto as for nb_listen.  It
   completes with NB_OK when a call has opened a session; nba_lsn and
   nba_callname then give the session and the caller's name. */
u8 nba_listen(nba *s, u16 h, const char *local, const char *remote,
              u8 rto, u8 sto);

/* Post a CALL on NCB h from the name local to the name remote, with the
   timeouts rto and sto as for nb_call.  It completes with NB_OK when
   the called station's LISTEN has opened a session; nba_lsn then gives
   it.  Other final codes: NB_RC_NONAME when local is not usable in the
   local name table, NB_RC_NOANSWER when no station has the name remote,
   NB_RC_REJECTED when it has no LISTEN posted for the call,
   NB_RC_SESSFULL when the local session table is full, NB_RC_TIMEOUT
   when the provider gave up on the call, and NB_RC_CANCELLED.  A CALL
   that completes while it is being cancelled has opened its session
   all the same: when nba_poll then gives NB_OK, the caller holds the
   session nba_lsn names and must hang it up (nb_hangup). */
u8 nba_call(nba *s, u16 h, const char *local, const char *remote,
            u8 rto, u8 sto);

/* Post a RECEIVE of the next message of session lsn into the buffer.
   It completes with NB_OK and a whole message, or with NB_RC_INCOMPLETE
   when the message is longer than len: the buffer then holds its first
   len bytes and the next RECEIVE on the session gets the rest; nba_len
   gives the bytes received.  Other final codes: NB_RC_SESSCLOSED once
   the session has been hung up by either end and every message sent
   before the hang-up has been received, NB_RC_ABORTED, NB_RC_CANCELLED,
   NB_RC_TIMEOUT when the session's receive timeout passed (the session
   goes on). */
u8 nba_recv(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len);

/* Post a SEND of the buffer as one message on session lsn.  It
   completes with NB_OK once the other end has received the whole
   message; other final codes: NB_RC_SESSCLOSED, NB_RC_ABORTED,
   NB_RC_CANCELLED, and NB_RC_TIMEOUT when the session's send timeout
   passed, after which the provider has ended the session. */
u8 nba_send(nba *s, u16 h, u8 lsn, cmem_blk *b, u16 off, u16 len);

/* NB_RC_PENDING while the command on h is in progress, else its final
   return code, the same on every call until h is posted again; NB_OK
   for an NCB never posted, NB_RC_ILLCMD for h out of range. */
u8 nba_poll(nba *s, u16 h);

/* What the last completed command on h left, for h in range: nba_lsn
   the session a LISTEN or CALL opened with NB_OK, or the lsn a RECEIVE
   or SEND was posted with; nba_len the bytes a RECEIVE received with
   NB_OK or NB_RC_INCOMPLETE, or the len of a SEND that gave NB_OK;
   nba_callname writes into out (NB_NAMELEN bytes) the caller's name of
   a LISTEN that gave NB_OK, its first NB_NAMELEN - 1 bytes without
   trailing spaces and NULs, then a NUL.  In every other case, an NCB in
   progress included, they give 0 and "". */
u8 nba_lsn(nba *s, u16 h);
u16 nba_len(nba *s, u16 h);
void nba_callname(nba *s, u16 h, char *out);

/* Cancel the command in progress on h (wait-form CANCEL).  Returns
   NB_OK when h is free afterwards: it was not in progress, or the
   command has completed, nba_poll then giving its final code
   (NB_RC_CANCELLED, or the code it completed with while being
   cancelled); NB_RC_PENDING when the provider still owns it (cancel or
   poll again later); NB_RC_ILLCMD for h out of range.  Cancelling a
   SEND makes the provider abort its session: the other end's commands
   on it complete with NB_RC_ABORTED. */
u8 nba_cancel(nba *s, u16 h);
#endif

/* ---------------------------------------------------------------------
 * Serial ports, CUART.C, and CUARTSV.C for the calls a client never
 * makes (uart_rxcount, uart_rxflush, uart_modem and uart_getinfo);
 * under Windows CCOMM.C.
 *
 * Only a file that defines COMPAT_UART before it includes this header
 * sees this section, which spares Watcom C 6.5's memory as
 * COMPAT_NETBIOS does.
 *
 * A port is COM1 or COM2, numbered 1..UART_MAXPORT.  It exists when the
 * BIOS data area holds an I/O base for it (the word at 0040:0000 +
 * 2 x (port - 1), 0 when the BIOS found none) and a chip answers there;
 * COM1 interrupts on IRQ 4, COM2 on IRQ 3.  uart_open tells the chip by
 * its scratch register, which an 8250 lacks, and the FIFO bits of its
 * IIR; it sets 8 data bits, no parity and 1 stop bit at the baud rate,
 * raises DTR, RTS and OUT2, installs the port's interrupt handler, and
 * only then enables the chip's interrupts and unmasks its IRQ at the PIC.
 * On a 16550A it turns the FIFOs on, receive trigger 1 byte, unless
 * UART_F_NOFIFO.  The handler is machine code that uart_open copies
 * into a far block of the port beside its two rings: it moves each
 * byte received into the receive ring and the bytes of the transmit
 * ring to the chip.  A ring of n bytes holds at most n - 1.  The
 * program moves bytes between the rings and its own far blocks with
 * uart_read and uart_write; no call waits.
 *
 * Request lines hold passwords, so no ring keeps a byte once it has
 * gone: the handler zeroes each transmit-ring byte as it loads it into
 * the chip, uart_read each receive-ring byte it moves out, and
 * uart_rxflush and uart_close every byte they drop.
 *
 * Holding the receiver.  A byte received while the receive ring is full
 * is not taken from the chip: the handler turns the chip's receive
 * interrupt off (IER bit 0), so that the bytes wait in the chip, 16 of
 * them in a 16550A's FIFO, and a line that waits for room (an
 * emulator's) loses nothing; uart_read and uart_rxflush let the handler
 * take them again once the ring holds UART_RXLOW or fewer.  A chip that
 * receives more meanwhile overruns.
 *
 * Errors, counted from uart_open on, each count wrapping at 65536: a
 * byte received with a framing or parity error is stored as it came and
 * counted; an overrun, a byte the chip lost because the one before it
 * was not read in time, is counted each time the chip reports one.
 * uart_info's count of bytes lost to a full receive ring stays 0 here,
 * the receiver being held instead; a test's simulated port counts the
 * ones it is told to lose.  The handler counts an error before it
 * stores the byte that shows it, the damaged byte or the first one
 * after a byte lost, so a caller that reads bytes and then uart_rxerrs
 * finds the count risen when any of them may be wrong.  The line has
 * no checksum: these counts are all that tells a byte lost or changed.
 *
 * Flow control.  With UART_F_RTSCTS the handler drops RTS once the
 * receive ring holds UART_RXHIGH bytes, uart_read and uart_rxflush raise
 * it again once it holds UART_RXLOW or fewer, and the handler starts a
 * byte only while CTS is up (a byte already in the chip's FIFO still
 * goes).  It also takes the chip's modem-status interrupts (IER bit 3)
 * and starts the transmitter again when CTS rises; uart_write starts it
 * too.  Without the flag RTS stays up and CTS is not looked at.
 *
 * Loopback.  UART_F_LOOP turns the chip's loopback on (MCR bit 4):
 * every byte sent comes back into the port's own receive ring, RTS
 * comes back as CTS, and nothing reaches the line; for self-tests.
 * Loopback also holds OUT2 inactive, and the boards gate the IRQ line
 * with OUT2, so the chip raises no interrupt: the port is served by
 * polling instead, uart_read, uart_write, uart_rxcount and
 * uart_txidle each doing the handler's work against LSR, with
 * interrupts disabled, before they answer.
 *
 * The first uart_open of a run registers an exit function (atexit)
 * that closes every port still open, so that no end of the program
 * through the C run time leaves a vector pointing into freed memory.
 * DOS can still end the program without it, leaving the handler
 * installed: by Ctrl-C or Ctrl-Break while a port is open and
 * dos_break_off is not in force, and by Abort answered at DOS's own
 * critical-error prompt while dos_crit_install is not in force.
 *
 * Under Windows the comm driver owns the chip and its interrupt, and
 * CCOMM.C serves through it the calls a client makes: uart_open,
 * uart_close, uart_read, uart_write, uart_txidle and uart_rxerrs.  The
 * driver's queues, of the sizes CCOMM.C gives, stand for the rings;
 * uart_open takes of the flags only UART_F_RTSCTS, under which the
 * driver drops RTS while less than a quarter of its receive queue is
 * free and sends only while CTS is up, and gives UART_EARG also for a
 * setting the driver refuses, such as a rate it lacks; uart_txidle
 * answers 1 once the transmit queue is empty, the chip perhaps still
 * sending.  A byte the chip overruns, or one that finds the receive
 * queue full, is lost.  The driver latches that, and a framing error,
 * until GetCommError reads and clears the latch; CCOMM.C reads it in
 * its calls and in uart_rxerrs, and its count is of the reads that
 * found any of the three, so uart_rxerrs counts what the driver
 * latched before it.  The driver zeroes no byte as it goes, so the
 * first uart_read or uart_txidle that finds the transmit queue empty
 * after a write overwrites the whole queue with zeros, sending none of
 * them, and uart_close does so before it closes the port: the bytes
 * stay in the queue only until they are sent.  The receive queue holds
 * only responses, which carry no password.  The paragraphs above on
 * the chip, its handler and the exit function are of DOS.
 *
 * The host build has no ports: uart_open returns UART_ENOPORT and every
 * other call acts as on a port not open.  Tests link TEST/FAKEUART.C in
 * place of CUART.C and CUARTSV.C.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_UART
#define UART_MAXPORT    2       /* COM1 and COM2                        */
#define UART_RXSZ       1024u   /* bytes of a receive ring              */
#define UART_TXSZ       1024u   /* bytes of a transmit ring             */
#define UART_RXHIGH     768u    /* RTS/CTS: fill that drops RTS         */
#define UART_RXLOW      256u    /* fill that raises it again, and lets a
                                   held receiver go                     */

/* Baud rates: UART_MINBAUD doubled up to UART_MAXBAUD, that is 1200,
   2400, 4800, 9600, 19200 and 38400; UART_DEFBAUD is the default of
   the server's and the client's settings, of INVTEST and of COMTEST. */
#define UART_MINBAUD    1200u
#define UART_MAXBAUD    38400u
#define UART_DEFBAUD    9600u

/* 1 when the u16 b is one of those rates, else 0.  b is evaluated more
   than once: pass a plain variable. */
#define UART_BAUDOK(b) \
    ((b) % UART_MINBAUD == 0 && (b) / UART_MINBAUD != 0 && \
     (b) <= UART_MAXBAUD && \
     (((b) / UART_MINBAUD) & ((b) / UART_MINBAUD - 1)) == 0)

/* uart_open flags. */
#define UART_F_RTSCTS   0x01    /* hardware flow control, as above      */
#define UART_F_LOOP     0x02    /* the chip's loopback, as above        */
#define UART_F_NOFIFO   0x04    /* leave a 16550A's FIFOs off           */

/* Results of uart_open. */
#define UART_OK         0
#define UART_EARG       1       /* port, baud or flags out of range     */
#define UART_ENOPORT    2       /* no base in the BIOS data area, or no
                                   chip answers there                   */
#define UART_EBUSY      3       /* the port is open already             */
#define UART_ENOMEM     4       /* no memory for the port's far block   */

/* Chips, from uart_getinfo. */
#define UART_T_8250     1       /* 8250: no scratch register            */
#define UART_T_16450    2       /* 16450 or 8250A: no FIFO              */
#define UART_T_16550    3       /* 16550: its FIFOs do not work         */
#define UART_T_16550A   4       /* 16550A: 16-byte FIFOs                */

typedef struct {
    u16 base;                   /* I/O base                             */
    u8  irq;
    u8  chip;                   /* UART_T_                              */
    u16 overrun;                /* overruns the chip reported           */
    u16 framing;                /* bytes received with a framing error  */
    u16 parity;                 /* bytes received with a parity error   */
    u16 lost;                   /* bytes dropped, receive ring full     */
} uart_info;

/* Open port (1..UART_MAXPORT) at baud with flags (UART_F_), its rings
   empty and its counts 0.  Returns UART_OK, or a UART_E code with
   nothing changed. */
int uart_open(u8 port, u16 baud, u8 flags);

/* Close port: the chip's interrupts off (IER 0) and its FIFOs off when
   uart_open turned them on, MCR and the PIC's mask bit of its IRQ as
   uart_open found them, and the vector as uart_open found it if it
   still points at the port's handler.  Bytes still in the transmit
   ring are dropped: a caller that needs them sent waits for
   uart_txidle first.  Both rings are zeroed; the far block is then
   freed, or, when the vector has been taken over, kept for the rest of
   the run with its handler intact, which the program that took the
   vector may still chain to and which, the chip's interrupts being
   off, only passes the interrupt on.  Does nothing for a port not open
   or out of range, so a second call is harmless. */
void uart_close(u8 port);

/* Move up to n bytes from port's receive ring into b at off, as
   cmem_put would (off + n within b), zeroing them in the ring;
   uart_write copies up to n bytes from b at off into the transmit
   ring, as many as it has room for, and starts the transmitter.  Each
   returns the bytes moved, 0 for a port not open. */
u16 uart_read(u8 port, cmem_blk *b, u16 off, u16 n);
u16 uart_write(u8 port, cmem_blk *b, u16 off, u16 n);

/* The bytes waiting in port's receive ring; 0 for a port not open. */
u16 uart_rxcount(u8 port);

/* 1 when port's transmit ring is empty and the chip has sent its last
   bit (LSR bit 6), or the port is not open; else 0. */
int uart_txidle(u8 port);

/* Drop every byte received and not yet read, the chip's FIFO included,
   zeroing them in the ring and raising RTS again under UART_F_RTSCTS.
   Does nothing for a port not open. */
void uart_rxflush(u8 port);

/* Fill i for port: its base, IRQ and chip, and its counts since
   uart_open; every member 0 for a port not open. */
void uart_getinfo(u8 port, uart_info *i);

/* The receive errors of port since uart_open, wrapping at 65536: the
   sum of uart_info's overrun, framing, parity and lost counts (under
   Windows CCOMM.C's own count, as above); 0 for a port not open.  A
   change between two reads says that a byte read after the first may
   have been lost or changed. */
u16 uart_rxerrs(u8 port);

/* Modem lines, from uart_modem: the bits of MSR that say a line is up.
   DSR and DCD are up while the far end's DTR is, on a full null-modem
   cable; a cable of three wires leaves them down. */
#define UART_M_CTS      0x10
#define UART_M_DSR      0x20
#define UART_M_DCD      0x80

/* The UART_M_ bits of port's modem lines that are up now, read from
   MSR, the handler then served as uart_write does, since the read
   clears the modem-status interrupt it may have been waiting for; 0
   for a port not open. */
u8 uart_modem(u8 port);
#endif

/* ---------------------------------------------------------------------
 * TCP connections of a Windows program, CWSOCK.C, through Windows
 * Sockets 1.1.
 *
 * Only a file that defines COMPAT_SOCKET before it includes this header
 * sees this section, as COMPAT_UART does.  A DOS program has no
 * implementation (INVTCP has WATTCP's own stack); T_LINKT defines the
 * calls itself, against a scripted peer.
 *
 * Winsock is WINSOCK.DLL, which a TCP/IP stack installs and Windows 3.0
 * lacks.  sk_open loads it only once OpenFile has found it on Windows'
 * search path, as Windows 3.0 shows a box for a library that is
 * missing, and then finds each entry by name, so that a program linking
 * CWSOCK.C starts and runs where there is none; it stays loaded while
 * the connection is open, a program having at most one at a time.  A
 * connection's socket never blocks: Winsock posts its events
 * (WSAAsyncSelect) to a hidden window of the connection's, made in the
 * instance dos_inst, whose procedure notes them, and every wait idles
 * (dos_idle) until the event it needs comes or its seconds run out, so
 * the program's dos_yield must dispatch the messages waiting.
 *
 * Request lines hold passwords.  sk_send hands Winsock the caller's
 * bytes and keeps no copy; Winsock copies them into buffers of its own,
 * which it keeps until the peer has them and need not clear, beyond the
 * program's reach.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_SOCKET
/* Results of sk_open and sk_send. */
#define SK_OK           0
#define SK_ENOSTACK     1       /* no Winsock: WINSOCK.DLL not found, not
                                   loaded or lacking an entry, or
                                   WSAStartup refused                   */
#define SK_ENOMEM       2       /* no socket, or no window for it       */
#define SK_EREFUSED     3       /* refused: nothing listens on the port */
#define SK_ECONN        4       /* not connected, or not in time        */
#define SK_ECANCEL      5       /* Esc ended the wait                   */
#define SK_ELOST        6       /* sk_send: the connection is lost      */

typedef struct sk sk;           /* an open connection (opaque)          */

/* Connect to port of the IPv4 address ip[0].ip[1].ip[2].ip[3] in *out,
   waiting at most secs seconds; Esc pressed while a window of the
   program is active ends the wait.  Returns SK_OK, or an SK_E code with
   *out NULL and nothing left open.  *err receives the number of the
   Winsock error behind a failure (WSAECONNREFUSED, WSAETIMEDOUT ...),
   0 when no Winsock call failed. */
int sk_open(sk **out, const u8 *ip, u16 port, u16 secs, u16 *err);

/* Send the n bytes at buf, waiting at most secs seconds each time
   Winsock has no room for more.  Returns SK_OK, or SK_ELOST when the
   connection failed or a wait ran out. */
int sk_send(sk *k, const char *buf, u16 n, u16 secs);

/* Receive 1 to room bytes into buf, waiting at most secs seconds for
   them.  Returns their count; 0 once the peer has closed the connection
   and every byte it sent has been received, or when the connection
   failed or the wait ran out. */
u16 sk_recv(sk *k, char *buf, u16 room, u16 secs);

/* Close the connection, the bytes sent still going out before the peer
   sees the close, and free k; k may be NULL.  It waits for nothing:
   no event of the socket's is needed. */
void sk_close(sk *k);
#endif

/* ---------------------------------------------------------------------
 * Far pointers, for SRC/COMPAT/ only.
 *
 * A module under SRC/COMPAT/ that needs far pointers, the interrupt flag
 * or interrupt vectors defines COMPAT_INTERNAL before it includes this
 * header.  It then gets FARP(type), a far pointer to type on the DOS
 * compilers and an ordinary pointer on the host; CFP(type, seg, off),
 * the far pointer to seg:off (DOS builds only); cmem_ptr, the far
 * address of a block's first byte (under Windows of a block of
 * cmem_alloc_perm or cmem_alloc_dos only, NULL for any other, which
 * moves between calls); cmem_heapsz, the program's near heap; and, each
 * described below, cmem_alloc_dos, the far reads and writes of the cf_
 * calls, FARD and FARX for far static data, the IRQ_ macros for the
 * interrupt flag, BDA_SEG and the vector calls.  Portable code never
 * defines COMPAT_INTERNAL, so none of this is visible to it; make lint
 * rejects COMPAT_INTERNAL, FARP, CFP, cmem_ptr, cmem_heapsz, FARD,
 * FARX, the IRQ_ macros, compat_flags and BDA_SEG outside SRC/COMPAT/,
 * so none of these names can be reached there.
 * --------------------------------------------------------------------- */
#ifdef COMPAT_INTERNAL
#ifdef COMPAT_OW
#define FARP(type)      type __far *
#endif
#ifdef COMPAT_WC6
#define FARP(type)      type far *
#endif
#ifdef COMPAT_TC
#define FARP(type)      type far *
#endif
#ifdef COMPAT_HOST
#define FARP(type)      type *
#else
/* On the 16-bit compilers a 32-bit integer converts to the far pointer
   whose segment is its high word and whose offset is its low word. */
#define CFP(type, seg, off) \
    ((FARP(type))(((u32)(u16)(seg) << 16) | (u32)(u16)(off)))
#endif

FARP(u8) cmem_ptr(cmem_blk *b);

/* cmem_alloc of a block of fixed memory in the first megabyte
   (GlobalDosAlloc), Windows only, whose address never changes, so that
   a real-mode driver may write into it while the program yields
   (nba_alloc).  It costs conventional memory, which the DOS sessions of
   386 enhanced mode share. */
#ifdef COMPAT_WIN16
cmem_blk *cmem_alloc_dos(u16 size);
#endif

/* The program's near heap in bytes, CMEM_NEARHEAP or CMEM_TCPHEAP, as
   its near-heap module defines it; cmem_init grows the Open Watcom near
   heap by it. */
extern const u16 cmem_heapsz;

/* Find-first (function 4Eh, DOS 2.0) of pattern with the attributes
   attr into dta, DOS_DTASIZE bytes, made the DTA for the call, the
   program's own set back after it (CDOS.C): 0, CF_ENOENT when nothing
   matches, or another CF_E code.  The match's attribute byte is at
   DOS_DTAATTR, its name, NUL-ended, at DOS_DTANAME.  DOS builds only;
   CLABEL.C reads a volume label with it.  Seen with COMPAT_FIND only,
   which spares Watcom C 6.5's memory. */
#ifdef COMPAT_FIND
#ifndef COMPAT_HOST
#define DOS_DTASIZE     43      /* bytes of a find DTA                  */
#define DOS_DTAATTR     21      /* offset of the found attribute        */
#define DOS_DTANAME     30      /* offset of the found name             */
int dos_find_entry(const char *pattern, u8 attr, u8 *dta);
#endif
#endif

/* cf_read into the far address p (CDOS.C); cf_read and cf_readblk call
   it.  cf_writefar is cf_write from p; cf_write and cf_writeblk call
   it. */
int cf_readfar(int h, FARP(u8) p, u16 n, u16 *got);
int cf_writefar(int h, FARP(const u8) p, u16 n);

/* Static data outside DGROUP: FARD qualifies the definition of such an
   object, FARX its extern declaration in the modules that read it.
   Open Watcom and Watcom C 6.5 define far data directly (the object
   lands in a far segment of its own, or in the code segment under
   -zc).  Turbo C 2.01 accepts far only on declarations, never on a
   definition, so for it FARD is empty and the defining module is
   compiled with -zRCSCH_FAR -zSCSCH_GRP -zTCODE, which places that
   module's whole data segment in a CODE-class segment outside DGROUP.
   The class is CODE, not FAR_DATA, because TLINK places FAR_DATA after
   _STACK, in the room the near heap and the stack grow into in the
   small and medium models; CODE-class segments come before DGROUP.  A
   module defining FARD data therefore holds no other data, and nothing
   reads FARD data except through FARX declarations.  The host build
   keeps both empty, and so does the Windows build, which defines no far
   data: a far data segment would be fixed in real mode, where the code
   holds its address, and would keep a second instance of the program
   from starting, and a code segment, which real-mode Windows discards
   and loads again elsewhere, cannot be read through a far pointer kept
   in the code.  It keeps the schema data in a resource instead
   (CSCHDAT.C). */
#ifdef COMPAT_OW
#ifdef COMPAT_WIN16
#define FARD
#define FARX
#else
#define FARD            __far
#define FARX            __far
#endif
#endif
#ifdef COMPAT_WC6
#define FARD            far
#define FARX            far
#endif
#ifdef COMPAT_TC
#define FARD
#define FARX            far
#endif
#ifdef COMPAT_HOST
#define FARD
#define FARX
#endif

/* The interrupt flag, DOS builds only.  IRQ_OFF() disables maskable
   interrupts (CLI) and IRQ_ON() enables them (STI); IRQ_SAVE() is the
   flags register as a u16, whose IRQ_IF bit is set while interrupts are
   enabled, so that code that runs with interrupts disabled can leave
   the flag as it found it:
       fl = IRQ_SAVE();
       IRQ_OFF();
       ...
       if (fl & IRQ_IF)
           IRQ_ON();
   A module that uses them includes <dos.h>, which declares the
   compilers' own disable and enable calls.  Turbo C reads the flags
   through its _FLAGS pseudo-variable, the Watcom compilers through the
   aux pragma compat_flags (PUSHF, POP AX: 9C 58), whose code is expanded
   in place, so no symbol is emitted or referenced.  BDA_SEG is the
   segment of the BIOS data area, whose fields are written 0040:offset
   in these comments.  Windows owns the interrupts and the BIOS data
   area, so the Windows build has none of these. */
#ifndef COMPAT_HOST
#ifndef COMPAT_WIN16
#define IRQ_IF          0x0200u /* interrupt flag in the flags register */
#define BDA_SEG         0x0040
#ifdef COMPAT_OW
#define IRQ_OFF()       _disable()
#define IRQ_ON()        _enable()
#endif
#ifdef COMPAT_WC6
#define IRQ_OFF()       _disable()
#define IRQ_ON()        _enable()
#endif
#ifdef COMPAT_TC
#define IRQ_OFF()       disable()
#define IRQ_ON()        enable()
#define IRQ_SAVE()      ((u16)_FLAGS)
#else
u16 compat_flags(void);
#pragma aux compat_flags = 0x9C 0x58 value [ax] modify [ax];
#define IRQ_SAVE()      compat_flags()
#endif

/* The vector of interrupt n: dos_getvect reads it (function 35h) into
   *seg:*off, dos_setvect points it at seg:off (function 25h).  An
   interrupt handler that a program installs is removed only while the
   vector still points at it: a resident program loaded later may have
   taken it over and chain to it.  DOS builds only. */
void dos_getvect(u8 n, u16 *seg, u16 *off);
void dos_setvect(u8 n, u16 seg, u16 off);
#endif
#endif
#endif

/* ---------------------------------------------------------------------
 * Text screen, CVIDEO.C.
 *
 * The program draws on a shadow of the 80 x 25 text screen, a far block
 * of VID_ROWS * VID_COLS cells, each a character byte (CP437) followed
 * by an attribute byte, as in the adapters' own memory.  vid_put and
 * vid_fill change only the shadow; a row is marked only when a cell
 * really changes, together with the span of columns from the first to
 * the last changed cell, and vid_flush copies just those spans to the
 * screen.  Rows and columns count from 0; text that would pass the
 * right edge is cut, and a row at or beyond VID_ROWS is ignored.
 *
 * vid_open finds the adapter from the BIOS data area: the video mode
 * byte (0040:0049) and the CRT controller port (0040:0063, 3B4h for a
 * monochrome adapter, 3D4h for colour), with INT 10h function 12h
 * (BL = 10h, EGA information) telling an EGA or later adapter from a
 * CGA, since an older BIOS leaves BL unchanged.  Mode 7 is the
 * monochrome text mode at segment B000h (MDA, Hercules, or an EGA or
 * later adapter on a monochrome monitor); modes 2 and 3 are the 80 x 25
 * colour-adapter text modes at B800h.  On an EGA or later adapter, and
 * only there (older BIOSes leave the byte undefined), the row count is
 * read from 0040:0084 (rows - 1); the column count is the word at
 * 0040:004A on every adapter.  In any other mode, or with any other row
 * or column count, vid_open sets mode 3, or mode 7 on a monochrome
 * adapter, through INT 10h function 00h, and vid_close sets the old mode
 * back; a 43- or 50-line or a wider text screen therefore comes back as
 * 80 x 25, as function 00h always gives.
 * The display page used is page 0: when the active page (0040:0062) is
 * another one, vid_open selects page 0 (function 05h) and vid_close
 * selects the old page again.
 *
 * vid_open marks every cell changed, so the first flush writes the whole
 * screen.  Writes go straight to video memory, except with VID_F_BIOS,
 * which draws through INT 10h only (function 02h to place the cursor,
 * 09h to write each cell, 08h to read cells back) for adapters whose
 * memory cannot be written directly; runs of cells with the same
 * character and attribute are written with one function 09h call (CX
 * the count).
 * Since function 09h writes at the cursor, a flush in this mode ends by
 * putting the cursor back where vid_cursor last placed it, or parking it
 * again when it is hidden.
 *
 * The CGA shows "snow" when its memory is written while the screen is
 * being drawn.  With snow avoidance on (see vid_snow), every word
 * written to or read from CGA memory waits for a horizontal retrace.
 * The status port is the CRT controller base (0040:0063) + 6, 3DAh on
 * a CGA.  For each word the loop first loads it (LODSW), then waits for
 * bit 0 of the status port to read 0 (the display is drawing), only then
 * disables interrupts, waits for the bit to turn 1 and stores the word
 * with a single STOSW straight after that edge, and enables interrupts
 * again.  Before each word it also tests bit 3 (vertical retrace): while
 * that is set, up to VID_VBURST words are stored without waiting.  A
 * read of CGA memory (vid_verify, saving the DOS screen, restoring it
 * from page 1) waits the same way first and loads the word with a
 * single LODSW straight after the edge, since it is the access to the
 * adapter's memory that must fall in the retrace.  The loops are
 * machine code (#pragma aux under Watcom, __emit__ under Turbo C, as
 * CDOS.C does), since compiled C is too slow to hit the retrace.
 * Each wait gives up after a fixed number of polls, far more than a
 * working CGA ever needs; the remaining words are then copied without
 * waiting and snow avoidance stays off for the rest of the run, so an
 * adapter whose status port never changes cannot hang the machine with
 * interrupts disabled.  Only the CGA needs it: the setting is ignored
 * on every other adapter and in VID_F_BIOS mode.
 *
 * vid_open saves the screen it finds (the cells of page 0, the cursor
 * position and shape from 0040:0050 and 0040:0060, the mode) and
 * vid_close puts it back, so the DOS prompt reappears as it was; when
 * vid_open had to change the mode, vid_close only sets the old mode
 * back, which clears the screen.  The cells are kept off screen in
 * video page 1 (B800:1000) when a colour mode's memory is written
 * directly and page 1 is not the page vid_close selects again;
 * otherwise (mode 7, whose MDA and Hercules have one page; VID_F_BIOS;
 * or page 1 active at start) in a far block of VID_CELLS * 2 bytes.
 * Nothing else in the program writes page 1.
 *
 * The host build draws on the shadow alone: there is no screen, vid_open
 * reports a CGA in mode 3 (an MDA in mode 7 with VID_F_MDA, so tests
 * can produce what a monochrome machine shows), vid_flush does nothing
 * beyond counting and clearing the row marks, and vid_verify always
 * matches.  Tests read the screen back with vid_getrow and
 * vid_cursor_get.
 * --------------------------------------------------------------------- */
#define VID_ROWS        25
#define VID_COLS        80
#define VID_CELLS       (VID_ROWS * VID_COLS)
#define VID_ROWBYTES    (2 * VID_COLS)  /* bytes of one row of cells    */

/* Adapters, from vid_adapter. */
#define VID_A_MDA       1       /* MDA or Hercules, mode 7 at B000h     */
#define VID_A_CGA       2       /* CGA, modes 2 and 3 at B800h          */
#define VID_A_EGA       3       /* EGA or later (a VGA reports this)    */

/* vid_open flags. */
#define VID_F_BIOS      0x01    /* draw through INT 10h only            */
#define VID_F_SNOW      0x02    /* avoid CGA snow from the start        */
#define VID_F_MDA       0x04    /* host build only: act as an MDA in
                                   mode 7; ignored by the DOS builds    */

/* Snow avoidance tests bit 3 before every word; while the vertical
   retrace lasts, at most this many words in a row are stored without
   waiting.  The cap keeps the burst inside the retrace even when the bit
   was seen just before it ended, on the slowest machine as on a fast
   one, where more words would fit. */
#define VID_VBURST      32

/* Cursor shapes. */
#define VID_CUR_HIDE    0       /* no cursor                            */
#define VID_CUR_LINE    1       /* underline: scan lines 6-7 (colour
                                   modes) or 11-12 (mode 7)             */
#define VID_CUR_BLOCK   2       /* full cell: 0-7 or 0-12               */

/* Save the screen, select the adapter's 80 x 25 text mode, allocate the
   shadow (filled with spaces of attribute 07h) and hide the cursor.
   Returns 0, or CF_ENOMEM when the shadow or the saved screen cannot be
   allocated; nothing is changed then. */
int vid_open(u8 flags);

/* Restore the screen vid_open saved, its cursor and mode, and free the
   shadow.  Does nothing when the screen is not open. */
void vid_close(void);

/* VID_A_ code of the adapter found by vid_open. */
u8 vid_adapter(void);

/* 1 when the screen shows no colour: mode 7, or mode 2 (the colour
   adapter's black-and-white 80 x 25 mode, chosen for composite and
   monochrome monitors); else 0.  The caller then keeps to the
   attributes 07h, 0Fh, 70h, 01h and 09h, the ones a monochrome display
   shows distinctly. */
int vid_mono(void);

/* Turn CGA snow avoidance on (1) or off (0) from the next write on.
   After a wait has timed out (see above) it stays off for the rest of
   the run, whatever is asked here. */
void vid_snow(int on);

/* 1 while the screen is open and snow avoidance applies: it is on, the
   adapter is a CGA, VID_F_BIOS is not set, and no wait has timed out;
   else 0.  For tests.  The host build, which has no waits, reports
   whether it would apply to its virtual CGA. */
int vid_snow_active(void);

/* Write n bytes of s (every byte is a character, none is special) at
   row, col with attribute attr. */
void vid_put(u8 row, u8 col, const char *s, u16 n, u8 attr);

/* Write n copies of the character ch with attribute attr. */
void vid_fill(u8 row, u8 col, u16 n, u8 ch, u8 attr);

/* Copy one row of the shadow, VID_ROWBYTES bytes (character, attribute,
   character, ...), into cells. */
void vid_getrow(u8 row, u8 *cells);

/* Save the h x w cells at row, col (cut at the screen's edges) into a
   new far block; NULL when memory has run out.  vid_restore writes them
   back to the shadow and frees the block; b may be NULL.  Pop-up boxes
   use these to put back what they covered. */
cmem_blk *vid_save(u8 row, u8 col, u8 h, u8 w);
void vid_restore(cmem_blk *b);

/* Copy the cells changed since the last flush to the screen. */
void vid_flush(void);

/* The number of cells the last vid_flush wrote (on the host, would have
   written): the sum of its rows' changed spans.  For tests, which use it
   to show that writes leaving a cell as it was mark nothing. */
u16 vid_flushed(void);

/* Mark every cell changed, so the next flush rewrites the whole screen;
   for when something else may have written to it (DOS echoing ^C after
   Ctrl-Break). */
void vid_invalidate(void);

/* Place the cursor and set its shape (VID_CUR_) through INT 10h
   functions 02h and 01h.  The hidden cursor is the shape 2000h, and is
   also parked below the last row for adapters that ignore that shape. */
void vid_cursor(u8 row, u8 col, u8 shape);

/* The position and shape last set by vid_cursor. */
void vid_cursor_get(u8 *row, u8 *col, u8 *shape);

/* Compare the shadow with what the screen holds (video memory, or INT
   10h function 08h with VID_F_BIOS), after a flush.  Returns 0 when
   every cell matches; else 1 with the first differing cell in *row,
   *col.  Used by the screen snapshots of the tests to prove that the
   writes reached the adapter. */
int vid_verify(u8 *row, u8 *col);

/* ---------------------------------------------------------------------
 * Keyboard, CKEY.C.
 *
 * Keys are read through INT 16h functions 00h (read) and 01h (peek),
 * which every PC BIOS has; the enhanced-keyboard functions 10h and 11h
 * are not used, so F11 and F12 are not seen.  key_read polls with
 * function 01h and issues INT 28h (the DOS idle interrupt, which lets
 * resident programs such as a print spooler run) until a key is
 * waiting, then reads it with function 00h.  A key with an ASCII code
 * is that code, 01h..FFh (CP437; Ctrl+letter gives 01h..1Ah, and Enter,
 * Tab, Backspace and Esc their control codes).  A key without one (the
 * BIOS returns AL = 00h, or E0h with a nonzero scan code from the grey
 * keys of an enhanced keyboard) is KEY_EXT plus its scan code; the
 * keys the programs use are named below.  On Ctrl-Break the BIOS
 * keyboard handler calls INT 1Bh (where DOS notes the break) and puts
 * the code 0000h in the keyboard buffer, which key_read returns as
 * KEY_BREAK; DOS may echo ^C on the screen when it next checks for a
 * break.
 *
 * A key source (key_set_source) replaces the keyboard, for tests: while
 * one is set, key_read, key_peek and key_ready ask the source, and the
 * keyboard is not looked at, so a test decides which of its keys are
 * typed ahead (seen by key_peek before they are read) and which come
 * only when the program asks for a key.
 * The host build has no keyboard: without a source, key_read returns
 * KEY_EOS at once.
 *
 * Once key_mouse (CMOUSE.H) has found a mouse driver, key_read and
 * key_peek report the mouse among the keys, the keyboard first: a press
 * of the right button is KEY_ESC, one of the left button a click
 * (KEY_CLICK, KEY_DCLICK of CMOUSE.H).  A key source replaces the mouse
 * as it replaces the keyboard.
 * --------------------------------------------------------------------- */
#define KEY_NONE        0x0000  /* no key (never returned by key_read)  */
#define KEY_CTRL(c)     ((c) - '@') /* Ctrl and the letter c, 'A'..'Z'  */
#define KEY_BS          0x0008
#define KEY_TAB         0x0009
#define KEY_ENTER       0x000D
#define KEY_ESC         0x001B
#define KEY_EXT         0x0100  /* KEY_EXT + scan code                  */
#define KEY_BREAK       0x0100  /* Ctrl-Break (scan code 0)             */
#define KEY_STAB        0x010F  /* Shift-Tab                            */
#define KEY_F1          0x013B
#define KEY_F2          0x013C
#define KEY_F3          0x013D
#define KEY_F4          0x013E
#define KEY_F5          0x013F
#define KEY_F6          0x0140
#define KEY_F7          0x0141
#define KEY_F8          0x0142
#define KEY_F9          0x0143
#define KEY_F10         0x0144
#define KEY_HOME        0x0147
#define KEY_UP          0x0148
#define KEY_PGUP        0x0149
#define KEY_LEFT        0x014B
#define KEY_RIGHT       0x014D
#define KEY_END         0x014F
#define KEY_DOWN        0x0150
#define KEY_PGDN        0x0151
#define KEY_INS         0x0152
#define KEY_DEL         0x0153
#define KEY_CLEFT       0x0173  /* Ctrl-Left                            */
#define KEY_CRIGHT      0x0174  /* Ctrl-Right                           */
#define KEY_CEND        0x0175  /* Ctrl-End                             */
#define KEY_CPGDN       0x0176  /* Ctrl-PgDn                            */
#define KEY_CHOME       0x0177  /* Ctrl-Home                            */
#define KEY_CPGUP       0x0184  /* Ctrl-PgUp                            */
#define KEY_EOS         0xFFFF  /* the key source has no more keys      */

/* Wait for a key and return it (never KEY_NONE). */
u16 key_read(void);

/* 1 when a key is waiting to be read, else 0; nothing is read. */
int key_ready(void);

/* The key waiting to be read (INT 16h function 01h), else the mouse's
   press, without reading it, or KEY_NONE when there is none.  With a
   key source set, the source's waiting key. */
u16 key_peek(void);

/* The letter 'A'..'Z' of an Alt+letter key (KEY_EXT + the letter's
   scan code), else 0. */
char key_alt_letter(u16 key);

/* Install a key source, or with fn NULL go back to the keyboard.  fn is
   called with arg and peek 0 each time key_read is, and returns the key
   to give; once it has returned KEY_EOS it must go on returning it.
   key_peek and key_ready call it with peek 1: it then returns the key
   the next key_read will give if that key is typed ahead, without
   taking it, or KEY_NONE when no key is waiting (also after the end). */
typedef u16 (*key_src_fn)(void *arg, int peek);
void key_set_source(key_src_fn fn, void *arg);

/* For tests: put the BIOS key word code (scan code in the high byte,
   ASCII code in the low byte, as INT 16h function 00h returns it) at the
   end of the BIOS keyboard buffer (head 0040:001A, tail 0040:001C), as
   the keyboard interrupt does, so that the INT 16h paths of key_read,
   key_ready and key_peek can be run without anybody typing.  Interrupts
   are disabled while the buffer is changed, and the interrupt flag is
   then left as it was found.  Returns 0, or 1 when the buffer is full
   or its head or tail is not a slot of the ring.  The host build has no
   buffer and returns 1. */
int key_stuff(u16 code);

/* ---------------------------------------------------------------------
 * Timer, CTIMER.C.
 *
 * The BIOS tick counter at 0040:006C counts timer interrupts, about 18.2
 * per second (1193182 Hz / 65536), and is set back to 0 at midnight after
 * TMR_DAY ticks.  It is read with interrupts disabled, as its two words
 * change together, and the interrupt flag is then left as it was found.
 * It is read from memory rather than with INT 1Ah function 00h, because
 * that function clears the midnight flag (0040:0070) that DOS reads,
 * through the same function, to move its date on; reading the counter
 * directly never disturbs the DOS date.  The host build counts virtual
 * ticks instead: each call of tmr_ticks advances the count by one, and
 * tmr_delay returns at once, so tests never wait and always see the same
 * values.  Under Windows the count is made from GetTickCount, the
 * milliseconds since Windows started, at TMR_10SEC ticks per ten
 * seconds, so it goes back to 0 every TMR_DAY ticks from the start of
 * Windows rather than at midnight, and once more, early, when
 * GetTickCount wraps after 49.7 days.
 * --------------------------------------------------------------------- */
#define TMR_DAY         0x1800B0UL  /* ticks from midnight to midnight  */
#define TMR_SEC         18      /* ticks in about one second            */
/* Ticks in ten seconds, 1193182 / 65536 * 10 rounded down: a span of t
   ticks is t / TMR_10SEC * 10 + t % TMR_10SEC * 10 / TMR_10SEC whole
   seconds, with no overflow for any u32. */
#define TMR_10SEC       182

/* The tick count, 0 .. TMR_DAY - 1. */
u32 tmr_ticks(void);

/* Ticks since start (a value of tmr_ticks), correct across one
   midnight: a count below start is taken to have passed midnight. */
u32 tmr_since(u32 start);

/* Wait at least n ticks, calling dos_idle meanwhile. */
void tmr_delay(u16 n);

/* The count of the timer chip's channel 0, which runs down with its
   1193182 Hz clock and starts again every tick (in the square-wave mode
   the BIOS sets, by two at a time, twice a tick): latched (port 43h)
   and read low byte first (port 40h) with interrupts disabled, so that
   no other reader comes between.  A source of entropy, not of time: a
   network event bears no relation to the chip's phase.  The host build
   returns the value last given to tmr_fine_set, 0 at start.  Not under
   Windows, which owns the chip.  Both are in CFINE.C, which only the
   programs that make login tickets or TCP sequence numbers link; a file
   that uses them defines COMPAT_FINE first (Watcom C 6.5 runs out of
   memory on some tests if every file reads them). */
#ifdef COMPAT_FINE
u16 tmr_fine(void);
#ifdef COMPAT_HOST
void tmr_fine_set(u16 v);
#endif
#endif

/* A loop's tick count (CRUN.C), which starts at 0 and never goes back,
   also across midnight, as long as tmr_clock_now runs at least once a
   day.  At each midnight it sees pass, tmr_clock_now reads the date
   through DOS (dos_today): the BIOS marks a midnight with a flag, not a
   count, and DOS moves its date on only as it reads its clock, so a
   loop left idle over two midnights would otherwise leave the DOS date
   a day behind. */
typedef struct {
    u32 now;                    /* the count                            */
    u32 tick;                   /* tmr_ticks when now was last updated  */
} tmr_clock;

/* Start c at 0 from the tick count now. */
void tmr_clock_start(tmr_clock *c);

/* c's count now, the span since its last update (tmr_since) added. */
u32 tmr_clock_now(tmr_clock *c);

/* ---------------------------------------------------------------------
 * Packet driver, CPKT.C.
 *
 * CPKT.C implements the packet-driver calls of WATTCP's kernel, which
 * WATTCP.H declares, on a class 1 (DIX Ethernet) driver of the Packet
 * Driver Specification 1.09.  The kernel opens the driver inside
 * tcp_init, which cannot report a failure; pkt_error tells its caller
 * afterwards.  DOS builds only.
 * --------------------------------------------------------------------- */
#define PKT_OK          0
#define PKT_ENONE       1       /* no packet driver on INT 60h..80h     */
#define PKT_EREFUSED    2       /* a driver was found but none opened:
                                   each was not class 1 or refused a
                                   handle or its address                */

/* The result of the last opening of the driver: PKT_OK when it
   opened, else why it failed; PKT_ENONE before the first. */
int pkt_error(void);

#endif
