# RETROINV — Installation

RETROINV is released on floppy disks and as a zip of the 1.44 MB disk, both written by
`make dist` (`DOC/BUILD.md`, Release). This document says what a site needs, how to install the
programs with the installers on the disks, and how to start on one PC, on a NetBIOS network,
over a serial line, with a mailbox and with the TCP bridge, under DOS and under Windows. Each
disk carries a short version of it as `README.TXT`, with the memory figures filled in.

## The disks

| Disk | Files |
|---|---|
| 360 KB disk 1 (`RETROINV1`) | `INV.EXE`, `INV.MSG`, `INV.HLP`, `INVTHIN.EXE` |
| 360 KB disk 2 (`RETROINV2`) | `INVSERV.EXE`, `INVSERV.MSG`, `INVSERV.CFG`, `INVTCP.EXE`, `WATTCP.CFG`, `DBPACK.EXE`, `INVDUMP.EXE`, `COMTEST.EXE`, the sample data `DATA\` |
| 720 KB disk 1 (`RETROINV1`) | every file of the 360 KB disks |
| 720 KB disk 2 (`RETROINV2`) | `INVWIN.EXE`, `INVWIN.HLP`, `INVWIN.HTX`, `INV.MSG`, `SETUP.EXE` |
| 1.2 MB disk (`RETROINV`) | every file of the two 720 KB disks |
| 1.44 MB disk (`RETROINV`) | every file of the two 720 KB disks |

Every disk also holds `INSTALL.EXE`, `INSTALL.LST`, `README.TXT`, `LICENSE.TXT` and
`WATTCP.TXT`. These, and `SETUP.EXE`, are on the disks as they are; every other file is
compressed in the format of Microsoft's `COMPRESS` and named as `COMPRESS -r` names it, the last
letter of its extension made `_`: `INV.EX_` holds `INV.EXE`, `INVSERV.CF_` holds `INVSERV.CFG`.
The sample data is the directory `DATA._`, each of its files compressed under its own name. No
program runs from the disks: every PC installs first, and the installers expand the files as
they copy them (Installing, below).

The DOS programs on every disk are the Watcom C 6.5 build, the one released. Each set is the
fewest disks of its size that hold its files. The 360 KB set serves the PCs, XTs among them,
whose drives read nothing larger; the Windows client `INVWIN` needs Windows 3.0 on a 286 or
better and is only on the 720 KB, 1.2 MB and 1.44 MB disks and in the zip. The 1.2 MB disk
serves an AT whose only floppy drive is a 5.25-inch 1.2 MB one. The first 720 KB disk holds
every DOS file, the second the Windows client and the files `SETUP` copies with it.

`RETROINV.ZIP` holds the files of the 1.44 MB disk as they are, none of them compressed, in a
folder `RETROINV\`, for an install on a hard disk or in an emulator without a floppy drive. It is
meant for an unzipper on a modern host; under DOS it needs `PKUNZIP` 2.04g or later, with `-d`.

| File | What it is |
|---|---|
| `INSTALL.EXE`, `INSTALL.LST` | the installer, which also removes an installation, and its list of the set's disks and files (Installing, below) |
| `SETUP.EXE` | the installer of the Windows client and its Program Manager icon, which also removes them (The Windows client, below) |
| `INV.EXE`, `INV.MSG`, `INV.HLP` | the client, its messages and its help (`DOC/CLIENT.md`) |
| `INVTHIN.EXE` | the network-only client: `INV` without the engine, which reads `INV.MSG` and `INV.HLP` too (The network-only client, below) |
| `INVWIN.EXE`, `INVWIN.HLP`, `INVWIN.HTX` | the client for Windows and its help, for Windows Help and as text; its messages are those of `INV.MSG` |
| `INVSERV.EXE`, `INVSERV.MSG`, `INVSERV.CFG` | the server, its messages and a settings file (`DOC/SERVER.md`) |
| `INVTCP.EXE`, `WATTCP.CFG` | the TCP bridge and a sample of its TCP/IP settings (`DOC/SERVER.md`, The TCP bridge) |
| `DBPACK.EXE`, `INVDUMP.EXE` | offline PACK and REINDEX of a table, and a table dump (`DOC/BUILD.md`) |
| `COMTEST.EXE` | the serial port test (`DOC/CLIENT.md`) |
| `DATA\` (`DATA._` on the disks) | the sample data: a small collection stored in an office and a garage, in dBASE III tables, memo files and indexes (`DOC/ENGINE.md`) |
| `LICENSE.TXT` | the MIT licence of RETROINV |
| `WATTCP.TXT` | the licence notice of WATTCP, the TCP/IP library linked into `INVTCP.EXE` |

`INVTCP.EXE` is the only file that contains WATTCP; no WATTCP source, object or library file
is on the disks or in the zip, as WATTCP's licence requires (`DOC/DESIGN.md`, WATTCP licence).

## Requirements

- **DOS.** MS-DOS 3.1 or later on an 8088 or better, with the free memory below (Free
  conventional memory); the network-only client needs the least (The network-only client,
  below), and that section says what common PC sizes leave free. MS-DOS 3.1 reads the 360 KB
  disks and, in a 1.2 MB drive, the 1.2 MB disk: the 720 KB disks need DOS 3.2 and the 1.44 MB
  disk DOS 3.3 (and the drives).
- **`FILES=45`** in `CONFIG.SYS`. With too few, `INV` and `INVSERV` stop at their start with
  error 303 (`DOC/ENGINE.md`, Opening a data directory). `INVWIN` needs no `FILES=`: Windows 3.0
  enlarges DOS's file table to 255 entries when it starts. DOSBox-X's own DOS keeps a table of
  its own, which Windows cannot enlarge and whose entries Windows' own files (about 18) share,
  so there `INVWIN` needs `files=65` in the `[config]` section of DOSBox-X's settings.
- **`SHARE.EXE`**, recommended on the PC that holds the data: with it DOS refuses a second
  program that opens the same data directory.
- **Free conventional memory**, as `CHKDSK` reports it where the program starts, in KB. The
  figures are the constants of `MAKEFILE` for the released Watcom C 6.5 build (`DOC/BUILD.md`,
  Runtime requirements); `README.TXT` on the disks gives their values.

  | Program | Free memory |
  |---|---|
  | `INV /LOCAL` (its deepest screens) | `INV_FREEKB_WC65` |
  | `INV /NET` (its deepest screens), network loaded | `INVSCRNET_FREEKB_WC65` |
  | `INV /COM1` (its deepest screens), network loaded | `INVSCR_FREEKB_WC65` |
  | `INV /CMD /NET`, network loaded | `INVNET_FREEKB_WC65` |
  | `INV /CMD /COM1`, network loaded | `INVCOM_FREEKB_WC65` |
  | `INVSERV`, network loaded | `INVSERV_FREEKB_WC65` |
  | `INVTCP`, NetBIOS and packet driver loaded | `INVTCP_FREEKB_WC65` |
  | `INVTHIN /COM1` (its deepest screens), network loaded | `THINSCR_FREEKB_WC65` |
  | `INVTHIN /NET` (its deepest screens), network loaded | `THINSCRNET_FREEKB_WC65` |
  | `INVTHIN /CMD /NET`, network loaded | `THINNET_FREEKB_WC65` |
  | `INVTHIN /CMD /COM1`, network loaded | `THINCOM_FREEKB_WC65` |

- **NetBIOS**, for `INV /NET` and `INVTHIN /NET`, `INVTCP` and `INVSERV` unless its
  `SESSIONS=0` (a server that serves only serial ports and a mailbox): a NetBIOS provider (INT
  5Ch), such as MS Network Client 3.0 with NetBEUI, the one tested. Its limits are set in the
  `[MS$NETBEUI]` section of `PROTOCOL.INI` and count for the whole PC: on the server, give
  `SESSIONS=` at least the server's `SESSIONS=` (`INVSERV.CFG`, at most `CONN_NETMAX`) and
  `NCBS=` at least twice that plus one, each plus what the redirector and other NetBIOS programs
  there use (`DOC/SERVER.md`, NetBIOS resources). For example:

  ```
  [MS$NETBEUI]
  DriverName=netbeui$
  SESSIONS=16
  NCBS=32
  ```

- **Serial cable**, for `INVSERV` with `SERIAL=` and `INV` or `INVTHIN` with `/COM1` or
  `/COM2`: a full null-modem cable, which carries each end's DTR to the other's DSR and DCD (and
  RTS to CTS, for `RTSCTS=1`), so that the server ends the session of a client that goes without
  BYE. A cable of three wires (TxD, RxD and ground) carries the requests too, but then only
  `IDLE=` ends such a session, and with `IDLE=0` it is never ended (`DOC/SERVER.md`, Serial
  lines).
- **Packet driver**, for `INVTCP` only: a packet driver for the card that carries TCP/IP, such
  as Crynwr's `NE2000.COM`, which is not on the disks; with NetBEUI, on a second card
  (`DOC/SERVER.md`, Running the bridge).

## Installing

Put a disk of the set, the first or any other, in `A:` and type `A:INSTALL` at the DOS prompt;
the installer reads the disks in the drive it was started from, so `B:INSTALL` installs from
`B:`. Every question shows its default answer in brackets, which Enter takes; Esc or Ctrl+C at
a question stops it with "Stopped. Nothing was installed.", as it asks everything before it
copies anything.

1. **What is this PC?** It offers only the choices the set holds a program for.
   1. *This PC alone*: `INV` and the data.
   2. *The server*: `INVSERV`, the TCP bridge `INVTCP`, the tools `DBPACK`, `INVDUMP` and
      `COMTEST`, `INV` and the data, which the other PCs reach through the server.
   3. *A network client*: `INV`, which calls the server and keeps no data.
   4. *A network client too small for `INV`*: the network-only client `INVTHIN` (The
      network-only client, below).
2. **Also the Windows client, `INVWIN`?** No unless you say yes; not asked from the 360 KB set,
   which has no `INVWIN`. Yes adds `INVWIN` and `SETUP`, which makes its Program Manager icon
   (The Windows client, below).
3. **Install in which directory?** `C:\RETROINV` unless you type another, a whole path below a
   drive's root; the installer makes its last level if it is not there.
4. **Start with the sample data?** Asked of a PC alone and a server when the directory has no
   `DATA\` yet, or an empty one. Yes, the default, copies the sample, a small collection with three users to try
   the program on (First login, below); no makes an empty `DATA\`, which `INV` fills with an
   empty inventory at its first start.

Each installation gets `INSTALL.EXE`, its list `INSTALL.LST`, `README.TXT` and `LICENSE.TXT`,
the server `WATTCP.TXT` too, and then only the files of its role: a PC alone and a network
client `INV.EXE`, `INV.MSG` and `INV.HLP`, with no tools; the server those and every program
and settings file of the 360 KB disk 2; the network-only client `INVTHIN.EXE`, `INV.MSG` and
`INV.HLP`; the Windows client `INVWIN.EXE`, `INVWIN.HLP`, `INVWIN.HTX`, `INV.MSG` and
`SETUP.EXE`. Each compressed file is expanded as it is copied. Before it copies, the installer
says how many kilobytes that needs and how many the drive has free, and stops if they do not
fit. The need counts each file at its expanded size in the drive's whole allocation units, less
the files of an earlier installation that the copy replaces. The sample data is copied last
from its disk, into a `DATA\` made only then, so a stopped or failed installation leaves either
the whole sample or no `DATA\` at all.

A file on another disk of the set is asked for by that disk's volume label, the name in
parentheses in the table above ("Insert the disk labelled RETROINV2 in A: and press Enter"). The
installer checks the label, and then that the disk's `INSTALL.LST` names the same release and
set, so a wrong disk, or the disk of another set or release with the same label, is refused with
"That is not the disk asked for." Esc at that question stops the installer and keeps what it has
copied: run `INSTALL` again to finish. From the 360 KB set every installation takes disk 1,
and a server and the sample data disk 2 as well; from the 720 KB set every DOS installation
takes the first disk alone, and the Windows client the second. A directory that holds a set's
files as they are, an installation or the zip's folder unpacked, serves as well as the disks:
where a file's compressed name is missing, `INSTALL` copies the file as it is under its own
name, so from such a directory it asks for no disk. A directory that holds a disk's files as the
disk does, compressed, serves as a source too, but not as its own target: `INSTALL` refuses to
install there ("it holds the disks' compressed files; install elsewhere"), and `SETUP` asks for
another directory.

At the end the installer prints how to start what it installed. It changes nothing outside the
directory, `CONFIG.SYS` and `AUTOEXEC.BAT` included (only Program Manager, when `SETUP` makes
the icon, writes in the Windows directory): add `FILES=45` to `CONFIG.SYS` yourself,
and load `SHARE` and the network as Requirements, above, says. An error (a full or
write-protected disk, a drive not ready, a damaged file) is said with the file it struck, and a
copy that fails leaves no partly written file. For a batch file, its exit status is 0 when it
is done, 1 for a usage error, 2 when a step failed, 3 when the user stopped it and 4 under a DOS
older than 3.1.

**Over an existing installation.** `INSTALL` into the same directory installs a new release or
another role there. It replaces the programs and never touches a `DATA\` that holds files ("Your
data in C:\RETROINV\DATA stays as it is."), so it asks no data question; an empty `DATA\` gets
the sample data question as a new installation does. The settings file `INV.CFG`,
on no disk, stays too. For `INVSERV.CFG` and `WATTCP.CFG` it asks whether to replace the one
already there with the new one, keeping it unless you answer yes.

**Removing.** `INSTALL /U` removes the installation in the directory `INSTALL.EXE` is in, and
`INSTALL /U dir` the one in `dir`; a drive's root is refused. It asks whether to remove RETROINV
from the directory, then whether to delete the data in `DATA\` too, which Enter keeps, and
deletes every file `INSTALL.LST` names that is there, the Windows client's included, and the
list last. A file it did not install, such as the server's event log `INVSERV.LOG`, stays, and
the directory with it ("C:\RETROINV holds other files, so it stays."); an empty directory is
removed. Where `INVWIN` is installed, first run `C:\RETROINV\SETUP /U` in Windows to remove its
Program Manager group, as the installer reminds you: `INSTALL` never talks to Program Manager.

**By hand.** The zip serves where the installer does not, an emulator's folder for one: unpack
its `RETROINV\` folder onto the hard disk, or into the host folder an emulator mounts as one,
and empty its `DATA\` for an empty inventory (First login, below). From the disks, a copy by
hand needs the `EXPAND` of MS-DOS 5 or later, which reads `COMPRESS`'s format, given the name
of each file it makes:

```
C:
MD \RETROINV
CD \RETROINV
EXPAND A:INV.EX_ INV.EXE
MD DATA
EXPAND A:\DATA._\ITEMS.DBF DATA\ITEMS.DBF
```

and likewise for each other file a role needs (Installing, above, lists them); `README.TXT`,
`LICENSE.TXT`, `WATTCP.TXT`, `INSTALL.EXE` and `SETUP.EXE` are copied with `COPY`. DOS before 5
has no `EXPAND`: there install with `INSTALL`.

## Starting

- **One PC:** `INV` in `C:\RETROINV` opens `DATA\` there (`INV /LOCAL dir` opens another, as
  does `DATA=` of its settings file `INV.CFG`); a `DATA\` that is not there stops it with "no
  such directory: make it (MD) or name another".
- **A network:** on the PC that holds the data, `INVSERV` in `C:\RETROINV` reads `INVSERV.CFG`
  from the current directory: the sample sets the NetBIOS name `RETROINV`, the data directory
  `DATA` and the event log `INVSERV.LOG`; its comments list the other keys. The clients start
  `INV /NET RETROINV` (or, too small for `INV`, `INVTHIN /NET RETROINV`). While the server
  runs, no other program may open its data, which `SHARE.EXE` enforces.
- **After a power cut:** a start that finds a change cut off part-way (a power cut or a reset
  between its first write and its last, not one while the program was idle) rebuilds the
  indexes of the tables that change named and both indexes of the audit log before it
  opens: some twenty minutes for each ten thousand entries of the log on an 8088.
- **A serial line:** join the server's port and the client's with a full null-modem cable,
  add `SERIAL=COM1` (or `COM2`, or both) to `INVSERV.CFG`, and `BAUD=` and `RTSCTS=1` when
  the default rate `UART_DEFBAUD` without flow control does not suit; `SESSIONS=0` there runs
  the server without NetBIOS, which a PC with no NetBIOS provider needs: `INVSERV` refuses to
  start there otherwise. The client puts the same `BAUD=` and `RTSCTS=` in its `INV.CFG` and
  starts `INV /COM1` (or `/COM2`; `INVTHIN` likewise). `COMTEST` (`COMTEST /COM2`) first checks
  a port in the chip's own loopback, with nothing on the line; `/BAUD:n` and `/RTSCTS` test it
  at the rate and with the flow control of the settings.
- **A mailbox:** add `MAILBOX=dir` to `INVSERV.CFG`, `dir` holding the directories `REQUEST`
  and `RESPONSE`, which the server does not make; on a network drive, any PC that can write
  there is a client. A program writes its protocol lines, starting with `HELLO` and `LOGIN`,
  in `REQUEST\` under a name not ending in `.REQ`, renames it to `<station>.REQ` once complete,
  and reads the answers from `RESPONSE\<station>.RSP` (`DOC/PROTOCOL.md`); `INV` itself does
  not use the mailbox, `INVWIN` does (The Windows client, below).
- **TCP/IP clients:** on a PC of its own, with NetBIOS and the packet driver loaded (`INVTCP`
  and `INVSERV` are both foreground programs), edit `WATTCP.CFG` (`my_ip=`, `netmask=`,
  `gateway=`) and start `INVTCP RETROINV` in the directory that holds it, or set the environment
  variable `WATTCP.CFG` to that directory (`DOC/SERVER.md`, Running the bridge). Clients connect
  to TCP port `BR_PORT` and speak the protocol of `DOC/PROTOCOL.md`: any program that does, a
  line-mode `nc host port` for one, and `INVWIN`; `INV` itself does not use TCP.

Every link but local mode carries the requests, the answers and the passwords in plain text,
and `INVTCP` accepts a connection from any address unless `/ALLOW:` names its networks. Before
a network, a serial line, a mailbox share or the bridge serves the data, read
`DOC/SECURITY.md`: what each link exposes, and how a site keeps its network, files and
passwords safe.

## The network-only client

`INVTHIN.EXE` is `INV` built without the engine, the part that opens the data. It takes `INV`'s
switches, settings file `INV.CFG`, messages and help, and its screens and `/CMD` work as
`INV`'s, but it only calls a server, `/NET name`, `/COM1` or `/COM2`: it has no `/LOCAL` and
cannot open a data directory, so started without a server named it shows its usage and stops.

A PC that only calls a server runs either. Where `INV` fits in its memory, `INV` is the simpler
choice: one program serves every PC, and it can also open data itself, a copy on a floppy for
one. `INVTHIN` is for the PC too small for `INV`, as it needs far less memory (Requirements,
above).

A PC has room for a program when its memory, less what DOS and its resident programs take, is
at least the program's figure. MS-DOS 3.10 with `FILES=45` takes about 40 KB: 640 KB less
`FREE310_TOP` bytes (`TEST/INTEG/free310.sh`), the free memory of a program started at its
prompt. With NetBIOS, MS-DOS 3.30, `SHARE` and MS Network Client 3.0 with NetBEUI together take
about 142 KB (`DOC/BUILD.md`, NetBIOS gate), `SHARE` included, which a station that only calls
a server need not load. So a 256 KB PC with MS-DOS 3.10 alone leaves about 215 KB free: enough
for `INVTHIN` over a serial line, with a little to spare for resident programs, but not for
`INV`. A 384 KB PC with the network leaves about 242 KB, room for `INVTHIN` over NetBIOS too;
the same PC with MS-DOS 3.10 alone leaves about 344 KB, room for `INV /LOCAL`. A 512 KB PC with
the network leaves about 370 KB, room for `INV` over NetBIOS or a serial line. Other network
software, drivers and resident programs take more or less: check with `CHKDSK` on the PC itself.

## The Windows client

`INVWIN.EXE` is the client for Windows: the screens of `INV` as windows, menus and dialogs, on
the same data, messages (`INV.MSG`) and settings file, with a help file of its own,
`INVWIN.HLP`, which Windows Help (`WINHELP.EXE`, part of Windows) shows, and its text
`INVWIN.HTX`, shown in a window of `INVWIN`'s own on a PC without Windows Help, and printing on
Windows' default printer.

**Requirements.** Windows 3.0 or later in standard or 386 enhanced mode, so a 286 or better,
with an EGA (640 × 350) or better display. `INVWIN` is marked for protected mode only, and
Windows 3.0 in real mode refuses to start it: when real-mode Windows discards code, it patches
the far stack frames of its callers, which it expects in a form Open Watcom does not compile
(`DOC/DESIGN.md`, Why INVWIN needs protected mode). Each link needs what `INV`'s does, started
before Windows: a NetBIOS provider for `/NET` (MS Network Client 3.0 with NetBEUI, the one
tested); a full null-modem cable for `/COM1` and `/COM2`, which go through Windows' comm
driver, so no other program, a serial mouse for one, may hold the port; for `/MBOX:`, the
server's mailbox on a drive the PC can write, such as a share of the Workgroup Add-On for
MS-DOS, the one tested. `/TCP:` needs a Windows Sockets 1.1 stack, its `WINSOCK.DLL` on
Windows' search path, such as Microsoft TCP/IP-32 on Windows for Workgroups 3.11, the one
tested; Windows 3.0 has none, and without one `INVWIN` refuses `/TCP:` alone. `SHARE.EXE`,
loaded before Windows on the PC that holds the data, stops a second program opening it, as
under DOS.

**Installing.** `SETUP`, in Program Manager's File > Run, installs the Windows client and its
icon. Its one dialog, RETROINV Setup, asks for the Directory, its own directory when that is on
a fixed disk and `C:\RETROINV` otherwise, and the Link switches the icon starts `INVWIN` with
(Starting, below), such as `/NET RETROINV`, or none for one PC; Program Manager takes no comma,
parenthesis, bracket or quote in them. Install copies `INV.MSG`, `INVWIN.EXE`, `INVWIN.HLP`,
`INVWIN.HTX` and `SETUP.EXE`, then `INSTALL.LST`, expanding those the disk holds compressed,
each replacing an earlier copy only once it is whole, and makes the Program Manager group
RETROINV with one item, RETROINV, that starts `INVWIN.EXE` there with those switches. Started as `A:SETUP`, it installs from the second 720
KB, the 1.2 MB or the 1.44 MB disk; after `INSTALL` with the Windows client (Installing, above),
`C:\RETROINV\SETUP` copies nothing and only makes the icon. `SETUP` copies no data: install
`DATA\` with `INSTALL`. It writes nothing outside the directory and leaves `WIN.INI` and
`SYSTEM.INI` alone; Program Manager itself keeps the group in `RETROINV.GRP` and `PROGMAN.INI`
in the Windows directory. Every run deletes the group RETROINV and makes it anew, so an item you
add to it is lost at the next run: put another link's item in a group of your own, with File >
New, Program Item, and the command line `C:\RETROINV\INVWIN.EXE` followed by that link's
switches; File > Properties changes an item's switches later. When Program Manager does not
answer, the files are installed and the message gives the command line to add by hand.

**Removing.** `C:\RETROINV\SETUP /U`, from File > Run, removes the Windows client of its own
directory (a floppy is refused). After a question that Enter answers no, it deletes the group
RETROINV and `INVWIN.EXE`, `INVWIN.HLP` and `INVWIN.HTX`, and `INV.MSG` unless a DOS program of
the installation still needs it; it keeps `SETUP.EXE`, which is running, `INSTALL.LST`, the data
and the settings, and `INSTALL /U` (Installing, Removing, above) removes the rest.

`INVWIN` finds `INV.MSG`, `INVWIN.HLP` and `INVWIN.HTX` beside its program, and reads its
settings from `INV.CFG` (`CFG_NAME` of `SRC/CLIENT/CFG.H`) in the current directory, which
Program Manager makes the program's own; `/CFG:file` names another. It is `INV`'s settings file
(`DOC/CLIENT.md`, Help and settings files): `INVWIN` uses its `DATA=`, `STATION=`, `USER=`,
`ITEMORD=`, `BAUD=` and `RTSCTS=`, which Options > Settings edits, and `PAGELEN=`, `LABELW=` and
`LABELH=` for its reports and labels; the keys of the screen and the printer are `INV`'s alone.
One copy runs at a time: a second start brings the first one's window to the front.

**Starting.** The switches are `INV`'s, and two more, `/MBOX:` and `/TCP:`:

- **One PC:** `INVWIN`, or `INVWIN /LOCAL dir`, runs the engine inside the program on `dir`,
  else on `DATA=` of the settings, else on `DATA\` in the current directory.
- **A network:** `INVWIN /NET RETROINV` calls the server by its NetBIOS name, as `INV /NET`
  does.
- **A serial line:** `INVWIN /COM1` (or `/COM2`), at `BAUD=` with `RTSCTS=` of the settings,
  which must match the server's.
- **A mailbox:** `INVWIN /MBOX:dir` reaches the server through its mailbox in `dir`, the
  `MAILBOX=` directory of `INVSERV.CFG` as this PC names it (at most `MBXCLI_MAXDIR` characters
  with its separator, `SRC/CLIENT/MBXCLI.H`), each request a file of its own, logged in with a
  login ticket (`DOC/CLIENT.md`, MBOX). Each PC needs a station of its own, 1 to
  `MBXCLI_STALEN` letters, digits, `-` or `_`: `STATION=` of its settings, else the DOS machine
  name, which MS Network Client 3.0 never sets. Without either, `INVWIN` refuses the link and
  asks for `STATION=`. When no answer comes, the message says whether `dir` does not exist,
  has no `REQUEST` directory, or the server does not answer.
- **TCP/IP:** `INVWIN /TCP:addr` connects to the bridge `INVTCP` at the IPv4 address `addr`,
  on port `LINK_TCPPORT` of `SRC/CLIENT/LINK.H`, the bridge's `BR_PORT`, or on another given
  as `/TCP:addr:port`; Esc gives up the connection while it is made.

A link that cannot open is said in a message box, with `INV`'s reason, and ends the program.
At the first login `INVWIN` asks for a new password at once, as `INV` does (First login,
below); F1 shows the help of the window or dialog in front; in a form, that of the field with
the focus.

## First login

The sample data has three users. The first password is the user name in lower case (`admin`,
`oper`, `reader`) and must be changed at the first login (the client asks for the new one at
once):

| User | Level | May |
|---|---|---|
| `READER` | 1 | read, search, report and print |
| `OPER` | 2 | also add and change items, categories, locations and their types, assemblies, service records, loans, reservations and uses |
| `ADMIN` | 3 | also users, backup, restore, pack, reindex, import, export and the audit log |

The level of every command is in `DOC/ENGINE.md` (Protocol layer). These passwords are
published (`DOC/SECURITY.md`, Recommendations): on the server's PC, before the first `INVSERV`,
run `INV` and log in as `ADMIN`, choosing a new password; in Admin > Users, give each sample
user you keep a new temporary password, which that user must change at their first login; and
delete the rest.

To start with an empty inventory instead, answer no to the installer's sample data question,
make `DATA\` with `MD DATA` and copy nothing into it, or empty it: the first program to open it
creates the tables, the standard categories and location types and the one user `ADMIN` with the
password `admin`, which must be changed at its first login (`DOC/ENGINE.md`, Opening a data
directory).

## First session

In `INV`:

1. Start `INV` and log in as `ADMIN` with the password `admin`, on the sample data as on an
   empty inventory, then type a new password twice when asked.
2. The home screen shows today's date: if it is wrong, quit (F3) and set the PC's clock with
   `DATE` and `TIME`, as loans, reservations and service records take today's date.
3. Locations > New location adds a place, a room first; a shelf, box or bin goes inside it
   (Inside, F4 to choose).
4. Items > New item adds an item: Category and the rest, its Location chosen with F4.
5. F1 shows the help of every screen, F9 the keys; F10 or Alt and a letter opens the menus.
   Reports show on the screen first; Options > Settings sets the printer.

`INVWIN` logs in the same way; its menus are Windows', and Help > Getting started gives the
same first steps. It prints on Windows' default printer.

## Backup

Back up the data often. In `INV` or `INVWIN`, logged in at level 3, Admin > Backup copies every
data file to a directory you name, which must exist (a formatted floppy will do), and Admin >
Restore replaces the data with such a copy; Admin > Pack and Reindex tidy the files. All four
need the data to themselves: on a network no other session may be open (`DOC/CLIENT.md`,
Administration). With every program stopped, a plain `COPY DATA\*.*` to another disk is a
backup too, and `DBPACK` packs or reindexes a single table offline. Put such a copy back
whole, every file of it; Admin > Restore copies the tables of a backup alone and builds every
index from them.
