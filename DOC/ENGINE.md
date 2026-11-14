# RETROINV — Data Files, Engine and Protocol Layer

The data directory holds dBASE III tables (`.DBF`), memo files (`.DBT`) and indexes (`.NDX`)
that dBASE III Plus 1.1 reads and writes as its own; `SRC/DBF/` reads and writes them. The
engine (`SRC/ENGINE/`, interface `ENGINE.H`) owns the files of one data directory and applies
the business rules. The protocol layer (`SRC/PROTO/`, interface `PROTO.H`) turns request lines
into engine calls and writes the responses. `proto_dispatch()` is the single entry point of
every transport and of the client in local mode. All of it is portable ANSI C89. The wire
format is in `DOC/PROTOCOL.md`.

## File format

As written by dBASE III Plus 1.1 and by `SRC/DBF/`:

- Table header: 32 bytes, then one 32-byte descriptor per field, then `0x0D`; header length is
  `32 + 32 × fields + 1`. Version byte `0x03` (no memo) or `0x83` (table has a memo field;
  dBASE III Plus opens a `0x03` table that has `M` fields without complaint but ignores the
  `.DBT` and shows every memo as empty, while `SRC/DBF/` refuses such a table).
- Last-update date in bytes 1–3 as YY MM DD with YY = year mod 100 (2026 → `0x1A`). Readers
  take 80–99 as 19YY, 00–79 as 20YY, and 100 and up, which some later xBase programs write,
  as 1900 + value.
- Descriptor bytes 12–31 are written as zeros. dBASE stores a memory address in 12–15 and a
  work-area number in 20 of a table made by `CREATE ... FROM` or `COPY STRUCTURE` (zeros in one
  made in its interactive `CREATE` screen); readers ignore them.
- Field names upper-case, ≤10 chars. `N` is right-justified ASCII; `D` is `YYYYMMDD`; `L` is
  stored as `T`/`F` (`Y`/`N` are accepted and stored as `T`/`F`; blank reads as false); `M` is a
  10-char right-justified block number into the `.DBT`. Blank `N`, `D`, `L` and `M` fields are
  spaces. Record byte 0 is `' '` (live) or `'*'` (deleted). File ends with `0x1A`; an empty
  table is the header followed by `0x1A`.
- The header record count and last-update date are rewritten when the table is flushed or
  closed after any change, and by PACK. After records have been appended the file is committed
  (DUP+CLOSE) before the header's record count is rewritten, and committed again afterwards,
  so the size on disk always covers the counted records; a stop between the two leaves only
  uncounted bytes after the last record, which readers ignore.
- Memo file (`.DBT`): 512-byte blocks; block 0 holds the next free block number (u32
  little-endian) in bytes 0–3. `dbt_create` writes the rest of block 0 as zeros and `dbt_flush`
  rewrites only bytes 0–3; dBASE leaves leftover memory in bytes 4–511, and readers ignore the
  rest. A memo starts on a block boundary and is its text followed by `0x1A 0x1A`; the file ends
  right after the last memo's terminator (no padding), and the gap before a new memo is
  zero-filled. Memos are append-only: an edited memo is written to new blocks and the old ones
  stay until PACK.

## ITEMS.DBF (0x83, memo in ITEMS.DBT)

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| ITEM_ID | N | 8 | 0 | PK from SEQ |
| CATEGORY | C | 4 | | FK CATS (seed rows: Opening a data directory, below) |
| ITEMTYPE | C | 20 | | free subtype, e.g. "ISA VGA", "MFM HDD" |
| MFR | C | 30 | | manufacturer |
| MODEL | C | 30 | | |
| SERIALNO | C | 30 | | |
| DESCR | C | 60 | | one-line description |
| COND | C | 3 | | cosmetic grade: NEW EXC GD FR PR (new, excellent, good, fair, poor), blank when not graded |
| WORKS | C | 1 | | Y works, I works intermittently, N does not work, `?` not tested |
| QTY | N | 5 | 0 | |
| ON_LOAN | N | 5 | 0 | units lent: the QTY of the item's open LOANS rows of KIND L |
| RESERVED | N | 5 | 0 | units reserved: those of its open rows of KIND R |
| IN_USE | N | 5 | 0 | units in use: those of its open rows of KIND U |
| ATTACHED | N | 5 | 0 | the open R and U rows naming the item as HOST_ID |
| LOC_ID | N | 8 | 0 | FK LOCS (LOC_ID), the place the item sits at any level; 0 = unknown; an installed item has its top-level item's |
| PARENT_ID | N | 8 | 0 | 0 = top level (containment) |
| ACQ_DATE | D | 8 | | |
| ACQ_SRC | C | 30 | | |
| ACQ_PRICE | N | 9 | 2 | |
| CUR_VALUE | N | 9 | 2 | |
| TAGS | C | 60 | | the user's tags, joined by single commas (`Retro,ISA,network`); their form: `DOC/PROTOCOL.md`, Item fields, filters, ITEM TAGS and ITEM SPLIT |
| STATUS | C | 1 | | A active, D disposed |
| CREATED | D | 8 | | |
| UPDATED | D | 8 | | |
| NOTES | M | 10 | | ITEMS.DBT |

## LOCS.DBF (0x83, memo in LOCS.DBT) — the location tree

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| LOC_ID | N | 8 | 0 | PK from SEQ; never changes |
| PARENT_ID | N | 8 | 0 | LOC_ID of the location holding it; 0 = top level |
| LOC_NAME | C | 30 | | what people read, e.g. "Shelf A"; unique among the live locations of one parent, case ignored |
| LOC_CODE | C | 10 | | optional short code for typing and labels, upper-case, e.g. `A1`; blank or unique among the live locations |
| LTYPE | C | 8 | | FK LTYPES, upper-case |
| CAPACITY | N | 4 | 0 | informational |
| NOTES | M | 10 | | LOCS.DBT |

A location's path is the names of its ancestors and its own from the top down, as in
`Office > Shelf A > Bin 1`; the tree is at most `ENG_MAXLOCDEPTH` levels deep and holds no loop
(Items, locations and code tables, below).

## CATS.DBF (0x03) — seeded, editable

| Field | Type | Width | Dec |
|---|---|---|---|
| CAT_CODE | C | 4 | |
| CAT_NAME | C | 20 | |
| SORTORD | N | 2 | 0 |

## LTYPES.DBF (0x03) — the location types, seeded, editable

| Field | Type | Width | Dec |
|---|---|---|---|
| LT_CODE | C | 8 | |
| LT_NAME | C | 20 | |
| SORTORD | N | 2 | 0 |

CATS and LTYPES are the code tables, of at most `ENG_MAXCODES` records each; their seed rows are
under Opening a data directory, below.

## MAINT.DBF (0x83, memo in MAINT.DBT)

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| MAINT_ID | N | 8 | 0 | PK |
| ITEM_ID | N | 8 | 0 | FK ITEMS |
| MDATE | D | 8 | | |
| MTYPE | C | 2 | | RP repair, CL clean, RC recap, TS test, MD modify, IN inspect |
| SUMMARY | C | 60 | | |
| COST | N | 8 | 2 | |
| DONE_BY | C | 20 | | |
| DETAIL | M | 10 | | MAINT.DBT |

## LOANS.DBF (0x03) — loans, reservations and in-use equipment

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| LOAN_ID | N | 8 | 0 | PK |
| ITEM_ID | N | 8 | 0 | FK ITEMS |
| KIND | C | 1 | | L loan, R reservation, U in use |
| QTY | N | 5 | 0 | |
| HOLDER | C | 30 | | the borrower (L), who or what it is reserved for (R), what it is used with (U) |
| HOST_ID | N | 8 | 0 | FK ITEMS: the item it is reserved for (R) or in use with (U); 0 for none, always for L |
| CONTACT | C | 30 | | |
| OUT_DATE | D | 8 | | |
| DUE_DATE | D | 8 | | due back (L), reserved until (R, optional); blank for U |
| RET_DATE | D | 8 | | returned or released; blank = open |
| NOTES | C | 60 | | |

## AUDIT.DBF (0x03) — append-only, one row per changed field

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| AUD_ID | N | 8 | 0 | PK |
| ADATE | D | 8 | | |
| ATIME | C | 8 | | HH:MM:SS |
| STATION | C | 16 | | from HELLO |
| USERNAME | C | 8 | | from LOGIN |
| TBL | C | 8 | | ITEMS, LOCS, MAINT, LOANS, CATS, LTYPES, USERS; AUDIT for an X row and the K row of an ADMIN command |
| REC_ID | N | 8 | 0 | the record's ID; the record number for CATS and LTYPES |
| ACTION | C | 1 | | I insert, U update, D delete/undelete, M move, P parent change, O counter raised (ON_LOAN, RESERVED, IN_USE, ATTACHED), R counter lowered, K import or ADMIN command, L login, F failed login, W password change, X rows of a change cut off |
| FLD | C | 10 | | |
| OLDVAL | C | 40 | | truncated; memo changes log "MEMO" |
| NEWVAL | C | 40 | | |

The rows each change writes, and the `X` row and repairs an open writes as `SYSTEM`, are in
Audit rows, below.

## USERS.DBF (0x03) — logins, modelled on dBASE III Plus PROTECT

| Field | Type | Width | Dec | Notes |
|---|---|---|---|---|
| USER_ID | N | 4 | 0 | PK |
| USERNAME | C | 8 | | login name, `A-Z 0-9 _`, upper-case (PROTECT limit), unique |
| FULLNAME | C | 30 | | |
| GROUPNAME | C | 8 | | free grouping, upper-case, shown in the user list |
| LEVEL | N | 1 | 0 | 1 read-only, 2 operator, 3 admin |
| SALT | C | 4 | | 4 hex digits, regenerated on every password change |
| PWHASH | C | 32 | | hex of one-way hash over SALT+PASSWORD; never plain text |
| STATUS | C | 1 | | A active, D disabled |
| MUSTCHG | L | 1 | | T = must change password at next login |
| LASTLOGIN | D | 8 | | |
| CREATED | D | 8 | | |

The level each command needs is in the command-level table under Protocol layer, the rules for
user names and passwords under Users and the password hash, and the `ADMIN` row the first open
writes under Opening a data directory (all below).

## SEQ.DBF (0x03)

| Field | Type | Width | Dec |
|---|---|---|---|
| SEQ_NAME | C | 10 | |
| NEXT_VAL | N | 8 | 0 |

One row per ID-bearing table (ITEMS, LOCS, MAINT, LOANS, AUDIT, USERS) holding the next ID to
issue.
The writer reads, increments, writes and flushes (DUP+CLOSE) **before** appending the new
record; how an open raises a row that lags behind is under Opening a data directory, below.

## Indexes

Each index's header holds its key expression, so dBASE maintains the index as this system
does. No two live records share a key of ITEMS1, LOCS1 or USERS1, yet the header's unique flag
stays 0: ITEM_ID and LOC_ID are unique because SEQ issues each ID once, and a user name is
checked against the live records before an add. LOCS3 holds a blank key for every location
without a code; the engine keeps a code unique among the live locations, and a name among a
parent's (LOCS2), before every add and change.

