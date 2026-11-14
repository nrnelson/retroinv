@ECHO OFF
PATH C:\DOS;C:\NET
C:\DOS\SHARE.EXE
REM Set up the PCI network card and record its state (see imglib.sh).
C:\DOS\DEBUG < C:\NET\PCNET.SCR > C:\NET\PCNET.OUT
REM A test run puts RUN.BAT on drive D:; starting a batch file without CALL
REM hands control to it for good, so the NET START below runs only on a
REM plain boot without a test payload.
IF EXIST D:\RUN.BAT D:\RUN.BAT
C:\NET\NET START
