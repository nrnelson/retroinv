@VERSION@ - an inventory of a retro computer collection for MS-DOS
=====================================================================

RETROINV keeps an inventory of a computer collection - items, their parts,
locations, service records, loans, reservations and the equipment in use
with each system - in dBASE III files.  INV.EXE is the program you use:
it opens the data itself on a single PC, or calls the server INVSERV.EXE
over a NetBIOS network or a serial line so that several PCs share one data
directory.  @THIN@.EXE is INV without the engine that opens the data: it
only calls a server, in much less memory.  The server also answers request
files left in a mailbox directory, and INVTCP.EXE lets TCP/IP clients
reach it.  INVWIN.EXE is the same client for Windows 3.0 and later.

INSTALLING

  Put any disk of the set in drive A: and type A:INSTALL.  It asks what
  this PC is - alone, the server, a network client, or one too small for
  INV, which gets @THIN@ - whether to add the Windows client, the
  directory (C:\RETROINV unless you type another) and, for new data, the
  sample data or an empty inventory.  Enter takes the answer in brackets;
  Esc stops.  It copies only what the answers need, expanding the files
  the disks hold compressed, and asks for each other disk by its label.
  No program runs from the disks.  INSTALL changes nothing outside its
  directory: put FILES=45 in CONFIG.SYS yourself.  Run again, it keeps
  DATA and asks before it replaces INVSERV.CFG or WATTCP.CFG.  INSTALL /U
  in the directory removes it all, DATA if asked.
  Windows: install the Windows client with INSTALL, then in Program
  Manager choose File > Run, type C:\RETROINV\SETUP and give it the
  link switches (see STARTING): it makes the group RETROINV.
  C:\RETROINV\SETUP /U removes the group and INVWIN, never the data.
  By hand: unpack RETROINV.ZIP, whose files are not compressed, or with
  DOS 5 or later MD C:\RETROINV and C:\RETROINV\DATA, then type
  EXPAND A:INV.EX_ C:\RETROINV\INV.EXE for each compressed file, and
  expand the files of A:\DATA._ into C:\RETROINV\DATA under their own
  names; for an empty inventory, leave DATA empty.  A disk's files copied
  to the hard disk stay compressed: INSTALL there installs them into
  another directory, never their own.

THE DISKS

  360 KB disk 1   INV.EXE INV.MSG INV.HLP, @THIN@.EXE, the network-only
                  client
  360 KB disk 2   INVSERV.EXE INVSERV.MSG INVSERV.CFG, INVTCP.EXE WATTCP.CFG,
                  DBPACK.EXE INVDUMP.EXE COMTEST.EXE, the sample data
  720 KB disk 1   everything on the 360 KB disks
  720 KB disk 2   INVWIN.EXE INVWIN.HLP INVWIN.HTX INV.MSG SETUP.EXE, the
                  client for Windows
  1.2 MB disk     everything on the 720 KB disks
  1.44 MB disk    everything on the 720 KB disks

  Every disk also holds INSTALL.EXE, INSTALL.LST, this file, LICENSE.TXT
  and WATTCP.TXT.  The other files but SETUP.EXE are compressed as
  Microsoft's COMPRESS makes them: INV.EX_ holds INV.EXE, DATA._ the
  sample data.  RETROINV itself is under the MIT licence; its source
  is at https://github.com/nrnelson/retroinv.  WATTCP.TXT is the licence
  notice of the TCP/IP library inside INVTCP.EXE.  MS-DOS 3.1 reads the
  360 KB disks and, in a 1.2 MB drive, the 1.2 MB disk; the 720 KB disks
  need DOS 3.2 and the 1.44 MB disk DOS 3.3.
  The DOS programs are built with Watcom C 6.5.  DBPACK, INVDUMP and
  COMTEST are linked with a reconstruction of its small-model run-time
  library, damaged on the only surviving disks:
  https://github.com/nrnelson/watcom-c-6.5-restoration

WHAT A PC NEEDS

  MS-DOS 3.1 or later, an 8088 or better, and FILES=45 in CONFIG.SYS.
  Free memory as CHKDSK reports it:
    INV /LOCAL                     @INV_FREEKB_WC65@ KB
    INV /NET, network loaded       @INVSCRNET_FREEKB_WC65@ KB
    INV /COM1, network loaded      @INVSCR_FREEKB_WC65@ KB
    INV /CMD /NET, network loaded  @INVNET_FREEKB_WC65@ KB
    INV /CMD /COM1, network loaded @INVCOM_FREEKB_WC65@ KB
    INVSERV, network loaded        @INVSERV_FREEKB_WC65@ KB
    INVTCP, network and packet drv @INVTCP_FREEKB_WC65@ KB
  @THIN@ needs, network loaded:
    /NET                           @THINSCRNET_FREEKB_WC65@ KB
    /COM1                          @THINSCR_FREEKB_WC65@ KB
    /CMD /NET                      @THINNET_FREEKB_WC65@ KB
    /CMD /COM1                     @THINCOM_FREEKB_WC65@ KB
  A network PC needs a NetBIOS provider such as MS Network Client 3.0; a
  server with SESSIONS=0, serving only serial lines and a mailbox, does
  not.  INVTCP also needs a packet driver and, with NetBEUI, a second
  network card.  A serial line needs a full null-modem cable.
  SHARE.EXE on the PC that holds the data stops a second program opening
  it by mistake.
  INVWIN needs Windows 3.0 or later in standard or 386 enhanced mode, so
  a 286 or better, with an EGA or better; Windows 3.0 will not start it
  in real mode.  It needs no FILES=, as Windows enlarges DOS's file
  table itself, but DOSBox-X's own DOS keeps a table Windows cannot
  enlarge: there give it files=65.