| File | Key expression | Used for |
|---|---|---|
| ITEMS1.NDX | `STR(ITEM_ID,8)` | lookup by ID; `ORDER=ID` |
| ITEMS2.NDX | `STR(LOC_ID,8)+STR(ITEM_ID,8)` | the items at a location: `LOC CONTENTS`, `LOC=`, `ORDER=LOC`, `LOC DEL` |
| ITEMS3.NDX | `STR(PARENT_ID,8)+STR(ITEM_ID,8)` | the parts of an item: `ASSY BOM`, `CHILDREN`, `LIST` |
| ITEMS4.NDX | `UPPER(MFR)+UPPER(MODEL)` | `ORDER=MFR` |
| ITEMS5.NDX | `UPPER(SERIALNO)` | `ITEM SERIAL`; `ORDER=SERIAL` |
| LOCS1.NDX | `STR(LOC_ID,8)` | lookup by ID; walks up the tree |
| LOCS2.NDX | `STR(PARENT_ID,8)+UPPER(LOC_NAME)+STR(LOC_ID,8)` (46 bytes) | the locations of a parent by name: `LOC LIST`, `LOC CONTENTS ALL=1`, the name check, `LOC DEL` |
| LOCS3.NDX | `LOC_CODE` | lookup by code; the code check |
| MAINT1.NDX | `STR(ITEM_ID,8)+STR(YEAR(MDATE),4)+RIGHT(STR(MONTH(MDATE)+100,3),2)+RIGHT(STR(DAY(MDATE)+100,3),2)` | `MAINT LIST` |
| LOANS1.NDX | `STR(ITEM_ID,8)+STR(LOAN_ID,8)` | `LOAN LIST ITEM=` |
| LOANS2.NDX | `STR(YEAR(RET_DATE),4)+STR(YEAR(DUE_DATE),4)+RIGHT(STR(MONTH(DUE_DATE)+100,3),2)+RIGHT(STR(DAY(DUE_DATE)+100,3),2)+STR(LOAN_ID,8)` (20 bytes) | open rows first (a blank RET_DATE gives the year `   0`), by due date, so the open and overdue lists are prefix scans: `LOAN LIST` without `ITEM=` (with `OPEN=1` or `HOST=`, the open prefix alone), `LOAN OVERDUE`, `REPORT LOANS` |
| AUDIT1.NDX | `TBL+STR(REC_ID,8)+STR(AUD_ID,8)` | `AUDIT LIST TBL=` |
| AUDIT2.NDX | `USERNAME+STR(99999999-AUD_ID,8)` | a user's rows, newest first (the largest AUD_ID gives the smallest key): `AUDIT LIST USER=` without `TBL=` |
| USERS1.NDX | `USERNAME` | login; `USER LIST` |

