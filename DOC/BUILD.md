# RETROINV — Build, Test and Environment

## Host

The build and every test run on an x86-64 Linux host; Debian 13 (trixie) is the one they are
tested on. They need no particular locale or time zone. There are two ways to get that host:

- **Natively on Linux**: install the tools below.
- **In a container**, with Docker or Podman, on Windows (Docker Desktop or Podman Desktop),
  macOS or Linux. `sh HOST/run.sh [MAKE ARGUMENT...]` runs `make MAKE ARGUMENT...` (`make gate`
  when none is given) in the image of `HOST/Containerfile`, which it builds first when it has
  none for the current `Containerfile` and the calling user: Debian 13 pinned by digest, the
  packages below at the versions of the tested host from one dated instant of
  `snapshot.debian.org`, and the parts of Open Watcom the build uses, checked against its
  SHA-256. The image holds tools only. The repository is mounted read-write as the working
  directory, and `RETRO_DL` (below) read-write as the container user's `~/retro-dl`, which
  `RETRO_DL` inside names too. What the build writes there belongs to the caller under rootful
  Docker or Podman, rootless Podman, rootless Docker and Docker Desktop alike (`HOST/run.sh`
  says how). `CONTAINER_ENGINE` chooses `docker` or `podman` (default: `docker` when
  installed). `sh HOST/run.sh -c 'COMMAND'` runs a shell command line in the container instead,
  with `$RETRO_DL` naming the period software there; `sh HOST/run.sh -c sh` opens a shell.
  On Windows the script runs in Git Bash, whose `sh` it needs; `.gitattributes` keeps every
  file of the checkout byte-exact there too.

  The image is `linux/amd64`: the build is tested with Open Watcom's x86-64 Linux tools, which
  the MAKEFILE runs, so on an ARM machine such as a Mac with Apple Silicon the image runs under
  the engine's x86-64 emulation, several times slower. The emulator runs of the tests then
  may need longer time limits: `sh HOST/run.sh gate DOSTIME_SCALE=3` (see Time limits).
  The images are named `retroinv-build:<checksum>-<uid>-<gid>`; one left behind by an edited
  `Containerfile` is removed with `docker image rm NAME` (or `podman image rm NAME`).

  The container is tested on x86-64 Linux, with Docker (rootful) and Podman (rootless, also
  behind a `docker` command). Windows and macOS are expected to work but are not tested.

### Setting up

The period software is not part of the repository: you supply your own copies (the inventory
below). The commands run in a POSIX shell on the host, from the repository's root, after

```
export RETRO_DL="${RETRO_DL:-$HOME/retro-dl}"
```

A command shown as `sh HOST/run.sh -c '...'` runs in the container; natively, run what is
inside the quotes. Downloads happen on the host: the image has no `curl` or `unzip` (it has
`7z`).

1. Create `RETRO_DL` and copy into `"$RETRO_DL/archives"` the archives you have, under the file
   names `TEST/INTEG/unpack.sh` lists: for the commit gate Turbo C 2.01 (package `tc201`),
   Watcom C 6.5 (`wc65`), dBASE III Plus 1.1 (`dbase11`), MS-DOS 3.10 (`dos310`), whose
   shell checks also use MS-DOS 3.30 (`dos330`) and 5.00 (`dos500`) when present, and, for
   the Windows 3.0 test machine (`TEST/WIN/mkwin.sh`), Windows 3.0 (`win30`, SHA-256
   `1cf2d7f93aa7498d0454302c7959b01ca11dde5d28d357c8b52e5f9195455ff6`); for `make integ`
   also MS-DOS 3.30 and 5.00, the MS Network Client 3.0 (`msclient`) and the Workgroup Add-On
   (`wgaddon`); for `make tcp` MS-DOS 3.30 and the Client; for `make wintcp` those two,
   Windows for Workgroups 3.11 (`wfw311`, SHA-256
   `3ae197b7fccdc1a43a0559998757bc5483dd5d913d51f84fedc590d4ea92723f`) and Microsoft TCP/IP-32
   3.11b (`tcp32b`, `4dccdf918b78fc3ff2d60a0d957d971ec3971ad491fd4ec1031948487ca12b41`).

   ```
   mkdir -p "$RETRO_DL/archives"
   ```
2. Unpack them (Period archives, below); `unpack.sh` reports a needed archive that is missing
   (one missing only for goals you do not run can be ignored) and skips the optional ones:

   ```
   sh HOST/run.sh -c 'sh TEST/INTEG/unpack.sh "$RETRO_DL/archives" "$RETRO_DL"'
   ```
3. Fetch WATTCP and the packet driver (both below), check their SHA-256 and unpack them:

   ```
   w=https://dos.retropc.se/network/tcpip/wattcp
   mkdir -p "$RETRO_DL/wattcp/dl/retropc" "$RETRO_DL/pktd"
   curl -fsSL -o "$RETRO_DL/wattcp/dl/retropc/source.zip" $w/source.zip
   curl -fsSL -o "$RETRO_DL/wattcp/dl/retropc/apps.zip" $w/apps.zip
   curl -fsSL -o "$RETRO_DL/pktd/pktd11.zip" http://crynwr.com/drivers/pktd11.zip
   sha256sum "$RETRO_DL/wattcp/dl/retropc/"*.zip "$RETRO_DL/pktd/pktd11.zip"
   sh HOST/run.sh -c 'cd "$RETRO_DL" &&
       7z x -owattcp/x/source wattcp/dl/retropc/source.zip &&
       7z x -owattcp/x/apps wattcp/dl/retropc/apps.zip && 7z x -opktd pktd/pktd11.zip'
   ```
4. Install Turbo C 2.01 with the `mcopy` commands of its Install item (Turbo C 2.01, below),
   pasted into `sh HOST/run.sh -c sh`.
5. Rebuild Watcom C 6.5's damaged small- and compact-model libraries (Watcom C 6.5, below) with
   the published restoration, then install the compiler:

   ```
   git clone https://github.com/nrnelson/watcom-c-6.5-restoration.git "$RETRO_DL/wc65rest"
   cp "$RETRO_DL/archives/Watcom C.ver.6.5.English.zip" "$RETRO_DL/wc65rest/"
   sh HOST/run.sh -c 'cd "$RETRO_DL/wc65rest" && python3 -I restore.py --test &&
       mkdir -p "$RETRO_DL/wc65/RECON" && cp out/CLIBS.LIB out/CLIBC.LIB "$RETRO_DL/wc65/RECON/"'
   sh HOST/run.sh -c 'sh WC65/install.sh "$RETRO_DL/wc65" "$RETRO_DL/wc65/WATCOM"'
   ```
6. Install dBASE III Plus:

   ```
   sh HOST/run.sh -c 'sh TEST/DBASE/install.sh \
       "$RETRO_DL/dbase11/dBase III Plus 1.1 (1987) (3.5-720k)" "$RETRO_DL/dbase11/DBASE"'
   ```
7. Run the commit gate: `sh HOST/run.sh`, natively `make gate`.

## Tools to install

```
sudo apt install gcc libc6-dev make python3 7zip dosbox-x qemu-system-x86 qemu-utils nasm \
    mtools mscompress syslinux-common samba iproute2 netcat-traditional patch fdisk \
    curl ca-certificates xz-utils
```

Debian's `7zip` package provides the `7z` command (25.01 on Debian 13). `mscompress` provides
`msexpand` for the SZDD files of the MS Client and Windows 3.0; `syslinux-common` provides the
MBR boot code and `fdisk` the `sfdisk` that partitions the VM hard disks. `patch` applies
`SRC/TCP/WATTCP.DIF`; `curl`, `ca-certificates` and `xz-utils` fetch and unpack Open Watcom.
The mailbox VM's share needs `smbd` (`samba`), run as the calling user on a private
configuration, and `ss` (`iproute2`); it and the TCP gate need the traditional
(`netcat-traditional`) or the OpenBSD `nc`, which takes `-q` and does not half-close its
connection at the end of its input, so that the answers that follow still arrive.

- **Open Watcom v2**: the dated snapshot `2026-10-01-Build` (commit `e2856866`, compiler
  "Version 2.0 beta Oct 1 2026", `__WATCOMC__` = 1300), installed from the release tarball:

  ```
  mkdir -p "$RETRO_DL/watcom" && cd "$RETRO_DL/watcom"
  curl -sL -o ow-snapshot-2026-10-01.tar.xz \
    https://github.com/open-watcom/open-watcom-v2/releases/download/2026-10-01-Build/ow-snapshot.tar.xz
  sha256sum ow-snapshot-2026-10-01.tar.xz   # e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe
  sudo mkdir -p /opt/watcom
  sudo tar xJf ow-snapshot-2026-10-01.tar.xz -C /opt/watcom --no-same-owner
  ```

  The MAKEFILE sets `WATCOM=/opt/watcom` (override on the command line), passes `wcc` its
  headers with `-i=$WATCOM/h`, and puts `$WATCOM/binl64` (falling back to `binl`) first on
  `PATH`.
- **Period software**: none is part of the repository, and the user supplies their own copies
  of the packages of the inventory below. They live in one directory outside the repository,
  named by `RETRO_DL` (default `~/retro-dl`), one directory per package as
  `TEST/INTEG/unpack.sh` lays them out; the scripts treat it as untrusted. The build also
  writes there: the setup steps install Turbo C, Watcom C 6.5 and dBASE III Plus into it, and
  the DOSBox-X runs mount those installs as writable drives (dBASE opens a table of its
  install in place), so the directory must be writable.
- **WATTCP** in `"$RETRO_DL/wattcp"`: Erick Engelke's Waterloo TCP, release of 14 September
  1994. The archives are `source.zip` (library and sample sources for Borland C; SHA-256
  `9d46641bc7e806936d14736be8f3c4571d502eafcd9607f7816247edb29e4f89`) and `apps.zip` (sample
  executables; `635b4c7b8fba516cc582bf46445603ce78645c2f0f4321041976195a8ab8d0e7`) from
  `https://dos.retropc.se/network/tcpip/wattcp/`, kept in `dl/retropc/` and unpacked into
  `x/source/` and `x/apps/`. The older release `wattcp.zip` (newest file dated 1993-09-30;
  `ee8cd07b6fb1a174d87b013bccd7533b266194f31a9685add1f2c38eddc3d830`) is at
  `https://ftp.funet.fi/pub/msdos/communications/networking/wattcp/`, kept in `dl/funet/`. The
  TCP smoke test runs the sample executables of `apps.zip`. `make dostest`, `make tc` and
  `make wc65`, and so the test runs built on them, prepare WATTCP's kernel from `source.zip`
  for the programs that link it (see WATTCP under Compiler flags) and fail without it. Its
  licence, and what the project may therefore keep and distribute, are recorded in
  `DOC/DESIGN.md` (WATTCP licence).
- **Packet driver**: the Crynwr collection `http://crynwr.com/drivers/pktd11.zip`
  (SHA-256 `5dbf0d465c612c0750ad41368dc1d66876edc66f8471c9981a375779622b240c`;
  `NE2000.COM` 11.4.3, GPL) unpacked in `"$RETRO_DL/pktd"`. Used only inside the test VMs.
- **Period archives**: `sh TEST/INTEG/unpack.sh ARCHIVE_DIR "$RETRO_DL"`, `ARCHIVE_DIR` being
  the folder that holds the archives you supply, under the file names the script's table
  lists, extracts each into `"$RETRO_DL/<package>"`; the table also names the make targets
  that need each package. It refuses archives containing links, absolute paths or `..`,
  extracts atomically, skips packages already present and optional archives that are absent,
  and exits 1 if a needed archive is missing or any archive is refused or fails to extract.
  Under `HOST/run.sh` the archives sit in a mounted directory, such as `"$RETRO_DL/archives"`.
  The MS Client's `DSK3-1.EXE` and `DSK3-2.EXE` and TCP/IP-32's `TCP32B.EXE` are ZIP archives
  behind a DOS extractor stub, which `TEST/INTEG/mkimg.sh` and `TEST/WIN/mkwin.sh` read on the
  host without running the stub.

## Period software inventory

`TEST/INTEG/unpack.sh` unpacks each package into the directory of `RETRO_DL` named below. The
build and the tests read only the packages that have a use.

| Package | Directory | Images | Use |
|---|---|---|---|
| MS-DOS 3.10 (Compaq OEM) | `dos310` | 360K | the floor: every MS-DOS 3.10 boot of the tests and of the release check |
| MS-DOS 3.30 | `dos330` | 2 × 360K | the NetBIOS VM image; the DOS of the Windows for Workgroups and `win30nb` test machines; the boot floppy of the TCP probe (default) and of the release check's 720 KB, 1.2 MB and 1.44 MB disks; the shell checks of `T_BOOT.SH` when present |
| MS-DOS 5.00 | `dos500` | 3 × 720K | the WGA VM image; the shell checks of `T_BOOT.SH` when present |
| MS Network Client 3.0 | `msclient` | DSK3-1/2.EXE | NetBEUI of the NetBIOS VMs and of the `win30nb` test machine, the TCP/IP stack of the TCP/IP VM image (needs DOS 3.3 or later; its TCP/IP stack needs a 286) |
| Workgroup Add-On for MS-DOS | `wgaddon` | 1.44M | the peer file server of the WGA VM image |
| Turbo C 2.01 | `tc201` | 3 × 720K | release compiler |
| Watcom C 6.5 (1988) | `wc65` | 8 disks (zip) | primary release compiler; its damaged small- and compact-model libraries are replaced by a reconstruction (Watcom C 6.5, below) |
| Windows 3.0 SDK (1990) | `win30sdk` | 6 × 1.2M | the help compiler `HC.EXE` of disk 1, which `HC/install.sh` installs (Windows Help, below), for `winhelp` and `dist`, and `INVWIN`'s Windows Help run of `wintest` |
| Watcom C/C++ 9.5 (1993) | `wcpp95b` | 12 of its 20 × 1.44M and two patch zips | the period-compiler check of `INVWIN` (`wc95`, `wc95test`), installed by `WC95/install.sh` (Watcom C 9.5, below); `unpack.sh` does not unpack it |
| dBASE III Plus 1.1 | `dbase11` | 720K | the DBF, NDX and DBT compatibility proof |
| Windows 3.0 | `win30` | 7 × 720K | the Windows 3.0 test machines of `wintest` and `winnb` (`TEST/WIN/mkwin.sh`) |
| Windows for Workgroups 3.11 (OEM) | `wfw311` | 8 × 1.44M | the Windows for Workgroups test machine of `wintcp` (`TEST/WIN/mkwin.sh -w wfw311`) |
| Microsoft TCP/IP-32 3.11b | `tcp32b` | TCP32B.EXE | the TCP/IP stack and Winsock of that machine |
| MS-DOS 3.31, Workgroup Connection, MS-NET 1.0 beta (1984), Turbo C 1.0 and 1.5, Turbo C++ 1.01 and 3.0, dBASE III Plus 1.0, FoxPro 1.0 to 2.6a, Windows 3.0a | `dos331`, `wgconn`, `msnet10`, `tc10`, `tc15`, `tcpp101`, `tcpp30`, `dbase10`, `fox*`, `win30a` | images | not used by the build or the tests |
| Manuals (Turbo C 2.0, dBASE III Plus) | `tc20man`, `dbase11man` | PDF | reference |

## Makefile targets

`MAKEFILE` is the build; `GNUmakefile` only includes it, as GNU make on a case-sensitive host
does not look for an upper-case `MAKEFILE`. `SRCS.MK` gives each executable its memory model
(`<NAME>_MODEL`, `s` or `m`) and sources (`<NAME>_SRCS`); `EXES` names what Open Watcom builds
for DOS, `WIN_EXES` what it builds for Windows (Windows 3.0 programs, under Compiler flags).
`TC/mkbat.sh`, `WC65/mkbat.sh` and `WC95/mkbat.sh` read it too, so it holds only `NAME := words`
assignments (also `=` and `+=`), backslash continuation lines and `#` comments. A value may
refer to a variable defined earlier as `$(NAME)`; the generators expand it as make does for `:=`
and refuse one not yet defined. `ENG_CORE`, `PROTO_CORE` and `DBF_CORE` hold the `SRC/ENGINE`,
`SRC/PROTO` and `SRC/DBF` modules in link order. A host test is built from its `<NAME>_SRCS`, or
from `<NAME>_HOST` when it exists for the host alone: `T_COMPAT`, and each name under which a
program runs again with other arguments (`INVUSAGE`, `SRVUSAGE` and the others).

| Target | Does |
|---|---|
| `all` | Open Watcom cross-build of every executable in `EXES` into `BIN/` and of every Windows program in `WIN_EXES` into `BIN/WIN/`, with `BIN/QEXIT.COM`, `BIN/INV.MSG`, `BIN/INVSERV.MSG`, `BIN/INV.HLP` and `BIN/INVWIN.HTX` |
| `wsmoke` | the Windows smoke test `BIN/WIN/WSMOKE.EXE` alone, its NE header checked |
| `test` | `lint`, then the host tests side by side |
| `asan` | `test` under the address and undefined-behaviour sanitizers |
| `lint` | the source, message, help, assembler and document checks |
| `dbcheck` | sample tables checked by `CHKDBF.PY` and by real dBASE III Plus 1.1 |
| `dostest` | the DOS unit tests and the INV scripts as Open Watcom programs, in DOSBox-X and on MS-DOS 3.10 |
| `dostest1` | `dostest` one run at a time (`DOS_JOBS` and `DOSBOX_JOBS` 1), none taken from the run cache |
| `wintest` | the Windows programs on the Windows 3.0 test machine, in standard and 386 enhanced mode under QEMU |
| `winhelp` | `BIN/INVWIN.HLP`, INVWIN's Windows Help file, compiled by the Windows 3.0 SDK's `HC.EXE` in headless DOSBox-X (Windows Help, below); not part of `gate` |
| `winhelpchk` | `winhelp`, then two more compiles of its source, which must give the same file (`TEST/hcchk.py`); not part of `gate` |
| `winimg` | the test machine's disk alone (`TEST/WIN/mkwin.sh`), which `wintest` builds first |
| `wintcp` | the Winsock gate: `WSOCKT`, `WSKT` and `INVWIN /TCP:` on the Windows for Workgroups 3.11 test machine through `INVTCP` into `INVSERV`, both of the Open Watcom build, on two VMs of the NetBIOS image, which it builds when absent or out of date; not part of `gate` |
| `wfwimg` | the Windows for Workgroups machine's disk alone (`TEST/WIN/mkwin.sh -w wfw311`), which `wintcp` builds first |
| `winser` | the Windows programs' SERIAL link between two machines under QEMU: `WSERT` and `INVWIN` on COM1 of the Windows 3.0 test machine, in standard and 386 enhanced mode, all side by side, against `INVSERV` on COM2 of MS-DOS 3.10 (`TEST/WIN/winser.sh`); part of `integ`, not of `gate` |
| `winnb` | the NET link of the Windows programs between two machines (`TEST/WIN/winnb.sh`): `WNET` and `INVWIN` on the `win30nb` test machine against `INVSERV` on a NetBIOS VM, in standard and 386 enhanced mode under QEMU; it builds the NetBIOS VM image when absent or out of date, and `win30nb`'s disk in `WINNBIMG_DIR` (default `OBJ/WIN30NB/`, `mkwin.sh -w win30nb`); part of `integ`, not of `gate` |
| `winmbx` | the MBOX link of the Windows client between two machines (`TEST/WIN/winmbx.sh`): `INVWIN /MBOX:` on the `win30nb` test machine against `INVSERV`'s mailbox on the share of a Workgroup Add-On VM, in standard and 386 enhanced mode under QEMU; it builds the Add-On's VM image when absent or out of date, and `win30nb`'s disk as `winnb` does; part of `integ`, not of `gate` |
| `tc`, `wc65` | Turbo C 2.01 or Watcom C 6.5 build of every executable in `TC_EXES` or `WC65_EXES` into `BIN/TC/` or `BIN/WC65/`, in headless DOSBox-X |
| `tctest`, `wc65test` | the `tc` or `wc65` build, then its DOSBox-X stage |
| `wc95` | Watcom C 9.5 build of every Windows program in `WC95_EXES` into `BIN/WC95/`, in headless DOSBox-X; not part of `gate` (Watcom C 9.5, below) |
| `wc95test` | the `wc95` build, then its programs on the Windows 3.0 test machine as `wintest` runs them, against the same goldens; not part of `gate` |
| `tc310`, `wc65310` | the `tc` or `wc65` build, then its MS-DOS 3.10 stage |
| `integ` | its parts one after another, each a goal of its own (`INTEG_PARTS`), so that a gate runner may run them side by side in trees of their own; every part runs, and `integ` fails when any did: `nbgate`, `winser`, `winnb` and `winmbx`, which build the disks of the `win30` and `win30nb` test machines (`winmbx` the Workgroup Add-On image too) when absent or out of date, then `integow`, `integtc` and `integwc65`. The period disks come from `DOS330_DIR`, `MSCLIENT_DIR`, `DOS500_DIR`, `WGA_DIR` and `WIN30_DIR` (default under `$RETRO_DL`) |
| `nbgate` | builds the NetBIOS VM image when absent or out of date, runs `shellgate`, the NetBIOS gate (`nbgate.sh`, then `nbgate.sh -x`) and `wgagate.sh -n` |
| `integow`, `integtc`, `integwc65` | the Open Watcom, Turbo C or Watcom C 6.5 build, made first, and the NetBIOS VM image as `nbgate` builds it, then on that build the server gate at its memory floors (once with INV, once with the network-only client), the screen gate of INV and of the network-only client, each over COM1 and NET at its floor, the starvation gate, the Samba mailbox gate and the Workgroup Add-On gate |
| `stvgate`, `smbgate`, `wgagate` | one gate of `integ` alone on each build; `wgagate` first runs `shellgate` and `wgagate.sh -n` |
| `shellgate` | the image shell check alone; it builds the NetBIOS, WGA and TCP/IP images when absent or out of date |
| `tcp` | the TCP probe of each build on MS-DOS 3.30 and 3.10, all six at once (logs `OBJ/TCP/<build><DOS>.LOG`), and meanwhile each build's TCP gate in turn at its memory floor; not part of `integ` |
| `gate` | the commit gate: the targets of `GATE_TARGETS` side by side, then `dosserial`, then each one's exit status |
| `dosserial` | the `<NAME>_DOSSERIAL` runs of the emulator targets of `DOS_SERIAL_TARGETS` (default: those of `GATE_TARGETS`), one at a time, each alone among the emulators |
| `fullgate` | `gate`, then `dostest1` once the other targets have ended |
| `dist` | the `tc` and `wc65` builds, `INVWIN` and `winhelp`, then the release floppy images in `DIST/` (Release) |
| `distcheck` | `dist`, then the images and the zip read back, every compressed file expanded by `msexpand`, each set installed from its images by `INSTALL` on MS-DOS 3.10 or 3.30 and the installed DOS programs run |
| `lzcheck` | `dist`, then every file of the disks, as it is, compressed by `REL/szdd.py` and expanded by `T_LZEXP` and by `msexpand` (`TEST/szddchk.py`) |
| `clean` | removes `BIN/` and `OBJ/` |

- `all` checks each map with `TEST/mapchk.sh` and the objects of each program of `NEAR_EXES`
  (INV and THIN) with `TEST/OMFNEAR.PY` against `TEST/NEARBUD.TXT` (Near data, below). nasm
  assembles `BIN/QEXIT.COM`; `TEST/msgchk.py` builds `BIN/INV.MSG` and `BIN/INVSERV.MSG` with
  one stamp from `INV_MSGS` and `SRV_MSGS`, and `OBJ/WIN/MSGNUM.H`, every message's constant
  defined as its number, for the resource scripts that name messages (`SRC/WIN/WLABEL.RC`);
  `TEST/hlpchk.py` checks and writes `BIN/INV.HLP` and INVWIN's `BIN/INVWIN.HTX`, each with an
  index of its sections at the head and CR LF line ends, and for `winhelp` the Windows Help
  source of `INVWIN.HTX` (Windows Help, below).
- `test` runs in a sub-make, side by side (`-Otarget` keeps each one's output together), in the
  jobs of an outer `make -j` or else `HOST_JOBS` at a time (default `nproc`):
  - every program of `HOST_TESTS`, built by gcc with the host flags, which must exit with its
    `<NAME>_HOSTRC` (default 0). One that writes files gets a scratch directory under
    `OBJ/HOST/`, and runs in it when it has `<NAME>_HOSTCWD`. A test of `MALLOC_TESTS` links
    with `malloc` and `free` wrapped, one with `<NAME>_HOSTWRAP` with each function it names
    wrapped (`--wrap`), so that it sees the calls. A program still running after `HOST_TIMEOUT`
    seconds (600 unless set on the command line or in the environment) is killed and fails as
    timed out; one that outlives the SIGTERM by the grace of `timeout -k`, or is killed from
    outside, fails as killed by SIGKILL;
  - `TEST/T_DUMP.SH`, `TEST/T_INV.SH` and `TEST/T_SCR.SH` (Programs and scripts); `CHKDBF.PY
    --strict` over the dBASE-written goldens and over the backup `T_ADMIN` leaves of its packed
    and reindexed data; `TEST/DBASE/chkbad.py`; `TEST/colchk.py`, the column drawing of
    INVWIN's snapshot goldens, their headings' states and `INVWINCW`'s stored widths against
    values worked out by hand, `TEST/cbochk.py`, their
    drop-down lists against the seeded code lists and choice names, `TEST/tabchk.py`, the
    tab strip's tabs, current tab, focus and notifications in `WTABT`'s golden, and
    `TEST/labchk.py`, the form dialogs' labels and access letters against texts worked out by
    hand, and every dialog's access letters shown once, `TEST/frmchk.py`, the form
    dialog's pages, buttons, captions and reserved letters in `WFORMTX`'s golden,
    `TEST/chrtchk.py`, the tool bar's buttons, grays, slot and notifications, a press that
    loses its capture, a double click and a glyph's grayed colour, the status bar's panels
    and texts, the splitter's drags, limits, notification, guide and cancels, and the preview
    panel's text, lines, cuts, scroll bar, focus rectangle, keys and capacity in `WCHRT`'s
    golden, the focus never the bars', the splitter's only while a drag is held and the
    panel's after a click on it,
    `TEST/toolchk.py`, the tool bar's bitmap that `SRC/WIN/INVTOOL.PY` writes against its
    glyphs' art and the rule of their grayed forms, the same bytes on every run,
    `TEST/menuchk.py`, the help texts of INVWIN's menu entries in its resource scripts (one
    for each entry shown, within its length limit, none left over) and its menu bar, marks,
    grayed entries and Record pop-up in its snapshots against lines worked out by hand,
    `TEST/framchk.py`, INVWIN's tool bar texts and commands against its Record menu's in the
    sources, and its tool bar, Find box and status bar panels and the split view's panes in
    every snapshot of its frame and in cases worked out by hand, and `TEST/splitchk.py`, the
    split view's selection, contents, focus, count, Record menu and widths in the goldens of
    its key scripts, and its settings key in `INVWINHA`'s settings save, `TEST/headchk.py`,
    the list windows' headings in every golden (each `HEAD` line's button face, the `COLS`
    line after it, its slots and states, and the pieces in them), `WLISTTV`'s clicks and drags
    against a model of the stored widths, and no `WINCOLS` line in `INVWINHA`'s settings save,
    `TEST/dlgchk.py`, every dialog procedure of `SRC/WIN` starting with the `WD_LOOKS`
    head of `WDLG.H`, so that each dialog has the grey 3D look, and `TEST/mousechk.py`, the
    test driver's mouse lines: `WTDRV2M`'s and `WTDRV2Q`'s pads replayed on a model of where
    each message goes, its point, its buttons, its `SHIFT-`, `CTRL-` and `ALT-` prefixes and
    the window holding the mouse, the refused and missing targets of `WTDRV2H`..`J`, `R`, `S`
    and `N`..`P`, and the right clicks, pane clicks, tool buttons and drags of `INVWINMR`,
    `MF`, `MT` and `MW` against states worked out by hand, with the settings `INVWINMW` leaves,
    `TEST/rtfchk.py`, the Windows Help source that `hlpchk.py --rtf` writes for fixture help
    files, read back as RTF (pages, footnotes, paragraphs, tables, jumps, the index, field
    sections), its project's context numbers, and its refusals, `TEST/dragchk.py`, rows
    dragged: the drop target, drops moving an item or doing nothing, a refused drop, the
    cancels, the threshold, a double click and level 1 in `INVWINDA`, `DR`, `DX`, `DT` and `DL`, and
    `WLISTTX`'s and `WLISTTY`'s notes, marks and ends of a drag, against states worked out by
    hand, `TEST/selchk.py`, several rows chosen: in every golden a `DRAW` line under the
    first and each selected row of a list window and several selected only in the frame's
    lists that take them, and the selections, counts, dialogs, stop boxes, notes and rows
    changed of `INVWINSL`, `SM`, `SN`, `SO` and `DS` against states worked out by hand, and
    `TEST/szddchk.py`, files of its own and the sample tables of `TEST/GOLDEN/DB3/`
    compressed by `REL/szdd.py` and expanded by `T_LZEXP` back to their bytes;
  - the checks of the test machinery: `TEST/T_RCACHE.SH` (the run cache's keys and store,
    without an emulator), `TEST/T_WATPR.SH` (`SRC/TCP/WATPREP.PY` on synthetic archives),
    `TEST/T_UNPACK.SH` (`TEST/INTEG/unpack.sh` on synthetic archives: the needed, optional,
    missing, unsafe and corrupt ones, and a second run; on a host without `7z`, which
    `unpack.sh` runs, it says it is skipped and passes),
    `TEST/T_ASMCK.SH` (`TEST/ASMCHK.PY` on small assembler and C pairs), `TEST/T_NECHK.SH`
    (`TEST/NECHK.PY` on synthetic Windows programs), `TEST/T_SHLIB.SH` (the time limits,
    errorlevel batch lines and tool-output check of `TEST/shlib.sh`), `TEST/T_VMGATE.SH` (the
    two-VM gates' failure reports and exit statuses and the screen gate's runs, with stand-ins
    for the VMs) and `TEST/T_TOOLS.SH` (the host `NBTEST`'s and `COMTEST`'s command lines,
    output and exit statuses).
- `asan` makes `test` with `HOST_SAN` (`-g -fsanitize=address,undefined
  -fno-sanitize-recover=all`) on every host compile and link and the objects under `OBJ/ASAN/`
  (`HOSTOBJ`, default `OBJ/HOST/`). An access out of bounds or to freed memory, a leak or
  undefined behaviour fails the test that does it, with the sanitizer's report in
  `OBJ/ASAN/SAN.<pid>` (a file, as some tests take over their standard error). It shares the
  scratch directories of `test`, so the two do not run at once in one tree.
