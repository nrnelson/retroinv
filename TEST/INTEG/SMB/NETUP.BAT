@ECHO OFF
REM NETUP FILE - start the TCP/IP network client, its messages appended
REM to FILE (CON for the screen): the card and protocol drivers, the
REM TCP/IP stack, NetBIOS over TCP/IP (names from C:\NET\LMHOSTS), the
REM redirector, and a logon under the computer name with an empty
REM password, read from ENTER.TXT, and no password list.
C:\NET\NET INITIALIZE >> %1
C:\NET\NETBIND >> %1
C:\NET\TCPTSR >> %1
C:\NET\TINYRFC >> %1
C:\NET\NET START >> %1
C:\NET\NET LOGON @COMPUTERNAME@ /YES /SAVEPW:NO < C:\NET\ENTER.TXT >> %1
