# RETROINV — Full-Screen Client

`INV.EXE` without `/CMD` is the full-screen client: a login screen, then menus, lists and forms
over the inventory, drawn on an 80 × 25 text screen (`VID_COLS` × `VID_ROWS`; MDA, Hercules,
CGA, EGA or later) and driven from the keyboard, and from the mouse when a mouse driver is
loaded. It is a protocol consumer only: every screen builds request lines, sends them through
`LINK.H` and reads the response lines, as a program on a serial line would. It never calls the
engine or the protocol layer, so the same screens work over every link. All of `SRC/CLIENT/` is
portable ANSI C89; the screen, keyboard, mouse, timer and far memory come from `SRC/COMPAT/`
through `COMPAT.H`.

## Modules

| File | Responsibility | Uses |
|---|---|---|
| `COMPAT/CMEM.C` | Far blocks (`cmem_*`); `cmem_ptr` for COMPAT modules | a near-heap module (CMEMHI or CMEMHR) |
| `COMPAT/CMEMHI.C`, `COMPAT/CMEMHR.C` | The near-heap module of INV and of `INVTHIN.EXE`: `CMEM_INVHEAP`, or for `INVTHIN.EXE` the smaller `CMEM_REMHEAP`, as Turbo C's `_heaplen`, and `CMEM_NEARHEAP` as `cmem_heapsz`, by which `cmem_init` grows the Open Watcom near heap | — |
| `COMPAT/CVIDEO.C` | Adapter detection, the shadow screen with changed-cell spans, direct or INT 10h output, CGA snow avoidance, cursor, saving and restoring the DOS screen (`vid_*`) | CMEM |
| `COMPAT/CKEY.C` | INT 16h keys mapped to `KEY_` codes with INT 28h while idle, the mouse's presses after them, the key source hook (`key_*`) | CDOS |
| `COMPAT/CMOUSE.C` | The mouse through the INT 33h driver (`key_mouse` and the click codes of `CMOUSE.H`, apart from `COMPAT.H` for Watcom C 6.5's compiler memory): the driver found without calling a null or dummy vector, presses read as `KEY_ESC` and clicks, coded with their double clicks timed by the tick count by `key_ms_press`, which the host build has too, the pointer shown only while `key_read` waits; only INV and `INVTHIN.EXE` link it | CKEY CDOS CTIMER |
| `COMPAT/CTIMER.C` | BIOS tick counter, wrap-safe elapsed ticks, delays (`tmr_*`) | CDOS |
| `COMPAT/CFINE.C` | The timer chip's count (`tmr_fine`), the server's entropy for login tickets; only the programs that make tickets link it | — |
| `COMPAT/CDOS.C` | DOS services (`dos_*`), among them `dos_machine_name` (5E00h), `dos_break_off`, `dos_break_restore` and `dos_break_get` (33h, INT 23h), `dos_dev_raw` (IOCTL 4400h/4401h); the stack check `cstk_fill`/`cstk_unused` | — |
| `COMPAT/CNETB.C` | NetBIOS through INT 5Ch (`nb_*`), the no-wait command sets (`nba_*`) and their buffers (`nba_alloc`); under Windows through `NetBIOSCall`, every command polled, the NCBs and buffers in `GlobalDosAlloc` memory | CMEM CTIMER CDOS |
| `COMPAT/CNBNAME.C` | The program's local NetBIOS name (`nb_local_name`): the machine name, or one made up from a prefix | CNETB CDOS CTIMER |
| `COMPAT/CNBDRAIN.C` | The wait at exit for the NCBs `nba_close` kept (`nba_drain`), which a key ends | CNETB CKEY CDOS |
| `COMPAT/CUART.C` | COM1 and COM2 (`uart_*`): chip detection, one far block per open port holding the interrupt handler and the receive and transmit rings, RTS/CTS flow control, the chip's loopback | CMEM CDOS |
| `COMPAT/CCOMM.C` | COM1 and COM2 of a Windows program (the client's `uart_*` calls) through the comm driver's queues, RTS/CTS flow control, the transmit queue overwritten with zeros once it has drained and at close | CMEM CTEXT |
| `COMPAT/CWSOCK.C` | A Windows program's one TCP connection (`sk_*`) through Windows Sockets 1.1, loaded at run time only once `OpenFile` finds `WINSOCK.DLL`: a non-blocking socket whose events come through a hidden window, every wait bounded and yielding, Esc ending the connect | CTIMER CDOS |
| `COMPAT/CPRN.C` | The printer (`prn_*`): BIOS parallel ports through INT 17h, the DOS device PRN on handle 4 in raw mode, the capture file of `prn_capture` | CDOS |
| `COMPAT/CPRNW.C` | A Windows program's printer (`prn_*`, `prn_cancel`): the default printer of `WIN.INI` through GDI, a job's text, line ends, page ends and Epson styles laid out on its pages in the printer's fixed-pitch font, double width in a font of the printer that is twice as wide or else left out, the abort procedure yielding and ending a cancelled job | CDOS |
| `COMPAT/CTEXT.C` | Text helpers (`txt_*`, the `TXT_` macros of `COMPAT.H`): digits, case-blind compares, bounded copy, secret wipe, the date check, path extensions, words of a line | — |
| `CLIENT/MSG.C` | The message file `INV.MSG`: load into far blocks, lookup by number (`msg_*`); file reading: `msg_fopen`, the one-read load of a file or indexed section into a far block (`msg_fread`, `msg_section`), the buffered line reader (`msg_rd_*`); `msg_date`, `msg_dec` | CMEM CDOS |
| `CLIENT/RQ.C` | Request building with the protocol's quoting; response decoding into data-line callbacks and an `rq_res` | LINK |
| `CLIENT/RQERR.C` | What a failed request tells the user and what the screen does next (`rqe_class`), the text of a storage error (`rqe_text`) and of a DOS error of the client's own files (`rqe_doserr`), the refusal below an access level (`rqe_deny`); no screen and no request | MSG RQ |
| `CLIENT/TUI.C` | Palettes, drawing, frames, hints and messages, `tui_key` with F1 and F9 help and the hint row's clicks, the clicks' cells (`tui_hit`), the key names (`tui_keyname`), message box, confirm, line editor, prompt, scrolling pick list, menu bar and pull-downs, status line, permission checks, `tui_fail`, `tui_fatal`, the main loop `tui_run` | CVIDEO CKEY CMOUSE CMEM CDOS MSG RQ RQERR |
| `CLIENT/PAGER.C` | Paging one list command (`pg_*`), portable, with no screen or key of its own: the two pages and the cursor ring, page fills, the End walk, reload, the bad-cursor restart, the rows above the first page held, an unpaged command's response paged by the pager itself and a walk's keys handed to a key sink (Windows and host builds), progress and failures through the caller's functions; the row source (`lv_src`) and the format helpers `lv_val`, `lv_col`, `lv_datecol`; for a survey of a list and a jump into it, its page fill and load (`PAGERINT.H`, Windows and host builds) | RQ MSG CMEM |
| `CLIENT/PAGERSV.C` | The survey of a pager's list and the jump into it (`psv_*`, Windows and host builds): the rows counted and each page's start cursor kept, one key-only page after another, and a page loaded from a kept start with the rows above it known and the starts before it on the cursor ring, all through the pager's own page fill, load and ring push; the keys of the rows a survey counts, collected into a set through the pager's key sink | PAGER RQ CMEM KEYSET |
| `CLIENT/KEYSET.C` | A set of row keys in far memory (`ks_*`, Windows and host builds): the keys of a whole list in its order, for a selection of all its rows, in one block that doubles as keys come, up to `KS_MAXROWS` | CMEM |
| `CLIENT/INTL.C` | Dates and numbers in the Control Panel's International formats (`intl_*` of `INTL.H`, linked by INVWIN): the short date pictures it supports, ISO otherwise, and the decimal and thousands separators and leading zero of a number; portable, the settings given by the caller | MSG |
| `CLIENT/LISTV.C` | The paged list view (`lv_*`): a pager drawn in a window, its keys and clicks, its progress and failures on screen, the note a form leaves | TUI MSG RQ PAGER CVIDEO CMOUSE |
| `CLIENT/FMODEL.C` | The form model (`form_*` of `FMODEL.H`): values and originals over a form table, the local checks, the save split into ADD and SET requests, the field a failed save names, the message naming a choice, the wiping of password values; no screen and no key | RQ MSG |
| `CLIENT/FORM.C` | Typed forms (`form_*` of `FORM.H`): the box, drawing and editing over the model | TUI MSG RQ FMODEL MEMO CVIDEO CKEY |
| `CLIENT/MEMO.C` | The memo editor (`memo_edit`, `memo_load_count`, `memo_leave`): the window, its keys and their limits, the messages of a load and a failed save | TUI MSG RQ FMODEL MEMOIO CVIDEO |
| `CLIENT/MEMOIO.C` | A record's notes as every client holds them (`mio_*` of `MEMOIO.H`): the far block of lines within FMODEL's memo limits, the NOTES request and the conversions of a load, the line count, the state of a loaded memo, the SET and NOTES ADD requests of a save; no screen and no key | RQ CMEM |
| `CLIENT/MIOTEXT.C` | The memo as one text with CR LF line breaks and back (`mio_text`, `mio_set_text`), for a client that edits it so; INV does not link it | MEMOIO |
| `CLIENT/KEYSCR.C` | Key scripts with their clicks, masks and screen snapshots for tests (`ks_*`) | CKEY CMOUSE CVIDEO CDOS MSG TUI |
| `CLIENT/CFG.C` | `INV.CFG` read and write (`cfg_*`) | MSG CDOS; the constants of PRINT, RPT and CPRN, and `UART_BAUDOK` |
| `CLIENT/PRINT.C` | Output jobs (`pr_*`): a buffer to the printer or a text file, Epson ESC/P codes, the wait function asked when the printer does not take a byte | CPRN CDOS |
| `CLIENT/RPT.C` | Report and label layout (`rpt_*`): pages, headers, groups, totals, category totals, labels; no screen and no request | PRINT MSG CMEM RQ (the line type only) |
| `CLIENT/RPTRUN.C` | The run of a report or labels (`rrun_*`): the requests of each kind, the far page buffer, the end by `rpt_close` or `rpt_abort`; progress and stop through a step function, no screen | RQ RPT PRINT CMEM CODETAB; SCRLFORM's `scr_loc_info` |
| `CLIENT/SESS.C` | The steps of a session (`sn_*`): the station, `HELLO` and `VER`, `LOGIN` and its outcome, `PASSWD` after its local checks, the record counts of `STATUS`; the wipe of the login password; no screen and no key | RQ CDOS; CFG's default station |
| `CLIENT/SCRLOGIN.C` | Login screen, home screen | TUI MSG RQ SESS FORM SCRPASS CKEY CTIMER CVIDEO; RPT's program name |
| `CLIENT/SCRITEM.C` | Item lists, item form with its tab strip | TUI MSG RQ LISTV FORM MEMO ITEMLS SCRIFORM SCRASSY SCRMAINT SCRLOAN SCRLOC SCRPICK |
| `CLIENT/ITEMLS.C` | The item list's row source (`itl_src`, `itl_row`): its command, orders, field lists, headers and the columns of INV's rows, and the field names and works words every client's item rows read; no screen | PAGER's row types and `lv_col`, SCR's commands, SCRNFORM's reasons |
| `CLIENT/SCRIFORM.C` | The item form's table (`scr_item_def`), a new item's first values, a read item's empty parent and its line of held units (`scr_item_kline`, `scr_item_loaded`), apart from `SCRITEM.C` so that a Windows program can link them | FMODEL, ITEMLS's field names, SCRSFORM's choice names, SCRNFORM's kinds' names, the grid of `SCRSHR.H` |
| `CLIENT/SCRASSY.C` | Assemblies list, the Parts tab, install, remove and move | TUI MSG RQ LISTV SCRITEM SCRPICK SCRAFORM |
| `CLIENT/SCRAFORM.C` | The parts list's row source (`scr_assy_src` and its functions) and the assembly requests (`scr_assy_send`), apart from `SCRASSY.C` so that a Windows program can link them | RQ MSG, PAGER's row types and `lv_col`, ITEMLS's field names, `SCRSHR.H`'s indent levels, SCRIFORM's `MI_NOITEM` |
| `CLIENT/SCRMAINT.C` | Recent service, the Service tab, the service record form | TUI MSG RQ LISTV FORM MEMO CVIDEO SCRITEM SCRMFORM |
| `CLIENT/SCRMFORM.C` | The service record form's table (`scr_maint_def`) and the service lists' row sources (`scr_maint_src`, `scr_maint_isrc`, `scr_maint_row`), apart from `SCRMAINT.C` so that a Windows program can link them | FMODEL, PAGER's row types and `lv_col`, ITEMLS's `ITEM_ID`, the grid of `SCRSHR.H` |
| `CLIENT/SCRLOAN.C` | Loans and holds: the list of every item's rows and its views, the Loans tab with the rows an item hosts, the forms of the three kinds, a new row, return and release | TUI MSG RQ LISTV FORM CVIDEO SCRITEM SCRPICK SCRNFORM |
| `CLIENT/SCRNFORM.C` | The forms of loans, reservations and uses (`scr_loan_form`, one table built per kind), the lists' row source (`scr_loan_src`, `scr_loan_view` and their functions) and the shared texts of `SCRNFORM.MSG`, apart from `SCRLOAN.C` so that a Windows program can link them | FMODEL, PAGER's row types and `lv_col`, ITEMLS's field names, the grid of `SCRSHR.H` |
| `CLIENT/SCRLOC.C` | Location tree, location form, contents | TUI MSG RQ LISTV FORM MEMO SCRITEM SCRPICK SCRLFORM |
| `CLIENT/SCRLFORM.C` | The location tree's row source (`scr_loc_src`, `scr_loc_row`), the location form's table (`scr_loc_def`), a form's location field (`scr_locf`), what `LOC GET` says of a location (`scr_loc_info`) and the type of the first location inside one (`scr_loc_first`), apart from the screens so that a Windows program can link them | PAGER's row types and `lv_col`, FMODEL's types, RQ's lines, CODETAB's type names |
| `CLIENT/SCRPICK.C` | What the screens share of pick lists: the code lists' pick list and fields (`scr_pick_code`, `scr_pick_field`), the location tree's view (`scr_lt_*`) that the tree and the location pick list (`scr_pick_loc`) show, with the one location types' list every open view shares (`scr_lt_types`), a location's `LOC GET` (`scr_loc_get`), a form's location field with its path (`scr_loc_show`, `scr_loc_field`), the item prompt and the item pick list (`scr_ask_item`, `scr_pick_item`), and the Del key's request (`scr_del`) | TUI MSG RQ LISTV FORM CVIDEO CODETAB SCRLFORM SCRSFORM SCRIFORM |
| `CLIENT/SCRCODE.C` | The Categories and Location types screens: a code list in a box, an entry's form | TUI MSG RQ FORM CODETAB SCRPICK SCRKFORM |
| `CLIENT/SCRKFORM.C` | The code lists' kinds (`SCR_CK_`), their messages (`ME_`) and their entries' forms (`scr_code_defs`), apart from `SCRCODE.C` so that a Windows program can link them | FMODEL, the box of `SCRSHR.H` |
| `CLIENT/CODETAB.C` | A code list (`CAT LIST`, `LTYPE LIST`) held whole in a far block (`ct_*`), names looked up by code; no screen, for every client's pick lists, type names and reports | RQ CMEM |
| `CLIENT/SCRSRCH.C` | Text, serial and filter search | TUI MSG FORM SCRITEM SCRPICK SCRSFORM |
| `CLIENT/SCRSFORM.C` | The search form's table (`scr_search_def`), the search its values ask for (`scr_search_get`) or why none (`scr_search_why`), its command, parts and results' title, the search an item field's typed text asks for (`scr_search_item`) and its item list source (`scr_search_src`), apart from `SCRSRCH.C` so that a Windows program can link them | FMODEL, RQ, MSG, ITEMLS, SCR's commands |
| `CLIENT/SCRRPT.C` | Report and label options forms, the run's progress, stop and printer questions, the report viewer | TUI MSG RQ FORM PRINT RPT RPTRUN CDOS CVIDEO SCRPICK SCRRFORM; CPRN's error codes |
| `CLIENT/SCRRFORM.C` | The report and label forms' table (`scr_rpt_form`), their first values and the run's filters and subtitle read from them, the SCREEN output's scratch file, the one-record refusal, and the viewer's line index and reader (`scr_rv_*`), apart from `SCRRPT.C` so that a Windows program can link them | FMODEL MSG PRINT RPT RPTRUN CMEM CDOS, the grid of `SCRSHR.H` |
| `CLIENT/SCRCSV.C` | Export and import screen: the form's loop, the export's progress, the box of refused lines | TUI MSG RQ RQERR FORM CMEM CVIDEO SCRCFORM |
| `CLIENT/SCRCFORM.C` | The export and import form (`scr_csv_form`), its requests (`scr_csv_send`) and the reading of their answers (`scr_csv_rows`, `scr_csv_imp_line`, `scr_csv_ferr`), apart from `SCRCSV.C` so that a Windows program can link them | FMODEL RQ MSG RQERR's `rqe_text`, the box of `SCRSHR.H` |
| `CLIENT/SCRSET.C` | Settings screen, the printer row checked against the ranges of `RPT.H` and the types of `PRINT.H` | TUI MSG FORM CFG CVIDEO CDOS SCRLOGIN; the constants of PRINT, RPT and CPRN |
| `CLIENT/SCRPASS.C` | Change password, forced change after login | TUI MSG RQ SESS FORM |
| `CLIENT/SCRUSER.C` | User list, user form | TUI MSG RQ LISTV FORM SCRPICK SCRZFORM |
| `CLIENT/SCRADMIN.C` | Backup, restore, pack and reindex; the audit log; the server's sessions | TUI MSG RQ RQERR LISTV FORM CVIDEO SCRZFORM |
| `CLIENT/SCRZFORM.C` | The Admin menu's tables: the user form (`scr_user_def`) and list source (`scr_user_src`), the operations on the data files (`scr_adm_ops`), the audit filter and list source (`scr_audit_def`, `scr_audit_src`), the sessions' columns (`scr_sess_cols`), apart from the screens so that a Windows program can link them | FMODEL, RQ, PAGER's row types and `lv_col`, TUI's `TUI_TOP` |
| `CLIENT/INV.C` | Its own switches and their rules, `INV.CFG`, `INV.MSG`, opening the link, command dispatch over the menu table, the exit path; `/CMD` mode | CMEM CVIDEO CKEY CMOUSE CDOS CPRN CNETB CNBDRAIN MSG TUI KEYSCR CFG ARGS LINK SESS INVCOM.C INVMENU.C, and the entry points of every SCR module |
| `CLIENT/INVLOC.C`, `CLIENT/INVNOL.C` | What `INV.C` takes from the program it is linked into (`INVCONN.H`), data only: the usage lines of its connections and those it lacks, INV's with `/LOCAL`, the network-only client's (`THIN` of `SRCS.MK`) without | ARGS's kinds |
| `CLIENT/INVMENU.C` | The menu bar (`invmenu_bar`): the menus and their entries, each with its level and command, constant data that any client builds its menus from; no code | TUI's menu types, SCR's commands |
| `CLIENT/ARGS.C` | The switches every client takes (`args_parse`): the connection (`/LOCAL`, `/NET`, `/COM1`, `/COM2`) and `/CFG:`, the rules between them, every other word handed to the program's function; nothing printed; `ARGSW.C`, the same built with `ARGS_WINSW`, adds `/TCP:` and `/MBOX:` for the Windows client, so INV carries none of them | — |
| `CLIENT/INVCOM.C` | The serial switches (`invcom_*`): opening the SERIAL link of `/COM1` and `/COM2` and saying why it failed | LINK CFG; the constants of CUART |
| `CLIENT/LINK.C` | The link to the inventory system (the link itself in the private `LINKINT.H`): the LOCAL backend's dispatch through its `disp`, the 103 answer to an over-long line, and the judging of a remote session's end for NET, SERIAL, TCP and MBOX behind the private `LINKREM.H` | the protocol's tokenizer and line assembly |
| `CLIENT/LINKLOC.C` | The LOCAL backend: `link_open_local`, the engine and one protocol session on it, whose responses it writes for `LINK.C` | LINK, the engine and the protocol layer |
| `CLIENT/LINKNOL.C` | What a program built without the engine links in place of `LINKLOC.C`: a `link_open_local` that opens nothing and an `eng_close` with no engine to close | LINK |
| `CLIENT/LINKNET.C` | The NET backend: `link_open_net` over `NBCLI.C` | LINK NBCLI |
| `CLIENT/LINKSER.C` | The SERIAL backend: `link_open_serial` over `SERCLI.C`; BYE at `link_close` of a live session, and a last one, not waited for, when the line is lost | LINK SERCLI |
| `CLIENT/LINKTCP.C` | The TCP backend of the Windows client, not linked into INV: `link_open_tcp`, a connection to the bridge `INVTCP` (`DOC/SERVER.md`), each request one write from a buffer zeroed after it, whole responses in | LINK CWSOCK RESPRD |
| `CLIENT/LINKMBX.C` | The MBOX backend of the Windows client, not linked into INV: `link_open_mbox`, each request a request file of its own, logged in with a login ticket (MBOX, below) | LINK MBXCLI |
| `CLIENT/MBXCLI.C` | The mailbox client (`mbxcli_*`) of LINKMBX and INVTEST: request lines queued in a far block and posted as one request file, renamed into place; the response file waited for, read whole, zeroed first when it holds a ticket, and removed; one left from before zeroed and removed before a post | CMEM CDOS CZERO CTIMER RESPRD |
| `CLIENT/NBCLI.C` | The NetBIOS session to the server (`nbcli_*`): CALL, request lines out, whole responses in | CNETB CNBNAME CMEM CDOS CTIMER RESPRD |
| `CLIENT/SERCLI.C` | The serial session to the server (`sercli_*`): request lines into the transmit ring, whole responses out of the receive ring, every wait bounded | CUART CMEM CDOS CTIMER RESPRD |
| `CLIENT/RESPRD.C` | Whole responses read from a session's byte stream by the protocol's framing (`rr_*`), shared by NBCLI, SERCLI, LINKTCP and MBXCLI | — |

Uses names the modules whose functions or constants a file calls or takes. It leaves out what
every client module takes (the limits of `LINK.H`, the `KEY_` codes of `COMPAT.H`, the helpers
of `CTEXT.C`) and the settings (`scr_cfg`) and date (`scr_date`) that screens take from
`INV.C`. A screen's FORM takes in the model of `FMODEL.C` that `FORM.H` brings with it.

Interfaces: `COMPAT.H`, `CMOUSE.H`, `LINK.H`, `MSG.H`, `RQ.H`, `RQERR.H`, `TUI.H`, `PAGER.H`,
`PAGERINT.H` (the pager's page fill and load, for a survey), `PAGERSV.H`, `KEYSET.H`, `LISTV.H`, `FMODEL.H`, `FORM.H`
(FORM.C and MEMO.C), `MEMOIO.H`, `KEYSCR.H`, `CFG.H`, `ARGS.H`, `PRINT.H`, `RPT.H`, `RPTRUN.H`,
`SESS.H`, `SCR.H` (the screen entry points and the `SCMD_` commands, all `INV.C` sees of the
screens), `SCRSHR.H` (what the screens share with one another), `SCRIFORM.H`, `SCRLFORM.H`,
`SCRMFORM.H`, `SCRNFORM.H`, `SCRSFORM.H`, `SCRAFORM.H`, `SCRCFORM.H`, `SCRZFORM.H`,
`SCRRFORM.H`, `INVMENU.H`, `ITEMLS.H`. Each header describes its functions in full; this
document gives the design that ties them together. `LINK.H` repeats, as `LINK_` constants, the
protocol, engine and schema limits the screens need (line and response lengths, `PAGE=` and
cursor sizes, user, password, station and field widths), each checked against its source at
compile time in `LINK.C`.

## Program start and switches

```
INV [/LOCAL [dir] | /NET name | /COMn] [/BIOS] [/CFG:file]   full-screen client (the default)
INV /CMD [/LOCAL [dir] | /NET name | /COMn [/CFG:file]]      scripted mode
```
`/COMn` is `/COM1` or `/COM2`. `args_parse` (`ARGS.H`) reads the connection switches and
`/CFG:` for every client and judges the rules among them; INV reads the rest through the
function it hands it; `/TCP:` and `/MBOX:`, the Windows client's TCP and MBOX links, are no
switches of INV's, usage errors. `/LOCAL`, the default, runs the engine inside INV on a data
directory: `dir`, else (in the full-screen client) `DATA=` of the settings, else `DATA\`.
`/CFG:file` names the settings file (default `CFG_NAME` in the current directory, as dBASE reads
`CONFIG.DB`); scripted mode reads it only with `/COMn`, for `BAUD=` and `RTSCTS=`, and refuses
`/CFG:` otherwise. `/BIOS` (or `BIOS=1`) draws through INT 10h only. The test switches are left
out of the usage text, and all but `/NOW:` are refused with `/CMD`, as `/BIOS` is:

- `/KEYS:file`, a key script in place of the keyboard, and `/SNAP:file`, its snapshots (only
  with `/KEYS:`; `DOC/BUILD.md`, Golden screens);
- `/MDA`, act as a monochrome adapter on the host; a DOS build accepts it only on an MDA;
- `/NOW:YYYYMMDDHHMMSS`, the engine's clock, and the date the reports print and judge loans by
  (`scr_date`);
- `/PRN:file`, every print job appended to `file`, created empty at start-up, instead of going
  to the printer; a DOS device name such as `PRN` is refused.

The network-only client, `INVTHIN.EXE` (`THIN` of `SRCS.MK`), is INV built without the engine:
`INVNOL.C` and `LINKNOL.C` take the place of `INVLOC.C` and `LINKLOC.C`, so it links no
engine, dispatcher, response writer or DBF layer, and its `link_open_local` refuses
(`ENG_ENOTAVAIL`).

```
INVTHIN {/NET name | /COMn} [/BIOS] [/CFG:file]   full-screen client
INVTHIN /CMD {/NET name | /COMn [/CFG:file]}      scripted mode
```
It lacks `/LOCAL` (`inv_lacks` of `INVCONN.H`): a line naming no connection, `/LOCAL` and
`/NOW:`, which goes with `/LOCAL` alone, are usage errors, answered with its usage text, which
says that it has no `/LOCAL`; a bad `/NOW:` time gets its message first, as in INV. Its messages
begin with its own name. Everything else is as in INV.

### NET

`/NET name` opens a NetBIOS session to the server called `name` (`link_open_net`); the screens
and `/CMD` run over it as over LOCAL. It is refused with `/LOCAL`, with `/NOW:` (the server
keeps the clock), without a name, and with a name that `txt_nbname_ok` refuses: it must be 1 to
`NB_NAMELEN - 1` characters, none a control character, space or DEL, the first not `*`.

INV says on standard error that it is calling, then calls from the DOS machine name, or else
from a name of its own, `RINV` and four hex digits of the timer (`NB_NAMETRIES` tried while
taken; deleted again when INV ends). MS Network Client 3.0 sets no machine name, whatever the
computer name in its `SYSTEM.INI`, so there every client calls from a name of its own. A CALL
that times out or is aborted is retried a second later, `NBCLI_CALLTRIES` times in all
(`NBCLI.H` says why).
The server takes the calling name, not HELLO's, as the station, so `READONLY=` on the server
can name only machines that have a machine name (`DOC/SERVER.md`, Sessions). Every SEND and
RECEIVE is a no-wait command polled with the machine idling, the RECEIVE posted before a
request's SEND (`DOC/PROTOCOL.md`, Transports, says why); a SEND or a message of a response that
takes more than `LINK_NETWAIT` seconds, or a session that ends while a request waits, makes the
link lost (`LINK_ELOST`). The link judges the end of a session as the server does
(`DOC/PROTOCOL.md`, Identity and login): after `+OK` to `BYE`, the `LINK_MAXFAIL`-th failed
LOGIN and `-ERR 501` it hangs up and sends nothing more.

### Serial

`/COM1` and `/COM2` open a session over that serial port (`link_open_serial`) at `BAUD=` of the
settings (a rate of `UART_BAUDOK`, default `UART_DEFBAUD`), with RTS/CTS flow control when
`RTSCTS=1`. They are refused together, and with `/LOCAL`, `/NET` and `/NOW:`. Nothing is sent
before the first request, since the server's port opens a session at the first byte it receives;
the first requests are HELLO, whose station (`STATION=`, else the DOS machine name) the server
takes, and LOGIN. A response that brings no byte for `LINK_NETWAIT` seconds makes the link lost,
and so does a receive error the port counts while a response is due (an overrun or a framing
error; under Windows also a byte lost to a full receive queue), as the line has no checksum to
tell what it changed; nothing read after it is shown. Whatever came while no response was due
is dropped as noise before the next request, the answer to a previous client's last BYE
included. Noise the server's port counts while it listens opens no session; noise it counts
after HELLO's session has opened makes the server drop HELLO unanswered, so the session's start
fails only after `LINK_NETWAIT` seconds. A HELLO refused with 101 came garbled (noise that
arrived as valid bytes ahead of it, or a server port that opened while it was on the way) and is
sent once more (`sn_hello`). The link judges the session's end as on NET and then closes the port
without another word, since anything more would open a new session. A link closed while its
session is live sends BYE and reads its response; a lost link sends CR LF and BYE without
waiting, so no logged-in session is left on the line. A client that goes without either
(switched off, cable pulled) leaves its session to the server's hang-up detection, which needs a
full null-modem cable (`DOC/SERVER.md`, Serial lines).

With `/NET` or `/COMn` BREAK checking is off while the link is open (`dos_break_off`) and the
critical-error handler is installed (`dos_crit_install`), so neither Ctrl-C, Ctrl-Break nor a
critical error ends INV with the NET session's RECEIVE posted in memory DOS would free, or with
the port's interrupt live; in `/CMD` a Ctrl-Z typed at the keyboard ends the input. The handler
answers Fail, or Retry for an error DOS does not let fail (where Fail would be taken as Abort),
so such a call waits until its error clears.

`COMTEST.EXE` (`SRC/TOOLS/COMTEST.C`, the check in `PORTCHK.C`) tests a port before INV or the
server uses it, with nothing on the line: `COMTEST [/COM1 | /COM2] [/BAUD:n] [/RTSCTS]` sends
every byte value once through the chip's own loopback of COM1, or COM2, at `n` baud (a rate of
`UART_BAUDOK`; default `UART_DEFBAUD`), with RTS/CTS flow control under `/RTSCTS`. It writes to
standard output a line each for the port and chip, the I/O base and IRQ, the speed (with
`RTS/CTS`), the bytes that came back in order, and the verdict: `COMTEST PASS`, or `COMTEST
FAIL` with the reason (`COMn:` and `no such port`, `port in use` or `no memory`; `byte lost or
changed`). BREAK and critical errors are handled as for `/COMn`. It follows the convention of
the test tools (`DOC/BUILD.md`, Programs and scripts) and exits with 2 when the port cannot be
opened and 3 when a byte was lost or changed.

### MBOX

`INVWIN /MBOX:dir`, a link of the Windows client only, reaches the server through its mailbox in
`dir` (`link_open_mbox`; `DOC/SERVER.md`, The mailbox), its request files named for the station
of `sn_station`. That station must be a mailbox's, 1 to `MBXCLI_STALEN` letters, digits, `-` or
`_`, and `dir` at most `MBXCLI_MAXDIR` characters with its separator, else the open fails with
`LINK_EMBXARG` and touches no file. A mailbox session lasts one request file, so every request is
a file of its own, and the link logs in with a login ticket (`DOC/PROTOCOL.md`, Session):

- LOGIN is sent with `TICKET=NEW`; the ticket its `+OK` ends with is kept and cut from the line
  the caller gets. Every later file holds `HELLO`, `LOGIN user TICKET=t` and the request, and
  the caller gets the request's answer alone. PASSWD's `+OK TICKET=t2` replaces the ticket
  likewise. The response files that hold a ticket are zeroed before they are removed.
- BYE is sent with `TICKET=END`; a log-out (the client's second HELLO) is followed in its file by
  the ticket's LOGIN and `BYE TICKET=END`; and `link_close`, while a ticket is held, posts that
  file too and waits at most `LM_ENDWAIT` seconds (`LINKMBX.C`) for its answer, so that the
  station's next program does not take that answer for its own; one try, so after a failed post
  the server's ticket store expires the ticket.
- A ticket the server refuses (401 or 403: it expired, its user's password changed or the user
  was disabled) is dropped. BYE, PING and VER, which need no login, are posted again without it;
  any other request is answered `-ERR 400` by the link, as the server answers a command that
  needs a login, which takes the client back to its login (`RQERR.H`). A request, and any
  password in it, therefore goes in one request file only.
- The link holds the station, the ticket's user and the ticket, never the password: a request
  goes from the caller's buffer into the mailbox client's queue, which is zeroed once posted.

Each computer needs a station of its own: computers of one station share its files, their
responses crossed and each login replacing the other's ticket. INVWIN therefore refuses the link
before it opens when `sn_station` reports its fallback to `CFG_DEFSTATION`, there being no
`STATION=` and no machine name; a station given that name is not refused. Its message, shown
before any login, names the settings file and asks for a line `STATION=` of 1 to `MBXCLI_STALEN`
letters or digits, and OK ends the program. MS Network Client 3.0 gives DOS no machine name, so
its stations need `STATION=`.

No response file for `LINK_NETWAIT` seconds, a mailbox file that cannot be written or read, or a
response file that ends inside a response makes the link lost (`LINK_ELOST`). The link judges the
session's end as on NET, the count of failed LOGINs kept across the files.

### Start-up and exit

1. `cmem_init`, the first call of `main` in both modes, so the near heap has its room
   (the size its near-heap module gives, or under Watcom C 6.5 all of DGROUP) before any far
   block is taken;
2. the DOS version check and the switches;
3. `cfg_load` (a failure other than a missing file ends INV with status 8); in `/CMD` mode only
   with `/COMn`;
4. `dos_break_off` and `dos_crit_install` (in `/CMD` mode `dos_break_off` only with `/COMn`);
5. the link: `link_open_local`, which takes the engine's near memory before any far block
   exists, `link_open_net` or `link_open_serial`; their errors go to standard error;
6. `msg_load`, from the program's directory, then the current one; on failure `MSG_NOFILE` and
   the reason go to standard error and INV ends with status 7 (`/CMD` mode never loads it);
7. `ks_open` and `cstk_fill` with `/KEYS:`; `prn_capture` with `/PRN:`;
8. `vid_open` with `VID_F_BIOS` and `VID_F_SNOW` from the switches and settings, then
   `key_mouse`, whose reset of a mouse driver fits the pointer to the video mode set;
9. the palette: `TP_MONO` when `vid_mono` reports mode 7 on an MDA (`vid_adapter`), `TP_MONOCGA`
   for any other monochrome mode (mode 2, or mode 7 on an EGA or VGA, which `vid_mono` cannot
   tell apart) or with `MONO=1`, else `TP_COLOR`;
10. `tui_run`.

The exit path serves `tui_run` returning, the end of a key script and fatal errors (the
function given to `tui_set_exit`). It runs `scr_wipe` (overwriting any password being typed),
`link_close`, `ks_close` (which ends the snapshot file with the stack figure), `vid_close`,
`dos_crit_remove`, `dos_break_restore` and `msg_free`. While the NetBIOS provider still owns a
command the closed NET link had to leave with it (`nba_stuck`), it says so on standard error
and waits until the command ends or a key is pressed (`nba_drain`), as `/CMD` mode does after
its `link_close`. It then reports a failure to close the data files or of the key script or
snapshot file, and calls `exit`.

The texts INV writes to standard error are built into the program, since they are needed
before the message file is loaded, without it, or in `/CMD` mode, which does not use it;
everything the full-screen client shows on the screen comes from `INV.MSG`. Each begins with the
program's name and a colon (`inv_tag` of `INVCONN.H`: `INV: `, or `INVTHIN: `). Besides the
texts of the exit statuses below, INV says that it is "calling" the server, and "NetBIOS busy"
when the exit path waits for the provider.

| Status | Meaning (standard error) |
|---|---|
| 0 | normal end, also at the end of a key script |
| 1 | usage error: "bad or repeated argument", "/NET needs the server's name" or "bad time" (`/NOW:`), each followed by the usage text; "bad key script", "cannot capture to" a `/PRN:` file that cannot be created or is a device, "/MDA is not available in this version" (a DOS build on an adapter other than an MDA); with the usage text alone, a switch refused as described above |
| 2 (`SCR_EXIT_OPEN`) | "cannot open the data directory", with the reason, for a missing one "no such directory: make it (MD) or name another" (`link_open_local`); "cannot call" the server, with the reason ("no NetBIOS", "local name refused", "no answer", "no memory", or "refused": no LISTEN posted, as when all its sessions are taken) and the NetBIOS error code when a command failed; "cannot open COM1" or "COM2", with "no such port", "port in use" or "no memory"; a `HELLO` the server refuses (its reason in a message box first) |
| 3 | "requires MS-DOS 3.1 or later" |
| 4 | "cannot read standard input" or "cannot write standard output" (`/CMD` only); "cannot close the data files" (flush and close at the end); "key script failed" (the script could not be read or a snapshot written) |
| 5 (`SCR_EXIT_LOGIN`) | login refused for the third time ("Too many failed logins" in a message box first) |
| 6 | "cannot set up the screen" (no memory for the shadow) |
| 7 | `MSG_NOFILE` and the reason: `INV.MSG` missing, unreadable, malformed or made for another program version |
| 8 | "cannot read the settings file": it exists but cannot be read (sharing, access denied, a directory), its name is longer than `CFG_PATHMAX`, or its extension starts with `$$$`, which DOS takes for the scratch name of a save; a missing file means every default |
| 9 (`TUI_EXIT_LOST`) | the connection to the server, NET or serial, was lost (`LINK_ELOST`): the screen says "Connection to the server lost" and that a change being saved, if there was one, may have been made, to be checked before it is made again (the request may have been carried out with only its reply lost), and INV ends at the next key; `/CMD` writes "connection to the server lost" |

## The screen

| Rows | Use |
|---|---|
| `TUI_MENUROW` | menu bar: `Items  Locations  Search  Reports  Admin  Options` |
| `TUI_TOP`..`TUI_BOTTOM` | work area of the current screen (`TUI_WORKROWS` rows) |
| `TUI_HINTROW` | key hints of the current screen; a message replaces them until the next key, even after the screen that showed it has closed (a screen that cannot open, for want of memory or after a failed request, leaves its message for the screen below) |
| `TUI_STATROW` | status line: station at `TUI_ST_STA`, `LOCAL`, `NET` or `COM` at `TUI_ST_CONN`, user and level (`ADMIN 3`) at `TUI_ST_USER`, then the screen's own information (sort order, progress) at `TUI_ST_INFO` |

What a screen shows never depends on elapsed time (no clock, no messages that fade), and nothing
polls: keys are read when the program needs one. The one exception is the idle hook (below),
which watches the clock while `key_read` waits and puts up a box only when its `PING` finds the
session ended; a key script, whose keys never wait, does not reach it. The only look-ahead is
`tui_esc_waiting`, called between the requests of a long operation (a list fill or End walk, a
report run, an export) and once after a report run: it reads the keys typed meanwhile up to an
Esc, dropping those before it. The waits that exist (the pause after a failed login) issue INT
28h and leave the screen unchanged.

### Output path

`CVIDEO.C` keeps a shadow of the screen in a far block of `VID_CELLS` character and attribute
pairs. Drawing changes the shadow; a row is marked only when a cell really changes, with the
span from the first to the last changed column, and `vid_flush`, which `tui_key` calls before
every key is read, copies only those spans. A screen is thus drawn once per key however many
calls built it, the CGA sees the fewest writes, and a test's snapshot is the shadow itself.
`vid_open` marks every cell changed. In `/BIOS` mode the spans are written with INT 10h function
09h, one call per run of equal cells, and the flush then puts the cursor back (or parks it
again when hidden). `vid_verify` compares the shadow with the adapter's memory (or with what
INT 10h function 08h reads back), which the snapshots record, so a DOS run also proves the
writes reached the adapter. `tui_key` calls `vid_invalidate` after Ctrl-Break, since DOS may
have echoed `^C`.

Adapter detection reads the BIOS data area (mode byte at 0040:0049, CRT controller port at
0040:0063) and asks INT 10h function 12h whether an EGA BIOS is present; a VGA reports as
`VID_A_EGA`. Only on an EGA or later is the row count at 0040:0084 read, as older BIOSes leave
it undefined; the column count is the word at 0040:004A. Text mode 7 is at B000h, modes 2 and
3 at B800h; any other mode, row count or column count is replaced by mode 3 (mode 7 on a
monochrome adapter) and set back at exit, a 43- or 50-line or wider start screen coming back
as 80 × 25, as mode setting gives. The client uses display page 0, selecting it when another
page is active (0040:0062) and the old one again at exit. The DOS screen, its cursor and mode
come back at exit as they were; meanwhile its cells wait in video page 1 (B800:1000) when a
colour mode is written directly and page 1 is not the page to restore, and otherwise (mode 7,
whose adapters have one page, `/BIOS` mode, or page 1 active) in a far block.

### CGA snow

The IBM CGA shows white streaks ("snow") whenever its memory is accessed while a line is being
drawn. With snow avoidance on (`SNOW=1`, `VID_F_SNOW`, or the settings screen through
`vid_snow`), each word is stored straight after the start of a horizontal retrace (bit 0 of the
status port, CRT controller base + 6), with interrupts off for at most one scan line; during a
vertical retrace (bit 3) up to `VID_VBURST` words go without waiting. The loops are machine code
(`#pragma aux` under Watcom, `__emit__` under Turbo C), as compiled C is too slow to hit the
retrace; `CVIDEO.C` gives them in full. Each wait gives up after `VS_SPIN` polls, far more than a
working CGA needs; the remaining words are then copied without waiting and snow avoidance stays
off for the run, so an adapter whose status port never changes cannot hang the machine.

It applies only to `VID_A_CGA` in direct mode: EGA, VGA and MDA have no snow, and in `/BIOS`
mode the BIOS does its own waiting. It is off by default, since most CGA clones and every later
adapter do not need it. `/BIOS` remains the fallback for adapters whose memory is not where the
BIOS data area says.

### Attributes and the monochrome rule

Screens draw with roles (`TA_`); `tui_palette` selects one of three tables, used from the next
screen drawn:

| Role | `TP_COLOR` | `TP_MONO` | `TP_MONOCGA` | Use |
|---|---|---|---|---|
| `TA_NORMAL` | 17h | 07h | 07h | work area text |
| `TA_BOLD` | 1Fh | 0Fh | 0Fh | labels, emphasis, the selected row of a list not reading keys, the selected value of the field being edited |
| `TA_FRAME` | 1Bh | 07h | 07h | box frames |
| `TA_TITLE` | 1Eh | 0Fh | 0Fh | box titles, column headers |
| `TA_SELECT` | 30h | 70h | 70h | selected list row, open menu entry |
| `TA_MENU` | 70h | 0Fh | 0Fh | menu bar and allowed entries |
| `TA_MENUOFF` | 78h | 07h | 07h | entries above the user's level |
| `TA_MENUHOT` | 74h | 09h | 0Fh | hot-key letter |
| `TA_STATUS` | 70h | 70h | 70h | status line |
| `TA_INPUT` | 07h | 01h | 07h | input fields |
| `TA_INPUTON` | 70h | 70h | 70h | the field being edited |
| `TA_ERROR` | 4Fh | 70h | 70h | messages about errors |
| `TA_HINT` | 17h | 07h | 07h | key hints |
| `TA_HINTKEY` | 1Eh | 0Fh | 0Fh | key names in the hints |
| `TA_ALERT` | 1Ch | 0Fh | 0Fh | marked list rows (overdue loans) |

Colour attributes never set bit 7, which the CGA shows as blinking. `TP_MONO` (mode 7: MDA,
Hercules, an EGA on a monochrome monitor) uses only the attributes a monochrome display shows
distinctly: 07h normal, 0Fh bright, 70h reverse, 01h underline and 09h bright underline.
`TP_MONOCGA` is the same scheme for a colour adapter in mode 2 (a CGA on a composite or
monochrome monitor) or with `MONO=1`, where 01h and 09h would be blue: they become 07h and 0Fh.
Where colour dims an entry to dark grey, the monochrome tables show allowed entries bright and
dimmed ones normal, so the difference survives on every display.

Frames are the CP437 box characters (`TF_` in `TUI.H`): double for message boxes, questions,
prompts, help and the memo editor; single for screens, forms, menus, lists and pick lists.
`TF_MARK` marks the current choice in pick lists.

## Keys

`CKEY.C` reads INT 16h functions 00h and 01h (every PC BIOS has them; F11 and F12 are not
used), polling with 01h and issuing INT 28h while no key is waiting, so resident programs such
as `PRINT` keep running. ASCII keys are their code; extended keys are `KEY_EXT` plus the scan
code, named in `COMPAT.H`.

| Where | Keys |
|---|---|
| Everywhere | F1 help for the current topic, F9 the keys every screen has, also in the help box (both inside `tui_key`) |
| Lists and home | F10 or Alt-letter menu; Ctrl-F search (`SCMD_FIND`); F3 quit (asks); Esc back |
| Menus | Left/Right between menus, Up/Down entries, letter or Enter chooses, Esc or F10 closes |
| Record menu | the bar's last, Alt-C: what the screen shown does with its records, each entry naming its key when it has one (`tui_record`); a verb without a key is reached only there |
| Lists | Up, Down, PgUp, PgDn, Home, End; Enter open; Ins new; Del delete (asks); Ctrl-E edit; Ctrl-D duplicate (items); F5 fetch the rows again; Esc while a page fills or End walks stops it; item lists also Ctrl-L lend, Ctrl-R reserve, Ctrl-U use with, and in the Record menu Move (with its parts), Install in and Take out; a location's contents also Not installed only and Everything below; loans and holds Ctrl-PgDn and Ctrl-PgUp next and previous view, Ctrl-L, Ctrl-R, Ctrl-U a new row of that kind, and in the Record menu Return or release and Open item |
| Item form (view) | Ctrl-E edit; in the Record menu Parts, Service records, Loans and holds, Used with it: the item's tabs, and Split units (all also in edit mode while nothing has changed); Esc goes back to the item shown before a part chosen in the Parts tab |
| Pick lists | Up, Down, PgUp, PgDn, Home, End, a letter jumps to the next entry starting with it, Enter chooses, Esc cancels |
| Forms | Tab, Down next field; Shift-Tab, Up previous; PgUp/PgDn first/last; outside view mode these pass every field that cannot be changed (an ID, Parent, Created, Updated, a new record's Notes) but the Notes of a saved record, where a character typed says that F4 opens them; the value of a field entered shows selected (`TA_BOLD` in the field): the first character typed, Backspace or Del replaces it, an arrow, Home or End edits it in place; Enter or Ctrl-S save (nothing in view mode); Esc cancel (asks if changed); F4 pick list for code and choice fields, and on the Notes field opens the memo, in view mode too; Ctrl-E edit (view mode); Space steps a choice or logical field, a filter's choice back to empty after its last word |
| Line editor | characters, Left, Right, Home, End, Backspace, Del, Ins (insert/overwrite), Ctrl-Y clear; any other control key ends the edit, for the form (the login's password field ignores them) |
| Memo editor | arrows, Home/End, Ctrl-Home/Ctrl-End, PgUp/PgDn, Enter split, Backspace/Del join, Ins, Ctrl-Y delete line, Ctrl-S save, Esc or F3 leave (asks if changed) |
| Dialogs | Enter or Esc closes a message; Y or N answers a question (Esc is N) |

The global keys Ctrl-F, F3, F10 and Alt-letter work in lists, on the home screen, and in forms in
view mode or while nothing has changed; a form with changes keeps them and answers
`MT_SAVEFIRST` ("Save with Enter or leave with Esc first"), and the memo editor never takes them
(F3 there is its Esc), so a menu jump never leaves unsaved changes behind. Forms whose values are not a record (search,
report options, export and import, the audit filter, change password: `form_noask`) let Esc and
the global keys leave at once, never asking to discard. Ctrl-Break arrives as `KEY_BREAK` and
acts as Esc: `dos_break_off` turns DOS's BREAK checking off for the run and points INT 23h at a
handler that lets DOS carry on, so a break never ends the program with the screen as drawn;
`dos_break_restore` puts BREAK back at exit.

### Mouse

The mouse is optional: without a driver nothing changes, and every action has its key. With one
(`CMOUSE.C`, whose `CMOUSE.H` gives the codes), the right button is Esc and a press of the left
button is a click on a cell, `KEY_CLICK` + row × `KEY_CROW` + column, or `KEY_DCLICK` + the cell
for a second press on the same cell within `KEY_MS_DBLTICKS` ticks (about half a second), so a
double click comes as a click, then the double click. They arrive through `tui_key` like keys,
and each loop tests them against what it shows with `tui_hit`:

| Where | Click |
|---|---|
| Hint row | a word stands for its key, the name before `=` (`tui_keyname`, the names of the key scripts, any case, or a single character such as `Y`); of names joined by `/` (`Up/Down`) the one clicked, the first when the click is past the `=`; not while a message covers the hints |
| Menu bar | a menu's title opens it (`tui_global`, `tui_isglobal`), in an open menu too |
| Open menu | an entry chooses it (refused as by Enter above the user's level); a click anywhere else closes the menu |
| Lists, pick lists | a row selects it; the double click's second press there is Enter, but only after its first press selected that row in the same list |
| Forms | a field's label or value moves to that field, its value selected as by Tab, and in the field being edited drops the selection; outside view mode a field that cannot be changed takes no click; a click on the menu bar is a global key there, kept back as F10 is while the form has changes |
| Message box | any click closes it |

The second press of a double click does nothing anywhere else, so a double click on a menu entry
or a hint word acts once and never reaches the screen its first press opened. The pointer is the
driver's text cursor, shown only while `key_read` waits: the driver draws it into video memory
and puts back the cell beneath when it moves, which would undo a cell `vid_flush` wrote under
it.

## Texts: the message file

Every text the client shows that does not come from the server is a numbered message in
`INV.MSG`: menu labels, key hints, list headers, form labels, dialog titles, the client's own
messages and the key names of the test scripts. Tables in the program (menus, forms, list
sources) hold message numbers, so near data holds no UI text. `MSG.H` gives the file format
(a `=STAMP` line, `nnn text` lines, escapes and the `^1` `^2` placeholders), the loading and
the rules for callers:

- `msg_load` reads the file with one read into a far block and checks every line. The ranges
  below `MSG_PAGEMIN` (TUI, INV, LISTV, FORM, MEMO, which every screen uses) and the server's
  from `MSG_PAGEEND` on stay in far memory for the run, in a block that takes the first one's
  place (`cmem_replace`, `COMPAT.H`) and is filled from a second read of the file's start, up
  to the last line of those ranges, into a block freed at once. Under every compiler the block
  kept so starts where the first did and nothing `msg_load` freed lies under it (`T_MSGLO`),
  so the memory a program needs does not depend on which later blocks happen to fit a hole.
  Every range between is a page, read with one read the first time one of its texts is wanted,
  into the least recently used of `MSG_NSLOT` far slots. A screen thus costs one read per
  paged range it uses, a page never needs memory, and no file handle is held between reads. A
  page that cannot be read (the file gone or changed) answers "?nnn".
- `msg(n)` and `msg_sub` (which fills in `^1` and `^2`) return one of `MSG_NBUF` rotating near
  buffers, so up to `MSG_NBUF` results may be passed as the arguments of one call; a result is
  never kept across a call into TUI, LISTV, FORM, MEMO or a screen. Every function that takes
  a text copies it before it fetches a message or reads a key. `tui_topic` keeps only a
  pointer, so topics are string literals.
- The stamp, computed by `TEST/msgchk.py` from the file format and every message's number, name
  and placeholder count, is written into the built file and, as `MSG_STAMP`, into the generated
  header `MSGSTMP.H`; an `INV.MSG` from another version is refused at start, while a change of
  wording keeps the stamp.

Each module owns a range of numbers (`MSG_R_` in `MSG.H`, which names each range's module and
constant prefix `M<x>_`) and a source file `SRC/CLIENT/<module>.MSG` of `nnn NAME text` lines,
NAME being the message's constant. The build writes their messages, without the names, into
`INV.MSG`, and in the same run, with the same stamp, the server's `SRC/SERVER/INVSERV.MSG` into
`INVSERV.MSG`. `msgchk.py`, run by `make lint`, checks the constants against the sources, the
texts, that every key hint fits the hint row and that every paged range fits `MSG_PAGE`. Shared
messages (the texts of failed requests, Yes/No) are TUI's, with their `MT_` constants in
`RQERR.H` and `TUI.H`.

Hints are messages too, such as `MO_HINT`, `Enter=Contents 2Ins=New 2Ctrl-E=Edit 2Del=Delete
Esc=Back F1=Help`. Words hold no spaces; the part before `=` is the key, drawn
`TA_HINTKEY`; a leading digit 1–3 is the access level the key needs and is not shown, and when
the user's level is lower the word is drawn `TA_MENUOFF`. The hint row has `VID_COLS` cells, the
level digits taking none. This document quotes every hint as drawn: `Enter=Contents Ins=New
Ctrl-E=Edit Del=Delete Esc=Back F1=Help`.

## Requests and responses

`RQ.C` builds one request in an `rq` (`LINK_MAXLINE` bytes): command words, positional words,
then pairs. Values are quoted only when they must be (empty, space, TAB, `"`, `\`, `=`, control
bytes, DEL 7Fh), with the tokenizer's escapes; a part that would overflow is refused whole, and
`rq_trypair` lets a caller split a change over several requests. `rq_send` hands the line to
`link_send` and decodes each response line as it arrives: `+OK text`, `-ERR code text
FIELD=name`, `+DATA` with `K`/`F`/`R` lines (unstuffed, split, unquoted, unescaped) and `L`/`H`
lines (text after exactly one space, unescaped, never split) to a callback, `C cursor` and `K
ERROR=n` into the `rq_res`. A callback must not send another request. The line sent is wiped,
as it may hold a password.

A screen handles a failure in two steps: what it can fix itself (a field named by `FIELD=`, a
vanished record in a list), then `tui_fail` for the rest, which shows what `rqe_class` of
`RQERR.C` chooses. The texts are the client's own, chosen by the code; only 102 and 202, and the
refused lines of an import (`MX_REFLINE`: 102, 201, 202), show the server's reason, as the code
and field cannot tell those reasons apart, its first letter in upper case where it starts a
message:

| Code | Shown | Then |
|---|---|---|
| 104 | Not available in local mode (a server command: ADMIN CLIENTS) | stay |
| 200 | The record no longer exists; in a form, for a `FIELD=` it has (an item the host names), the server's text | list reloads; the form stays, cursor on the field |
| 201 | Already exists | cursor on the key field |
| 102 | the server's text, when no form field takes it | stay |
| 202 | the server's reason (has children, on loan, reserved or in use, equipment used with it, location holds items, unknown code) | stay |
| 203 | Permission denied (your access level is m) | stay |
| 204 | The list has changed; starting again at the top | list goes Home |
| 300–304 | Database error n: the client's text for n (`rqe_text`: a read or write error, an index is damaged, the disk is full, too many open files, a file in use; 301 adds: rebuild the indexes with Admin > Reindex, or offline with `DBPACK /R`) | stay |
| 401, 403 | Invalid user name or password / Account disabled | stay (the login screen handles its own) |
| 402 | Other sessions are open; this needs the system to itself (IMPORT, ADMIN PACK, REINDEX, BACKUP, RESTORE) | stay |
| 400, 404 | Session expired / Password change required | login screen (which runs the forced change itself after LOGIN answers `MUSTCHG=1`) |
| 501, closed link | The session has ended | quit |
| lost link (`RQ_ELOST`) | Connection to the server lost; a change being saved, if any, may have been made, to be checked first | end with status 9 |
| any other (100, 101, 103, 500, …) | Internal error n | stay |

A menu entry or key whose command needs a higher level than `tui_s.level` is drawn
`TA_MENUOFF` and refused with the 203 text by `tui_allow` (`rqe_deny`), without a request; the
server checks again regardless. A menu title is dimmed when none of its entries is allowed.

`tui_run` sets the idle hook `key_idle` of `CKEY.C`, which `key_read` calls while it waits for a
key, one tick read a call. When the tick count has gone back (midnight passed) it reads the DOS
date: DOS moves its date on only when it reads its clock, so INV left waiting over two midnights
would otherwise stamp loans and audit rows days late. A session that sends nothing for its
server's `IDLE=` minutes is ended by the server, which would lose a form or memo typed at length,
so on a remote link (NET, serial) the hook also sends `PING` every `TUI_PINGT` ticks, half the
least `IDLE=` of one minute: INV counts from its `PING`'s send, the server from its answer, so a
`PING` at the full minute would race the server's end. A `PING` due while a request is under way
(`rq_busy`: a printer's question inside a report's response) is dropped before it is built, as
`rq_send` would refuse it, so the deepest key waits never carry its frames; one that finds the
session ended is reported by `tui_fail` as any failed request, the hook removed first. A key
source never waits, so key scripts send no `PING`; `T_TUI` checks the hook.

## The list view

`LISTV.C` shows one paged command in a window of up to `LV_MAXPAGE` rows, drawing the rows of a
pager (`PAGER.C`), which fetches and keeps them and has no screen or key of its own. The pager
keeps two adjacent pages A and B (twice the window's rows of `LV_ROWBYTES`: `LV_KEYSZ` bytes of
key, `LV_ROWTEXT` of display text, which is `LV_TEXTLEN`, the screen's width, but in the
Windows client, and a byte for the source's row mark) in one far block, the
start cursors of A and B and the cursor after B in its near descriptor, and a ring of up to
`LV_MAXBACK` earlier page start cursors in a second far block, allocated when the first cursor
goes on it. The ring packs its cursors, each taking only its length: its first block,
`LV_RINGSZ` bytes, holds `LV_MAXBACK` of the cursors of lists in ID order, whose keys are one
or two IDs. Longer cursors (the location tree's, a list in maker order) that do not fit move
the ring to a block of `LV_RINGMAX`, room for `LV_MAXBACK` of any length; without the memory
for it the ring keeps the newest that fit, fewer pages. Down and Up scroll a row at a time
across the A/B boundary; once the window lies wholly in B, B becomes A and A's start cursor
goes on the ring. PgUp pops the ring and fetches the previous page into A; above the oldest
cursor the ring kept, it restarts at the first page, and so it does above A when there was no
memory for the ring, which then holds none until a later push finds the memory.

End walks forward to the last page asking only for the key field (the source's `walkfields`), so
each response stays short, calling the wait function before every request (stopping on the page
reached when it says so). The page it stops on is then fetched with the full field list, and
when it holds fewer rows than the window, the page before it too, so the window ends on the last
row, which is selected. Without memory for the ring a short last page is shown alone, its rows at
the window's top; a last page of none still shows the page before it, whose cursor the walk keeps
itself. A source without `walkfields` walks with full rows: `LOC LIST`, whose
rows are short; `USER LIST`, which takes no `FIELDS=`; `LOAN LIST` and `LOAN OVERDUE`, whose
columns are fixed.

In the Windows client and the host builds (`PAGER_ABOVE` of `PAGER.H`; the DOS programs leave it
out, as their memory floors have no room for it) the pager counts the rows of the list before A
(`above` of `pg`) from its own moves alone: 0 on the first page, grown as B becomes A, shrunk by
a page fetched above A, set by the End walk from the rows it passes. A page above A that no
longer ends where A starts (the list changed, or a stop had left it short) makes the count
`PG_NOABOVE` until the first page is loaded again; rows others add or delete before A are not
seen until then. There `PAGERSV.C` surveys a list and jumps into it: the survey walks from the
first page with key-only requests, as the End walk does, counting the rows and keeping each page's
start cursor in one far block, for up to `PSV_MAXPAGES` pages; a jump loads the kept page that
holds a given row, with the rows above it known, and puts the starts of the pages before it on
the ring, so moves up go on from there. Both work through the pager's page fill (`pg_fetch`), its
page load (`pg_load`) and its ring's push, which `PAGERINT.H` gives them, so that their requests
and pages are those of the moves. A survey given a key set (`KEYSET.H`) also collects the key of
every row it counts, through the pager's key sink, which formats each row of the walk as a kept
row is formatted; a stopped or failed survey leaves the set empty. A pager made unpaged
(`pg_unpaged`) serves a command that takes no `PAGE=` or `AFTER=` (`CAT LIST`): it sends the
command bare, cuts each page from the whole response at a cursor that is a decimal row offset, and
so reads the whole list again for every fill.

A page is filled with as many requests as it takes (`PAGE=` the rows still missing, at most
`LINK_PAGEMAX`, `AFTER=` the last `C` cursor), since a window may hold more rows than one request
returns and a request may stop early after its bounded work; it never asks for more rows than it
keeps, so stored cursors always follow the stored rows. Between those requests the pager calls its
wait function, and a failed request goes to its fail function, which can keep it on the list after
a bad cursor (204) to start again at the first page. The list view's wait function shows the
progress on the status line and reads the keys typed meanwhile with `tui_esc_waiting`: an Esc stops
the fill with the rows found so far (PgDn continues from there); other keys typed are dropped. Its
fail function is `tui_fail`. A source may have several sort orders, each with its own `FIELDS=`
list (or the command's default) and column header; the screen chooses one by name before the first
load (`lv_set_order`), as each Items > Browse by entry does. The source's format function
makes each row's text and key from the `R` values by field name (`lv_val`). The view reloads its
pages after an edit, delete or copy, keeping the selection's row number.

The selected row is drawn `TA_SELECT` while `lv_run` reads the keys and `TA_BOLD` otherwise: from
`lv_open`, after `lv_run` hands a key back to its screen and while a failed move's error shows. A
form, question, pick list or error box over the list then leaves no ends of the bar beside it,
only the row's visible text, bright. The help box, which `tui_key` opens inside `lv_run`, spans
the screen's width, so nothing of the rows it covers shows.

## Forms and the memo editor

A form is a table of `form_field` entries. Its model (`FMODEL.C`) holds the values in wire form,
each with its original, so a save sends only changed fields; it reads no label or position, so a
client may reach the fields by index and draw them as it likes. `FORM.C` draws the form in a box
and edits it; a form opened for editing, or turned to editing by Ctrl-E, starts on the first field
that can be changed rather than on a record ID. Local checks before sending:

- required fields;
- numbers: digits, sign, decimals, width;
- dates: `YYYY-MM-DD` on screen, a real calendar date, `YYYYMMDD` on the wire;
- choices: F4 on a choice field lists the field's own values, each followed by its name when
  the field names them (`form_choice_name`: the message `dec` places after the label); the
  screen's pick function serves code fields only;
- text fields take bytes 20h–FFh but 7Fh (DEL), password fields 21h–7Eh, but a password
  being set, one character wider than `LINK_PWMAX`, takes every byte, so that its check refuses
  a space or one character too many rather than keep what is left without them;
- memo fields hold their line count and are never sent.

The Windows client's form dialog (`SRC/WIN/WFORM.H`) runs the same model over the same tables; its
labels are INV's but where INV abbreviates for its grid: those are strings of the program's own
(`SRC/WIN/WLABEL.RC`, numbered `WF_S_LABEL` plus the message's number), such as Description for
Descr and Date acquired for Acquired. A form of more than one page has a tab strip
(`SRC/WIN/WTAB.H`): the item form's pages (`wrow_item_pages` of `SRC/WIN/WROWS.H`) are Overview,
Technical, Acquisition and Notes, under an identity row of its ID, maker and model; any other form
with a memo has Record and Notes; Ctrl+PgDn and Ctrl+PgUp (Ctrl+Tab and Ctrl+Shift+Tab too), a
click or a tab's letter choose a page, and a field that gets the focus shows its own. Each row of
a page is packed: labels as wide as their
text, a row's first control at one value column, each later field after the control before it, a row
too wide for the dialog showing its drop-down lists' codes alone, then carrying its last fields on to
a line of their own from the value column, and sharing the rest among its edit controls; a date,
number or choice field is never narrower than its widest value, measured in the dialog's font, in
every mode, nor cut. In view mode the values are plain text (Static controls), but a check box, a
password, the memo and free text that may be longer than its box, which stay read-only fields that
scroll; the focus starts on the tab strip, or on Close on a form of one page, and no label has an
access letter. Only the labels of fields that can be changed take access
letters, never a letter of the tabs or of the buttons, so no two controls share one. The buttons
are OK and Cancel in edit and new mode, Close and, for a
record, Edit in view mode, then Help; the item form has Parts, Service, Loans and Used with too.
There a choice field, and a category or location type field whose code list (`CAT LIST`,
`LTYPE LIST`, one request each as the dialog opens) loaded, is a drop-down list whose rows give each
code or word with its name (`form_choice_name` for a choice), so a letter typed selects the next
code starting with it; one that cannot be changed shows its row read-only. A tag or location field,
and a category or location type field whose list failed, has the pick list of `SRC/WIN/WPICK.H`,
opened by F4 in the field or by the button beside it, and a location field (`scr_locf` of
`SCRLFORM.H`) keeps its path field showing where it is, as INV's does. A date or number field that
cannot be changed shows its value as the lists do, in the Control Panel's formats, but a record's ID
(`FF_ID` of `FMODEL.H`); what is typed stays an ISO date and plain digits. The tag pick list, of a
field named TAGS or TAG, loads the tags in use with `ct_load_tags`, an Esc between its pages
stopping it (`wl_wait` of `WLIST.H`), is captioned "Tags in use (incomplete)" when the load was
stopped or more tags were in use than it holds, even before any tag came, else "No tags in use" or
"Tags in use", and adds the tag chosen to the field (`ct_tags_add`), clearing the message line, or
says "No room for another tag" there. In INV's form outside view mode no field that cannot be
changed takes a Tab stop but the Notes of a saved record, and in view mode no local read-only field; in the dialog only
those without a label of their own take none (a location's path, a code's name, an item's
held units, a loan's item and host). In the dialogs that ask for an item (a new loan or hold,
install) Item ID takes an ID or text, whose pick list (F4, the button, or OK on text) lists the
items holding it (`scr_search_item`, `wp_list`); in view mode Ctrl+E does what the Edit button
does, and the caption of a record in edit mode ends " - Edit".

Every dialog of INVWIN, this one included, has the grey 3D look (`wd_look` of `SRC/WIN/WDLG.H`):
labels, check boxes and buttons stand on grey when the display shows the default light grey button
face solid, and keep Windows' white otherwise (a monochrome EGA); every edit, drop-down and list
stays white, framed by a one-pixel shadow along its left and top and white along its right and
bottom.

Server field errors (102, 201, 202 with `FIELD=`) put the cursor on the named field and leave the
message to `tui_fail`: on the hint row the server's text for 102 and 202 and "Already exists"
(`MT_DUP`) for 201. Closing
a form overwrites its password values.

Saving a new record sends `<table> ADD`, which always carries the `FF_KEY` field (as its
positional word, for `LOC ADD code`) and every `FF_REQ` field, then other changed pairs while
they fit. Once the ADD answers `+OK ID=n` the record exists: the form becomes `FORM_EDIT` for ID
n and the ADD's fields are marked saved, whatever happens next. Pairs that did not fit follow in
`<table> SET <id>` requests, each atomic on the server and marked saved when it answers `+OK`;
after a failure the unsaved fields stay changed, so a repeated save sends only those, as a SET.
The screen then retitles the form for the new record ("Item n") and reports that some fields
were not saved, before the server's reason.

The memo editor (`MEMO.C`) edits notes in a `MEMO_COLS` × `MEMO_ROWS` window over a memo of
`MEMOIO.C`, which holds up to `MEMO_MAXLINES` lines in a far block and at most `LINK_MAXMEMO`
bytes as stored, and loads and saves it for every client. The editor works on a saved record
only: on a new one the Notes field says to save first. A request holds at most `LINK_MAXLINE`
bytes, and a save puts as many lines in each as fit, joined by `\n`: a memo that fits one is
sent as `<table> SET <id> NOTES="..."`, a longer one in parts the server stages and writes whole
(`DOC/PROTOCOL.md`, Memos saved in parts), `<table> NOTES <id> NEW="..."` with the leading
lines, `PART="..."` with more and `END="..."` with the last. Typed text needs escapes only for
`"` and `\`, so a line of `MEMO_COLS` characters always fits one request. Either way the save is
one change on the server, which writes the memo to new blocks of the append-only memo file
(it grows until PACK reclaims the old blocks) and one audit row for NOTES (value `MEMO`), and
keeps the memo it had until then. If a request fails the editor stays open, says what became
of the notes (`mio_failmsg` of `MEMOIO.H`: how many lines arrived and that the stored notes
are unchanged; that they may have been saved when the link was lost with the request that
writes them; that the server is too old to save notes this long, in place of its refusal of
`NEW=`) and then the failure; the next save starts again. The longest
memo needs about `LINK_MAXMEMO / LINK_MAXLINE` + 1 requests.
Hints: `Ctrl-S=Save Ins=Insert Ctrl-Y=Delete-line Esc=Back F1=Help`, and `Esc=Close F1=Help` when
read-only.

A memo is loaded with `<table> NOTES <id>`; TABs are expanded to `MEMO_TAB` columns, other
control bytes shown as `?`, and lines longer than `MEMO_COLS` split, all announced when the
editor opens and written back only if the user saves. A memo the server truncated (`K
TRUNCATED=1`), one of more than `MEMO_MAXLINES` lines, or one that would exceed `LINK_MAXMEMO`
once expanded and split opens read-only, so saving can never shorten it on the server. Level 1
users get the editor read-only.

## Screens

Commands (`SCMD_` in `SCR.H`) come from the menu (`INVMENU.H`); `INV.C` maps each to its screen.
Every screen sets its help topic and its hints.

| Menu | Entries (level) |
|---|---|
| Items | Browse by ID (1), Browse by location (1), Browse by maker (1), Browse by serial (1), New item (2), Assemblies (1), Service (1), Loans and holds (1), Categories (1) |
| Locations | Browse locations (1), New location (2), Location types (1) |
| Search | Find text Ctrl-F (1), Find serial number (1), Filter items (1) |
| Reports | Item listing (1), Valuation (1), Loans and holds (1), Item labels (1), Bin labels (1) |
| Admin | Users (3), Backup (3), Restore (3), Pack (3), Reindex (3), Audit log (3), Sessions (3), Export CSV (3), Import CSV (3) |
| Options | Settings (1), Change password (1), Log out (1), Quit F3 (1) |

INVWIN builds the same menus from the same table, with an entry and three menus of its own: Items
takes Browse by category (1) after Browse by serial, the categories with the selected one's items
beside them, checked while they show; File first, holding the table's Log out and Quit, the latter
as Exit naming Alt+F4, the frame's Close (F10 being Windows' key of the menu bar); Record, the
commands of the list shown, each beside its key when it has one (Open Enter, Edit Ctrl+E, New Ins,
Delete Del, both only without Shift and Ctrl, whose Ctrl+Ins, Shift+Ins and Shift+Del are Windows'
clipboard keys and do nothing in a list, the Categories dialog's included, Select all Ctrl+/ on a list that takes several rows, Move, Duplicate Ctrl+D, the
screen's own commands, Lend, Reserve and Use with (Ctrl+L, Ctrl+R, Ctrl+U), an item's parts, service
records, loans and holds and what is used with it, a row's item, next list (Ctrl+PgDn), next order,
a location's items not installed only and everything below, next pane (F6), back (Esc)), which a
right click on a list row, or Shift+F10, also opens as a popup menu over the row; View, which holds
Pane beside the list, checked while a split view shows, and Refresh (F5); and Help, which holds Help
on this screen, Getting started, Keyboard and About
(`SRC/WIN/INVWIN.H`). An entry whose command always opens a dialog box ends in "...", the Browse
entry of the item list's order is checked while the item list shows, and each entry has a one-line
help text in the string table (`IW_H_MENU`), shown in the status bar while the entry is highlighted.
Under the menus a tool bar (`SRC/WIN/WTOOL.H`) gives New, Open, Edit, Delete, Move, Install and
Lend, each the command of its Record entry and grey where that entry is, and a Find box: Ctrl+F
reaches it, and Enter or its Find button lists the items holding its text (`ITEM FIND`), as the
search dialog's Text alone does, or, empty, opens that dialog; Esc or Tab goes back to the list. The
status bar (`SRC/WIN/WSTAT.H`) has four panels: the connection, the user and level, the count of the
list with the focus and a help panel, which holds the menu help, a note of what a command changed
until the next key or click, or a long request's progress. The lists of the screens of items, an
item's parts and the loans and holds choose several rows, as File Manager's do: Shift+click and
Shift with Up and Down extend the selection, Ctrl+click toggles a row, and the count panel says
"7 selected"; a selection lives in the rows shown, so a move that fetches another page drops it,
but Shift with Up or Down at the edge of the rows shown while several are chosen beeps and keeps
the selection and the focus row, rather than drop a batch the gesture was extending.
Shift+F8 turns on an add mode for rows far apart, as in File Manager: the arrows move the focus
rectangle and keep the selection, Space toggles the focus row, Shift with an arrow or a click adds
the rows from the anchor, and Shift+F8 again or a plain click ends it; meanwhile the count panel
says "Shift+F8 add mode: 3 selected". Ctrl+/, or Select all of the Record menu, chooses every row
of the list on all its pages, as File Manager's Select All does, and Ctrl+\ leaves the focus row
alone chosen: the list is surveyed then for its rows' keys, counted on the help panel
("Collecting 250...") and stopped by Esc, the keys are kept in far memory (`KEYSET.H`), and the
count panel says "All 260 selected" while PgUp, PgDn, Home, End and the scroll bar page through
them; an arrow key, a click, another order or the list fetched again leaves the focus row alone
chosen. A list of more than `KS_MAXROWS` rows is refused in a message box; after that, an Esc or
a failed request, the rows chosen before stay chosen. As in File Manager, the two keys are those
where / and \ sit on a US keyboard (virtual keys BFh and DCh), wherever another layout puts the
characters; on such a keyboard Select all of the Record menu serves. A reader may choose all rows
too, as choosing changes nothing.
Move, Install in and Take out (the Record menu), Return or release, Delete, and a new loan, reservation
or use (Ctrl+L, Ctrl+R, Ctrl+U) act on every row chosen: one dialog asks once and sends the first
row's request, the others follow in one busy spell counted on the help panel ("Moving 3 of 7..."),
the first refusal or an Esc stops them with a box saying how many were done, and the list is fetched
again once; with every row chosen they act on the rows as surveyed, Delete asking "Delete all 260
items in this list, including those not shown?"; Open, Edit and the other commands act on one row
alone, from a list the focus row, the one with the focus rectangle (`SRC/WIN/WLIST.H`, Several
rows), and from a preview panel the row it follows. On its screens of items Ctrl+L, Ctrl+R and
Ctrl+U open
a new loan, reservation or use of the selected item in its form, and in the item form its record
buttons open its parts, service records, loans and holds, and what is used with it, as the Record
menu's Parts, Service records, Loans and holds and Used with it do from the item lists; on its
loans and holds Ctrl+PgDn and Ctrl+PgUp step the six views as INV's do and the Record menu's Open
item opens the row's item. On its screens of items the Record menu's Split units splits units off
the selected item (`ITEM SPLIT id QTY=n`, level 2); its location tree (`SRC/WIN/WLOC.H`) has INV's
keys, Ins adding inside the selected location and the Record menu's Move moving it; on a
location's contents the Record menu's Not installed only and Everything below switch `TOP=1` and
`ALL=1` of `LOC CONTENTS`; Locations is a split view, as File Manager's, the tree
on the left and the selected location's contents on the right, which follow the tree's selection
once it has stayed on a location for `IW_SETTLE` ms, so that arrowing down the tree sends no request
per row; F6 or Tab, with Shift too, goes from the tree to the contents and back, as Esc does from
the contents, the Record menu, the tool bar and the
count on the status bar ("3 items in Office") following the list with the focus, while Enter on the
tree still opens the contents as a screen of their own; the splitter between them
(`SRC/WIN/WSPLIT.H`) is dragged with the mouse, and the tree's width is kept as `WINSPLIT=` of the
settings, whose `WINPANE.LOC=0` shows the tree alone, as it shows without that line on a serial line
or a mailbox, where each load of the contents holds the program (with `WINPANE.LOC=1` they show
there too, and follow after `IW_SETTLE_SLOW` ms), and an item of the contents dragged onto a tree
row with the mouse moves there as the Record menu's Move
moves it, from level 2 (`WL_DRAGS` of `SRC/WIN/WLIST.H`), several selected items with it when
the drag starts on one of them; the assemblies are a split view of two lists too, the selected
assembly's parts on the right with the keys and commands of the parts' own screen (Install part
installs in that assembly), and there a command in either list fetches both again, as a part
taken out may leave no assembly, the assemblies keeping the one whose parts show selected, or,
once it is gone, taking the focus; the loans and holds likewise show, beside each of their
views, the history of the selected row's item, all its loans and holds, where Return or release
and a new loan, reservation or use act on that item; Items > Browse by category lists the categories
(`CAT LIST`, which its list pages itself, `pg_unpaged`), code, name and sort order, and beside them
the selected category's items (`ITEM LIST CAT=`), an item list with its orders, keys and commands
whose new item takes the category and which an item given another category leaves; Enter on a
category opens its items as a screen of their own, which Esc leaves for the categories with that one
selected, and Ctrl+E, Ins and Del there are the Categories dialog's Edit, New and Delete, with its
form, its question and its rules (`wcode_form` and `wcode_del` of `SRC/WIN/WCODE.H`), the category
saved selected again where its sort order puts it; the users, at level 3, show beside them the
audit log's entries that the selected user made (`AUDIT LIST USER=`), newest first and to read
only; the item list and a search's results are split
views too, a read-only preview of the selected item on the right (`SRC/WIN/WPREV.H`: its ID, maker
and model, the fields of the item form's Overview page, the location with its path as the form
shows it, and its notes, read with `ITEM GET`, `LOC GET` for an item that has a location and
`ITEM NOTES`), which follows the list as the contents follow the tree, in add mode the one
selected row wherever the focus row is, is read again after a command that fetched the list
again, and takes the focus with F6 or Tab: there Up and Down scroll it, Enter opens the item, F5
reads it again and the list's record keys and Record menu, which a right click or Shift+F10 opens
there as a popup, act on the item shown; View > Pane beside the list hides or shows the pane of
any split view and keeps the choice as its `WINPANE.` of the settings, as a drag of its splitter
keeps the list's width there; Categories and Location types are
dialogs of their own (`SRC/WIN/WCODE.H`); and a record's caption names the record as its delete
question does. Its lists page the shared sources' commands,
orders and field lists, but make rows of their own (`SRC/WIN/WROWS.H`): each field at its full
width, up to `LV_ROWTEXT` cells, an item's category as its code and the name of the `CAT LIST` the
frame holds while a list of items or parts shows, each column at a default width in average
characters that fits every header word and fixed-format value, the item columns up to the location
within a 640-pixel screen, the header's words from `WROWS.RC`. Dates are shown in the Control
Panel's short date and quantities, costs and capacities with its separators, read once at start from
`WIN.INI`'s `[intl]` (`wrow_intl` of `WROWS.H`, `INTL.H`); any other short date picture than
`INTL.H` supports is shown as ISO. A list wider than its window scrolls sideways with its horizontal
bar or Left and Right (`WLIST.H`). Its header is a row of headings drawn as buttons (`wcol_heads` of
`SRC/WIN/WCOLS.H`): on the item list ID, Maker, Location and Serial number name its orders, the one
in use with an arrow, and a click switches to that order, as the Record menu's Next order takes
the next; every other heading is flat.
In the frame's lists the divider at the right of each heading can be dragged, an inverted guide
following the mouse, Esc cancelling; the width goes to every list of those columns (the item list, a
location's contents, a search's results and the assemblies share theirs) and is kept as
`WINCOLS.<table>=` of the settings. On a LOCAL, NET or TCP link a list counts its rows as it opens
and when its order changes (`WL_EXACT` of `SRC/WIN/WLIST.H`), and the status bar shows that count in
the noun of the screen ("345 items", "12 locations", "1 user"), which follows the records INVWIN
itself adds, copies, splits off and deletes, and is carried over when the list is opened again for a
new height of the frame. A list that shows some of its records only (`WS_FILTER` of `WSCR.H`: a
search's results, a location's contents, the assemblies, an item's parts, the loan views, what is
reserved for or used with an item) is counted again after every change INVWIN makes to a record, as
is any list after an import or a restore.

The sketches are copied from the golden snapshots of the screen tests (`TEST/GOLDEN/SCR/`,
named with each sketch), with frames drawn as `+ - |` and the pick-list mark `TF_MARK` as `>`.
Blank rows, some list rows, and the menu bar, status line and surrounding screen where they tell
nothing are left out. Their data is the tests' seed data, their station `TEST`.

### Login and home (`SCRLOGIN.C`)

```
          +------------------------- RETROINV 1.0 --------------------------+
          |                                                                 |
          |   Enter user name:                                              |
          |   Enter password:                                               |
          |                                                                 |
          |   Data: TDATA                                                   |
          +-----------------------------------------------------------------+
 Enter=Next Esc=Quit F1=Help
 TEST              LOCAL
```

(`LOGIN.SNC`, snapshot `LOGIN`.) The title is the `VER` response, `PROTO_VERSION`; `Data:` names
the data directory, or the server with `/NET`. Requests: `HELLO <station>` (the station INV
chooses with `sn_station`: on NET the name the link called from, `link_netname`, else
`STATION=`, else `dos_machine_name`, else `CFG_DEFSTATION`), `VER` (once per run), `LOGIN <user>
<password>`, each sent by `SESS.C`, which also judges `LOGIN`'s outcome. The user name is
upper-cased as typed (`TI_UPPER`, at most `LINK_USERLEN`), offered from `USER=`; the password is
typed without echo (`TI_NOECHO | TI_PRINT`, at most `LINK_PWMAX`) and wiped as soon as the
request holds it (`sn_login`). `+OK LEVEL=n NAME="..."` fills `tui_s` and goes home. After `+OK
LEVEL=n MUSTCHG=1` the screen calls `scr_passwd(SCMD_MUSTCHG)` itself; the full name stays empty
until the next login, and a log-out from that change comes back here with the user name kept and
the cursor on the password. 401 and 403 show their message, wait `TMR_SEC` ticks (as dBASE III
Plus PROTECT paused after a bad password) and ask again; when the response closes the link (the
third failure on the session) the client shows "Too many failed logins" and ends through
`tui_fatal(..., SCR_EXIT_LOGIN)`. 300-range errors are shown and the screen stays, a message on the
hint row until the next key. A failure that asks for the login (400: the server ended the session
for its `IDLE=` or was restarted, which a serial line's next request finds) brings the screen back
with a new `HELLO`, the user name kept and the cursor on the password (`SCRR60.SNC`). A link lost
before or during the `HELLO` or the `LOGIN` is called again once on NET (`scr_reopen` of `INV.C`),
its station named again for the new call on the status line, "Connection lost and made again:
type the password again" (`SCRR61.SNC`); on a serial link INV ends with `TUI_EXIT_LOST`
(`SCRR65.SNC`). A `HELLO` the server refuses (a station it does not take) is shown in a box and
ends INV with `SCR_EXIT_OPEN` (`SCRR62.SNC`); one refused with 101 also when sent again, garbled
twice, says so instead: "The line to the server garbles what it carries. Check the cable and that
both ends use the same speed, then start again.". Esc or F3 quits. Log out (Options) sends `HELLO` again,
which ends the login on the session, and returns here.

```
 Items  Locations  Search  Reports  Admin  Options
          +------------------------ RETROINV ------------------------+
          |                                                          |
          |   User              System Administrator                 |
          |   Access level      3 (administrator)                    |
          |   Today             2026-10-08                           |
          |                                                          |
          |   Item records      3                                    |
          |   Location records  3                                    |
          |   Category records  10                                   |
          |                                                          |
          |   Press F10 or Alt-letter for the menus, F1 for help.    |
          |                                                          |
          |                                                          |
          +----------------------------------------------------------+
 F10=Menu Ctrl-F=Search F3=Quit F9=Keys F1=Help
 TEST              LOCAL  ADMIN 3
```

(`LOGIN.SNC`, snapshot `HOME`.) The home screen shows the user's full name and level, today's
date (`scr_date`) and the record counts from `STATUS` (`K ITEMS=`, `K LOCS=`, `K CATS=`), which
count deleted records too until a pack, hence "records". Below
them, with no item yet, how to add the first (`FIRST.SNC`); and a date before `HM_MINYEAR`, the
date of a PC whose clock was never set, is warned of in `TA_ERROR`, as loans, holds and service
records take today's date (`SCRR29.SNC`, snapshot `OLDDATE`).

### Items (`SCRITEM.C`)

```
 Items  Locations  Search  Reports  Admin  Options  Record
+-------------------------------- Items by ID ---------------------------------+
|      ID Cat  Maker           Model           Works   Qty Location   Status   |
|       1 SYS  IBM             5150            yes       1 Office              |
|       2 DRV  Tandon          TM100-2         no        2 Box B2              |
|       3 CARD Hercules        Graphics Card   ?         1 Shelf A1            |
|                                                                              |
+------------------------------------------------------------------------------+
 Enter=Open Ins=New Ctrl-E=Edit Del=Delete Ctrl-D=Duplicate Esc=Back F1=Help
 TEST              LOCAL  ADMIN 3     Order: ID
```

(`ITEMS.SNC`, snapshot `ITEMLIST`.) List: `ITEM LIST [ORDER=ID|MFR|SERIAL] [FIELDS=...]
PAGE=n [AFTER=c]`, or for the location order `ITEM LIST LOC=0 ALL=1` (the tree's order: the
items of no location, then those of each location depth first; `LV_TREEORDER` of `PAGER.H`),
showing ID, category, maker, model, works state, quantity, the location's name (the names
ending in `~`, `MSG_CUTMARK`, when cut) and why the item is not available, from the default
fields for ID, LOC and MFR, and `ITEM_ID,MFR,MODEL,SERIALNO,LOC_NAME` for SERIAL. The Works
column words the `WORKS` letter: yes, no, intrm (intermittently) or ? (not tested), `MT_WK_Q` of
`TUI.MSG` and the three after it, texts kept with those every screen has at hand, so a row reads
no page of texts for them; a letter dBASE left that is none of these shows as it is. The last
column words the `AVAIL` letter the server computes: blank for `Y`, else On loan, Reserved or In
use (`L`, `R`, `U`: every unit held), Installed (`I`), Disposed (`D`) or None left (`N`, no
units), the names `MH_K_NAME` of `SCRNFORM.MSG` and the five after it; the source and the
columns are `ITEMLS.C`'s, which the other clients share. Enter opens the item form in view mode,
Ctrl-E in edit mode, Ins a new item; Del asks "Delete item 2 Tandon TM100-2?", the ID, maker and
model taken from the row as shown (`il_delq`), and sends `ITEM DEL id` (202: has installed
parts, units lent, reserved or in use, or equipment used with it), the list being fetched again
only after a delete or a 200; Ctrl-D sends `ITEM COPY id` and opens the copy in edit mode. A copy
left with Esc before any save was sent (a save the server refused counts: its first request may
have changed the copy, as INVWIN also judges), and before its notes were opened for editing (the
memo editor saves them itself) or any of its tabs or a split, is deleted again (`ITEM DEL`), so
a copy begun by mistake leaves nothing; otherwise it is kept, and the list says "Copied as item
n". Ctrl-L, Ctrl-R and Ctrl-U open the Lend, Reserve or Use with form for the selected item
(`scr_loan_add`). All but Enter need level 2. Every item list has the hint row shown; the status
line names the order, which Items > Browse by changes.

Move, Install in and Take out of the Record menu act on the selected item through `SCRASSY.C`: Move
asks for a location (location pick list, opened on the item's own from one `ITEM GET`) and sends
`ASSY MOVE id LOC=n` with its LOC_ID, moving the item and all its parts (an installed item gives 202
"installed in an assembly"); choosing where the item already is sends nothing. Install in asks for
the item to install it in (`scr_ask_item`, below) and sends `ASSY INSTALL id parent`; Take out asks
for the location to put it in, the list opened on the assembly's (where its parts lie), and sends
`ASSY REMOVE id LOC=n`. A 200 to the `ITEM GET` means the item has gone: the list comes again. Each
reloads the list when it succeeds; 202 shows the server's reason ("Would contain itself", "Nested
too deeply", "Too many parts", "Item is on loan", "Item is reserved", "Not part of an assembly").

**The item prompt and pick list.** Where an item is asked for (Install in, the Parts tab's Ins,
a new loan or hold from Items > Loans and holds), `scr_ask_item` opens a prompt, "Item ID, or
text to find it", of up to `LINK_MAXFIND` characters, and `scr_find_item` looks up what was
typed, as it does for the host field of a reservation or use, its blanks at both ends left out.
One to `LINK_IDLEN` digits are the item's ID when `ITEM GET` finds it (written without leading
zeros, `scr_search_id`); anything else, digits naming no item and 0, which the server refuses as
an ID, included (a model or serial number such as 5150), opens the item pick list
(`scr_pick_item`): the items `scr_search_item` of `SCRSFORM.C` finds, those holding the text
(`ITEM FIND`), or every item when nothing was typed (`ITEM LIST`), for a new loan or hold only
the available ones (`AVAIL=1`), for a host only the active ones (`STATUS=A`; `SCR_SI_` says
which), with the item list's rows in a box across the work area, titled "Choose an item"
("Choose an available item"), help topic `PICKITEM`. Enter chooses the selected item; Esc goes
back to the prompt with the text kept, an empty list saying "No items found" ("None available").
A refused `ITEM GET` (a 203, say) asks again, its text left on the hint row until the next key,
as `tui_prompt` keeps a message still up. The box puts back what it covered, the hints and the
help topic. The Windows client builds its pick list from the same search and source
(`scr_search_src`).

```
+-------------------------- Choose an available item --------------------------+
|      ID Cat  Maker           Model           Works   Qty Location   Status   |
|       2 DRV  Tandon          TM100-2         no        2 Box B2              |
|                                                                              |
+------------------------------------------------------------------------------+
 Enter=Choose Esc=Cancel F1=Help
```

(`PICK1.SNC`, snapshot `PICKLEND`; `PICK2.SNC` installs through it, `PICK3.SNC` takes digits and
0 as text, `DEEPITEM.SNC` opens it from a Parts tab, `DEEPHOLD.SNC` from a host field.)

**The tag pick list.** F4 on a tags field (the item form's Tags, the search's and the reports'
Tags filter) loads the tags in use into a code list (`SCR_CK_TAG`, `CODETAB.H`'s
`ct_load_tags`): `ITEM TAGS`, then `ITEM TAGS AFTER=` the cursor of each page until one comes
without, each page the distinct tags of up to `ENG_MAXREADS` items, A-Z with the case of letters
ignored. The pages are merged in that order, a tag once in the spelling that came first, at most
`LINK_MAXCODES` of them: past that the first `LINK_MAXCODES` A-Z stay. Between two pages the
status line says "Reading tags... found: n" and Esc stops the load (`tui_esc_waiting`). The tags
are shown by `tui_pick`, titled "Tags in use", or "Tags in use (incomplete)" when the load was
stopped or there were more; "No tags in use" on the hint row when there are none. Enter appends
the tag chosen to the field (`scr_pick_tag` of `SCRPICK.C`). The code list's far block holds
`LINK_MAXCODES` rows only while the pick list is open.

```
+----------------------------------- Item 1 -----------------------------------+
| ID        1     +-------- Tags in use (incomplete) --------+                 |
| Type            |>deep-p01-aaa                             |                 |
| Maker     IBM   | deep-p01-bbb                             |                 |
| Model     5150  | deep-p02-aaa                             |                 |
```

(`TAGPICK.SNC`, snapshot `STOPPED`: Esc typed while the first page loaded; `FULL` and `FULLEND`
the first `LINK_MAXCODES` of more tags, the second page's sorting first; `SRCHPICK` from the
search; `TAGFORM.SNC` the list of a few tags, a tag appended and one with no room; `DEEPTAG.SNC`
the list on the deepest path to an item form, for the memory figure.)

```
+----------------------------------- Item 1 -----------------------------------+
| ID        1             Category  MOBO          Status    A                  |
| Type                                            Condition EXC                |
| Maker     Hercules                                                           |
| Model     M07-B                                                              |
| Serial    SN-0037                                                            |
| Descr     Seeded item 1                                                      |
| Works     Y             Quantity  2      Available 2                         |
| Location  A2            Parent                                               |
|           Garage > Shelf A, bottom                                           |
| Acquired  1987-02-02    Source                                               |
| Price     3.50          Value                                                |
| Tags                                                                         |
| Created   2026-10-08    Updated   2026-10-08                                 |
| Notes     Lines: 0 (F4 to open)                                              |
+------------------------------------------------------------------------------+
+--------------------------  Parts  Service  Loans  ---------------------------+
| The Record menu (Alt-C) shows the parts installed in this item, its          |
| service records, its loans and holds, and the items used with it.            |
|                                                                              |
+------------------------------------------------------------------------------+
 Ctrl-E=Edit Esc=Back F1=Help
 TEST              LOCAL  ADMIN 3
```

(`SCRE24.SNC`, snapshot `ITEM1`.) `ITEM GET id` loads the form (K lines, through
`scr_item_kline`); `memo_load_count` gives the Notes field its line count. Fields:

- after the quantity, a line without a label that the Tab key passes over (HELD, local): the
  units on loan, reserved and in use, each when not 0 ("On loan 1  Reserved 2"), then for an
  active top-level item "Available n", the units left of the quantity, when there are any, then
  "n used with it" when other items' open reservations or uses name this one as their host
  (`ATTACHED`); a line longer than `SCR_HELDW` ends in `~`; `scr_item_loaded` words it from the
  four counts `scr_item_kline` kept (`SCRE72.SNC`, snapshot `ITEM5`);
- ITEM_ID, PARENT_ID, CREATED, UPDATED read-only (PARENT_ID 0, a top-level item, shown
  blank; for an installed item the Parent field shows the parent's ID, maker and model, from the
  parent's row of `ASSY PATH id` (`scr_item_parent` of `SCRIFORM.C`, which INVWIN shares), sent
  after `ITEM GET`, so a view or edit of an installed item takes one request more; when it fails
  the failure is reported and the item shown with the bare parent ID);
- CATEGORY a code, required (F4: the category pick list, from one `CAT LIST` of at most
  `LINK_MAXCODES` rows kept in a far block (`CODETAB.H`), shown by the scrolling `tui_pick`);
- STATUS choice A D, required; COND, the cosmetic grade alone, choice NEW EXC GD FR PR or
  blank (not graded); WORKS choice ? Y I N, required; their pick lists name each code (Active,
  Disposed of; New, Excellent, Good, Fair, Poor; Not tested, Yes, Intermittently, No:
  `SCRSRCH.MSG`'s `MS_C_` texts, which the search form's Status and Works share;
  `CHOICES.SNC`); Space steps a choice through its codes, and through blank for a field that
  is not required (`FF_REQ`), as a filter;
- TAGS a code field of `LINK_DESCRLEN` characters: words between commas or blanks, each 1 to
  `LINK_ITAGLEN` of `A-Z 0-9 - _ . / +`, which the server checks and keeps normalized (one
  comma between tags, a repeat, case ignored, left out), so a saved item shows its tags as kept
  when it is opened again; F4 opens the tag pick list (below) and appends the tag chosen after a
  comma, unless the field holds it already, or says "No room for another tag" (`ct_tags_add` of
  `CODETAB.C`; `TAGFORM.SNC`);
- QTY, ACQ_PRICE, CUR_VALUE numbers, not negative;
- LOC_ID the location, typed as its LOC_ID or its code (F4: the location pick list, which puts
  in the code, or the LOC_ID of a location without one), blank for none; the row below shows
  its path from `LOC GET` (`scr_loc_show`, cut from the left at a ` > ` so the nearest names
  stay, `msg_path`), after F4, after a value is typed and when the item is loaded, which also
  shows a location's code in place of its ID;
- ACQ_DATE a date; the rest text at their field widths.

A new item starts with WORKS ?, QTY 1, STATUS A and no grade. Save: `ITEM ADD k=v...` (`+OK
ID=n`) or `ITEM SET id k=v...`, split as described under forms. Notes: `memo_edit` on `ITEM`. In
view mode Ctrl-E switches to edit (level 2); the edit-mode hints are `Enter=Save F4=List
Esc=Cancel F1=Help`.

**Split.** Split units of the Record menu (level 2; view mode, or edit mode with nothing changed,
else `MT_SAVEFIRST`) asks in a prompt titled "Split item n" for the units to take off (`Quantity`,
digits, at most `LINK_QTYLEN`; empty or Esc sends nothing) and sends `ITEM SPLIT id QTY=n`.
On `+OK ID=m` the form loads the item again, its quantity lower, and the hint row says "Split off item m";
the new item, at the same location, shows in the list the form goes back to. 102 and 202 ("not
enough on hand") are shown as the server gives them, the form staying; after a 200 the form leaves,
as after a save. Errors: 102 and 202 with `FIELD=` go to the field (unknown category or location,
quantity below the units lent, reserved or in use, a location change on an assembly, Status D while
units are held or equipment is reserved for or in use with the item, `SCRE72.SNC`, snapshot
`REFUSED`); 200 says the item is gone and leaves.

**Tabs.** The item form takes `SCR_ITEMFORM_H` rows; below it a strip names the item's tabs. The
Record menu's Parts, Service records, Loans and holds and Used with it (view mode, or edit mode with
nothing changed; otherwise, and in a new item's form, `MT_SAVEFIRST`) open the Parts, Service or
Loans tab, the last on the item's own rows or on those it hosts: a list in a panel from
`SCR_TAB_TOP` to the bottom of the work area, covering the form from that row down, with its own
keys and hints, until Esc redraws the form. `form_run` returns the Record menu's verbs
(`TUI_VERBKEY` to `TUI_VERBLAST`) in every mode, so the save-first rule for them is the item form's
own. A tab holds one list view, closed before anything else opens, so the item form adds at most one
list view to the screen it came from.

```
+----------------------------------- Item 1 -----------------------------------+
| ID        1             Category  MOBO          Status    A                  |
| Type                                            Condition EXC                |
| Maker     Hercules                                                           |
| Model     M07-B                                                              |
| Serial    SN-0037                                                            |
+-------------------------- [Parts] Service  Loans  ---------------------------+
|      ID Cat  Maker                Model                  Qty Location      S |
|       2 CPU  Logitech             M14-C                    3 Shelf A, bot~ A |
|       3 CARD Tandon               M21-D                    1 Shelf A, bot~ A |
|       4 DRV    Seagate            M28-E                    2 Shelf A, bot~ A |
|       7 MAN      IBM              M49-C                    2 Shelf A, bot~ A |
|       8 CBL        Hercules       M06-D                    3 Shelf A, bot~ A |
+------------------------------------------------------------------------------+
 Enter=Open Ins=Install Esc=Form F1=Help
```

(`SCRE24.SNC`, snapshot `BOM1`.)

- **Parts** (`SCRASSY.C`, its rows from `SCRAFORM.C`): `ASSY BOM id PAGE=n [AFTER=c]`, depth first;
  the maker is indented `AC_INDENT` columns per level below the first, up to `SCR_BOMINDENT` levels.
  Enter shows the selected part in the item form; the form keeps the IDs and modes of up to
  `SCR_ITEMBACK` earlier items, dropping the oldest, so Esc goes back through them one by one, then
  to the list, and a save goes back the same way, its note on the hint row of the item it returns
  to. Ins (Install part) asks for an item (`scr_ask_item`) and sends `ASSY INSTALL <that> <this>`;
  Take out asks for the location of the selected part (location pick list) and sends `ASSY REMOVE
  <part> LOC=n`. An ID that names no item is answered "No item n" (`scr_assy_send`). Empty: "No
  installed parts".
- **Service** (`SCRMAINT.C`): `MAINT LIST id`, oldest service date first; Enter shows a record,
  Ins adds one, Ctrl-E edits, Del deletes (asks). Hints: `Enter=Open Ins=New Ctrl-E=Edit
  Del=Delete Esc=Form F1=Help`.
- **Loans** (`SCRLOAN.C`): `LOAN LIST ITEM=id`, the item's loans, reservations and uses, open and
  ended, in row order, in the columns `! Loan Kind Qty Holder Due Ended`; Ctrl-PgDn shows instead
  the open rows of other items that name it as their host (`LOAN LIST HOST=id`, the columns of the
  list of every item) and back, as does Ctrl-PgUp, the status line saying which. Ins chooses the
  kind, or Ctrl-L, Ctrl-R or Ctrl-U gives it, and adds a row of the item, or beside the rows it
  hosts, of an item asked for, the host preset; Return or release ends the selected row (asks for
  the date, empty for today), Enter shows a row, Ctrl-E edits it; Open item beside the hosted rows
  hands the row's item to the item form, which shows it in place of this one (Esc comes back, as
  from a part). A row added, changed or ended changes the item's held units or its count of hosted
  rows, so the form loads the item again when the tab closes. Hints: `Enter=Open Ins=New Ctrl-E=Edit
  Ctrl-PgDn=Used with it Esc=Form F1=Help`; beside the hosted rows, all of them reservations and
  uses, `Enter=Open Ins=New Ctrl-E=Edit Ctrl-PgDn=This item Esc=Form F1=Help` (`SCRE72.SNC`).

### Assemblies, service, loans and holds (`SCRASSY.C`, `SCRMAINT.C`, `SCRLOAN.C`)

Items > Assemblies is `ASSY LIST TOP=1` through `scr_item_list`, with the item list's columns
and keys ("No assemblies" when empty).

Items > Service is `MAINT RECENT`, the newest record added first:

```
+------------------------------- Recent service -------------------------------+
|     Rec     Item Date       Ty     Cost Summary                              |
|      24        1 2026-10-07 MD     4.00 Fitted a turbo switch                |
|      23       31 2026-09-20 IN          Check 19                             |
|      22       30 2026-09-19 IN          Check 18                             |
|      21       32 2026-09-18 IN          Check 17                             |
+------------------------------------------------------------------------------+
 Enter=Open Ctrl-E=Edit Del=Delete Esc=Back F1=Help
```

(`SCRE27.SNC`, snapshot `RECENT`.) The service record form, for a record (`MAINT GET`, `MAINT
SET`) or a new one (`MAINT ADD <item>`, the item being the form's `FF_KEY` field, preset and
read-only), is a pop-up over the list:

```
|    +------------------------- Service record 24 -------------------------+   |
|    | Record    24            Item      1                                 |   |
|    | Date      2026-10-07    Type      MD                                |   |
|    | Summary   Fitted a turbo switch                                     |   |
|    | Cost      4.00          Done by                                     |   |
|    | Notes     Lines: 0 (F4 to open)                                     |   |
|    +---------------------------------------------------------------------+   |
 Ctrl-E=Edit Esc=Back F1=Help
```

(`SCRE27.SNC`, snapshot `VIEW`.) In view mode the Record menu's Open item shows the record's item in
the item form; from the Service tab, where the item is already shown, it is not offered. Edit mode:
`Enter=Save F4=List Esc=Cancel F1=Help`. MTYPE (a choice: RP CL RC TS MD IN, named in the help) and
SUMMARY are required; an empty date is today on the server. Notes is the DETAIL memo, edited with
`memo_edit` on `MAINT`.

Items > Loans and holds shows the rows of every item, each lending units of an item to a borrower,
reserving them (for a purpose, a person or another item, its host) or marking them in use (with
something or a host). Ctrl-PgDn goes through its views, Ctrl-PgUp back (`SCR_LV_` of `SCRNFORM.H`,
the box titled by each): the open rows (`LOAN LIST OPEN=1`, those without a due date first, then by
due date), the open loans, reservations and uses alone (`KIND=`), the overdue ones (`LOAN OVERDUE`:
loans and reservations; a use has no due date) and every row, ended ones included, with the date
each ended in place of the item's maker and model. Late rows (`LATE=1`) are drawn `TA_ALERT` and
marked `!`, so the mark shows on every palette; a row naming only its host shows "For item n" (a
reservation) or "With item n" (a use) as its holder, a column left blank among the rows of that
host:

```
+---------------------------- Open loans and holds ----------------------------+
|!     Loan Kind         Item  Qty Holder          Due        Maker and model  |
|         7 In use         19    1 With item 5                Commodore M33-E  |
|!        1 On loan         5    1 Jane Smith      2026-10-01 Commodore M35-A  |
|!        4 On loan         6    1 Dora            2026-10-02 Western Digital ~|
|         5 On loan        17    1 Carl            2026-10-08 Tandon M19-C     |
|         6 Reserved       12    1 Club show       2026-10-20 Commodore 5150 C~|
|         2 On loan        11    2 Bob Jones       2026-11-01 Seagate M27-B    |
+------------------------------------------------------------------------------+
 Enter=Open Ins=New Ctrl-E=Edit Ctrl-PgDn=Loans Esc=Back F1=Help
```

(`SCRE29.SNC`, snapshot `OPEN`, and one snapshot per view.) Ins offers Lend..., Reserve... and Use
with... (`tui_pick`), or Ctrl-L, Ctrl-R or Ctrl-U gives the kind, then asks for the item among the
available ones (`scr_ask_item`) and shows the kind's form for it, titled Lend, Reserve or Use with
(`SCRE71.SNC`). Return or release asks "Return loan n?", "Release reservation n?" or "Release from
use n?", the kind read back from the row's text (`scr_loan_rowkind`), with the date (empty for
today), and sends `LOAN RETURN n [DATE=]`. Open item opens the row's item in the item form
(`scr_loan_rowitem`). Each kind's form is a pop-up over the list (`SCR_POPTOP`, `SCR_POPLEFT`),
built from one table by `scr_loan_form`, a loan's a row lower than the others, which have the host:

```
|    +------------------------------ Reserve ------------------------------+   |
|    | ID                        Item      23          Quantity  1         | C~|
|    |               Logitech M11-D                                        |   |
|    | Reserved for  Club show                                             |   |
|    | For item      5                              Commodore M35-A        |   |
|    | Contact                                                             |   |
|    | From                      Until                 Released            |   |
|    | Notes                                                               |   |
|    +---------------------------------------------------------------------+   |
 Enter=Save F4=List Esc=Cancel F1=Help
```

(`SCRE71.SNC`, snapshot `HOSTSET`.) The item's maker and model show below its ID and the host's
after its own, each from `ITEM GET` (`scr_loan_kitem`, `scr_loan_khost`; an item since deleted
leaves them blank). The labels follow the kind: a loan's Borrower, Out and Due, both required,
and Returned; a reservation's Reserved for, For item, From, Until (optional) and Released; a
use's In use with, With item, Since and Released, without a due date. A reservation or use
needs a holder or a host, which the server checks. The host takes an ID or text. Leaving the
field looks up what may be an ID (`scr_search_item`'s rule) and shows that item's maker and
model, or clears them; F4 lists the active items holding the text (all of them for an ID or
nothing); a save first finds the host with `scr_find_item`: an item's ID stands, anything
else, digits that name no item included, is chosen from the active items holding it, the form
staying open when that list is left (`SCRE71.SNC`, snapshots `HOSTTEXT`, `HOST999`, `STAYS`).
The ID chosen replaces the text. A new row is sent through `form_save` as `LOAN
OUT`, `LOAN RESERVE` or `LOAN USE <item> ...` (`scr_loan_new` sets the word with
`form_set_add`), the item being the `FF_KEY` field; QTY starts at 1, and QTY and the date out
can be set only on a new row; later saves are `LOAN SET`. Errors with `FIELD=` go to the field
(not enough on hand: QTY; a host that is no item, disposed of, or the item itself or one of its
parts: the host, a 200 among them, which `form_fail` takes as a field error; installed or
disposed: the item field, which leaves the form). View mode:
`Ctrl-E=Edit Esc=Back F1=Help`; edit mode `Enter=Save Esc=Cancel F1=Help`, with `F4=List` for the forms
with a host.

### Locations (`SCRLOC.C`, `SCRPICK.C`)

```
+--------------------------------- Locations ----------------------------------+
| Name                                   Type               Code       Capacity|
| Attic                                  Room               ATT                |
|   Attic shelf                          Shelf              E1               10|
| Garage                                 Room               GAR                |
|   Shelf A, bottom                      Shelf              A2               12|
|   Shelf A, top                         Shelf              A1               12|
|     Bin B1                             Bin                B1                6|
|     Bin B2                             Bin                B2                6|
| Office                                 Room               OFF                |
|   Cabinet                              Cabinet            CAB                |
|     Drawer 1                           Drawer             C1               20|
|   Desk                                 Shelf              D1                4|
|                                                                              |
+------------------------------------------------------------------------------+
 Enter=Contents Ins=New Ctrl-E=Edit Del=Delete Esc=Back F1=Help
```

(`SCRE7.SNC`, snapshot `LOCS`.) The locations are a tree, shown whole: `LOC LIST PAGE=n
[AFTER=c]` gives it in tree order (depth first, those inside one location by name), each row's
name indented `SCR_LT_INDENT` columns a level below the top, then the type's name, from one
`LTYPE LIST` held in a far block while the screen is open (`CODETAB.H`), the code and the
capacity (`scr_loc_row` of `SCRLFORM.C`; INVWIN makes its own wider rows, `WROWS.H`), a name
cut at its column ending in `~`. The views open at once (the tree, a pick list over it) share one `LTYPE LIST` (`scr_lt_types`),
which the location form and the type pick list use too. The view is `scr_lt` of
`SCRPICK.C`, which the location pick list shares; a letter moves the selection to the next
location whose name starts with it, wrapping once, looking through at most `LV_SEEKMAX`
rows.

Enter shows the contents. Ins adds a location inside the selected one, or at the top level when the
tree is empty (Locations > New location, or an empty Inside, adds at the top level too); on a row at
the deepest level (its indent tells its depth) it is refused at once, "No location can go inside
name". Ctrl-E edits; the Record menu's Move moves the selected location, with everything in it, into
one chosen from the location pick list (titled "Move into", its lead row "(top level)", opened on
the location it is in) by `LOC SET id PARENT_ID=n`, the location itself and those inside it refused
in the list ("A location cannot go inside itself"), one at the deepest level refused before any
request ("No location can go inside name"), as the form refuses such a new Inside on save; Del asks
"Delete location name?", with the full name from `LOC GET`, and sends `LOC DEL id` (202: "location
holds items" or "location holds locations"). Those four need level 2. After an add, an edit or a
move the tree is fetched again from its start, as a change can move rows between pages, and the
changed location is sought and selected (a move or rename of a location between pages could
otherwise skip or repeat rows); after a delete the rows are fetched again where they were.

```
+---------------------------- New location ----------------------------+
| Name         Front hall                                              |
| Type         RACK      Rack                                          |
| Inside                                                               |
|              (top level)                                             |
| Code                                                                 |
| Capacity                                                             |
| Notes        (after saving)                                          |
+----------------------------------------------------------------------+
```

(`SCRE8.SNC`, snapshot `TOPLEVEL`.) Form: LOC_NAME (required), LTYPE (a code of the location
types, required; F4 their pick list; its name shown beside it, LT_NAME), PARENT_ID (the location it is inside, typed as its LOC_ID
or code, blank for the top level; F4 the location pick list), the read-only path of that
location below it, LOC_CODE (optional, upper case, unique), CAPACITY (not negative), Notes
(`memo_edit` on `LOC`). A new location inside another starts with that one as PARENT_ID, shown
by its code, and with the type of the first location already inside it (`LOC LIST UNDER=id
PAGE=1`), else none; a new top-level one starts as `ROOM`. While the type is still the one it was
given, a new Inside gives it that of the new place. The path line is no Tab stop: a form's moves
pass the fields that only show a value (`FF_LOCAL | FF_RO`). Save: `LOC ADD ...` (`+OK ID=n`) or
`LOC SET id ...`; a changed PARENT_ID moves the location with everything inside it. 202 (a move
into itself or below itself, or deeper than the server allows) and 201 (a code already used) go
to their fields.

```
+--------------------------- Garage > Shelf A, top ----------------------------+
|      ID Cat  Maker           Model           Works   Qty Location   Status   |
|       7 MAN  IBM             M49-C           no        2 Shelf A, ~          |
|      14 DRV  IBM             M48-E           ?         3 Shelf A, ~          |
|      21 MOBO IBM             M47-B           no        1 Shelf A, ~          |
|       2 CPU  Logitech        M14-C           intrm     3 Bin B1              |
|       9 PART Logitech        M13-E           yes       1 Bin B1              |
+------------------------------------------------------------------------------+
 Enter=Open Ins=New Ctrl-E=Edit Del=Delete Ctrl-D=Duplicate Esc=Back F1=Help
```

(`SCRE7.SNC`, snapshot `CONT`.) Contents: `LOC CONTENTS id [TOP=1] [ALL=1] PAGE=n [AFTER=c]` through
`scr_item_list`, titled with the location's path from `LOC GET` (cut from the left to
`SCR_IL_TITLEW`); Enter and Ctrl-E open the item form, Ins a new item with LOC_ID preset
(`scr_item_new`); 200 means the location has gone. The Record menu's Not installed only switches
between every item there, installed parts included, the first view, and those not installed in
another (`TOP=1`); Everything below between the location's own items and those of every location
below it too (`ALL=1`). A location that holds others, as the first row of `LOC LIST UNDER=id PAGE=1`
tells (`scr_loc_first`, one request more), opens on everything below it, a room's items lying on its
shelves; any other on its own items. Each switch fetches the first page again. The status line says
which of each (`Showing: all items, this location`, `Showing: not installed, everything below` and
the two others); the hints and the other keys are those of every item list.

```
+----------------------- Move into ------------------------+
| Name                                   Type              |
| (top level)                                              |
| Attic                                  Room              |
| Garage                                 Room              |
|   Shelf A, bottom                      Shelf             |
|   Shelf A, top                         Shelf             |
|     Bin B1                             Bin               |
|     Bin B2                             Bin               |
| Office                                 Room              |
|   Cabinet                              Cabinet           |
|     Drawer 1                           Drawer            |
|   Desk                                 Shelf             |
|                                                          |
|                                                          |
+----------------------------------------------------------+
```

(`SCRE9.SNC`, snapshot `MOVEPICK`.) The location pick list (`scr_pick_loc`) is the same tree
in a pop-up box, the name and type columns of its rows (`SCR_LT_PICKW` characters), after a
lead row: "(no location)" in an item's form and the filters, "(top level)" for a move or a
location's container, "(every location)" for bin labels. It opens on the location its field holds
(sought through the tree, at most `LV_SEEKMAX` rows); a seek that stops short of it leaves the
row it stopped on selected, and Enter on that row asks `MO_PICKQ` first, No keeping the list
open, so that an untouched Enter cannot choose a location nobody picked (`PICKFAR.KEY`). Its
keys are those of the tree's view; Enter chooses, and gets the chosen location's `LOC GET` (for
a container, the levels above it, so that a location cannot be put inside itself; the location form refuses a typed Inside naming
the location or one inside it with the same words, before the save). A move in the tree that
re-chooses the location it is in sends nothing. In a form a location field (`scr_locf` of `SCRLFORM.H`: the field and its path
field; the item form's is `scr_item_loc`, the location form's `scr_loc_inside`) takes the chosen location's code, or its LOC_ID when it has none, and blank for the lead
row; in a filter blank means any location and the lead row gives `0`, no location.

### Categories and location types (`SCRCODE.C`)

```
| Code     Name              Order         |
|>SYS      Systems               1         |
| MOBO     Motherboards          2         |
| CPU      Processors            3         |
| CARD     Expansion cards       4         |
| DRV      Drives                5         |
| PERI     Peripherals           6         |
| SW       Software              7         |
| MAN      Manuals               8         |
| CBL      Cables                9         |
| PART     Parts                10         |
+------------------------------------------+
```

(`SCRE65.SNC`, snapshot `CATS`.) Items > Categories and Locations > Location types each show
their code list in a box (`tui_pick_keys`, which stays drawn under an entry's form, the Del
question and the menus), from one `CAT LIST` or `LTYPE LIST` of at most
`LINK_MAXCODES` rows held while it is shown (`scr_code_load`), with the code, the name and the
sort order under their headings; the category and location type pick lists show the same, and
open on the field's code. Ins adds an entry, Enter or Ctrl-E edits the selected one, Del asks and
sends `CAT DEL
code` or `LTYPE DEL code` (202 while the code is in use); those need level 2. The list is
fetched again after each change. An entry's form holds the code, given once as the positional
word of `CAT ADD` or `LTYPE ADD` and not editable after, the name (required) and the sort order
(`SORTORD`, not negative), which orders the lists; saves send `CAT SET` or `LTYPE SET`. The
forms' tables are `SCRKFORM.C`'s, which INVWIN shares. Help topics `CATS` and `LTYPES`.

### Search (`SCRSRCH.C`)

```
+-------------------------------- Search items --------------------------------+
| Text                                       Tags                              |
| Serial                                     Works                             |
| Category             Location              Status        Available           |
|                                                                              |
|                      All below                                               |
+------------------------------------------------------------------------------+
 Enter=Search F4=List Esc=Back F1=Help
```

(`SCRE13.SNC`, snapshot `SERSTART`.) A form of local fields. The location, typed or chosen
from the location pick list, has its path shown below it; blank is any location, `0` (the pick
list's "(no location)") the items of none; All below, a logical field under it, adds the items
of every location inside it (`ALL=1`, in the tree's order, `SRCHALL.SNC`), and is left out
without a location.
The text is looked for in the maker, model, description, tags, serial number and type.
Available is a logical field, blank at first, then Y or N (Space, or the Y and N keys); Y keeps
the items that can be lent, reserved or used (status A, top level, not every unit held), N, like
blank, filters nothing. Tags (`LINK_TAGFILT` characters, F4: the tag pick list, which appends)
keeps the items carrying every tag typed, whole, the case of letters ignored, its blanks sent as
commas (`ct_tags_bare` of `CODETAB.C`): `net` does not find `network`, as the text does. Works
keeps the items in one works state, `?` (not tested), `Y`, `I` or `N`, or, with `Y?I` (Not known
broken), every one but the broken; blank keeps all. The two close the cycle of fields after
Available, so the three starts below keep their places. Enter runs the first search that applies:
text (1..`LINK_MAXFIND` characters) → `ITEM FIND "text" [CAT=] [LOC= [ALL=1]] [STATUS=]
[AVAIL=1] [TAG=] [WORKS=]`, the filters given; else serial → `ITEM SERIAL "sn"`, the filters
left out; else the filters → `ITEM LIST [CAT=] [LOC=] [STATUS=] [AVAIL=1] [TAG=] [WORKS=]`
(ORDER=LOC when a location is given, ALL=1 in its place with All below). A status beside
Available is refused before any request, "Choose a status or Available, not both", as the server
refuses `STATUS=` beside `AVAIL=1`; the status field steps back to empty with Space. The results
are titled `Items containing "text"`, `Items containing "text" matching the filters`, `Items
with serial number sn` or `Items matching the filters`, appear in `scr_item_list` with the item
list's keys and default fields, and give a new item the location filtered on; Esc goes back to
the search form with its values kept (`SRCHTAG.SNC`: the network cards of the sample data, those
tagged retro, those of them that work, and those not known to be broken). A text search reads a
bounded number of records per request, so a page may take several requests on a large table,
with progress shown and Esc stopping it. No rows: "No items found". The field widths keep every
value within what the server accepts, and any failure of the result list's requests is reported
by the list. Ctrl-F elsewhere opens this screen on the text field (`SCMD_FIND`), the menu entries on
the serial (`SCMD_SERIAL`) or category (`SCMD_FILTER`) field. The form's table, the choice of
search from its values and the search's command, parts and results' title live in `SCRSFORM.C`
(`SCRSFORM.H`), which INVWIN's search shares.

### Reports and labels (`SCRRPT.C`, `RPTRUN.C`, `RPT.C`)

The server sends rows; the client lays them out. Each report is one paged command, asked for
with `PAGE=` `LINK_PAGEMAX` and repeated with `AFTER=` while a `C` line comes. Each response's
rows go into a far page buffer and are laid out (`rpt_line`) after `rq_send` has returned, so a
printer question never waits while a response is in flight.

| Entry | Requests | Order | Groups and totals |
|---|---|---|---|
| Item listing | `REPORT ITEMS [CAT=] [LOC=] [STATUS=] [TAG=] [WORKS=] ORDER=ID\|MFR`; ORDER=LOC, or All below, the tree's order | the form's order, LOC first | in the tree's order: a heading per location with items (its path, "No location" for none) and its count and quantity; at the end the count and quantity |
| Valuation | `CAT LIST` once, then the tree's order with `[CAT=] STATUS=A [TAG=] [WORKS=]` | location, then ID | per location: items, quantity, price paid, value; the total; then totals by category in `CAT LIST` order |
| Loans and holds | `REPORT LOANS [OVERDUE=1] ASOF=date` | due date | the count of rows and of overdue ones |
| Item labels | one item: `ITEM GET id`, then `LOC GET` of its location; else the tree's order with `[CAT=] [TAG=] [WORKS=]` | location | — |
| Bin labels | `LTYPE LIST` once; one location: `LOC GET`; with All below, or when no location is given (or `0`), a label for each row of `LOC LIST [UNDER=loc]` | tree order | — |

The tree's order (`RPTRUN.C`) is one paged stream, `REPORT ITEMS LOC=loc ALL=1` with the filters:
the items at the location given, then those of each location below it, depth first; with no
location given, `LOC=0 ALL=1`, the items of no location first, then the whole tree. All below
with "(no location)" lists the items of no location alone. The rows carry their LOC_ID; where it
changes a group starts, headed with the location's path from `LOC GET` (`rpt_group`), or with
the name its rows carry when it has gone since, the run going on. A page may come back short, or
empty with a cursor, as the server reads a bounded number of records a request; the run follows
the cursor until none comes. A bin label takes its name, code and type from its `LOC LIST` row
(the type's name from the `LTYPE LIST` held for the run, `CODETAB.H`) and its path from the
names of the rows above it, kept by depth across the pages, after the first location's path
from `LOC GET`; the path is cut from the left to the label (`msg_path`: the nearest names after
"... > ", or the last name alone, ending in `~` when even that is too long). A 200 of the run is
told on the field that named what was not found, the one label's item or a location typed,
never on an empty or a "(no location)" field. The report's subtitle names the filters given,
each after its label: the category, the location by its full path (`LOC GET`), cut from the left
to the room the others leave, the status, the tags as sent and the works state by its name.

`ASOF=` is the client's own date (`scr_date`: the `/NOW:` date in tests, else the DOS date), so
the date printed in the header and the overdue marks agree.

The options form, of local fields, is a pop-up at `SCR_POPTOP`, `SCR_POPLEFT`:

```
     +--------------------------- Item listing ----------------------------+
     | Category                 Location                                   |
     | All below N                                                         |
     | Order     LOC            Status    ALL                              |
     | Works                    Tags                                       |
     | Output    SCREEN         File                                       |
     +---------------------------------------------------------------------+
 Enter=Run F4=List Esc=Back F1=Help
```

(`SCRR1.SNC`, snapshot `LFORM`.) Each kind places its fields on rows of its own, without
holes, the location's path right under it on the All below row, and the cursor visits them in
that order (`rr_kinds` of `SCRRFORM.C`, which INVWIN's dialogs share).

| Kind | Fields |
|---|---|
| Item listing | category (F4: category pick list); location (F4: location pick list, its path shown beside All below); order (LOC, ID, MFR); status (ALL, A, D); all below (N; Y adds every location inside it); works and tags, the search's filters (F4 on tags: the tag pick list); output; file |
| Valuation | category, location, all below; works, tags; output, file |
| Loans and holds | overdue (N; Y lists only the loans and reservations due before the date); as of (a date, empty for today); output, file |
| Item labels | category and location, or one item's ID; all below; works, tags; output, file |
| Bin labels | one location (blank: every location), all below; output, file |

Output is `SCREEN`, the first choice and the first value, `PRINTER` or `FILE`, so a first run
shows the report in the viewer whatever printer there is. Enter runs the report and the form stays
open for another run until Esc. A file name is required for `FILE` ("Type the name of the
file"), one the CSV export would take (`txt_outfile_ok`: no data or scratch file, no device,
"Not a name a report file can have"), and an existing file is replaced only after "Replace
FILE?". While the report runs the status line shows `Pages: n  Records: m`; Esc between requests
stops it with "Report stopped", the page in progress being finished. A complete run ends with
"Done; records: n". Help topics `REPORTS` and `LABELS`. The forms' table, their first values,
the filters and subtitle a run takes from them, the scratch file and the viewer's reading of it
live in `SCRRFORM.C` (`SCRRFORM.H`), which INVWIN's reports share.

Screen output is laid out into a scratch file in the current directory, `INVRxxxx.$$$`, `xxxx`
being four hex digits of a checksum of the station name, so stations sharing a network directory
each write their own. It is created only when no file of that name exists (DOS function 5Bh);
else the checksum is XORed with 1, 2 and so on, up to `SCR_RP_SCRTRY` names. The viewer shows
the file and removes it when it closes; a station stopped before that leaves the file and its
name taken. When no name can be created, the screen says "Cannot write INVRxxxx.$$$ here:
choose File output"; once leftovers hold all `SCR_RP_SCRTRY` names of a checksum, every SCREEN
run of that station fails so until those `INVRxxxx.$$$` files are deleted while no station
shows a report.

Layout: `RPT_WIDTH` columns, each column followed by one space except the last; column headings
in `INV.MSG`. Widths, (r) right-aligned:

| Report | Columns |
|---|---|
| Item listing | ID 8 (r), Cat 4, Maker 14, Model 14, Serial 12, Works 5 (its word, as the item rows), Qty 5 (r), Location 10 |
| Valuation | ID 8 (r), Cat 4, Maker 16, Model 16, Qty 6 (r), Paid 11 (r), Value 11 (r) |
| Loans and holds | ! 1, Loan 8 (r), Item 8 (r), Kind 8, Qty 3 (r), Holder 12 ("For item n" or "With item n" for a row naming only its host), Contact 10, Due 10, Maker/model 11 |

Cat is as wide as a category code (`LINK_CATLEN`); Location holds the start of the location's
name, and the valuation, grouped by location, has no location column. A group heading is the
location's path, cut from the left (`msg_path`) when it is longer than a line. On the
valuation, the subtotal and total lines put their label over the ID, Cat and Maker columns,
"Items: n" in the Model column, then the quantity, price paid and value in their own columns.
The category table ("Totals by category") has headings of its own, Cat, Name, Items, Qty, Paid
and Value, under its title and on every page it continues on; each category line puts the bare
count under Items; categories beyond the table's room are summed on an "Other categories" line.
On the report of loans and holds `!` marks a late loan or reservation.

A page is `PAGELEN` lines: `RPT_TOP` header lines (the program, the title, the date and the page
number, bold on an Epson printer; the subtitle naming the filters; a blank line; the column
heading, bold; a rule), the body, and `RPT_BOTTOM` blank lines over the perforation before the
form feed. A group heading is bold and never starts on the last `RPT_KEEP` body lines of a page;
a page that starts inside a group repeats its heading, marked as continued. Numbers too long for
their right-aligned column print as `*` marks; other values are cut. Amounts have two decimals;
dates are `YYYY-MM-DD`. Price paid and value are each item's own amounts, added as stored and
never multiplied by the quantity (the report help says so). Totals are kept in cents in 32 bits;
one that does not fit prints as `*` marks. A report without rows prints "No records". A run that
is stopped, or whose request or printing fails, ends with `rpt_abort`: the last page carries
"Report stopped before the end" and no totals, so a partial report never shows a total.

Labels run on continuous one-across stock, `LABELW` characters wide on a pitch of `LABELH`
lines: at most `LABELH` − 1 text lines, padded with line feeds to the pitch, with no form feed
and no form length (a form feed would eject a page of labels). An item label from
`TEST/GOLDEN/PRT/PRN2.PRN` (`LABELW` at its default, `CFG_DEFLABELW`) and a bin label from
`TEST/GOLDEN/PRT/RPT9.TXT` (20 characters wide, as narrow as a label gets, so the path is cut):

```
No. 1                           SYS     item label: ID and category bold
IBM 5150                                maker and model
S/N 0012345                             serial number (an empty line when none)
Loc Office                              the location's name

Attic box                               bin label: name bold, double width
Code ATTIC-BOX2                         code (no line when it has none)
... > Attic box                         path, cut from the left to the width
Box                                     the type's name
```

The viewer shows a report written to the screen below a rule titled with the report's title,
a line of the report on each row and a form feed as a rule:

```


--------------------------------- Item listing ---------------------------------
RETROINV  Item listing                                       2026-10-08  Page 1


      ID Cat  Maker          Model          Serial       Works   Qty Location
-------------------------------------------------------------------------------
Garage
       5 PERI Commodore      M35-A          SN-0185      no        3 Garage
      12 CPU  Commodore      5150 CGA       SN-0007      no        1 Garage
 Esc=Close F1=Help
```

(`SCRR1.SNC`, snapshot `VIEW1`.) Up, Down, PgUp, PgDn, Home and End move; Esc closes it (topic
`VIEWER`). The print job counts the lines it writes (`pr_lines`), so the viewer reads the file
once to keep the offset of each line, up to `RV_MAXLINES`, in a far block sized to the count,
and shows a screen by seeking to its first line and reading on. A longer report shows its first
`RV_MAXLINES` lines and says "Shown: first n lines". When that block cannot be had, the viewer
asks for half as many offsets at a time, down to `RV_MINLINES`, showing the first lines only and
saying so; when not even `RV_MINLINES` fit, it says "Not enough memory" and does not open.

### Export and import (`SCRCSV.C`, its form and requests from `SCRCFORM.C`)

```
     +----------------------- Export to a CSV file ------------------------+
     | Table        CATS                                                   |
     | File         SCRR6.CSV                                              |
     | Field names  Y                                                      |
     +---------------------------------------------------------------------+
 Enter=Export F4=List Esc=Back F1=Help
```

(`SCRR6.SNC`, snapshot `FILLED`.) One form serves both commands: table (a choice; F4 lists the
tables), file, and whether the first line holds the field names. Its field names are those the
server names in a refusal (`TABLE`, `FILE`, `HEADER`), so a 102 puts the cursor on the field
with the server's text, and a file error naming `FILE` (200, 201 after the first request,
300–304) puts it on the file name with the client's text for the code ("File or directory not
found", "The file already exists", "File error: the disk is full"). Both need level 3, checked
with `tui_allow` before the form opens. The file is on the machine that runs the engine, so on a
network the administrator names a shared drive. The format is dBASE III Plus `COPY TO ...
DELIMITED`, with an optional first line of field names (`DOC/ENGINE.md`, Text files (CSV)).

- Export (title "Export to a CSV file"): tables `ITEMS LOCS CATS LTYPES MAINT LOANS AUDIT`. Enter sends
  `EXPORT table FILE=name [HEADER=1]`, then again with `AFTER=` while a `C` line comes, the
  status line showing `Records: n`. A file that exists (201 naming FILE) is replaced only after
  "Replace FILE?", the export then starting over with `REPLACE=1`. Esc between requests stops
  it: "Export stopped; FILE was not written". The server writes the export under a scratch name,
  the file's own name with an extension of `&C` and the session's slot as a hex digit (`&C0` in
  local mode), and gives it the file's name only in the last answer, so a stopped export leaves
  that file on the machine that holds the data. A 204 in a later answer means the scratch file
  has gone ("The export was interrupted; start it again"). At the end: "Records written to FILE:
  n".
- Import (title "Import a CSV file", hints `Enter=Import F4=List Esc=Back F1=Help`): tables `ITEMS
  LOCS`. Enter asks first ("Add the records of FILE to ITEMS? This cannot be undone."), says on the
  hint row that the import is running, and sends one `IMPORT`. `+OK ADDED=n` gives "Records
  added to TABLE: n". Refused lines (none is added then) are shown in a box titled "Nothing
  imported", up to `LINK_IMPERRS` lines such as `Line 12  CATEGORY: unknown category` kept in a
  far block, and "Lines refused in all: n". A failure after some records were added gives
  "Records added: n; then the import stopped:" and the reason. 402 (other sessions are open) is
  told by `tui_fail`.

Help topics `EXPORT` and `IMPORT`.

### Settings (`SCRSET.C`)

```
+---------------------------------- Settings ----------------------------------+
| Data directory   DATA\                                                       |
| Station name     TEST                                                        |
| Default user                                                                 |
| Item list order  ID                                                          |
| CGA snow         N         Monochrome  N         BIOS output  N              |
| Printer          LPT1   Type EPSON  Page lines 66   Label 35 x 6             |
| Adapter          ?????????????????????????? Settings file  COLOR.CFG         |
| Program          RETROINV 1.0               DOS            ??????????????????|
+------------------------------------------------------------------------------+
 Enter=Save F4=List Esc=Cancel F1=Help
```

(`SETTINGS.SNC`, snapshot `SET`; the `?` cells, the adapter and DOS lines, differ between
machines and are masked.) The fields are the `INV.CFG` keys except `BAUD` and `RTSCTS` (see Help
and settings files); the adapter, DOS version, settings file and program (the `VER` text kept
from login) are shown for information. No request is made. Enter first checks the page length,
label width and label pitch against their ranges and puts the cursor on the first one outside,
with its range. It then writes the settings file with `cfg_save` and applies snow avoidance at
once, the palette from the next screen drawn and the printer settings from the next report; the
data directory, station and BIOS output take effect at the next start, which the message says. A
write failure names the file and what its DOS error means (`rqe_doserr`: "Cannot save the
settings (access denied): INV.CFG"). Allowed at every level: the file is the user's own.
`INVTHIN` shows and saves the data directory too, but never reads it: the field serves INV
started with the same settings file.

### Change password (`SCRPASS.C`)

```
                  +------------- Change password -------------+
                  |  User                 CLERK               |
                  |  Old password                             |
                  |  New password                             |
                  |  Repeat new password                      |
                  +-------------------------------------------+
 Enter=Change Esc=Cancel F1=Help
```

(`PASSWD.SNC`, snapshot `PASSWD`.) All three passwords without echo; the new one and its
repetition take every byte, one more than `LINK_PWMAX` (`PWSET.KEY`). Local checks, which
`sn_passwd` makes before it sends the request: new and repeat equal, `LINK_PWMIN`..`LINK_PWMAX`
characters of 21h..7Eh, different from the old one. Request `PASSWD old new` (`+OK`: "Password
changed"); 401 "The old password is wrong" (not a failed login); 102 the server's text. Every
password buffer is wiped when the screen closes. As `SCMD_MUSTCHG` the title is "Password change
required" and the hints are `Enter=Change Esc=Log-out F3=Quit F1=Help`: F3 asks to quit and Esc
offers to log out instead; until the change succeeds the session accepts nothing else.

### Users (`SCRUSER.C`)

```
+----------------------------------- Users ------------------------------------+
| User     Full name                      Group    Level Status Chg Last login |
| ADMIN  +------------------------ User OPER -------------------------+6-10-08 |
| OPER   |  User name        OPER           Status  A                 |6-10-08 |
| READER |  Full name        Otto Operator                            |6-10-08 |
|        |  Group                                                     |        |
|        |  Level            2                                        |        |
|        |                                                            |        |
|        |  New password                                              |        |
|        |  Repeat password                                           |        |
|        +------------------------------------------------------------+        |
 Enter=Save F4=List Esc=Cancel F1=Help
```

(`SCRR31.SNC`, snapshot `EDITOPER`.) Admin > Users, level 3 only (`tui_allow`, also when the
screen is started otherwise). List: `USER LIST PAGE=n [AFTER=c]` in name order with the
command's default fields; a disabled user's row is marked `TA_ALERT`. Hints: `Enter/Ctrl-E=Edit
Ins=New Del=Delete Esc=Back F1=Help`. Enter or Ctrl-E opens the form (`USER GET name`), Ins a new
one; Del asks and sends `USER DEL name` (202: the last active administrator). Form: USERNAME
(the key: sent positionally by `USER ADD name`, editable only when new, upper case), FULLNAME,
GROUP (loaded from `USER GET`'s GROUPNAME, sent as `GROUP=`), LEVEL (choice 1 2 3, required),
STATUS (choice A D, only for a saved user, as `USER ADD` takes none), and a password typed twice
without echo. A save refuses a repetition that differs ("The password and its repetition differ")
before any request; the repetition is never sent.

A new user's password goes in its `USER ADD`. For a saved user a typed password is sent first as
`USER RESET name PASSWORD=`, then the other changes as `USER SET`; when that SET fails, a box
says that the new password was saved and the rest may not have been, before the failure is
reported. Either way the user must change the password at the next login; left empty, it stays.
Errors with `FIELD=` go to the field: 102 the server's text (a password too short or too long),
201 "Already exists" on the user name, 202 the server's reason (lowering or disabling the last
active administrator). Both passwords are wiped when the form closes, and by `form_wipe` on the
exit path while it is open. Help topics `USERS` and `USER`.

### Administration (`SCRADMIN.C`)

Admin > Backup, Restore, Pack, Reindex, Audit log and Sessions, level 3 only (`tui_allow`, also
when a screen is started otherwise). One table holds the four operations on the data files: the
`ADMIN` word, the directory prompt's title, the question and the success note.

| Entry | Asks | Sends | Success |
|---|---|---|---|
| Backup | a directory (upper-cased as typed) | `ADMIN BACKUP DIR=dir` | "Data backed up to DIR" |
| Restore | a directory, then "Replace all the data with the backup in DIR? This cannot be undone." | `ADMIN RESTORE DIR=dir` | "Data restored from DIR" |
| Pack | "Remove the deleted records for good and rebuild every index? This cannot be undone." | `ADMIN PACK` | "Data packed; records removed: n" (`REMOVED=`) |
| Reindex | nothing | `ADMIN REINDEX` | "Every index rebuilt" |

Esc or an empty directory at the prompt, or N to the question, sends nothing. While the request
runs the hint row says to wait, as it may take minutes. The directory is on the machine that
runs the engine. A refusal naming `DIR` is told in the client's words: 200 is "Directory DIR not
found" for a backup and "No complete backup in DIR" for a restore (a file missing, or the set cut
short); 300–304 is "Backup or data directory:" and `rqe_text`'s text (a backup file that does
not open or has other fields, or a failure of the file made in one directory and looked for from
the other to tell them apart). Everything else goes to `tui_fail`: 102 shows the server's reason
(a bad directory name, the data directory itself), 402 that other sessions are open.

No screen keeps rows across a menu command: the open screen returns first, and every screen
fetches what it shows when it opens (the home screen its counts, with `STATUS`), so nothing shown
after a restore or a pack comes from before it.

```
+--------------------------------- Audit log ----------------------------------+
|  Table ITEMS   Record 3          Station                                     |
+------------------------------------------------------------------------------+
+------------------------------------------------------------------------------+
| Date       Time  User     Table    Record A Field      Old value  New value  |
| 2026-10-08 12:00 ADMIN    ITEMS         3 I ITEM_ID               3          |
| 2026-10-08 12:00 ADMIN    ITEMS         3 U CUR_VALUE             12.25      |
 Esc=Filter F1=Help
```

(`SCRR36.SNC`, snapshot `ITEM3`.) The audit log is a one-row filter form of local fields above a
list view of `AUDIT LIST`. The fields are named as the request's keys (`TBL`, `REC`, `STATION`),
so the list's arguments are the form's non-empty values; a record without a table is refused in
the form ("A record needs its table"), as the server would refuse it. The list opens unfiltered,
newest first, with the command's default fields (a `FIELDS=` list would not fit
`LINK_MAXFSPEC`, so the End walk asks for `AUD_ID` alone). Enter in the form shows the list with
the form's values and moves into it (hints `Esc=Filter F1=Help`); Esc there goes back to the
form (hints `Enter=Show Esc=Back F1=Help`), and Esc in the form leaves. Each row shows the date, the
time to the minute, the user, the table, the record, the action, the field and the old and new
value, the last two cut to their columns; failed logins (`F`) and the rows of a change cut off
(`X`) are marked `TA_ALERT`. Help topic `AUDIT`.

```
+---------------------------------- Sessions ----------------------------------+
| Slot Type Station          User     Level State             Idle             |
|    1 NETB PC1              ADMIN        3 Logged in            0             |
|    2 NETB LAB2                          0 Not logged in       75             |
|    3 NETB LAB3             OPER         2 Password change      4             |
 F5=Refresh Esc=Back F1=Help
```

(`SCRR37.SNC`, snapshot `SESSIONS`.) The sessions are the rows of one `ADMIN CLIENTS`, unpaged
(at most `PROTO_MAXPEERS`), drawn as they arrive and placed by the names of its `F` line, the
state in words and the requester's own row `TA_BOLD`; rows beyond the box are not drawn. F5
fetches them again. In local mode the protocol answers 104 ("Not available in local mode"). Help
topics `ADMIN` (the operations) and `SESSIONS`.

## Printing

Printing happens in the client only: the printer is at the desk of the person who asked, a
printer error needs someone to answer it, and a BIOS printer call can block for its time-out on
every byte, which a server of several sessions cannot afford.

```
SCRRPT.C  forms, progress, Esc, Retry question, viewer
   |  rrun_open, rrun_layout, rrun_go, rrun_close
RPTRUN.C  requests, page buffer                 (no screen)
   |  rows from rq_send -> far page buffer -> rpt_line per row
RPT.C     pages, groups, totals, labels         (no screen, no request)
   |  pr_text, pr_eol, pr_ff, pr_style, pr_form
PRINT.C   the job: buffer, ESC/P or plain, wait function
   |  prn_open, prn_write, prn_close     or     cf_create, cf_write
CPRN.C    INT 17h, the DOS device PRN in raw mode, the capture file
```

Destinations: the printer (Epson ESC/P, or plain text for any other printer: `PRTYPE`), a file
(exactly the bytes a plain printer gets: text, CR LF, FF), and the screen (the same bytes in the
station's scratch file, shown by the viewer). The ESC/P codes are those of the Epson FX-80 that
later Epson-compatible printers keep (`PRINT.H` lists them): initialise at the start of a job,
the form length (`PAGELEN`) for reports, bold and double width. Reports are `RPT_WIDTH` columns
at 10 characters per inch on 66-line (11-inch) paper by default.

`PRINTER=LPT1`..`LPT3` drives the BIOS port through INT 17h; the port must have an address in
the BIOS data area, and its status is read before the first byte, so an absent, switched-off or
paper-less printer is reported at once. `PRINTER=PRN` writes to DOS handle 4 in raw mode (IOCTL,
DOS 2.0), so a printer redirected by `MODE LPT1:=COM1` or a network gets every byte unchanged;
DOS reports its errors through the client's critical-error handler, which fails the call.

When the printer is not there or does not take a byte, the job asks "No printer on this port.
Retry?", "Printer out of paper. Retry?" or, for any other reason, "Printer not ready. Retry?":
Y sends the same bytes again, N cancels the job, dropping the rest of the report, and the run
says "Printing cancelled". Nothing polls and nothing waits on a clock.

INVWIN prints the same job on Windows' default printer, whatever its kind: `CPRNW.C` draws the
job's text in the printer's fixed-pitch font, and bold and double width are fonts of the
printer, not codes. A bin label's name is in double width where the printer has a fixed-pitch
font twice as wide, the expanded pitch of a matrix printer or a font that a page printer
scales; on any other printer, Generic / Text Only among them, it prints bold at the plain
width, and the label's layout is the same.

## Help and settings files

`INV.HLP` holds the help text: sections start with a line `@TOPIC` and hold lines of at most
`TUI_HELPW` characters, the help box's text width. The build puts an index of the sections at
the head of the file, a line `=TOPIC hhhh hhhh` each (offset and length in hex). It is read from
the program's directory (`tui_set_helpdir`), else from the current one. `tui_help` finds the
section in the index (`msg_section`), reads it with one read into a far block, packs its lines in
place and shows at most `TUI_HELPLINES` of them, scrollable, with the hints `Up/Down=Scroll
PgUp/PgDn=Page Esc=Close F9=Keys`; F9 inside the box shows the section `TUI_HELPKEYS`, the keys
every screen has. Topics: `LOGIN HOME MENU ITEMS ITEM MEMO LOCS LOC CATS LTYPES CONTENTS
SEARCH SETTINGS PASSWD SERVICE SERVREC PARTS LOANS LOAN REPORTS LABELS VIEWER EXPORT IMPORT
PICKITEM USERS USER ADMIN AUDIT SESSIONS KEYS`. The Windows client's help text, `INVWIN.HTX`, has
the same format and build, with texts of its own for its keys (`SRC/WIN/WHELP.H`) and the same
topics but `HOME`, `MENU`, `MEMO`, `VIEWER` and `PICKITEM`, and besides `INDEX`, Windows Help's
index, `START`, getting started, `BYCAT`, the items by category, and a section `T_F` for each
field `F` of the forms of topic `T` that have them (items, locations, categories, location
types, service records, loans, users, settings), which F1 shows in the field (`WHELP.H`); a
field without one, or F1 with no field focused, shows the form's. INVWIN
shows its help in Windows Help (`WINHELP.EXE`), from `INVWIN.HLP`, which `HC.EXE` compiles from the
same text (`DOC/BUILD.md`, Windows Help): each topic a page with its title, the key lists with
each description wrapped beside its key, the "See also" titles as jumps, and Index, Back, Browse and Search. Without
`WINHELP.EXE`, or without `INVWIN.HLP` beside the program, the section shows in a window of its
own.

`INV.CFG` holds `KEY=value` lines; `CFG.H` gives the full rules (comments, unknown and repeated
keys, the save). A missing file means every default. A save streams the old file line by line
into a scratch file beside it (extension `.$$$`), keeping comments and unknown lines, so the file
has no size limit.

| Key | Meaning |
|---|---|
| `DATA=dir` | data directory of a LOCAL connection (default `DATA\`) |
| `STATION=name` | station name sent with HELLO (default the DOS machine name, which MS Network Client 3.0 never sets, else `CFG_DEFSTATION`) |
| `USER=name` | user name offered by the login screen |
| `SNOW=0\|1` | CGA snow avoidance (default 0) |
| `MONO=0\|1` | the monochrome palette on a colour adapter (default 0) |
| `BIOS=0\|1` | draw through INT 10h only, as `/BIOS` (default 0) |
| `ITEMORD=o` | first sort order of the item list: `ID` (default), `LOC`, `MFR` or `SERIAL` |
| `PRINTER=p` | `LPT1` (default), `LPT2`, `LPT3`, or `PRN` for the DOS device |
| `PRTYPE=t` | `EPSON` (default) or `PLAIN` |
| `PAGELEN=n` | lines per page, `RPT_MINPAGE`..`RPT_MAXPAGE` (default `CFG_DEFPAGE`) |
| `LABELW=n` | label width, `RPT_MINLABELW`..`RPT_MAXLABELW` (default `CFG_DEFLABELW`) |
| `LABELH=n` | label pitch in lines, `RPT_MINLABELH`..`RPT_MAXLABELH` (default `CFG_DEFLABELH`) |
| `BAUD=n` | serial speed, a rate of `UART_BAUDOK` (default `UART_DEFBAUD`) |
| `RTSCTS=0\|1` | RTS/CTS flow control on the serial connection (default 0) |
| `WINSPLIT=n` | `INVWIN` only: the width of the location tree beside the contents, in average characters, 0..`CFG_SPLITMAX`, 0 for the tree's Name and Type columns (default 0); `INV` keeps the line as it is |
| `WINCOLS.t=w,...` | `INVWIN` only: the widths of the columns of table t (`ITEM`, `SER`, `PART`, `TREE`, `SVC`, `ISVC`, `LOAN`, `ILOAN`, `USER` or `AUDIT`), in average characters, one to `CFG_COLMAX` numbers of 0..`CFG_COLWMAX` in the columns' order, 0 for the column's own width (default all 0); a drag of a list's divider writes it, and a table whose widths are all its own gets no line; `INV` keeps the line as it is |
| `WINPANE.t=s[,w]` | `INVWIN` only: the pane beside the list of its split screen t (`LOC`, the contents beside the location tree; `ITEM`, the preview beside the item list and a search's results; `ASSY`, the selected assembly's parts beside the assemblies; `LOAN`, the history of the selected row's item beside the loans and holds; `USER`, the selected user's audit entries beside the users; `CAT`, the selected category's items beside the categories): s 1 shows it and 0 the list alone, and without a line it shows on a LOCAL, NET or TCP link and not on a serial line or a mailbox; w, if given, is the width of the list beside it in average characters, 0..`CFG_SPLITMAX`, 0 for `INVWIN`'s choice, which `LOC`, whose width is `WINSPLIT=`, does not use; View > Pane beside the list writes s, and a drag of the bar between a list and its pane w, with s 1, but for `LOC`; a save writes no line for a pane without one; `INV` keeps the line as it is |

## Memory

`INV.EXE` and `INVTHIN.EXE` are built in the medium model: code in one segment per module, data
pointers near.

**Near data.** The static near data of the whole program (DGROUP without the stack) must stay
within `MAX_NEAR` of `TEST/mapchk.sh`, for every compiler. Two checks enforce it:

- `TEST/OMFNEAR.PY` reads each object file's OMF records and adds up the segments that the
  `GRPDEF` records place in DGROUP (the stack excluded) and the near communal variables
  (`COMDEF`, `LCOMDEF`), the same way for Open Watcom, Watcom C 6.5 and Turbo C 2.01 objects,
  and checks each module against its budget in `TEST/NEARBUD.TXT`, which also holds a line for
  the C library's own near data. That file holds the figures and explains how DGROUP's 64 KB
  divide between static data, stack and near heaps.
- `TEST/mapchk.sh` checks the DGROUP total from each compiler's map file, and that its extent,
  the stack and the near heap fit in 64 KB.

What each client module's budget covers:

| Module | Holds |
|---|---|
| CTIMER (DOS build), CNBNAME, CNBDRAIN, PRINT | nothing (the print job is on the near heap) |
| CKEY | the key source, the Alt-letter table, the mouse's hook |
| CMOUSE | the press read and not yet taken, the last click and its tick count |
| CMEM, CMEMHI, CMEMHR | the fault hook `cmem_fault` (block descriptors are on the near heap); `cmem_heapsz`, and under Turbo C `_heaplen` |
| CVIDEO | mode and cursor state, a changed-column span per row |
| CTEXT, CZERO | the hex digits; the block of zeros `cf_zero` writes from, in the BSS |
| CNETB | the NCBs of the wait-form commands and of a listen with a deadline, the list of NCB sets the provider still owns |
| CUART | the interrupt handler's machine code, copied into each open port's far block, the ports' IRQ numbers and state |
| CPRN | the capture path, the open job's state |
| MSG | `MSG_NBUF` buffers of `MSG_MAXLEN` + 1, the first number of each range, the block handle and slot order, the directory and file name `msg_load` got, the reason buffer (`MSG_WHYLEN`) |
| RQ | constants and the flag that refuses a request sent from a callback |
| LINK, LINKNET, LINKSER, NBCLI, SERCLI, RESPRD | protocol words, line ends, the open's failure texts, the backends' tables of operations, NBCLI's name prefix (each link's and session's state is on the near heap) |
| TUI | three palettes (`3 × TA_NROLES`), session, frame state, the hint text (`TUI_HINTBUF`), help topic, the key codes of the key names and the letters in scan code order |
| LISTV, FORM, FMODEL, MEMO | state of the open dialogs |
| PAGER, SESS, MEMOIO | the protocol words of their requests; PAGER's table of its moves; SESS's table of the `STATUS` keys and the text of `CFG_DEFSTATION` |
| KEYSCR | `KS_MAXMASK` masks, one script line of `KS_MAXLINE` + 1, file state |
| CFG | key names, the printer port and type names |
| RPT | column tables (field, width, heading message), the field names, each kept once |
| INV.C, INVMENU.C, INVCOM.C, ARGS.C | menu tables (message numbers), switch names, the texts written to standard error (the `/CMD` buffers are on the stack of its loop) |
| LINKLOC.C, LINKNOL.C, INVLOC.C, INVNOL.C | what INV and INVTHIN link apart: LINKLOC's failure texts of `link_open_local`; LINKNOL's refusal; the usage lines and program name of INVLOC.C and INVNOL.C (`INVCONN.H`) |
| SCRLOGIN | the version text (`scr_version`), the user name and password being typed |
| the other screens, ITEMLS | form tables, list sources, choices, protocol words; SCRITEM, with the tab table, and SCRIFORM, the item form's, are the largest; ITEMLS holds the item list's source and the field names of its rows |

No UI text counts against these: field names (`"MFR"`) and protocol words (`"ITEM LIST"`) are
the only string literals the screens keep.

**Near heap and stack.** DGROUP also holds the stack (`_stklen` of `CDOS.C`) and the near heap,
where the engine keeps its handles and buffers (`DOC/ENGINE.md`, Memory per open engine). The
client's near heap use is small and bounded: an `lv` descriptor holding its pager (three cursors
of `LV_CURSZ`, the F names of `LV_MAXFIELDS` × (`LINK_NAMELEN` + 1), state) per open list view,
a form's two value sets (the sum of its field widths plus one each, twice), the `cmem_blk`
descriptors, an output job's buffer and, while a valuation runs, its category codes (`RPT_NCAT`
× (`LINK_CATLEN` + 1)). On the stack a request costs an `rq` and an `rq_res` in the caller and
about `LINK_MAXRESP` of decoding space in `rq_send`. At most four list views are open at once:
the location tree, a location's contents, an item form's tab, and a location or item pick list
over it. Under Turbo C, INV reserves `CMEM_INVHEAP` of near heap, less than the `CMEM_NEARHEAP`
of `INVSERV` and the tests, and `INVTHIN.EXE`, which has no engine, only `CMEM_REMHEAP`; the rest
goes to the far heap. `CMEMHI.C` and `CMEMHR.C` say how those were measured and why Open Watcom
keeps `CMEM_NEARHEAP` for both.

**Free memory.** The free memory INV and `INVTHIN.EXE` need, measured at the deepest screens,
is in `DOC/BUILD.md` (Runtime requirements).

Requests are built and sent in small leaf functions, and no screen function holds an `rq` or
`rq_res` across a call that opens another screen, so nested screens do not stack up request
buffers. Runs with `/KEYS:` end their snapshot file with the stack bytes never touched
(`cstk_unused`), which the DOS golden-screen runs check against a margin.

**Far blocks** (`CMEM.C`, none above `CMEM_MAXBLK`):

| Block | Size | Lifetime |
|---|---|---|
| Shadow screen | `VID_CELLS × 2` | whole run |
| Saved DOS screen | `VID_CELLS × 2` | whole run; only when the start mode is kept and it is mode 7, `/BIOS` mode, or page 1 was active (else video page 1 holds it) |
| Message texts | an index of the ranges, `MSG_NSLOT` page slots of `MSG_PAGE`, a read area as large as the largest paged range's lines, the pages of the ranges below `MSG_PAGEMIN` (and from `MSG_PAGEEND` on: the server's file, which has no paged range and so no slots) | whole run |
| Message file | the file's size, in whose place the message texts are then allocated; then, above them, its start up to the last line of a range that stays loaded | while `msg_load` runs |
| Pop-up save | its cells × 2, at most `VID_CELLS × 2` | while the pop-up is open |
| List view rows | `2 ×` the window's rows `× LV_ROWBYTES` | per open list view |
| List view cursor ring | `LV_RINGSZ`, or `LV_RINGMAX` (`LV_MAXBACK × LV_CURSZ`) once its cursors need it; both for a moment while the ring moves to the larger | per open list view, from its first page start cursor pushed |
| Code list (`CODETAB.H`) | `LINK_MAXCODES` rows of `CT_ROWSZ`: name, sort order and code; a tag list holds its tags alone, `CT_TAGSZ` each, in the same block | while a code pick list (the tag pick list included), a code list screen, the location tree or a bin label run is open |
| Memo lines | `MEMO_MAXLINES × MEMO_COLS` | from `mio_open` to `mio_close`: while the editor is open |
| Help section | the section's bytes in `INV.HLP` | while help is shown |
| Report page buffer | `LINK_PAGEMAX × (RPT_ROWMAX + LINK_IDLEN + 1)`: the rows, then the LOC_IDs of one page of the walk | while a report runs |
| Category totals | `RPT_NCAT` (2 × `LINK_MAXCODES`) entries of name and totals, about 4 KB | valuation only: from `rpt_open` to `rpt_close` or `rpt_abort` |
| Viewer line offsets | `RV_MAXLINES × RV_OFFSZ` at most | while the viewer is open |
| Refused import lines | `LINK_IMPERRS × (SCR_CX_REFW + 1)` | while an import runs |
| NET session buffer | `2 × NB_MSGMAX` (a message sent, a message received) | while the NET link is open |
| NET session NCBs | 2 NCBs of 64 bytes (the RECEIVE, the SEND) | while the NET link is open |
| Serial session buffer | `2 × SC_CHUNK` of `SERCLI.C` (a piece of the line being sent, bytes read from the ring) | while the SERIAL link is open |
| Serial port | the handler, its variables and the rings of `UART_RXSZ` and `UART_TXSZ` bytes, aligned on a paragraph within the block | while the SERIAL link is open |

A pop-up (help, message box, question, prompt, pick list, menu) whose save block cannot be had
does not open: it shows "Not enough memory" and gives its cancel answer, and a message box shows
its text on the hint row instead.

**Code.** Every module's code segment must fit `MAX_CODE` of `TEST/mapchk.sh`; client modules
are far below it, the screens being table-driven (form and menu tables).
