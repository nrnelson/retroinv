* CHECK.PRG - list a sample database as dBASE III Plus sees it.
*
* Run with rundb.sh (check.sh does) in a directory holding ITEMS.DBF +
* ITEMS.DBT, LOCS.DBF + LOCS.DBT, CATS.DBF, LTYPES.DBF and USERS.DBF, for
* example the files our own library writes.  Writes OUT.TXT: for each
* table its structure, record count and number of deleted records, every
* record
* (deleted ones included) and, for tables with a memo, each record's memo
* text; for ITEMS also totals of its numeric fields, which makes dBASE
* convert every N value.  The last line is "END OF CHECK", so a listing
* cut short is recognised.  Any error dBASE reports ends the run through
* DBERR.
ON ERROR DO DBERR
SET DELETED OFF
SET MEMOWIDTH TO 76
SET ALTERNATE TO OUT
SET ALTERNATE ON
USE ITEMS
LIST STRUCTURE
? "RECORDS", RECCOUNT()
COUNT FOR DELETED() TO NDEL
? "DELETED", NDEL
SUM ACQ_PRICE, CUR_VALUE, QTY TO SPRICE, SVALUE, SQTY
? "SUMS", SPRICE, SVALUE, SQTY
LIST
LIST ITEM_ID, NOTES
USE LOCS
LIST STRUCTURE
COUNT FOR DELETED() TO NDEL
? "RECORDS", RECCOUNT(), "DELETED", NDEL
LIST
LIST LOC_ID, NOTES
USE CATS
LIST STRUCTURE
COUNT FOR DELETED() TO NDEL
? "RECORDS", RECCOUNT(), "DELETED", NDEL
LIST
USE LTYPES
LIST STRUCTURE
COUNT FOR DELETED() TO NDEL
? "RECORDS", RECCOUNT(), "DELETED", NDEL
LIST
USE USERS
LIST STRUCTURE
COUNT FOR DELETED() TO NDEL
? "RECORDS", RECCOUNT(), "DELETED", NDEL
LIST
CLOSE DATABASES
?
? "END OF CHECK"
CLOSE ALTERNATE
DO DBEND
QUIT
