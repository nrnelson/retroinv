@ECHO OFF
PATH C:\DOS;C:\NET
C:\DOS\SHARE.EXE
REM Set up the PCI network card and record its state (see imglib.sh).
C:\DOS\DEBUG < C:\NET\PCNET.SCR > C:\NET\PCNET.OUT
REM A test run puts RUN.BAT on drive D:, which starts the network itself
REM (CALL C:\NET\NETUP file); a plain boot starts it below.
IF EXIST D:\RUN.BAT D:\RUN.BAT
C:\NET\NETUP CON
