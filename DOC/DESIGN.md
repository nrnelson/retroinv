# RETROINV — Design

## Purpose

RETROINV tracks the inventory of a retro PC and hardware collection on the machines of that
collection: MS-DOS from 3.1 on an 8088, data in dBASE III files, several stations on a LAN,
and one line protocol that external systems use as well.

## Design decisions

| Topic | Decision | Reason |
|---|---|---|
| Floor | **MS-DOS 3.1**, 8086/8088 real mode, 640 KB, 80x25 text on MDA, CGA or EGA. `INV`, `INVSERV` and `INVTCP` refuse an older DOS with a message; every program uses only INT 21h calls that DOS 3.1 has (`DOC/STYLE.md`, Hard rules, lists them), and the modules a program links make them all in `SRC/COMPAT/CDOS.C`. No DOS program needs more than `DOS_MAXFREEKB` KB of free conventional memory (`MAKEFILE`); each one's own figure is measured and kept as low as its code allows (`DOC/BUILD.md`, Runtime requirements). | The oldest DOS with sharing-mode opens (3.0) and the network calls (3.1) a LAN program needs, on the processor of the first PC. A 640 KB PC with MS Network Client 3.0 loaded keeps about 497 KB free (`DOC/BUILD.md`, NetBIOS gate), so the ceiling leaves room for a mouse driver and other resident programs beside any of ours. |
| Windows client floor | **Windows 3.0 in standard or 386 enhanced mode**, so an 80286 or better, with an EGA 640x350 display or better. `INVWIN` and the other Windows programs (`WIN_EXES`) are marked for protected mode only, and Windows 3.0 refuses to start them in real mode. What Windows 3.0 lacks and the client needs (a tab strip, a tool bar, a status bar, a splitter, list headers that sort) `INVWIN` draws itself. Dates and numbers show in the Control Panel's International formats for display only: what is typed and what the protocol carries stay ISO dates and plain digits, and `INV` keeps ISO. A command on several selected rows sends one request per row, stopping at the first refusal, as the protocol has no command on many records. Its windows and dialogs are in `DOC/CLIENT.md` (Screens). | Open Watcom cannot give every far function the frame that real-mode Windows patches (Why INVWIN needs protected mode, below); `INV` remains the client of the 8088 floor and does everything `INVWIN` does. |
| Windows client panes | `INVWIN` shows beside a list a pane that follows its selected row (`DOC/CLIENT.md`, Screens). A pane loads from the message loop once the selection has rested on a row, one request at a time, and shows by default on a LOCAL, NET or TCP link, not on a serial line or a mailbox; the item preview is one painted window, not a dialog of controls. | The protocol answers one request at a time, so a pane loaded at every arrow key would hold the program, for seconds a row on a slow link, and a view dialog as a child window would take a control for each label and value out of USER's 64 KB heap. Panes are presentation alone: they add no command, and `INV` keeps its full-screen form. |
| Language and compilers | Strict **ANSI C89**; everything compiler-specific in `SRC/COMPAT/`. Built by **Open Watcom v2**, cross-compiling from Linux, and by **Turbo C 2.01** (1989) and **Watcom C 6.5** (1988), each running headless in DOSBox-X; gcc builds the host unit tests. | The period's own compilers build the release, and four compilers that must agree catch 16-bit and portability faults. |
| Database | Our own C library writing genuine **dBASE III** files: `.DBF` (header 03h, 83h with memo), `.DBT` memos in 512-byte blocks and `.NDX` B-tree indexes. `make dbcheck` has dBASE III Plus 1.1 open the tables and search our indexes. | The data stays readable by the period's own tools, and outlives the program. |
| Topology | The server **`INVSERV.EXE`** is the only process that opens the data files; the client **`INV.EXE`** reaches it over NetBIOS or a serial line. `INV /LOCAL` links the engine in-process for one user without a server; INV's network-only build (`THIN` of `SRCS.MK`) leaves the engine out, for a PC too small for INV. | One writer needs no record locks over the network, and the basic redirector an 8088 runs cannot take them. |
| Server loop | `INVSERV` serves every transport from one cooperative event loop with no NetBIOS post routine, every NetBIOS command a no-wait NCB it polls; the serial ports' handler is its one hardware interrupt handler. Every request does bounded work, and each session gets one request per pass (`DOC/SERVER.md`, The loop). | DOS runs one program at a time; bounded requests taken in turn keep any session from starving another. |
| Exclusive commands | IMPORT and ADMIN PACK, REINDEX, BACKUP and RESTORE need the data to themselves: they run in local mode, or on a server with no other session open, and are refused with 402 otherwise. | No long operation holds other clients up. |
| API and transports | **One ASCII line protocol** (`DOC/PROTOCOL.md`), which the full-screen client uses too. `INVSERV` serves it over NetBIOS sessions (INT 5Ch), RS-232 serial lines (COM1 and COM2; 8250, 16450 or 16550, with an interrupt handler of our own) and a mailbox directory of request files; `SESSIONS=0` serves the last two without NetBIOS. The bridge `INVTCP.EXE` relays each TCP connection on port `BR_PORT` over a NetBIOS session of its own to `INVSERV`. | Every client, the project's own included, goes through the same commands and the same level checks, so a transport adds no rules of its own. |
| TCP stack | **WATTCP** (Waterloo TCP, Erick Engelke), the release of 14 September 1994, behind the bridge's thin socket layer `SRC/TCP/TSOCK.H`. The packet-driver interface and the assembly helpers are our own code in `SRC/COMPAT/`. WATTCP is never in the repository: the build prepares it from its archive (WATTCP licence, below). | Plain C over a packet driver, which the three compilers build; the socket layer keeps the stack replaceable. |
| Keys | `INV`'s keys follow IBM's Common User Access for text screens: F1 help, F3 quit, F9 the keys, F10 or Alt and a letter the menu bar, Esc back; in a form Tab moves between the fields, Enter saves and F4 gives a field's pick list; what a screen does with its records is in the Record menu, the last of the bar, and the frequent commands have a Ctrl letter as well. `INVWIN` follows Windows 3.0 instead where the two differ, as in Alt+F4 to exit. | The standard of the period's DOS and Windows programs, so a user's habits carry over. |
| Saving notes | The notes of an item, a location or a service record (a memo of up to `ENG_MAXMEMO` bytes) are saved whole or not at all. Notes that fit one request go as one `SET`; longer ones go in parts that the server stages for the session, in a scratch file of the data directory named by the table and the session's slot (on a mailbox, whose sessions last one request, by its station and user instead), and the last part writes them in one edit of the record (`DOC/PROTOCOL.md`, Memos saved in parts). A save stopped by a refusal, a full disk, a lost line or an ended session leaves the notes as they were, and two sessions saving the same notes at once each write theirs whole, the last one winning, or one is refused when two mailbox stations share a scratch file; never a mix. `ADD=` appends a single line, and a client that sends `SET` then `ADD=`s changes the notes with each request. | Notes are the largest write a client makes, so the one most likely to meet a full disk or a broken line, and a half-written note loses text that no audit row keeps. A stage per session holds nothing in memory between requests and needs no lock; a mailbox's follows its station across its one-request sessions. |
| Features | Items, categories, nested storage locations (`Office > Shelf A > Bin 1`) of types the user extends, assemblies with their bill of materials, a service log, loans, reservations and the equipment in use with each system, with an overdue report, collection tags of the user's own and a works state (working, intermittent, broken or not tested) beside the cosmetic grade, search and filters, reports and labels on ESC/P printers and on Windows' default printer, CSV export and import, an audit log, backup and restore, PACK and REINDEX. | What a collection needs to say what it holds, where each piece is, what it is part of, who has it and what was done to it. |
| Users and logins | Modelled on dBASE III Plus PROTECT (1986): a user name of up to `ENG_USERLEN` characters, a password, a group and an access level. `USERS.DBF` keeps a salted one-way hash, never the password. A session must `LOGIN` before any other command. Levels: 1 read-only, 2 operator (items, loans and holds, service), 3 administrator (users, backup, pack, import, shutdown); each command's level is in `DOC/ENGINE.md` (Protocol layer). Audit rows name the user and the station. A new data directory gets the user `ADMIN`; the first password is the user name in lower case and must be changed at the first login. | The period's own model of multi-user access, checked in `DISPATCH.C` for every transport. PROTECT's own account file, `DBSYSTEM.DB`, is not reimplemented: it is an undocumented, scrambled Ashton-Tate format, and a scramble that can be undone keeps passwords recoverable, where RETROINV stores only a salted one-way hash; PROTECT also encrypts the tables themselves, which would end their byte compatibility with plain dBASE III Plus; and the checks belong in the server's dispatcher, for every link, where dBASE's own login never takes part. So RETROINV keeps PROTECT's model (upper-case names, groups, levels, the prompts, the pause after a refused login) and its own storage. |
| Memory model | Medium (`-mm`) for `INV.EXE`, its network-only build (`THIN` of `SRCS.MK`), `INVSERV.EXE` and the programs on WATTCP (`INVTCP.EXE`, `TCPTEST.EXE`), small for the other tools; large buffers are far blocks of `CMEM.C`. | Code may take several 64 KB segments while near data stays in one DGROUP. |
| Installation | The installer `INSTALL.EXE`, on every disk with its list `INSTALL.LST`, asks up to four numbered questions on standard input and output (what the PC is: alone, the server, a network client or a network client too small for `INV`; the Windows client; the directory; the sample data or an empty inventory), all before it copies, then only the files those answers need, asking for each further disk by its volume label and checking that disk's list; `INSTALL /U` removes what its list names. `SETUP.EXE`, a Windows 3.0 program installed with the Windows client, copies `INVWIN`'s files when started from a disk, makes its Program Manager group RETROINV anew at every run, and `SETUP /U` removes the group and the Windows client's files; it reads the same `INSTALL.LST` (role W). Neither writes outside the installation directory: `CONFIG.SYS`, `AUTOEXEC.BAT`, `WIN.INI` and `SYSTEM.INI` are left as they are, only Program Manager itself writing its group file and `PROGMAN.INI`, and the data is never replaced. `make dist` writes each set's `INSTALL.LST` from `REL/DISKS.TXT`, whose roles column says which kind of PC needs each file. `SETUP` talks to Program Manager in raw `WM_DDE_*` messages carrying its commands `[CreateGroup]`, `[AddItem]` and `[DeleteGroup]`. | Questions and answers in plain lines run on every display adapter and console, keep the installer small enough for a 360 KB disk beside its programs, and are tested with redirected input, as a full-screen program would not be. A PC's start-up files are its owner's: an edit there can break other software and outlives the program, so the installer says what `CONFIG.SYS` needs (`FILES=45`) instead. One list writes the disks and tells the installer what they hold, so the two cannot disagree. Windows 3.0 has no DDEML, the DDE library Windows 3.1 brought; Program Manager's DDE commands are the only way it offers to make a group, and raw messages need no import beyond Windows 3.0's (`TEST/W30EXP.TXT`). |
| Release disks | Every file of a release disk is compressed in the format of Microsoft's `COMPRESS` (SZDD: LZSS over a window of `LZ_WINSZ` bytes, `SRC/TOOLS/LZEXP.H`) under the name `COMPRESS -r` gives it (`INV.EX_`), but what must be read as it is: `INSTALL.EXE` and `INSTALL.LST`, `SETUP.EXE`, which Windows starts from the disk, and the texts a reader opens there, `README.TXT`, `LICENSE.TXT` and `WATTCP.TXT`. The sample data is the directory `DATA._`, its files compressed under their own names. `INSTALL` and `SETUP` expand while they copy, through one streaming reader, `LZEXP.C`, into the temporary file a plain copy uses; a file whose compressed name is missing is copied as it is, so an installation or the zip's folder serves as a source. No program runs from the disks: every PC installs first, and each set is the fewest disks of its size. `REL/szdd.py` compresses at `make dist`; the zip keeps its files as they are. Every disk carries one build of the DOS programs, Watcom C 6.5's; the Turbo C 2.01 build is compiled and tested by the gate, not shipped. The first 360 KB disk holds the two DOS clients. | Compression brings each set down to two disks or one. The format is the period's own: MS-DOS 5's `EXPAND` and Windows 3.1's LZEXPAND library read it, so a file can still be expanded by hand, while DOS 3.1 and Windows 3.0 have no reader, which `INSTALL` and `SETUP` therefore carry; plain LZSS expands fast on an 8088 in little code and one buffer far below 32 KB. A compressed program cannot run where it lies, and installing first frees every disk from keeping room to run on its own data, so the release serves no PC with two floppy drives and no hard disk. The zip is compressed already, and emulator users unpack it to run the programs. Watcom C 6.5's build needs less memory for `INV` and `INVSERV` and is the smaller. A network client installs from one 360 KB disk. |
| Test systems | DOSBox-X with its own DOS, and Compaq MS-DOS 3.10 (the floor) and MS-DOS 3.30 under QEMU. NetBIOS between two QEMU machines running MS-DOS 3.30 and MS Network Client 3.0 NetBEUI; the serial line between their COM1 ports and from the host to a COM2. The mailbox without NetBIOS on Compaq 3.10, on a Samba share reached by Client 3.0's TCP/IP, and on a Workgroup Add-On for MS-DOS server under MS-DOS 5.00. The bridge on a second card of a NetBIOS machine, and its stack alone on MS-DOS 3.30 and Compaq 3.10. `DOC/BUILD.md` describes each test. | Each transport is proven on the DOS and network software of the period, the floor included. |