- `lint` runs:
  - `TEST/lint.py` over every C source and header, with `--compat` for the files built without
    the strict-ANSI switch (Compiler flags), `--win` for the Windows sources among them (Windows
    3.0 programs), `--kernel` over `WATTCP_KERNEL` (WATTCP), `--docs` over the documents of
    `DOC_LINT_FILES` and the texts of `REF_LINT_FILES`, `--names` over the documents of
    `NAME_LINT_FILES` and `--scripts` over the screen
    tests: the `th_scripts` tables of
    `T_SCRE.C` and `T_SCRR.C` against their key scripts in `TEST/KEYS/`,
    the goldens of `TEST/GOLDEN/SCR/` and `TEST/GOLDEN/PRT/` that `T_SCRE`, `T_SCRR` and `T_RPT`
    compare, `SCRE_KEYS`, `SCRR_KEYS`, the two tests' boot files, and the mono goldens of their
    `<NAME>_DOSBOXDATA` against their `SF_MONO` rows;
  - `TEST/msgchk.py` over the message sources;
  - `TEST/hlpchk.py` over `INV.HLP` and `SRC/WIN/INVWIN.HTX`: line widths against `TUI_HELPW`
    (`TUI.C`) or `WH_HELPW` (`WHELP.H`), the sections of `INV.HLP` against `TUI_HELPLINES`, topic
    names and index lines against `MSG_IDXLINE` (`MSG.H`), no line before the first section
    starting with `=` (the index's mark), no empty section, a section for every topic the
    program's sources use and a use for every section, and every "See also" a list of other
    sections' titles, and for a form with field sections one for each of its fields, naming
    its width after "up to " and its choices; for `INVWIN.HTX`, built for Windows Help too,
    titles that make keywords, the index title within `HC.EXE`'s field, characters of cp1252
    and one context number per topic;
  - `TEST/ASMCHK.PY`, which checks the machine code in C against its nasm source;
  - `TEST/T_CHK.SH`, which feeds faulty fixtures to `msgchk.py`, `hlpchk.py`, the checks of
    `lint.py` (user-visible text, `cmem_heapsz`, the WATTCP headers and kernel list, the private
    headers, line length, the Windows keywords, incomplete structs, block initializers, screen
    tables, documents, program names), `TEST/mapchk.sh` and `WC65/toolarge.sh`, and expects
    each fault refused.
- `lint.py --docs` fails a passage held twice, in one document or in two of one extension: a
  paragraph, list item, table row or heading of `DOC_UNIT` characters or more, or `DOC_RUN`
  lines in a row of `DOC_LINE` characters or more; code fences are not compared. It fails a
  reference, in a document or a text of `--refs`, to a document that does not exist or to a
  heading, bold label or table row label that the document lacks, and a document's reference
  to a label of its own, above or below, that is not on that side.
- `lint.py --names` holds the documents of `NAME_LINT_FILES` (`README.md`, `DOC/INSTALL.md`,
  `DOC/CLIENT.md`), which name the programs for their readers, to the build's names: each must
  name `THIN` of `SRCS.MK`, and every word `INV` followed by letters or digits must be a program
  of `EXES` or `WIN_EXES`, so that renaming a program leaves no old name behind. A word followed
  by `.C` or `.H` names a source module (`INVNOL.C`), so the documents write a module so, and
  the English words of `NOT_NAMES` (`INVALID`, ...) are no program names either.
- `dbcheck` writes sample tables with `TEST/MKITEMS.C`, checks them with `CHKDBF.PY --strict`,
  then runs `TEST/DBASE/check.sh`, `packchk.sh` and `csvchk.sh` (Compatibility, below), each
  holding the emulator lock shared. It needs the install of `TEST/DBASE/install.sh`.
- `dostest` runs the emulator check and `TEST/T_BOOT.SH` (Programs and scripts), then its two
  stages on the Open Watcom build:
  - DOSBox-X: every program of `DOS_TESTS` (built into `BIN/TEST/`) through `TEST/rundos.sh`,
    those of `DOS_MONO_TESTS` again with a Hercules adapter, `TEST/T_INV.SH -d dosbox`, and
    `TEST/T_SCR.SH` with a CGA and with a Hercules adapter;
  - MS-DOS 3.10 under QEMU: every boot of every unit test with its extra runs (DOS unit),
    `TEST/T_INV.SH -d qemu`, `TEST/T_SCR.SH -d qemu`, `TEST/T_PRN.SH`, `TEST/INTEG/mbox310.sh`;
    `INVSERV.EXE` and `INVTCP.EXE` without a NetBIOS provider, which must exit with
    `INVSERV_NONBRC` and `INVTCP_NONBRC`; `INVTEST.EXE` running `TEST/INTEG/NODISK.SCR`, whose
    mailbox is on an empty `B:`, which must exit with 1 and print `dj_critpass` (`mailbox
    failed: FIND-REQ, DOS error n`) without a critical-error prompt; and `COMTEST.EXE` with
    `COMTEST_COMARGS` on a 16550A as COM2, which must exit with 0 and print `COMTEST_COMPASS`.

  Every INV script runs with the free memory of `INV_FREEKB_OW` (Runtime requirements).
- `wintest` runs every program of `WIN_EXES` in the two kinds of run of `WIN_KINDS`: Windows 3.0
  in standard and 386 enhanced mode on QEMU's 486, each through `TEST/WIN/wintest.sh` (Windows
  tests, under Programs and scripts) with its settings of `SRCS.MK`. The test machine's disk is
  made first by `winimg`, in `WINIMG_DIR` (default `OBJ/WIN30/`), only when its inputs have
  changed, holding the emulator lock shared for its boot check. A program with key scripts runs
  once per script, its snapshot compared with the script's golden byte for byte (`WTDRV`, and
  `WTDRV2` and `INVWIN` with each of theirs), but for the `=STACK UNUSED=n` line the test driver
  (`TEST/WIN/TDRV.H`) writes last, which `wintest.sh` and the integration scripts leave out and
  hold to `STACK_MARGIN` (`TEST/T_SCRH.C`, `snap_check` of `TEST/shlib.sh`), as `T_SCR.SH` does;
  a `WTEST` report's `STACK` line is held to the same margin; any other is judged by its report:
  `WSMOKE`'s `PASS`, `WSOCKT`'s `NOWINSOCK`, the unit tests' `PASS n` with the same count in
  every mode. `MAKEFILE` keeps the real-mode kinds beside them, real mode on QEMU's 486 and on
  DOSBox-X's 8086 with a CGA, out of `WIN_KINDS` while the programs are built for protected mode
  only (`DOC/DESIGN.md`, Why INVWIN needs protected mode).
- `tctest` and `wc65test` run the DOSBox-X stage on their build: every DOS unit test it built
  (those with `<NAME>_NODOSBOX` excepted), `TEST/T_INV.SH -d dosbox` and `TEST/T_SCR.SH` with a
  CGA and with a Hercules adapter, the scripts with `INV_FREEKB_TC` or `INV_FREEKB_WC65` free.
- `tc310` and `wc65310` run the emulator check and the MS-DOS 3.10 stage of `dostest` on their
  build, with the same per-test settings and that build's free-memory figures. The data files,
  among them the host-built `BIN/INV.MSG` and `BIN/INV.HLP`, are shared by every compiler.

A run's log is `OBJ/<NAME>.<ext>`, or `OBJ/<NAME>.<boot>.<ext>` for one boot of a test with
several, with `TC.`, `WC65.` or `WC95.` before the name for a period build. `<ext>` names the
kind of run: `CGA` and `HGC` (DOSBox-X, by adapter), `310` (a boot on MS-DOS 3.10, and
`INVTEST`'s empty-`B:` run), `SHR` (with `SHARE`), `NOD` (`T_CFILE` with an empty `B:`), `PG1`
and `WID` (`T_VIDEO` on page 1 and with 81 columns), `NNB` (without NetBIOS), `MBX`
(`mbox310.sh`), `COM` (`COMTEST`) and `LPT` (`T_PRN.SH`). The scripts' logs are named after them
(`OBJ/T_INV.CGA`, `OBJ/T_SCR.HGC`, `OBJ/T_INV.310` and so on); the emulator check writes
`OBJ/WRAPCHK.310` (`OBJ/TC.WRAPCHK.310`, `OBJ/WC65.WRAPCHK.310`) and `TEST/T_BOOT.SH`
`OBJ/T_BOOT.LOG`. A run of `wintest` writes `OBJ/<NAME>.<ext>`, or `OBJ/<NAME>.<x>.<ext>` for
the key script `TEST/<NAME><x>.KEY`, `<ext>` being `WS` or `W3` (standard or 386 enhanced mode
under QEMU), or `WR` and `WCG` for the real-mode kinds; its directory, `OBJ/RUN/<log name>/`,
stays only when it failed. A run that times out leaves there the program's files as they were on
its disk (the test driver commits each snapshot as it writes it, so a hung run's `.SNP` holds
the sections written before the hang) and, under QEMU, `OBJ/RUN/<log name>.PNG`, its screen, and
`OBJ/RUN/<log name>.MON`, the monitor's `info registers` and `info pic`, all taken with the
machine stopped at the limit; one whose QEMU ended early leaves `OBJ/RUN/<log name>.RST`, QEMU's
log of the CPU's resets, quits and run states (Windows runner). DOSBox-X has no monitor, so its
runs leave only the files.

INV's key-script runs and every `INVSERV` run measure the stack they never used (`cstk_fill` at
the start, `cstk_unused` at the end): INV writes the figure as the `=STACK UNUSED=n` line of its
snapshot file, `INVSERV` as the `STACK` event of its event log. `TEST/T_SCR.SH`,
`TEST/T_INV.SH` and the screen gate fail an INV run that leaves fewer than `STACK_MARGIN`
(`TEST/T_SCRH.C`) bytes, `TEST/INTEG/mbox310.sh` and the two-VM gates (`log_check` of
`TEST/INTEG/vmgate.sh`) an `INVSERV` run that leaves fewer than `SRV_STACK_MARGIN`
(`TEST/shlib.sh`). Under Turbo C the figure counts from the end of the near heap's reservation
(`_heaplen` of the program's near-heap module) and is 0 once the heap has grown past it, so every
build is held to the same margin. The stack of `INV /CMD` is not measured.

### Emulator stages, the lock and the gate

The emulator runs of `dostest`, `tctest`, `wc65test`, `tc310` and `wc65310` go in two stages,
and those of `wintest` and `wc95test` in one, each one goal of a sub-make with its own `-j`. The
DOSBox-X stage runs `DOSBOX_JOBS` at a time, by default half of `DOS_JOBS`; the MS-DOS 3.10
stage and the Windows stage `DOS_JOBS` at a time, by default the host's logical CPU count less
2; each at least 1. Every QEMU command line passes `-accel "$QEMU_ACCEL"` (`TEST/shlib.sh`),
whose `tb-size` caps TCG's translation cache: left at its default, the cache grows each process
to about 1.2 GB resident whatever the guest's memory.

`make dostest DOS_JOBS=1` (which also makes `DOSBOX_JOBS` 1), or `make dostest1`, runs them one
at a time in list order, to tell a failure that depends on host load. The MS-DOS 3.10 runs then
stop at the first failure (also under `make -k`); the DOSBox-X runs all run.

- **Output.** Each run writes its own log and, once it passed, `<log>.ok` beside it, and prints
  one `passed:`, `cached:` or `FAILED:` line as it ends. When a stage is done its logs are
  printed in list order (test order, then boot order), then those of the failed runs again under
  their names; the target fails if any run failed, and a DOSBox-X failure stops it before its
  MS-DOS 3.10 stage. The serial steps (the emulator check, the `CONFIG.SYS` variants) come
  first.
- **Isolation.** `rundos.sh`, `boot310.sh`, `T_INV.SH`, `T_SCR.SH`, `T_PRN.SH` and `runwin.sh`
  make their scratch and kept directories with `mktemp`, a unit test's DOSBox-X run copies the
  program and its data files into `OBJ/RUN/<log name>/`, which `rundos.sh` mounts read-only as
  `C:`, and a Windows run makes its own directory there. So concurrent runs, and targets
  started together, share no file they write, and a test reaches only the files it is given.
- **Order.** The runs of `DOSBOX_LONG_TESTS`, `DOS_LONG_TESTS` and `WIN_LONG_TESTS`
  (`SRCS.MK`, longest first) start first in their stage. A test with `<NAME>_DOSSERIAL`
  (those that measure time) has its runs go first, one at a time, before the others start;
  under `make gate` (`DOS_SERIAL_LATER`) the stages leave them out and `dosserial` runs them.
- **The emulator lock** `OBJ/DOSRUN.LCK` (`flock`) is held by every run, shared, or exclusive
  for the `<NAME>_DOSSERIAL` tests; the emulator check, `TEST/T_BOOT.SH`, the dBASE runs of
  `dbcheck`, `winimg` and `wfwimg` (`TEST/WIN/mkwin.sh`) and the `tc`, `wc65` and `wc95` builds
  (while DOSBox-X runs) hold it shared. An exclusive taker passes through `OBJ/DOSRUN.Q`
  first, as every taker does, so it waits only for the runs already going, and then runs alone
  among the emulators of every target of the tree. Other host load, such as compilers and host
  tests, may still run beside it. A taker that does not get the lock within `DOS_LOCK_WAIT`
  seconds fails, with a `FAILED:` line naming the lock.
- **Sub-makes** keep the command line's variables but not the outer jobserver. `make -j all` and
  `make -j test` give the same results as without `-j`; do not combine `clean` with other goals
  under `-j`.

`make gate` runs the commit gate: each target of `GATE_TARGETS` in its own make, its output in
`OBJ/GATE/<target>.LOG` (`make gate GATE_TARGETS="lint test"` runs only those), then each
target's exit status; it fails if any target failed or did not run.

- One make first builds what several targets need (the message and help files, every Open Watcom
  program and test, the host test programs, the `CONFIG.SYS` variants), its output in
  `OBJ/GATE/PREP.LOG`; should it fail, the targets run one at a time.
- The targets start side by side in the order of `GATE_START`. `tctest` and `tc310` wait for
  `tc`, `wc65test` and `wc65310` for `wc65` (which then runs even when not named), use that
  build (`TC_BUILD`, `WC65_BUILD` empty), and are not run when it failed. `asan` waits for
  `test` when the gate runs it, as the two share their scratch directories.
- The emulator targets leave their `<NAME>_DOSSERIAL` runs out: each such run waits for every
  run of the tree going, which would hold up the stages running side by side. Once all the
  targets have ended, `dosserial` runs them together, one at a time, its output in
  `OBJ/GATE/dosserial.LOG`, leaving out those of a target whose build failed.
- All targets, their emulator runs included, share one jobserver of `DOS_JOBS` slots
  (`DOS_POOL_SHARED`); the period builds run `TC_JOBS` and `WC65_JOBS` instances besides. `make
  gate DOS_JOBS=1` runs everything one at a time.
- The targets of `GATE_SERIAL` (none unless set) run after `dosserial`, one at a time. `make
  fullgate` is `make gate GATE_SERIAL=dostest1`, which adds `dostest` with every run one after
  another, in `OBJ/GATE/dostest1.LOG`.

### The run cache

The run cache lets an emulator run of `dostest`, `tctest`, `wc65test`, `tc310`, `wc65310`,
`wintest` or `wc95test` stand on an earlier pass of the very same run. It is on when
`DOS_CACHE_DIR` names a directory (off by default); trees on one host may share it, never hosts.
`TEST/runcache.sh` keeps it.

- **The key** of a run is the SHA-256 of a manifest, kept beside its log as `<log>.KEY`, which
  `TEST/runcache.sh` and `dj_keyx` of `MAKEFILE` list in full: the run's command line and pass
  pattern; the SHA-256 of every file it names, of a DOSBox-X run's own directory, for the INV
  scripts of `TEST/KEYS/`, `TEST/GOLDEN/` and the files beside INV, and for a Windows run of
  the test machine's `INPUTS.SHA`, which covers `TEST/WIN/runwin.sh`; the emulators, the boot
  floppy, the host's packages, time zone and locale, `DOSTIME_SCALE`, `DOS_CACHE_SALT` (text a
  host adds to keep its results apart), `MAKEFILE` and the test scripts; and the sources a
  script reads through `cdefine` or `srcs_vars` of `TEST/shlib.sh`, found by reading the
  scripts. A read that names no file turns the cache off for the stage, and a command line that
  does not start `sh <script>` leaves its run uncached. Paths are relative to the tree, so trees
  at different places share keys.
- **A hit** is a pass stored under the key, with the same manifest, at most `DOS_CACHE_DAYS`
  days old (default 7, `TEST/runcache.sh`), so a result that depends on the date is checked
  again. Its stored log becomes the run's log behind a line `=== from the run cache: passed
  <time> in <secs>s, key <key>`, and it prints `cached:`. Each stage, and the gate's status line
  of each target, says how many runs came from the cache.
- **Never cached**: the tests with `<NAME>_DOSSERIAL` and those of `DOS_NOCACHE_TESTS`
  (`SRCS.MK`), whose results can change between two runs of the same files, always run and are
  not stored. The emulator check, `TEST/T_BOOT.SH`, the builds, `winimg` and `dbcheck` are not
  cached.
- **`DOS_CACHE`** is `1` (default: take hits, store passes), `write` (run everything, store the
  passes) or `0` (off). `make fullgate` and `dostest1` run with `write`.
- **A contradiction**: a run that fails where the cache holds a pass for its key removes that
  pass and prints `RUN CACHE CONTRADICTION`; the gate counts these. One seen without the cache
  shows an input the key misses, or a test whose result changes between runs.
- **The store**: an entry is the file `DOS_CACHE_DIR/v1/<two hex digits>/<key>`: a header line,
  the manifest, a `---` line and the log. It is written under a temporary name and renamed into
  place, so gates storing one key at once leave one whole entry. Each stage first removes the
  expired entries.

### Time limits

`DOSTIME_SCALE`, a whole number from 1 to 9 (default 1; unset or empty means 1, any other value
stops make or the script with an error), multiplies the host-side time limit of every emulator
run of the gate's targets and of `dbcheck`:

- a unit test's MS-DOS 3.10 boot (`dj_qtime` of `MAKEFILE`: `<NAME>_DOSTIME` where `SRCS.MK`
  sets it) and its DOSBox-X run (`dj_dbtime`, longer for a period compiler's build);
- the limits inside `TEST/T_INV.SH`, `TEST/T_SCR.SH` and `TEST/T_PRN.SH`, and the dBASE runs of
  `dbcheck`;
- a Windows run of `wintest` or `wc95test` (`<NAME>_WINTIME` where `SRCS.MK` sets it, else
  `WIN_TIME` of `MAKEFILE`), that of `wintcp` and the boot check of `TEST/WIN/mkwin.sh`, all
  through `TEST/WIN/runwin.sh`;
- the period builds' `TC_TIMEOUT`, `WC65_TIMEOUT` and `WC95_TIMEOUT`, and `DOS_LOCK_WAIT`;
- the `TIMEOUT` of `TEST/INTEG/run2vm.sh`, `tcpprobe.sh`, `tcpgate.sh`, `smbgate.sh`,
  `smbprobe.sh`, `stvgate.sh` and `wintcp.sh`, and the `-t` of `TEST/WIN/winser.sh`, the limit
  of its machines and of each step of its relay `WSRELAY.PY`.

The setup steps keep their fixed limits: the dBASE install (`DB_TIMEOUT` of
`TEST/DBASE/install.sh`), the Watcom C 9.5 install (`WC95/install.sh`) and the VM image builds
(`mkimg.sh`, `mkwga.sh`). Each script multiplies the limit it is given once, with `dos_secs` of
`TEST/shlib.sh`, and its timeout message names both figures (`timed out after 600s (300s x 2)`).
The limits only stop a hung emulator, so a slower or busier host raises the scale without losing
coverage: the tests that time themselves do so on guest time and run alone (`<NAME>_DOSSERIAL`).
The gate's start line names the scale when it is not 1.

## Compiler flags

- Open Watcom: `wcc -0 -bt=dos -os -s -zq -wx -we -zc -i=$WATCOM/h -i=<dir>... -m<model>`, plus
  `-za` (strict ANSI) for every file outside `SRC/COMPAT/` and for the portable modules of
  `COMPAT_PORTABLE` (`SRCS.MK`) inside it, WATTCP's kernel and the files of `WATTCP_USERS`
  (WATTCP, below) and the Windows sources (Windows 3.0 programs, below) excepted. `-we` makes
  every warning an error.
  - The `-i` directories are those of the source's layer (`INC_<dir>` in `MAKEFILE`, plus
    `INC_<path>` for a single source), so an `#include` of a header from a layer above fails.
    The client sees `SRC/COMPAT` and `SRC/CLIENT` only, `LINK.C` and `LINKLOC.C` adding
    `SRC/PROTO`, `SRC/ENGINE` and `SRC/DBF`, `LINKNOL.C` the last two; in `SRC/COMPAT` the
    schema tables `CSCHEMA.C` and `CSCHDAT.C` and the host tool `CSCHBIN.C` add the engine's and
    DBF's, the DOS-only tests `T_DTA.C`, `T_CLOCK.C`, `T_NHEAP.C`, `T_CWATT.C` and `T_UART.C` add
    `TEST`, `T_MSGLO.C` adds `TEST` and `SRC/CLIENT`, and the Windows-only test `T_WCOMP.C` adds `TEST` and the engine's and DBF's; `TEST`
    sees all.
  - Objects go to `OBJ/WC/<model>/<source path>.OBJ` with a `.D` dependency file, so a header
    edit rebuilds what includes it. Link: `wlink option quiet system dos option
    map=BIN/<NAME>.MAP option stack=<n>`, `n` being `_stklen` of `SRC/COMPAT/CDOS.C` (the Turbo
    C stack), which the makefile reads; any wlink warning fails the link.
  - The programs of `WC_ELIM` (`MAKEFILE`: those of `EXES` but `INV`, `THIN` and `INVSERV`) take
    their objects from `OBJ/WC/<model>z/`, compiled with `-zm` (a segment per function), and
    link with `option eliminate`, which drops every function nothing references; `INV`, `THIN`
    and `INVSERV` call most of what they link, and `-zm` makes them larger (THIN by 1.6 KB). The
    period compilers have neither switch. `TEST/mapchk.sh` then checks each map (Near data,
    under Programs and scripts), and the link fails when the map lacks a function of the
    program's `<NAME>_MAPSYMS` (`SRCS.MK`): `INVTCP` must link `wt_isn`, which only
    `WATTCP.DIF`'s change to the kernel calls, so a `WATTCP.DIF` without that change fails the
    build.
- `-za` does **not** reject `__far`, `__near`, `__interrupt` or `//` comments (it predefines
  `_NO_EXT_KEYS`), and the Watcom C library reaches INT 21h function 68h (DOS 3.3 commit)
  through `fsync()`, `_commit()`, `_dos_commit()` and fopen mode `"c"`. `make lint` therefore
  rejects, in every file built with `-za`, the forbidden constructs of `DOC/STYLE.md` (Hard
  rules), among them the names `COMPAT.H` keeps for `SRC/COMPAT/` (`COMPAT_INTERNAL`, the
  far-pointer helpers, the interrupt-flag helpers, `BDA_SEG`).
- In every file, `--compat` ones included, `lint.py` rejects:
  - `//` comments;
  - `#elif`, as Watcom C 6.5 enables the wrong group after a taken `#if`;
  - a `const` pointer to a struct incomplete where it is written, in a parameter list outside a
    function body or a typedef's declarator, which Watcom C 6.5 refuses in a definition (E091);
  - an initializer in a declaration inside a block, other than a `static` one (Layout of
    `DOC/STYLE.md`);
  - a compile-time check by a negative array size (`[(cond) ? 1 : -1]`, `[1 - 2 * !(cond)]`, in
    a typedef or in `sizeof`), which Watcom C 6.5 and Turbo C 2.01 accept silently. Such a check
    is written as two declarations of one array, `extern char x[1];` then `extern char x[(cond)
    ? 1 : 2];`, which every compiler refuses when the sizes disagree; an array never referenced
    adds no data;
  - an `#include` of a WATTCP header (`TCP.H`, `WATTCP.H`, `ELIB.H`, `ETHDEV.H`, `ICMP.H`,
    `BOOTP.H`, `COPYRIGH.H`, with or without a directory) outside `WATTCP_USERS` (given as
    `--wattcp`), of `SRC/DBF/DBFINT.H` outside `SRC/DBF/` and `TEST/`, of
    `SRC/CLIENT/SCRSHR.H` outside `SRC/CLIENT/SCR*.C` and `TEST/`, and of
    `SRC/CLIENT/PAGERINT.H` outside `SRC/CLIENT/PAGER.C`, `SRC/CLIENT/PAGERSV.C` and `TEST/`;
  - a line over 79 columns (Layout of `DOC/STYLE.md`).

  Comments and string literals are skipped when matching code; the header checks read each line
  as written. `lint.py` also checks that the row names of `SRC/ENGINE/TXTAB.H` are those of
  `TEST/TXNUM.TXT` in order, new rows after them (whose names are then appended to `TXNUM.TXT`),
  so that a text keeps its number. In `SRC/CLIENT/`, `SRC/SERVER/` and `SRC/TCP/` it rejects a
  string literal holding a lower-case letter (escapes and `printf` conversions aside) as
  user-visible text outside the message files, except in the files of its `PROSE_FILES` table,
  which write start-up and usage errors on standard error.
- Host (gcc): `-std=c89 -pedantic -Wall -Wextra -Wconversion -Werror -DHOST_LINUX -I<dir>...`,
  with the Watcom build's directories, each source wrapped in `-x c … -x none` because gcc
  treats an upper-case `.C` file as C++.

### WATTCP

- `make` prepares WATTCP's build tree `OBJ/WATTCP/` (`WATTCP_DIR` of `SRCS.MK`, git-ignored)
  with `python3 -I SRC/TCP/WATPREP.PY <archive> SRC/TCP/WATTCP.DIF OBJ/WATTCP`, whose header
  gives its steps: the archive's SHA-256 checked, the kernel modules and headers extracted, our
  changes applied with GNU `patch` (fuzz 0, no offset), the mechanical changes made, and the
  stamp `OBJ/WATTCP/.ok` written last. The archive is `$RETRO_DL/wattcp/dl/retropc/source.zip`.
  The stamp and the kernel modules (`WATTCP_KERNEL`) are the targets of one rule, which depends
  on the archive, `WATTCP.DIF` and `WATPREP.PY`, so a change to one of them prepares the tree
  again and rebuilds the modules' objects. `lint.py --kernel` checks, without preparing the
  tree, that `WATTCP_KERNEL` names exactly the modules of `WATPREP.PY`'s `KERNEL` list.
- Only what compiles WATTCP's code waits for the tree: under Open Watcom the objects of the
  kernel modules and of `WATTCP_USERS`, and `make tc` and `make wc65` as a whole when a program
  they build links the kernel; the gate's `PREP` step makes it once. Without the archive these
  fail, the period builds before any compile, and `WATPREP.PY` names the path and the SHA-256
  the archive must have.
- The kernel modules and the files of `WATTCP_USERS` (`SRCS.MK`: `TSWAT.C`, `CPKT.C`, `CWATT.C`,
  `T_CWATT.C`) are built without the strict-ANSI switch and with WATTCP's byte packing and
  signed `char`, so their structs have WATTCP's layout, which `CWATT.C` checks at compile time
  with the sign of `char`. Open Watcom adds `-zp1 -j`; Turbo C's defaults are both already;
  Watcom C 6.5 adds `/j` and always packs to one byte. The kernel modules are built without
  warnings (Open Watcom `-w0`, Turbo C `-w-`, Watcom C 6.5 `/w0`), as WATTCP's 1994 C is changed
  only as `WATTCP.DIF` needs; Watcom C 6.5 still warns W008 ("Missing return value") for
  `icmp_Reply` of `PCICMP.C` and `sock_close` of `PCTCP.C`, which `WC65/runwc.sh` does not
  count. Our files keep their usual warnings.
- Include paths: the kernel modules read `OBJ/WATTCP/INCLUDE` and `SRC/COMPAT` (for `WTSHIM.H`);
  the files of `WATTCP_USERS` add `OBJ/WATTCP/INCLUDE` to those of their directory. The period
  builds copy the prepared tree into their work trees with `SRC/` and `TEST/`: the kernel
  modules to the same paths, its headers flat into `INC` with the others, where a header of ours
  or a generated one named like a WATTCP header is refused.

### Windows 3.0 programs (Win16)

- The programs of `WIN_EXES` (`SRCS.MK`) run on Windows 3.0 and later, in standard and 386
  enhanced mode, so on a 286 or later; Windows 3.0 refuses to start them in real mode
  (`DOC/DESIGN.md`, Why INVWIN needs protected mode). Open Watcom builds them with its Windows
  headers (`$WATCOM/h/win`), libraries (`$WATCOM/lib286/win`) and resource compiler `wrc`
  (`$WATCOM/binl64`), which the build host's image keeps (`HOST/Containerfile`). `make all`
  builds each into `BIN/WIN/<NAME>.EXE` beside its map, so the gate builds them too;
  `make wsmoke` builds the smoke test alone. Besides `<NAME>_SRCS` and `<NAME>_MODEL`, a
  program has `<NAME>_RC`, its resource script. The DOS period builds take only `TC_EXES` and
  `WC65_EXES`; Watcom C 9.5 builds those of `WC95_EXES` too (Watcom C 9.5, below).
- Compile: `wcc -0 -os -s -zq -wx -we -i=$WATCOM/h -bt=windows -zW -dWINVER=0x0300 -dSTRICT
  -i=$WATCOM/h/win -i=<dir>... -m<model>`, plus `-za` as for DOS but for the Windows sources,
  `WIN_NOANSI` of `MAKEFILE` (`SRC/WIN/` and the Windows test programs of `TEST/`), as
  `windows.h` needs the compiler's keywords; `SRC/WIN/` reads the headers of `SRC/COMPAT` and
  `SRC/CLIENT`.
  Objects go to `OBJ/WIN/<model>/<source path>.OBJ`, so a module shared with a DOS program has
  a Windows object of its own.
  - The functions Windows calls need the entry code that loads DS from AX, which `-zW` gives
    every function declared `__export`, and wlink exports each of them. That code works only in
    an exported function: its first instructions copy DS into AX, and the loader patches them
    out for exported entries alone, so that DS comes from the AX that Windows or a
    `MakeProcInstance` thunk sets. So every window, dialog, timer or other callback procedure is
    declared `WCALLBACK` (`SRC/COMPAT/CWIN.H`, `FAR PASCAL __export`); `lint.py --win` bars
    `PASCAL` anywhere else (the lint paragraph below says what it cannot see). `-zw` would give
    the entry code to every `FAR PASCAL` function too, which buys nothing without the export and
    costs bytes in `WinMain`.
  - `-zW` also gives every far function that calls another an odd saved BP (a leaf, which can
    trigger no move, has no frame), the mark of the far frames whose return addresses
    real-mode Windows mends, but not the saved DS at BP-2 that it expects there too; hence the
    mark for protected mode only (Link, below; `DOC/DESIGN.md`, Why INVWIN needs protected
    mode).
  - `WINVER` 0x0300 leaves out of `windows.h` what Windows 3.0 lacks, so a later call does not
    compile; `STRICT` gives each kind of handle a type of its own, so a handle passed for another
    does not compile either. Without `-zc`, constants stay in DGROUP, out of the discardable
    code.
- Resources: `wrc -q -bt=windows -r -i=$WATCOM/h/win -i=<dir>... -fo=OBJ/WIN/<NAME>.RES`
  compiles `<NAME>_RC`, again when it or a header beside it changes. A program that links the
  engine binds its schema data, which a Windows program cannot keep as far data (`FARD` of
  `COMPAT.H`), as the discardable resource `CSCH_RES` of type `CSCH_TYPE`: the host tool
  `SRC/COMPAT/CSCHBIN.C`, built with the arrays of `CSCHDAT.C`, writes them into
  `OBJ/WIN/CSCHDAT.BIN` (`CSCH_BLOB` of `MAKEFILE`) in the layout of `SRC/COMPAT/CSCHDAT.H`, the
  program's resource script (`INVWIN.RC`, `WLISTT.RC`, `WFORMT.RC` or `TEST/WTESTE.RC`) names it,
  and `CSCHEMA.C` reads it (`SRC/COMPAT/` below); the host test `T_SCHBIN` checks the file
  against the arrays.
- Link: `wlink option quiet system windows option map=BIN/WIN/<NAME>.MAP` with the stack
  `WIN_STACK` of `MAKEFILE`, or `<NAME>_WINSTACK` of `SRCS.MK` for a program whose deepest
  calls need more (`INVWIN`), the initial local heap `WIN_HEAP` of `MAKEFILE`, `option
  packcode=$(WIN_PACKCODE)`, so that the code comes in segments Windows can drop and load
  again on their own, and the segment classes of `WIN_SEGMENTS`: `CODE` moveable, discardable
  and loaded on its first call; `BEGDATA`, `DATA`, `BSS` and `STACK`, the classes of DGROUP,
  moveable. Any warning fails it. wlink leaves a Win16 segment fixed and preloaded unless told,
  and its `TYPE CODE` and `TYPE DATA` forms set the segments of the other kind, hence the
  classes; DGROUP is moveable only when all four are. `wrc -q -bt=windows -30
  OBJ/WIN/<NAME>.RES BIN/WIN/<NAME>.EXE` then binds the resources and marks the program as
  written for Windows 3.0 (expected version 3.00; 3.10 without `-30`, which only this second
  pass heeds); `-t` marks it for protected mode only, so Windows 3.0 refuses to start it in
  real mode.
- `TEST/NECHK.PY` checks the NE header of each program as it is built, and its header gives the
  fields: target Windows, expected version 3.00, an application with a data segment per
  instance marked for protected mode only, every code segment moveable and discardable,
  every data segment moveable and not discardable, the heap and stack sizes, every import a
  function that `TEST/W30EXP.TXT` lists, the exports of the Windows 3.0 modules, and for a
  program with an icon of its own (`NECHK_ICON_<NAME>`) its group icon. The C
  library's `sprintf`, `time`, `ctime` and `getenv` import `IsDBCSLeadByte` (KERNEL.207), which
  Windows 3.0 lacks, so the Windows sources format with `wsprintf`. `TEST/T_NECHK.SH` (`make
  test`) feeds the script synthetic programs with each fault.
- `lint.py --win` holds the Windows sources to the `--compat` checks and the keyword checks:
  the memory model and calling convention are written with the macros of `windows.h` (`FAR`,
  `NEAR`, `PASCAL`, `CDECL`) and `WCALLBACK`, never as keywords (`far`, `__far`, `pascal`,
  `__export` ...) nor as `WINAPI` or `CALLBACK`, which the Windows 3.0 headers lack; `PASCAL`
  stands only before `WinMain` and in a pointer to a `FAR PASCAL` function (a callback written
  without it, then cast to `FARPROC`, still passes lint and the compiler).