`KEYEXPR.C` evaluates the subset of the dBASE III Plus expression language that index keys need
— field names, character and numeric constants, string `+`, numeric `+` and `-`, unary minus,
`STR(n,w[,d])`, `UPPER(c)`, `RIGHT(c,n)`, `SUBSTR(c,s[,n])`, `YEAR(d)`, `MONTH(d)`, `DAY(d)`,
with function names abbreviable to four letters — and produces keys byte-identical to what
dBASE III Plus computes (checked against dBASE itself, `TEST/GOLDEN/KEYEXPR.TXT`). Expressions
whose key length dBASE would derive differently from the value (`STR` without a width,
out-of-range `RIGHT`/`SUBSTR`) are refused. dBASE III Plus has no `DTOS()`; the date part above
yields the same `YYYYMMDD` text (a blank date gives `   00000`, which sorts before every real
date). An N field is read as dBASE reads it, so a damaged one still has a key: its leading number
(`  12  12` is 12, text with no digit 0), or, when it starts with `*` (dBASE's overflow), a value
that `STR` writes as asterisks at any width (the rules: `KEYEXPR.C`).

## NDX layout

As written by dBASE III Plus 1.1 (measured from `TEST/GOLDEN/DB3/`):

- 512-byte pages; page 0 is the header, and the file is `page count × 512` bytes.
- Header: root page (u32 at 0), page count = next free page including the header (u32 at 4),
  key length (u16 at 12), keys per page = `508 / entry size` (u16 at 14), key type (u16 at 16:
  0 character, 1 numeric), entry size = `8 + key length` rounded up to a multiple of 4 (u16 at
  18), unique flag (byte 23, set by `SET UNIQUE ON`), and from byte 24 the key expression as
  typed (dBASE keeps a trailing space) ending in NUL.
- Page: entry count (u16 at 0; bytes 2–3 unused), then entries of
  `{lower page u32, record number u32, key}`. Leaf entries have lower page 0. Inner entries have
  record number 0, point at a child page and carry the **highest** key of that child's subtree;
  an inner page with n keys has n + 1 child pointers, the last in slot n with no key, and holds
  up to keys-per-page keys (keys-per-page + 1 children).
- In dBASE's files a page's bytes 2–3, the header bytes after the expression's NUL and key
  bytes past the key length are leftover memory, and the slots after a page's count hold
  leftover memory or stale entries; readers ignore them.
- Key type 1 (an index on a numeric or date field) stores an 8-byte IEEE double, dates as the
  Julian day number; this system uses character keys only.
- Insert splits full pages. Delete removes the leaf entry, tightens the parent's key to the
  leaf's new highest key (and so on upward), and unlinks a leaf that becomes empty, leaving an
  inner page with 0 keys and one child rather than merging pages (REINDEX restores density).
  Every inner key therefore always equals the highest key below it, which dBASE's key-only
  descent relies on. Deleted records keep their keys; readers skip `*` records as dBASE
  does. Key-field update = delete + insert. dBASE too leaves an inner page with no key and one
  child, and never reuses or frees an emptied page: it stays in the file, counted in the
  header's page count, outside the tree.
- When an open index's flush wrote new pages, the file is committed (DUP+CLOSE) after the
  pages are written and before the header's root and page count are, and committed again
  afterwards, so the header never counts more pages than the size on disk holds. A reindex
  writes its new file under a scratch name, which becomes live only once complete.
- dBASE's `INDEX ON` builds bottom-up, writing each page as soon as it is full, and folds a
  trailing partial level that holds a single child into its parent. The tree is therefore not
  balanced: leaves can sit at different depths (a 2000-key index on `STR(ID,8)` has a root whose
  children are two inner pages and one leaf), so readers tell leaves from inner pages per page
  (leaf entries have lower page 0) rather than by depth.

## Rules over the tables

The rules the engine keeps over these tables (the `PARENT_ID` trees of assemblies and of
locations and their limits, the location of installed items, the counters `ON_LOAN`, `RESERVED`,
`IN_USE` and `ATTACHED` of ITEMS as sums over the open LOANS rows) are in the sections below.
One of them binds anyone who edits the files by hand: `MAINT_ID` and `LOAN_ID` have no index,
and a record is found by a binary search over the record numbers, so MAINT.DBF and LOANS.DBF are
only appended to, with ascending IDs; records are never reused, reordered or renumbered by hand
(`TEST/CHKDBF.PY` checks the order).

## Text files (CSV)

`EXPORT` and `IMPORT` write and read the files of dBASE III Plus `COPY TO file DELIMITED` and
`APPEND FROM file DELIMITED`, byte for byte as dBASE writes them (`TEST/DBASE/csvchk.sh`
compares the two):

- One line per live record, in record order, ended by CR LF; the non-memo fields in file order,
  separated by commas; after the last line one Ctrl-Z (1Ah). A table without live records gives
  a file of that one byte (after the field-name line of `HEADER=1`). Memo fields are never
  written or read.
- C: in double quotes, trailing spaces removed, leading spaces kept; an empty value is `""`.
  N: the number without leading spaces; a blank N is written as nothing (`,,`), a zero as `0`
  or `0.00`. D: `YYYYMMDD`; a blank D is written as nothing.
- One difference from dBASE: a `"` inside a C value is written doubled (`""`); dBASE leaves
  it bare, and its own `APPEND FROM` then cuts the value there. Bytes 80h..FFh (code page 437)
  pass unchanged; a byte below 20h or 7Fh in a stored value is written as a space.
- `HEADER=1` adds a first line of the field names, bare and comma-separated (dBASE writes
  none).
- Import (ITEMS, LOCS) accepts LF or CR LF, skips blank lines and lines of spaces, stops at a
  Ctrl-Z and refuses a line holding any other byte below 20h but TAB. A bare value is trimmed of
  spaces; a quoted one is kept as it is, `""` read as `"`, commas inside literal. Without a
  header the columns are the non-memo fields in file order and a line may hold fewer; with one,
  its names (any case, each once, no memo field) choose the columns. An empty value is no value,
  so the defaults of ADD apply. The read-only columns of ITEMS (ITEM_ID, ON_LOAN, RESERVED,
  IN_USE, ATTACHED, PARENT_ID, CREATED, UPDATED) are ignored, so an export can be imported as
  new records; an item's LOC_ID is 0 or names a live location by its LOC_ID or code. In LOCS the
  file's LOC_ID only labels its line, the new location taking an ID from SEQ, and a PARENT_ID
  names a live location or another line of the file, so an export imports back with its tree
  (the rules: CSV export and import, below). An import only adds records; how lines are checked
  and refused is in `DOC/PROTOCOL.md` ("Reports, export and import"), its audit rows under Audit
  rows, below.

## Modules

| File | Responsibility | Uses |
|---|---|---|
| `ENGINE/SCHEMA.C` | The near part of the schema (rows of `SCHTAB.H`): table and index names and numbers (`eng_table_def`, `eng_index_def`), field flags (`eng_field_flags`) | — |
| `COMPAT/CSCHEMA.C`, `CSCHDAT.C` | The far part, read only by an open or a failure: field layouts, key expressions, the code tables' seed rows, numbered texts. `CSCHDAT.C` holds the data, `CSCHEMA.C` copies it out (`eng_schema_field`, `eng_index_expr`, `eng_seed_row`, `eng_tx_copy`), under Windows from the program's resource in the layout of `CSCHDAT.H`, which the host tool `CSCHBIN.C` writes | SCHEMA |
| `ENGINE/TABLES.C` | `eng_open_files`, flush, close, the closed-engine functions of `ENGADMIN.H`; error state; actor and clock; the batch flag (`eng_batch_on`); field lookup (`eng_field`, `eng_field_num`, `eng_field_name`, `eng_fieldlist`), record and memo reads (`eng_rec_*`, `eng_seek_live`) | SCHEMA CSCHEMA |
| `ENGINE/IDGEN.C` | IDs from SEQ (`eng_seq_*`), `eng_id_find`, the STR(id,8) ID keys | SCHEMA TABLES |
| `ENGINE/AUDIT.C` | Audit rows, their IDs and index entries; batches (`eng_batch_begin`, `eng_batch_end`) | SCHEMA TABLES IDGEN |
| `ENGINE/CURSOR.C` | Cursor text, `eng_scan_*` bounded scans | SCHEMA TABLES |
| `ENGINE/EDIT.C` | `eng_edit_*`: record edits, index upkeep, memos and note lines, field-change audit. Its state is static, so a program holds one open engine | SCHEMA TABLES AUDIT |
| `ENGINE/USERS.C` | `eng_mix128`, password hash and salt, login, password change, accounts, ADMIN seed | TABLES IDGEN AUDIT CURSOR EDIT |
| `ENGINE/USERTK.C` | Logins with a ticket: `eng_user_hash`, `eng_user_resume`, on what `USERS.C` shares (`ENG_USERS_SHARED` of `ENGUSER.H`); only the programs that keep login tickets link it | AUDIT USERS |
| `ENGINE/CODES.C` | The code tables: categories and location types | TABLES EDIT |
| `ENGINE/LOCS.C` | Location commands, the location tree and its walks | TABLES IDGEN CURSOR EDIT CODES |
| `ENGINE/ITEMS.C` | Item commands and scans, `eng_item_heal` | TABLES IDGEN AUDIT CURSOR EDIT CODES LOCS |
| `ENGINE/ASSY.C` | Assemblies, their walks, `eng_assy_heal` | TABLES IDGEN AUDIT CURSOR EDIT LOCS ITEMS |
| `ENGINE/MAINT.C` | Maintenance records | TABLES IDGEN CURSOR EDIT ITEMS |
| `ENGINE/LOANS.C` | Loans, reservations and in-use rows, the ITEMS counters, `eng_loan_heal` | TABLES IDGEN AUDIT CURSOR EDIT ITEMS ASSY |
| `ENGINE/CSV.C` | CSV export and import | SCHEMA TABLES AUDIT CURSOR CODES LOCS ITEMS |
| `ENGINE/INITDATA.C` | `eng_open`, `eng_init_data`: SEQ repair, seed rows, the repairs after a stop | CSCHEMA TABLES IDGEN AUDIT CURSOR EDIT USERS ITEMS ASSY LOANS |
| `ENGINE/ADMIN.C` | AUDIT LIST; PACK, REINDEX, BACKUP, RESTORE | SCHEMA TABLES IDGEN AUDIT CURSOR INITDATA |
| `PROTO/TOKEN.C` | Line assembly (`proto_acc_*`), tokenizer (`proto_parse`, `proto_get`, `proto_in_list`) | `ENGINE.H` types |
| `PROTO/RESPONSE.C` | Response writer, `proto_errtext`; apart from `TOKEN.C` so that THIN, which only reads requests, links no writer | `ENGINE.H` types; CSCHEMA |
| `PROTO/SESSION.C` | Session state changes (`sess_*`) | TOKEN |
| `PROTO/DISPATCH.C` | `proto_dispatch`, command table, gate, handler helpers | TOKEN SESSION; SCHEMA TABLES CURSOR |
| `PROTO/DPSESS.C` | HELLO LOGIN PASSWD WHOAMI BYE PING VER STATUS | TOKEN SESSION DISPATCH; SCHEMA TABLES USERS |
| `PROTO/DPTKT.C` | LOGIN, PASSWD and BYE with the host's login table (`proto_ticket_cmd`): login tickets and the failed-login throttle, which `DISPATCH.C` runs in place of DPSESS's when the host has the table; only the server links it | TOKEN SESSION DISPATCH DPSESS; USERS USERTK |
| `PROTO/DPUSER.C` | USER commands | TOKEN DISPATCH; USERS |
| `PROTO/DPITEM.C` | ITEM commands, REPORT ITEMS | TOKEN DISPATCH; ITEMS |
| `PROTO/DPLOC.C` | LOC, CAT and LTYPE commands | TOKEN DISPATCH; TABLES CODES LOCS |
| `PROTO/DPASSY.C` | ASSY commands | TOKEN DISPATCH; TABLES ASSY |
| `PROTO/DPMAINT.C` | MAINT commands | TOKEN DISPATCH; MAINT |
| `PROTO/DPLOAN.C` | LOAN commands, REPORT LOANS | TOKEN DISPATCH; CURSOR LOANS |
| `PROTO/DPCSV.C` | EXPORT, IMPORT; compile-time checks of the protocol limits | TOKEN DISPATCH; CSV |
| `PROTO/DPADMIN.C` | AUDIT LIST, ADMIN commands | TOKEN DISPATCH; TABLES ADMIN |

"Uses" names the modules whose functions a file calls; for a protocol file the engine modules
follow the semicolon. Each module uses only those listed above it, with one exception:
`eng_shut` in `TABLES.C`, and `eng_close` through it, calls `eng_edit_cancel` (`EDIT.C`) and
`eng_audit_drop` (`AUDIT.C`), so that no open edit or audit reservation outlives its files. The
`eng` structure is private to `TABLES.C`; the other files reach the tables through `eng_dbf`,
`eng_dbt`, `eng_ndx` and `eng_kx`. Every program that holds an engine, and every engine test,
links all of `ENG_CORE` (`SRCS.MK`): the engine modules, `CSCHEMA.C` and `CSCHDAT.C`.

**Headers.** `ENGINE.H` declares what most modules use. The rest is declared in headers that
only their users include, after `ENGINE.H`, because Watcom C 6.5 runs out of compiler memory
on the largest programs when every file reads every declaration:

| Header | Declares | Included by |
|---|---|---|
| `ENGINE/SCHTAB.H` | the schema rows (X-macros, no include guard) | `ENGFLD.H`, `SCHEMA.C`, `CSCHEMA.C`, `CSCHDAT.C` |
| `ENGINE/TXTAB.H` | the numbered texts (X-macro rows `TX(name, text)` in number order, no include guard) | `ENGTX.H`, `CSCHDAT.C` |
| `ENGINE/ENGTX.H` | the text numbers `ENG_TX_`, `ENG_TXMAX`, `eng_tx_copy`; with `ENGTX_STORE` the packed layout | the engine modules, `RESPONSE.C`, `CSCHDAT.C`, `CSCHEMA.C`, tests |
| `ENGINE/ENGFLD.H` | `ENG_FIELD(t, name)`, `ENG_NFIELDS(t)`; with `ENGFLD_WIDTHS` the field widths and key lengths | modules that name fields by number; with `ENGFLD_WIDTHS`, for buffer sizes and compile-time checks, `SCHEMA.C`, `ITEMS.C`, `DPCSV.C`, `LINK.C` |
| `ENGINE/ENGSCH.H` | `CSCHEMA.C`'s copies of the far schema (`eng_schema_field`, `eng_index_expr`, `eng_schema_fault`, `eng_seed_row`) | `CSCHEMA.C`, `TABLES.C`, `INITDATA.C`, their tests |
| `ENGINE/ENGID.H` | `IDGEN.C`: `eng_seq_*`, `eng_id_find`, the ID keys (`eng_id_key`, `eng_key_id`) | the modules that issue or find IDs, their tests |
| `ENGINE/ENGEDIT.H` | `EDIT.C`: the record edits (`eng_edit_*`) | the modules that write records, their tests, `T_PROTO.C` and `T_PROTO2.C` |
| `ENGINE/ENGNOTE.H` | how a NOTES write treats a memo (`ENG_NOTE_`), the stage's name `ENG_NOTESTAGE` | `EDIT.C`, `ITEMS.C`, `LOCS.C`, `MAINT.C`, `DISPATCH.C`, their tests |
| `ENGINE/ENGUSER.H` | `USERS.C` and `USERTK.C` (`ENGINE.H` keeps the `ENG_LVL_` levels and `eng_login`); with `ENG_USERS_SHARED` what the two share | `USERS.C`, `USERTK.C`, `INITDATA.C`, `DPSESS.C`, `DPTKT.C`, `DPUSER.C`, their tests |
| `ENGINE/ENGCODE.H` | `CODES.C` | `CODES.C`, `ITEMS.C`, `LOCS.C`, `CSV.C`, `DPLOC.C`, their tests |
| `ENGINE/ENGLOCS.H` | `LOCS.C`, with `ENG_LOCPATHLEN`; with `ENGLOCS_WALKS` the walks of the tree | `LOCS.C`, `ITEMS.C`, `ASSY.C`, `CSV.C`, `DPLOC.C`, `LINK.C` (its compile-time checks), their tests |
| `ENGINE/ENGITEM.H` | `ITEMS.C`, with the `ENG_ORD_` orders | `ITEMS.C`, `ASSY.C`, `MAINT.C`, `LOANS.C`, `CSV.C`, `DPITEM.C`, their tests |
| `ENGINE/ENGASSY.H`, `ENGMAINT.H`, `ENGLOAN.H` | `ASSY.C`, `MAINT.C`, `LOANS.C` | those modules, their DP files and tests; `INITDATA.C` includes `ENGASSY.H` and `ENGLOAN.H` for the repairs, `ITEMS.C` and `ASSY.C` include `ENGLOAN.H` (the held units and the INSTALL host check), `LOANS.C` includes `ENGASSY.H` (`eng_assy_path`) |
| `ENGINE/ENGCSV.H` | `CSV.C`, with the export and import limits (`ENG_EXPROWS`, `ENG_IMPERRS`, `ENG_CSVLINE`) | `CSV.C`, `DPCSV.C`, their tests |
| `ENGINE/ENGADMIN.H` | `ADMIN.C`; the `TABLES.C` functions on a closed engine's files (`eng_shut`, `eng_reopen`, `eng_marker_on`, `eng_put_copies`, `eng_drop_copies`, `eng_layout`), the scratch names, `ENG_COPYMARK` | `TABLES.C`, `ADMIN.C`, `DPADMIN.C`, their tests |
| `PROTO/DPCMD.H` | `proto_call`, the handler helpers, the fixed column lists, the `dp_*` handlers | `DISPATCH.C` and the `DP*.C` files |

## Return codes and error state

Engine and protocol functions return `ENG_OK` or an `ENG_E` code whose value is the protocol
error number (`DOC/PROTOCOL.md`, Error codes), so the dispatcher sends it unchanged.

- **Error state.** A failure (`eng_fail`) records a field name (at most `DBF_NAMELEN`
  characters) and a text number, returned by `eng_errfield`, `eng_errmsg` and `eng_errtx` until
  the next failure or `eng_clear_error`, which `proto_dispatch` calls for every request.
- **Mapping.** `DBF_E` codes map once, in `TABLES.C` (`eng_dbferr`, and `eng_cferr` for a
  file call's `CF_E` code): `DBF_EFORMAT` on a table → 300, on an index → 301; `DBF_EFULL` →
  302; `DBF_EHANDLES` → 303; `DBF_EINUSE` → 304 (SHARE refused the deny-read/write open);
  `DBF_ENOMEM` → 500; everything else → 300. A schema row that `eng_open_files` cannot copy
  out of the far data is 500 too ("schema error").
- **Numbered texts.** Failure texts, the why texts of an open and the standard text of each
  error code are numbered `ENG_TX_name` by their row in `TXTAB.H`. New texts go at the end, so
  a number keeps its meaning and a client may map numbers to texts of its own language. The
  texts, at most `ENG_TXMAX` characters, are packed in the far data of `CSCHDAT.C`, and
  `eng_tx_copy` copies one out only to show it, so they take no near data (a Windows program
  reads them from its resource `CSCH_RES` of `SRC/COMPAT/CSCHDAT.H`).
- **Data unchanged.** A failed call changes no data unless its code is in the 300s, except for
  the audit row of a failed login.

## Opening a data directory

`eng_open(&e, dir, now, why, whysz)` takes a directory *prefix* (`""`, `"C:\DATA\"`,
`"OBJ/HOST/PDATA/"`) to which the upper-case 8.3 names are appended, and the starting clock
(`NULL`: the DOS clock). It runs `eng_open_files`, then `eng_init_data`.

`eng_open_files`:

1. **Recovers.** It finishes or undoes an interrupted PACK or REINDEX (`dbf_pack_recover`,
   `ndx_reindex_recover`), renames the copies of an `ADMIN RESTORE` into place when its commit
   mark `ENG_COPYMARK` is there, and else removes the scratch files of a stopped create or
   RESTORE (`eng_put_copies`). When the edit marker `ENG_EDITMARK` exists, the indexes of AUDIT
   and every index of each table it names are noted for rebuilding (every index when the marker
   cannot be read or is longer than `ENG_MARKMAX`), and the AUDIT records past its count are
   taken as rows that may describe changes that never landed (none without a count line, all
   when it cannot be read).
2. **Creates** each missing table under scratch names (`.&DB`, `.&DT`, `.&NX`), renamed into
   place once complete, the `.DBT` first and the `.DBF` last as the commit point. New tables,
   and tables missing an index, are named in the marker first, so an interrupted open repeats
   the work. A missing `.DBT` beside an existing table is 300, never recreated; a `.DBT` without
   its `.DBF` is replaced only when the marker names its table or it is empty, else 300.
3. **Opens and checks.** It holds `ENG_NHANDLES` + `ENG_TMPHANDLES` read-only, deny-none handles
   of `SEQ.DBF` at once, so a `FILES=` too small for the engine fails here with 303 naming
   `SEQ.DBF`, not in a later request. It opens all `ENG_NHANDLES` files `DBF_RDWR`
   (deny-read/write), checks every table's fields against `eng_schema_field` (else 300) and
   every index's stored expression (case and trailing blanks ignored) and key length against
   `eng_index_expr` and `eng_xdef` (else 301), and compiles the key expressions (`kx_compile`).
   A created or noted index is built afresh from its schema and table under a scratch name
   (`ndx_reindex`, skipped for an empty table, whose empty index is already that file) without
   reading the old file, so a damaged index never blocks the open, and deleting a damaged or
   foreign `.NDX` makes the next open rebuild it. AUDIT2 is also checked against its table, with
   one record read and one seek: the last AUDIT record, the newest row, must be the first AUDIT2
   entry of its user, where the key puts it. An `AUDIT2.NDX` left from other data fails that (it
   lacks the row, or holds later rows of that user) and is built afresh like a missing one, with
   no marker, as a stopped build leaves it missing, empty or as it was and the next open builds
   it again. The check cannot tell an index that agrees with the table about its last record and
   differs elsewhere: one whose surplus is later rows of other users, or one that differs below
   a user's newest entry. A surplus entry fails with 300 the list that reaches it while its
   record is not there, and the append of the row with its key, whose marker makes the next open
   rebuild the index; an entry missing, or one whose record now holds another row of the same
   user, leaves a row out of that user's list or shows one twice. `ADMIN REINDEX` rebuilds the
   index whatever it holds, and deleting `AUDIT2.NDX` makes the next open build it. A table the
   marker names, SEQ, and AUDIT whenever a marker exists lose the bytes after their last counted
   record (an append whose header never reached the disk) and get their end-of-file byte back;
   other tables keep them, as dBASE may be appending there.

The marker is then deleted, unless it named ITEMS or LOANS, could not be read, or AUDIT holds
records past its count. Then it is kept (`eng_marker_kept`) for the repairs: rewritten and
sealed (*Edit marker*) as its count line (0 when unreadable) and a line per table it named
(every table when unreadable). A failed rewrite fails the open; one cut short leaves an empty
marker (DOS sets the size at close), read as unreadable next time, so every audit row is
flagged. On the host, where a file takes its size at once, a stop between the zeros and the cut
leaves a marker longer than `ENG_MARKMAX`, read as naming every table.

`eng_init_data` runs as `ENG_SYSTEM` (station and user `SYSTEM`), audits each seed row as one
`I` row, and leaves the actor blank for the protocol layer to set. When the marker was kept, it
first takes it over (`eng_mark_adopt`: bracket count 1, a batch begun), so the commits of the
steps before the repairs add to it:

1. **SEQ rows.** It appends any missing `SEQ.DBF` row (ITEMS, LOCS, MAINT, LOANS, AUDIT, USERS;
   NEXT_VAL 1).
2. **SEQ heal.** It heals SEQ (`eng_seq_heal`), raising a row below the table's largest ID + 1.
   IDs are appended in increasing order, so the largest is that of the last record, deleted or
   not, stepping down at most `ENG_IDSTEP` records past IDs that are blank, not a number, zero
   or negative (none readable gives 0); for ITEMS and LOCS, whose first index is a unique index
   on the ID (ITEMS1, LOCS1), the last key of that index counts too.
3. **Code tables.** It seeds each empty code table that none of its audit rows shows its users
   deleting from (no `D` row among its AUDIT1 entries): `CATS.DBF` with SYS Systems, MOBO
   Motherboards, CPU Processors, CARD Expansion cards, DRV Drives, PERI Peripherals, SW
   Software, MAN Manuals, CBL Cables, PART Parts (SORTORD 1–10), `LTYPES.DBF` with ROOM Room,
   SHELF Shelf, RACK Rack, CABINET Cabinet, DRAWER Drawer, BOX Box, BIN Bin, BUCKET Bucket
   (SORTORD 1–8). So a new table is seeded, and so is one whose seeding a stop cut off, but one
   its users emptied, which PACK leaves without records, stays empty. A seed row that cannot be
   copied out of the schema data (`eng_seed_row` gives an empty code or name) stops the open
   with 500 and is never written blank; the rows seeded before it stay, and a table they made
   non-empty is not seeded again, as after a stop partway through its seeding.
4. **ADMIN.** It seeds an empty `USERS.DBF` with `ADMIN` (`eng_user_seed`: "System
   Administrator", group ADMIN, level 3, salt `0000`, password `admin`, MUSTCHG T). Without a
   kept marker, the seeding of the code tables and of ADMIN runs in one batch begun on CATS, so
   seeding a new directory writes the marker once and flushes each file once.
5. **Repairs.** With a kept marker it flags and repairs: right after the SEQ heal, one `X` row
   flags the audit rows the marker covers, if any (`eng_marker_from`); then, each only once
   everything before it has succeeded:
   - `eng_item_heal` completes an ITEM SPLIT cut off after its new item reached the disk (Items,
     locations and code tables). It reads the rows the marker covers once.
   - `eng_assy_heal` gives every live installed item its top-level item's LOC_ID, compared as a
     number (blank as 0) so that a second pass writes nothing; deleted parts, and parts whose
     chain is broken, passes `ENG_MAXDEPTH` or ends at a LOC_ID that is negative or not a number,
     are left alone. It reads ITEMS3 once, grouped by parent.
   - `eng_loan_heal` sets each live item's ON_LOAN, RESERVED and IN_USE to the quantity of its
     open rows of each kind, capped together at QTY, and ATTACHED to the number of open R and U
     rows naming it as host: one pass over LOANS1 grouped by item, then one over ITEMS clearing
     stale counters (a LOANS1 seek per item with a counter other than 0). A host is counted on
     a walk of the open rows, for every item whose ATTACHED is other than 0 and for every host
     of an open row whose ATTACHED is 0.

   The first two are linear in what they read, the third in the open rows times the hosts it
   counts; all are an accepted exception to the bounded-work rule (a request reads at most
   `ENG_MAXREADS` records) for a repair after a crash. Each corrected item gets UPDATED stamped
   and an audit row by `SYSTEM`, flushed with the AUDIT indexes before the record, whose commit
   writes no row (`eng_edit_quiet`), so a correction never lands without its row.
   `eng_batch_end` removes the marker only when every step and the flush succeeded; otherwise
   the open fails and the next one repairs again.

**Handles.** An open engine holds `ENG_NHANDLES` DOS handles (`ENG_NTABLES` tables, `ENG_NMEMO`
memo files, `ENG_NINDEX` indexes) and, while it works, up to `ENG_TMPHANDLES` more (the edit
marker, the duplicate a flush closes, a memo's stage, a reindex's or pack's new files). A
program starts with a 20-entry handle table, five entries taken by the standard handles, and DOS
3.1 has no function to enlarge it, so `eng_open_files` (and `LINKLOC.C` before it) calls
`dos_set_handles(ENG_PROCHANDLES)`: those two counts, the five standard handles and
`ENG_SPAREHANDLES` for the program's own files (configuration, export and import, printer
capture, the server's event log, a mailbox session's request and response). Each open file also
takes a system-wide entry of `FILES=`; as the open holds all the engine's handles at once (Opens
and checks, above), too small a `FILES=` stops `INV` and `INVSERV` at their start with 303, and
an open that runs out of handles closes what it opened and returns 303. The standard handles
share three entries (CON, AUX, PRN), so a full table uses at most `ENG_PROCHANDLES` − 2 entries
and `FILES=45` (`ENG_FILES`) leaves `ENG_RESFILES` for resident programs and for standard
handles redirected to a file, each of which takes one more (`INV <requests >answers` takes two).

**Clock.** `eng_now` reads `dos_today` and `dos_time` unless `eng_set_now` (or the `now`
argument of `eng_open`) fixed it; a fixed clock also fixes the table-header date
(`dbf_set_today`). Tests always fix the clock.

**Flushing.** Every function that changes data flushes what it touched before returning: SEQ
first (at ID issue), then AUDIT and its indexes, then the memo file, the table and its indexes, so an
audit row is on the disk no later than its record. `eng_flush` flushes what is left in the same
order and, once SEQ or AUDIT has failed to flush, leaves the tables' record counts unwritten, so
no record is counted before its row. `eng_close` cancels an open edit, drops the audit
reservation, then flushes and closes everything under the same rule: once SEQ or AUDIT has
failed to close, each table is closed without its record count (`dbf_drop`), so its appended
records stay uncounted and the marker stays for the next open.

## Records and fields

There is no C structure per table. `SCHTAB.H` lists each field in file order with its name,
type, width, decimals and flags. The flags stay in near data (`eng_field_flags`); the rest is
far data, used only to create and check the tables at open (`eng_schema_field`), after which
fields are read from each table's own handle (`eng_field`). Modules name fixed fields by number
(`ENG_FIELD(ITEMS, QTY)`); `KEY=value` pairs and `FIELDS=` lists are matched to field names
case-insensitively (`eng_field_num`).

| `ENG_F_` name | In `SCHTAB.H` | Meaning |
|---|---|---|
| `ENG_F_RO` | `RO` | not settable through pairs (`eng_edit_apply` with `deny = ENG_F_RO`) |
| `ENG_F_UPPER` | `UP` | stored upper-case |
| `ENG_F_HIDDEN` | `HID` | never sent; edits write no audit row for it |
| `ENG_F_LIST` | `LST` | in the default `F`/`R` field list |
| `ENG_F_NOAUDIT` | `NOA` | its changes produce no audit row |
| `ENG_F_AUDMOVE` / `ENG_F_AUDPAR` | `MOV` / `PAR` | a change is audited as `M` / `P` instead of `U` |
| `ENG_F_REQ` | `REQ` | must be non-blank on ADD (`eng_edit_required`) |
| `ENG_F_NONNEG` | `NNG` | an N value may not be negative |
| `ENG_F_AUDLOAN` | `LON` | a change is audited as `O` when it rises, `R` when it falls, and not at all when the value stays equal (blank to 0) |

The audit flags sit on ITEMS: LOC_ID `MOV`, PARENT_ID `PAR`, and `LON` on the counters
ON_LOAN, RESERVED, IN_USE and ATTACHED. LOANS has no `LST` field, as loan lists have fixed
columns; every AUDIT and SEQ field is `RO`, and every
AUDIT field is `LST`. `SCHEMA.C` checks at compile time that each default list holds at most
`ENG_MAXFLIST` fields of at most `ENG_MAXRWIDTH` characters in all.

Values are text on the wire. `eng_rec_text` gives C trimmed, N trimmed, D `YYYYMMDD` or empty, L
`T`/`F`/empty and nothing for M. `eng_edit_put` checks the type, else 102 with the field name:

- C fits the width and has no byte below 20h or equal to 7Fh;
- N is an optional `-`, digits and at most `dec` decimals that fit the width (below zero
  refused for `ENG_F_NONNEG`, so `-0` is zero), converted to a scaled integer without floating
  point;
- D is a valid `YYYYMMDD` or empty; L is T/F/Y/N or empty;
- M is up to `ENG_MAXMEMO` bytes (as stored) of bytes 20h and above, TAB and LF; each LF not
  preceded by CR is stored as CR LF, and a CR not followed by LF is refused.

**Edits.** `eng_edit_begin(e, t, recno)` (0 for a new record) saves the record image and the
current key of every index of the table (at most `ENG_MAXKEYS` bytes, the ITEMS indexes);
`eng_edit_begin_copy` starts a new record as a copy of another, `eng_edit_begin_reuse` one
written over a deleted record (CAT ADD or LTYPE ADD on a full table), audited as an insert.
Until commit or cancel the table's buffer belongs to the edit, so a module looks up the same
table before `eng_edit_begin`; other tables may be read. `eng_edit_commit(e, recid)` writes the
audit rows (none after `eng_edit_quiet`), appends a pending memo to the `.DBT`, writes or
appends the record, replaces the index entries whose key changed and flushes. It does not check
unique keys: USER ADD and the ADD of a code table look the key up among live records first and
give 201, before `eng_edit_begin` (USERNAME and a code cannot change afterwards), and LOC ADD
and LOC SET look up the name among the new parent's locations and the code among all before
theirs; with a single writer nothing comes between. Deleting marks the record `*` and leaves its
index entries, as dBASE does; readers skip `*` records, and a deleted code, location name or
user name may be used again.

**Edit marker.** Every change that writes audit rows, then a table and its indexes, is
bracketed by the marker file `ENG_EDITMARK` (`EDITING.$$$`) in the data directory, so a stop in
between leaves a trace for the next open.

- **Content.** A count line, the number of AUDIT records when the marker was created, in
  decimal, and CR LF (at most `ENG_MARKHEAD` bytes; written when AUDIT is open), then one line
  per table, its name and CR LF.
- **Order.** A commit creates the marker afresh, writes, seals and closes it, and has DOS write
  its buffers out (function 0Dh), so it is on disk before SEQ; then come SEQ, the audit rows,
  the memo, the record and the changed index entries, each flushed, and the marker is deleted.
  A change cut off may leave audit rows for a record that never landed, never a record without
  its rows; the next open flags them with one `X` row instead of editing them, so AUDIT stays
  append-only. A commit that changes nothing writes no marker.
- **Seal.** A marker written from its start is followed by zeros (`cf_zero`) to the end of its
  `DOS_SECTOR`-byte sector, then cut back to its text: DOS's first write into a sector past a
  file's end sends the rest of the sector from a disk buffer as it is, which may hold another
  file's text, such as a mailbox request with its password.
- **Rows outside a commit** (login, failed login, the open-time repairs) are bracketed by
  `eng_audit_begin` and `eng_audit_end` with the name AUDIT unless a bracket is open. A password
  change or reset brackets its `W` row and its commit together, marking USERS first, so the row
  reaches the disk before the new SALT and PWHASH.
- **Counted brackets.** A failed change leaves its bracket open and the marker on disk until
  the next open. A later change appends its own table as a line (`eng_mark_line`), never
  rewriting what is there nor adding to a marker longer than `ENG_MARKMAX`, which already
  stands for every table, and its unmark only lowers the count; so no run of failures loses a
  table owed a repair. Once a change is on disk, a marker that cannot be deleted is not an
  error: the request succeeds, so a retrying client cannot duplicate an item.
- **Batches.** A change of several commits (an assembly change, a loan and its items, a split)
  is a batch: `eng_batch_begin(e, t)` marks t and sets a flag under which the commits and
  `eng_audit_end` skip their flushes, except that a commit rewriting a record in place flushes
  AUDIT.DBF first. `eng_batch_end(e, rc)`, given the steps' result, flushes everything, AUDIT
  before the tables, clears the flag and removes the marker only when rc is `ENG_OK` and the
  flush succeeded; it returns rc when that is an error, else the flush's result. Only the flag
  decides: a bracket a failed change left open does not stop later commits flushing.
- **Cost.** With no bracket open, a change costs a create, the line, the zeros, the cut and a
  close of the marker, then its remove, each followed by a disk reset. While a bracket is open,
  marking a table the marker is known to name costs nothing; another mark reads the marker and
  writes only a new line.

**Memos** never appear in GET or LIST output; `ITEM NOTES`, `LOC NOTES` and `MAINT NOTES` send
them as `L` lines (`DOC/PROTOCOL.md`, Responses). A memo the engine wrote takes at most two
written bytes per stored byte, so its response stays within `PROTO_MAXUNPAGED`; a longer one
from another program is cut there. `NOTES=` on ADD and SET replaces the memo and
`NOTES ... ADD="text"` appends a line, both under the M input rules. A `NOTES=` text over
`ENG_MAXMEMO` bytes is 102 `FIELD=NOTES`; an appended line that would pass `ENG_MAXMEMO`, and
reading a longer memo written by dBASE, are 202 `FIELD=NOTES`. Memo files are append-only, so
every change writes the whole memo to new blocks and the `.DBT` grows until PACK.

A memo too long for one request is staged (`eng_edit_begin_note` with `ENG_NOTE_NEW`,
`ENG_NOTE_PART` and `ENG_NOTE_END`; `DOC/PROTOCOL.md`, Memos saved in parts). The stage is a
file of the data directory named by its table, `ENG_NOTESTAGE` and the hex digit of its tag
(`ENG_NOTE_TAG`: the session's slot, which the protocol gives every session but a mailbox's) or,
for tag 0, one hashed from the actor's station (`ITEMS.&M3`): the record number (four bytes, low
first), the tag, the actor's station and user (`ENG_STALEN` and `ENG_USERLEN` bytes,
NUL-padded), then the memo so far as stored. `NEW=` writes it from the line alone, and `PART=`
reads it into `eng_memobuf`, appends the line as `ADD=` does and writes it whole again, so a
stage costs no memory between requests and one that is not written whole is removed; neither
opens an edit. `END=` appends its line likewise, removes the stage and commits the memo in one
edit of the record, `ADD=`'s, so the record keeps its old memo until that commit (above, Edits).
No stage, or one of another record, tag, station or user, refuses `PART=` and `END=` and leaves
such a stage alone. A stop leaves the stage, which the next `NEW=` of its tag (or, for tag 0, its
station) in that table replaces; the open does not look for one.

