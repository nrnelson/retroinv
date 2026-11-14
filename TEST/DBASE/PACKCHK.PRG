* PACKCHK.PRG - check a packed table and rebuilt indexes in dBASE III Plus.
*
* Run by packchk.sh (through rundb.sh) in a directory holding ITEMS.DBF
* + ITEMS.DBT packed by DBPACK, with ITEMS1.NDX (STR(ITEM_ID,8)) rebuilt
* by DBPACK /R, BIG.DBF with BIG1.NDX (STR(ID,8)) and BIG2.NDX (NAME),
* and NUM2000.DBF with NUM2000.NDX (STR(ID,8)), all rebuilt by DBPACK
* /R.  Writes OUT.TXT:
*   - the ITEMS record count, deleted count, records and memo text, and
*     SEEK results on ITEMS1 for keys 1 to 10;
*   - for BIG1 and NUM2000, every key 1 to N + 2 sought (a present key
*     not found prints "NOT FOUND", one found on the wrong record "WRONG
*     RECORD", an absent one "ABSENT"; the results near page boundaries
*     are listed), and a walk in index order with the count of steps
*     whose key is not above the previous one ("OUT OF ORDER");
*   - with BIG1 and BIG2 open, every record sought by both keys, before
*     and after dBASE appends 150 records and changes one through both
*     indexes ("SEEK MISS" for each miss, a BAD count per pass, and an
*     OUT OF ORDER count for a walk in each order).
* The last line is "END OF PACKCHK", so a listing cut short is
* recognised.  Any error dBASE reports ends the run through DBERR.
ON ERROR DO DBERR
SET DELETED OFF
SET MEMOWIDTH TO 76
SET ALTERNATE TO OUT
SET ALTERNATE ON
USE ITEMS
? "RECORDS", RECCOUNT()
COUNT FOR DELETED() TO NDEL
? "DELETED", NDEL
LIST ITEM_ID, CATEGORY, MFR, MODEL
LIST ITEM_ID, NOTES
USE ITEMS INDEX ITEMS1
LIST ITEM_ID, MFR
K = 1
DO WHILE K <= 10
  SEEK STR(K, 8)
  IF FOUND()
    ? "ITEMS SEEK", K, "FOUND", RECNO(), ITEM_ID
  ELSE
    ? "ITEMS SEEK", K, "NO KEY"
  ENDIF
  K = K + 1
ENDDO
* The STR(ID,8) indexes: BIG1 (300 keys, two levels) and NUM2000 (2000
* keys, three levels with leaves at different depths).
T = 1
DO WHILE T <= 2
  IF T = 1
    TB = "BIG"
    IX = "BIG1"
    LAST = 300
  ELSE
    TB = "NUM2000"
    IX = "NUM2000"
    LAST = 2000
  ENDIF
  USE &TB INDEX &IX
  ? TB, "RECORDS", RECCOUNT()
  * Present keys near every page boundary, then absent ones.
  K = 1
  DO WHILE K <= LAST + 2
    SEEK STR(K, 8)
    IF FOUND()
      IF ID <> K
        ? TB, "SEEK", K, "WRONG RECORD", RECNO()
      ENDIF
    ELSE
      IF K <= LAST
        ? TB, "SEEK", K, "NOT FOUND"
      ELSE
        ? TB, "SEEK", K, "ABSENT"
      ENDIF
    ENDIF
    IF MOD(K, 31) < 2 .OR. K > LAST - 3
      ? TB, "SEEK", K, FOUND(), RECNO()
    ENDIF
    K = K + 1
  ENDDO
  SEEK STR(0, 8)
  ? TB, "SEEK 0", FOUND()
  * Walk in index order.
  N = 0
  BAD = 0
  PREV = -1
  GO TOP
  DO WHILE .NOT. EOF()
    IF ID <= PREV
      BAD = BAD + 1
    ENDIF
    PREV = ID
    N = N + 1
    SKIP
  ENDDO
  ? TB, "WALK", N, "OUT OF ORDER", BAD
  GO BOTTOM
  ? TB, "BOTTOM", ID
  T = T + 1
ENDDO
* BIG with both rebuilt indexes open: every record is found by each
* key, then 150 records are appended and one changed through dBASE,
* which updates both indexes, and everything is sought again.
SET EXACT OFF
USE BIG INDEX BIG1, BIG2
? "BIG RECORDS", RECCOUNT()
PASS = 1
DO WHILE PASS <= 2
  BAD = 0
  N = RECCOUNT()
  R = 1
  DO WHILE R <= N
    GO R
    KID = STR(ID, 8)
    KNM = NAME
    SET ORDER TO 1
    SEEK KID
    IF .NOT. FOUND() .OR. RECNO() <> R
      BAD = BAD + 1
      ? "PASS", PASS, "ID SEEK MISS", R
    ENDIF
    SET ORDER TO 2
    SEEK KNM
    IF .NOT. FOUND() .OR. RECNO() <> R
      BAD = BAD + 1
      ? "PASS", PASS, "NAME SEEK MISS", R, KNM
    ENDIF
    R = R + 1
  ENDDO
  ? "PASS", PASS, "RECORDS", N, "BAD", BAD
  SET ORDER TO 1
  GO TOP
  C = 0
  PREV = -1
  OOO = 0
  DO WHILE .NOT. EOF()
    IF ID <= PREV
      OOO = OOO + 1
    ENDIF
    PREV = ID
    C = C + 1
    SKIP
  ENDDO
  ? "PASS", PASS, "ID WALK", C, "OUT OF ORDER", OOO
  SET ORDER TO 2
  GO TOP
  C = 0
  PN = ""
  OOO = 0
  DO WHILE .NOT. EOF()
    IF NAME < PN
      OOO = OOO + 1
    ENDIF
    PN = NAME
    C = C + 1
    SKIP
  ENDDO
  ? "PASS", PASS, "NAME WALK", C, "OUT OF ORDER", OOO
  IF PASS = 1
    K = 1
    DO WHILE K <= 150
      APPEND BLANK
      REPLACE ID WITH MOD(K * 37, 151) + 1000
      REPLACE NAME WITH "A" + STR(MOD(K * 53, 157), 3)
      K = K + 1
    ENDDO
    GO 5
    REPLACE ID WITH 5000
    REPLACE NAME WITH "ZZZ"
  ENDIF
  PASS = PASS + 1
ENDDO
CLOSE DATABASES
?
? "END OF PACKCHK"
CLOSE ALTERNATE
DO DBEND
QUIT
