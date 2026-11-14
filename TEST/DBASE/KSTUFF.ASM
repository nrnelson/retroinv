; KSTUFF.COM - put keystrokes into the BIOS keyboard buffer.
;
; usage: KSTUFF token...
;
; Each token is one of
;   hhhh     four hex digits: scan code (high byte) and character (low
;            byte) of one key, e.g. 7600 = Ctrl-PgDn, 7500 = Ctrl-End,
;            250B = Ctrl-K, 1C0D = Enter
;   'text    the characters of text, each as a key with scan code 0
;            (text ends at the next space or the end of the line)
; Tokens are separated by spaces.  The keys are stored with INT 16h
; function 05h, so a program started next (or the program that ran
; KSTUFF, once it reads the keyboard again) receives them as if typed.
; Function 05h exists only in AT-class (and later) BIOSes, not in a PC or
; XT BIOS.  This is a test tool for DOSBox-X and QEMU, whose BIOSes
; have it.  The BIOS buffer holds 15 keys.
;
; Errorlevel: 0 all keys stored, 1 buffer full, 2 malformed token.
; 8086 instructions only.

        cpu     8086
        org     100h

start:  cld
        mov     si, 81h         ; command tail in the PSP, ends with CR
next:   lodsb
        cmp     al, ' '
        je      next
        cmp     al, 9
        je      next
        cmp     al, 0Dh
        je      ok
        cmp     al, 27h         ; ' starts literal text
        je      text
        dec     si
        xor     dx, dx          ; DX = scan:char being built
        mov     cx, 4
hex:    lodsb
        cmp     al, '0'
        jb      bad
        cmp     al, '9'
        jbe     dig
        and     al, 0DFh        ; upper-case a-f
        cmp     al, 'A'
        jb      bad
        cmp     al, 'F'
        ja      bad
        sub     al, 7
dig:    sub     al, '0'
        push    cx
        mov     cl, 4
        shl     dx, cl
        pop     cx
        or      dl, al
        loop    hex
        mov     al, [si]        ; a token is exactly four digits
        cmp     al, ' '
        je      put
        cmp     al, 0Dh
        jne     bad
put:    mov     cx, dx
        call    stuff
        jmp     next

text:   lodsb
        cmp     al, ' '
        je      next
        cmp     al, 0Dh
        je      ok
        xor     ch, ch
        mov     cl, al
        call    stuff
        jmp     text

; stuff: store key CX (CH scan, CL char); exits with errorlevel 1 if the
; buffer is full.
stuff:  mov     ah, 05h
        int     16h
        or      al, al
        jnz     full
        ret

ok:     mov     ax, 4C00h
        int     21h
full:   mov     ax, 4C01h
        int     21h
bad:    mov     ax, 4C02h
        int     21h
