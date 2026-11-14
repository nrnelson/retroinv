* DBERR.PRG - error handler for unattended runs.
* A program installs it with ON ERROR DO DBERR.  It writes the error
* number and message to DBERR.TXT and leaves dBASE, so a headless run
* ends instead of waiting at the "Cancel, Ignore, or Suspend?" prompt.
SET ALTERNATE TO DBERR.TXT
SET ALTERNATE ON
? "ERROR", ERROR(), MESSAGE()
CLOSE ALTERNATE
QUIT
