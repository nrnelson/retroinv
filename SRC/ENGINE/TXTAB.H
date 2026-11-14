/*
 * TXTAB.H - the rows of the numbered texts: every reason the engine
 * records with a failure, the why texts of an open, and the standard
 * text of every protocol error code.
 *
 * No include guard: a file that includes it first defines the row macro
 * TX(name, text), then undefines it.  ENGTX.H numbers the rows ENG_TX_name
 * from 1 in the order they stand here, and SRC/COMPAT/CSCHDAT.C stores
 * the texts outside DGROUP.  A number keeps its text for good: a new
 * text is added at the end, and its name to TEST/TXNUM.TXT, which make
 * lint checks; a row is never moved or removed.  A row macro uses its
 * name argument only with ##, so a name is never replaced by a macro of
 * that name.  No text is longer than ENG_TXMAX.  A text gives a limit
 * through ENG_TXNUM(constant) of ENGTX.H, the constant's digits, so the
 * number it states follows the constant.  A text that a program without
 * these rows needs as well is a macro of ENGTX.H, ENG_TXT_name.
 */

/* Rules of assemblies, maintenance and loans. */
TX(CYCLE, "cannot be put inside itself or its contents")
TX(DEEP, "parts nested more than " ENG_TXNUM(ENG_MAXDEPTH) " levels deep")
TX(BIG, "too many parts")
TX(ONLOAN, "item is on loan")
TX(NOTINST, "not part of an assembly")
TX(INST, "installed in an assembly")
TX(NOLOC, "unknown location")
TX(DISPOSED, "item was disposed of")
TX(NOSTOCK, "not enough on hand")
TX(RETURNED, "already returned or released")
TX(DUEOUT, "due date before out date")
TX(RETOUT, "end date before out date")
TX(NOSET, "field not settable")

/* CSV export and import. */
TX(QUOTE, "bad quoting")
TX(COLUMNS, "too many columns")
TX(LONGLINE, ENG_TXT_LONGLINE)
TX(COLUMN, "unknown column")
TX(REPCOL, "repeated column")
TX(FILEDUP, "duplicate in file")
TX(EXISTS, "file already exists")
TX(NOCSV, "table not allowed")
TX(FNAME, "bad file name")
TX(NOSPACE, "not enough disk space")
TX(NOEXPORT, "no export in progress")

/* Storage, the open and its why texts, record access. */
TX(IXBAD, "index damaged or not as the schema")
TX(TBLBAD, "table damaged or not as the schema")
TX(DISKFULL, "disk full")
TX(NOHANDLE, "too few file handles: need FILES=" ENG_TXNUM(ENG_FILES)
            " or more")
TX(INUSE, "data files in use")
TX(NOMEM, "out of memory")
TX(NOFILE, "file not found")
TX(SCHEMA, "schema error")
TX(IOERR, "database I/O error")
TX(NODIR, "no such directory")
TX(NOMARK, "cannot write the edit marker")
TX(RDMARK, "cannot read the edit marker")
TX(MEMOORPH, "memo file without its table")
TX(MEMOMISS, "memo file missing beside its table")
TX(DIRLONG, "data directory name too long")
TX(SETUP, "setting up the data directory")
TX(NOTABLE, "no such table")
TX(NOFIELD, "no such field")
TX(NOINDEX, "no such index")
TX(FLISTLONG, "field list too long")
TX(FLDEMPTY, "empty field name")
TX(MEMOLIST, "memo fields cannot be listed")
TX(MANYFLD, "too many fields")
TX(WIDEFLD, "fields too wide")
TX(NOTMEMO, "not a memo field")
TX(MEMOLONG, "memo too long")

/* The edit marker and the audit. */
TX(MARKFILE, ENG_EDITMARK)
TX(MARKTBL, "bad marker table")
TX(NOAUDID, "no audit ID reserved")

/* Scans. */
TX(BADCUR, "bad cursor")
TX(BADSCAN, "bad scan")
TX(SCANFAIL, "scan failed earlier")
TX(NOSCAN, "scan not open")

/* Edits and field values. */
TX(NOEDIT, "no edit open")
TX(EDITOPEN, "edit already open")
TX(BADTABLE, "bad table")
TX(TWOMEMO, "two memo fields")
TX(NOTDEL, "not a deleted record")
TX(UNKFIELD, "unknown field")
TX(VALLONG, "value too long")
TX(BADCHAR, "bad character")
TX(NOTNUM, "not a number")
TX(NEGATIVE, "negative value")
TX(BADMEMO, "bad memo text")
TX(BADTYPE, "bad field type")
TX(BADVALUE, "bad value")
TX(NOTNUMF, "not a number field")
TX(VALWIDE, "value too wide")
TX(MANYPAIRS, "too many pairs")
TX(REQUIRED, "value required")
TX(IXMISS, "index entry missing")
TX(NODBT, "no memo file")

