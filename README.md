# RETROINV — Retro Hardware Inventory for MS-DOS

An inventory system for a collection of retro PCs and hardware that runs on **MS-DOS 3.1 or
later on an 8086/8088** and keeps its data in **real dBASE III files**. Every client speaks one
**period-correct line protocol**: INV runs the engine in-process for one user, or reaches a
dedicated DOS server over NetBIOS or a serial line; the server also answers request files left
in a mailbox directory, and a bridge carries the protocol from TCP/IP clients to it.

| Program | Role |
|---|---|
| `INV.EXE` | The client: a full-screen inventory of items with their parts, service records, loans, reservations and the equipment in use with each system, tags and whether each works, storage locations nested as rooms, shelves and bins, search, reports and labels, CSV export and import, users and settings; or, with `/CMD`, protocol requests from standard input. It runs the engine on a local data directory (`/LOCAL`, the default) or calls a server over NetBIOS (`/NET name`) or a serial line (`/COM1`, `/COM2`). [`DOC/CLIENT.md`](DOC/CLIENT.md) |
| `INVTHIN.EXE` | The network-only client: `INV` without the engine. It reaches a server over NetBIOS or a serial line and cannot open data itself, so it needs far less memory, for a PC too small for `INV`. [`DOC/INSTALL.md`](DOC/INSTALL.md) |
| `INVWIN.EXE` | The client for Windows 3.0 and later in standard or 386 enhanced mode: the screens of `INV` as windows and dialogs, on the same data, messages and settings, over INV's links and two more, the server's mailbox (`/MBOX:dir`) and TCP through the bridge (`/TCP:addr`). [`DOC/INSTALL.md`](DOC/INSTALL.md) |
| `INVSERV.EXE` | The server, sole owner of the `.DBF` files while it runs: NetBIOS sessions, serial ports and a mailbox directory, served from one event loop behind a status screen. [`DOC/SERVER.md`](DOC/SERVER.md) |
| `INVTCP.EXE` | The TCP bridge, on the WATTCP library: each TCP connection on port 7000 is relayed over a NetBIOS session to the server. [`DOC/SERVER.md`](DOC/SERVER.md) |
| `INSTALL.EXE` | The installer, on every release disk: asks what the PC is (alone, the server, a network client or one too small for INV), whether to add the Windows client, the directory and the data, and copies what those answers need; `INSTALL /U` removes the installation. [`DOC/INSTALL.md`](DOC/INSTALL.md) |
| `SETUP.EXE` | The installer of `INVWIN` for Windows 3.0: copies its files from a disk and makes its Program Manager group RETROINV with the link's switches; `SETUP /U` removes the group and the Windows client's files. [`DOC/INSTALL.md`](DOC/INSTALL.md) |
| `DBPACK.EXE` | Offline PACK and REINDEX of a table and its indexes, with crash recovery. |
| `INVDUMP.EXE` | Dumps a table's header, fields, records and memos. |
| `COMTEST.EXE` | Tests a serial port in the chip's own loopback, with nothing on the line. |
| `NBTEST.EXE` | NetBIOS probe and two-station session test. |
| `INVTEST.EXE` | Scripted protocol driver for a running server, over several NetBIOS, serial and mailbox sessions at once. |
| `TCPTEST.EXE` | TCP probe: an echo server on the bridge's sockets, without NetBIOS. |

The tools' command lines are in [`DOC/BUILD.md`](DOC/BUILD.md) (Programs and scripts) and, for
`COMTEST`, [`DOC/CLIENT.md`](DOC/CLIENT.md).

## Quick start

On a PC with MS-DOS 3.1 or later (or in DOSBox, or any PC emulator), put a release disk in `A:`,
type `A:INSTALL` (from the zip, `INSTALL` in its unpacked folder), answer its questions
(`C:\RETROINV` is its default directory) and add `FILES=45` to `CONFIG.SYS`. Then start the
client and log in as `ADMIN` with the password `admin`, which it asks you to change at once:

```
C:\>CD \RETROINV
C:\RETROINV>INV
```

[`DOC/INSTALL.md`](DOC/INSTALL.md) gives the rest: the disks, the memory each program needs,
the network, serial, mailbox, TCP and Windows set-ups, and the first steps. Every link but local
mode carries passwords in plain text: before the server or the bridge serves a network, read
[`DOC/SECURITY.md`](DOC/SECURITY.md).

## Building

The build and its tests run on Linux, Debian 13 being the tested host, or in a container on
Windows, macOS or any other Linux: with Docker or Podman installed, `sh HOST/run.sh` (in Git
Bash on Windows) builds the image of `HOST/Containerfile` once and runs the commit gate,
`make gate`, in it; other make goals are given as its arguments. The period compilers, DOS
versions and dBASE the tests run are not part of the repository and are supplied by the user.
`make test` needs only the Debian packages of the tool list (gcc, Python, nasm, patch among
them); `make all` also needs Open Watcom and WATTCP's `source.zip`; the gate needs the period
software as well. Tools, period software and setup: [`DOC/BUILD.md`](DOC/BUILD.md).

## Release

`make dist` writes the release floppy images into `DIST/`, and `RETROINV.ZIP`, the files of the
1.44 MB disk for a hard disk or an emulator. What they hold is in
[`DOC/INSTALL.md`](DOC/INSTALL.md) (The disks), how they are made and checked in
[`DOC/BUILD.md`](DOC/BUILD.md) (Release).

## Documentation

- [`DOC/INSTALL.md`](DOC/INSTALL.md) — installing and running: the disks, requirements, each
  link, the Windows client, first login, backup
- [`DOC/SECURITY.md`](DOC/SECURITY.md) — what the logins protect and what they do not, per link,
  and how a site keeps its data safe
- [`DOC/DESIGN.md`](DOC/DESIGN.md) — design decisions, module layout, data model, constraints
- [`DOC/ENGINE.md`](DOC/ENGINE.md) — the dBASE III files, the engine and the protocol layer
- [`DOC/PROTOCOL.md`](DOC/PROTOCOL.md) — the wire protocol
- [`DOC/CLIENT.md`](DOC/CLIENT.md) — the clients: switches, screens, keys, settings, memory
- [`DOC/SERVER.md`](DOC/SERVER.md) — the server and the TCP bridge: settings, sessions, log,
  shutdown, connections
- [`DOC/BUILD.md`](DOC/BUILD.md) — toolchain, build, every test, release, memory figures
- [`DOC/STYLE.md`](DOC/STYLE.md) — contributor rules: the DOS floor, code style, tests, scripts,
  documents and commits

## Licence

RETROINV's own code is released under the MIT licence: [`LICENSE`](LICENSE). WATTCP keeps its
own terms, both the library linked into `INVTCP.EXE` and the fragments of its 1994 code in the
context and changed lines of `SRC/TCP/WATTCP.DIF`: a program built with it may be given away or
sold, but the library may not be sold, nor passed on modified in source, object or library form,
without its author's written permission ([`REL/WATTCP.TXT`](REL/WATTCP.TXT);
[`DOC/DESIGN.md`](DOC/DESIGN.md), WATTCP licence).