- `INVWIN`, the client for Windows (`SRC/WIN/INVWIN.C`, `INVWIN.RC`, `INVWIN.H`), links the engine
  in-process behind `LINK.C` and `LINKLOC.C`, and the NET, SERIAL, TCP and MBOX backends
  (`LINKNET.C` and `NBCLI.C`, `LINKSER.C` and `SERCLI.C`, `LINKTCP.C`, `LINKMBX.C` and
  `MBXCLI.C`, with `RESPRD.C`), with the client modules it shares with INV: `ARGSW.C` (`ARGS.C`
  with `/TCP:` and `/MBOX:`) for its switches, `SESS.C` for the login, `INVMENU.C` for its menu
  bar, `ITEMLS.C` and `PAGER.C` for its item list, `SCRIFORM.C` and `FMODEL.C` for its item
  form, `SCRLFORM.C` for its location tree and form, `CODETAB.C` for its code lists and the type
  names, `SCRMFORM.C` for its service lists and form, `SCRNFORM.C` for its loan
  lists and form, `SCRSFORM.C` for its search, `SCRAFORM.C` for its parts list and assembly
  requests, `SCRZFORM.C` for its user list and form and its audit log, `SCRCFORM.C` for its export
  and import, `SCRRFORM.C`, `RPTRUN.C`, `RPT.C` and `PRINT.C` for its reports and labels,
  `MEMOIO.C` and `MIOTEXT.C` for their notes, `RQERR.C`, `RQ.C`, `CFG.C` and `MSG.C`, whose
  `INV.MSG` it reads at run time and whose Windows object, like the DOS ones, waits for
  `OBJ/MSGSTMP.H`; its location tree `WLOC.C`, the panes beside its lists `WPANE.C`, service
  screens `WSVC.C`, loan screens
  `WLOAN.C`, search `WSRCH.C`, assembly commands `WASSY.C`, users screen `WUSER.C` and the other
  commands of its Admin menu `WADMIN.C`, screens of the frame of `WSCR.H`, whose panes `WPANE.C`
  gives, the items' in the preview panel `WPREV.C`; its export and import
  `WCSV.C`; its reports `WRPT.C`, whose dialogs and strings `WRPT.RC` gives `INVWIN.RC`, printing
  through `CPRNW.C`; its settings `WSET.C`, with the texts of `SCRSET.H`; its help `WHELP.C`,
  which shows the topics of `INVWIN.HLP` in Windows Help, or the sections of `INVWIN.HTX`, built
  from `SRC/WIN/INVWIN.HTX` as `INV.HLP` is, beside the program; the list window `WLIST.C`,
  with the columns of its rows `WCOLS.C`, its rows `WROWS.C`, whose column headers and
  `WIN.INI` names `WROWS.RC` gives `INVWIN.RC`, with the Control Panel's date and number
  formats of `INTL.C`, and the survey that counts them `PAGERSV.C` with its key set
  `KEYSET.C`; the form dialog `WFORM.C` and the pick lists of its code and location fields
  `WPICK.C`, whose strings, labels and pick list template `WFORM.RC` gives `INVWIN.RC`; the code
  lists of the categories and the location types `WCODE.C`; the file name dialog `WFILE.C`, whose
  template and strings `WFILE.RC` gives it; the password controls of `WEDIT.C`; the dialogs' place and list boxes of columns `WDLG.C`;
  and the test driver `TDRV.C`, which its `/KEYS:` and `/SNAP:` switches start. `INVWIN.H` gives its windows
  and their IDs. Its key scripts `TEST/INVWIN.KEY` and `TEST/INVWIN<x>.KEY` run on the data
  `TEST/T_SCR.SH` seeds (`INVWINRL.KEY`, `INVWINHS.KEY` and `INVWINRE.KEY` on sixty more items,
  `TEST/KEYS/RLLONG.REQ`, `INVWINRF.KEY` on forty bins in a box, `TEST/KEYS/DEEP.REQ`,
  `INVWINPY.KEY` on an installed part and an item with the longest notes,
  `TEST/KEYS/PVNOTES.REQ`, `INVWINPT.KEY` on three assemblies, `TEST/KEYS/PANEA.REQ`,
  `INVWINPS.KEY` on thirty, `TEST/KEYS/PANEB.REQ`, `INVWINPR.KEY` on one built of assemblies,
  `TEST/KEYS/PANEC.REQ`, `INVWINPH.KEY` on six loans and holds, `TEST/KEYS/PANEL.REQ`,
  `INVWINPU.KEY` with a second administrator, `TEST/KEYS/PANEU.REQ`, `INVWINBA.KEY` and
  `INVWINBC.KEY` on items of three categories, `TEST/KEYS/BYCAT.REQ`, `INVWINBB.KEY` on
  forty-eight categories, `TEST/KEYS/BYCATL.REQ`, and the sixty items; `INVWINEA.KEY` to
  `INVWINEI.KEY`, which choose every row of a list, on 257 more, but `INVWINEF.KEY` on 4094,
  `INVWINEH.KEY` and `INVWINBL.KEY` on 4117 and `INVWINEI.KEY` on 300, in request files that `MAKEFILE` has
  `TEST/WIN/mkitems.sh` write to `OBJ/ITEMS<n>.REQ` rather than keep them in the tree, and
  `INVWINEJ.KEY`, which chooses every row beside a pane and in one, on the three assemblies), with
  `BIN/INV.MSG`, `BIN/INVWIN.HTX` (so help shows in its own window), the settings files
  `TEST/KEYS/ORDLOC.CFG`, `ORDSER.CFG`, `PAGE20.CFG`, `MBXSTA.CFG`, `MDSTA.CFG` and the files to
  import `TEST/WIN/IMPMIX.CSV` and `IMPLOC.CSV` beside it, against
  `TEST/GOLDEN/WIN/INVWIN[x].SNP`: the logins at each level and their menus, refused and failed
  logins, the forced password change, log-out, the item list's
  moves and orders, its order kept over new sizes of the frame, the item form's view, edit, add
  and delete at each level, its view switched to edit by Ctrl+E and by its Edit button, items
  duplicated by Ctrl+D and a copy left unchanged by Esc deleted again, the item pick lists of a
  lend and an
  install, the location and item pick lists longer than their window, the row count on the status
  bar as the list opens, changes order, gains and loses records and is resized, on lists that fit
  their window and on longer ones, a search's results and a location's contents among them, and
  after a restore, the notes of
  changes to items, locations, service records, loans and users on the status bar, the location
  tree and form likewise and the moves between the tree and the item list, new locations inside
  the selected one, their names, codes and capacities refused, locations deleted and refused for
  what they hold, the tree's letters, its moves made and refused, the form's pick lists of the
  type and of Inside and its path, a location's contents, opened on everything below it, and its
  own items, the categories and the location types added, renamed, reordered and deleted and their
  deletes refused, the service
  lists and form likewise, an item's records opened from the item list and
  from the item form and back, the loan lists, lends refused and made from them and from an
  item's loans, returns refused and made, the loan keys at each level, an item's parts with
  installs, moves and removals refused and made and their notes, the assembly keys at each
  level, a chain of parts walked down through their forms and back, the assemblies and the
  parts beside them (`PT`, which must leave `TEST/GOLDEN/WIN/PANEA.CFG`), an item's history
  beside the loans and holds (`PH`, which must leave `PANEL.CFG`), a user's audit entries
  beside the users (`PU`, which must leave `PANEU.CFG`), the categories with their items
  beside them, their list paged by the client, a category added, renamed, reordered and
  deleted there or in the Categories dialog over them, and its items as a screen of their
  own (`BA`, `BB` and `BC`, which must leave `BYCAT.CFG`, `BYCATW.CFG` and `BYCATR.CFG`), the search
  by text, serial number and each filter and its results at each level, the users added, edited,
  reset and deleted and the server's refusals of the last administrator's, the operations on the
  data files, the audit log and the sessions, the Esc of the user list and the audit log back to
  the list each was opened from, the Admin menu below level 3, exports and imports refused and
  made, the file name dialog's directories and refusals, the reports and labels: their dialogs
  with a refused file and a replace refused, the preview paged through, a label refused for an
  item that does not exist, and a report and labels printed on the test machine's printer, what
  it printed compared with `TEST/GOLDEN/WIN/INVWINRC.PRN` and `INVWINRD.PRN`, then a listing
  over two pages of the default length and labels over four of the printer's pages (`RE` and
  `RF`, against `INVWINRE.PRN` and `INVWINRF.PRN`); the settings
  saved, shown again and refused, the fields of the settings, search and report dialogs reached
  by their access letters, the help of the screens and dialogs, the Help menu with Getting
  started, and About, F10 opening the menu bar and Alt+F4 quitting, the
  notes of the forms shown, typed, refused past their limits and saved, Windows ending its
  session with a dialog open, the NET and SERIAL links refused on a machine without NetBIOS or a
  serial port, the TCP link on Windows 3.0, which has no Winsock, and the MBOX link for a
  station too long for a mailbox, for none of its own, whose directory does not exist and
  without its `REQUEST` directory, and a settings file DOS refuses to read; the runs of
  `J`, `N` and `O` name a settings file by `/CFG:` (`INVWIN_WINARGSx`), whose `ITEMORD=` opens
  the list, those of `RA`, `RB` and `RC` `PAGE20.CFG`, its pages 20 lines long and its labels 20
  apart, that of `HB` one in a directory that does not exist, those of `0`, `5` and `TC`
  `/NET RISRV`, `/COM1` and `/TCP:10.0.2.15`, that of `MB` `/MBOX:MBOX` with `MBXSTA.CFG`, whose
  `STATION=` is that station, that of `MC` `/MBOX:MBOX` alone, those of `MD` `/MBOX:MBOX` and
  `ME` `/MBOX:DATA` with `MDSTA.CFG`, whose `STATION=` fits, and that of `HF` `/CFG:DATA`, a
  directory; `CA` must leave its exports equal to
  `TEST/GOLDEN/WIN/CALOCS.CSV`, `CAITEMS.CSV`, `CABARE.CSV` and `CALTYPES.CSV`
  (`INVWIN_WINCMPCA`), and `HA`
  its settings file, the default data directory left unset, equal to `TEST/GOLDEN/WIN/INV.CFG`
  (`INVWIN_WINCMPHA`). `make winser`, `make winnb`, `make winmbx` and `make wintcp` run it over
  those links to a server with the key script `TEST/WIN/INVLINK.KEY`, against
  `TEST/GOLDEN/WIN/INVSER.SNP`, `INVNB.SNP`, `INVMBX.SNP` and `INVTCP.SNP`; `winser` also sets two
  users' passwords with `TEST/WIN/INVUSER.KEY`, against `INVUSR.SNP`, and goes back to a list
  longer than a window with `TEST/WIN/INVBACK.KEY`, against `INVSERB.SNP`; `winnb` calls a name no
  station has with `TEST/WIN/INVCALL.KEY`, against `INVCALL.SNP`, and `wintcp` also has its TCP
  connection refused and its wait ended by Esc (`TEST/WIN/INVFAIL.KEY`). Its icon, a 32 x 32
  picture in the 16 colours of the VGA, is drawn as text in `SRC/WIN/INVICON.PY`, which writes
  it to `OBJ/WIN/INVWIN.ICO` for `INVWIN.RC`, as an image is not kept in the repository; its
  drag cursor, 32 x 32 in black and white, likewise in `SRC/WIN/INVDRAG.PY`, which writes
  `OBJ/WIN/INVDRAG.CUR`.
- The smoke test `TEST/WSMOKE.C` (`WSMOKE.RC`, `WSMOKE.H`) registers a window class, makes a
  window whose title comes from its string table, writes `WSMOKE.OUT` in its current directory
  (the Windows and DOS versions, the mode from `GetWinFlags`, the free global memory and `PASS`
  once Windows has called its window procedure) and ends the session with `ExitWindows`; its
  header gives the lines.
- The Winsock test `TEST/WSOCKT.C`, bound to `TEST/WTEST.RC`, reaches Winsock as a TCP link of
  a Windows program must where Windows may have none: only in protected mode, only when
  `OpenFile` finds `WINSOCK.DLL` on Windows' search path (Windows 3.0 shows a box for a missing
  library), then through `LoadLibrary` and each entry by name with `GetProcAddress`. It sends
  `HELLO`, `VER` and `BYE` to the address and port of its arguments on a non-blocking socket
  that `WSAAsyncSelect` drives, a timer bounding the exchange, and writes `WSOCKT.OUT` (its
  header gives the lines). `wintest` runs it on Windows 3.0, where every mode must find no
  Winsock; `wintcp` on Windows for Workgroups 3.11 through `INVTCP`.
- `TEST/WSKT.C` (`WTEST.RC`) tests the Windows programs' TCP connection, `CWSOCK.C`, as its
  header gives: on Windows 3.0 (`wintest`) the refusal without Winsock; under `wintcp` a port
  refused, a wait for a silent address that runs out, and a session whose answers and close
  have reached the stack before it reads them, every byte read before the end. `TEST/WTEST.C`
  sets the program's instance, `dos_inst`, for the connection's window.
- The test driver `SRC/WIN/TDRV.C` is the Windows counterpart of INV's `/KEYS:` and `/SNAP:`: a
  program hands its two switches to `td_init`, and a timer of the driver's, which every message
  loop dispatches (dialog boxes and open menus too), types the key script into the program,
  and posts its mouse messages (`M` lines, routed through the window holding the mouse), and
  has its active window painted again as after a switch to another program lying over it and
  back (`C` lines, the next snapshot marking each list row of which the screen shows nothing
  `BLANK`),
  whenever it is idle and writes snapshots of its windows, menus and list rows as text, with the
  pieces that owner-drawn lists and their headers draw in columns, as `WCOLS.C` records them for
  it, the same in every mode of Windows; `TDRV.H` gives the script and snapshot formats. When the
  script ends, the driver ends the program's loop, and the program, once it has closed its files,
  calls `td_done`, which writes the last line and ends Windows when `/SNAP:` was given, so that a
  stray `/KEYS:` leaves Windows running; a `Q` line that every window agrees to always ends
  Windows. A run that `td_init` refuses (a file not opened, a script line not understood, no
  timer) ends there, as `td_done` ends one, whatever the program does with the quit. A program whose dialogs `WDLG.C` places (INVWIN, WFORMT) calls `td_place`, and a dialog
  that opens elsewhere fails its run (`PLACE`). It needs `CDOS.C`, `CTEXT.C`, `WCOLS.C` and
  `CMEM.C`, whose far block holds the widths that `WCOLS.C` lays columns out in. Its test
  `TEST/WTDRV.C` (`WTDRV.RC`, `WTDRV.H`) is a small dialog program, which `TEST/WTDRV.KEY` types
  into: an edit control, a check box, a modal dialog with a password, menus by `ALT` and by
  accelerators, work that holds the driver through `td_busy`, and the exit. Run as `WIN WTDRV
  /KEYS:WTDRV.KEY /SNAP:WTDRV.SNP`, it writes `TEST/GOLDEN/WIN/WTDRV.SNP` byte for byte in
  standard and 386 enhanced mode; run as `WIN WTDRV /PLACE`, `TEST/WTDRVP.KEY` gives
  `TEST/GOLDEN/WIN/WTDRVP.SNP`, its main window, which stands where its template puts it, failing
  `PLACE`; run as `WIN WTDRV /DROP`, which drops the quit waiting before its loop,
  `TEST/WTDRVD.KEY`, its second snapshot name too long, gives `TEST/GOLDEN/WIN/WTDRVD.SNP`, the
  script refused. `TEST/WTDRV2.C` (`WTDRV2.RC`, `WTDRV2.H`) is a second proof, written from `TDRV.H`
  alone: list boxes, a combo box, multi-line and password edit controls, the `SHIFT-`, `CTRL-` and
  `ALT-` prefixes, a message box, OEM text and the keys of an `A` line, typed ahead when the
  program asks for them. Its `WTDRV2.KEY` gives
  `TEST/GOLDEN/WIN/WTDRV2.SNP`; its other scripts, `WTDRV2<x>.KEY` with
  `TEST/GOLDEN/WIN/WTDRV2<x>.SNP`, give the format cases (`Z`, `L`) and the failures of `TDRV.H`
  (`A`..`D`, `G` a script line refused; `F` focus, `Y` busy, `E` ended; `T` timer, run as `WTDRV2
  /HOG ...`, which takes every timer first; `W` a `W 1` line that a 1.5 s Busy overruns), and its
  mouse lines: `M`, run as `WTDRV2 /MOUSE ...`, which adds pads that report each mouse message
  they get, clicks, drags across two windows and double-clicks them, and `Q`, run so too, the
  `SHIFT-`, `CTRL-` and `ALT-` prefixes of its events; `H`, `I`, `J`, `R` and `S` have a mouse
  line refused, `N`, `O` and `P` one whose window or place is not there (`TARGET`), and `X` one
  after the program has ended (`ENDED`); `1`, run as `WTDRV2 /BLANK ...`, which adds a list row
  without text, has the `C` line paint the window again and the next snapshot mark that row
  `BLANK`.
- The list window `SRC/WIN/WLIST.C` (`WLIST.H`, which gives its contract) shows one paged
  command through `PAGER.C` in a child window: an owner-drawn list box in `ANSI_VAR_FONT` that
  holds only the rows of the pager's window, drawn with its header in the columns of
  `SRC/WIN/WCOLS.C` (`WCOLS.H`), the keys of INV's list view, a scroll bar over the list's
  length, which the survey of `SRC/CLIENT/PAGERSV.C` counts on a fast link, one across its
  columns when they are wider than the window, waits that dispatch the messages and stop on Esc, and failures in a message box with the texts of `rqe_class`. Its
  test `TEST/WLISTT.C` (`WLISTT.RC`) opens the LOCAL link on a data directory seeded as
  `TEST/T_SCR.SH` seeds its runs, logs in as `ADMIN` and shows the item list two rows high,
  counted. Its owner keys, which its header gives, click, stop a walk, make a failure, post the
  scroll bar's gestures, open the list again, counted or not, and report the pieces a row's
  columns cut, and its title counts the busy moves and shows the bar's position and range and
  the list's count. `TEST/WLISTT.KEY` pages through, sorts, clicks, reloads, stops, filters and
  makes fail, and `WLISTTM.KEY` seeks rows by letters and by `wl_find` and shows a lead row, and
  `WLISTTN.KEY` destroys the list while its End walk waits, and `WLISTTV.KEY` clicks the
  headings and drags the dividers with posted mouse messages, `TEST/headchk.py` checking it
  against a model of the widths, and `WLISTTW.KEY` drags a divider less than half a character,
  counts the rows that stored widths cost (`wl_rows`) and opens the list without dividers, and
  `WLISTTX.KEY`, with `/DRAG` (the list opened with `WL_DRAGS`), drags its rows, and `WLISTTY.KEY`
  likewise once the window has scrolled within eleven items, `TEST/dragchk.py` checking both, and
  `WLISTTZ.KEY`, with `/MULTI` (the list opened with `WL_MULTI`) on 260 items, chooses every row:
  what keeps and ends the choice and what a choice stopped, failed or refused leaves chosen and
  counted, `TEST/selchk.py` checking it; run as `WIN WLISTT TDATA
  /KEYS:WLISTT.KEY /SNAP:WLISTT.SNP` beside `INV.MSG`, each writes its golden
  `TEST/GOLDEN/WIN/WLISTT[x].SNP` byte for byte in standard and 386 enhanced mode.
- `SRC/WIN/WEDIT.C` (`WEDIT.H`) gives the edit controls Windows 3.0 cannot make alone: password
  controls that keep no copy of what is deleted, read-only ones, and controls that hold CP437
  text as it is, in `OEM_FIXED_FONT`.
- `SRC/WIN/WDLG.C` (`WDLG.H`) centres each dialog but the report preview over its owner as it
  opens, or over the screen when it has none or its owner is minimized, kept on the screen, as
  Windows 3.0 has no `DS_CENTER`. It draws the owner-drawn list boxes of the code lists and the
  sessions, and their static headers, in the columns of `WCOLS.H` and widens the dialog for a
  row; as a static of Windows 3.0 cannot be owner-drawn, a header's replaced procedure sends its
  dialog `WM_DRAWITEM` itself.
- The tab strip `SRC/WIN/WTAB.C` (`WTAB.H`, which gives its contract) is a child window drawn with
  the GDI alone, as Windows 3.0 has no tab control: its window text holds its tabs, which a click,
  the arrows, Ctrl+PgDn, Ctrl+PgUp, Ctrl+Tab and a tab's mnemonic, with Alt or alone on a control
  that takes no characters, choose, all but the first two handed to it by its owner's message
  filter (`wt_key`), and the
  test driver writes its current tab on its line. Its test `TEST/WTABT.C` (`WTABT.RC`, `WTABT.H`)
  opens a dialog holding a strip, an edit control and a static that counts the strip's
  notifications; its header gives the keys that click the strip and set its tabs. Run as `WIN
  WTABT /KEYS:WTABT.KEY /SNAP:WTABT.SNP`, `TEST/WTABT.KEY` writes `TEST/GOLDEN/WIN/WTABT.SNP` byte
  for byte in standard and 386 enhanced mode.
- The splitter `SRC/WIN/WSPLIT.C` (`WSPLIT.H`, which gives its contract) is a child window
  four pixels wide between INVWIN's location tree and its contents, drawn with the GDI alone. A
  drag takes the mouse and draws an inverted guide over the panes, Esc or `WM_CANCELMODE` ends
  it without a move, and the release moves the splitter and tells its owner. The test driver
  writes its place after `AT` and the limits of a drag after `LIMITS`.
- The preview panel `SRC/WIN/WPREV.C` (`WPREV.H`, which gives its contract) is a child window
  that paints a record's fields and notes, read-only, beside INVWIN's item lists: its lines sit in
  one far block, the labels in a column and the values and notes broken at words, with a vertical
  scroll bar, a focus rectangle, and every key it does not scroll by sent to its owner. The test
  driver writes its text on its line, its scroll bar after `VBAR`, and each of its lines as a `PV`
  line under it.
- The tool bar `SRC/WIN/WTOOL.C` and the status bar `SRC/WIN/WSTAT.C` (`WTOOL.H` and `WSTAT.H`,
  which give their contracts) are child windows drawn with the GDI alone, as Windows 3.0 has
  neither. The tool bar's window text lists its buttons, gaps and the slot its owner fills; each
  button is an owner-drawn push button whose mouse the bar takes itself, so that a click never moves
  the focus, and whose glyph it draws from the bitmap resource `WTOOL`. The glyphs, 16 x 16 in the
  16 colours of the VGA with black outlines that read in black and white, and their grayed forms are
  drawn as text in `SRC/WIN/INVTOOL.PY`, which writes them to `OBJ/WIN/INVTOOL.BMP` for the resource
  scripts that bind them, INVWIN's and WCHRT's. The status bar's window text holds its panels'
  texts. The test driver writes each bar on one line, a tool bar's disabled buttons marked, a status
  bar CUT when a panel but the last is too narrow for its text. Their test `TEST/WCHRT.C`
  (`WCHRT.RC`, `WCHRT.H`) makes a frame holding both bars, an edit control, two statics that say the
  bar's notifications and the calls' answers, the splitter `SRC/WIN/WSPLIT.C` (`WSPLIT.H`) and the
  preview panel `SRC/WIN/WPREV.C`; its header gives the keys that click the buttons, gray them, set
  the panels, drag and cancel the splitter by posted mouse messages, fill, measure and scroll the
  panel and resize the frame. Run as `WIN WCHRT /KEYS:WCHRT.KEY /SNAP:WCHRT.SNP`, `TEST/WCHRT.KEY`
  writes `TEST/GOLDEN/WIN/WCHRT.SNP` byte for byte in standard and 386 enhanced mode.
- The form dialog `SRC/WIN/WFORM.C` (`WFORM.H`, which gives its contract) shows any form table of
  `FMODEL.H` in a modal dialog it builds in memory, one control per field, with the model's
  checks, its save through the link and the failures as `rqe_class` gives them, and the pick lists
  of its code and location fields from `SRC/WIN/WPICK.C` (`WPICK.H`), a location field's path
  shown as the server gives it, its pages under the tab strip. A program that links it links
  `WPICK.C`, `WTAB.C`, `WLIST.C`, `WCOLS.C`, `WROWS.C`, `INTL.C`, `PAGERSV.C`, `KEYSET.C`,
  `SCRLFORM.C` and `CODETAB.C` too, and includes `SRC/WIN/WFORM.RC`, the dialog's strings, its
  labels of `WLABEL.RC` and the pick list's template, which includes `<windows.h>` itself, in its resource
  script, with `OBJ/WIN` (the generated `MSGNUM.H`) on its include path. Its test `TEST/WFORMT.C`
  (`WFORMT.RC`) logs in on the data `TEST/T_SCR.SH` seeds and opens, by keys of its frame, the
  item form of `SCRIFORM.C` in each mode and a form with a field of every type; its header gives
  the keys. `TEST/WFORMT.KEY` adds an item and changes one, each shown again as the server holds
  it, and reaches the item form's fields by their access letters, and `WFORMT<x>.KEY` type into
  every type and reach a check box, a drop-down list, a read-only field and a password by their
  letters, a disabled one by none (`A`), fail each local check (`B`), fail saves on the server
  (`C`), cancel (`D`), save a new item in part, its tags refused in the SET (`P`), and a memo in
  part (`Q`), wipe a password once a refused save, and a save whose memo failed, sent it (`R`),
  and open a dialog over the frame maximized and moved past two corners of the screen, then place
  a popup larger than the screen and one over the frame minimized (`S`), and pick a location from
  F4 in a location field, its path shown, and meet the item form's category list and an ended
  session (`T`), and load the tag pick list over two ITEM TAGS pages, the tags all on the second,
  then stop it by Esc between them, empty and titled incomplete (`U`), and cut it at
  `LINK_MAXCODES` tags (`V`); run as `WIN WFORMT TDATA /KEYS:WFORMT.KEY /SNAP:WFORMT.SNP` beside
  `INV.MSG`, each writes its golden `TEST/GOLDEN/WIN/WFORMT[x].SNP` byte for byte in standard and
  386 enhanced mode. Every dialog it opens is checked against the display floor, which its title
  would show, and the driver checks its place.
- The Windows programs' display floor is EGA, 640 × 350 pixels: a dialog fits it when its window
  comes to at most that at the dialog base units of the EGA driver's system font, 8 × 12
  (`WFORM.H`). Hercules, 720 × 348, is wider but two lines shorter, so whether a dialog of nearly
  the floor's height fits it depends on its driver and system font. CGA, 640 × 200, is below the
  floor: a dialog taller than its screen passes the bottom.
- `SRC/COMPAT/` serves a Windows program through the `COMPAT_WIN16` variants of `COMPAT.H` (Open
  Watcom or Watcom C 9.5 with `__WINDOWS__`, which implies `COMPAT_OW`): far blocks of global
  memory locked only inside each call (`CMEM.C`), the timer from `GetTickCount` (`CTIMER.C`),
  `SetHandleCount`, `SetErrorMode` and a yield hook where a DOS program patches its PSP, hooks
  INT 23h and 24h or issues INT 28h (`CDOS.C`), and the schema data from the program's
  resource `CSCH_RES`, locked inside each call that copies a value out (`CSCHEMA.C`; only the
  seed rows' tables stay in DGROUP, `CSCHDAT.C`), which needs the program's instance in
  `dos_inst`, set by its `WinMain` (`TEST/WTEST.C` for the unit tests); the
  serial ports go through the comm driver of Windows (`CCOMM.C`, in place of `CUART.C`, whose
  header refuses to compile for Windows), and NetBIOS through `NetBIOSCall`, with every NCB and
  buffer the provider owns in `GlobalDosAlloc` memory, each command no-wait and polled
  (`CNETB.C`, `nba_alloc`); TCP goes through Windows Sockets 1.1, loaded at run time, each wait
  polled with the yield hook (`CWSOCK.C`), and the printer through GDI, the default printer's
  pages laid out from a job's bytes (`CPRNW.C`). No video, keyboard, serial or printer module of
  DOS is linked, nor a near-heap module.
- Unit tests built as Windows programs: `WENGINE` and `WPROTO` are `T_ENGINE` and `T_PROTO`,
  `T_WCOMP` (`SRC/COMPAT/T_WCOMP.C`) tests the Windows variants, the schema reader of
  `CSCHEMA.C` among them, and prints a job whose double-width run a write divides, once on the
  Epson 9-pin driver of the Windows 3.0 setup disks, which `TEST/WIN/windrv.sh` expands into
  `OBJ/WINDRV/EPSON9.DRV` (`T_WCOMP_WINDATA`) and whose bytes, the expanded pitch among them,
  the test itself compares, and once on the test machine's printer, which has no font of that
  width, compared with `TEST/GOLDEN/WIN/T_WCOMP.PRN` (`T_WCOMP_WINPRN`). Under `COMPAT_WIN16`,
  `TEST/TESTFW.H` renames a test's `main`, `printf` and `sprintf`, and `TEST/WTEST.C` gives the
  `WinMain` that runs it, a formatter, and `NAME.OUT`, the file in the current directory that
  holds the lines printed and the exit status, mode, free memory, data segment, unused stack
  and run time, as its header gives them; the program then ends Windows with `ExitWindows`.
  Each binds `TEST/WTESTE.RC`, the schema data of the engine or of its schema reader; `wrc -30
  -t` binds it and marks the program for Windows 3.0 in protected mode.
- `TEST/WSERT.C` (`WTESTE.RC`) tests `CCOMM.C` under the link's SERIAL backend, as its header
  gives: the refusals on the test machine, which has no serial port; and, run as `WIN WSERT 1`
  by `make winser`, a session on COM1, one request of `LINK_MAXLINE` bytes among them, whose
  requests and response lines it writes into `WSERT.OUT`, which must equal
  `TEST/GOLDEN/WIN/WSERT.TXT`, with a `PING` after its `LOGIN` and one after its link's close at
  which the password must be nowhere in the machine's memory.
- `TEST/WNET.C` (`WTESTE.RC`) tests the NET link (`LINKNET.C`, `NBCLI.C`, `CNETB.C`), as its
  header gives: `WNET /NONB` must find no NetBIOS on the test machine and refuse at once, and
  `make winnb` plays `TEST/WIN/WNET.SCR` with it against a server.
- `TEST/WSETUP.C` (`WSETUP.RC`) tests `SETUP`, `INVWIN`'s installer (`SRC/WIN/SETUP.C` and its
  portable part `SETUPL.C`; `WIN_TOOLS` of `SRCS.MK`, built as the Windows programs are and run
  only through this test), as its header gives: beside the `SETUP.EXE` it starts again and again
  it writes a release's list and files, then drives the dialog and message boxes as a user
  would, each box's text checked against `SETUP.RC`, with Program Manager answering and deaf.
  It checks the files of an installation and the group's item in `RETROINV.GRP`, refused
  directories and switches, a rerun from the installed directory, failed copies, and
  `SETUP /U` answered no and yes. The host test `T_SETUP` covers `SETUPL.C`: the list,
  directories, the item's command line and Program Manager's commands.
- `make wintest` runs every one of these programs on the Windows 3.0 test machine, in
  standard and 386 enhanced mode (Makefile targets; Windows tests, under Programs and scripts).

### Turbo C 2.01 (`make tc` → `TC/runtc.sh`)

- Install: the command-line files lie unpacked on the disks, so mtools copies them into `TC_DIR`
  (default `$RETRO_DL/tc201/TC`), mounted as `D:` (`D:\TCC.EXE`, `D:\TLINK.EXE`, headers in
  `D:\INCLUDE`, libraries in `D:\LIB`):

  ```
  S="$RETRO_DL/tc201/Borland Turbo C 2.01 (1989) (3.5-720k)"; T="$RETRO_DL/tc201/TC"
  mkdir -p "$T/INCLUDE/SYS" "$T/LIB" "$T/EXTRA"
  mcopy -n -m -i "$S/Compiler.img" '::/*' "$T/"
  mcopy -n -m -i "$S/Header.img" '::/*.H' "$T/INCLUDE/"
  mcopy -n -m -i "$S/Header.img" '::/SYS/*.H' "$T/INCLUDE/SYS/"
  mcopy -n -m -i "$S/Header.img" '::/*.OBJ' '::/*.LIB' "$T/LIB/"
  mcopy -n -m -i "$S/Header.img" '::/TLIB.EXE' "$T/"
  mcopy -n -m -i "$S/Install.img" '::/C0H.OBJ' '::/CH.LIB' '::/MATHH.LIB' "$T/LIB/"
  ```
- `TC/mkbat.sh` reads `TC_EXES` and the `<NAME>_SRCS` and `<NAME>_MODEL` lists of `SRCS.MK` and
  writes the compile batch files `BIN/TC/CC1.BAT`, `CC2.BAT`, … (`TC_JOBS` of them, fewer when
  there are fewer objects), which share out the objects by source size, largest first, and the
  link batch file `BIN/TC/BUILD.BAT`. TCC 2.01 needs CR LF line ends (with bare LF it takes the
  rest of the file for part of the first `#include` and silently compiles nothing), so every
  `.C` is copied with CR LF into `BIN/TC/SRC/…` and compiled from there, and every `.H` into the
  one directory `BIN/TC/INC`, named in the generated `BIN/TC/TURBOC.CFG` (`-ID:\INCLUDE;INC`).
  TCC 2.01 does not search the including file's directory, so two headers of one name, under
  `SRC/`, `TEST/`, WATTCP's tree or among the generated ones, are refused.
