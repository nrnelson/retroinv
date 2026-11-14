# RETROINV — Contributor Rules

The rules every contributor keeps: the DOS floor and the language, the style of the C sources,
headers, tests, scripts and documents, and how a change is committed, so that every module
reads as if one person wrote it. When code and this document disagree, the code is changed, or
this document is corrected in the same commit.

## Hard rules

- **Floor: MS-DOS 3.1, 8086/8088 real mode, 640 KB, 80×25 text.** Only INT 21h calls of DOS
  3.1 or earlier: handles and find-first from 2.0, record locks (5Ch) and sharing-mode opens
  from 3.0, the machine name (5E00h) and the redirector (5Fxxh) from 3.1. Never 68h (commit,
  DOS 3.3): a handle is flushed by duplicating it (45h) and closing the duplicate. No 286 or
  later instructions.
- **Strict ANSI C89** in every file but those of `SRC/COMPAT/`, the WATTCP users
  (`WATTCP_USERS` of `SRCS.MK`) and the Windows sources (`WIN_NOANSI` of `MAKEFILE`), which
  write the memory model and calling convention only with the macros of `windows.h` and
  `WCALLBACK` of `SRC/COMPAT/CWIN.H`, which declares every function Windows calls. Portable
  code (`SRC/DBF/`, `SRC/ENGINE/`, `SRC/PROTO/`, `TEST/`) compiles with `gcc -std=c89
  -pedantic`, `wcc -za`, Turbo C 2.01 `-A` and Watcom C 6.5. Every strict file (lint's
  `LINT_FILES`, which takes in `SRC/COMPAT/`'s portable modules) has none of:
  - a declaration after a statement, or an unused parameter or static function;
  - a string literal over 255 characters, `long double`, a packed struct, or `#pragma`;
  - a memory-model or calling-convention keyword, port I/O, an interrupt call or direct video
    access;
  - a compiler-specific header (`<dos.h>`, `<conio.h>`, ...) or DOS library call (`_dos_*`,
    `_bios_*`, `fsync`, `_commit`, `fopen` mode `c`), or a helper `COMPAT.H` keeps for
    `SRC/COMPAT/` (far pointers, interrupt flags, BIOS data);
  - a `(void)x;` statement (Watcom C 6.5 warns "meaningless statement").
- No C source or header has a `//` comment, an `#elif` (Watcom C 6.5 mis-handles it), or a
  `const` pointer to a struct incomplete where it is written (Watcom C 6.5 refuses it, E091).
- Turbo C builds every C source with `-Z`, which takes a variable read again from the register
  that still holds it, even across a write through a pointer and even when the variable is
  `volatile`; it loads again only after a call or at a jump target, and always loads data reached
  through a pointer. So no function changes a variable through a pointer between two reads of it
  with no call between them, and data an interrupt handler changes is read through a pointer or
  after a call, never as a named `volatile` scalar read twice in straight-line code.
- Compiler-specific code lives only in `SRC/COMPAT/`, behind `COMPAT_OW`, `COMPAT_WC6`,
  `COMPAT_TC` and `COMPAT_HOST` of `COMPAT.H`, its Windows variants behind `COMPAT_WIN16`, and
  what Watcom C 9.5 lacks of Open Watcom behind `COMPAT_WC95`. In the programs' sources outside
  it, these macros select a program, not a compiler: a client header (`CFG.H`, `FMODEL.H`,
  `PAGER.H`) gives the Windows client, and the host build for the tests of its code, what the
  DOS clients leave out to keep their bytes, under `#if defined(COMPAT_WIN16) ||
  defined(COMPAT_HOST)`.
  WATTCP's headers are included only by the files of `WATTCP_USERS`, which the build compiles
  with WATTCP's packing; `SRC/DBF/DBFINT.H` only by `SRC/DBF/` and the unit tests,
  `SRC/CLIENT/SCRSHR.H` only by the screens `SRC/CLIENT/SCR*.C` and the unit tests,
  `SRC/CLIENT/PAGERINT.H` only by the pager `PAGER.C`, its survey `PAGERSV.C` and the unit
  tests.
- The engine's texts are numbered by their row in `SRC/ENGINE/TXTAB.H`, so rows are only ever
  appended; `TEST/TXNUM.TXT` holds the names in order.
- Every file and directory that DOS sees has an upper-case 8.3 name.
- `SRC/CLIENT/` reaches the engine only through the protocol (`LINK.C` and its LOCAL backend
  `LINKLOC.C`), never by calling an engine function. Every command's user level is checked in
  `SRC/PROTO/DISPATCH.C`, never in the screens, so every transport enforces the same
  permissions. A password is never stored or logged in plain text.
- `INV.EXE`, its network-only build (`THIN` of `SRCS.MK`), `INVSERV.EXE`, the programs on
  WATTCP (`INVTCP.EXE`, `TCPTEST.EXE`) and the Windows programs (`WIN_EXES`) are medium model,
  the other tools small (`<NAME>_MODEL` of `SRCS.MK`). No buffer exceeds `CMEM_MAXBLK` (32 KB),
  the largest far block `cmem_alloc` gives.

`make lint` (`TEST/lint.py`) rejects the forbidden constructs, the header and `TXTAB.H` rules,
and the strict compiler modes catch the rest of the C rules; the DOS test runs refuse a name
that is not 8.3 (`TEST/shlib.sh`, `TEST/rundos.sh`). Which INT 21h calls the code makes, the
instruction set and the `-Z` rule are held by review.

## Files

- A module's name starts with its layer letter or word: `C*.C` in `SRC/COMPAT/` (but `WTSHIM.H`,
  WATTCP's per-compiler macros), `ENG*.H` beside `ENGINE.H`, `DP*.C` protocol handlers,
  `SCR*.C` client screens, `T_*.C` unit tests.
- A file starts with a header comment: its name, a dash and one sentence saying what the module
  is; then what a reader needs before the code (formats, invariants, callers, why it is shaped
  so).

  ```c
  /*
   * DPLOC.C - the LOC commands (GET ADD SET DEL LIST CONTENTS NOTES) and
   * the commands of the code tables, CAT and LTYPE (LIST ADD SET DEL).
   *
   * A location argument is a token (ENGLOCS.H): all digits a LOC_ID, ...
   */
  ```
- A header holds the header comment, guard and includes, then sections, each opened by a
  banner and holding its constants, types and prototypes. The banner also divides long `.C`
  files; a `/*` line followed by ` *` lines is used only for file headers and banners.

  ```c
  /* ---------------------------------------------------------------------
   * Tables.
   * --------------------------------------------------------------------- */
  ```
- The guard is `NAME_H` for `NAME.H`, closed by a bare `#endif` on the last line. A header
  included several times (an X-macro row file such as `SCHTAB.H`) has no guard and says so.
- Includes: standard headers, then, without a blank line, project headers from the lowest layer
  up (`COMPAT.H`, `DBF.H`, `ENGINE.H`, `ENG*.H`, `PROTO.H`, `DPCMD.H`), the module's own last.
  Every file names `COMPAT.H`; in `SRC/COMPAT/` it comes first, as its `COMPAT_*` macros choose
  the compiler headers after it.
- Include only what the file uses. A portable file using `NULL` includes `<stddef.h>` or
  `<stdio.h>`, which alone define it under Turbo C 2.01 `-A`. `<stdio.h>` is for programs that
  print (tools, `INV.C`, tests); library modules format numbers themselves.
- No tabs in C sources and headers (`MAKEFILE` recipes need them), no trailing blanks, one
  newline at the end, never two blank lines in a row.

## Naming

- Everything is lower-case `snake_case`, except macros and constants, which are upper case.
- A prefix belongs to an interface, a header, and every module implementing it shares it:
  `eng_` across `SRC/ENGINE/`, `link_` in `LINK.C` and its backends `LINK*.C`,
  `cf_` in `CDOS.C`, `CMEM.C` and `CZERO.C`. Names read prefix, object, verb: `eng_loc_get`,
  `dbf_put_str`. A sub-interface may take a prefix of its own (`nba_` for the no-wait NetBIOS
  commands, `cuart_` for what `CUART.C` shares with `CUARTSV.C`). `CSCHEMA.C` is the one COMPAT
  module with `eng_` names: it implements the schema calls of `ENGINE.H`. `CPKT.C` and
  `CWATT.C` define names WATTCP calls (`pkt_`, `set_timeout`, `intel`, ...), and test fakes and
  stubs define the names they replace (the simulated NetBIOS, UART and TCP/IP stack, `__wrap_`
  functions).
- Macros and constants use the upper-case prefix (`DBF_MAXFIELDS`, `ENG_T_ITEMS`). INVTCP's
  exit statuses are `TCP_EXIT_`, a prefix of constants only, as WATTCP owns the `tcp_`
  function names. A constant
  private to one `.C` file may use a short local prefix (`PK_W`, `OC_CAP`), still upper case
  and defined near the top. `ENG_IDLEN`, `ENG_LOCLEN` and `ENG_CATLEN` are used by name, never
  through a local alias. A file of dense table rows may define short names for the flags or
  levels its rows repeat (`SCHEMA.C`, `DISPATCH.C`), so each row stays on one line.
- Return codes are `MOD_OK` (zero) and `MOD_Exxx`: negative in `SRC/DBF/`, the protocol's error
  numbers in the engine (`ENG_E*`). The cf_ layer has no `CF_OK`; its success is compared with
  `0`, every other module's code with its own `MOD_OK`.
- Static functions have no prefix: short verb or noun phrases (`write_slot`, `seq_find`).
  Statics working on one local type may share a short prefix (`pool_acquire`, `pool_release`).
- File-scope variables carry the module prefix (`vs_mode`, `tui_inhelp`, `ks_st`); never bare
  (`fready`) nor `g_`. A string constant shared inside a file and holding one protocol name is a
  `static const char` array with a role prefix: `k_` for a field, key or option name, `w_` for a
  command word; `k_` also names a single shared text (`k_closedata`). Word lists and tables are
  named for what they hold (`orders`, `verbs`, `table_exts`, the handlers' `list_keys`), as is a
  module's own data (`ks_letters`). Test programs name their data freely.
- Types are lower-case typedefs without `_t`: `dbf_field`, `eng_scan`. An opaque handle is
  declared `typedef struct dbf dbf;` in the header and defined only in its `.C` file.
- Fixed-width types (`u8 u16 u32 i16 i32`) for every value whose width matters: file fields,
  counts, offsets, record numbers, IDs. `int` for return codes, flags and small loop indexes;
  `size_t` only at calls into the C library; `char` for text. Portable code has no plain
  `long`, `short` or `unsigned`, and assumes no more than 16 bits of an `int`. File and wire
  values are read and written byte by byte (`get_le16`, `put_le32` of `COMPAT.H`), never through
  a struct laid over the bytes.
- Client message numbers are `M<x>_NAME`, one letter `<x>` per module, the number inside the
  module's `MSG_R_` range (`MSG.H`); `TEST/msgchk.py` holds the prefixes.

## Layout

- Indent 4 spaces. Lines at most 79 columns, comments included: `make lint` refuses a longer
  one in any C source or header under `SRC/` and `TEST/`. The `.MSG`, `.HLP` and `.HTX`
  sources are data, bounded instead by `msgchk.py` and `hlpchk.py`: a built `INV.MSG` or
  `INVSERV.MSG` line (`nnn text`) by `MSG_MAXRAW`, its unescaped text by `MSG_MAXLEN`, a help
  line by `TUI_HELPW`, in `INVWIN.HTX` by `WH_HELPW`.
- A function definition has its return type and name on one line and its opening brace alone
  on the next. Every other brace is on its statement's line; `} else {` is one line.

  ```c
  static int read_at(int h, u32 pos, void *buf, u16 n)
  {
      int rc;
      u16 got;

      rc = cf_seek(h, pos);
      if (rc == 0)
          rc = cf_read(h, buf, n, &got);
      ...
  }
  ```
- A single-statement body has no braces; once any branch of an `if`/`else` needs them, all
  have them.
- Long argument lists and conditions wrap after a comma or a binary operator (`&&`, `||`, `+`
  end the line), aligned with the first argument, or the first operand inside the parenthesis.
- Declarations open the block, one per line, without initializers, then a blank line, then the
  statements. `static const` tables are initialized where declared, inside the function when
  only it uses them, as is any other `static` local, whose initializer runs once; `make lint`
  refuses every other initializer in a block.
- `switch`: `case` labels at the `switch`'s indentation, bodies one level in, always a
  `default:`, labels sharing a body stacked, no fall-through from a non-empty body, no braces
  on cases.
- One space after `if for while switch return`, none between a function name and its `(`, none
  inside parentheses, none after a cast: `(u16)(off + n)`. `sizeof x` for objects,
  `sizeof(type)` for types. `return x;`, never `return (x);`.
- `#define` values align at column 25; trailing comments on defines and struct members close at
  column 74, so they read as a table.
- One statement per line; a dense table of operations (a hash's rounds) may take two per line
  to show its symmetry.

## Comments

- Comments say what the code cannot: the contract, the reason, the invariant, the dBASE or
  DOS fact behind a choice; never what the next line plainly does.
- An exported function is documented once, above its prototype: what it does or returns, its
  arguments where the names do not say it, its errors and what it leaves behind on failure.
  The definition repeats none of it.
- A static function gets a comment unless its name and signature say it all (`is_sep`
  of `TOKEN.C`). A predicate's starts "1 if" or "1 when"; a value-returning function's starts
  with that value ("Index of the named field (case-insensitive), or -1.").
- Multi-line comments continue at three spaces, with no leading stars:

  ```c
  /* Read exactly n bytes at pos; a short read is DBF_EFORMAT.  The
     caller keeps the first error. */
  ```
- Whole English sentences, two spaces after a full stop, identifiers as in the code; reflowed
  to the line limit when changed.
- Self-sufficient: no references to plans, milestones, tasks, conversations or other
  documents' section numbers; no history ("changed from"); no commented-out code. A value held
  in a constant is named by the constant, not repeated.

## Errors, cleanup and memory

- A function that can fail returns an `int` code, values through out-params; predicates return
  1 or 0; a function that cannot fail returns `void` or its value.
- Steps chain on one `rc`, which keeps the first error; an early `return` is used when nothing
  needs undoing. There is no `goto`.

  ```c
  rc = first_arg(c, k_loc, "", &loc);
  if (rc == ENG_OK)
      rc = eng_loc_get(e, loc, &recno);
  if (rc != ENG_OK)
      return rc;
  ```
- Cleanup that must run whatever happened takes a second code, and the first error stands:
  `rc2 = cf_close(h); if (rc == 0) rc = rc2;`. A result deliberately ignored is discarded by
  calling the function as a statement, not by assigning it.
- Lower-layer codes are translated once, at the layer boundary, by one mapping function
  (`dbf_map_cf` of `DBF.H` for cf_ codes, the engine's for `DBF_E` codes).
- The engine records why a call failed with `eng_fail(e, code, field, tx)` and returns its
  result. Its texts are numbered (`ENG_TX_` of `ENGTX.H`, rows in `TXTAB.H`), out of DGROUP; a
  text used in several places is one number.
- Codes are grouped by the hundreds digit of the protocol error number: request syntax, data
  and rules, storage, session and login, server. Outside the storage class a failure leaves the
  data unchanged, but for the exceptions `ENGINE.H` lists. `DOC/PROTOCOL.md` lists the numbers;
  code uses the names.
- Near memory comes from `malloc` and goes back with `free`. Buffers that could strain the
  near-data budget are far blocks from `cmem_alloc`, at most `CMEM_MAXBLK`, reached only through
  `cmem_get`/`cmem_put`; portable code never holds a far pointer. Whoever allocates frees, in
  the matching close function. `free` takes a possibly NULL pointer untested, as C89 and the
  three DOS libraries allow.
- Allocations are sized to need when it is known (a record length, a message file's total) and
  made once, at open; a request never allocates in its inner loop.

## API shape

- Parameters: the handle (`eng *e`, `dbf *t`, `proto_call *c`), inputs, out-params. A
  constructor takes its result first: `int dbf_open(dbf **out, const char *path, int mode)`.
  The cf_ calls put the out handle last, as the DOS calls do. `form_new`, `form_open`,
  `lv_open`, `cmem_alloc` and `vid_save` instead return the object or NULL: they have many
  callers and one failure, no memory (for the two form calls also more than `FORM_MAXFIELDS`
  fields or one wider than `FORM_MAXVAL`, faults of the form's table).
- A text out-param is a caller buffer with its size (`char *out, u16 outsz`) or a buffer whose
  size is a named constant given in the comment.
- `const` on every pointer to data the function does not change, but not on pointers to opaque
  structs (Hard rules).
- A `static const` struct used as a template is copied with `memcpy`, never assigned: Watcom C
  6.5 refuses to assign from a const object (E030).
- Everything not called from another file is `static`. A header declares only what other files
  call; per-module headers (`ENGITEM.H`, `DPCMD.H`) keep large interfaces from files that do
  not need them.
- Function-like macros only where a function cannot do: byte serialization (`get_le16`),
  X-macro rows, compile-time checks, compiler shims in `SRC/COMPAT/`, the character tests of
  `COMPAT.H` (`TXT_ISDIGIT`), the key code of a Ctrl letter (`KEY_CTRL` of `COMPAT.H`, a
  constant for `case` labels), and addressing inside a cached page on a hot path (`NDX_ENT` of
  `DBFINT.H`). Count arrays with `sizeof a / sizeof a[0]` in place, or one `NITEMS`-style macro
  per file at most.
- Compile-time checks use a double declaration, never a negative array size, which Turbo C
  2.01 and Watcom C 6.5 accept silently: `extern char x[1]; extern char x[(cond) ? 1 : 2];`

## Speed and size on an 8088

- The target fetches about one byte per four clocks and has 64 KB of near data shared by
  everything; size and speed are mostly one problem.
- Do not scan twice: compute a length, field index or lookup once and keep it in a local or the
  handle. A field known when the code is written is named by `ENG_FIELD`, not looked up.
- Do not read the disk twice: one read per record per request, through the existing caches
  (the shared NDX page pool, the table's record buffer, the message pages). Every scan stops at
  its bound, so every request does bounded work.
- Size every buffer from a named constant that says what it holds (`char key[ENG_MAXKEYLEN]`):
  no literal sizes, no buffer "big enough" for safety. Large buffers live in far blocks or on
  the stack of the one function that needs them, never in static near data.
- Prefer tables to repeated code: command tables (`DISPATCH.C`), schema rows (`SCHTAB.H`), form
  fields, menus, key names. Branches that differ only in data become a `static const` table
  and one loop.
- A string literal used twice in a file becomes one `static const char` array, so one copy is
  kept whatever the compiler's string pooling. UI text lives in the message files, never in
  DGROUP.
- No `printf` family in library code, nor in `INV` and `INVSERV`: it costs kilobytes per
  program, and the few lines they write on standard error take `fputs` and the project's
  number-to-text helpers. `INVTCP`, the one program that keeps it, formats every line of its
  event log with it.
- Character tests and upper-casing use `TXT_ISDIGIT`, `TXT_ISALPHA` and `TXT_UPPER` of
  `COMPAT.H`, never `<ctype.h>`, whose tables Turbo C 2.01 links into DGROUP. The macros
  evaluate their argument more than once: pass a plain variable.
- `int` for loop counters and temporaries (word arithmetic is fastest); `u8` for flags and
  small values in structs and static data, where the byte saved repeats.

## User-visible text

- Every text a user reads is a message of a `.MSG` source or a section of a help file
  (`INV.HLP`, `INVWIN.HTX`), so it can be reworded or translated without touching the code.
  Exceptions: what a program writes on standard error before its message file is loaded or when
  it cannot be (the usage and start-up errors of `INV.C` and `INVSERV.C`, those `SRVRUN.C`
  writes before `INVSERV.MSG` is loaded, the message loader's and `link_open_local`'s reasons,
  the reasons `INVCOM.C` gives for a serial port that did not open); `INVTCP.C`'s usage,
  start-up errors and stop question, as it has no message file; and the output of test programs
  and offline tools.
  `make lint` rejects, in `SRC/CLIENT/`, `SRC/SERVER/` and `SRC/TCP/` outside `lint.py`'s
  `PROSE_FILES`, a string literal holding a lower-case letter once escapes and `printf`
  conversions are taken out.
- The client shows its own text for a protocol error, chosen by the code and, where it
  matters, the field named (`rqe_class`, `rqe_text`); the server's reason text is for its
  logs and `INV /CMD`. Exceptions are 102, 202 and each refused import line's reason
  (`MX_REFLINE`, codes 102, 201 and 202), shown as the server gives them, as code and field
  alone cannot tell them apart.
- A message drawn in a slot of fixed width (a key hint, a title, a form label, a column header,
  a tab title, a menu entry, a text in a fixed box or field) states its width in its `.MSG`
  source: a comment line `# W=n`, optionally followed by words naming the slot, bounds every
  message line below it up to the next comment or blank line, n being the most cells the text
  may take with its arguments at their widest; a key hint's level digits do not count. When an
  argument is data that can be wider (a directory, a serial number, a server's reason), the
  code cuts the text at n and the comment says so. Messages on the hint row (`tui_msg`) or
  wrapped in a message box need none.
- Stored codes, field names, protocol words and keys, choice codes and configuration keys are
  never localized: they are upper-case ASCII constants in the code, data files and protocol,
  whatever the language of the messages.

## Shared code

- A helper with one job in three or more files, or in two files of one layer, is one shared
  function in the lowest layer every caller may use, never crossing the rule that screens reach
  the engine only through the protocol. Local code is right for a truly local job: a different
  contract, a hot loop the general helper would slow, a two-line expression clearer inline.
- A shared helper has one name and one signature; copies with different integer types or
  argument orders are merged.
- Homes: `SRC/COMPAT/CTEXT.C` for the portable text helpers every layer uses (`txt_`);
  `SRC/DBF/DBFINT.H` for what only `SRC/DBF/` shares; the engine's `TABLES.C`, `EDIT.C` and
  `CURSOR.C`, exported in `ENGINE.H` and its per-module headers; `DISPATCH.C` and `DPCMD.H` for
  the protocol handlers; `TUI.C`, `LISTV.C`, `PAGER.C`, `FMODEL.C`, `FORM.C`, `MEMO.C`,
  `MEMOIO.C`, `MSG.C` and `RQ.C` for the screens, `SCRSHR.H` for what the screens share with one
  another; `TEST/T_FILES.C` (scratch directories, wipes, the fault recorder), `TEST/TENGH.C`
  (engine scaffolding), `TEST/TEWIPE.C` (removing the engine's files) and `TEST/TSCRIPT.C` (the
  end-to-end tests' INV request script and golden path) for the tests; `TEST/shlib.sh` for the
  shell scripts.

## Tests, scripts and build files

- A unit test is `TEST/T_NAME.C` (8.3: at most six letters after `T_`), built by gcc for
  `make test` and by the DOS compilers for the DOS runs. Its header comment gives `usage:`,
  what it checks as a dashed list, and its exit status. A DOS-only test making DOS calls of its
  own, which portable code may not, is `SRC/COMPAT/T_NAME.C`, built only for the DOS runs.
- Tests include `TESTFW.H`, check with `TF_CHECK`, `TF_EQ` and `TF_STREQ`, call `tf_begin`
  first and end `main` with `return tf_done();`. The Watcom C 6.5 build defines `TF_TERSE` for
  every file, for its compiler memory; some large tests also define it before the include. A
  split test's second file defines `TF_FILE "<8.3 name>"` (`"T_ENGIN2"`) before the include,
  so its terse failure lines name it.
- Test functions are `static void test_xxx(void)`, called from `main` in order. Mutable
  file-scope test state, having no module prefix, is `g_xxx` (`g_dir`, `g_e`), lower case.
  `TESTFW.H`'s counters and comparisons may use plain `long`, as `printf` prints them.
- A test that writes files takes a scratch directory as its first argument and removes what it
  made at the start and at the end. A failed run may keep its files for the reader: `T_RPT` and
  `T_TUI` remove theirs at the end only when every check passed, and the screen tests keep a
  snapshot or output that differed from its golden. `T_TABLES` always leaves its reopened data
  directory, for a structural checker.
- Protocol tests are `TEST/SCRIPTS/*.SCR`: `> request` and `< expected response` lines, `#`
  comments. Screen tests are key scripts `TEST/KEYS/*.KEY` (`K` keys, `T` text, `S` snapshot,
  `M` mask, `#@` run options) with goldens in `TEST/GOLDEN/SCR/` (`.SNC` colour, `.SNM`
  monochrome, `.SNB` mono on CGA) and printer or file output in `TEST/GOLDEN/PRT/`. A golden is
  regenerated only by its test's update switch and reviewed as a diff.
- Shell scripts are POSIX `sh`: `#!/bin/sh`, a header `# name - what it does`, `# usage:`
  lines and the meaning of every option, environment variable and exit status, then `set -eu`.
  A script that only sets variables for others to source has no shebang and says who sources
  it.
- `SRCS.MK` holds only `NAME_VAR := words` lists with `\` continuations, `$(VAR)` references to
  lists above and `#` comments, as the batch generators parse it. In `MAKEFILE` settings and
  lists are upper case and `:=`; `?=` gives the defaults a user may set from the environment
  (`WATCOM`), `+=` collects a list across rules (`ALL_OBJS`), and `=` is kept for what must
  expand when used: text with automatic variables or later settings (`WDEPS`, `SUBMAKE`) and
  the one upper-case call function, `HOSTC`. Other call functions are lower case and `=`, named
  after their group (`dj`, `dj_log`, `pool_w`), beside the lower-case tables they read
  (`pool_var_DOSBOX`). There is one `.PHONY` line, recipes are indented by a tab, and a comment
  stands above every rule whose purpose its target does not make obvious.
- Python helpers run as `python3 -I` and take every path as an argument. The docstring's first
  line is `name - what it does`, its second paragraph the `usage: python3 -I PATH ...` lines;
  `main(argv)` takes `sys.argv[1:]`; a usage error writes `<script>: <reason>` and that
  paragraph (`__doc__.split('\n\n')[1]`) to standard error and exits 2; other messages start
  with the lower-case script name and a colon; strings use single quotes.

## Documents

- `DOC/*.md` describe the system as it is, in the present tense, for a reader with the code
  open. The title is `# RETROINV — Subject`; sections are `##`, subsections `###`.
- Lead with what the part is and does, then the rules a reader must keep, then the details.
  Module lists are tables (`File | Responsibility | Uses`).
- Each topic lives in exactly one document, the one its reader reads (`README.md` lists them),
  and each fact is stated once there and linked from elsewhere: no document repeats another, a
  small or overlapping document is merged into the one its reader already has, and `make lint`
  rejects a passage held twice, in one document or across two. A reference names the document,
  and the heading when it means part of it, never a number; `make lint` rejects a reference to
  a missing document or heading.
- Identifiers, file names, switches and commands are in backticks; a number that comes from a
  constant is given by its name.
- Paragraphs wrap at about 96 columns. A paragraph covers one subject; a long one becomes a
  list or a table.
- A document is self-sufficient, as a comment is (Comments). A change corrects, in the same
  commit, every comment, document and number it makes wrong.

## Commits

- A commit holds one change, small and focused. Its subject follows Conventional Commits,
  `type(scope)!: subject`, the scope and the `!` (a breaking change) optional: the type one of
  `feat fix docs style refactor perf test build ci chore revert`, the scope the module (`dbf`,
  `ndx`, `engine`, `proto`, `server`, `client`, `compat`, `tcp`, `tools`, `test`, `build`,
  `doc`), the subject imperative, starting with a lower-case letter or digit, without a full
  stop, at most 72 characters. A body says why when the reason is not
  obvious. `.githooks/commit-msg` checks the subject in a clone that has run `git config
  core.hooksPath .githooks`.
- Every commit passes the commit gate, `make gate` (`DOC/BUILD.md`).
- Runtime data (`DATA/`'s tables), build output (`BIN/`, `OBJ/`, `DIST/`), period software, disk
  images and logs are never committed; `.gitignore` holds them out.
