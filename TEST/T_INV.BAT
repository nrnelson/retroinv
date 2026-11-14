ECHO OFF
REM T_INV.BAT - INV.EXE under DOS: the scripted run, the full-screen client
REM driven by key scripts, and the exit statuses.  Run as
REM "COMMAND /C T_INV /NOW:YYYYMMDDHHMMSS D:" from the drive holding INV.EXE,
REM INV.MSG, INVCMD.IN, the key scripts and TEST.CFG; the second argument
REM names an empty drive for the data.  First CHKDSK, from the boot floppy
REM in A:, writes its report of that drive and of the free memory, as INV
REM finds it here, to FREE.TXT.  The scripted run writes its responses to
REM CMD.OUT, the login run its snapshots to LOGIN.SNP; then every case
REM prints one line "== name exit n" to standard output, n being 9 for an
REM exit status of 9 or more, and INV's own messages go to the console.
REM Last, with BREAK on, INV /CMD /COM2, COMTEST /COM2 and INV /CMD
REM /LOCAL each print "== name" and DOS's BREAK setting after them.
REM The file is stored with LF line ends; TEST/T_INV.SH converts it to
REM CR LF before copying it to a DOS disk.
IF "%2"=="" GOTO USAGE
A:\CHKDSK %2 >FREE.TXT
INV /CMD /LOCAL %2\ %1 <INVCMD.IN >CMD.OUT
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == cmd exit %RC%
INV /CMD /BOGUS
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == usage exit %RC%
INV /CMD /NOW:20261332000000 <NUL
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == badtime exit %RC%
INV /CMD /NET SERVER
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == net exit %RC%
INV /LOCAL %2\ /KEYS:EMPTY.KEY
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == tui exit %RC%
INV /LOCAL %2\ /CFG:TEST.CFG /KEYS:SAVECFG.KEY /SNAP:LOGIN.SNP
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == login exit %RC%
INV /LOCAL %2\ /KEYS:BAD3.KEY
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == badlogin exit %RC%
INV /LOCAL %2\ /SNAP:X.SNP
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == snapnokeys exit %RC%
INV /LOCAL %2\ /KEYS:EMPTY.KEY /MDA
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == mda exit %RC%
INV /LOCAL %2\ /CFG:X.$$$ /KEYS:EMPTY.KEY
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == cfgscr exit %RC%
REN INV.MSG INV.MS_
INV /LOCAL %2\ /KEYS:EMPTY.KEY
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == nomsg exit %RC%
REN INV.MS_ INV.MSG
INV /CMD /LOCAL A:\NODIR <NUL
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == nodir exit %RC%
inv -cmd -Local %2\ <NUL
FOR %%E IN (0 1 2 3 4 5 6 7 8 9) DO IF ERRORLEVEL %%E SET RC=%%E
ECHO == reopen exit %RC%
BREAK ON
INV /CMD /COM2 <NUL >NUL
ECHO == cmdcom
BREAK
COMTEST /COM2 >NUL
ECHO == comtest
BREAK
INV /CMD /LOCAL %2\ <NUL >NUL
ECHO == cmdlocal
BREAK
GOTO END
:USAGE
ECHO usage: T_INV /NOW:YYYYMMDDHHMMSS datadrive
:END