## Module layout

```
MAKEFILE SRCS.MK  the build and its per-program source lists (DOC/BUILD.md)
GNUmakefile       host-only shim that includes MAKEFILE (GNU make ignores an upper-case name)
SRC\COMPAT\       compiler- and machine-specific code (DOS, Windows, BIOS, video, keyboard,
                  printer, NetBIOS, serial ports, packet driver, far memory and far data,
                  declared in COMPAT.H), the shared text helpers, the DOS-only tests T_DTA,
                  T_CLOCK, T_NHEAP, T_MSGLO, T_CWATT and T_UART, and the Windows-only test
                  T_WCOMP
SRC\DBF\          portable dBASE III files: tables, memos, indexes, key expressions, PACK
                  (DOC/ENGINE.md)
SRC\ENGINE\       portable inventory engine over the tables (DOC/ENGINE.md)
SRC\PROTO\        portable line protocol: tokenizer, sessions, dispatcher, command handlers
                  (DOC/ENGINE.md, DOC/PROTOCOL.md)
SRC\CLIENT\       INV.EXE, the full-screen client, and its network-only build, with their
                  message and help sources (DOC/CLIENT.md)
SRC\SERVER\       INVSERV.EXE, the server, with its message source (DOC/SERVER.md)
SRC\TCP\          INVTCP.EXE, the TCP bridge, its socket layer over WATTCP and WATTCP's
                  preparation (DOC/SERVER.md)
SRC\TOOLS\        the installer INSTALL, the offline tools DBPACK and INVDUMP, the serial port
                  test COMTEST, and the test programs NBTEST (NetBIOS), INVTEST (scripted
                  NetBIOS, serial and mailbox sessions) and TCPTEST (the TCP stack)
                  (DOC/BUILD.md, DOC/INSTALL.md)
SRC\WIN\          INVWIN.EXE, the Windows client, with its resources and help text, and its
                  installer SETUP.EXE (SETUP*.C) (DOC/INSTALL.md)
TC\               the Turbo C 2.01 build in headless DOSBox-X (DOC/BUILD.md)
WC65\             the Watcom C 6.5 build in headless DOSBox-X (DOC/BUILD.md)
WC95\             the Watcom C 9.5 check build of INVWIN in headless DOSBox-X, and its install
                  (DOC/BUILD.md)
TEST\             unit tests, protocol and key scripts, goldens, the dBASE III Plus rig
                  (DBASE\), the QEMU harness (INTEG\), lint and memory checks
                  (DOC/BUILD.md)
REL\              the release: the disks' contents, from which INSTALL's lists are
                  made, the sample data's requests, the install text, the licence notice and
                  settings samples, and the scripts that write and check the floppy images
                  and the 1.44 MB disk's zip in DIST\
                  (DOC/BUILD.md, DOC/INSTALL.md)
DATA\             the default data directory: *.DBF *.DBT *.NDX
DOC\              DESIGN.md ENGINE.md PROTOCOL.md CLIENT.md SERVER.md BUILD.md INSTALL.md
                  SECURITY.md STYLE.md
```

