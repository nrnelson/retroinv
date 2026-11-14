; WRAPCHK.COM - check that the emulator wraps IP to 16 bits on a near CALL.
;
; A near CALL whose displacement carries IP below offset 0 of the code
; segment must land at the wrapped offset in the same segment, as on an
; 8086.  Open Watcom medium-model programs depend on this: the linker turns
; far calls within one code segment into PUSH CS plus a near CALL.  The
; program builds a segment X about 64 KB above its own, puts at X:0100 a
; CALL to X:FF00 (displacement FDFDh), at X:FF00 a far jump to "ok", and
; at the linear address an unwrapped IP would reach (X*16 - 100h) a far
; jump to "bug"; then it enters X:0100.  It prints "WRAP OK" or "WRAP BUG"
; and ends with errorlevel 0 or 1.  It uses the 130 KB above its own
; segment, which DOS leaves free for a .COM program.  8086 instructions
; only.

        cpu     8086
        org     100h

start:  mov     ax, cs
        add     ax, 1000h
        and     ax, 0FF00h
        add     ax, 80h         ; X: linear base with low 12 bits 800h,
                                ; so that the CALL and the unwrapped
                                ; target lie in one 4 KB page, the case
                                ; QEMU's in-page block chaining gets wrong
        mov     es, ax
        mov     [xseg], ax
        mov     byte [es:100h], 0E8h        ; X:0100  CALL FF00h
        mov     word [es:101h], 0FDFDh
        mov     byte [es:0FF00h], 0EAh      ; X:FF00  JMP FAR cs:ok
        mov     word [es:0FF01h], ok
        mov     [es:0FF03h], cs
        sub     ax, 10h                     ; (X-10h):0000 = X*16 - 100h
        mov     es, ax
        mov     byte [es:0], 0EAh           ;         JMP FAR cs:bug
        mov     word [es:1], bug
        mov     [es:3], cs
        push    word [xseg]
        mov     ax, 100h
        push    ax
        retf                                ; enter X:0100

ok:     mov     dx, mok
        mov     bl, 0
        jmp     short done
bug:    mov     dx, mbug
        mov     bl, 1
done:   push    cs
        pop     ds
        mov     ah, 9
        int     21h
        mov     al, bl
        mov     ah, 4Ch
        int     21h

xseg    dw      0
mok     db      'WRAP OK', 13, 10, '$'
mbug    db      'WRAP BUG', 13, 10, '$'
