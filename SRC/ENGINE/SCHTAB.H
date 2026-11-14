/*
 * SCHTAB.H - the rows of the schema: every table with its fields in
 * file order, every index, and the seed rows of the code tables.
 *
 * No include guard: a file that includes it first defines the five row
 * macros, keeps the rows it needs and drops the others, then undefines
 * them.  ENGFLD.H makes the field numbers (ENG_FIELD) and the field
 * widths and key lengths (ENG_FW_, ENG_FD_, ENG_KL_) from it, SCHEMA.C
 * the near descriptions used on every request (table and index names,
 * field flags) and the compile-time checks of the schema limits, and
 * SRC/COMPAT/CSCHDAT.C the descriptions only an open needs (field names,
 * types and widths, key expressions, seed rows), placed outside DGROUP.
 * SRC/PROTO/DPCSV.C (the longest requests) and SRC/CLIENT/LINK.C (the
 * widths LINK.H repeats) read the widths through ENGFLD.H, for
 * compile-time checks only.  A row macro uses its field-name argument
 * only with # or ##, so a name is never replaced by a macro of that
 * name.
 *
 *   SCH_TABLE(t, memo, seq, firstx, nindex)    a table and its fields,
 *   SCH_FIELD(t, name, type, len, dec, flags)  in ENG_T_ order, closed
 *   SCH_END(t)                                 by SCH_END
 *   SCH_INDEX(x, t, expr, keylen, unique, dup) in ENG_X_ order
 *   SCH_SEED(t, code, name)                    by table, in SORTORD order
 *
 * memo is 1 for a table with a .DBT; seq is 1 for a table whose IDs come
 * from SEQ.DBF under the table's name, else 0; firstx and nindex give
 * the table's indexes.  dup is the number of the field a 201 duplicate
 * error names (ENG_FIELD, so the includer also includes ENGFLD.H), or
 * -1.
 *
 * Key expressions may use SCH_YMD(d), the YYYYMMDD text of D field d
 * built from functions dBASE III Plus has (it has no DTOS); only
 * CSCHDAT.C, which keeps the expressions, defines it.  The STR(...,8)
 * of an expression is dBASE text and stays a literal; the key lengths
 * are written with the ENG_ widths they add up to, except ITEMS4's,
 * whose two fields have no constant.
 *
 * flags are written with the short names RO UP HID LST NOA MOV PAR REQ
 * NNG LON (ENG_F_RO ... ENG_F_AUDLOAN), which only SCHEMA.C defines.
 */