Portable code has no packed structs: every on-disk structure is serialized field by field with
little-endian helpers, so gcc, Watcom and Turbo C agree byte for byte. The one exception is
WATTCP's. Outside `SRC/COMPAT/` only `SRC/TCP/TSWAT.C` includes WATTCP's headers, inside it only
`CPKT.C`, `CWATT.C` and the test `T_CWATT.C`; every file that does, WATTCP's own included, is
built with WATTCP's byte packing and signed `char` (`-zp1 -j` under Open Watcom, `/j` under
Watcom C 6.5, which always packs to one byte), or the struct layouts differ and DGROUP is
corrupted silently.

Three routines that run at interrupt time are kept in C as machine code. The serial ports'
handler in `CUART.C` and the packet driver's receive upcall in `CPKT.C` are checked byte for
byte by `TEST/ASMCHK.PY` against their nasm sources `CUARTISR.ASM` and `CPKTUP.ASM`; the
critical-error handler of `CDOS.C`, a few instructions, carries its listing in the comment
above its bytes.

## Data model

The tables, the indexes and the rules over them are in `DOC/ENGINE.md`.

- Record IDs come from `SEQ.DBF`, issued only by the single writer (the server, or the client
  in local mode). A crash may leave gaps; an open raises `SEQ` to each table's largest ID
  (`DOC/ENGINE.md`, Opening a data directory).