## Audit rows

Each row carries the clock's date and time and the actor (`eng_set_actor`: station from
HELLO, user from LOGIN). The IDs of all rows of one change are reserved with one
`eng_seq_next` (`eng_audit_begin`) and the rows appended one at a time, so a change of many
fields needs only one row's text at a time. Each row gets an entry in both indexes of AUDIT:
AUDIT1, by table and record and then by AUD_ID, for a record's history, and AUDIT2, by the
user name and then by `99999999-AUD_ID`, which puts a user's newest row first, so a user's rows
are read forwards like every other index scan. TBL is the table name (ITEMS, LOCS, CATS, LTYPES,
USERS, MAINT, LOANS); REC_ID is the ID field for ITEMS, LOCS, USERS, MAINT and LOANS and the
record number for the code tables, CATS and LTYPES.

| Event | Rows |
|---|---|
| New record | one `I` row, FLD = first field, NEWVAL = its value (ID or code) |
| Changed field | one `U`/`M`/`P` row per field, OLDVAL/NEWVAL cut to `ENG_AUDVAL`, memo as `MEMO` |
| Delete / undelete | one `D` row, FLD blank, NEWVAL / OLDVAL `DELETED` |
| Login | `L`, TBL USERS, REC_ID the user's ID |
| Failed login | `F`, TBL USERS, REC_ID the user's ID or 0, FLD USERNAME, NEWVAL the user's stored name, or `?` when the name tried names no live user |
| Failed login with a ticket | `F` as a failed login's, but FLD TICKET; a login with a ticket that succeeds writes no row |
| Password change or reset | exactly one `W`, FLD PWHASH, no values |
| ITEM SPLIT | the new item's `I` row, then `U` (TBL ITEMS, REC_ID the item split, FLD QTY, old → new) |
| ASSY INSTALL, REMOVE | the item named: one `P` row (PARENT_ID old → new); it and each part whose location changes: one `M` row (LOC_ID) |
| ASSY MOVE | one `M` row per item of the assembly whose LOC_ID changes |
| LOC SET PARENT_ID= (a move) | one `U` row of the location (FLD PARENT_ID); nothing below it is written, so no item gets a row |
| MAINT ADD / SET / DEL | `I` (TBL MAINT, REC_ID MAINT_ID, FLD MAINT_ID) / `U` per field, DETAIL as `MEMO` / `D` |
| LOAN OUT, RESERVE, USE | `I` (TBL LOANS, REC_ID LOAN_ID, FLD LOAN_ID), then `O` (TBL ITEMS, REC_ID the item, FLD the counter of the kind, old → new), then for a host `O` (REC_ID the host, FLD ATTACHED) |
| LOAN RETURN | `U` (TBL LOANS, FLD RET_DATE, blank → date), then `R` (TBL ITEMS, FLD the counter of the kind), then for a host `R` (FLD ATTACHED); none for an item deleted or a counter already 0 |
| LOAN SET | `U` per changed field; a new host of an open row: `R` of the old host's ATTACHED, `O` of the new one's |
| IMPORT | one `K` row first (TBL the table, REC_ID 0, FLD IMPORT, NEWVAL the path), also for a file with no line, then the `I` row of each record added |
| ADMIN PACK, REINDEX, BACKUP, RESTORE | one `K` row once the command has succeeded, by the request's actor: TBL AUDIT, REC_ID 0, FLD the command's word, NEWVAL the directory of BACKUP and RESTORE; BACKUP's row is written after the copy, RESTORE's into the restored table |
| Change cut off | by `SYSTEM` at the next open: one `X`, TBL AUDIT, REC_ID the first flagged row's AUD_ID, FLD AUD_ID, NEWVAL the last one's; the flagged rows may describe changes that never landed; a flagging open cut short flags them again, its own row included |
| Open-time repairs | by `SYSTEM`: one `U` (FLD QTY) per split completed, one `M` per part moved back, one `O`/`R` per corrected counter; a repair cut short and made again can give a row twice, never none, and so can the `I` rows of seed records of the same open |

