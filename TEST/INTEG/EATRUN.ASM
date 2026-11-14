; EATRUN.COM - run a program with a given amount of free conventional
; memory, to find or check the memory it needs.
;
; usage: EATRUN kb file [arguments]
;
;   kb    kilobytes to leave free, 0..640, a plain decimal number
;   file  the program to run: its file name with the extension (.COM or
;         .EXE), not looked up on the PATH
;
; Shrinks its own memory block so that, above it, kb kilobytes stay free
; for the program, plus 32 paragraphs that cover the environment and the
; memory control blocks of that program and one paragraph for the memory
; control block of the free block itself; then runs file with the
; arguments as its command tail, the blank before them included, a copy
; of the environment and the two FCBs that INT 21h function 29h parses
; from the arguments, as COMMAND.COM gives them, so that a program that
; takes a drive from its first FCB, such as CHKDSK, sees its own.  The
; program inherits the standard handles, so a batch line's redirections
; apply to it.  CHKDSK run this way reports kb KB free, as the program
; run the same way finds it.  Ends with the program's exit code, or with
; errorlevel 255 when kb is not such a number, leaves this program too
; little memory, or file cannot be run.  8086 instructions only.

        cpu     8086
        org     100h

start:  cld
        mov     sp, stktop      ; the block may end below 64 KB
        mov     si, 81h         ; command tail in the PSP, ends with CR
        call    skipbl
        xor     bx, bx          ; kb, kept <= 640 so that it cannot wrap
        xor     cx, cx          ; digits read
number: sub     al, '0'
        cmp     al, 9
        ja      endnum
        cbw
        xchg    ax, bx
        mov     dx, 10
        mul     dx
        add     bx, ax
        cmp     bx, 640
        ja      fail
        inc     cx
        lodsb
        jmp     number
endnum: test    cx, cx
        jz      fail
        add     al, '0'         ; the number must end at a blank
        cmp     al, ' '
        je      size
        cmp     al, 9
        jne     fail
size:   mov     cl, 6           ; paragraphs left free: kb * 64 + 33
        shl     bx, cl
        add     bx, 33
        mov     ax, [2]         ; the top of memory, from the PSP
        mov     dx, cs
        sub     ax, dx          ; paragraphs from the PSP to the top
        sub     ax, bx
        jb      fail
        cmp     ax, IMGPARA
        jb      fail
        mov     bx, ax
        mov     ah, 4Ah         ; ES is the PSP: resize this block
        int     21h
        jc      fail
        call    skipbl          ; the file name, NUL-terminated in place
        cmp     al, 0Dh
        je      fail
        lea     dx, [si-1]
name:   lodsb
        cmp     al, ' '
        je      named
        cmp     al, 9
        je      named
        cmp     al, 0Dh
        jne     name
named:  mov     byte [si-1], 0
        mov     di, tail + 1    ; a blank and the rest are the tail
        xor     cx, cx
        cmp     al, 0Dh
        je      tailed
        mov     al, ' '
        stosb
        inc     cx
copy:   lodsb
        cmp     al, 0Dh
        je      tailed
        stosb
        inc     cx
        jmp     copy
tailed: mov     al, 0Dh
        stosb
        mov     [tail], cl
        mov     si, tail + 1    ; the FCBs, leading blanks skipped
        mov     di, fcb1
        mov     ax, 2901h
        int     21h
        mov     di, fcb2        ; SI is past the first name
        mov     ax, 2901h
        int     21h
        mov     [pblock + 4], cs
        mov     [pblock + 8], cs
        mov     [pblock + 12], cs
        mov     bx, pblock
        mov     ax, 4B00h       ; DOS keeps only CS:IP across EXEC
        int     21h
        mov     bx, cs
        cli
        mov     ss, bx
        mov     sp, stktop
        sti
        mov     ds, bx
        mov     es, bx
        jc      fail
        mov     ah, 4Dh         ; the program's exit code in AL
        int     21h
        mov     ah, 4Ch
        int     21h
fail:   mov     ax, 4CFFh
        int     21h

; skipbl - AL = the first character at SI that is not a blank, SI past it.
skipbl: lodsb
        cmp     al, ' '
        je      skipbl
        cmp     al, 9
        je      skipbl
        ret

; The EXEC parameter block: a copy of the environment, then the command
; tail and the two FCBs, their segments set before the call.
pblock: dw      0
        dw      tail, 0
        dw      fcb1, 0
        dw      fcb2, 0
tail:   times   128 db 0        ; length, at most 126 bytes, CR
fcb1:   times   37 db 0
fcb2:   times   37 db 0
        times   256 db 0        ; the stack
stktop:

IMGPARA equ     ($ - $$ + 100h + 15) / 16