- Containment is a `PARENT_ID` tree at most `ENG_MAXDEPTH` deep, with a cycle check on
  install. An installed item has the location of its top-level item, so a location's listing
  includes the installed parts (`DOC/ENGINE.md`, Assemblies).
- Storage is a second tree, of locations (`LOCS.DBF`): a location is known by its `LOC_ID`,
  which never changes, and sits inside the location its `PARENT_ID` names (0 at the top), at
  most `ENG_MAXLOCDEPTH` levels deep and never inside itself; an item names its location by
  `LOC_ID` (0 unknown) at any level, so moving a location moves everything in it without
  writing an item. People read a location by its name, unique among its siblings, and its path
  (`Office > Shelf A > Bin 1`); a short code is optional, for typing and labels. Its type comes
  from the code table `LTYPES.DBF`, seeded and extended by the user like the categories
  (`DOC/ENGINE.md`, Items, locations and code tables). Storage and installation stay apart: a
  card in a bin is available, a card in a machine is installed.
- Deleted rows keep the dBASE `*` flag until PACK; readers skip them.
- Every data file is opened deny-read/write. With `SHARE.EXE` loaded a second program on the
  same data directory fails with 304 (`ENG_EINUSE`); without SHARE, one program per data
  directory is the operator's rule.
- The open holds every handle the engine will need at once, so too small a `FILES=` stops `INV`
  and `INVSERV` at their start with 303 (`DOC/ENGINE.md`, Opening a data directory).
