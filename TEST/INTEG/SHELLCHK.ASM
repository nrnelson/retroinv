; SHELLCHK.COM - check that the shell reloads its transient part after a
; program that overwrote it.
;
; COMMAND.COM keeps its transient part in the free memory at the top, which
; a program may use, and on the program's return trusts a 16-bit
; one's-complement sum of words to tell whether it must reload that part
; from disk; a program that leaves bytes there with the same sum makes it
; jump into them.  This program makes that happen on purpose: in every
; paragraph of the 64 KB below the top of memory (PSP:0002) it rotates the
; last five of the eight words, which garbles most instructions there and
; keeps the sum of words over any range that starts on a paragraph and
; ends 0, 2, 4 or 6 bytes past one.  The shells' ranges are such ranges:
; each starts at a paragraph plus 100h and is 3A00h bytes long on MS-DOS
; 3.10, 3E44h on 3.30 and 87C2h on 5.00, all within those 64 KB.  A shell
; that trusts its sum then hangs or crashes; one that reloads runs the
; next command line.  Ends with errorlevel 0, or 1, having changed
; nothing, when those 64 KB would reach below the end of its own 64 KB
; segment.  8086 instructions only.

        cpu     8086
        org     100h

TOPPARAS equ    1000h           ; the 64 KB below the top, in paragraphs
SUMLEN  equ     87C2h           ; the longest range a shell sums (MS-DOS 5.00)

%if TOPPARAS * 16 < 100h + SUMLEN
%error "TOPPARAS does not cover the longest summed range"
%endif

start:  mov     ax, [2]
        sub     ax, TOPPARAS
        mov     bx, cs
        add     bx, 1000h
        cmp     ax, bx
        jb      low
        mov     cx, TOPPARAS
para:   mov     ds, ax
        mov     bx, [6]
        xchg    bx, [8]
        xchg    bx, [10]
        xchg    bx, [12]
        xchg    bx, [14]
        mov     [6], bx
        inc     ax
        loop    para
        mov     ax, 4C00h
        int     21h
low:    mov     ax, 4C01h
        int     21h