/* ID numbers. */
TX(NOID, "table has no ID")
TX(NOSEQ, "SEQ row missing")
TX(IDSOUT, "no ID numbers left")

/* Items. */
TX(NOCAT, "unknown category")
TX(BADCOND, "bad condition code")
TX(STATUS, "status must be A or D")
TX(QTYLOAN, "quantity below units lent, reserved or in use")
TX(NOITEM, "no such item")
TX(KIDSMANY, "too many deleted parts; run Admin > Pack")
TX(ASSYMOVE, "assemblies move as a whole")
TX(HASKIDS, "item has parts installed: remove them first")
TX(NOTDELD, "item is not deleted")
TX(CATDEAD, "category not live")
TX(PARDEAD, "parent not live")
TX(LOCDEAD, "location not live")
TX(FILTLONG, "filter value too long")
TX(BADORDER, "bad order")
TX(BADTEXT, "bad search text")
TX(SERLONG, "serial number too long")

/* Locations and categories. */
TX(NOSUCHLOC, "no such location")
TX(BADLOC, "bad location code")
TX(LOCDUP, "location code already used")
TX(LOCFULL, "location holds items: move or delete them")
TX(LOCSMANY, "too many deleted items; run Admin > Pack")
TX(ZONELONG, "zone too long")
TX(NOSUCHCAT, "no such category")
TX(NOSORT, "sort order required")
TX(BADCAT, "bad category code")
TX(CATDUP, "category already exists")
TX(CATSFULL, "category table full")
TX(CATUSED, "items still use this category")

/* Users and logins. */
TX(NOUSER, "no such user")
TX(BADPW, "password too short, too long or not printable")
TX(LASTADM, "the last active administrator must remain")
TX(DISABLED, "account disabled")
TX(BADLOGIN, "bad user name or password")
TX(WRONGPW, "wrong password")
TX(SAMEPW, "new password must differ from the old one")
TX(BADLEVEL, "level must be 1, 2 or 3")
TX(NOTUSET, "not a user setting")
TX(BADUSER, "user name too long, empty or not A-Z 0-9 _")
TX(RESERVED, "name reserved for the system")
TX(USERDUP, "user already exists")
TX(NOLEVEL, "level required")

/* The standard texts of the error codes (proto_errtext) that no row
   above gives already. */
TX(SYNTAX, "syntax error")
TX(UNKCMD, "unknown command")
TX(BADARG, "bad or missing argument")
TX(NOTAVAIL, "command needs the server")
TX(NOTFOUND, "not found")
TX(DUP, "duplicate")
TX(CONSTR, "constraint violated")
TX(PERM, "permission denied")
TX(IXCORRUPT, "index corrupt (run REINDEX)")
TX(HANDLES, "file handles exhausted: need FILES=" ENG_TXNUM(ENG_FILES)
            " or more")
TX(INUSEBY, "data files in use by another program")
TX(NOLOGIN, "HELLO or LOGIN required")
TX(BUSY, "server busy")
TX(MUSTCHG, "password change required")
TX(INTERNAL, "internal error")
TX(SHUTDOWN, "server shutting down")
TX(ERROR, "error")

/* Administration. */
TX(DIRNAME, "bad directory name")
TX(DATADIR, "same as the data directory")
TX(SETMISS, "backup set incomplete")

/* Location types and the location tree. */
TX(NOSUCHLT, "no such location type")
TX(BADLT, "bad location type code")
TX(LTDUP, "location type already exists")
TX(LTFULL, "location type table full")
TX(LTUSED, "locations still use this location type")
TX(NOLTYPE, "unknown location type")
TX(NAMEDUP, "name already used at this level")
TX(LOCKIDS, "location holds locations: move or delete them")
TX(LOCTREE, "location tree damaged")

/* Reservations, in-use equipment and the depth of the location tree. */
TX(LOCDEEP, "locations nested more than " ENG_TXNUM(ENG_MAXLOCDEPTH)
            " levels deep")
TX(HELDRES, "item is reserved")
TX(HELDUSE, "item is in use")
TX(ATTACHED, "equipment is reserved for or in use with it")
TX(HOSTSELF, "cannot be used with itself or its own parts")
TX(NOHOLDER, "name who or which item it is for")

/* Collection tags and the works state of items. */
TX(BADTAG, "each tag 1-" ENG_TXNUM(ENG_ITAGLEN) " of A-Z a-z 0-9 - _ . / +")
TX(WORKS, "works must be Y, N, I or ?")

/* Notes saved in parts. */
TX(NOSTAGE, "no note being saved")
