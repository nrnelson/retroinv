* CSVCHK.PRG - CSV files written and read by dBASE III Plus.
*
* Run by csvchk.sh (through rundb.sh) in a data directory where INV has
* exported ITEMS, LOCS, CATS, LTYPES, MAINT, LOANS and AUDIT to
* <table>.CSV.
* With SET DELETED ON, writes each table with COPY TO <table> DELIMITED
* (<table>.TXT, which csvchk.sh compares with <table>.CSV byte for
* byte), then copies each table's structure to X<table>, appends
* <table>.CSV to it with APPEND FROM ... DELIMITED and lists the copy
* in OUT.TXT, preceded by its record count.  The last line is "END OF
* CSVCHK", so a listing cut short is recognised.  Any error dBASE
* reports ends the run through DBERR.
ON ERROR DO DBERR
SET DELETED ON
SET MEMOWIDTH TO 76
SET ALTERNATE TO OUT
SET ALTERNATE ON
N = 1
DO WHILE N <= 7
  T = SUBSTR("ITEMS LOCS  CATS  LTYPESMAINT LOANS AUDIT ", N * 6 - 5, 6)
  T = TRIM(T)
  X = "X" + T
  F = T + ".CSV"
  USE &T
  COPY TO &T DELIMITED
  COPY STRUCTURE TO &X
  USE &X
  APPEND FROM &F DELIMITED
  ? "TABLE", T, "RECORDS", RECCOUNT()
  LIST
  N = N + 1
ENDDO
CLOSE DATABASES
?
? "END OF CSVCHK"
CLOSE ALTERNATE
DO DBEND
QUIT