- `USERS.DBF` holds the PROTECT-style accounts (Users and logins, above); what its hash does
  and does not withstand is in `DOC/SECURITY.md` (Users and passwords).
- The `.NDX` files are dBASE III's own layout, checked both ways: dBASE III Plus 1.1 reads and
  searches our indexes, and `T_NDX` reads indexes dBASE built (`TEST/GOLDEN/DB3/`). Layout and
  delete rules: `DOC/ENGINE.md` (NDX layout).

## Why INVWIN needs protected mode

`INV` runs in real mode on an 8088; `INVWIN` needs Windows in standard or 386 enhanced mode.
A DOS program owns its memory and nothing moves or drops its code while it runs; under
real-mode Windows 3.0 the system does both, and the fault lies between how it does so and the
frames Open Watcom compiles, not in the client's own code:

- In real mode, KERNEL moves and discards code segments to make room. When it discards one,
  it mends every far return address on the stack that points into it: it finds each far frame
  by its odd saved BP, points the return at a reload thunk, and keeps the true return offset
  in the frame's BP-2 slot, which it takes to hold the saved DS. On the reload it takes DS back
  from that slot and resumes at the offset it held. (This was traced in a hung run; how a move
  without a discard mends the stack was not.)
- Microsoft C's `-Gw` gives every far function that frame,
  `inc bp; push bp; mov bp,sp; push ds`. Open Watcom's `-zW` gives the odd BP but saves DS at
  BP-2 only in exported functions, `-zw` changes only `FAR PASCAL` functions, and no switch
  gives every far function the slot. So when Windows discarded the caller of one of our own far
  functions, the patch overwrote a saved register at BP-2, and the return resumed at the offset
  of DGROUP's paragraph.