- Compile: `D:\TCC -c -m<model> -1- -O -Z -G- -w -w-stu -A -oOBJ\<M>\<source path>.OBJ <source>`,
  without `-A` for the non-portable files of `SRC/COMPAT/` and the files of `WATTCP_USERS`, and
  with `-w-` in place of `-w -w-stu -A` for WATTCP's kernel modules; models `s m c l h`.
  `-w-stu` silences only "Undefined structure", which TCC reports for every opaque handle. `-Z`
  (register load suppression) brings a rule of its own for the sources (`DOC/STYLE.md`, Hard
  rules). The build leaves out `-d` (merge duplicate strings), which mis-generates
  function-local static initialisers.
- Link: TCC 2.01 cannot pass a response file to the linker, so each link runs `D:\TLINK /c /s
  @<NAME>.RSP` with `C0<m>.OBJ`, the objects, and `EMU.LIB`, `MATH<m>.LIB` and `C<m>.LIB`,
  writing `BIN/TC/<NAME>.EXE` and, through `/s`, the detailed segment map `BIN/TC/<NAME>.MAP`.
- `SRC/COMPAT/CSCHDAT.C` alone is also compiled with `-zRCSCH_FAR -zSCSCH_GRP -zTCODE`. Turbo C
  2.01 accepts `far` on a declaration but not on the definition of data, and has no `#pragma`,
  so the module that defines the schema's far data has its whole data segment renamed into the
  CODE-class segment `CSCH_FAR` (group `CSCH_GRP`), which TLINK places among the code segments,
  before DGROUP; `CSCHEMA.C` reads it through `extern … far` declarations (`FARX`). The class is
  not `FAR_DATA` because TLINK places that after `_STACK`, in the room the near heap and stack
  of the small and medium models grow into. OMFNEAR counts the segment as not near.
- Run: `runtc.sh` fills the paths, the batch file and its log into a temporary copy of
  `TC/TC.CONF` per DOSBox-X instance and runs `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
  timeout -k 10 $TC_TIMEOUT dosbox-x -conf … -nogui -nomenu -exit` (DOS version reported as
  3.10). Every compile batch file runs at once, each in its own instance with its own scratch
  directory, writing `BIN/TC/CC<k>.LOG`; then `BIN/TC/BUILD.BAT` in one more, writing
  `BIN/TC/LINK.LOG`. TCC writes no temporary files and is told each object's path, so the
  instances share the work tree. `BIN/TC/BUILD.LOG` collects the logs.
- The run fails on any error or warning line in `BIN/TC/BUILD.LOG`, a missing end marker of a
  batch file, or a missing EXE, and then deletes that run's EXEs (TLINK may write one despite
  errors). The EXEs are byte for byte those of a build in one instance. Whole runs are
  serialised by `flock` on `TC/.LOCK`, and `runtc.sh` runs the map and near data checks
  (`TC_CHECK`) under that lock, so a second build cannot replace `BIN/TC` while they read it; a
  failed check fails the build and deletes its EXEs. `make tc` passes `DOS_LOCK`, so the build
  holds the emulator lock shared while DOSBox-X runs. `TC_SRCS` and `TC_SRCROOT` point it at
  another source list and tree for testing.

### Watcom C 6.5 (1988; `make wc65` → `WC65/runwc.sh`)

- Install: `TEST/INTEG/unpack.sh` gives package `wc65` (`DISK1`..`DISK8`, plain files); `sh
  WC65/install.sh "$RETRO_DL/wc65" "$RETRO_DL/wc65/WATCOM"` copies them as the distribution's
  `INSTALL.SCR` lists (only its `mkdir`, disk and `copy` directives are read; nothing from the
  disks runs on the host), then the reconstructed `CLIBS.LIB` and `CLIBC.LIB` over the disks'
  copies from its optional third argument, a directory holding those two files (default `RECON`
  beside `DISK1`..`DISK8`). `WC65_DIR` (default `$RETRO_DL/wc65/WATCOM`) is mounted as `D:`, the
  repository as `C:`, the work tree `BIN/WC65` as `E:`.
- The libraries: on the only surviving copy of the disks the small- and compact-model run-time
  libraries `CLIBS.LIB` and `CLIBC.LIB` are about half overwritten by other files' data. The
  build uses a reconstruction of both, rebuilt from the intact medium-, large- and huge-model
  libraries with every surviving byte kept, published with its per-module provenance at
  <https://github.com/nrnelson/watcom-c-6.5-restoration>. `WC65/CLIBREC.SHA` pins their SHA-256:
  `WC65/install.sh` refuses to install, and `runwc.sh` to build with, a copy that differs, and
  `WC65/chklib.py` checks the libraries of every model in use before each build. A new pin
  forces a relink of the shipped programs only for those that use `int86` or `int86x` in the
  compact model, or `intr`, `_chain_intr` or `signal` in any model; the others are unaffected.
  Every program is built in its `<NAME>_MODEL`.
- Compile: `D:\BIN\WCC <source> /m<model> /0 /os /s /zq /dTF_TERSE /w3`, with
  `/j` added for the files of `WATTCP_USERS` and `/j /w0` in place of `/w3` for
  WATTCP's kernel modules, split over `BIN/WC65/CC<k>.BAT` (`WC65_JOBS` of
  them) as for Turbo C, linking in `BIN/WC65/BUILD.BAT`. 6.5 has no object-name
  switch and names its temporary files (`__LEAF__.TMP`, `__QUAD__.TMP`,
  `_TEMPA.TMP`) without a directory, so each batch file compiles in its own
  `E:\CC<k>` and copies each object to `E:\OBJ\<m>\<source path>.OBJ`. WCC 6.5
  reads memory it has not written, so an object would depend on what was
  compiled before it in its batch file; each compile therefore runs after
  `E:\ZEROMEM.COM` (`WC65/ZEROMEM.ASM`, assembled by `nasm`), which zeroes the
  memory the compiler is given, and the build's output is the same whatever
  `WC65_JOBS`. Headers are copied flat into `E:\INC` (`SET
  INCLUDE=D:\H;E:\INC`), duplicate names refused; there are no response files,
  so every batch line stays within `DOS_MAXCMD` characters (`TEST/shlib.sh`).
- Checks: 6.5 has no strict-ANSI mode (its own headers use `near` and `far`) and no
  warnings-as-errors switch, so the log is scanned for its warnings and errors. It also says
  `Procedure 'NAME' too large: may affect optimizations` of a function it then optimises less:
  the build fails when it says so of a function of a program module (a source under `SRC/` other
  than a test) that `WC65/TOOLARGE.TXT` does not list, and `WC65/toolarge.sh` reports a listed
  function of a compiled object that it no longer names. Linking uses `WLINK @<NAME>.LNK`
  directive files with the stack of `_stklen`; WLINK 4.1 writes an EXE even with undefined
  symbols, so a failed run deletes its EXEs. `TEST/mapchk.sh` reads the WLINK 4.1 maps
  `BIN/WC65/<NAME>.MAP` of every executable, and `TEST/OMFNEAR.PY` the objects of `NEAR_EXES`.
- Known 6.5 quirks, kept out of the code by `make lint` and `DOC/STYLE.md`: `#elif`
  mis-handling, warning W012 on `(void)x;`, a negative array size accepted without a diagnostic
  (Turbo C 2.01 too), error E091 on a `const` pointer parameter whose struct was incomplete at
  the prototype, no `CLOCKS_PER_SEC`, `FILENAME_MAX`, `FOPEN_MAX`, `wchar_t` or `localeconv`.
  Worked around in the code: a `#pragma aux` code body longer than 79 bytes is corrupted from
  byte 79 on (the machine-code loops in `SRC/COMPAT/` stay below that); a byte loop storing
  through an indexed array member is mis-generated (the text decoder in `SRC/CLIENT/RQ.C` writes
  through a plain `char *`); an indexed read `a[row][k]` of a two-dimensional far array with a
  variable `row` reads the wrong bytes (`CSCHEMA.C` computes the row's address); in the run-time
  library of the small and medium models, a `malloc` that grows the near heap to the top of
  DGROUP from a break 6 bytes below it writes the heap's end marker over a free block's link,
  then follows that link and never returns (`dos_heap_top` in `SRC/COMPAT/CDOS.C` grows the heap
  to the top in one step from the break the program starts with; `cmem_init` and the `main` of
  each program without it call it first, which `T_NHEAP` and `T_NHEAPG` hold; a unit test
  without `cmem_init` keeps the library's steps, as `COMPAT.H` can show `tf_begin` the call only
  at the cost of compiler memory three large tests do not have, and meets the fault only when
  its near heap fills DGROUP).
- Compiler memory: WCC 6.5 stops with E084 when its own fixed heap is full, however much DOS
  memory is free; the declarations a file reads and its string literals fill it. Hence the
  engine's functions are split into per-module headers (`ENGFLD.H`, `ENGSCH.H`, `ENGID.H`,
  `ENGEDIT.H`, `ENGUSER.H`, `ENGCODE.H`, `ENGLOCS.H`, `ENGITEM.H`, `ENGASSY.H`, `ENGMAINT.H`,
  `ENGLOAN.H`), `INV.C` does not read `SCRSHR.H` (what the screens share), `TUI.H` shows the
  Record menu only to a file that defines `TUI_VERBS` (its verb table only with `TUI_VERBTAB`),
  `SCRSHR.H` its location-tree helpers only with `SCRSHR_TREE`, the client's
  `RQERR.H`, `PAGER.H` and `FMODEL.H` show their calls only to a file that defines
  `RQERR_CLASS`, `PAGER_CALLS` or `FMODEL_CALLS`, the protocol handlers sit in `DPCMD.H`,
  `COMPAT.H` shows its NetBIOS sections only to a file that defines `COMPAT_NETBIOS` first (its
  vector read only with `COMPAT_VECT`, its handle duplication only with `COMPAT_DUP`,
  `dos_heap_top` only with `COMPAT_HEAP`, `cf_zero` and `DOS_SECTOR` only with `COMPAT_DISK`),
  the build defines `TF_TERSE`, and a test too large
  for one file is split into parts (Unit tests).
- The failure rules, logs (`BIN/WC65/CC<k>.LOG`, `BIN/WC65/LINK.LOG`, together in
  `BIN/WC65/BUILD.LOG`), the lock `WC65/.LOCK` with the checks under it (`WC65_CHECK`),
  `DOS_LOCK`, `WC65_TIMEOUT`, `WC65_SRCS` and `WC65_SRCROOT` are those of the Turbo C build.
  WLINK 4.1 copies uninitialised memory into some gaps of the EXE it writes, so its bytes there
  depend on what ran before WLINK in the same DOSBox-X session.
- A Turbo C and a Watcom C 6.5 build may run at the same time, in one make or in two: each has
  its own work tree and lock, and the files they share, the message files with `OBJ/MSGSTMP.H`
  and WATTCP's tree, are put in place whole before their stamp (`OBJ/MSG.MK`,
  `OBJ/WATTCP/.ok`) says so, and neither build takes one that is old or half written. Two
  makes that both find WATTCP's tree out of date prepare it one at a time, under the lock
  `OBJ/WATTCP.LCK`, and the second, finding it up to date once it has the lock, leaves the tree
  the first one's build reads as it is.

### Watcom C 9.5 (1993; `make wc95` → `WC95/runwc.sh`)

- A second build of the Windows programs of `WC95_EXES` (`SRCS.MK`: `INVWIN`) by a compiler of
  their period, which checks their code beside the Open Watcom build; Open Watcom's is the one
  shipped. It builds Windows programs only. `make wc95test` runs them on the Windows 3.0 test
  machine as `wintest` runs Open Watcom's, against the same goldens.
- Install: the disks of WATCOM C/C++32 9.5 (`W9532_01.img` .. `W9532_08.img`) and of the
  C/C++16 Delta Pack (`W9516_01.img` .. `W9516_04.img`) and the binary patches `Patch32.zip` and
  `Patch16.zip`, all in one directory, as the set "Watcom CPP 9.5b (1993) (3.5-1.44mb)" holds
  them (its `W9532_09`, `W9532_10` and `OS2TK_*` images are not needed):

  ```
  sh HOST/run.sh -c 'sh WC95/install.sh \
      "$RETRO_DL/wcpp95b/Watcom CPP 9.5b (1993) (3.5-1.44mb)" "$RETRO_DL/wc95/WATCOM"'
  ```

  The delta pack takes its headers, resource compiler and linker files from the 32-bit set, and
  both are packed in WATCOM's own format, which only their `INSTALL.EXE` unpacks; so
  `install.sh` runs it in headless DOSBox-X with each set's questions answered in its install
  script, then the patches' `APPLYA.BAT` and `APPLYB.BAT`, about 6 minutes in all. Nothing from
  the disks or the patches runs on the host. `WC95_DIR` (default `$RETRO_DL/wc95/WATCOM`) is
  mounted as `D:`, the repository as `C:`, the work tree `BIN/WC95` as `E:`.
- The pin: `WC95/TOOLS.SHA` holds the SHA-256 of every file of the install that the build reads,
  the compiler `BIN\WCC.EXE` ("WATCOM C16 Optimizing Compiler Version 9.5a"), the linker
  `BIN\WLINK.EXE` (9.5b) and its system file, the resource compiler `BINB\WRC.EXE` (1.0), the
  headers and the libraries of `LIB286` and `LIB286\WIN`; `install.sh` refuses to install, and
  `runwc.sh` to build with, a copy that differs. `WCC.EXE` and `WLINK.EXE` are DOS/16M programs,
  which need a 286 with extended memory, as DOSBox-X gives them.
- The 9.5a caveat: the 16-bit compiler and its Windows C library stay at patch level A. The
  patches' level B lists for the 16-bit set files (`wccd.b`, `2cswl.b`, `wlinkb.b`, ...) that
  neither archive holds, and its README names the code-generator faults it mends, each one wrong
  code without a diagnostic: signed 16-bit arithmetic folded to bogus values, a one-time loop
  followed by another loop, a loop with a known induction range thrown out, incompatible
  assignments to bit-fields not converted, wrong conversions between temporaries and bad code
  under complex circumstances. So the build is a check and never a release; `wc95test`, every
  script of `INVWIN` in standard and 386 enhanced mode, is what tells whether the 9.5a code of
  a change runs as Open Watcom's does. It does not reach the TCP layer, though: the test
  machine has no Winsock, so `INVWINTC` stops at its failing open, and the Winsock
  declarations that only 9.5 compiles are checked against Open Watcom's `winsock.h` alone.
- Compile: `WCC E:\<source> -fo=E:\OBJ\<M>\<source path>.OBJ -m<model>`, with the switches of
  the Open Watcom Windows build but its `-i` directories in the variable `WCC` (`-0 -os -s -zq
  -w4 -we -bt=windows -zW -dWINVER=0x0300 -dSTRICT`, `-w4` being 9.5's highest warning level, as
  it has no `-wx`), and `-za` but for the sources of `NOANSI` (`WC95_CFLAGS`, `WC95_ANSI` and
  `WC95_NOANSI` of `MAKEFILE`). Its `-zW` gives the entry code and odd saved BP of Open
  Watcom's, so the programs are marked for protected mode only as well. The compiles are shared
  out over `BIN/WC95/CC<k>.BAT` (`WC95_JOBS` of them) as for Watcom C 6.5, each compiling in its
  own `E:\CC<k>` for the compiler's scratch files. The headers, generated ones and the resource
  scripts beside each program's `<NAME>_RC` are copied flat into `E:\INC`, duplicate names
  refused, which `INCLUDE` names first (`E:\INC;D:\H;D:\H\WIN`), so that `SRC/CLIENT/PRINT.H` is
  found before the compiler's `PRINT.H`.
- Link: `BIN/WC95/BUILD.BAT` compiles `<NAME>_RC` (`WRC /q /r`), links with `WLINK @<NAME>.LNK`,
  which holds `system windows`, the map and the directives of the Open Watcom link
  (`WC95_LINK` of `MAKEFILE`, the `WIN_LINKOPTS` list), then binds the resources with `WRC /q
  /30 /t`, and `TEST/NECHK.PY` checks each program's header and imports as for `BIN/WIN/`
  (`WC95_CHECK`). A program is the same byte for byte whatever `WC95_JOBS` says.
- The code: `COMPAT.H` selects 9.5 by `__WATCOMC__` 950 with `__WINDOWS__` and `M_I86` (it has
  no `__I86__`), defining `COMPAT_WC95`, `COMPAT_OW` and `COMPAT_WIN16`; it has no `winsock.h`,
  so `CWSOCK.C` declares under `COMPAT_WC95` the part of Windows Sockets 1.1 that it uses.
- The log is scanned for WCC's `Error!` and `Warning!` lines, WLINK's `Error(n)` and
  `Warning(n)`, WRC's, the DOS/16M extender's and DOS's failures and the `*** FAILED` lines of
  the batch files. The logs (`BIN/WC95/CC<k>.LOG`, `BIN/WC95/LINK.LOG`, together in
  `BIN/WC95/BUILD.LOG`), the lock `WC95/.LOCK` with the checks under it, `DOS_LOCK`,
  `WC95_TIMEOUT`, `WC95_SRCS` and `WC95_SRCROOT` are those of the Turbo C build.
- Cost, and why neither goal is part of `gate`: on a 32-thread host the build takes 38 s in one
  DOSBox-X instance and 8 s in 15, and `wc95test`'s runs (one per key script of `INVWIN_WINKEYS`
  in each mode) 72 s at 30 at a time and 3 min 41 s at 6 (`DOS_JOBS=6`), some 20 to 25 minutes
  of CPU either way. In the gate, every change to the Windows client or to a module it links
  would pay those minutes again for a second compiler's check of a program that `wintest`
  already runs script by script, and every gate host would need a compiler from disks outside
  the gate's list. So `make wc95test` is run when such a change lands, and before a release.

### Windows Help (`make winhelp` → `HC/runhc.sh`)

`INVWIN`'s help is a Windows Help file, `BIN/INVWIN.HLP`, compiled from `SRC/WIN/INVWIN.HTX`
by the help compiler of the Windows 3.0 SDK, `HC.EXE` 3.00, a DOS program.

- Install, once: `sh HC/install.sh IMG "$RETRO_DL/win30hc"`, `IMG` the SDK's disk 1
  (`"$RETRO_DL/win30sdk/Microsoft Windows 3.0 SDK (1990) (5.25-1.2mb)/Disk01 - Development
  Tools.img"` from `unpack.sh`). It copies `RESTOOLS\HC.EXE` off the image with mtools and keeps
  it only when it matches `HC/TOOLS.SHA`; `HC/runhc.sh` checks it again at every run.
- Source: `TEST/hlpchk.py --rtf` writes `OBJ/HC/INVWIN.RTF` and its project `INVWIN.HPJ` with
  `BIN/INVWIN.HTX`. Each section is a topic: its name the context string, its first line the
  title, the keyword and a heading, all topics in one browse sequence in the order of the file,
  section `INDEX` (`WH_HELPINDEX`) first and the help's index, its title the file's, followed by
  a jump to every other section but the fields' (`T_F`), which their form's topic ends with
  instead, each field's ending with a jump back. A paragraph is wrapped; a list of keys
  (`key_list` of `hlpchk.py`) becomes a paragraph per key, its description wrapped with a hanging indent at a
  tab of 120 twips per character of the list's description column, at most two inches; any
  other paragraph with a line that starts with a blank keeps its lines in Courier; a numbered
  list wraps each item; a "See also: " paragraph makes its titles jumps. The text goes from CP437 to the Windows set
  (cp1252). The project maps each topic to its context number, `ctx` of `WHELP.C`
  (`WH_CTXMUL`), and asks for a compressed file.
- Build: `HC/runhc.sh` runs `HC.EXE` on the project in one headless DOSBox-X instance
  (`TC/TC.CONF` filled in, the project's directory as C:, `HC_DIR` as D:, under `DOS_LOCK`,
  `HC_TIMEOUT` default 120 s), and fails on a line of its log `OBJ/HC/HC.LOG` holding "error" or
  "warning", as `HC.EXE` sets no error level, or on a missing output. `HC.EXE` writes the time
  of the build and leftover memory into its file, so `HC/hlpstamp.py` sets those bytes and bits:
  the system file's date, from the RTF's SHA-256; the unused ends of its fixed fields and B-tree
  pages; all but bit 0 of the system file's flags word; and in each record of paragraphs of the
  topic file all but bit 0 of the word before the flags, and the three flags no paragraph
  property sets (`PARA_FLAGS` holds the others). So the same source gives the same
  `INVWIN.HLP`. The bits of the three words hold what `HC.EXE`'s stack held, which is the same
  at every run but where a timer interrupt came between, as now and then it does, mostly on a
  loaded machine. `HC.EXE` clears bit 0 of the system file's flags and Windows Help refuses a
  file with it set; Windows Help reads bit 0 of the paragraphs' word and none of the cleared
  bits, so both bits stay as `HC.EXE` wrote them.
- `make winhelpchk` (`TEST/hcchk.py`) compiles the source twice more, in other directories and
  time zones and with the timer interrupt far faster (`HC_TIMER` of `runhc.sh`), which changes
  the leftovers of some paragraph records in nearly every compile, each to the same bytes as
  `BIN/INVWIN.HLP`. It also checks that each differs from what `HC.EXE` wrote only in the bytes
  and bits named above, that `hlpstamp.py` clears just those bits of the three words, the
  build's stamp, its context numbers against the project's, and the refusals of `hlpstamp.py`
  and `runhc.sh`. A compile that differs from the build's is a field `HC.EXE` leaves unset
  that `hlpstamp.py` does not yet set: it is found by comparing the two files and added there,
  never passed by running again.
- Neither `winhelp` nor `winhelpchk` is part of `gate`, whose machines need no SDK. `wintest`
  runs `INVWIN` with `INVWIN.HTX` alone, so its help shows in its own window, but for the key
  scripts of `INVWIN_WINHLPKEYS`, which it adds only where `HC_DIR` holds `HC.EXE`, each with
  its data `INVWIN_WINDATA<x>`, which adds `BIN/INVWIN.HLP`; there the test driver also sees
  Windows Help's windows (`TDRV.H`).

## Tests

The goldens of `TEST/GOLDEN/` are the files a run must give byte for byte. Many hold CP437 text
(box lines, shades, accented letters), which `grep` in a UTF-8 locale takes for binary and so
prints only "binary file matches": search them with `grep -a`.

### Unit tests

Each test's header comment says in full what it checks. Every test is in `HOST_TESTS` and
`DOS_TESTS` of `SRCS.MK` unless the row says otherwise. A test that writes files takes its
scratch directory as its first argument.