STARTING

  One PC: type INV in C:\RETROINV.
  Network: start INVSERV on the PC that holds DATA\ (its settings are in
  INVSERV.CFG) and type INV /NET RETROINV on the others, or, on a PC too
  small for INV, @THIN@ /NET RETROINV, which takes INV's switches except
  /LOCAL.  Where INV fits, it is the simpler choice: one program on every
  PC, which can also open data itself.
  Serial line: add SERIAL=COM1 (and SESSIONS=0 on a PC without a network)
  to INVSERV.CFG and type INV /COM1 (or @THIN@ /COM1, as above) on the
  client; COMTEST (COMTEST /COM2 for COM2) checks a PC's port first.
  TCP/IP: on a PC of its own, put its addresses in WATTCP.CFG, then type
  INVTCP RETROINV in C:\RETROINV (or set the WATTCP.CFG variable to the
  directory of WATTCP.CFG).  Its START line ends with the TCP port.  Any
  program that speaks RETROINV's line protocol is a TCP client, such as a
  line-mode nc host port; INV itself does not use TCP.  F3, then Y,
  stops INVTCP.
  Windows: INVWIN takes INV's switches /LOCAL, /NET name, /COM1 and /COM2
  and its settings file INV.CFG, and two more: /MBOX:dir, through the
  server's mailbox, and /TCP:address, through INVTCP, for which Windows
  needs a Winsock (WINSOCK.DLL).  A mailbox: make the directories REQUEST
  and RESPONSE in a directory the server and the Windows PCs all reach,
  say F:\MBOX, put MAILBOX=F:\MBOX (and SESSIONS=0 on a PC without a
  network provider) in INVSERV.CFG and start INVSERV; give each Windows
  PC a name of its own, STATION=PC2 in its INV.CFG, and start
  INVWIN /MBOX:F:\MBOX there.

FIRST LOGIN

  The sample data has three users.  The first password is the user name
  in lower case and must be changed at the first login:
    ADMIN   level 3, administrator: users, backup, pack, import
    OPER    level 2, operator: add and change items, service records,
            loans, reservations and uses
    READER  level 1, read only
  An empty inventory has the one user ADMIN, password admin.
  These passwords are known to everyone who has read this file.  Before
  the first INVSERV, run INV on the server's PC and log in as ADMIN,
  choosing a new password; in Admin > Users, give each sample user you
  keep a new temporary password, which that user must change at their
  first login; and delete the rest.

FIRST SESSION

  1. Type INV, log in as ADMIN, password admin, and type a new password
     twice when asked.
  2. The home screen shows today's date: if it is wrong, quit (F3) and
     set the PC's clock with DATE and TIME, as loans, reservations and
     service records take today's date.
  3. Locations > New location adds a place, a room first; shelves, boxes
     and bins go inside it (Inside, F4 to choose).
  4. Items > New item adds an item, its Location chosen with F4.
  5. F1 shows the help of every screen, F9 the keys; F10 or Alt and a
     letter opens the menus.  Reports show on the screen first; Options >
     Settings sets the printer.

SECURITY

  Requests, answers and passwords cross every link but a single PC in
  plain text: the network, the serial line, the mailbox share and
  INVTCP's TCP port, the last word of its START line.  INVTCP takes a
  connection from any address unless /ALLOW: names the networks it may
  come from: INVTCP /ALLOW:192.168.1.0/24 RETROINV ("Running the bridge"
  in DOC/SERVER.md of the source).  Anyone who can read DATA or a backup
  reads the inventory, and anyone who can write DATA can change anything
  in it, the users included.  So run the server and the bridge only on a
  network of machines you trust, never let INVTCP's port be reached from
  the Internet, and give each user a password used nowhere else.  Let
  only the server's PC reach DATA and the backups, and only the stations
  that use it reach the mailbox.  Admin > Audit log, Table USERS, shows
  failed logins (F); keep IDLE= in INVSERV.CFG above 0.  A forgotten
  password is set anew by an administrator in Admin > Users; if no
  administrator can log in, see "A lost password" in DOC/SECURITY.md of
  the source (THE DISKS).

BACKUP

  Back up the data often: Admin > Backup in INV or INVWIN copies every data
  file to a directory you name, which must exist, and Admin > Restore
  brings such a copy back.  Both need the data to themselves: on a
  network, no other session may be open.