- Measured under real-mode Windows: a test program with many nested far calls between our
  segments hung or vanished in up to a tenth of its runs, and `MessageBox` sometimes returned
  0 without showing a box. Keeping the code moveable but not discardable was tried and
  rejected: `MessageBox` still failed in a few runs, and `WPROTO` no longer started on the
  8086 machine.
- In standard and 386 enhanced mode, code is reached through selectors: a move changes a
  descriptor and a discarded segment faults back in, so KERNEL never patches the stack, and
  the fault cannot occur. Every failure seen was in real mode. Windows 3.1 dropped real mode.

Started in real mode, a program built so gets Windows 3.0's box "Application Execution Error:
This application requires Standard or Enhanced Mode Windows." and never runs.

The Windows code keeps the rules of real mode all the same, on purpose: no far pointer into a
moveable block is held across a message or a yield (`CMEM.C` locks a block only inside each
call), the blocks a program keeps for the whole run are fixed in real mode
(`cmem_alloc_perm`), memory that DOS must reach comes from `GlobalDosAlloc`, every procedure
Windows calls is a `WCALLBACK`, and nothing needs `COMMDLG.DLL`.

Bringing real mode back, and with it the 8086, would take a compiler that gives every far
function the real-mode frame (Microsoft C's `-Gw`, or a patched Open Watcom), dropping `wrc -t`
and the check of `TEST/NECHK.PY` that wants it, and putting the real-mode kinds `winr` and
`wincga` back into `WIN_KINDS` (`DOC/BUILD.md`).

## Design constraints

| Constraint | How the design meets it |
|---|---|
| NetBIOS under emulation; half-open sessions | Only the INT 5Ch interface is used, so any NetBIOS provider serves. The two QEMU machines run Client 3.0's unmodified `AM2100.DOS` on QEMU's PCnet card once `AUTOEXEC.BAT` enables its bus mastering (`DOC/BUILD.md`, Network card). A session request that comes late leaves a half-open session: NetBEUI ends the server's LISTEN (18h), or the LISTEN completes and the server holds the slot until `IDLE=`; either way the server listens again. |
| 64 KB code segments | Medium model; every build checks each code segment against `MAX_CODE` of `TEST/mapchk.sh`; `-os`; table-driven screens, commands and printer codes. |
| 64 KB of near data; far objects | No buffer over `CMEM_MAXBLK`; far buffers only through `CMEM`; static near data within `MAX_NEAR`, or `INVTCP_MAXNEAR` for the programs on WATTCP, whose sockets and packet buffers sit in DGROUP, checked from every map by `TEST/mapchk.sh`. |
| Turbo C 2.01's ANSI gaps | Portable files compiled strict ANSI (`-A` under Turbo C, `-za` under Open Watcom); the forbidden constructs of `DOC/STYLE.md` (Hard rules), enforced by `make lint`. |
| Watcom C 6.5's quirks and damaged libraries | `#elif`, `(void)x;` and `const` pointers to incomplete structs are banned (lint). On the only surviving copy of its disks the small- and compact-model run-time libraries are about half overwritten by other files' data, so the build uses a published reconstruction whose SHA-256 `WC65/CLIBREC.SHA` pins, and `WC65/chklib.py` checks the libraries of every model in use before each build. |
| 16-bit `int` faults that gcc hides | Fixed-width typedefs; `-Wconversion`; every unit test but the host-only `T_COMPAT` also runs as a DOS program under the three compilers, in DOSBox-X and on MS-DOS 3.10 (`T_UART` on MS-DOS 3.10 alone, below). |
| Drift above the DOS 3.1 floor | One module makes every INT 21h call (Floor, above), so the floor is checked in one file; the tests boot Compaq MS-DOS 3.10. |
| An absent or stalled printer | A missing printer is reported at once from the BIOS data area and the status read, and every byte is bounded by the BIOS time-out, so printing never hangs; the user retries or cancels. DOS stops a PRN write at 1Ah in cooked mode, so a job switches PRN to raw mode, where every byte value passes unchanged, as through INT 17h. |
| Abandoned serial sessions | A full null-modem cable lets the server notice a client that went without BYE; on a cable of three wires only `IDLE=` ends such a session (`DOC/INSTALL.md`). QEMU's joined ports carry no modem signals, so the hang-up is tested on simulated ports (`T_TSER`); DOSBox-X's loopback repeats bytes, so `T_UART` runs on QEMU's 16550A. |
| A Linux share for MS Network Client 3.0 | Client 3.0 offers at best the dialect `DOS LM1.2X002`, which Samba counts as LANMAN2, so Samba's default floor (SMB2), or any floor above LANMAN2, refuses it: the tests' share speaks exactly LANMAN2 (`server min protocol` and `server max protocol`), without Unix extensions and following no link (`TEST/INTEG/SMB/SMB.CNF`), so that a client cannot plant a link that the host would write through. |
| WATTCP under the period compilers | `SRC/TCP/WATTCP.DIF` holds the changes the three compilers need (return types, prototypes, casts, the IP header's 4-bit fields typed `WT_BF4`) and one more, which starts each connection a listening socket takes at an initial sequence number of `wt_isn` (`SRC/COMPAT/CWATT.C`) in place of WATTCP's fixed one. `SRC/TCP/WATPREP.PY` applies it to the checked archive with `patch --fuzz=0`, so a hunk that does not match exactly stops the build, then drops the end-of-file Ctrl-Z, converts `//` comments and upper-cases include names (`DOC/BUILD.md`, WATTCP). |

## WATTCP licence

WATTCP's 1994 release states its licence in `INCLUDE/COPYRIGH.H`: programs built with WATTCP
may be given away or sold in source or executable form; the library or a portion of it may not
be sold, and modified versions of the library code may not be distributed in source, OBJ or LIB
form without the author's written permission. The author later released the same library code
under the GNU LGPL, version 2 or later (release `wat1104`, 2004). `SRC/TCP/WATTCP.DIF` is the
only text in the repository derived from WATTCP's sources; it carries fragments of the 1994
code in its context and changed lines, a small gray area the project accepts. Hence no WATTCP
file is ever committed, the prepared tree `OBJ/WATTCP/` stays local and git-ignored, and only
`INVTCP.EXE` is distributed, with the notice `REL/WATTCP.TXT`.