| Test | Module | Covers |
|---|---|---|
| `T_COMPAT` | `COMPAT.H` (host only) | the widths of the fixed-width types, `get_le16`/`put_le32` and their kin on every boundary value, `nb_present` without NetBIOS, the `uart_` calls without ports |
| `T_CRUN` | `CRUN.C` (host only) | a loop's tick count over the host's virtual ticks across two midnights, the date read through DOS at each midnight and never otherwise; `dos_stamp`'s form, and its second read when the day changes between its reads of the date |
| `T_CFILE` | `CDOS.C`, `CMEM.C` | the `cf_*` file calls (positions above 64 KB, read-only handles, far blocks, truncate, rename, remove, error codes), `cf_exists` under the read hook, `dos_find_first` and `dos_find_next`, `cf_mkdir` and `cf_rmdir`, the date and time, `dos_set_handles` up to the boot's `FILES=`, `dos_disk_free`, `dos_dev_raw`; with `SHARE` a sharing conflict returning `CF_ESHARE` without a prompt; with `NODISK` a critical error on an empty `B:` failed without a prompt |
| `T_CTEXT` | `CTEXT.C`, `cf_exists` | every `txt_` helper and `TXT_` macro over its edge values (`txt_dosdev` over every DOS device name and its near misses), and `cf_exists` on files, missing paths and directories |
| `T_DTA` | `cf_exists`, `dos_find_first`, `dos_find_next` (DOS only) | the program's DTA left undisturbed by `cf_exists` and a `dos_find` search, a directory counted as absent; INT 24h after `dos_crit_install` answering Fail when allowed, else Retry |
| `T_CLOCK` | `dos_time` (DOS only) | the time of day in range and in order across the ticks MS-DOS 3.10 reads as second 60 |
| `T_NHEAP`, `T_NHEAPG` | `cmem_init`, `dos_heap_top` (DOS only) | the near heap whole after `cmem_init`: 1 KB blocks taken until `malloc` refuses, and as many again once freed; under Watcom C 6.5 with DOS short of memory for DGROUP, and from the break at which the library's 8 KB steps would damage the heap, with the break then at the top of DGROUP; `T_NHEAPG` is the same sources built in the small model, where the test calls `dos_heap_top` in place of `cmem_init` |
| `T_MSGLO` | `msg_load` over `cmem_replace` (DOS only) | the far block `msg_load` keeps, for `INV.MSG` and `INVSERV.MSG`, starting where its first block did, its second block and a block allocated after the load above it |
| `T_CWATT` | `CWATT.C` with WATTCP's kernel and `CPKT.C` (DOS only) | the kernel starting without a packet driver; `inchksum` against RFC 1071 over a far 32 KB block; `intel16` and `intel`; `resolve`; `outs` through a redirected handle 2; the timeouts, across midnight; `wt_isn`'s numbers all different, their high words and their steps varying |
| `T_UART` | `CUART.C`, `CUARTSV.C` (DOS only, on MS-DOS 3.10 under QEMU with a 16550A as COM2, as DOSBox-X's loopback repeats bytes) | the refusals with the chip and vector left as they were; in the chip's loopback at 9600 and 38400 baud, with and without FIFOs, every byte back in order, overruns counted, the receiver held at a full ring and RTS dropped under RTS/CTS, nothing lost, no byte kept in a ring; 2 KB at 38400 baud; a vector taken over after the open left in place |
| `T_DBF` | `DBF.C` | create, append, read, write, delete/undelete, every field type at its limits, a byte-exact `ITEMS.DBF` header, the dBASE year byte, damaged and truncated files, a deterministic mutation loop |
| `T_DBT` | `DBT.C` | block-boundary memo sizes, exact on-disk bytes, append-only reuse rules after a lagging header, limits |
| `T_NDX` | `NDX.C` | `NKEYS` keys with duplicates against a sorted model (walks, seeks, splits, deletes with dBASE-style key tightening and leaf unlinking), every dBASE-built index of `TEST/GOLDEN/DB3/` read, searched and edited, damaged files |
| `T_KEYEX` | `KEYEXPR.C` | the keys of every expression of `TEST/GOLDEN/KEYEXPR.TXT` over `FX_NRECS` fixture records, damaged N fields among them, byte for byte as dBASE computed them; syntax, type and range errors; second part `T_KEYEX2` (the fixture) |
| `T_PACK` | `PACK.C` | packs with and without memos, a pack, a reindex and their recovery stopped at every disk-changing call, impossible states refused, reindexed indexes compared page for page with dBASE's `INDEX ON` |
| `T_TOKEN` | `TOKEN.C`, `RESPONSE.C`, `SESSION.C` | line assembly, the tokenizer, the response writer, the comma-list matcher, session state |
| `T_PROTO` | the protocol layer | the scripts `TEST/SCRIPTS/*.SCR` run through `proto_dispatch`, every response line compared (Protocol test scripts, below), `TICKET.SCR` the login tickets on a fake store; other parts `T_PROTO2` (the directives) and `T_PROTO3` (the fake store) |
| `T_TABLES` | `SCHEMA.C`, `TABLES.C`, `INITDATA.C` | opening a data directory: creation and byte layouts, seed rows, SEQ repair, recovery, index rebuild and checks, files in use, handles; other parts `T_TABLE2` and `T_TABLE3` (records appended past the engine, code tables emptied and packed) |
| `T_CURSOR` | `CURSOR.C` | paging cursors and bounded scans over every index, malformed cursors, records changed between pages, the read limit; second half `T_CURSO2` |
| `T_EDIT` | `EDIT.C`, `AUDIT.C`, `IDGEN.C` | record edits, audit rows (one across midnight on the host), SEQ IDs, memos, index upkeep, the edit marker through a change stopped at every disk write; other parts `T_EDIT2` and `T_EDIT3` |
| `T_USERS` | `USERS.C`, `USERTK.C` | the password hash vectors, logins and their audit rows, logins with a ticket's tag and their revocation, password rules and changes, accounts, the last-admin rule |
| `T_ENGINE` | `ITEMS.C`, `CODES.C`, `LOCS.C`, `CSCHEMA.C` | items, the code tables, every list order and its paging, the read limit; the location tree, its rules and walks, a damaged tree; the numbered texts; other parts `T_ENGIN2` (lists, categories, bounded phases), `T_ENGIN3` (locations and walks) and `T_ENGIN4` (texts, tags) |
| `T_ASSY` | `ASSY.C` | install, remove and move, depth and size limits, BOM walks and paging, the repair of part locations, damaged parent chains |
| `T_MAINT` | `MAINT.C` | maintenance records: add, refusals, get, set, delete, notes, the two lists and their paging |
| `T_LOAN` | `LOANS.C` | loans, reservations and in-use rows (kinds L, R and U): their refusals, the hosts' rules, the counters ON_LOAN, RESERVED, IN_USE and ATTACHED with units counted once, the item rules while units are held, INSTALL of a host into the item it serves, the lists by item, host and kind and the overdue list, the repair of the four counters at open; second half `T_LOAN2` |
| `T_CSV` | `CSV.C` | CSV export of every exportable table byte for byte, paging, the scratch file and its rename, the path rules, concurrent exports; the import parser, its column mapping and refusals, the audit rows; other parts `T_CSV2` and `T_CSV3` (LOCS files in any order, a tree exported and imported back) |
| `T_OPENFT` | opening a data directory | an open of six starting states stopped at every disk-changing call, then a consistent directory |
| `T_HEAL` | the repairs of `eng_init_data` | the repairs after an interrupted change of several items, stopped at every disk-changing call |
| `T_CSVF` | `CSV.C` | imports of ITEMS and LOCS and an export with and without REPLACE stopped at every disk-changing call: none or all of the records, never a partial export file |
| `T_ADMIN` | `ADMIN.C`, the open's handle check | PACK, REINDEX, BACKUP and RESTORE through the engine, over a damaged N field and beside a failed change's marker, bad directories and backup sets refused with the data unchanged, RESTORE and BACKUP stopped at every disk-changing call, failing closes on the host, the open failing with 303 when `FILES=` leaves one handle too few; it takes a second, backup directory (`C:` on MS-DOS 3.10, so not run under DOSBox-X); second half `T_ADMIN2` |
| `T_FAKENB` | `TEST/FAKENB.C` | the simulated NetBIOS provider (below): commands before `nb_present`, names, CALL meeting LISTEN, message boundaries and pieces, RECEIVEs posted late, hang-ups, aborts, injected failures, the session and NCB limits, latency, cancels, stalls, timeouts and the pump; second half `T_FAKEN2` |
| `T_FAKEU` | `TEST/FAKEUART.C` | the simulated serial ports: `uart_open`'s refusals, bytes both ways and between paired ports, flow control with nothing lost, loopback, pacing, injected errors and their sum in `uart_rxerrs` (from the open, wrapping at 65536), every byte zeroed as it leaves a ring |
| `T_TSFAKE` | `TEST/TSFAKE.C` | the simulated TCP/IP stack (below): `ts_init`, `ts_term` and the calls refused around them, listening slots, the peers' addresses, the byte rings and acknowledgements, closes, FIN, resets, aborts and the pump |
| `T_BRIDGE` | `BRIDGE.C` | the TCP bridge over `TSFAKE.C` and `FAKENB.C`: `br_start`'s refusals, the allow-list, `TS_SLOTS` connections and one more refused, CALLs refused, timed out, retried and cancelled, `br_stop` with a cancel never confirmed; the relaying, every end of a slot and the wipes of its buffers in `T_BRIDG2` |
| `T_INVTCP` | `INVTCP.C` | INVTCP's run through `invtcp_main` over the fakes: usage errors and `/?`, `/ALLOW:` letting a client in or refusing it, a clean F3 and Y, the question of F3 (asked with no session too, counting one session and two, answered no with a session that then still relays, F10 doing nothing), the stops with NCBs given back late or never, every start failure with its status and message, the BREAK setting and critical-error handler put back, the atexit rerun of the stop |
| `T_SRVCFG` | `SRVCFG.C` | `INVSERV.CFG`: every default, key and bound, the READONLY= and SERIAL= lists, SESSIONS=0, line ends, comments and Ctrl-Z, every refusal at the right line, read failures |
| `T_CONN` | `CONN.C` | the connection slots over a fake transport and the real engine: events and trace texts, blank and over-long lines, input and passwords wiped, BYE and the last failed LOGIN, pipelined and split lines, fairness, STATUS, IMPORT's 402, the read-only cap, ADMIN CLIENTS, the idle timeout, shutdown; tickets offered only on a mailbox's slots and the output block zeroed when a slot is freed; the failed-login throttle (`THROTL.C`); other parts `T_CONN2` and `T_CONN3` |
| `T_TNETB` | `TNETB.C` | the NetBIOS transport over `FAKENB.C`, CONN and the engine: `tnetb_start`'s refusals, the LISTENs, `CONN_NETMAX` sessions and one more refused, posts refused and retried, every latency, flow control, the ends of a session, unconfirmed cancels, `tnetb_stop`; other parts `T_TNET2` and `T_TNET3` |
| `T_TSER` | `TSERIAL.C` | the serial transport over `FAKEUART.C`, CONN and the engine: `tserial_start`'s refusals, a slot listening until the first byte, the hang-up after `TSER_HANGUP` ticks of DSR and DCD down, the output drained or dropped after `CONN_SHUTWAIT`, at most `TSER_CHUNK` bytes a pass, RTS/CTS, two ports, shutdown; second half `T_TSER2`, with the damaged lines: a request line received while the port's error count rose, of each kind and also with its damaged bytes and its end in different passes, neither carried out nor answered, the session ended and the next one starting logged out |
| `T_TMBX` | `TMBOX.C` | the mailbox transport on a scratch mailbox with CONN and the engine: `tmbox_start`'s refusals and leftover sessions, one session per request file, BYE, the service order, `READONLY=` by file name, looks `TMBOX_POLL` apart, failed renames, opens, creates and reads and a full disk, requests zeroed and purged before CONN sees them, responses sealed in their last sector, shutdown; other parts `T_TMBX2` and `T_TMBX3` |
| `T_MBXCLI` | `MBXCLI.C` | the mailbox client of INVTEST and the MBOX link, the test playing the server: station and directory checks, the queue's limit, posts and waits, response framing, `MBXCLI_ECUT`, `MBXCLI_RSPMAX`, every failing step named by `mbxcli_ioerr`, the one retry of a post, the calls a redirector repeats after a reconnect, a response file zeroed before its removal when asked (`mbxcli_zero_next`) and one left from before zeroed by the next post, and the buffers zeroed |
| `T_SRVSCR` | `SRVSCR.C` | the status screen against the goldens of `TEST/GOLDEN/SRV/` over stubs: idle, sessions, the trace, the F3 question (asked with no session too, F10 doing nothing), every slot, kind and state at the longest values, the serial ports, the mailbox, BIOS and snow drawing, the MDA palette; second half `T_SRVSC2` |
| `T_INVSRV` | `SRVRUN.C` | the server's run over the fakes and a scratch mailbox with the real transports, slots, screen and engine: start-up failures and their exit statuses, far memory failed at each allocation, ADMIN SHUTDOWN, READONLY=, an F3 answered no with a session open, served meanwhile and after, the idle timeout, the stuck-NCB wait, the event log, midnight, `SESSIONS=0`, missing or busy ports and mailbox, a mailbox client's login ticket and none on NetBIOS or serial sessions, the throttle across mailbox sessions; other parts `T_INVSR2` and `T_INVSR3` |
| `T_TICKET` | `TICKET.C` | the store of login tickets: issue and redeem, the binding to user and station, idle expiry and `TKT_LIFE`, drops, one ticket per station, eviction, no ticket in the store's bytes, and on the host the recipe of a ticket rebuilt; under DOS the timer chip's count moves |
| `T_NBCLI` | `NBCLI.C` | the client's NetBIOS session over `FAKENB.C`: local names, failed opens with nothing left behind, CALLs made again, requests and responses, framing, a RECEIVE posted before each response comes, waits, ended sessions, the NCB limit, the send copy wiped; second half `T_NBCL2` |
| `T_NBNONE` | `NBCLI.C` | the client's NetBIOS session with the real `CNETB.C` and no provider: `NBCLI_ENONB` and nothing held |
| `T_NBDRN` | `CNBDRAIN.C` | `nba_drain` over `FAKENB.C` and a key source: nothing kept, an NCB given back during the wait, a key ending it |
| `T_INVTST` | `INVSCR.C` | INVTEST's script interpreter over the fakes and a scratch mailbox against a canned server: every directive, each failing line, masked passwords, failed mailbox posts named with step and DOS error, CALLs made again; second half `T_INVTS2` |
| `T_NETE` | the NET link and the server | `LINK.C`'s NET backend with TNETB, CONN and the engine over `FAKENB.C`: `TEST/T_INV.SH`'s request script giving the same bytes over NET as its golden, two links, READONLY=, IMPORT's 402, ADMIN CLIENTS and SHUTDOWN, no password left behind; other parts `T_NETE2` and `T_NETE3` |
| `T_SERE` | the SERIAL link and the server | `LINK.C`'s SERIAL backend with TSERIAL, CONN and the engine over two paired `FAKEUART.C` ports: the same request script giving the same bytes; noise ahead of the first request, a byte with a framing error opening no session and a clean byte leading HELLO, refused with 101 and the session in step, HELLO sent again and the requests after it served |
| `T_MBXE` | the mailbox client and the server | `MBXCLI.C` with TMBOX, CONN and the engine on a scratch mailbox: the names and longest station `MBXCLI.H` mirrors equal to `TMBOX.H`'s, the same request script giving the same bytes |
| `T_TCPE` | the bridge and the server | TCP clients through `BRIDGE.C` to TNETB, CONN and the engine over `TSFAKE.C` and `FAKENB.C`: the same request script giving the same bytes, sessions at once and their cap, the bridge's station name, no password in an event, every session closed once the server stops; other parts `T_TCPE2` and `T_TCPE3` |
| `NBTEST` | `SRC/TOOLS/NBTEST.C` (host, and DOS in `TEST/T_INV.SH -d dosbox`) | without a NetBIOS provider the probe exits 2; `TEST/T_TOOLS.SH` checks every bad command line (exit 1, the usage lines on standard error), `/?` (exit 0, on standard output) and the good ones |
| `INVTEST`, `INVUSAGE`, `INVNOSRV` | `SRC/TOOLS/INVTEST.C` (host only) | without a NetBIOS provider INVTEST exits 2 before it reads its script; a usage error exits 1; with no server (`-`) it does not look for NetBIOS and exits 1 for the missing script |
| `INVSERV`, `SRVUSAGE`, `SRVHELP`, `SRVNOCFG`, `SRVTWOCF`, `SRVBADCF` | `SRC/SERVER/INVSERV.C` (host only) | without a NetBIOS provider INVSERV exits 5 before it opens the data; an unknown switch, an empty `/CFG:` and a second `/CFG:` exit 1, `/?` 0, and a settings file that is not one 8 |
| `COMTEST` | `SRC/TOOLS/COMTEST.C` (host, and on MS-DOS 3.10) | on the host, which has no serial port, `TEST/T_TOOLS.SH`: each port form refused with `COMTEST FAIL COMn: no such port`, exit 2; every bad command line exit 1 with the usage text on standard error, `/?` 0; under QEMU, `COMTEST_COMARGS` on a 16550A as COM2, every byte back in order |
| `INSTALL` | `SRC/TOOLS/INSTALL.C` (host only) | a bad command line exits 1 before anything is asked; `REL/distchk.sh` installs with it from the disks (Release) |
| `T_INST` | `INSTPLAN.C`, the installer's decisions (host only) | the list read and every malformed field refused, the roles a set offers, the target directory's form, a server with the Windows client and the sample data getting exactly its roles' files, the disks asked for in order until the drive holds the one of that label whose list names the same set, a settings file kept unless the user says to replace it, a failed write leaving no part of its file, leaving at a disk question keeping what was copied, nothing copied when the source is the target, a compressed file and the compressed sample data expanded, the same list's files copied as they are from an installation, a damaged compressed file or a disk filling during an expansion leaving no part of its file, and the removal: the data only when told, the directory only when empty |
| `T_SETUP` | `SRC/WIN/SETUPL.C`, the portable part of `SETUP` (host only) | the list's lines and roles and every malformed one refused, the directory's form and what Program Manager cannot take in it, the item's command line and its limit, Program Manager's commands at their longest, and the copy through `SETUP.$$$`: a compressed file expanded, one held as it is copied, a damaged one, a full disk and a missing file each keeping the earlier copy; `WSETUP` tests `SETUP` itself (Windows 3.0 programs) |
| `T_LZEXP` | `SRC/TOOLS/LZEXP.C`, the expander of `INSTALL` and `SETUP` | streams built token by token, read a few bytes at a time: a bad header refused with nothing written, the header alone, literals and matches of every length with a refill at each place of a token, every cut short of the end and a stated length one off either way refused, a window wrap, the reader's and the writer's codes passed back, a long run cut every `CUTSTEP` bytes, and the names `lz_name` gives; with two file arguments it expands the first and compares it with the second, for `TEST/szddchk.py` |
| `INV` | `SRC/CLIENT/INV.C` (host only) | a usage error exits 1 before any input is read; `TEST/T_INV.SH` tests the rest |
| `THIN` of `SRCS.MK` | `SRC/CLIENT/INV.C` with `INVNOL.C` and `LINKNOL.C`, the network-only client (host only) | a line naming no connection exits 1 before any input is read; `TEST/T_INV.SH` tests the rest of its start-up |
| `T_NOLOC` | `LINK.C` over `LINKNOL.C` with `TOKEN.C` alone (host only) | the link of a program without the engine: `link_open_local` refused with 104 and a reason, an over-long line answered `-ERR 103 line too long` unsent, with no response writer linked |
| `T_SCHBIN` | the file of `CSCHBIN.C` with `SCHEMA.C`, `CSCHEMA.C` and `CSCHDAT.C` (host only) | the schema data the Windows programs bind: a header entry per part of `CSCHDAT.H`, the parts back to back to the file's end, each part's element size and count, and every field row, seed row, text and key expression as the host build copies it out |
| `T_DOSX` | `CDOS.C`, `CKEY.C`, `CTIMER.C` | the stack check; `dos_machine_name` (none on the host and on MS-DOS 3.10 under QEMU; DOSBox-X reports its host's, so screen tests fix `STATION=`); BREAK off and restored, also with a Ctrl-C typed ahead of a read from CON; `key_alt_letter`; the key source and its typed-ahead keys; under DOS, keys stuffed into the BIOS buffer; the tick counter across midnight and `tmr_delay` |
| `T_CMEM` | `CMEM.C` | `cmem_*`: the near heap `cmem_init` promises, sizes up to `CMEM_MAXBLK`, refusals, get/put/set, overlapping `cmem_move`, `cmem_copy`, more than 64 KB held at once, bounds (the host build aborts on an access outside a block) |
| `T_VIDEO` | `CVIDEO.C` | `vid_*` in direct, snow and BIOS modes (on the host also as an MDA): drawing, change spans (`vid_flushed`), save and restore, `vid_invalidate`, cursor, `vid_snow_active`; under DOS `vid_verify` after every flush and the DOS screen back after `vid_close`, also started on video page 1 (`PAGE1.COM`); on MS-DOS 3.10 a BIOS data area of 81 columns, whose open sets the mode and so finds a cleared screen |
| `T_RQ` | `RQ.C` | quoting and `rq_cost`, overflow and `rq_trypair`, decoding of every line kind, dot-stuffing, escapes, `K ERROR=`, `C`, `-ERR ... FIELD=`, a callback stopping a response, the wiping of the line sent, a closed session; over a LOCAL link |
| `T_RQERR` | `RQERR.C` | `rqe_class`'s show, act and text for every row of its table, the order in which the rows apply and a closed session on any code; `rqe_text` for each storage code, another code and a negative one; `rqe_deny` at the boundary levels; no text longer than `RQE_TEXTLEN`, even with the longest reason the server can send |
| `T_ARGS` | `ARGS.C`; `T_ARGST` `ARGSW.C` (host only) | `args_parse` (`ARGS.C`): each switch matched whole, without regard to case, with `/` and `-`; `/LOCAL` with and without its dir, `/NET` with its name (none, a bad one), `/COMn` with `comname` upper-cased, `/CFG:` (also empty); the repeats, `/COM2` after `/COM1`, the order of the results (the first error in the words, clash, bad name); the caller's function (the words it is given, skipped when `NULL`, its error counted at its word while the scan goes on); `dir`, `net` and `cfg` pointing into `argv`; `/TCP:` and `/MBOX:` handed to the function; `T_ARGST`, the same test built with `ARGS_WINSW`, adds `/TCP:` with its address and port at their bounds and every malformed one refused, and `/MBOX:` with its dir as typed and an empty one refused, their repeats and clashes, and `tcp` and `mbox` pointing into `argv` |
| `T_MENU` | `INVMENU.C` | `invmenu_bar` (`INVMENU.C`) against `INVMENU.H`, with the texts of the built `INV.MSG`: the menus and their entries in order, each entry's command and level, one hot letter per text and none twice in the bar or a menu, no text starting or ending with a blank, the key names of `MN_FIND` and `MN_QUIT` alone, no command twice and `SCMD_ITEMS` and `SCMD_MUSTCHG` in none |
| `T_SESS` | `SESS.C` | `SESS.C` over the recording link: `sn_station`'s order of choice, cut to `LINK_STALEN` and fallback reported; `sn_hello` with `ver` empty and not, a failure of either request, a HELLO refused with 101 sent once more and no more; `sn_login` on every outcome from canned responses and the engine, the `LEVEL=` rule with bad values, the name, `level` and `name` left alone on a failure, `pw` wiped whole; `sn_passwd`'s local checks in their order with nothing sent, then done, old password wrong and failed, the three buffers unchanged, a wrong old password never a failed login; `sn_status`'s counts, `?` for missing ones; the exact request lines |
| `T_LINKN` | `LINK.C`, `LINKNET.C` over `NBCLI.C` | the NET link over the simulated NetBIOS against a scripted server: the session's end after +OK to BYE, the `LINK_MAXFAIL`-th counted LOGIN failure and -ERR 501, each hung up at once; no password left in the link's memory; a lost session (`LINK_ELOST`, `RQ_ELOST`); a failing callback; 103 answered locally; empty lines; `link_feed` |
| `T_LINKT` | `LINKTCP.C` (host only) | the TCP link, the socket calls simulated against a scripted peer: the open's address, port (`LINK_TCPPORT` by default, equal to `BR_PORT`) and wait, each refusal's code and Winsock error, no block left; one write of line and CR LF, no password left in what it was sent from or in the link; responses in pieces of any size; the session's end after BYE and the last failed LOGIN, the connection closed at once; a peer that falls silent or closes early and a failed send (`LINK_ELOST`); `link_close` |
| `T_LINKM` | `LINKMBX.C` over `MBXCLI.C` (host only) | the MBOX link, against the server's mailbox, ticket store and engine in the same program: the open's refusals and failed allocations, no file touched; the lines of every request file, with a ticket and without; the ticket cut from the line delivered and replaced by PASSWD's, the response files that held one zeros when removed; a ticket expired or its user disabled, PING, VER and BYE posted again and answered by the server, any other request answered 400 by the link with its password in one file only, 400 next; a failed LOGIN dropping the ticket held; BYE and `link_close` ending the ticket; -ERR 501 before the request; no password left in a block once sent, none or a ticket when freed |
| `T_SERCLI` | `SERCLI.C` | the serial session (`SERCLI.C`) on the simulated ports (`TEST/FAKEUART.C`): the open's refusals, request lines and response framing however split; in its second half `T_SERCL2`, the bounded waits (`SERCLI_SENDWAIT`, `SERCLI_BYEWAIT`), a line longer than the transmit ring, the close with and without BYE, every byte zeroed as it leaves a ring, and receive errors: one of each kind while a response arrives, also inside a `+DATA` response, loses the line (`SERCLI_ELOST`, nothing more sent or read, a last CR LF and BYE at the close), while an error before a request, between exchanges or right after the open, is dropped with its noise and loses no exchange, as is clean noise and an earlier client's answer to its last BYE |
| `T_LINKS` | `LINKSER.C`, `INVCOM.C`, `PORTCHK.C` | the SERIAL link (`LINKSER.C`), `INVCOM.C` and COMTEST's loopback check (`PORTCHK.C`) on the simulated ports: the open's refusals (`LINK_ENOPORT`, `LINK_ESETUP` for a rate or flag the port refuses, `LINK_EBUSY`, `LINK_ENOMEM`), requests, 103, no password left, the session's end without BYE, `link_close` with BYE, a response that never comes; the loopback report with every byte back, under RTS/CTS, on a slow line, a byte lost and one changed |
| `T_MSG` | `MSG.C` | loading, escapes, placeholders, the rotating buffers, malformed lines, a foreign stamp, the missing file; paged ranges (first use, least recently used slot, an unreadable or changed page answering "?nnn", page and read-area limits); in `T_MSG2.C` the far blocks of a load (freed last in first out, the size of the one kept), a load failing at each read and allocation, the file changed between its two reads; the text file reader on a handle and a far block; `msg_fread`, `msg_section`, `msg_date`, `msg_dec` |
| `T_MOUSE` | `CMOUSE.C` | `key_ms_press`: the cell of a press from its pixel, at the edges and past the screen; a click, and a second press on its cell within `KEY_MS_DBLTICKS` ticks a double click, to the tick on the host |
| `T_TUI` | `TUI.C`, `KEYSCR.C` | hints with level digits, line editor (control bytes and DEL refused), pick list, message boxes and confirm, help through the file's index, dialogs and menus refused without memory, dimmed menu entries and titles, the global keys, `tui_fail` texts, a lost NET link ending with `TUI_EXIT_LOST`, the idle hook removed, the idle hook's `PING` every `TUI_PINGT` ticks on NET and serial links, at a steady interval (counted on the host, whose clock is virtual), none during a request (`rq_send` not called), a lost link and an ended session at a `PING`, none on LOCAL, the DOS date read once past midnight (on the host), every palette's `TA_ALERT`, the main loop; in its second half, `T_TUI2`, the key scripts and snapshot files, the key names, `tui_hit` and the clicks of key scripts and of the hint row |
| `T_LISTV` | `LISTV.C` | paging over several hundred items: multi-request pages, Up/Down across A and B, PgUp through the ring and past `LV_MAXBACK`, End, reload after a delete, bad-cursor restart, sort orders, `lv_seek`, row marks; Esc and Ctrl-Break during a fill and the End walk |
| `T_ITEMLS` | `ITEMLS.C` | the item list's source from `ITEMLS.H`'s contract: the template's orders, command, field lists, End-walk field and messages, the browse commands' agreement with the orders, `itl_row`'s columns and keys for every order, the word of each works letter, and the template as a client uses it, a pager over the engine through `TRLINK`, also turned into another item command |
| `T_CODETB` | `CODETAB.C` | the tags of a code list from `CODETAB.H`'s contract: `ct_tags_add` (append, separators dropped, a tag already there whole and A-Z folded, the exact fit and one over) and `ct_load_tags` over the engine through `TRLINK`: no tags, a few in their first spelling and A-Z folded order, pages merged with the wait function between them, more than `LINK_MAXCODES` kept as the first A-Z with `part`, a later page's tag sorting first, a stop by the wait function, a failed request |
| `T_MEMOIO` | `MEMOIO.C` | `MEMOIO.C` over the engine through `TRLINK`, from `MEMOIO.H`'s contract: loads of no line, one and `MEMO_MAXLINES` and one more, a line of exactly `MEMO_COLS` and the conversions, the state of truncated and too long memos, the line edits keeping the bytes, a SET of exactly `LINK_MAXLINE` and the split to NOTES NEW= and END=, the longest memo's NEW=, PART= and END= read back, trailing blanks, the clear, saves failing at each request with the stored memo kept and the session ending, the CR LF text both ways |
| `T_PAGER` | `PAGER.C` (host only) | the pager alone over the engine through `TRLINK`, from `PAGER.H`'s contract: the moves, the cursor ring, End, reload, sort orders and the request lines, multi-request fills, the wait and fail functions, the bad-cursor restart, `pg_seek`, the readers, small lists, the rows above A after every kind of move, an unpaged `CAT LIST` (bare requests, PgDn, Down, PgUp, End, Reload, above, a full last page, a seek over its pages, a lead row) |
| `T_PGSV` | `PAGERSV.C` (host only) | the survey and the jump of `PAGERSV.H` over the engine through `TRLINK`, against the test's own paging of each list: the cursors' block (taken once, none without memory), the counts and exact requests for pages of 1 row to `LV_MAXPAGE`, several requests a page, an empty list, a lead row, no walk field, a second order, filtered lists whose scans end early, one whose empty page after its last full page is not kept, lists of exactly `PSV_MAXPAGES` pages and longer; the wait function's calls and stops, failed requests; the pager's rows, window, ring, above and order unchanged by a survey; a jump to every page and past the last kept, then moves up past the ring, down and End with above checked; a jump's bad cursor, restart, other failure and stopped fill; the keys a survey collects (in order, the lead row's not, over for a list longer than the table or a set without memory, none after a stop or a failure, no set, the key sink off after); an unpaged `CAT LIST` surveyed, its keys, a jump to every page |
| `T_KEYSET` | `KEYSET.C` (host only) | the key set of `KEYSET.H`: the empty set, the block's growth from `KS_FIRST` to `CMEM_MAXBLK` with the old block freed, keys read back in order, keys of every length and one cut, `KS_MAXROWS` and the refusals after it, no memory for the first or a larger block, `ks_get` past the end, `ks_free` |
| `T_FORM` | `FORM.C` | each field type's checks, `form_save` splitting over `LINK_MAXLINE`, the ADD's key and required fields, the form becoming an edit form after ADD, partial-save marking, `FIELD=` focus, the global keys kept while changed, `form_wipe`; in its second half, `T_FORM2`, the memo save in parts read back with NOTES, a failed one keeping the stored memo, read-only memos, the screens' shared helpers, and the fields Tab and clicks stop at with the value entered selected, replaced, cleared or edited in place |
| `T_FMODEL` | `FMODEL.C` (host only) | the form model from `FMODEL.H` alone over a recording link: `form_new`'s limits and block, values and cuts, every `MF_` check, the ADD/SET split with exact request lines, partial, `FIELD=` lookup, the choice helpers, the wiping of secrets, independence from the drawing members |
| `T_CFG` | `CFG.C` | load with unknown keys, comments, bad and over-long lines, text values with a control byte or DEL; save keeping them; the scratch-file fallback; the printer and serial keys, their defaults and ranges; paths up to `CFG_PATHMAX`; scratch names refused by load and save; a failed scratch write or close, or old file that cannot be removed, leaving the old file and no scratch file |
| `T_PRINT` | `PRINT.C` | output jobs over the capture file: ESC @ first in an Epson job, the form length, style codes only when they change, long text, control bytes as `?`, plain and file jobs; a new-file job refusing an existing file; the wait function on a failed write (retry sends exactly the bytes not taken) and on an absent printer; file errors; on the host, no memory |
| `T_RPT` | `RPT.C` | every report and label kind laid out from made-up rows into a file and into Epson bytes, compared with `TEST/GOLDEN/PRT/RPT*.TXT` and `.PRN`; `T_RPT -u golddir` rewrites them |
| `T_RRUN` | `RPTRUN.C` | the run of each report and label kind over the recording link: its requests, paging, the step function and stop, failed requests and printing, a missing GET record, the end by close or abort |
| `T_PRN` | `CPRN.C` (DOS only) | every byte value through `PRN_LPT1` and `PRN_DOS` into the capture file, a capture write failing part-way, a device or over-long path refused, LPT2 absent; with `LPT`, the same jobs to the real port and device, for `TEST/T_PRN.SH` |
| `T_SCRE`, `T_SCRR` | the screens, without `INV.C` (harness `T_SCRH.C`, `T_SCRH2.C`) | Screen tests without INV, below |

- **Fault injection**: the `cf_fault` hook of `COMPAT.H` makes every module's file operations
  fail from a chosen call on. `T_PACK`, `T_EDIT`, `T_OPENFT`, `T_HEAL`, `T_CSVF` and `T_ADMIN`
  use it (through the fault recorder of `TEST/T_FILES.H` where they sweep), so a stop at any
  disk-changing call, and in `T_PACK` a stop of the recovery itself, must leave a complete old
  or new state. `T_OPENFT`, `T_HEAL`, `T_CSVF` and `T_ADMIN` take an optional step after their
  directories (every n-th call).
- **Simulated NetBIOS**: `TEST/FAKENB.C` takes the place of `SRC/COMPAT/CNETB.C` in the link of
  `T_FAKENB`, `T_TNETB`, `T_INVSRV`, `T_NBCLI`, `T_INVTST`, `T_LINKN`, `T_NETE`, `T_BRIDGE`,
  `T_TCPE`, `T_INVTCP` and `T_NBDRN`. It defines every `nb_` and `nba_` call of `COMPAT.H` with
  the provider's return codes and simulates a provider and a network in portable C, so a client,
  a server or both run over NetBIOS on the host and under every DOS build alike; `TEST/FAKENB.H`
  describes it.
- **Simulated TCP**: `TEST/TSFAKE.C` takes the place of `SRC/TCP/TSWAT.C` in the link of
  `T_TSFAKE`, `T_BRIDGE`, `T_TCPE` and `T_INVTCP`. It defines every `ts_` call of
  `SRC/TCP/TSOCK.H` with the contracts given there and simulates the stack and its peers without
  a packet driver; it is never kinder than the stack (what `TSOCK.H` lets the stack drop, it
  drops). `TEST/TSFAKE.H` describes it.
- **Goldens**: `TEST/GOLDEN/DB3/` holds tables and indexes written by dBASE III Plus 1.1 (dBASE
  reference files, below). `TEST/DBASE/mkkx.sh` regenerates `TEST/GOLDEN/KEYEXPR.TXT`, having
  dBASE evaluate `KXCHECK.PRG` and build an index per expression, whose every leaf key
  `kxndx.py` checks. dBASE computes in 80-bit extended precision and stores doubles; `KEYEXPR.C`
  emulates that rounding in integers. `TEST/GOLDEN/SRV/*.SNP` are the status screens `T_SRVSCR`
  compares (format in the header of `TEST/T_SRVSCR.C`); `T_SRVSCR -u` rewrites them.

### Protocol test scripts

`TEST/T_PROTO.C` runs `TEST/SCRIPTS/*.SCR` through `proto_dispatch` with an in-memory sink; the
directives that set up data, files and the host are in `TEST/T_PROTO2.C`, and the header comment
of `T_PROTO.C` gives each directive's full rules. Its arguments are a data directory prefix (an
existing, otherwise unused directory such as `OBJ/HOST/PDATA/`; a scratch drive under DOS) and
script paths; without paths it runs `default_scripts` from its own directory, as on a DOS test
drive. Before each script, and after the last, it removes every engine file, every leftover of
an interrupted pack, reindex, create, edit or import, and the files the scripts write
(`script_files`, `te_remove`); a failed `eng_close` is a failure. Each script starts with a fresh
engine, the clock fixed at 2026-10-08 12:00:00, and a new session. Script lines:

| Line | Meaning |
|---|---|
| `# text` or blank | comment |
| `> request` | sent to `proto_dispatch` as it is (`>` alone: an empty line) |
| `@SEND text` | sent like `>`, `\\` and `\xHH` decoded; a final `*n` repeats the byte before it to n copies (`@SEND PING\x20*246`); at most `SCRLINE` − 1 bytes |
| `@REPEAT n` | send the next `>` or `@SEND` line n times; each response must be `+OK`, or `+OK ID=` one more than the last; the `<` lines match the last response |
| `< line` | the next output line, compared exactly (`<` alone: an empty line) |
| `<*` | ignore the rest of the current output |
| `@CLOSED` | the last request returned `PROTO_R_CLOSE` |
| `@SESSION [station]` | a new session (`sess_init`), optionally with a fixed station |
| `@REOPEN` | close and reopen the engine on the same files |
| `@NOW YYYYMMDD HHMMSS` | fix the clock |
| `@READONLY [STA1,STA2]` | set `proto_host.readonly` (none without an argument) |
| `@MEMO table recno field count text` | store `count` copies of `text` as the memo of a record as they are (`eng_edit_putmemo`), audited, at most `ENG_MAXMEMO` bytes |
| `@STATUS n` | the host adds `K HOSTLINE=1` … `K HOSTLINE=n` to STATUS, after `K HOSTROOM=r` when its room is limited |
| `@ROOM n` | the sink reports n bytes of room less what the response has written (0: unlimited) |
| `@TABLE name [FIELDS=a,b]` | an `F` line and per record `R` (live) or `D` (deleted) with its values; every field, hidden ones included, without FIELDS= |
| `@OTHERS n` | `proto_host.others` is n (0 at the start of every script) |
| `@PEERS n [SELF=j] [IDLE=s]` | `proto_host.peers` lists n sessions, j the script's own |
| `@STOP n` | `proto_host.stop` is set and must be called n times, each after exactly `+OK` |
| `@WRITE file`, then `= text` lines | create `file` with each `text` and CR LF |
| `@FILE file` | output the CR LF lines of `file`, `\` as `\\` and bytes below 20h and from 7Fh up as `\xHH`; `(no file)` when it cannot be opened |
| `@REMOVE file` | remove `file` if it exists |
| `@FAULT n [code]` | the n-th call from now that creates, writes, truncates, removes or renames a file (`cf_fault`) fails with `code` (default `CF_EFULL`); 0 for none |

`$D` in a request or directive stands for the data directory prefix. Each response, `@TABLE`
dump and `@FILE` listing must be matched by the `<` lines that follow (or skipped by `<*`); a
missing or extra line fails with the script name, line number, expected and actual text. Every
response is also checked for its framing: no output exactly for `PROTO_R_NONE` or a closed
session, else one `+OK` or `-ERR` line or `+DATA` … `.` with nothing after it; every line ends
in CR LF within `PROTO_MAXRESP` bytes; a response without `R` lines, CAT LIST and ADMIN CLIENTS
stay within `PROTO_MAXUNPAGED`. Scripts check audit rows with
`@TABLE AUDIT FIELDS=STATION,USERNAME,TBL,REC_ID,ACTION,FLD,OLDVAL,NEWVAL`. The same program
runs as a gcc host test and as a DOS test under DOSBox-X.

### Screen tests without INV

`T_SCRE` and `T_SCRR` run screens without `INV.C`, on one harness (`TEST/T_SCRH.C`, `T_SCRH2.C`,
which seeds the data, and `T_SCRH.H`): it opens a LOCAL link over `TEST/TRLINK.C` on a seeded
scratch directory, logs in through requests and starts the screens of a script's commands under
its key script, writing snapshots compared with `TEST/GOLDEN/SCR/`. A script's table entry may
have one request answered by the test instead of the engine (`tl_fail`; one line, or a whole
`+DATA` answer, which ends the session after it when it ends in a newline; or `tl_lost`, the
link lost there, or `tl_reset`, the request dispatched in a fresh session, as after the server's
`IDLE=`), some far allocations fail (`cmem_fault`), or, for `T_SCRR`, one file operation fail
(`cf_fault`); with `SF_NET` the screens see a NET link. Every script starts with no help topic
and the cursor hidden, and must free every far block its screens took (`cmem_fault`,
`cmem_freeing`), unless it ends inside a screen (`SF_EXIT`): it then leaves through the exit
function with status `th_status` (0 unless the program sets another), which runs the program's
exit wipe (`th_onexit`, which `T_SCRR` sets to call `scr_wipe`). On the host every near block a
run took must be freed by its end (`tm_held`, checked in `run_once`). Each program links only
the screens it runs, so that `T_SCRE`'s deepest scripts run with the free memory INV needs
(Runtime requirements, below). Their scripts have a `.SNC` golden; the monochrome runs
compare an `.SNM` golden where one exists (`SF_MONO`) and otherwise the text of the `.SNC`
alone. Each script's first comment says what it checks; `T_SCRH.C`'s header describes the seed
and the faults, `T_SCRE.C` and `T_SCRR.C` their own additions and files.

- `T_SCRE` (`TEST/KEYS/SCRE*.KEY`): the item, location, code list, search, assembly,
  maintenance and loan screens, with assemblies, records, loans, a reservation and a use added
  by the scripts that need them: item lists in every order with paging both ways; item add,
  edit, copy (an unchanged copy deleted again) and delete; the pick lists; the memo editor; the
  location tree, its form, moves and contents; the categories and location types; every search
  mode; the item's tabs and their forms; the views of the loans and holds, a reservation and a
  use added with their hosts typed as text, as an ID and as digits that name no item (the active
  items searched for them, a disposed one left out), a row refused for want of units, a release,
  an item's hosted rows and the dispose of a host refused (`SCRE29`, `SCRE71`, `SCRE72`); the
  permission display of a level-1 user; every message for 200, 201, 202 and 203; an item form
  loading its item again after a tab was answered 200 (`SCRE73`); screens and the item prompt's
  pick list that cannot get memory leaving their message on the hint row, the next prompt
  asking again after a refused `ITEM GET`, its refusal kept on the hint row (`SCRE74`); and a
  400 or 404, or a session that ends, reaching `tui_run` through every screen open (back to the
  login, or the end of the program).
- `T_SCRR` (`TEST/KEYS/SCRR*.KEY`): the report, CSV, user, administration, password, settings
  and login screens, on a seed with current values, works states, tags and some disposed items,
  the report forms' Works and Tags filters (`SCRR57`), the printer on LPT2
  (absent on every test machine) or the capture file, compared with
  `TEST/GOLDEN/PRT/<script>.PRN`. The files the screens write land in the current directory.
  Covered: every options form; every report and label kind; the viewer; an absent printer, a
  request refused after the first page, a capture or report file write failing; the viewer's
  scratch file not creatable, its name held by another station or by one with the same checksum,
  all `SCR_RP_SCRTRY` names taken, no `INVRxxxx.$$$` file left behind; Esc during a report run;
  export and import with their refusals, a paged export of the audit log (`SCRR15.CSV`), 204
  and 201 on a later page, Ctrl-Break during an export, refused lines and an import stopped
  halfway; a level-2 user refused every level-3 screen without a request; the user list and
  form (adds, changes, a password reset checked as sent, the last administrator refused, F3,
  the repetition never sent); the own password changed; backup, restore, pack and reindex with
  their refusals, 402, 301 and 302; the audit log filtered, paged and marked; the sessions and
  104 in local mode; near blocks freed and passwords overwritten by the exit wipe (`SCRR48`,
  `SCRR49`); a settings save that cannot create its scratch file; a report file named after a
  data file or a device refused (`SCRR59`); the login screen sending `VER` only once, coming
  back with a new `HELLO` after the session was found ended (`SCRR60`), calling the server
  again for a lost NET link, as the new call's station (`SCRR61`), ending after a refused
  `HELLO` (`SCRR62`) or a lost serial link (`SCRR65`), keeping a refusal's message (`SCRR63`);
  the forced password change without memory for its form logging out, on the host (`SCRR64`);
  and the home screen on a PC whose clock was never set.

The "out of paper" question is checked by hand: it needs a printer port whose INT 17h status
reports paper out, which a key script cannot reach (a failing capture file gives "not ready").

### Golden screens

`/KEYS:file` replaces the keyboard by a key script and `/SNAP:file` writes snapshots, both
described in `KEYSCR.H`. Keys are handed over only when the program asks for one, and a snapshot
is taken at that moment, after `vid_flush`, so the output is the same on every machine and every
run. The keys of a `P` line are typed ahead, seen waiting (`key_peek`) before they are read, so
a script can stop a list fill, a report or an export with Esc. `C row col` clicks the cell at
row, col, and `D row col` is the second press of a double click there, so the mouse is tested
without a driver; the right button is `K ESC`. `M row col len` masks cells that differ between
machines (the settings screen's adapter, DOS version and flags). The host build has a virtual
screen (the shadow) and virtual ticks, so it needs no terminal. Example script:

```
# log in, open the item list, page down, snapshot
T ADMIN
K ENTER
T secret
K ENTER
S HOME
K ALT-I ENTER
S ITEMS1
K PGDN
S ITEMS2
K F3
T Y
```

A snapshot is 25 text rows and 25 attribute rows (`VID_ROWS` each), so the screen is checked cell
by cell. The text is the same on every adapter; the attributes depend on the palette.

`TEST/T_SCR.SH` runs every script in `TEST/KEYS/` except `SCRE*.KEY` and `SCRR*.KEY` in the whole
INV. Each run starts from a fresh scratch current directory, on a data directory given relatively
(`/LOCAL TDATA`) and filled by `INV /CMD` from a seed request file, with the clock fixed by
`/NOW:` and a settings file given relatively (`/CFG:COLOR.CFG` or `/CFG:MONO.CFG`) that fixes
`STATION=TEST`, so no snapshot shows a path, a machine name or a date of its own. `INV.MSG` and
`INV.HLP` are found beside the program. Each script has a `TP_COLOR` golden (`.SNC`) and a
`TP_MONO` golden (`.SNM`); one also has a `TP_MONOCGA` golden (`.SNB`). The header of `T_SCR.SH`
lists the `#@` directives a script may carry, among them `#@ PRN`, which gives every run
`/PRN:file` and compares the captured print jobs with `TEST/GOLDEN/PRT/<script>.PRN`, and `#@
FILE NAME`, which compares the file the run writes with `TEST/GOLDEN/PRT/<script>.<extension>`,
on every platform. `#@ DEEP` seeds `TEST/KEYS/DEEP.REQ` after the seed, which makes every list on
the deepest paths longer than a page, and the location tree and the item list in maker order so
long that their cursors move their rings to the larger block; the deepest scripts carry it and
page each list they pass to its end and back (End, Home), so that each holds its ring of page
cursors, as INV's free memory figure counts them (Runtime requirements, below).
`#@ ROOMS` seeds the rooms of `TEST/mkrooms.sh` after the seed, `LV_SEEKMAX` of them, so that
a location pick list's seek gives up before the last of them. `#@ SAMPLE` seeds the release's
sample data (`REL/SAMPLE.REQ`) in place of the seed, so a golden shows what a new user first
sees. The runs:

| Where | Palette | Runs |
|---|---|---|
| Host (`make test`) | `TP_COLOR`; `TP_MONO` with the host-only switch `/MDA`; `TP_MONOCGA` with `MONO=1` for its one script | every script |
| DOSBox-X, `machine=cga`, 8086 (`make dostest`) | `TP_COLOR` | every script; one script again with `BIOS=1` and with `SNOW=1`, against the same goldens; the `MONO=1` script |
| DOSBox-X, `machine=hercules` | `TP_MONO`, chosen by `vid_mono` alone | every script |
| Compaq MS-DOS 3.10 under QEMU (VGA, reported as EGA) | `TP_COLOR` | every script: the boot test of the full-screen client |

Every snapshot of a DOS run must also report `VRAM=OK`, and every DOS run must leave a stack
margin: the last line of the snapshot file, `=STACK UNUSED=n`, is left out of the golden
comparison and checked against the harness's margin instead (the host writes no such line).

The `T_SCR.SH` scripts, each described by its first comment, cover what needs the whole INV:
login (a bad password, the third failure ending with status 5, the first-run ADMIN forced to
change its password, a log-out from that change), the home screen, every menu entry, the
permission display of a level-1 and a level-2 user, screens reached from the menus and Ctrl-F, help,
settings saved and read back (the file compared too), password rules, the item pick list of a
loan and an install (`PICK1`, `PICK2`, `PICK3`), the names in the pick lists of choices
(`CHOICES`), the release's sample data with its reservation and the monitor in use with the
5150 (`SAMPLE`), tags and the works state in the item form (`TAGFORM`), the item form typed in
order with its read-only fields passed and each value entered selected (`TYPEIN`), the tag pick list over
many tags, stopped and whole (`TAGPICK`), the search's Tags and Works filters on the sample data
(`SRCHTAG`), the deepest nestings of screens with every screen linked (`DEEPLOAN`, `DEEPHOLD`,
`DEEPMEMO`, `DEEPPICK`, `DEEPITEM`, `DEEPTAG`, `RPTHELP`), and printing and
files: `PRN1` (loans, then the item listing, valuation and open loans and holds printed; the
seed has no prices, so its valuation shows zeros), `PRN2` (item and bin labels), `RFILE` (a
report written to a file, then replaced) and `CSV1` (locations and items exported and imported
back).

`TEST/T_PRN.SH` then prints `PRN1` and `PRN2` on a real port (Printing on the parallel port,
below).

On the host, `INV /LOCAL dir` without `/KEYS:` ends at once with status 0, because the host
keyboard has no keys (`key_read` returns `KEY_EOS`); under DOS the same command waits for the
keyboard. What emulation cannot show (real CGA snow, the look of a real MDA monitor, a real
keyboard) is checked by hand on the hardware.

### dBASE reference files

Every `.DBF`, `.DBT` and `.NDX` file of `TEST/GOLDEN/DB3/` was written by dBASE III Plus 1.1
itself, running headless in DOSBox-X 2025.02.01 (SDL2, Debian package), so readers and writers
are tested against what the original program writes. The leftover memory that dBASE copies
into some header bytes includes the DOSBox-X DOS environment (its PATH and `Z:\COMMAND.COM`), so
another DOSBox-X version or configuration can change those bytes and nothing else. What dBASE
leaves in them that a reader must tolerate and need not reproduce is in `DOC/ENGINE.md` (File
format, NDX layout). Regenerate them with:

```
TEST/DBASE/install.sh "${RETRO_DL:-$HOME/retro-dl}/dbase11/dBase III Plus 1.1 (1987) (3.5-720k)" "${RETRO_DL:-$HOME/retro-dl}/dbase11/DBASE"
TEST/DBASE/mkgold.sh
```

`install.sh` installs dBASE outside the repository; `mkgold.sh` runs `TEST/DBASE/MKGOLD.PRG`
through `TEST/DBASE/rundb.sh` with the DOS date set to 2026-10-08 and copies the results there.
Two runs give byte-identical files. `TEST/CHKDBF.PY --strict` accepts every file, and
`TEST/DBASE/chkbad.py` checks that it rejects damaged copies.

| File | Contents |
|---|---|
| `ITEMS.DBF`, `ITEMS.DBT` | an ITEMS layout modelled on ITEMS.DBF's (`DOC/ENGINE.md`; version 0x83, 21 fields); 5 records: full values, a negative and a blank N(9,2), a blank date, a deleted record, a record that is blank except for its ID, a short memo, a memo longer than one 512-byte block followed by another memo, empty memos |
| `ITEMS1.NDX` | `INDEX ON STR(ITEM_ID,8)`: one leaf page |
| `CATS.DBF`, `LOANS.DBF` | layouts without memo (version 0x03); LOANS has a blank RET_DATE |
| `USERS.DBF` | the USERS layout; the logical MUSTCHG holds `T`, `F` and blank |
| `BIG.DBF` | 300 records (ID N 8, NAME C 10, D date) |
| `BIG1.NDX` | `STR(ID,8)`, built in one pass by `INDEX ON`: full leaves under one root |
| `BIG2.NDX` | `NAME`, created on the empty table and updated by every append in scrambled key order, so it shows page splits |
| `BIG3.NDX` | the date field `D`: numeric keys (IEEE double holding the Julian day number) |
| `NUM2000.DBF`, `NUM2000.NDX`, `NUM993.DBF`, `NUM993.NDX` | `STR(ID,8)` over 2000 and 993 records: three levels, built bottom-up by `INDEX ON`, with leaves at different depths (a trailing single-child level is folded into its parent) and inner pages holding 31 keys plus the rightmost child |
| `WIDE.DBF`, `WIDE1.NDX` | 60 records with a 100-character key (4 keys per page); records 30-37 share one key |
| `WIDED.DBF`, `WIDED1.NDX` | a copy of WIDE whose index then had the keys of records 1-16 and of the equal-key run replaced (delete plus insert): emptied pages stay in the file outside the tree, and inner pages are left with no key and one child in slot 0 |
| `MKGOLD.TXT` | dBASE's own `LIST STRUCTURE` and `LIST` output for every table (CR/LF) |

### Programs and scripts

Each script's header gives its options, settings and checks in full; this section says what each
one is for and what it must show.

- **The test tools** `NBTEST`, `TCPTEST` and `COMTEST` (`DOC/CLIENT.md`) share one convention.
  Operands name things or pick a mode; a setting is `/NAME:value` and a flag `/NAME`, in any
  case, `-` also standing for `/`. `/?` alone writes one `usage:` line per form to standard
  output and exits 0; any other bad command line writes them to standard error, nothing to
  standard output, and exits 1. Otherwise every line of standard output starts with the tool's
  name and a blank, and the last is the verdict, `<TOOL> PASS` or `<TOOL> FAIL <reason>`, the
  reason in a few lower-case words, which may begin with the device (`COMTEST FAIL COM1: no such
  port`); other detail may go to standard error. Exit status: 0 pass, 1 usage error, 2 the
  provider is missing (no NetBIOS, no packet driver, a serial port that cannot be opened), 3 the
  test ran and failed, 4 DOS older than 3.1.
- **NetBIOS probe** (`NBTEST.EXE`): `NBTEST SERVER name [/WAIT:seconds] [/ASYNC] [/EXIT]` adds
  the name and waits that long for a call; `NBTEST CLIENT localname remotename [/COUNT:n]
  [/ASYNC] [/EXIT]` calls it (retrying for a while), exchanges `n` `PING i`/`PONG i` messages,
  sends a bulk message of `NB_MSGMAX` bytes that the server answers with `BULK` and its length,
  and ends with `QUIT`/`BYE`; the defaults are in `NBTEST.C`. The server uses the wait-form
  commands and receives each message whole; with `/ASYNC` it uses the no-wait `nba_` commands
  with a far receive buffer of `RX_LEN` bytes, smaller than `NB_MSGMAX`, so it takes the bulk
  message in pieces through the "incomplete" return code. With `/ASYNC` the client places each
  CALL as a no-wait `nba_call` and polls it, printing `pending=1` when the CALL was still in
  progress once posted. One line per NetBIOS step with its return code; `/EXIT` ends a QEMU VM
  with the exit status.
- **TCP probe** (`TCPTEST.EXE`, on the bridge's sockets of `TSOCK.H` over WATTCP and the packet
  driver, without NetBIOS): `TCPTEST [/PORT:n]` listens on all `TS_SLOTS` slots on port `n`
  (default `BR_PORT`) and echoes each line a client sends; the stack refuses a connection beyond
  the slots. `QUIT` from any client closes every connection, waits up to `TT_CLOSEWAIT` for them
  to end, releases the packet driver and ends with `TCPTEST PASS`. One line per event (`LISTEN`,
  `OPEN`, `CLOSE`, `QUIT`); exit 2 without a packet driver, 3 when the stack does not start or
  listen.
- **Scripted server driver** (`INVTEST.EXE`): `INVTEST <server> <script> [/EXIT]` runs a request
  script against the server `<server>` (`-` for none, NetBIOS then left alone) over up to
  `INVSCR_SESSIONS` sessions at once: NetBIOS sessions, each from a local name of its own so the
  server sees a station per session, serial lines and mailbox sessions; `SRC/TOOLS/INVSCR.H`
  gives the script format. Responses are checked as they travel, dot-stuffing and the `.`
  terminator included. `@CALL` calls again a second apart for up to a minute while the name is
  not up or has no LISTEN posted, or after a CALL that timed out (the stall, under Two-VM
  harness) or was aborted. The transcript shows every request and response line with its
  session's number, passwords as `***`, and ends with `INVTEST PASS` or `INVTEST FAIL line n` and
  a line saying why. A failed mailbox call, the first of the post's last attempt or look, is
  named with its step, a word of `MBXCLI_STEPS`, and DOS error (`mailbox failed: RENAME-TMP, DOS
  error 5`); the critical-error handler is installed, so a disk or network error fails its call
  without a prompt. Exit 0 pass, 1 fail or usage error, 2 no NetBIOS provider, 3 a NetBIOS
  session could not be opened, 4 a serial port could not be opened; `/EXIT` ends a QEMU VM with
  that status.
- **Integration helper**: `BIN/QEXIT.COM` (`TEST/INTEG/QEXIT.ASM`, 8086 only). `QEXIT [n]`
  (0..127, default 0; anything else gives 127) writes `n` to the QEMU `-device
  isa-debug-exit,iobase=0x501,iosize=0x01` port, so the VM ends with host status `n*2+1`;
  without that device it ends with DOS errorlevel `n`.
- **Table tools** (`DBPACK.EXE`, `INVDUMP.EXE`):
  - `INVDUMP [/H] [/S] [/D] [/M] [/F:FIELD,...] [/R:from-to] FILE[.DBF]` prints a table's
    header, fields, records and memos.
  - `DBPACK table[.DBF] [/P] [/R index[.NDX] ...] [/C]` packs a table (`/P`, the default without
    `/R`), rebuilds the listed indexes (`/R`, which must name at least one) and with `/C` only
    finishes or undoes an interrupted pack or reindex; it always runs that recovery first. A
    pack needs room for a full new copy of the table and memo file, a reindex about three times
    the index size; `SRC/DBF/PACK.C` gives the free file handles each needs.
- **DOS 3.10 floor** (`TEST/INTEG/boot310.sh [options] [COMMAND]...`): boots a scratch copy of
  the Compaq MS-DOS 3.10 360K disk in QEMU (default) or DOSBox-X (`-e dosbox`: `cputype=8086`,
  the adapter of `-m`), copies the `-f` files to `A:\`, runs each command with its output
  appended to `A:\OUT.TXT` (console captured through `CTTY COM1`), prints the output and exits
  with the errorlevel of the last external program (0..199; 200 host timeout, 201 other emulator
  end, 2 usage error, 129, 130 or 143 on HUP, INT or TERM). QEMU's oldest CPU model is a 486, so
  8086-only checks need DOSBox-X. Example: `TEST/INTEG/boot310.sh -f BIN/NBTEST.EXE "NBTEST
  SERVER X"` (exits 2: no NetBIOS).
  - **The shell patch.** `COMMAND.COM` keeps its transient part in the free memory at the top,
    which the program under test may overwrite, and reloads it from disk only when a 16-bit sum
    of its words has changed. A program that leaves bytes there with the same sum (about once in
    65,535 layouts, and then every time for that layout) makes the shell jump into them once the
    program ends, and the run hangs, under either emulator. So when the scratch floppy's
    `COMMAND.COM` is one that `DOS_SHELLS` of `TEST/shlib.sh` lists by SHA-256 (those of Compaq
    MS-DOS 3.10, MS-DOS 3.30 and 5.00), the two bytes of its jump that skips the reload become
    NOPs (`shell_patch`), and the shell reloads after every program. The original image is never
    modified. Any other `COMMAND.COM` is left as it is, and `boot310.sh` says so on standard
    error, as it does for an image without one. The emulator check proves the patch.
  - `-D script` plays a user with `TEST/INTEG/dialog.py`: it waits for text in the console
    transcript, types on the keyboard and changes the disk in `B:` (with `-B`), as the script
    says; a command starting with `!` then runs with its output on the console, where the
    script sees it, and a program reads the keyboard through `CON` (`INSTALL <CON`), as the
    console is `COM1`. QEMU only.
  - On a QEMU time-out it reads the CPU's registers through a monitor socket and prints, after
    "timed out after Ns", a line such as `boot310.sh: CPU at F000:FF53, SS:SP 5AAF:0727,
    interrupts off`.
  - `-h` files go to `C:\` of a fresh 16 MB scratch hard disk, attached only with `-h` (a
    `VDISK` is then `D:`), as the floor's floppy has little room: one FAT12 partition, with the
    17 sectors per track MS-DOS 3.10 assumes whatever the BIOS reports. A 1.2 MB floppy is no
    alternative: MS-DOS 3.10 neither boots from nor reads one in QEMU, whose BIOS reports no
    change line.
  - `-p FILE` attaches QEMU's LPT1 to a host file that receives every byte the guest prints.
    `-b` attaches a second floppy drive with no disk in it, so that an access to `B:` raises a
    critical error (without it `B:` is the phantom of `A:`, and DOS's disk-change prompt hangs
    the run); `-B IMAGE` puts a scratch copy of a floppy image there. MS-DOS 3.10 reads only the
    360 KB size there; the MS-DOS 3.30 disk (`DOS330_FLOPPY` of `TEST/shlib.sh`, booted with
    `-i`) also reads 720 KB, 1.2 MB and 1.44 MB images. `-s` gives QEMU's machine a COM2 (2F8h,
    IRQ 3) whose line leads nowhere, `-S SOCKET` one on a unix socket where a host program
    listens. `-e dosbox` refuses `-p`, `-b`, `-B`, `-s` and `-S`.
- **Dump golden test** (`TEST/T_DUMP.SH [-u]`, run by `make test`): builds `TEST/MKITEMS.C`
  (sample `ITEMS`, `LOCS`, `CATS`, `LTYPES` and `USERS` tables with the `DOC/ENGINE.md`
  structures, the locations a small tree, dated 2026-10-08), `INVDUMP` and `DBPACK`, records a
  checksum and header bytes of each generated file, runs `INVDUMP` with every switch and error
  case and `DBPACK` with a recovery-only run and the command lines it refuses (Table tools,
  above), and compares everything with `TEST/GOLDEN/INVDUMP.TXT`; `-u` rewrites it.
- **Scripted client** (`TEST/T_INV.SH`; INV's switches, `/CMD` mode and exit statuses are in
  `DOC/CLIENT.md`):
  - On the host (`make test`), the output of a fixed request script must equal
    `TEST/GOLDEN/INVCMD.TXT` byte for byte (`-u` rewrites it); the test also reopens the data,
    checks every exit status, and runs the full-screen client with the key scripts its header
    lists. Snapshots must not show the password.
  - `TEST/T_INV.SH -d qemu|dosbox [-s | -m KB] EXE` boots Compaq MS-DOS 3.10 through
    `boot310.sh` with INV and its files on `C:` (`FILES=45`, a VDISK as `D:` for the data,
    `SHARE` with `-s`) and runs `TEST/T_INV.BAT`, which first has `CHKDSK` report the free
    memory. The scripted run must reproduce the golden file, its exit statuses (`== name exit
    n`) must match, `SAVECFG.KEY`'s snapshots must report `VRAM=OK`, and the run must leave
    `STACK_MARGIN` stack bytes untouched. With `qemu` a second boot gives the machine a COM2 on
    a host socket (`boot310.sh -S`) whose peer answers every request `+OK`: `INV /COM2` must
    exit with 0 from the login screen. With `dosbox` it also checks, in DOSBox-X's own DOS
    reporting the versions the script lists, the version gate of INV and of `COMTEST`, `NBTEST`
    and `TCPTEST` beside `EXE`, and the tools' command lines, verdicts and exit statuses without
    their providers.
- **Golden screens** (`TEST/T_SCR.SH`): runs the key scripts of Golden screens, above, against
  the goldens of `TEST/GOLDEN/SCR/` and `TEST/GOLDEN/PRT/`; the header of `T_SCR.SH` lists the
  `#@` run options. On MS-DOS 3.10 under QEMU the program, scripts, snapshots and data are on
  `C:`, a VDISK as `D:` taking memory.
  - **Free memory.** With `-m KB` (`T_INV.SH` and `T_PRN.SH` take it too) the VDISK under QEMU
    is sized by `TEST/INTEG/free310.sh` so that INV starts with `KB` kilobytes of free
    conventional memory, less than one more, and each boot first runs `CHKDSK` where INV runs
    (`FREE.BAT`), which must report that figure. In DOSBox-X's own DOS a `LOADFIX` block sized
    the same way takes the memory, and the MS-DOS 3.10 `CHKDSK`, run on a blank floppy, checks
    the figure; `T_INV.SH -d dosbox` boots the MS-DOS 3.10 floppy with a VDISK, as under QEMU.
    The makefile passes the build's `INV_FREEKB_<build>` to every run, so the documented
    requirement is checked (Runtime requirements). Without `-m` the VDISK is `FREE310_MINKB` KB,
    the least RAM disk of `free310.sh`, which leaves every build at least what it needs: `make
    lint` holds it to leaving `DOS_MAXFREEKB` free.
  - DOS runs must leave `STACK_MARGIN` stack bytes untouched (`=STACK UNUSED=n`, not compared).
    The scratch directory is under `/tmp` whatever `TMPDIR` says, as INV accepts at most
    `LINK_MAXDIR` characters for its own directory. `-u` rewrites the goldens from the host run;
    host runs also report goldens that no run compares.
  - `T_SCRE` and `T_SCRR` run their own scripts `TEST/KEYS/SCRE*.KEY` and `SCRR*.KEY` on the
    harness `TEST/T_SCRH.C`; `T_SCRR`'s files lie in its current directory, on the host its
    scratch directory (`T_SCRR_HOSTCWD`). Their DOS boots are set up by `<NAME>_DOSKEYS` and
    `<NAME>_DOSFLOOR` (DOS unit, below).
- **Printing on the parallel port** (`TEST/T_PRN.SH [-m KB] EXE...`): for each `INV.EXE`, on
  Compaq MS-DOS 3.10 under QEMU with LPT1 writing to a host file (`boot310.sh -p`): the same
  build's `T_PRN.EXE LPT`, whose two jobs (the byte values 00h..FFh with a tag, through INT 17h
  and through the DOS device PRN in raw mode) must reach the file unchanged; then `PRN1` and
  `PRN2` run by INV without `/PRN:` with `TEST/KEYS/PRNLPT.CFG` (LPT1, EPSON), and `PRN1` again
  with `TEST/KEYS/PRNDOS.CFG` (the DOS device PRN); each printer file must equal
  `TEST/GOLDEN/PRT/PRN1.PRN` or `PRN2.PRN`, the goldens of the host's `/PRN:` runs. The data is
  on a VDISK, sized as for `T_SCR.SH` (through `TEST/INTEG/boot310m.sh`). No settings or capture
  file is named after a DOS device: DOS takes `PRN` with any extension for the printer.
- **Near data.** `TEST/mapchk.sh` fails a build whose map has a CODE segment over `MAX_CODE`,
  near data in DGROUP (stack excluded) over the program's budget (`<NAME>_MAXNEAR` of `SRCS.MK`,
  or mapchk's `MAX_NEAR`), a far data segment over `MAX_FAR`, a budget or extent of the near
  data that with the stack and the near heap of the program's near-heap module (`CMEMHN.C`,
  `CMEMHI.C`, `CMEMHR.C` or `CMEMHT.C`) exceeds 64 KB, or `CMEM.C` linked without exactly one
  such module. `make all` runs it on every Open Watcom map, `make tc` and `make wc65` on every
  TLINK `/s` and WLINK 4.1 map. For a Windows program `-w` gives it the stack, its
  `<NAME>_WINSTACK` or `WIN_STACK`, and has it expect no near-heap module, as Windows grows the
  local heap in DGROUP.
  The unit tests obey their programs' budgets under Open Watcom and Watcom C 6.5 (`T_CWATT`
  takes `INVTCP_MAXNEAR`); under Turbo C 2.01, which stores every repeated string literal again,
  they are checked with `-f`, only for DGROUP to fit in 64 KB, since Turbo C's start-up code
  ends a program whose static data, stack and near heap do not. Each build also checks the
  objects of each program of `NEAR_EXES` (INV and THIN) with `TEST/OMFNEAR.PY` against
  `TEST/NEARBUD.TXT`, failing a module without a budget line.
- **Load size.** The free-memory floors (`*_FREEKB_*` of `MAKEFILE`, Runtime requirements) are
  measured by gates that take an hour, and a floor can leave the deepest screens only a few
  bytes: a change that adds a few bytes of code can move DGROUP up a paragraph and fail one.
  So `TEST/mapchk.sh -l BUILD` also holds each program of `LOAD_EXES` (INV, THIN, INVSERV,
  INVTEST, INVTCP) to its line in `TEST/LOADBUD.TXT`: the load size, the end of the map's
  highest segment rounded up to a paragraph, at which its floors last passed. `make all`, `make
  tc` and `make wc65` check every such map in the same run as the near data, in milliseconds;
  `sh TEST/mapchk.sh -v` prints the load size. A program that grows past its line fails its
  build with the bytes over. Trim it, or, when the growth is justified, run the gates of its
  floors (`make integ`, and `make tcp` for INVTCP, with every build) on the larger program: if
  they pass, raise the line to the load size printed, in the same change; if not, raise the
  floor in `MAKEFILE` until they do, then the line. A program that shrinks keeps its line.
