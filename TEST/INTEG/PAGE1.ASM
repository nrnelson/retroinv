; PAGE1.COM - start a colour-adapter program on video page 1, and check
; afterwards that page 1 was left alone; or start it with a column count
; other than 80 in the BIOS data area.
;
; usage: PAGE1          paint pages 0 and 1, then select page 1
;        PAGE1 CHECK    check that page 1 is active and as painted
;        PAGE1 WIDE     paint pages 0 and 1, then set the column count
;                       (0040:004A) to 81, the mode left as it is
;
; Both pages get the screen T_VIDEO paints: 2000 cells of characters
; 2Dh, 2Eh, ... 7Eh, 21h, ... (one more per cell, wrapping after 7Eh), the
; last 80 of them blanks; page 0 in attribute 07h, page 1 in 70h, so a
; copy of page 0 over page 1 shows.  An argument starting with W means
; WIDE, any other CHECK; blanks are skipped, since the tail of a
; redirected command may hold some.  Ends with errorlevel 0, or 1 when
; the check fails.  80 x 25 colour text mode at B800h, pages 4096 bytes
; apart.  8086 instructions only.

        cpu     8086
        org     100h

start:  cld
        mov     si, 81h         ; command tail in the PSP, ends with CR
skip:   lodsb
        cmp     al, ' '
        je      skip
        mov     dh, al          ; CR: no argument, paint
        mov     ax, 40h
        mov     ds, ax
        cmp     dh, 0Dh
        je      paint
        cmp     dh, 'W'
        je      paint
        cmp     byte [62h], 1   ; active page
        jne     bad
paint:  mov     ax, 0B800h
        mov     es, ax
        xor     di, di
        mov     cx, 2000
        mov     al, 2Dh
cell:   cmp     cx, 80          ; the last row is blank
        ja      text
        mov     al, ' '
text:   mov     ah, 70h
        cmp     dh, 0Dh
        je      put
        cmp     dh, 'W'
        jne     check
put:    mov     [es:di+1000h], ax
        mov     ah, 07h
        mov     [es:di], ax
        jmp     short next
check:  cmp     [es:di+1000h], ax
        jne     bad
next:   inc     di
        inc     di
        inc     al
        cmp     al, 7Fh
        jb      same
        mov     al, 21h
same:   loop    cell
        cmp     dh, 0Dh
        je      select
        cmp     dh, 'W'
        jne     done            ; CHECK leaves page 1 active
        mov     word [4Ah], 81
        jmp     short done
select: mov     ax, 0501h
        int     10h
done:   mov     ax, 4C00h
        int     21h
bad:    mov     ax, 4C01h
        int     21h