SCH_TABLE(ITEMS, 1, 1, ENG_X_ITEMS1, 5)
SCH_FIELD(ITEMS, ITEM_ID,   'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(ITEMS, CATEGORY,  'C', ENG_CATLEN,      0, UP | REQ | LST)
SCH_FIELD(ITEMS, ITEMTYPE,  'C', 20,              0, 0)
SCH_FIELD(ITEMS, MFR,       'C', 30,              0, LST)
SCH_FIELD(ITEMS, MODEL,     'C', 30,              0, LST)
SCH_FIELD(ITEMS, SERIALNO,  'C', ENG_SERLEN,      0, 0)
SCH_FIELD(ITEMS, DESCR,     'C', 60,              0, 0)
SCH_FIELD(ITEMS, COND,      'C', 3,               0, UP)
SCH_FIELD(ITEMS, WORKS,     'C', 1,               0, UP | LST)
SCH_FIELD(ITEMS, QTY,       'N', 5,               0, NNG | LST)
SCH_FIELD(ITEMS, ON_LOAN,   'N', 5,               0, RO | NNG | LON)
SCH_FIELD(ITEMS, RESERVED,  'N', 5,               0, RO | NNG | LON)
SCH_FIELD(ITEMS, IN_USE,    'N', 5,               0, RO | NNG | LON)
SCH_FIELD(ITEMS, ATTACHED,  'N', 5,               0, RO | NNG | LON)
SCH_FIELD(ITEMS, LOC_ID,    'N', ENG_IDLEN,       0, LST | MOV)
SCH_FIELD(ITEMS, PARENT_ID, 'N', ENG_IDLEN,       0, RO | PAR)
SCH_FIELD(ITEMS, ACQ_DATE,  'D', ENG_DATELEN,     0, 0)
SCH_FIELD(ITEMS, ACQ_SRC,   'C', 30,              0, 0)
SCH_FIELD(ITEMS, ACQ_PRICE, 'N', 9,               2, NNG)
SCH_FIELD(ITEMS, CUR_VALUE, 'N', 9,               2, NNG)
SCH_FIELD(ITEMS, TAGS,      'C', 60,              0, 0)
SCH_FIELD(ITEMS, STATUS,    'C', ENG_STATLEN,     0, UP | LST)
SCH_FIELD(ITEMS, CREATED,   'D', ENG_DATELEN,     0, RO)
SCH_FIELD(ITEMS, UPDATED,   'D', ENG_DATELEN,     0, RO | NOA)
SCH_FIELD(ITEMS, NOTES,     'M', 10,              0, 0)
SCH_END(ITEMS)

SCH_TABLE(LOCS, 1, 1, ENG_X_LOCS1, 3)
SCH_FIELD(LOCS, LOC_ID,     'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(LOCS, PARENT_ID,  'N', ENG_IDLEN,       0, LST)
SCH_FIELD(LOCS, LOC_NAME,   'C', ENG_LOCNAMELEN,  0, REQ | LST)
SCH_FIELD(LOCS, LOC_CODE,   'C', ENG_LOCLEN,      0, UP | LST)
SCH_FIELD(LOCS, LTYPE,      'C', ENG_LTYPELEN,    0, UP | REQ | LST)
SCH_FIELD(LOCS, CAPACITY,   'N', 4,               0, NNG | LST)
SCH_FIELD(LOCS, NOTES,      'M', 10,              0, 0)
SCH_END(LOCS)

SCH_TABLE(CATS, 0, 0, -1, 0)
SCH_FIELD(CATS, CAT_CODE,   'C', ENG_CATLEN,      0, RO | UP | LST)
SCH_FIELD(CATS, CAT_NAME,   'C', ENG_CODENAMELEN, 0, REQ | LST)
SCH_FIELD(CATS, SORTORD,    'N', 2,               0, NNG | LST)
SCH_END(CATS)

SCH_TABLE(LTYPES, 0, 0, -1, 0)
SCH_FIELD(LTYPES, LT_CODE,  'C', ENG_LTYPELEN,    0, RO | UP | LST)
SCH_FIELD(LTYPES, LT_NAME,  'C', ENG_CODENAMELEN, 0, REQ | LST)
SCH_FIELD(LTYPES, SORTORD,  'N', 2,               0, NNG | LST)
SCH_END(LTYPES)

SCH_TABLE(MAINT, 1, 1, ENG_X_MAINT1, 1)
SCH_FIELD(MAINT, MAINT_ID,  'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(MAINT, ITEM_ID,   'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(MAINT, MDATE,     'D', ENG_DATELEN,     0, LST)
SCH_FIELD(MAINT, MTYPE,     'C', 2,               0, UP | REQ | LST)
SCH_FIELD(MAINT, SUMMARY,   'C', 60,              0, REQ | LST)
SCH_FIELD(MAINT, COST,      'N', 8,               2, NNG | LST)
SCH_FIELD(MAINT, DONE_BY,   'C', 20,              0, 0)
SCH_FIELD(MAINT, DETAIL,    'M', 10,              0, 0)
SCH_END(MAINT)

SCH_TABLE(LOANS, 0, 1, ENG_X_LOANS1, 2)
SCH_FIELD(LOANS, LOAN_ID,   'N', ENG_IDLEN,       0, RO)
SCH_FIELD(LOANS, ITEM_ID,   'N', ENG_IDLEN,       0, RO)
SCH_FIELD(LOANS, KIND,      'C', 1,               0, RO)
SCH_FIELD(LOANS, QTY,       'N', 5,               0, NNG)
SCH_FIELD(LOANS, HOLDER,    'C', 30,              0, 0)
SCH_FIELD(LOANS, HOST_ID,   'N', ENG_IDLEN,       0, NNG)
SCH_FIELD(LOANS, CONTACT,   'C', 30,              0, 0)
SCH_FIELD(LOANS, OUT_DATE,  'D', ENG_DATELEN,     0, 0)
SCH_FIELD(LOANS, DUE_DATE,  'D', ENG_DATELEN,     0, 0)
SCH_FIELD(LOANS, RET_DATE,  'D', ENG_DATELEN,     0, RO)
SCH_FIELD(LOANS, NOTES,     'C', 60,              0, 0)
SCH_END(LOANS)

SCH_TABLE(AUDIT, 0, 1, ENG_X_AUDIT1, 2)
SCH_FIELD(AUDIT, AUD_ID,    'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(AUDIT, ADATE,     'D', ENG_DATELEN,     0, RO | LST)
SCH_FIELD(AUDIT, ATIME,     'C', ENG_TIMELEN,     0, RO | LST)
SCH_FIELD(AUDIT, STATION,   'C', ENG_STALEN,      0, RO | LST)
SCH_FIELD(AUDIT, USERNAME,  'C', ENG_USERLEN,     0, RO | LST)
SCH_FIELD(AUDIT, TBL,       'C', ENG_TBLNAMELEN,  0, RO | LST)
SCH_FIELD(AUDIT, REC_ID,    'N', ENG_IDLEN,       0, RO | LST)
SCH_FIELD(AUDIT, ACTION,    'C', 1,               0, RO | LST)
SCH_FIELD(AUDIT, FLD,       'C', DBF_NAMELEN,     0, RO | LST)
SCH_FIELD(AUDIT, OLDVAL,    'C', ENG_AUDVAL,      0, RO | LST)
SCH_FIELD(AUDIT, NEWVAL,    'C', ENG_AUDVAL,      0, RO | LST)
SCH_END(AUDIT)

SCH_TABLE(USERS, 0, 1, ENG_X_USERS1, 1)
SCH_FIELD(USERS, USER_ID,   'N', 4,               0, RO)
SCH_FIELD(USERS, USERNAME,  'C', ENG_USERLEN,     0, RO | UP | LST)
SCH_FIELD(USERS, FULLNAME,  'C', ENG_FULLNAMELEN, 0, LST)
SCH_FIELD(USERS, GROUPNAME, 'C', 8,               0, UP | LST)
SCH_FIELD(USERS, LEVEL,     'N', 1,               0, REQ | LST)
SCH_FIELD(USERS, SALT,      'C', ENG_SALTLEN,     0, RO | HID)
SCH_FIELD(USERS, PWHASH,    'C', ENG_HASHLEN,     0, RO | HID)
SCH_FIELD(USERS, STATUS,    'C', 1,               0, UP | LST)
SCH_FIELD(USERS, MUSTCHG,   'L', 1,               0, RO | NOA | LST)
SCH_FIELD(USERS, LASTLOGIN, 'D', ENG_DATELEN,     0, RO | NOA | LST)
SCH_FIELD(USERS, CREATED,   'D', ENG_DATELEN,     0, RO)
SCH_END(USERS)

SCH_TABLE(SEQ, 0, 0, -1, 0)
SCH_FIELD(SEQ, SEQ_NAME,    'C', DBF_NAMELEN,     0, RO)
SCH_FIELD(SEQ, NEXT_VAL,    'N', ENG_IDLEN,       0, RO)
SCH_END(SEQ)

SCH_INDEX(ITEMS1, ITEMS, "STR(ITEM_ID,8)", ENG_IDLEN, 1,
          ENG_FIELD(ITEMS, ITEM_ID))
SCH_INDEX(ITEMS2, ITEMS, "STR(LOC_ID,8)+STR(ITEM_ID,8)",
          2 * ENG_IDLEN, 0, -1)
SCH_INDEX(ITEMS3, ITEMS, "STR(PARENT_ID,8)+STR(ITEM_ID,8)",
          ENG_ITEMS3LEN, 0, -1)
SCH_INDEX(ITEMS4, ITEMS, "UPPER(MFR)+UPPER(MODEL)", 60, 0, -1)
SCH_INDEX(ITEMS5, ITEMS, "UPPER(SERIALNO)", ENG_SERLEN, 0, -1)
SCH_INDEX(LOCS1,  LOCS,  "STR(LOC_ID,8)", ENG_IDLEN, 1,
          ENG_FIELD(LOCS, LOC_ID))
SCH_INDEX(LOCS2,  LOCS,  "STR(PARENT_ID,8)+UPPER(LOC_NAME)+STR(LOC_ID,8)",
          2 * ENG_IDLEN + ENG_LOCNAMELEN, 0, -1)
SCH_INDEX(LOCS3,  LOCS,  "LOC_CODE", ENG_LOCLEN, 0, -1)
SCH_INDEX(MAINT1, MAINT, "STR(ITEM_ID,8)+" SCH_YMD("MDATE"),
          ENG_IDLEN + ENG_DATELEN, 0, -1)
SCH_INDEX(LOANS1, LOANS, "STR(ITEM_ID,8)+STR(LOAN_ID,8)",
          2 * ENG_IDLEN, 0, -1)
SCH_INDEX(LOANS2, LOANS,
          "STR(YEAR(RET_DATE),4)+" SCH_YMD("DUE_DATE") "+STR(LOAN_ID,8)",
          4 + ENG_DATELEN + ENG_IDLEN, 0, -1)
SCH_INDEX(AUDIT1, AUDIT, "TBL+STR(REC_ID,8)+STR(AUD_ID,8)",
          ENG_TBLNAMELEN + 2 * ENG_IDLEN, 0, -1)
SCH_INDEX(AUDIT2, AUDIT, "USERNAME+STR(99999999-AUD_ID,8)",
          ENG_USERLEN + ENG_IDLEN, 0, -1)
SCH_INDEX(USERS1, USERS, "USERNAME", ENG_USERLEN, 1,
          ENG_FIELD(USERS, USERNAME))

SCH_SEED(CATS, "SYS",  "Systems")
SCH_SEED(CATS, "MOBO", "Motherboards")
SCH_SEED(CATS, "CPU",  "Processors")
SCH_SEED(CATS, "CARD", "Expansion cards")
SCH_SEED(CATS, "DRV",  "Drives")
SCH_SEED(CATS, "PERI", "Peripherals")
SCH_SEED(CATS, "SW",   "Software")
SCH_SEED(CATS, "MAN",  "Manuals")
SCH_SEED(CATS, "CBL",  "Cables")
SCH_SEED(CATS, "PART", "Parts")
SCH_SEED(LTYPES, "ROOM",    "Room")
SCH_SEED(LTYPES, "SHELF",   "Shelf")
SCH_SEED(LTYPES, "RACK",    "Rack")
SCH_SEED(LTYPES, "CABINET", "Cabinet")
SCH_SEED(LTYPES, "DRAWER",  "Drawer")
SCH_SEED(LTYPES, "BOX",     "Box")
SCH_SEED(LTYPES, "BIN",     "Bin")
SCH_SEED(LTYPES, "BUCKET",  "Bucket")