- **Emulator check** (first step of `dostest`, `tc310` and `wc65310`): QEMU's direct chaining of
  translated blocks does not wrap IP to 16 bits on a near CALL or JMP whose target lies across
  offset 0 of the code segment (seen with QEMU 10.0.13). Open Watcom medium-model programs make
  such calls, as the linker turns a far call within one code segment into `PUSH CS` and a near
  `CALL`, and hang at a spot that depends on where DOS loaded them. Every QEMU command line in
  `TEST/INTEG/` therefore passes `-d nochain -D /dev/null`, and `TEST/INTEG/WRAPCHK.ASM` must
  print `WRAP OK` under `boot310.sh` before the DOS tests run. In the same boot
  `TEST/INTEG/SHELLCHK.ASM` garbles the 64 KB below the top of memory, keeping the shell's
  checksum, and the internal command `ECHO SHELL OK` must then print `SHELL OK`, which only a
  shell that reloaded its transient part does (the shell patch); a shell that trusts the
  checksum hangs, and the boot's time-out fails the check. `dostest` then runs `TEST/T_BOOT.SH
  BIN/SHELLCHK.COM`, which checks `shell_patch` on the host and `boot310.sh` itself in boots
  side by side under both emulators, on the MS-DOS 3.10, 3.30 and 5.00 shells and on a copy with
  an unknown shell, including the time-out and CPU lines.
- **DOS unit** (`dostest`): the unit tests as Open Watcom DOS programs, first under DOSBox-X
  (`TEST/rundos.sh [-m cga|hercules] [-t SECS] EXE...`: DOS 3.10, 8086, the test's own directory
  as a read-only `C:`, a scratch `D:` passed as `argv[1]`; a run passes only with exit 0 and the
  test's `<NAME> PASS n` line, so an EXE that DOS cannot load fails), then on the real Compaq
  MS-DOS 3.10 under QEMU, whose directory updates and sharing behaviour DOSBox-X does not
  reproduce. On MS-DOS 3.10 every test runs from the scratch hard disk `C:` of `boot310.sh`, and
  the floppy boots get `FILES=45`. `tc310` and `wc65310` run their builds of these tests the
  same way. Extra runs on MS-DOS 3.10:
  - each test of `DOS_SHARE_TESTS` that is also in `DOS_TESTS` again as `C:\<NAME> A:\ SHARE`
    with `SHARE.EXE` loaded;
  - `T_CFILE` again as `C:\T_CFILE A:\ NODISK` with an empty `B:` (`boot310.sh -b`);
  - `T_VIDEO` again as `C:\T_VIDEO A:\ PAGE1` between `PAGE1` and `PAGE1 CHECK`
    (`BIN/PAGE1.COM`, `TEST/INTEG/PAGE1.ASM`, 8086 only): `PAGE1` paints video pages 0 and 1,
    page 1 in another attribute, and selects page 1, so the client starts there; `PAGE1 CHECK`,
    whose errorlevel is the run's, requires page 1 still shown as painted;
  - `T_VIDEO` again as `C:\T_VIDEO A:\ WIDE` after `PAGE1 WIDE`, which also sets the column
    count at 0040:004A to 81 in mode 3: the client must set the mode, so its first open finds a
    cleared screen.

  The tests of `DOS_MONO_TESTS` run under DOSBox-X a second time with `-m hercules`. Per-test
  settings in `SRCS.MK`:

  | Setting | Effect |
  |---|---|
  | `<NAME>_DOSDATA` | files that go next to the test, and on MS-DOS 3.10 both beside it on `C:` and onto its floppy |
  | `<NAME>_DOSBOXDATA` | files that go next to the test for the DOSBox-X runs only, never onto a floppy |
  | `<NAME>_DOSRAM` | on MS-DOS 3.10, run against a VDISK RAM disk as `D:`, each size with its own `CONFIG.SYS` in `OBJ/DOS310R<KB>/` |
  | `<NAME>_DOSRAMKB` | the size of that RAM disk in KB (default `DOS310_RAMKB`); VDISK takes it from conventional memory, so a test that needs much memory gets a smaller one |
  | `<NAME>_DOSRUNS`, `<NAME>_DOSRUNn` | boot numbers when the input files do not fit on one floppy, and the files of boot n |
  | `<NAME>_DOSKEYS` | for a screen test, the number of its key scripts, one per boot: it stands for `<NAME>_DOSRUNS` and `<NAME>_DOSRUNn`, each boot with its script, its golden and the files of `<NAME>_DOSKEYDATA` of the same name |
  | `<NAME>_DOSFLOOR` | boot numbers that run through `TEST/INTEG/boot310m.sh` with a RAM disk sized by `TEST/INTEG/free310.sh` to leave the build's `INV_FREEKB_<build>` free, which `CHKDSK` must report before the test in the same boot |
  | `<NAME>_DOSTIME` | the limit of each of its MS-DOS 3.10 boots, in seconds, in place of `dj_qtime`'s default |
  | `<NAME>_DOSARGS` | added to its MS-DOS 3.10 command line (for the fault sweeps, the step) |
  | `<NAME>_DOSPRE` | a command line each of its unit-test boots on MS-DOS 3.10 runs before the test's own |
  | `<NAME>_DOSSERIAL` | its runs go alone, before the other runs of each stage, or under `make gate` at its end (`dosserial`) |
  | `<NAME>_NODOSBOX` | not run under DOSBox-X (the full fault sweeps, which outlast the time a DOS run is given) |
  | `<NAME>_UART` | its MS-DOS 3.10 boots get a COM2 whose line leads nowhere (`boot310.sh -s`) |
- **Compatibility**: `TEST/CHKDBF.PY [--strict] [--table T.DBF] FILE...` (a stdlib-only
  structural validator for DBF, DBT and NDX, run as `python3 -I`; `--strict` also rejects what
  dBASE III Plus 1.1 itself never writes) must accept `TEST/GOLDEN/DB3/`, and
  `TEST/DBASE/chkbad.py` checks that it rejects damaged copies of those files. dBASE runs only
  inside headless DOSBox-X:
  - `TEST/DBASE/install.sh SRC DEST` installs it outside the repository from the 720 KB system
    disk image (checked by SHA-256), typing the user, company and serial (`DB_USER`,
    `DB_COMPANY`, `DB_SERIAL`; the serial defaults to the `Serial:` line of the archive's notes
    file) into the vendor's `ID.EXE`.
  - `TEST/DBASE/rundb.sh [-t SECS] [-o FILE] WORKDIR PRG` runs one program with WORKDIR as `C:`
    and the install (`DB_DIR`, default `$RETRO_DL/dbase11/DBASE`) as `D:`, the DOS date from
    `DB_DATE`. dBASE III Plus has no `LIST TO FILE` (output goes through `SET ALTERNATE`) and no
    `REPLACE` into memo fields (memos are typed into the memo editor with keys queued by
    `KSTUFF.COM`). Every program ends with `DO DBEND`, and `rundb.sh` succeeds only when that
    marker file exists, so a dBASE that never ran cannot pass: exit 1 on a dBASE error
    (`DBERR.PRG`), 2 setup error, 3 timeout or no marker.
  - `TEST/DBASE/mkgold.sh [OUTDIR]` regenerates the golden files from `MKGOLD.PRG` byte for
    byte.
  - `TEST/DBASE/check.sh [-n | -e EXPECTED] WORKDIR` opens our `ITEMS`, `LOCS`, `CATS`, `LTYPES`
    and `USERS` tables with `CHECK.PRG` and compares dBASE's listing with
    `TEST/GOLDEN/DB3CHK.TXT` (default): dBASE opens many damaged tables silently, so the
    listing, not the absence of errors, is the check.
  - `TEST/DBASE/packchk.sh [-w]` packs copies of the golden tables and rebuilds their indexes
    with the host `DBPACK`, then has dBASE `SEEK` every key, walk every index, append records
    with `REPLACE` and `SEEK` again (`PACKCHK.PRG`), the report compared with
    `TEST/GOLDEN/PACKCHK.TXT`.
  - `TEST/DBASE/csvchk.sh [-w] INVBIN` compares the CSV files INV exports with dBASE's `COPY TO
    ... DELIMITED`, has dBASE `APPEND FROM` them (listing compared with
    `TEST/GOLDEN/CSVCHK.TXT`), imports dBASE's ITEMS and LOCS files through INV and compares
    them with INVDUMP, the location tree and the items' places included, then runs `CHKDBF.PY
    --strict` and `check.sh -n`.
- **NetBIOS VM image** (`TEST/INTEG/mkimg.sh`): builds `$NET330_DIR/BASE.IMG` (default
  `TEST/INTEG/vm/net330/`) from the MS-DOS 3.30 disks and MS Network Client 3.0 (`DOS330_DIR`,
  `MSCLIENT_DIR`).
  - The client's self-extracting `DSK3-*.EXE` are ZIP archives behind a PKLITE stub, read in
    Python on the host, the stub never run; SZDD files are expanded with `msexpand`. DOS's own
    `FORMAT C: /S` installs DOS inside QEMU, and the client goes to `C:\NET` with the templates
    of `TEST/INTEG/`: `CONFIG.SYS` (`FILES=45`, which the engine's open checks, and
    `BUFFERS=18`: the stall of the Two-VM harness depends on the memory layout these two set,
    and with `BUFFERS=19` beside `FILES=45` the NetBIOS gate fails; the two VMs' unequal NetBEUI
    T1 timers keep their polls from crossing after a loss (`net330.sh`)), `AUTOEXEC.BAT` (SHARE,
    PCI setup, `NET START`), `SYSTEM.INI` and `PROTOCOL.INI` (`AM2100.DOS` at C000h/IRQ 10,
    NetBEUI as LANA 0 with `SESSIONS=16` and `NCBS=32`, room for a server's `CONN_NETMAX`
    sessions and its clients, NDISHLP, the basic redirector).
  - `SETUP.EXE` must run once to create its binary `WFWSYS.CFG`; `qmon.py` drives it headless
    through the QEMU monitor's view of the text screen, stopping at a screen it does not expect.
  - `img_shell` of `imglib.sh` patches `C:\COMMAND.COM` and `C:\DOS\COMMAND.COM` as `boot310.sh`
    patches its floppy's, since the servers run at their memory floors, and proves the patch in
    one boot (`SHELLCHK`, then an `ECHO` only a reloaded shell runs).
  - The image is built in `$NET330_DIR.new` and replaces the old one only once its checks pass.
    Each VM image (this one, the TCP/IP and the WGA image) records in `INPUTS.SHA` the hash of
    the files it is built and proved from and is rebuilt when they change; `run2vm.sh` refuses
    an out-of-date NetBIOS image.
- **Network card**: each VM has QEMU's AMD PCnet (`pcnet`, PCI slot 5, where SeaBIOS assigns I/O
  C000h and IRQ 10) driven by the client's unmodified NE2100 driver `AM2100.DOS`. The card is a
  PCI bus master, which `AM2100.DOS`, written for the ISA NE2100, never enables and SeaBIOS
  leaves off, so the test VMs turn it on (test-VM setup only, not part of a real installation):
  `AUTOEXEC.BAT` first runs `C:\NET\PCNET.SCR` through DEBUG, which finds the card with the PCI
  BIOS (INT 1Ah B102h), sets bus-master enable in its command register and records the command
  register, I/O base and interrupt line in `C:\NET\PCNET.OUT`. `run2vm.sh` fails a VM whose card
  is missing, has bus mastering off or another I/O base or IRQ than `PROTOCOL.INI`, naming the
  cause. QEMU's NE2000 does not carry NetBEUI: Microsoft's `NE2000.DOS` sends an idle frame with
  a byte count of 0, after which QEMU 10.0's netdev takes no further frame from that card.
- **Two-VM harness** (`TEST/INTEG/run2vm.sh DIR_A 'CMD_A' [DIR_B 'CMD_B']`): each VM boots a
  qcow2 overlay of its image's `BASE.IMG` as `C:` and a fresh payload disk as `D:` holding the
  files of DIR (8.3 names). `IMAGE_A` and `IMAGE_B` choose each VM's image: `net330` (default)
  or `wga500` (the WGA VM image, whose `NET START` also starts the peer server). Each VM's
  `D:\RUN.BAT` installs its `SYSTEM.INI` (computer name `RIVMA` or `RIVMB`) and `PROTOCOL.INI`
  (its NetBEUI response timer `T1`), runs `NET START` (`D:\NETSTART.OUT`), `CHKDSK C:`
  (`D:\CHKDSK.OUT`) and CMD (`D:\OUT.TXT`), writes the errorlevel to `D:\DONE.TXT` and ends
  through `QEXIT`. A VM passes only when QEMU's status and `DONE.TXT` agree, so one that never
  booted cannot pass, and when `NET START` printed its success line (an empty `NETSTART` skips
  that check); otherwise `NET START`'s `Error` line is printed. A program that ends the VM
  itself (`/EXIT`) leaves no `DONE.TXT` and counts as incomplete. The gates that run on it keep
  a failed run as `$OUT.fail.<timestamp>`.
  - The VMs are linked by a TCP `stream` netdev on 127.0.0.1 (`NETMODE=stream`, default; VM-A
    listens, VM-B reconnects every 200 ms until A is up). `NETMODE=dgram` (a UDP pair) and
    `NETMODE=mcast` (`-netdev socket,mcast=…`, needing `sudo ip link set lo multicast on` where
    the loopback lacks multicast) remain available. Under heavy host load the emulated timers
    can starve NetBEUI name registration.
  - Runs share the images: each holds the lock `DIR.lock` of every image it boots shared, and
    `mkimg.sh` and `mkwga.sh` hold it exclusive, so a build waits for the runs and a run for the
    build. One run at a time per `OUT` (a second exits 2); runs take different ports, each
    holding a port slot of `PORT_LOCKS` unless `PORT_A` is given, and `OUT/PORTS` records the
    ports before the VMs start. The header of `run2vm.sh` gives the port bands and the other
    settings (`PCAP=1` for a packet capture, `MONDIR` for the QEMU monitors, `TIMEOUT`, names,
    timers, addresses, `QEMU_EXTRA`). Signals stop the VMs (exit 129, 130 or 143).
  - `NIC2_B=1` gives VM-B a second card, an `ne2k_isa` at 0x320/IRQ 11 on `-netdev user` (slirp;
    the host is 10.0.2.2), with `hostfwd` from `127.0.0.1:PORT_T` to the guest's TCP port 7000.
    The Crynwr `NE2000.COM` (GPL) is copied from `PKTDRV` (default `$RETRO_DL/pktd`) into the
    run payload only; `CMD_B` loads it after `NET START` with `NE2000 0x60 11 0x320`, and
    NetBEUI on the PCnet and WATTCP on the NE2000 then run side by side. slirp accepts every
    host connection to `PORT_T` at once and, with nothing listening in the guest, closes it
    without data, after up to about 75 s, so a readiness check never takes a connect alone as
    ready.
  - `SERIAL=1` joins the VMs' COM1 ports (3F8h, IRQ 4) through a unix socket in a private
    directory, connected as the network is. `COM2_A=path` then also gives VM-A a COM2 (2F8h, IRQ
    3) on a unix socket at `path`, where QEMU listens for a host program. QEMU's socket line
    carries no modem-status lines and paces nothing, whatever the baud rate. Without `SERIAL=1`
    neither VM has a serial port of its own (`-serial none`).
  - The two VMs run NetBEUI with different LLC response timers (`PROTOCOL.INI` `T1=`, `NB_T1_A`
    and `NB_T1_B` of `net330.sh`, whose comment explains the session deadlock that equal timers
    allow). `RELAY=1` routes VM-B's link through `TEST/INTEG/LOSSY.PY` on `PORT_B`, which loses
    the frames behind that deadlock and logs each in `OUT/RELAY.LOG`. `RELAY=delay` routes it
    through `LOSSY.PY --delay RELAY_MS` (whole milliseconds, default 10), which passes every
    frame both ways unchanged and in order, each no sooner than `RELAY_MS` after it was read.
    The relay creates `OUT/RELAY.LOG` once it has joined the VMs, so a relayed run without one
    fails ("the relay never joined VM B to VM A (no RELAY.LOG)").
  - **The stall.** Under QEMU a NetBEUI station, Client 3.0's and the Add-On's alike, sometimes
    ignores the answer to its own poll (an `RR` or `UA` with the final bit) that comes back
    within a millisecond, while it is still dealing with its own transmit, as QEMU completes a
    transmit at once and delivers frames on host time. The station polls again, T1 doubling, and
    after about half a minute sends `DISC`, ending every session on the link; it strikes most a
    VM that a loaded host starves, and the Add-On's keepalive of an idle session. A session
    request it delays leaves a half-open session: the listener's LISTEN ends with
    `NB_RC_ABORTED` (18h) and the caller's CALL times out (`NB_RC_TIMEOUT`, 05h), or the LISTEN
    completes for a CALL already given up (INVSERV holds that slot until `IDLE=`). So NBTEST's
    server listens again for the rest of its `/WAIT:`, INVTEST's `@CALL` calls again after a
    CALL that timed out, and INVSERV posts a new LISTEN after one that ended. An answer 10 ms
    late is always taken, so `RELAY=delay` removes the stall.
- **NetBIOS gate** (`TEST/INTEG/nbgate.sh [-x] [count]`): VM-B runs `NBTEST CLIENT RICLI RISRV
  /COUNT:count` (default 10) against VM-A's `NBTEST SERVER RISRV`, once in wait form and once
  with `/ASYNC` on both; both VMs must exit 0 with `NBTEST PASS`, and the no-wait LISTEN and
  CALL lines (a CALL completed with `rc=00` and `pending=1`) must show. A third mode has VM-A
  alone run `/WAIT:1`, which must exit 3 with `NBTEST FAIL`. Every mode runs even when an
  earlier one fails. `-x` runs the loss mode instead: the wait form with `RELAY=1`, which must
  pass with the resent bulk data dropped, and fails when the VMs' timers are equal. With SHARE,
  `AM2100.DOS`, NetBEUI and the basic redirector loaded, `CHKDSK` reports 509,024 of 654,336
  bytes free.
- **Server gate** (`TEST/INTEG/srvgate.sh [-u] [-m KB] [-c KB] [-s KB] [-t NAME] [EXEDIR]`):
  one `run2vm.sh` run with `SERIAL=1` of one build's `INVSERV.EXE`, `INV.EXE` and `INVTEST.EXE`
  (`EXEDIR`, default `BIN`, or `BIN/TC` or `BIN/WC65`); with `-t NAME`, `NAME.EXE`, the
  network-only client (`THIN` of `SRCS.MK`), runs wherever `INV.EXE` does below.
  - VM-A runs `INVSERV /CFG:SRV.CFG` twice on a fresh `D:\DATA`, serving six NetBIOS sessions,
    COM1 and the empty mailbox `D:\MBOX`.
  - VM-B runs `INVTEST` scripts over NetBIOS (two sessions with requests in flight) and COM1;
    `INV /CMD /COM1` and `INV /CMD /NET` on the same requests (`NETCMD.TXT`), the first with
    BREAK on, which it must leave on; an `INV /CMD /COM1` reading a Ctrl-C, `BYE` and Enter that
    `KSTUFF.COM` (`TEST/DBASE/KSTUFF.ASM`) queued, which the Ctrl-C must not end; an `INV /CMD
    /NET` that must exit 9 once the first server is shut down from its screen (F3, Y, typed
    through `qmon.py`); and `DOWN.SCR`, which ends the second server with `ADMIN SHUTDOWN`.
  - The VM clocks are fixed, so the transcripts must equal `TEST/GOLDEN/NET2.TXT`, `SER.TXT`,
    `NETCMD.TXT` (both transports) and `DOWN.TXT`, only the uptime and idle figures masked; the
    host also checks every exit status, the event log, that no new password is in any file the
    VMs left, and `CHKDBF.PY --strict` on the data. `-u` rewrites the goldens.
  - `-m KB` starts INVSERV with KB kilobytes free, `-c KB` VM-B's `INVTEST` runs and `INV /CMD
    /NET`, `-s KB` `INV /CMD /COM1`. Each such program is started by `EATRUN.COM`
    (`TEST/INTEG/EATRUN.ASM`, built by nasm into `BIN/`; `EATRUN kb file [arguments]`), which
    shrinks its own block to leave KB kilobytes free above it and runs the program; `CHKDSK`,
    started the same way, must report that figure. `make integ` passes the build's
    `INVSERV_FREEKB_<build>`, `INVNET_FREEKB_<build>` and `INVCOM_FREEKB_<build>`, then runs
    the gate again with `-t` and `THINNET_FREEKB_<build>` and `THINCOM_FREEKB_<build>`.
- **Screen gate** (`TEST/INTEG/scrgate.sh -r NAME,LINK[,KB]... [EXEDIR]`): the full-screen
  clients' deepest screens over a serial line or NetBIOS, from one build's `INVSERV.EXE`,
  `INVTEST.EXE` and the clients of its runs. Each `-r`, one at least, adds a run of every
  script by `NAME.EXE`, `INV` or the network-only client, over `LINK`, `COM1` or `NET`.
  - The host `INV` seeds two data directories at `SEED_NOW`, as `TEST/T_SCR.SH` seeds its runs:
    from `SEED.REQ` alone, and from `SEED.REQ` then `DEEP.REQ`.
  - One `run2vm.sh` run with `SERIAL=1`, both VM clocks starting at that same time. VM-A runs
    `INVSERV` on each directory in turn, each time under a NetBIOS name of its own. For each,
    VM-B waits until `INVTEST` reaches it over NetBIOS, then for each run, in its own directory
    `Rn`, runs the key scripts that set `INV_FREEKB_<build>` (Runtime requirements): `RPTHELP`
    on the first, every `#@ DEEP` script on the second, as
    `NAME /COM1 /CFG:Rn\x.CFG /KEYS:x.KEY /SNAP:Rn\x.SNP` (`/NET server` over NET), `x.CFG` a
    fresh copy of `TEST/KEYS/COLOR.CFG`; then `INVTEST` ends that server with `ADMIN SHUTDOWN`
    over COM1.
  - Each snapshot file must equal the script's golden of the local runs in `TEST/GOLDEN/SCR/`
    with its status line as the link must show it: `COM` or `NET` in the connection field
    where the golden has `LOCAL`, and over NET, as the station, `RINV` and four hexadecimal
    digits, the NetBIOS name made up for each call; and leave at least `STACK_MARGIN` stack
    bytes unused. Every exit status and both servers' event logs are checked as in the server
    gate.
  - A run's `KB` starts each of its client runs with KB kilobytes free through `EATRUN.COM`,
    which `CHKDSK`, started the same way, must report. `make integ` runs the gate with four
    runs: the network-only client over COM1 at `THINSCR_FREEKB_<build>` and over NET at
    `THINSCRNET_FREEKB_<build>`, and `INV` over COM1 at `INVSCR_FREEKB_<build>` and over NET at
    `INVSCRNET_FREEKB_<build>`.
- **Starvation gate** (`TEST/INTEG/stvgate.sh -r ROUNDS [EXEDIR]`): proves the serial line is
  served promptly under NetBIOS load and does not hold the load up. One `run2vm.sh` run with
  `SERIAL=1`, VM-A's COM2 on a host socket (`COM2_A`) and `NETMODE=dgram`, so that only the
  guests' speed paces the load: VM-A runs `INVSERV /CFG:STV.CFG` on data the host `INV` seeds;
  `TEST/INTEG/SERPING.PY` logs in on COM2 and sends a `PING` every half second; VM-B runs
  `LOAD_SESSIONS` NetBIOS sessions through `LOAD_ROUNDS` rounds (`-r`, from
  `STVGATE_ROUNDS_<build>` of `MAKEFILE`) of paged lists of over 2 KB, sized so that the build's
  fastest run lasts half as long again as `LOAD_SECS`. Every program must exit 0, every `PING`
  be answered `+OK` within `PING_LIMIT` seconds, times `DOSTIME_SCALE`, and the event log show a
  load window, from the login of the last load session to the last NetBIOS request, of at least
  `LOAD_SECS` seconds with no gap over `LOAD_GAP`, in which the serial session made at least
  `PING_COUNT` requests. The settings are at the top of the script.
- **Mailbox on MS-DOS 3.10** (`TEST/INTEG/mbox310.sh [-u] INVSERV.EXE INVSERV.MSG CONFIG.SYS`,
  run by `dostest`, `tc310` and `wc65310`): the server on the Compaq MS-DOS 3.10 floppy, with
  `TEST/INTEG/SRV310.CFG` (`SESSIONS=0`, a mailbox, an event log) and no NetBIOS provider, must
  answer the request `TEST/INTEG/ADMIN.REQ` as `TEST/GOLDEN/SRV310.RSP`, zero and remove the
  request, and exit 0 at its `ADMIN SHUTDOWN`; the new password must be nowhere on the disk and
  the last `STACK` event must show `SRV_STACK_MARGIN` bytes never used. The request exercises
  find first and next, rename and delete, the walks of the location tree (`LOC LIST` and `LOC
  CONTENTS 0 ALL=1`), and export, reindex and pack, on the floor DOS, and its `CAT LIST` takes
  the stack deepest. A second request, `ABC.REQ` (served first, as it sorts first), logs in with
  `TICKET=NEW` while `MUSTCHG` is pending and ends with `BYE TICKET=END`: its response must
  equal `TEST/GOLDEN/SRV310T.RSP` (the ticket shown as `<t>`), the request must be gone and the
  ticket not in `SRV.LOG`. A fixed request cannot name a random ticket, so reuse, expiry and
  `PASSWD` reissue are left to `smbgate.sh`. `-u` rewrites both goldens.
- **TCP/IP VM image** (`TEST/INTEG/mktcpc.sh`): builds `$TCP330_DIR/BASE.IMG` and `PAYLOAD.IMG`
  (default `TEST/INTEG/vm/tcp330/`, settings in `TEST/INTEG/tcp330.sh`) from the NetBIOS VM
  image, which it builds first when needed, without running a VM: MS Network Client 3.0's TCP/IP
  stack (`TCPDRV.DOS`, `NEMM.DOS`, `TCPTSR.EXE`, `TINYRFC.EXE`) replaces NetBEUI, with the
  templates of `TEST/INTEG/SMB/`. `C:\NET\NETUP.BAT` starts the client and logs on with an empty
  password. The VM reaches the host's share as `\\RIHOST\MBOX` (`LMHOSTS`) at 10.0.2.100 on
  QEMU's user network, which forwards port 139 through `nc -q 0` to the share's `smbd`. `CHKDSK`
  reports 462,912 bytes free with the client and `E:` connected.
- **WGA VM image** (`TEST/INTEG/mkwga.sh [DOS500_DIR [WGA_DIR]]`): builds `$WGA500_DIR/BASE.IMG`
  and `PAYLOAD.IMG` (default `TEST/INTEG/vm/wga500/`, settings in `TEST/INTEG/wga500.sh`), a
  NetBEUI peer file server, from the MS-DOS 5.00 disks, the Workgroup Add-On disk and the
  NetBIOS VM image, which it builds first when needed and which lends it its blank payload disk;
  the steps both builds share are in `TEST/INTEG/imglib.sh`.
  - The files are copied off the disks with mtools and expanded with `msexpand`, each one's name
    taken from the disk's `SETUP.INI`, as DOS 5.00's SZDD header keeps none. The Add-On goes to
    `C:\NET` with the templates of the NetBIOS VMs (the same `AM2100.DOS` on the PCnet card) and
    `TEST/INTEG/WGA/SYSTEM.INI` (file sharing on, the basic redirector, computer name
    `WGA_COMPUTER`); its `SETUP.EXE`, driven by `qmon.py`, creates `WFWSYS.CFG` and
    `SHARES.PWL`. The shells are patched and proved as the NetBIOS image's.
  - On a plain boot `NET START` starts the redirector, NetBEUI and the server with no logon
    prompt; a share is then `NET SHARE MBOX=C:\MBOX /FULL /SAVESHARE:NO /YES`. Run from a
    payload's `D:\RUN.BAT` after `NET START`, `MEM` reports 456,784 bytes for the largest
    program, the same after a `NET SHARE`, so `INVSERV` starts beside the server without
    `DOS=HIGH`.
  - A NetBIOS VM reaches the share with the basic redirector after `NET LOGON name /YES
    /SAVEPW:NO` (empty password). Reading, writing, renaming and deleting work, and a range the
    server locks blocks the client's read and write, but every record lock (INT 21h 5Ch) the
    basic redirector asks for fails with error 21h (lock violation); Client 3.0's full
    redirector, which an 8088 cannot run, has locks granted and conflicting ones refused.
  - Two builds differ only in a few bytes `SETUP.EXE` writes differently each run, the disk
    identifier, the volume serial number and the time stamps.