`ENG_F_NOAUDIT` fields (UPDATED, LASTLOGIN, MUSTCHG) and `ENG_F_HIDDEN` fields (SALT, PWHASH)
produce no rows of their own.

## Users and the password hash

User names are 1–`ENG_USERLEN` characters `A-Z 0-9 _`, upper-cased on input. Passwords are
`ENG_PWMIN`–`ENG_PWMAX` characters 21h–7Eh, case-sensitive.
`PWHASH = eng_mix128(SALT + PASSWORD)`.

**`eng_mix128`.** State words `a b c d` = `67452301 EFCDAB89 98BADCFE 10325476` (hex). The
message is padded with byte `80`, then zero bytes up to a multiple of 4, then its unpadded
length as a 32-bit little-endian word. For each 32-bit little-endian word `w`: `a ^= w`, then
two rounds. The state is then saved as `t`, `ENG_HASHROUNDS` more rounds are made, and `t` is
added back word by word (`a += t.a`, …), so the final rounds cannot be run backwards from the
output. A round (all modulo 2^32):

```
a += b; d ^= a; d = rotl(d,16);   c += d; b ^= c; b = rotl(b,12);
a += b; d ^= a; d = rotl(d, 8);   c += d; b ^= c; b = rotl(b, 7);
```

The result is `a b c d`, each as 8 upper-case hex digits, most significant first. It is
integer-only and honest about its era: it keeps plain passwords out of the file and out of a
dBASE `BROWSE`, not a determined attacker out of the system. One pass over a short message and
`ENG_HASHROUNDS` rounds are quick work on an 8088 at a login and far quicker for whoever holds a
copy of `USERS.DBF`, who can try passwords against a PWHASH without the server and its limit on
failed logins; and `ENG_SALTLEN` hex digits are so few salts that the guesses can be worked out
once for all of them. What keeps the passwords is therefore the data directory, kept to the
server, and passwords used nowhere else (`DOC/SECURITY.md`).

| Message (ASCII) | eng_mix128 |
|---|---|
| (empty) | `875B0C1F68A19BA301F4C449A1452877` |
| `FFFFx` | `92BD92EDB6D9CC40C0A37A7C02FB5F39` |
| `ABCDxyz` | `6E0C4600B312D3363532CFD7396E6957` |
| `0000admin` | `A049CE34F45C0DB9864186CE4761D6AB` |
| `ADMNADMIN` | `34F3E857238FE6CC2B6FC6AFE2B903AF` |
| `1A2Bsecret` | `E2932989995E645505E7CCF6BC07E18D` |
| `C0DERetro1986` | `C95E94C0C913FDDE2978184169D1A750` |
| `9F3E0123456789ABCDEF` | `ADCE03B0219259C4AB2749F815850940` |

The seeded ADMIN user therefore holds SALT `0000` and PWHASH `A049CE34F45C0DB9864186CE4761D6AB`.

**Salt.** `ENG_SALTLEN` upper-case hex digits: the first ones of `eng_mix128` over the clock as
`YYYYMMDDHHMMSS`, the user name, the value `eng_seq_peek(e, ENG_T_AUDIT, …)` gives just before
the change's `eng_edit_begin` in decimal (the AUD_ID of the change's first audit row: its `I`
row for USER ADD, its `W` row for PASSWD and RESET) and the old PWHASH (none for a new user),
at most `ENG_MAXSEED` bytes in all. With a fixed clock the result is reproducible. The seeded
ADMIN uses the fixed salt `0000` instead. The name `SYSTEM` (`ENG_SYSTEM`) is reserved for the
rows written while opening: USER ADD of it is 201.

**Rules.** Login needs a live user with STATUS A and a LEVEL of 1..3; a malformed or unknown
name and a wrong password are all 401, a disabled user or one with another LEVEL 403. `PASSWD`
needs the old password and a valid new one different from it; it clears MUSTCHG. `USER ADD` and
`USER RESET` set MUSTCHG. `USER SET` and `USER DEL` refuse (202) any change that leaves no live,
active level-3 user. The protocol key `GROUP` maps to the field GROUPNAME, which may also be
named by its own name; both in one request are 102. A user's level and status are read at LOGIN,
so changes take effect at the next LOGIN.

**Logins with a ticket** (`USERTK.C`). A login ticket keeps the tag of the PWHASH its user had
when it was made, the first `ENG_TAGLEN` hex digits (`eng_user_hash` gives the whole PWHASH).
`eng_user_resume` logs the user in when the tag is the start of the user's whole PWHASH; the
salt, and so PWHASH, changes with every PASSWD and USER RESET, which thus revoke the user's
tickets. The tag is checked before the status, as the password is, and the record is read once.
It reads LEVEL, STATUS and MUSTCHG anew and writes neither an `L` row nor LASTLOGIN, which is so
the date of the last login with a password.

## Items, locations and code tables

The values these commands take and the texts of their refusals are in `DOC/PROTOCOL.md` (Item
fields, filters, ITEM TAGS and ITEM SPLIT); the rules behind them:

- **ITEM ADD** requires CATEGORY (a live category); LOC_ID, a location token (below), is empty,
  0 (unknown) or a live location; COND and WORKS hold the codes of their fields (ITEMS.DBF,
  above); STATUS A or D; QTY, ACQ_PRICE and CUR_VALUE are not negative. Defaults: QTY 1, COND
  blank, WORKS `?`, STATUS A, LOC_ID 0, PARENT_ID 0, the counters (Loans, reservations and
  in-use equipment) 0, CREATED and UPDATED today. ADD, SET and IMPORT keep the same rules and
  store COND and WORKS upper case. A COND or WORKS another program left in lower case is
  upper-cased by the item's next commit, with its `U` row; any other COND, WORKS or TAGS outside
  the rules blocks the item's next SET (102 naming it) until it is set right. WORKS is
  independent of availability: AVAIL and the commands that lend, reserve, use or install an item
  ignore it.
