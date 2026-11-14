; SETMACH.ASM - set the DOS machine name of the test machine.
;
; usage: SETMACH NAME   errorlevel 0 when DOS has taken NAME (its first
;                       NAMELEN characters), 1 when the command line names
;                       none or DOS refused it
;
; MS Network Client 3.0 reads its computer name from SYSTEM.INI but never
; hands it to DOS: after NET START, and NET LOGON too, function 5E00h
; reports no machine name (CH 0) under DOS and under Windows alike.  This
; program sets the name as a redirector that does so would, with function
; 5E01h (DOS 3.1): CH nonzero to set it, CL its NetBIOS name number (1
; here; DOS only keeps it for 5E00h to return) and DS:DX the name, NAMELEN
; characters padded with spaces and then a NUL.  8086 instructions only.

        cpu     8086
        org     100h

SET_NAME    equ     5E01h
NAMELEN     equ     15
CMDLINE     equ     81h         ; the command line's text in the PSP

start:  mov     si, CMDLINE
        mov     di, name
        mov     cx, NAMELEN
skip:   lodsb
        cmp     al, ' '
        je      skip
copy:   cmp     al, ' '
        jbe     named           ; a blank, or the CR that ends the line
        stosb
        lodsb
        loop    copy
named:  mov     al, 1
        cmp     di, name
        je      done
        mov     ax, SET_NAME
        mov     cx, 0101h       ; CH: set the name, CL: its number
        mov     dx, name
        int     21h
        mov     al, 0
        adc     al, 0
done:   mov     ah, 4Ch
        int     21h

name:   times   NAMELEN db ' '
        db      0
