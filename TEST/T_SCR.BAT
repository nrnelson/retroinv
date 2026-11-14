ECHO OFF
REM T_SCR.BAT - one golden-screen run of INV under DOS, for TEST/T_SCR.SH.
REM
REM usage: T_SCR run key cfgsrc cfgname seed out work now inv [file]
REM   run      name of the run (1-8 characters)
REM   key      the key script, as a full path
REM   cfgsrc   the settings file to start from, copied to cfgname
REM   cfgname  the settings file's name in the run's directory (/CFG:)
REM   seed     S to fill the data directory from SEED.REQ, D from SEED.REQ
REM            and then DEEP.REQ, M from SEED.REQ and then ROOMS.REQ, R
REM            from SAMPLE.REQ, N to leave it empty; SP or NP the same as
REM            S or N, the print jobs captured too
REM   out      drive and directory for the results, e.g. A: or D:
REM   work     drive for the run's scratch directory, e.g. C: or D:
REM   now      the /NOW: switch fixing the clock
REM   inv      the drive (and directory) holding INV.EXE, SEED.REQ,
REM            DEEP.REQ, ROOMS.REQ and SAMPLE.REQ, e.g. A: or C:
REM   file     a file the run writes in its directory, to keep
REM
REM The run's current directory is work\run, fresh, with the data
REM directory TDATA in it.  The responses to the seed requests go to
REM out\run.SED, the snapshots to out\run.SNP, the print jobs (seed SP
REM or NP, through /PRN:) to out\run.PRN, the settings file as the run
REM left it to out\run.CFG, file to out\run.OUT, and a line "run status"
REM is appended to out\RESULT.TXT, status being 9 for an exit status of 9
REM or more.  The scratch directory is removed again, the engine's tables,
REM indexes and scratch files in TDATA included, so that many runs fit on
REM a RAM disk; when that fails, a line "run LEFT" is appended to
REM out\RESULT.TXT.  The current drive is then set back to inv's, where
REM the caller's batch file is.  MS-DOS 3.10 has no CALL, so the caller
REM runs this file through COMMAND /C.  The tenth argument is reached with
REM SHIFT, after which run is %0, out %5 and inv %8.
IF "%9"=="" GOTO USAGE
%7
CD \
MD %1
CD %1
MD TDATA
IF %5==S %9\INV /CMD /LOCAL TDATA %8 <%9\SEED.REQ >%6\%1.SED
IF %5==SP %9\INV /CMD /LOCAL TDATA %8 <%9\SEED.REQ >%6\%1.SED
IF %5==D %9\INV /CMD /LOCAL TDATA %8 <%9\SEED.REQ >%6\%1.SED
IF %5==D %9\INV /CMD /LOCAL TDATA %8 <%9\DEEP.REQ >>%6\%1.SED
IF %5==M %9\INV /CMD /LOCAL TDATA %8 <%9\SEED.REQ >%6\%1.SED
IF %5==M %9\INV /CMD /LOCAL TDATA %8 <%9\ROOMS.REQ >>%6\%1.SED
IF %5==R %9\INV /CMD /LOCAL TDATA %8 <%9\SAMPLE.REQ >%6\%1.SED
COPY %3 %4 >NUL
IF %5==SP GOTO PRINT
IF %5==NP GOTO PRINT
%9\INV /LOCAL TDATA %8 /CFG:%4 /KEYS:%2 /SNAP:%6\%1.SNP
GOTO RAN
:PRINT
%9\INV /LOCAL TDATA %8 /CFG:%4 /KEYS:%2 /SNAP:%6\%1.SNP /PRN:%6\%1.PRN
:RAN
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
COPY %4 %6\%1.CFG >NUL
ECHO %1 %RC% >>%6\RESULT.TXT
DEL TDATA\*.DBF
DEL TDATA\*.DBT
DEL TDATA\*.NDX
DEL TDATA\*.OLD
DEL TDATA\*.$*
DEL TDATA\*.%%*
DEL TDATA\*.&*
DEL TDATA\*.#*
RD TDATA
DEL %4
SHIFT
IF NOT "%9"=="" COPY %9 %5\%0.OUT >NUL
IF NOT "%9"=="" DEL %9
CD \
RD %0
IF EXIST %0\NUL ECHO %0 LEFT >>%5\RESULT.TXT
%8
GOTO END
:USAGE
ECHO usage: T_SCR run key cfgsrc cfgname seed out work now inv [file]
:END