- **Tags** (TAGS) are the user's own words for the collection an item belongs to (Retro, a
  project); the engine never infers one. A commit leaves out a repeated tag, keeping its first
  spelling, so the stored text is never longer than the one given. Every comparison of tags
  folds A-Z.
- **ITEM SET**, **DEL**, **UNDEL**, **NOTES ADD=** and **NOTES END=** set UPDATED. ITEM SET
  refuses STATUS D while the item holds units, has equipment attached or is installed
  (`eng_item_hold`), and a QTY below the units held; a LOC_ID change on an item with a parent or
  children is 202 naming LOC_ID (assemblies move as a whole).
- **ITEM DEL** is 202 while the item has live children (ITEMS3) or `ENG_MAXREADS` reads of
  deleted children have not settled that, or while a counter is above 0 (`eng_item_hold`). **ITEM
  UNDEL** works only on a deleted item (202 for a live one, 200 for an unknown one), and is 202
  if its category or parent, or for a top-level item its location, is no longer live; an
  installed item takes its parent's current LOC_ID (audited as `M`). GET of a deleted item is
  200.
- **ITEM COPY** duplicates fields, tags and notes into a new top-level item, another unit
  neither graded nor tested: SERIALNO and COND blank, WORKS `?`, the counters 0 and today's
  dates.
- **ITEM SPLIT** takes `QTY=` units off an item into a copy as ITEM COPY makes it, but keeping
  COND and WORKS (the lot was graded and tested as a whole), holding that QTY and the item's
  LOC_ID (an installed item's is its top-level item's); the item keeps the rest, its PARENT_ID,
  parts, loans and serial, and gets UPDATED. QTY= is above 0 and leaves the item at least one
  unit and its held units. It is a batch naming ITEMS: the new item's `I` row and
  the item's `U` row of QTY, in that order, then the new item's quiet commit, a flush of every
  file, and only then the item's rewrite, quiet too. So a stop leaves nothing, the whole split,
  or the new item and both rows on disk without the lower QTY; the next open's `eng_item_heal`
  finds that last case by the rows the marker covers. It takes the last row of an ITEMS QTY by a
  user (not `SYSTEM`); only when the row just before it is the ITEMS `I` row of a live item, and
  the QTY row's item is live, holds the row's old QTY, and that less the new item's QTY is the
  row's new QTY and no less than the units its open rows hold (`eng_item_held`), does it give
  the item that new QTY. A later change of any QTY in those rows (a session that went on after a
  failed split) leaves the split as it is.
- **ITEM LIST** reads ITEMS1 for ORDER=ID, ITEMS2 (LOC_ID, then ITEM_ID) for LOC, scanning only
  the location's prefix when `LOC=` is given, ITEMS4 for MFR and ITEMS5 for SERIAL. `AVAIL=`
  tests `eng_item_avail`. `TAG=` and `WORKS=` have no index: each is one more test of a record
  the scan has read already. The filters are filled once (`eng_item_filters`, into an
  `eng_ifilt`) and are one match function (`eng_item_keep`), shared by ITEM LIST, ITEM FIND,
  REPORT ITEMS and the walks of the location tree.
- **ITEM FIND** scans ITEMS in record order, its text one more filter; with `ALL=1` it walks the
  tree below `LOC=` as ITEM LIST does.
- **ITEM TAGS** (`eng_item_tags`) gathers a request's tags in `eng_memobuf`, at most
  `ENG_TAGSET` bytes (a tag and its NUL each), kept in A-Z folded order: a binary search finds
  each tag's place, and a record whose TAGS equal the record before's is passed over, so a
  request costs little more than an ITEM FIND's scan, not a pass over the set per tag. The
  request ends when fewer than one record's worth of tags (a full TAGS and its NUL) would still
  fit, so every request moves on by at least one record and the next one resumes there.
- **ITEM SERIAL** reads ITEMS5, paged, as serials need not be unique.

**Locations** (`LOCS.C`, `ENGLOCS.H`) form a tree, the storage of the collection: a room holds
shelves, a shelf bins, and an item may sit at any level. A location is known by its LOC_ID,
issued by SEQ and never changed, which items (ITEMS.LOC_ID, 0 for unknown) and audit rows
(REC_ID) name. Every ADD and SET keeps these rules, each looked up before the edit begins (the
texts of their refusals: `DOC/PROTOCOL.md`, Locations and location types):

- PARENT_ID is 0 for a top-level location, at depth 1, or a live location. Setting it moves the
  location with everything below it, locations and items, by writing the location alone; the
  new parent is neither the location itself nor one below it, and no move or ADD leaves any
  location deeper than `ENG_MAXLOCDEPTH`, the number the refusal's text gives (`ENG_TXNUM`).
- LOC_NAME is required and unique among the live locations of one parent, A–Z folded, so a path
  names one location.
- LOC_CODE is optional: blank, or a code of the form `DOC/PROTOCOL.md` (Locations and location
  types) gives, upper-cased and unique among the live locations. It may be changed or cleared;
  the LOC_ID stays.
- LTYPE is required and names a live location type; CAPACITY is informational.

A **location token** is how a request names a location: its LOC_ID or its code, as
`DOC/PROTOCOL.md` (Commands) gives the token's form and where 0 stands for none.

- **LOC GET** builds its `PATH` with `eng_loc_path`.
- **LOC DEL** is 202 while a live item is at the location (ITEMS2) or a live location is in it
  (LOCS2), or while `ENG_MAXREADS` reads of deleted ones have not settled either. A deleted
  location's name and code may be used again.
- **LOC LIST** and **LOC CONTENTS** walk the tree (`eng_lwalk`) depth first, the locations of one
  parent in name order (LOCS2), and at each location its items by ITEM_ID (ITEMS2), each
  location's depth counted from the top. The same walk serves ITEM LIST, ITEM FIND and REPORT
  ITEMS with `ALL=1`: `eng_loc_contents` takes the item filters of `eng_item_scan` (FIND's text,
  CAT, STATUS, TAG, WORKS, `ENG_IF_TOP`, `ENG_IF_AVAIL`, `ENG_IF_UNAVAIL`; `eng_item_filters`,
  `eng_item_keep`) and passes over the items that fail them, each still counted toward
  `ENG_MAXREADS`. Each step seeks afresh above the last entry's key, keeping no index position
  between steps. Every entry examined, deleted ones included, counts one toward `ENG_MAXREADS`,
  its record read included; restoring the place from a cursor (the cursor's location and its
  ancestors, at most `ENG_MAXLOCDEPTH` + 1 reads) counts nothing. The cursor names the last entry
  examined: for LOC LIST a LOCS2 cursor, for LOC CONTENTS an ITEMS2 cursor whose key is the
  location's STR(LOC_ID,8) and the item's ID (0 on arriving at a location). A cursor whose
  location is not the walk's root or below it is refused; the answers, and what a change of the
  tree between two requests does to the walk, are in `DOC/PROTOCOL.md` (Locations and location
  types).
- **Damaged trees.** Another program may leave a PARENT_ID that names no live location, a loop,
  or a chain deeper than `ENG_MAXLOCDEPTH`. Every walk up or down is bounded by the depth limit
  and reports such damage as 300 (`location tree damaged`) instead of following it;
  `TEST/CHKDBF.PY` finds it in the files.

**Code tables** (`CODES.C`, `ENGCODE.H`): the categories, `CATS.DBF`, and the location types,
`LTYPES.DBF`, each a code, a name and a SORTORD, kept by one implementation. A code, of the form
`DOC/PROTOCOL.md` (Locations and location types) gives, is stored upper case (`eng_code_norm`)
and unique among the live rows. A table holds at most `ENG_MAXCODES` records, deleted ones
included, which bounds every lookup. ADD on a full table reuses the first deleted row (audited
as an `I` row with that record number), and is 202 only when every row is live. SORTORD defaults
to `ENG_CODESORTDEF` and cannot be made blank (102). DEL is 202 while a live item has the
category or a live location the type; neither field has an index, so DEL reads the whole of
ITEMS.DBF or LOCS.DBF, an accepted exception to the bounded-work rule for a rare operation.

## Assemblies

An item is installed in another through `PARENT_ID` (0: top level); ITEMS3
(`STR(PARENT_ID,8)+STR(ITEM_ID,8)`) lists an item's parts in ID order. An *assembly* is a
top-level item and everything installed in it, at any depth. The rules (`ASSY.C`; the texts of
their refusals: `DOC/PROTOCOL.md`, Assemblies, maintenance, loans and holds):

- **No cycle.** INSTALL walks the parent's ancestors (at most `ENG_MAXDEPTH`); a parent that is
  the child or one of the child's parts is refused.
- **Depth.** A top-level item is at level 1; no part lies below level `ENG_MAXDEPTH`.
- **Size.** An assembly holds at most `ENG_MAXASSY` ITEMS3 entries, deleted parts included until
  PACK, so every walk of one is bounded. INSTALL counts the parent's assembly and the child's
  parts, each count stopping once past `ENG_MAXASSY`; the counts and the child's height include
  deleted parts, so a later UNDEL can break neither limit.
- **Location.** Every installed item has its top-level item's LOC_ID, so the contents of a
  location (ITEMS2, `LOC CONTENTS`) include installed parts; ASSY MOVE moves a whole assembly. A
  top-level LOC_ID that is negative or not a number (written by another program) makes INSTALL
  into it 300 `FIELD=PARENT`.
- **Loans.** An item with units lent, reserved or in use is not installed (`eng_item_hold`), and
  only top-level items are held, so held units imply PARENT_ID = 0; an assembly is lent whole.
  Nor is a host put inside the item it serves: when the top-level item the child joins has open
  reservations or assignments (RESERVED + IN_USE above 0), its LOANS1 entries are read, ended
  rows skipped, until its open rows hold those units, and one naming the child or one of its
  parts as HOST_ID refuses the install (`eng_loan_hosted`). A walk that would read more than
  `ENG_MAXREADS` of that item's own entries is refused rather than passed.
- **Damaged data.** A PARENT_ID that is negative or not a number names no item: the item counts
  as installed with a broken chain (ASSY PATH and INSTALL of it are 300, MOVE is 202 `FIELD=ID`,
  REMOVE makes it top level). A chain is read up to `ENG_MAXDEPTH` items, so a PARENT_ID cycle
  reads as a chain too deep; a walk down that meets its own item again is 300.

| Command | Effect |
|---|---|
| `ASSY INSTALL child parent` | Both live. The child, top level or installed elsewhere, gets PARENT_ID = parent; it and its parts take the parent's location. Installing a child in its own parent again changes nothing. |
| `ASSY REMOVE child LOC=loc` | The installed child becomes top level at the location token loc, a live location or 0 (unknown), with its parts. |
| `ASSY MOVE id LOC=loc` | The top-level item and all its parts move to the location loc, as for REMOVE; items already there are not written. |
| `ASSY BOM id` | Paged walk of the parts (Walks, below), each part before its own parts. Deleted parts are skipped. |
| `ASSY PATH id` | The ancestors, top-level item first; unpaged, its byte bound a compile-time check in `DPCSV.C`. |
| `ASSY CHILDREN id` | The direct parts: a paged ITEMS3 prefix scan. |
| `ASSY LIST [TOP=1]` | The items with a live part: a paged walk of ITEMS3 (Walks, below). |

A change of several items is one batch naming ITEMS, one commit per changed item, the parts
first and the item named last; each gets UPDATED stamped. If it is cut off, the next open
rebuilds the ITEMS indexes and `eng_assy_heal` moves every installed item back to its top-level
item's location, so an assembly change is all or nothing as seen after the next open. Each
part's `M` row is flushed before the part is rewritten, so it survives a stop and is flagged by
the next open's `X` row; each part moved back gets an `M` row by `SYSTEM`.

