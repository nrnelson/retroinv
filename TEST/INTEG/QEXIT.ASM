; QEXIT.COM - end a QEMU test VM with an exit status.
;
; usage: QEXIT [n]      n = 0..127, default 0; larger values give 127
;
; Writes n to the isa-debug-exit device at I/O port 501h; QEMU then exits
; with host status (n*2)+1.  On machines without that device the port is
; normally unused, so the program falls through and ends with DOS
; errorlevel n.  An argument that is not a plain decimal number (such as
; "abc" or "5O") counts as 127 so a garbled call can never read as
; success.  8086 instructions only.

        cpu     8086
        org     100h

start:  cld
        mov     si, 81h         ; command tail in the PSP, ends with CR
        xor     bx, bx          ; accumulated value, kept <= 127
skip:   lodsb
        cmp     al, ' '
        je      skip
        cmp     al, 9
        je      skip
        cmp     al, 0Dh         ; empty tail: status 0
        je      done
        cmp     al, '0'
        jb      bad
        cmp     al, '9'
        ja      bad
digit:  sub     al, '0'
        mov     cl, al
        xor     ch, ch
        mov     ax, bx
        mov     dx, 10
        mul     dx
        add     ax, cx
        cmp     ax, 127
        jbe     keep
        mov     ax, 127
keep:   mov     bx, ax
        lodsb
        cmp     al, '0'
        jb      endnum
        cmp     al, '9'
        jbe     digit
endnum: cmp     al, 0Dh         ; the number must end the argument
        je      done
        cmp     al, ' '
        je      done
        cmp     al, 9
        je      done
bad:    mov     bl, 127
done:   mov     al, bl
        mov     dx, 501h
        out     dx, al
        mov     ah, 4Ch
        int     21h