- **Samba share** (`TEST/INTEG/smbd.sh start RUNDIR SHAREDIR`, `stop RUNDIR`): starts `smbd` as
  the calling user, without root, on 127.0.0.1 at the first port of a port slot, with a private
  configuration from `TEST/INTEG/SMB/SMB.CNF` (the LANMAN2 dialect that Client 3.0's `DOS
  LM1.2X002` gets, a guest share without Unix extensions or symbolic links) and its state under
  RUNDIR; it makes SHAREDIR's `REQUEST` and `RESPONSE`. `TEST/INTEG/smbprobe.sh` is the smoke
  test of the image and the share: files exchanged both ways by rename, the dialect, 8.3 names.
- **Samba mailbox gate** (`TEST/INTEG/smbgate.sh [-u] [EXEDIR]`): the TCP/IP VM, with `E:`
  connected to a run's `smbd.sh` share, runs `INVSERV /CFG:SMB.CFG` (`SESSIONS=0`, the mailbox
  `E:\`, data and event log on `D:`); `TEST/INTEG/MBOXCLI.PY` posts request files, each
  written as `.TMP` and renamed `.REQ`, and reads each `.RSP` (zeroing it before it removes it):
  `HOST1` logs in, changes the password, adds and lists an item; the login ticket flow follows
  (`IDLE=1` in `SMB.CFG`), with stations `TKA` and `TKB`: a ticket is issued and redeemed,
  `PASSWD` revokes it and issues a new one, and 401 refuses the old one, the new one shown
  under another station (which also drops it for its holder), a ticket ended by `BYE
  TICKET=END` and one idle for `IDLE=` minutes (a 401 closes its session at once; MBOXCLI.PY
  prints tickets as `TICKET=<t>`); `HOST2` ends the server
  with `ADMIN SHUTDOWN`. The idle expiry takes `IDLE=` minutes (a wait of 70 s times
  `DOSTIME_SCALE`) and its code is portable C, so only the Open Watcom build runs it. The
  responses must equal `TEST/GOLDEN/SMBMBOX.TXT` (the expiry's, `TEST/GOLDEN/SMBEXP.TXT`), INVSERV
  must exit 0, the share must be empty at the end, the event log well formed, the new passwords
  and every ticket MBOXCLI.PY saw in no file of the share or of `D:`, and `CHKDBF.PY --strict`
  content with the data. `-u` rewrites the goldens.
- **Workgroup Add-On gate** (`TEST/INTEG/wgagate.sh [-u] [EXEDIR]`, `wgagate.sh -n`): proves
  that a station with only NetBEUI and Client 3.0's basic redirector, the set-up an 8088 can
  run, works the mailbox and the NetBIOS sessions of an `INVSERV` running on a Workgroup Add-On
  file server. One `run2vm.sh` run with `IMAGE_A=wga500`, `SERIAL=1` and `RELAY=delay` (the
  gate's default, which `RELAY` and `RELAY_MS` override; without the delay the stall can end the
  sessions):
  - VM-A (MS-DOS 5.00, SHARE, the Add-On's server) shares `D:\DATA` and `D:\MBOX` with `NET
    SHARE` and runs `INVSERV /CFG:SRV.CFG` on fresh data (NetBIOS sessions, COM1 and the
    mailbox).
  - VM-B (MS-DOS 3.30) logs on, connects `E:` to the mailbox and `F:` to the data once a marker
    file of VM-A's shows both shares up, then runs `TEST/INTEG/MBOX.SCR` (two mailbox stations,
    `RIREAD` capped at level 1 by `READONLY=` through its file name alone, and a session on
    COM1), `TEST/INTEG/WGANB.SCR` (NetBIOS sessions, one idle for longer than the Add-On's
    keepalive), `INV /CMD /LOCAL F:\`, which must exit 2 as the server holds the data files, and
    `MBOXEND.SCR`, which ends the server; then `INV /CMD /LOCAL F:\` must open the data and exit
    with 0. Marker files keep VM-A serving until VM-B has finished.
  - The gate checks the DOS versions, the `NET` commands, every exit status, the transcripts
    against `TEST/GOLDEN/MBOX.TXT` and `WGANB.TXT`, the mailbox left holding only the markers,
    the event log and its NetBIOS sessions (`wganb_check` of `TEST/INTEG/wganb.sh`), that no new
    password is on either VM's `D:` but in the scripts (`pw_check` of `TEST/INTEG/vmgate.sh`),
    and `CHKDBF.PY --strict` on the data. `-u` rewrites both goldens.
  - `-n` runs only the checks of the gate's helpers, listed in its header: `pw_check` on planted
    scratch trees, `TEST/INTEG/T_RELAY.PY`, and `run2vm.sh`'s refusals and failures (an unknown
    image, a failed `NET START`, a relay that never joined the VMs).
- **Image shell check** (`TEST/INTEG/shlgate.sh SHELLCHK`, first in `make integ` and `make
  wgagate`): proves the shell patch of the two-VM images. Both shells of the NetBIOS image must
  differ from the MS-DOS 3.30 disk's `COMMAND.COM` in exactly the two bytes of its jump, both of
  the WGA image from the MS-DOS 5.00 disk's likewise, and the TCP/IP image's must equal the
  NetBIOS image's. Under `run2vm.sh` `SHELLCHK` must end with errorlevel 0 on both images and
  time out on a copy of either with the disks' own shells put back. `img_shell` itself runs on
  scratch copies of the NetBIOS image's disks, with an unknown shell and with a proof that fails
  (the cases are in the header). No period disk and no built image may change.
- **TCP probe** (`TEST/INTEG/tcpprobe.sh [-v 310|330] [EXEDIR]`): boots the MS-DOS 3.30 or
  Compaq 3.10 floppy in QEMU with an `ne2k_isa` at 0x320/IRQ 11 on `-netdev user`, loads
  `NE2000.COM` from a 360 KB `B:` (MS-DOS 3.10 under QEMU hangs with a larger one) and runs
  `TCPTEST`. `TEST/INTEG/TCPCLI.PY` drives `TS_SLOTS` echo sessions through the run's port slot,
  one more connection, which gets no bytes, a burst of more frames than the probe has receive
  buffers, a reused slot and `QUIT`. No copy of the burst's marker may be left in the guest's
  memory, nor of the bytes past those `TCPTEST` reads with `QUIT` or those a session reset
  during the closes left unread; `TCPTEST` must end with `TCPTEST PASS` and exit 0. WATTCP's
  `PING` then proves the packet driver released, and its `FINGER` that a guest reaches the host.
  `NE2000.COM` drops an oversize frame itself, so CPKT's check of one is not reached. Exit 0
  pass, 1 test failure, 2 setup error.
- **TCP gate** (`TEST/INTEG/tcpgate.sh [-u] [-m KB] [EXEDIR]`): VM-A runs `INVSERV`, VM-B
  `INVTCP RISRV` on its second card (`NIC2_B`), and `TCPCLI.PY` plays `TEST/INTEG/TCP.SCR`
  through `PORT_T`: `TS_SLOTS` sessions at once, one more refused, a freed slot reused, `ADMIN
  SHUTDOWN`, then connections that find no server or send bytes the bridge never reads. The
  transcript and INVTCP's event lines (on VM-B's COM2) must match `TEST/GOLDEN/TCP.TXT` and
  `TCPEV.TXT`, both programs exit 0 and `PING` show the driver released. The new password must
  be in no file the VMs left, and dumps of VM-B's memory from INVTCP's PSP up must hold neither
  it once its requests are answered nor the unread bytes once their connection has ended. `-m
  KB` starts INVTCP with KB kilobytes free, through `EATRUN.COM`; `make tcp` passes
  `INVTCP_FREEKB_<build>`.
- **Winsock gate** (`TEST/INTEG/wintcp.sh [-u] [EXEDIR]`, `make wintcp`): VM-A runs `INVSERV`,
  VM-B `INVTCP RISRV` on an NE2000 placed as `NIC2_B` places it but on a private Ethernet
  segment, a QEMU `stream` netdev on a port of the gate's own port slot, which the Windows for
  Workgroups machine (`runwin.sh -w wfw311 -n`) joins once INVTCP has said `START`. There
  `WSOCKT` connects to VM-B's address (`TEST/INTEG/WATTCP.CFG`) and `BR_PORT`, sends `HELLO`,
  `VER` and `BYE` and reads the answers; then `INVWIN /TCP:` that address, beside `INV.MSG`,
  plays `TEST/WIN/INVLINK.KEY` on the data the host INV seeds from `TEST/KEYS/SEED.REQ`, and
  `TEST/WIN/INVENDQ.KEY`, which ends Windows' session with the link open; then INVWIN plays
  `TEST/WIN/INVFAIL.KEY` with the port after `BR_PORT`, which is refused, and with an address
  no station has, its wait ended by Esc, which the host presses through the Windows machine's
  QEMU monitor once VM-B's capture of the segment (`filter-dump`) holds the ARP request for it
  (`TEST/INTEG/ARPWAIT.PY`); last `WSKT` runs. WSOCKT's report, the server's version masked,
  and INVTCP's event lines must match `TEST/GOLDEN/WINTCP.TXT`, INVWIN's snapshots
  `TEST/GOLDEN/WIN/INVTCP.SNP`, `INVTCPQ.SNP`, `INVTCPR.SNP` and `INVTCPC.SNP`, each connection
  close within `CLOSE_SECS` of its `OPEN`, WSKT's report pass, both DOS programs exit 0 and the
  server's event log pass `log_check`; `-u` rewrites the goldens. `srvgate.sh`, `scrgate.sh`,
  `tcpgate.sh`, `stvgate.sh`, `wgagate.sh` and `wintcp.sh` share `TEST/INTEG/vmgate.sh`;
  `smbgate.sh` uses its `log_check` and `pw_check`.
- **Windows test machines' images** (`TEST/WIN/mkwin.sh [-w win30|win30nb|wfw311] [-x
  EXTDIR]...`):
  builds the one disk of a test machine (`TEST/WIN/winlib.sh`), 16 MB, without an emulator:
  - `win30` (the default), `$WINIMG_DIR/BASE.IMG` (default `OBJ/WIN30/`): MS-DOS 3.10 from the
    floppy of `DOS310_FLOPPY` (`TEST/shlib.sh`) and Windows 3.0 from the disks of `WIN30_DIR`
    (default the `win30` package), the files SETUP copies for a 386 with a VGA, an enhanced
    keyboard, no mouse, no network and US settings, with `SYSTEM.INI`, `WIN.INI`,
    `CONFIG.SYS` and `AUTOEXEC.BAT` from the templates of `TEST/WIN/`; `SHARE` is loaded
    before Windows. Its one printer, the default, is the Generic / Text Only driver
    (`TTY.DRV`) on the file port `C:\RUN\PRINTER.OUT`, without the spooler, so that a run's
    printing ends before Windows does and comes back with the run's files.
  - `win30nb`, `$WINNBIMG_DIR/BASE.IMG` (default `OBJ/WIN30NB/`): `win30`'s Windows on MS-DOS
    3.30 (`DOS330_FLOPPY`), as MS Network Client 3.0 refuses 3.10, with the client's NetBEUI on
    a PCnet card, taken from the NetBIOS VM image (which must be current): its files of
    `C:\NET`, and `TEST/INTEG/SYSTEM.INI` and `PROTOCOL.INI` with the computer name
    `WIN_NBNAME` of `TEST/WIN/winlib.sh` and VM B's response timer. A run that puts `NET.ON`
    beside its program starts the network before Windows: `PCNETBM.COM` (`TEST/WIN/PCNETBM.ASM`)
    turns on the card's bus mastering, as the NetBIOS VMs' `PCNET.SCR` does, then `NET START`
    runs, its output in `C:\RUN\NETSTART.OUT`, then the run's `C:\RUN\NETUSE.BAT`, when it has
    one, which may log on and connect a drive, and `SETMACH.COM` (`TEST/WIN/SETMACH.ASM`) makes
    the computer name the DOS machine name, which the client leaves unset (function 5E00h finds
    none after `NET START` or `NET LOGON`, under DOS and Windows alike).
  - `wfw311`, `$WFWIMG_DIR/BASE.IMG` (default `OBJ/WFW311/`): MS-DOS 3.30, the oldest that
    Windows for Workgroups 3.11 starts on (`DOS330_FLOPPY`), and Windows for Workgroups 3.11
    from the disks of `WFW311_DIR` (the `wfw311` package), SETUP's files for the same machine
    without the optional accessories, games, screen savers, wallpapers, readme files and
    TrueType fonts, then the NDIS 3 driver of an NE2000 (`NE2000.386`) and Microsoft TCP/IP-32
    3.11b and its `WINSOCK.DLL`, read from `TCP32B.EXE` in `TCP32_DIR` (the `tcp32b` package);
    the templates are those of `TEST/WIN/WFW/`, `PROTOCOL.INI` among them. It runs in 386
    enhanced mode only, on protected-mode network drivers alone, with a fixed address,
    10.0.2.16, and no DHCP. The Microsoft Windows Network is left out: its driver needs the
    security settings `WFWSYS.CFG`, which only SETUP's half in Windows makes. The disks hold
    their files in Microsoft's KWAJ compression, which `TEST/WIN/KWAJ.PY` expands;
    MS-DOS 3.30's boot sector (of `win30nb` too) takes its BIOS drive from offset 1FDh, which
    the build sets to the first hard disk.

  386 enhanced mode runs without a swap file. The same inputs give the same bytes; the image is
  rebuilt only when the hash of its inputs (`INPUTS.SHA`) changes, and a copy must boot under
  each emulator of the machine before it replaces the old one. That check boots DOS alone
  (`runwin.sh -m dos`), not Windows, so it serves DOSBox-X's 8086 too, on which no goal runs the
  programs while they need protected mode. `-x EXTDIR` adds files to `C:` and lines to
  `CONFIG.SYS` and `AUTOEXEC.BAT` (`CONFIG.ADD`, `AUTOEXEC.ADD`), for drivers such as a network
  client. The disk's master boot code is `TEST/WIN/MBR.ASM`, 8086 instructions only.
- **Windows runner** (`TEST/WIN/runwin.sh [-w win30|win30nb|wfw311] [-e qemu|dosbox] [-m
  r|s|3|dos] [-t SECS] [-i IMAGE] [-I INTL] [-n NETDEV] [-d DUMP] [-s SOCKET] [-p MONITOR] [-k]
  RUNDIR 'COMMAND'...`): boots a scratch copy of the machine's image, copies RUNDIR to `C:\RUN`,
  starts `WIN /R`, `/S` or `/3` with COMMAND from there (or, with `-m dos`, runs COMMAND under
  DOS), waits for Windows to end, through the program's `ExitWindows`, does so for each COMMAND in
  turn, and copies `C:\RUN` back. QEMU is a 486 with 4 MB and a VGA, its clock starting at
  `CLOCK`, the time `wintest.sh` seeds its data at, so that a date a program shows of its own (a
  user's last login) is the same every day, for all three modes of `win30`, the standard and 386
  enhanced modes of `win30nb` and the 386 enhanced mode of `wfw311`; DOSBox-X, for `win30`
  alone, an 8086 with 640 KB and a CGA, for real mode, since the VGA and EGA drivers of Windows
  3.0 need a 186. `SYSTEM.INI` excludes E000h-EFFFh from 386 enhanced mode: under QEMU's PC
  machine WIN386 quits at start-up for some DOS memory layouts without it. `wfw311` has an
  NE2000 at 300h, IRQ 5, `win30nb` a PCnet in PCI slot 5, which `-n` joins to a network, a QEMU
  `-netdev` value: `user`, on which the host is 10.0.2.2, or a `stream` netdev shared with
  another machine. With `-s` QEMU's machine has a COM1 whose line is a unix socket; without it
  the machine has no serial port. `-p` puts QEMU's monitor at a path of the caller's, for
  another program to drive it. The machines' `WIN.INI` sets ISO short dates, a `.` decimal
  separator, no thousands separator and a leading zero, so INVWIN shows dates and numbers as the
  wire gives them; `-I` replaces that `[intl]` section in the copy with a file's lines, its own
  `[intl]` line first. WIN leaves errorlevel 0 whatever `ExitWindows` is given, so the
  program's files carry its result. Exit status: the errorlevel of the last COMMAND (0..199),
  200 when Windows has not ended at the limit (QEMU is then stopped and saves the screen as
  `RUNDIR.PNG`, or `-d DUMP`, and its monitor's `info registers` and `info pic` beside it,
  ending `.MON` for `.PNG`), 201 when the emulator stopped before the run did (QEMU's log of the
  CPU's resets, a triple fault, a shutdown asked for and the run states then goes beside the
  dump, ending `.RST`, as `-no-reboot` turns a guest's reset into QEMU's silent exit), 202 when
  the machine never reached the run, 2 on a setup error; `-t` is scaled by `DOSTIME_SCALE`. Runs
  share nothing they write, so they go side by side.
- **Windows tests** (`TEST/WIN/wintest.sh [-e EMU] [-m MODE] [-M WORD] [-t SECS] [-s SOCKET]
  [-p MONITOR] [-I INTL] [-f FILE]... [-d DIR [-q REQUESTS -i HOSTINV]] [-k KEYS] [-g GOLDEN]
  [-c FILE]... [-o PRINTED] RUNDIR EXE [ARG...]`): one run of `make wintest`. It makes RUNDIR
  afresh with the program, the `-f` files and the empty directory of `-d`, which the host INV of
  `-i` first fills from `-q`'s requests as `TEST/T_SCR.SH` seeds its runs, runs the program there
  through `runwin.sh` (with `-s`, `-p` and `-I`, its COM1 and monitor on those sockets and its
  International settings) and judges the file it leaves. With `-k` that file is the test driver's
  snapshot of the key script, which must equal `-g` byte for byte, each `-c` golden is a file the
  program writes beside it, under the golden's name, which must equal it byte for byte too;
  otherwise it is the report `NAME.OUT`, whose `MODE` line must name the mode of `-M` (`REAL`,
  `STANDARD` or `ENHANCED`, given apart from `-m`, so that a run started in another mode fails)
  and whose `EXIT` line, if any, must say 0. With `-o`, in either case, what the machine's printer
  wrote, `PRINTER.OUT`, must equal `-o`. It prints the result, in which `wintest` looks for a line matching
  `<NAME>_WINPASS`. Exit 0 pass, 1 fail, `runwin.sh`'s 200 to 202 when Windows did not end, 2 on a
  setup error; RUNDIR stays only when the run did not pass. The settings of a Windows program in
  `SRCS.MK`:  | Setting | Effect | |---|---| | `<NAME>_WINARGS` | its arguments | |
  `<NAME>_WINDATA` | files copied beside it | | `<NAME>_WINDIR` | an empty directory made beside
  it | | `<NAME>_WINSEED` | the requests that fill that directory first | | `<NAME>_WINPASS` | a
  grep pattern a line of its report must match, the same in every mode | | `<NAME>_WINKEYS` | its
  key scripts, one run each: `-` for `TEST/<NAME>.KEY`, `x` for `TEST/<NAME>x.KEY`, each snapshot
  compared with `TEST/GOLDEN/WIN/<NAME>[x].SNP` | | `<NAME>_WINARGSx` | the arguments of the run
  of key script `x`, in place of `<NAME>_WINARGS` | | `<NAME>_WINSEEDx` | the request files, in
  turn, that fill the directory of the run of key script `x`, in place of `<NAME>_WINSEED` | |
  `<NAME>_WINCMPx` | the goldens of the files the run of key script `x` writes (`-c`) | |
  `<NAME>_WINPRNx` | the golden of what the run of key script `x` prints (`-o`) | |
  `<NAME>_WINTIME` | the limit of each of its runs, in seconds, in place of `WIN_TIME` | -
  **Windows serial link** (`TEST/WIN/winser.sh [-u] [-m MODES] [-t SECS] WSERT.EXE INVWIN.EXE
  INV.MSG INVSERV.EXE INVSERV.MSG CONFIG.SYS HOSTINV`): `make winser`, one run of each program
  per Windows mode, all side by side. The host INV seeds a data directory from
  `TEST/KEYS/SEED.REQ`; `TEST/WIN/WSRELAY.PY` makes a unix socket for each machine's serial
  line; `boot310.sh -S` boots MS-DOS 3.10 with `INVSERV`, `TEST/WIN/WINSER.CFG` and the data on
  its hard disk, the server on COM2; and `wintest.sh` runs `WSERT 1`, or `INVWIN /COM1` with
  `INV.MSG` and the key script `TEST/WIN/INVLINK.KEY`, its settings `TEST/WIN/PANE1.CFG`, which
  show the contents beside the location tree on this slow link too, or, as the run `INVUSER`,
  `TEST/WIN/INVUSER.KEY`, or, as the run `INVBACK`, `TEST/WIN/INVBACK.KEY` on data that
  `TEST/KEYS/RLLONG.REQ` has given sixty items more, with COM1 on the other socket and QEMU's monitor on a third (`-s`,
  `-p`). The relay joins the two lines once the server answers a probing `HELLO` and has ended
  that session, passing on no more than a 9600-baud line carries (`LINE_RATE` bytes a second
  each way), as QEMU's ports would otherwise deliver a whole response at once; at each `PING`
  WSERT sends, at each `ITEM GET` INVWIN sends after its login and at each `USER LIST` of
  `INVUSER`, it saves the machine's memory through the monitor, the machine stopped meanwhile
  (`stop`, `pmemsave`, `cont`), which must not hold the ADMIN password, nor for `INVUSER` the
  passwords its script sets, a new user's and a reset one (`USERPWS` of `winser.sh`), as
  `tcpgate.sh` searches INVTCP's (the key scripts type every password key by key, so that they
  hold it nowhere); it requires that Windows send no NUL, those lines (two `PING`s, four `ITEM
  GET`s, for `INVBACK` `BGETS`, `USERLISTS` `USER LIST`s) and a last `BYE`, and then ends the server with `ADMIN
  SHUTDOWN`. A run passes when `wintest.sh`, the relay and the server end with 0; for INVWIN
  `wintest.sh` compares the snapshot with `TEST/GOLDEN/WIN/INVSER.SNP`, for INVUSER with
  `INVUSR.SNP`, for INVBACK with `INVSERB.SNP`, and for WSERT the session lines of `WSERT.OUT` must equal
  `TEST/GOLDEN/WIN/WSERT.TXT`, which `-u` rewrites. A failed run's files stay in
  `OBJ/WINSER/<program>.<mode>`. A signal ends every program and machine of the runs before the
  script. Exit 0 pass, 1 fail, 2 on a setup error.
- **Windows NetBIOS gate** (`TEST/WIN/winnb.sh [-m MODES] WNET INVWIN INV.MSG HOSTINV`, `make
  winnb`): for standard and 386 enhanced mode, VM A of `run2vm.sh`, alone, runs `INVSERV
  /CFG:WINNB.CFG` (the name `RISRV`) on data the host INV seeds from `TEST/KEYS/SEED.REQ`. Once
  its ports are known, `LOSSY.PY --delay RELAY_MS` (default 10) relays the frames between VM A's
  port and the `win30nb` machine, which `runwin.sh -w win30nb -n` starts with `NET.ON` and the
  programs beside it: the delay keeps off the stall of the Two-VM harness, which otherwise
  leaves now and then a CALL of the Windows machine half open on a loaded host, in either mode.
  Windows starts four times in turn: WNET calls until the server answers (`WAIT_SECS` of
  `winnb.sh`) and plays a `HELLO`; INVWIN runs `/NET NOSUCH`, a name no station has, with the
  key script `TEST/WIN/INVCALL.KEY`, and `/NET RISRV` with `TEST/WIN/INVLINK.KEY`; and WNET
  plays `TEST/WIN/WNET.SCR` (HELLO, VER, a login, the item list, a page of over `NB_MSGMAX`
  bytes, `ADMIN SHUTDOWN`) and cancels a no-wait LISTEN. A mode passes when the Windows run
  ends, INVWIN's snapshots equal `TEST/GOLDEN/WIN/INVCALL.SNP` and `INVNB.SNP`, the last WNET's
  report says `WNET PASS`, the mode and `EXIT 0`, `INVSERV` ends with 0, and its event log
  `WINNB.LOG` shows at least three sessions (WNET's two and INVWIN's), each opened from the
  machine name `WIN_NBNAME`; a failed Windows run stops VM A at once. Each mode's verdict and
  seconds are printed; the runs live in `OBJ/WINNB/`, kept on failure. Exit 0 pass, 1 fail, 2
  on a setup error.
- **Windows mailbox gate** (`TEST/WIN/winmbx.sh [-m MODES] INVWIN INV.MSG HOSTINV`, `make
  winmbx`): for standard and 386 enhanced mode, VM A of `run2vm.sh`, alone on the Workgroup
  Add-On image, shares `D:\MBOX` as `MBOX` and runs `INVSERV /CFG:WINMBX.CFG` (no NetBIOS
  session, the mailbox `MBOX`, and COM1, which `QEMU_EXTRA` puts on a socket of the run) on data
  the host INV seeds from `TEST/KEYS/SEED.REQ`. `TEST/WIN/MBXRELAY.PY` relays the frames between
  VM A and the `win30nb` machine `RELAY_MS` late (default 10), as the NetBIOS gate's relay does,
  and watches them; `runwin.sh -w win30nb -n` starts that machine with `NET.ON` and a
  `NETUSE.BAT` that logs on and connects `E:` to the share, and Windows runs `INVWIN /MBOX:E:\`
  with `TEST/WIN/INVLINK.KEY`, its station the machine name `WIN_NBNAME` that `SETMACH` gives
  DOS. The relay requires the ADMIN password to cross only in the LOGIN that asks for a login
  ticket, before the `ITEM GET` of the item view; at that `ITEM GET` it saves the Windows
  machine's memory through its monitor, which must not hold the password, the network client's
  buffers included; and it notes every ticket that crossed. `SERPING.PY` then ends the server
  with `ADMIN SHUTDOWN` on COM1. A mode passes when the Windows run, the relay, `SERPING.PY` and
  `INVSERV` end with 0, the snapshot equals `TEST/GOLDEN/WIN/INVMBX.SNP`, the logon, the
  connection and the share report success, the server's log shows mailbox sessions, each opened
  from `WIN_NBNAME`, the mailbox holds nothing but the file `UP.TXT` the server's machine put
  there to say it is ready, and neither the password nor any ticket that crossed is on VM A's
  disk. Each mode's verdict and seconds are printed; the runs live in `OBJ/WINMBX/`, kept on
  failure. Exit 0 pass, 1 fail, 2 on a setup error.

## Release

`make dist` writes the release into `DIST/` (git-ignored): the floppy images that
`REL/DISKS.TXT` lists, `RETROINV.ZIP` with the files of the 1.44 MB disk, and `SHA256.TXT`,
the checksums of all of them. `REL/mkdist.sh` makes them:

- **Programs.** `INSTALL`, `INV`, `INVSERV`, `INVTCP`, `DBPACK`, `INVDUMP` and `COMTEST` of the
  Watcom C 6.5 build (`BIN/WC65/`), the one build on every disk, with `INV.MSG`, `INV.HLP` and
  `INVSERV.MSG`, and the network-only client (`THIN` of `SRCS.MK`). `INVWIN` (`BIN/WIN/`), with
  `INVWIN.HLP`, `INVWIN.HTX`, `INV.MSG` and `SETUP`, needs a 286 or better and is on no 360 KB
  disk. `INSTALL` and its list `INSTALL.LST` are on every disk. Why Watcom C 6.5's build is the
  one shipped is in `DOC/DESIGN.md` (Design decisions, Release disks). `INSTALL`, `DBPACK`,
  `INVDUMP` and `COMTEST`, the small-model programs, are linked with the reconstructed
  `CLIBS.LIB` (Watcom C 6.5, above). The test programs are not released. `DOC/INSTALL.md`
  describes the installation.
- **INSTALL's list.** Each line of `REL/DISKS.TXT` that places a file is `DISKS ROLES PATH
  SOURCE`: ROLES are the letters of the installations that copy it, given in
  `SRC/TOOLS/INSTPLAN.H` (S one PC alone, V the server, N a network client, T the network-only
  client, W the Windows client beside one of those, D the sample data, K a settings file an
  earlier installation keeps unless the user says to replace it), and Z marks a file the disks
  hold compressed (below). `mkdist.sh` stages every disk first, then writes each set's
  `INSTALL.LST` onto each of its disks, the source `LIST`: the release's name (`PROTO_VERSION`),
  the set's disk size and labels, the number of the disk it is on, then a line `ROLES DISKS SIZE
  PATH` per file of the set, DISKS the numbers of the disks that hold it and SIZE its bytes once
  installed (the sample data's summed, the list's its own), CR LF. Its first two lines tell sets
  and releases apart; `INS_TITLEMAX`, `INS_MAXDISKS`, `INS_MAXFILES` and `INS_LISTMAX` of
  `INSTPLAN.H` bound it. So the disks and the installer read one list, and the roles `INSTALL`
  offers are those a set's programs serve.
- **Sample data.** The host `INV /CMD /LOCAL` plays the requests of `REL/SAMPLE.REQ` on an empty
  directory with the clock fixed by `/NOW:` (`SAMPLE_NOW` of `mkdist.sh`); every request must
  succeed and every file pass `CHKDBF.PY --strict`. It holds a small collection: a tree of
  locations (an office with shelves, bins, a cabinet and a workbench with a drawer, a garage
  with a rack, a box and a bucket, one type added to the seeded list, codes on most), items at
  every level from a room's floor down, systems with their installed parts, service records,
  open, overdue and returned loans, and the users `ADMIN`, `OPER` and `READER` (levels 3, 2 and
  1), the first password the user name in lower case, with a password change due. Every build
  makes it anew; it is never committed.
- **Text files.** `REL/README.TXT` (install and first-login text), `LICENSE` (as `LICENSE.TXT`),
  `REL/WATTCP.TXT` (the WATTCP licence notice for `INVTCP.EXE`; no WATTCP source, object or
  library is on the disks), and the samples `REL/INVSERV.CFG` and `REL/WATTCP.CFG` go on the
  disks with CR LF line ends, their `@NAME@` marks replaced by the memory figures of `MAKEFILE`
  (`DIST_MARKS`: `INV_FREEKB_WC65`, `THINSCR_FREEKB_WC65` and the others), `@VERSION@` by
  `PROTO_VERSION` and `@THIN@` by `THIN` of `SRCS.MK`, a mark `REL/DISKS.TXT` may hold too, so
  that the network-only client's file is named by the build alone.
- **Compression.** Every file of `DISKS.TXT` but `INSTALL.EXE`, `INSTALL.LST`, `SETUP.EXE`,
  `README.TXT`, `LICENSE.TXT` and `WATTCP.TXT` is marked Z and goes on the disks compressed by
  `REL/szdd.py` in Microsoft's `COMPRESS` format (`SRC/TOOLS/LZEXP.H`), byte for byte the same
  on every run, under the name `COMPRESS -r` gives it (`lzname` of `mkdist.sh`, `lz_name` of
  `LZEXP.C`: `INV.EX_`); the sample data goes in the directory `DATA._`, each file compressed
  under its own name. `mkdist.sh` stages each disk's files as they are in `OBJ/DIST/PLAIN/L/`,
  then as they go on the disk in `OBJ/DIST/L/`, compressing each file once, for all the disks
  that hold it. `INSTALL` and `SETUP` expand them while they copy (`lz_copy` of `LZCOPY.C`), and
  copy a file whose compressed name is missing as it is, so an installation or the zip's folder
  serves as a source too (`DOC/DESIGN.md`, Design decisions: Release disks).
- **Images.** mtools formats each disk (FAT12, the volume label and serial number of
  `DISKS.TXT`) and copies the files with every time stamp at `SAMPLE_NOW` (`SOURCE_DATE_EPOCH`,
  in UTC), so the same programs give the same images byte for byte; what each set holds is in
  `DOC/INSTALL.md` (The disks). mtools gives the 1.2 MB disk the layout of DOS's own `FORMAT`:
  15 sectors per track, 2 heads, 80 cylinders, one sector per cluster, 224 root entries, two
  7-sector FATs, media byte F9h. `make dist` prints the files and free bytes of each disk.
- **Zip.** `REL/diskzip.py` packs the files of the 1.44 MB disk as they are, none compressed,
  into `RETROINV.ZIP` under the folder `RETROINV/` (both named after that disk's volume
  label), for an install on a hard disk or in an emulator. It is as byte-stable as the images:
  the entries in name order, deflated at level 9, each dated `SAMPLE_NOW` like its file on the
  disk, a plain Unix file with the permissions 0644, and no extra field or comment. Its bytes
  also depend on the host's zlib, whose version `HOST/Containerfile`'s Debian snapshot pins.

`make distcheck` then runs `REL/distchk.sh`:

- Every image is read back with mtools (in UTC, keeping the files' times) and must equal the
  files staged for it. `diskzip.py --check` then holds the zip to each promise above on its own,
  against the 1.44 MB disk's files as they are: names in order and under `RETROINV/`, deflate
  and its very bytes at level 9, the system, attributes and date of every entry, no extra field
  or comment. Its entries, extracted with their CRCs checked, must equal those files. Every disk
  must hold `INSTALL.EXE`, `INSTALL.LST`, `README.TXT`, `LICENSE.TXT` and `WATTCP.TXT` in its
  root as they are, and its other files one for one with those it holds as they are: each that
  its list marks Z compressed under its `COMPRESS -r` name, the sample data in `DATA._`, and
  `msexpand` of the `mscompress` package, a reader apart from `LZEXP.C`, must give each back.
  The disk with `SETUP.EXE` must hold every file `SETUP` copies; the disks of each size that
  hold `INV.EXE` must hold the sample data, and those of each size but 360 KB `INVWIN.EXE`.
  Every file a disk takes from the build must be the build's bytes, and every `INSTALL.LST`
  within the bounds of `INSTPLAN.H`.
- `INSTALL` then installs from each set in one boot of `TEST/INTEG/boot310.sh`, all at once,
  the set's first disk as `B:` and a hard disk as `C:`, which the boot keeps (`boot310.sh -k`)
  so that its files can be compared with what the installations must leave. A two-disk set's
  questions are typed on the keyboard by `TEST/INTEG/dialog.py` (`boot310.sh -D`), which changes
  `B:` to each disk `INSTALL` asks for; the others' answers are in files:
  - `INST360`, MS-DOS 3.10 (which in QEMU reads no larger floppy): a server with the sample data
    in `C:\RETROINV` from the 360 KB disk 1, the question for disk 2 answered first with disk 1
    still in `B:`, which `INSTALL` must refuse by its label; then the network-only client in
    `C:\THIN` from disk 2, which asks for disk 1.
  - `INST720`, MS-DOS 3.30: the same from the 720 KB disks, the server with the Windows client.
  - `INST120`, MS-DOS 3.30: the same from the 1.2 MB disk.
  - `INST144`, MS-DOS 3.30: from the 1.44 MB disk, the network-only client with the Windows
    client in `C:\THIN`, a server with the sample data in `C:\SRV` and again over an edited
    `INVSERV.CFG`, which must stay, and one PC alone with an empty inventory in `C:\ONE`, from
    `C:\SRV\INSTALL` as from the zip's folder; then `INSTALL /U` removes `C:\ONE` with its data
    and `C:\SRV` keeping its data.

  Every run of `INSTALL` must exit 0. Once the server is installed, its programs and the
  network-only client run from `C:\RUN`, which holds a copy of the server's `DATA\` and
  `INVSERV.CFG`. `INV /KEYS:` runs `REL/HELP.KEY` (help on the login screen, then Esc), and its
  snapshot must equal the host `INV`'s but for the line of unused stack. `INV /CMD /LOCAL` must
  answer `REL/CHECK.REQ` exactly as the host `INV` does on the same data (a login that owes a
  password change, the change, the location types, the location tree and everything in it,
  items, an assembly, service records and overdue loans, and `ADMIN`'s password reset). `INVTCP
  /?`, `COMTEST /?`, `DBPACK`, `INVDUMP` and the network-only client's `/?` must print their
  usage, and, but in `INST144`, `INVSERV` must read `INVSERV.CFG` and stop for want of NetBIOS
  (`INVSERV_NONBRC`). The hard disk, `C:\RUN` and `CHECK.REQ` aside, must then hold the files of
  the installations' roles, each byte for byte as it is, from the first disk of the set that
  holds it; in `INST144`, `C:\THIN` and only the sample data in `C:\SRV`, `C:\ONE` gone. Not
  booted: a full or write-protected disk and a disk of another set under DOS (`T_INST` covers a
  failed write, a damaged compressed file and a disk of another set on the host), and the Turbo
  C and Open Watcom builds of `INSTALL`, which are not shipped.
- Last, `mkdist.sh` runs twice more, before and after the boots, with every mark of the staged
  `REL/` files set to 0, and both runs must write the same `SHA256.TXT`: the images and the
  zip are byte-stable.

## Runtime requirements

What a site needs, the free-memory figures among it, is in `DOC/INSTALL.md` (Requirements). This
section says what each figure covers and how it is measured.

- `INV /LOCAL` (the program and its in-process engine) needs, as `CHKDSK` reports it where INV
  is started, at least `INV_FREEKB_TC`, `INV_FREEKB_WC65` or `INV_FREEKB_OW` KB of free
  conventional memory for the Turbo C 2.01, Watcom C 6.5 or Open Watcom build (`MAKEFILE`), so
  that the deepest nesting of screens (a list, an item form, its tab, a pick list (the tag pick
  list included) or the memo editor or help over it, the location tree's contents with an item's
  location pick list and help over it, or with an item's Parts tab and the item pick list over
  it, or with an item's Loans tab, a new reservation's form and the item pick list of its host
  over it) and the report viewer with help over it still open, every list on the way holding its
  ring of page cursors (`DOC/CLIENT.md`, The list view), and the outermost list's ring in its
  larger block: the location tree's, or the item list's after paging in maker order.
  Each figure is the least free memory, in 4 KB steps, at which `T_SCRE`'s deepest scripts
  (`T_SCRE_DOSFLOOR`) and INV's own deepest golden-screen scripts (`DEEPMEMO`, `DEEPLOAN`,
  `DEEPPICK`, `DEEPITEM`, `DEEPHOLD`, `DEEPTAG`, `TAGPICK`, `RPTHELP`; all but the last on the
  data of `#@ DEEP`, paging each list they pass to its end and back, so that each takes its ring,
  the location tree's and the location pick list's, and in `DEEPMEMO` the item list's in maker
  order, with cursors enough to move it to the larger block) pass on MS-DOS
  3.10, a VDISK of growing size taking the rest, and under DOSBox-X with a CGA and with a
  Hercules adapter, the most of the three; it is measured again whenever the program grows or
  the screens' far allocations change, and every INV script run checks it (Golden screens).
  With less, a screen that cannot get memory does not open and says "Not enough memory" until
  the next key, and a list that cannot get memory for its ring stays open without it: going up
  past the pages it holds then starts again at the first page. So it does sooner on a list
  whose long cursors cannot get the ring's larger block, which keeps fewer pages. The
  full-screen `INV` over a link to a server has figures of its own, `INVSCR_FREEKB_<build>`
  over COM1 and `INVSCRNET_FREEKB_<build>` over NET (below).
  The memory the engine takes per open data directory is in `DOC/ENGINE.md` ("Memory per open
  engine").
- With the network loaded, as `CHKDSK` reports it where the program is started, at least
  (`MAKEFILE`, per build `OW`, `TC` or `WC65`):
  - `INVSERV_FREEKB_<build>` KB for `INVSERV` serving every NetBIOS session it can, a serial
    port and a mailbox (`SESSIONS=6`, `SERIAL=COM1` and `MAILBOX=` in `TEST/INTEG/SRV.CFG`) on a
    data directory it first creates, on a 640 KB PC running MS-DOS 3.30 with `SHARE` and MS
    Client 3.0 NetBEUI with the basic redirector, without upper memory; with one kilobyte less
    its first start exits with `SRV_EXIT_NOMEM`;
  - `INVNET_FREEKB_<build>` KB for `INV /CMD /NET`, which with less exits at start-up;
  - `INVCOM_FREEKB_<build>` KB for `INV /CMD /COM1`;
  - `THINNET_FREEKB_<build>` and `THINCOM_FREEKB_<build>` KB for the network-only client (`THIN`
    of `SRCS.MK`) in the same two runs, `srvgate.sh -t` running it in INV's place.

  These are the least at which `TEST/INTEG/srvgate.sh` passes with `-m`, `-c` or `-s`, one
  kilobyte less failing: exact, unlike `INV_FREEKB_<build>`, as with less the program stops at
  start-up. `make integ` runs the server gate of each build with them, and they are measured
  again whenever INVSERV, INV or THIN grows.
- Full-screen with the network loaded, the network-only client needs at least
  `THINSCR_FREEKB_<build>` KB over COM1 and `THINSCRNET_FREEKB_<build>` KB over NET, and `INV`
  `INVSCR_FREEKB_<build>` KB over COM1 and `INVSCRNET_FREEKB_<build>` KB over NET (`MAKEFILE`):
  each the least, in 4 KB steps as `INV_FREEKB_<build>`, at which its run of
  `TEST/INTEG/scrgate.sh` passes (Screen gate), running the key scripts that set
  `INV_FREEKB_<build>`, 4 KB less failing. The NET link holds its far blocks from the moment it
  opens: its two messages (`NBC_BUFSZ`, twice `NB_MSGMAX`, `NBCLI.C`) and the NCBs of
  `nba_open`, more than the serial link holds (`SERCLI.C`, `CUART.C`). That leaves less for
  the deepest screens' far blocks, which set the figures (`msg_load`, which reads `INV.MSG`
  whole at start-up, needs less); so NET's figures are COM1's or a step or two higher.
  Scripted mode loads no `INV.MSG` and opens no screen, so there NET and
  COM1 (`THINNET_FREEKB_<build>`, `THINCOM_FREEKB_<build>`) differ by at most a kilobyte.
  `make integ` runs the screen gate of each build with all four; they are measured again
  whenever INV, THIN or the screens' far allocations grow.
- `INVTCP` needs, with NetBIOS and the packet driver loaded, at least `INVTCP_FREEKB_<build>` KB
  (`MAKEFILE`), the least at which `TEST/INTEG/tcpgate.sh` passes with `-m`, one kilobyte less
  failing; `make tcp` runs the TCP gate of each build with them.
- No figure above may pass `DOS_MAXFREEKB` (`MAKEFILE`), the ceiling of the floor
  (`DOC/DESIGN.md`); `make lint` checks them all. The ceiling is not a budget to fill: a figure
  rises only by a new measurement, in the commit whose growth needs it.