**Walks** (`eng_walk`). BOM and LIST are not one key range, so they walk ITEMS3 themselves:
every entry examined counts one toward `ENG_MAXREADS`, its record read included; restoring the
position from a cursor counts nothing (for BOM, reading the cursor item's ancestors back to the
BOM's item, at most `ENG_MAXDEPTH` reads). Their cursors are ITEMS3 cursors naming the last
entry examined (`eng_cursor_text`); for LIST, once an item is decided its cursor key is its ID
followed by `ENG_IDLEN` `9` digits, so the next request starts with the next item. A LIST walk
examines an entry only while two of its reads remain (the entry and the item it may decide); a
cursor naming a deleted part resumes under the same, still undecided, item; an entry whose
parent is not an ID is counted and passed over; any well-formed ITEMS3 cursor is taken. A BOM
cursor whose item is not below the BOM's item (ancestors read live or deleted) is 204.

## Maintenance records

One `MAINT.DBF` record per repair, cleaning, recap, test, modification or inspection of an item
(MAINT.DBF, above; the commands' keys and refusals: `DOC/PROTOCOL.md`, Assemblies, maintenance,
loans and holds), with MDATE (today when not given; an empty `MDATE=` stores a blank date,
which MAINT LIST puts first) and COST (not negative). ITEM_ID is set by ADD and never changes;
MAINT_ID comes from SEQ.

There is no index on MAINT_ID or LOAN_ID: IDs are appended in increasing order and PACK keeps
the order, so `eng_id_find` binary-searches the record numbers (about log2 of the record count
reads), with no `.NDX` file and no DOS handle. A probed record whose ID is blank, not a number,
zero or negative stands for the nearest readable one below it (at most `ENG_IDSTEP` down; none
is 300). This rests on an operator rule: MAINT.DBF and LOANS.DBF are only appended to, with
ascending IDs, never reused or renumbered by hand in dBASE; `TEST/CHKDBF.PY` checks the order.

MAINT LIST reads the item's MAINT1 prefix; MAINT RECENT scans from the last record backwards
(`ENG_SCAN_RRECORDS`). MAINT ADD and MAINT LIST for an item that is not live are 200
`FIELD=ITEM`; deleting an item leaves its records, which MAINT GET, SET, DEL, NOTES and RECENT
still reach.

## Loans, reservations and in-use equipment

One `LOANS.DBF` record, a *row*, takes QTY units of an item out of stock from OUT_DATE (today
when not given or empty) until RET_DATE is set; it is open while RET_DATE is blank. Its KIND
says how (`ENG_K_` of `ENGLOAN.H`; the commands' keys and refusals: `DOC/PROTOCOL.md`,
Assemblies, maintenance, loans and holds):

| KIND | Command | HOLDER | HOST_ID | DUE_DATE |
|---|---|---|---|---|
| `L` loan | `LOAN OUT` (also `LOAN ADD`) | the borrower, required | always 0 | required, not before OUT_DATE |
| `R` reservation | `LOAN RESERVE` | who or what it is for | the item it is for | optional, "until"; not before OUT_DATE |
| `U` in use | `LOAN USE` | what it is used with | the item it is used with | always blank |

- A new row needs a live, top-level item with STATUS A, a QTY (default 1, at least 1) not above
  the item's QTY less its held units, ON_LOAN + RESERVED + IN_USE, and, for R and U, a HOLDER or
  a HOST_ID.
- A HOST_ID is 0 or a live item with STATUS A that is not the row's item nor one of its parts;
  the item, being top-level, has no ancestors.
  Equipment keeps its own LOC_ID: moving or renaming the host moves nothing.
- **RETURN** sets RET_DATE (default today, not before OUT_DATE) of a row of any kind, the
  release of a reservation or an assignment; a row already ended is refused.
- **SET** changes HOLDER, CONTACT, NOTES, DUE_DATE (L and R) and HOST_ID (R and U) under the
  same rules; KIND, ITEM_ID, QTY and the dates other than DUE_DATE are not settable.

**Counters.** ITEMS.ON_LOAN, RESERVED and IN_USE of every item are the sums of the QTY of its
open L, R and U rows, and ATTACHED the number of open R and U rows naming it as HOST_ID. A new
row, a return and a SET that moves an open row to another host write the row, then the item's
counter, then the hosts' ATTACHED, each a commit with UPDATED stamped, in one batch naming LOANS;
after a stop between them the next open recomputes every counter (`eng_loan_heal`). The counters'
changes are audited as `O` or `R` through their `ENG_F_AUDLOAN` flag. While units are held, or
equipment is attached, the item rules refuse ITEM SET STATUS=D, ITEM DEL and ASSY INSTALL of the
item (`eng_item_hold`); they also refuse ITEM SET of a QTY below the units held and ITEM SPLIT of
held units. An item is *available* (`eng_item_avail`, the `AVAIL=` filter and the `AVAIL` column
of the item lists) when its STATUS is A, it is top level and its QTY is above its held units:
ending one row makes it available again only when nothing else holds all its units.

**Lists** (their rows, orders and filters: `DOC/PROTOCOL.md`, Assemblies, maintenance, loans and
holds). LOANS2 is `STR(YEAR(RET_DATE),4)`, the due date and the row's ID, so the open rows (year
`   0`) come first. LOAN LIST without ITEM= reads LOANS2, `OPEN=1` scanning only the `   0`
prefix; with ITEM= the item's LOANS1 prefix, `OPEN=1` filtering. `HOST=`, `KIND=` and `HOLDER=`
are filters of the same scans, as no index holds HOST_ID or HOLDER (a host walks the open
prefix), and the rows they pass over count toward `ENG_MAXREADS` as any other. LOAN OVERDUE
scans the open prefix with the upper bound `   0` plus ASOF (`eng_scan.bound`), so it ends at
the first row due on or after ASOF; open rows with a blank DUE_DATE (every U row) sort first
and are skipped. Each row's item MFR and MODEL come from one ITEMS1 lookup per row returned
(`eng_loan_item`), bounded by the page size and not counted toward `ENG_MAXREADS`. REPORT LOANS
checks ASOF first (`eng_loan_asof_ok`), then opens one scan of the open rows (with OVERDUE=1 the
LOAN OVERDUE range).

## Cursors and bounded scans

A cursor names the last entry a request examined: `index:hexkey:recno`.

- `index` is the index name (`ITEMS1`..`ITEMS5`, `LOCS2`, `USERS1`, `MAINT1`, `LOANS1`,
  `LOANS2`, `AUDIT1`, `AUDIT2`) or, for a scan in record order (ITEM FIND and ITEM TAGS, and MAINT
  RECENT and AUDIT LIST, which run backwards), the table name (`ITEMS`, `MAINT`, `AUDIT`).
- `hexkey` is the key in hex, two digits per byte, trailing spaces left off (empty in record
  order and for an all-blank key) and padded back on reading.
- `recno` is the record number in hex.

Cursors are written in upper case without leading zeros. On input, names and hex digits are
case-insensitive and leading zeros accepted; `recno` has 1–8 digits and is not 0; `hexkey` has
an even number of digits, at most twice the index's key length. A cursor that breaks these
rules, names another index or table than the command's scan, or whose key does not start with
the scan's prefix (an ITEM LIST cursor of another `LOC=`) is 204; the walks of the location tree
check theirs likewise (Items, locations and code tables). Example: the ITEMS4 key `IBM` + 27
spaces + `5150` + 26 spaces at record 12 is `ITEMS4:49424D` + `20` × 27 + `35313530:C`.

The server keeps no state: the request repeats its filters, and `eng_scan_open` seeks the first
entry after (key, recno) with `ndx_seek_after`. Equal keys are kept in record-number order, and
the index descent binary-searches the child pages of a long run of equal keys (many blank
serials on ITEMS5, many items of one make and model on ITEMS4), so positioning reads a few pages
whatever the run's length, and counts nothing. After that every entry examined counts one,
returned, deleted or filtered out, its record read included, and the scan stops before an entry
once `ENG_MAXREADS` are counted. So each request moves the cursor at least one entry, and a page
ends after `PAGE=` returned entries or `ENG_MAXREADS` examined ones. The paging helper
`proto_send_page` sends at most `PAGE=` (1–`PROTO_PAGEMAX`, default `PROTO_PAGEDEF`) `R` lines,
stops early when the sink reports less than `PROTO_ROOMMIN` bytes of room or the scan stops, and
sends `C <cursor>` when `eng_scan_more` finds another entry in the range (without a record
read). A final page may be empty when only filtered-out entries remained.

## CSV export and import

`CSV.C` (`ENGCSV.H`) writes and reads the CSV files of Text files (CSV), above; the
commands' wire forms and errors are in `DOC/PROTOCOL.md` (Reports, export and import). Lines
are built and read in `eng_memobuf`, so memo fields are never exported or imported.

**Paths** (`eng_csv_path`, through `txt_outfile_ok` of `COMPAT.H`, which also judges the name of
a report's file in the clients): 1..`ENG_CSVPATH` characters without control bytes, spaces or the
bytes DOS refuses in names, `" * + , ; < = > ? [ ] |`. The last component is judged as DOS reads
it: a base name of at least one character, at most one dot, no extension whose first three
characters (all DOS keeps of a longer one) are DBF, DBT or NDX or that starts with `&`, `$`, `%`
or `#` (the scratch files of the engine and of an export, and `ENG_EDITMARK`), and no DOS device
as its base name, cut to eight characters and in any case (CON AUX PRN NUL COM1..COM4 LPT1..LPT3
CLOCK$, which DOS finds in every directory with any extension). `eng_csv_table` judges the table,
among those each command takes.

**Export** (`eng_export`) is paged through a record-order scan whose read count starts at
`ENG_MAXREADS - ENG_EXPROWS`, and its cursor is that scan's. The lines go, one `cf_write` each,
to a scratch file: the target's name with the extension `ENG_CSVSCRATCH` and a hex digit, the
export's tag (`ITEMS.&C3`), which the protocol takes from its session's slot (0 in local mode).
The request that finds the table exhausted writes the Ctrl-Z, checks the target again and
renames the scratch file to it. A request that fails cuts the scratch file back to its size
before the request. A stop leaves the scratch file, which the next export with that tag and base
name replaces, never a partial target; between the final remove and rename the complete export
is in the scratch file. Export writes no audit row and uses one DOS handle.

**Import** (`eng_import`) is one request, allowed only while no other session is open. The file
is opened deny-write and read through a window in `eng_memobuf`, twice for ITEMS and more often
for LOCS (below); a line whose end does not come within `ENG_CSVLINE` bytes is refused as too
long, and one holding a byte below 20h other than TAB as a bad character (102 naming LINE), so
a NUL never cuts a line's values short.

- **Parents in a LOCS file.** A parent may be on another line of the file, before or after its
  child. A PARENT_ID that is a code names a live location or else the line with that code. A
  number depends on the file's columns: when it has a LOC_ID column (a file without a header
  always has one), each LOC_ID value numbers its line (an ID from 1 to `ENG_MAXID`, once in the
  file; else 102 or 201 naming LOC_ID) and a number names the line of that number, 0 the top
  level, and never a live location (202 naming PARENT_ID when no line has it); without that
  column a number is a live LOC_ID. So an export imports back with its tree into any data
  directory, and no number means two things in one file.
- **First pass.** Each line is split into pairs and checked with `eng_item_check` or
  `eng_loc_check` (ITEM ADD and LOC ADD up to their commit, so no rule is kept twice). For LOCS
  a scratch index `IMPORT.&NX` in the data directory, removed before and after the import, keeps
  what the lines will add. A labelling read gathers every line's LOC_ID and code (each once in
  the file); then sweeps over the file check each line whose parent is a live location, the top
  level or a line already checked, with the depth it gets there, keep its name under its parent
  and what became of it: accepted with its sweep and depth, or refused. A chain of lines in any
  order is checked one level per sweep at worst, so at most `ENG_MAXLOCDEPTH` + 1 sweeps run,
  and fewer when no line is left waiting or a sweep decides none. The lines left are refused
  with 202 naming PARENT_ID: in a loop of lines (`ENG_TX_CYCLE`), below a refused line
  ("unknown location") or in a chain too long (`ENG_TX_LOCDEEP`). A file that lists parents
  first is read twice by the first pass and once by the second. A code below 300 refuses the
  line, reported through the callback with its line number, field and text, LOCS lines in the
  order they are found, at most `ENG_IMPERRS` of them and all counted; a code from 300 up ends
  the import. Nothing is written when a line was refused, or when the data disk has less than
  `ENG_IMPSLACK` + lines × `ENG_IMPROWCOST` bytes free.
- **Second pass.** One batch naming the table: the `K` audit row, then one ITEM ADD or LOC ADD
  per line, each with its `I` row. LOCS lines are added sweep by sweep, each sweep in file order
  as the first pass took them, so parents come before children and the first pass refused
  exactly what this one would; the scratch index keeps the LOC_ID each numbered line was added
  as, and a PARENT_ID of that number is given it. No commit inside the batch rewrites the
  table's header, so after a stop before `eng_batch_end` the next open cuts the uncounted
  records, rebuilds the table's indexes and flags with an `X` row the import's audit rows on the
  disk: none of the records remain (`TEST/T_CSVF.C` stops the machine at every disk-changing call
  to prove it). A failure in this pass (300 range) ends the batch with its records flushed and
  returns the code with the count added.

