* DBEND.PRG - completion marker for unattended runs.
* A program runs DO DBEND as its last command before QUIT.  It writes
* DBEND.TXT, which tells the script that started dBASE that the program
* ran to its end.  Any alternate file still open is closed first.
SET ALTERNATE TO DBEND.TXT
SET ALTERNATE ON
? "DBEND"
CLOSE ALTERNATE