Import uses two DOS handles besides the engine's: the file and, for LOCS, the scratch index.

## Protocol layer

The wire rules (framing, tokenizer, quoting, sessions and login, responses) are in
`DOC/PROTOCOL.md`; this section covers how the layer implements them.

**Host.** A transport describes the program around the engine in a `proto_host`: the engine `e`,
the read-only stations `readonly`, extra STATUS lines (`status`; STATUS itself is in
`DOC/PROTOCOL.md`, Commands), `others`, the number of other open sessions it counts before each
request, the `peers` that ADMIN CLIENTS lists, the `stop` that ADMIN SHUTDOWN calls after its
`+OK`, and the server's login table `tickets`: its login tickets and its failed-login throttle
(`DOC/SERVER.md`, Login tickets and Failed logins), which `DPTKT.C`'s LOGIN consults, a user the
throttle refuses having the password checked as a wrong one. Local mode sets only `e`: no
capped station, no host STATUS lines, no other sessions, no `peers`, `stop` or login table.

**Lines.** `proto_acc_feed` takes bytes 0–255, ends a line at LF (dropping a CR before it) and
swallows an over-long line up to its LF, so the transport answers 103 and stays in step.
`proto_parse` tokenizes; an empty line, or one of only spaces and tabs, makes `proto_dispatch`
return `PROTO_R_NONE` and write nothing. On a transport that fixes the station (`sess_init`
given one), HELLO still checks its argument, but the transport's station is used.

**Limits.** Compile-time checks in `DPCSV.C` show that the longest requests fit `PROTO_MAXLINE`
(the paged lists with every option, ITEM SERIAL and ITEM FIND with a fully quoted text, EXPORT
with a quoted path; the limits note in `PROTO.H` names their parts); a request that escapes more
than it needs can pass it and is refused with 103. Response lines stay within `PROTO_MAXRESP`: a
`FIELDS=` list totals at most `ENG_MAXRWIDTH` characters of field width and its computed columns
(`DPCMD.H`: LOC_NAME and AVAIL of an item row) at most `DP_XWIDTH` more, which even fully quoted
stay below it; LOC GET's PATH line (`DPLOC.C`) and the fixed columns of LOC LIST fit it too; and
memo lines are cut by `PROTO_MEMOLINE`. The writer (`proto_out`, over the caller's `write` and
optional `room`) leaves out whole any part that would pass `PROTO_MAXRESP`, so a value is never
cut inside. An unpaged response (GET, NOTES, CAT LIST, LTYPE LIST, ASSY PATH, ADMIN CLIENTS,
WHOAMI, STATUS) is at most `PROTO_MAXUNPAGED` bytes, which transports size their output for.

**Sessions** (`SESSION.C`) move NEW → HELLO → (MUSTCHG) → READY → CLOSED. A failed LOGIN (once
its arguments are present) logs the session out to HELLO and counts; the `PROTO_MAXFAIL`-th is
answered and returns `PROTO_R_CLOSE`. Only `sess_init` and a successful LOGIN reset the count.
LOGIN fixes the level: the user's, capped at 1 for a station in `proto_host.readonly`. A session
whose transport offers login tickets (`tkok`) holds the number of its ticket, `tk`; a failed
login with a ticket closes it at once (`sess_bye`).

**Dispatch** (`DISPATCH.C`). `proto_dispatch` clears the engine error, parses the line, looks
the command up (the verb, and the next word for USER ITEM CAT LTYPE LOC ASSY MAINT LOAN REPORT
AUDIT ADMIN; an entry without a second word, such as EXPORT and IMPORT, matches whatever
follows; the first match is taken), sets the actor, runs the gate, then the handler: for LOGIN,
PASSWD and BYE on a host with a login table, the host's ticket command (`DPTKT.C`). Lookup
comes first, so an unknown command, a group word alone or with an unknown word (`ASSY`, `REPORT
BINS`) included, is 101 in every state. The gate (`proto_gate`), in order:

1. a CLOSED session runs nothing;
2. `PROTO_C_ANY` commands (HELLO BYE PING VER) run in every open state;
3. any other command is 300 while the engine holds no file (`eng_is_open`, after an ADMIN
   command failed to open the data again);
4. LOGIN needs HELLO (400); everything else needs a login (400) and no pending MUSTCHG (404)
   unless flagged `PROTO_C_MUSTCHG` (PASSWD);
5. the level (203); for a `PROTO_C_KEYOPER` command (ITEM, LOC and MAINT NOTES) a request with
   any pair (`ADD=`, `NEW=`, `PART=`, `END=`; a read has none) also needs `ENG_LVL_OPER` (203);
6. a `PROTO_C_EXCL` command, whose work is not bounded per request, is 402 while
   `proto_host.others` is not 0.

| Level | Commands (`cmds[]` of `DISPATCH.C`, in its order) |
|---|---|
| none | HELLO, BYE, PING, VER; LOGIN after HELLO |
| 1 | PASSWD (also while MUSTCHG is pending), WHOAMI, STATUS; ITEM GET LIST FIND SERIAL NOTES TAGS; CAT LIST; LTYPE LIST; LOC GET LIST CONTENTS NOTES; ASSY LIST BOM PATH CHILDREN; MAINT GET LIST RECENT NOTES; LOAN GET LIST OVERDUE; REPORT ITEMS LOANS |
| 2 | ITEM ADD SET DEL UNDEL COPY SPLIT; ITEM/LOC/MAINT NOTES with a pair (`PROTO_C_KEYOPER`); CAT ADD SET DEL; LTYPE ADD SET DEL; LOC ADD SET DEL; ASSY INSTALL REMOVE MOVE; MAINT ADD SET DEL; LOAN OUT ADD RESERVE USE RETURN SET |
| 3 | USER LIST GET ADD SET RESET DEL; EXPORT; IMPORT (`PROTO_C_EXCL`); AUDIT LIST; ADMIN CLIENTS FLUSH SHUTDOWN; ADMIN PACK REINDEX BACKUP RESTORE (`PROTO_C_EXCL`) |

This table is the one statement of the command levels; other documents refer to it. While
MUSTCHG is pending, HELLO, PING, VER, BYE, LOGIN and PASSWD run; others give 404.

A handler writes the whole response and returns `ENG_OK`, or returns an `ENG_E` code having
written nothing, and `proto_dispatch` writes `-ERR code text [FIELD=name]`; a failure after
`+DATA` has begun ends the block with `K ERROR=<code>` before `.`.

## Administration

`ADMIN.C` (`ENGADMIN.H`) gives the audit list and the four commands that work on the data files
as a whole; their wire forms and errors are in `DOC/PROTOCOL.md` (Administration).

**AUDIT LIST** (`eng_audit_scan`) is a bounded scan: without a table, over AUDIT.DBF from the
last record backwards (`ENG_SCAN_RRECORDS`); with one, the AUDIT1 prefix of the table name padded
to `ENG_TBLNAMELEN`, and with a record also its `STR(REC_ID,8)`; with a user and no table, the
AUDIT2 prefix of the user name, upper-cased and padded to `ENG_USERLEN`.

**PACK, REINDEX, BACKUP, RESTORE** (`eng_admin`, `PROTO_C_EXCL`) close every file of the engine
(`eng_shut`, under `eng_close`'s rule), work on the files by name, and open them again whatever
happened (`eng_reopen`: the steps of `eng_open_files` on the same engine, then
`eng_init_data`), so the program's engine pointer stays valid; the request's actor is put back.

- PACK names every table in the marker (added to one a failed change left, so its rows are still
  flagged) and runs `dbf_pack` on each; the reopen rebuilds every index and makes the repairs. A
  stop leaves each table packed or not (`PACK.C` recovers it) and the marker for the next open.
- REINDEX names every table in the marker and reopens, which builds every index afresh.
- BACKUP and RESTORE first, when a failed change has left the marker (`eng_marker_on`), reopen
  and shut the files, which makes its repairs, so a backup never holds unrepaired tables and a
  RESTORE that fails after its new marker loses none of the old one's repairs.
- BACKUP and RESTORE copy through `eng_memobuf`, in whole sectors, under the scratch names
  (`.&DB`, `.&DT`, `.&NX`) in the target directory, then write the commit mark `ENG_COPYMARK`
  there. BACKUP copies every table, memo file and index and renames the copies into place
  (`eng_put_copies`, which removes the mark last). RESTORE first checks the backup (every table
  opens read-only with the schema's fields, `eng_layout`, and every memo file opens), copies the
  tables and memo files, names every table in a new marker before the mark, and leaves the
  renames to its reopen (`eng_open_files`, Recovers, under Opening a data directory), which also
  finishes one a stop cut off after the mark; the reopen rebuilds every index and repairs the
  counters of ITEMS and the assembly locations. A stop before the mark changes nothing in place.
- Before anything changes, a scratch file made through one prefix (the target's for BACKUP, the
  data directory's for RESTORE) that is seen, and then gone once removed, through the other
  shows the two name one directory.

## Memory per open engine

The near heap an open engine holds, in the medium model (2-byte data pointers, 2-byte
allocation header), with each structure's `sizeof` under Open Watcom `-mm` and the blocks of an
open on a fresh data directory:

| Part | Bytes |
|---|---|
| 9 `dbf` handles (34 B each), 81 field descriptors (16 B each), record buffers (record length + 1 each, 1098 B in all) | 2 754 |
| 3 `dbt` handles (10 B each) | 36 |
| 14 `ndx` handles (234 B each), their stored key expressions (516 B in all) | 3 848 |
| Shared NDX page pool, one block: `NDX_NPOOL` pages of `NDX_PAGE` bytes, 624 B split scratch, 200 B key scratch, 96 B insertion path | 3 994 |
| 14 compiled key expressions: 14 `kx` (4 B), 86 nodes (18 B), 5 numeric constants (12 B), 4 `RIGHT` buffers (3 B) | 1 894 |
| Engine context (2 374 B): the memo buffer (`ENG_MAXMEMO` + 1), the path of the file being opened (`ENG_MAXPATH`), the directory prefix (`ENG_MAXDIR` + 1), the last numbered text (`ENG_TXMAX` + 1), station, user and error field, handle pointers and flags | 2 376 |
| DOS handle table (`ENG_PROCHANDLES` bytes, `dos_set_handles`) | 45 |
| **Total** | **14 947** |

The open edit's state (540 B: the record image of `ENG_MAXREC` bytes, the index keys of
`ENG_MAXKEYS` bytes and each field's audit action) is static near data in `EDIT.C`.

A program that links `CMEM.C` (`INV`, `INVSERV` and the screen tests) keeps its near heap, under
Turbo C and Open Watcom, to `CMEM_NEARHEAP` bytes (`COMPAT.H`, set by the program's near-heap
module, which `CMEM.C` names; INV under Turbo C to the smaller `CMEM_INVHEAP` of `CMEMHI.C`) so
that the rest of DGROUP goes to the far heap. Under Open Watcom `cmem_init` grows the heap by
that much before the first far block, and a near allocation beyond it fails as out of memory.
Under Turbo C it is `_heaplen`, a reservation below the stack that the allocator does not
enforce; a heap that outgrew it would take stack space, which the golden-screen runs' stack
checks report. Under Watcom C 6.5 `cmem_init` grows the near heap to all of DGROUP, since that
library's far allocator takes blocks from free near heap once DOS has none left.

On the stack per request (`sizeof` under Open Watcom `-mm`): `proto_req` 454 B, `proto_call`
28 B, `eng_scan` 226 B (for LOC LIST, LOC CONTENTS and the item lists with `ALL=1` `eng_lwalk`,
518 B), value buffers of `ENG_MAXVAL` bytes, and in a commit the old and new text of one audit
row (2 × (`ENG_AUDVAL` + 1) bytes). The transport owns the `proto_out` (530 B; in local mode
part of the block `LINK.C` allocates). Every DOS build runs with the stack of `_stklen` in
`SRC/COMPAT/CDOS.C`.

Static near data: the largest parts are the command table (`DISPATCH.C`), the open edit's state
(`EDIT.C`) and the near schema (`SCHEMA.C`); every numbered text is far data. Each module's
budget is in `TEST/NEARBUD.TXT`, which `TEST/OMFNEAR.PY` checks on `INV`'s objects against the
`MAX_NEAR` of `TEST/mapchk.sh`.
