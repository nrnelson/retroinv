; ZEROMEM.COM - zero the conventional memory DOS gives this program.
;
; WCC 6.5 reads memory it has not written, so an object depends on the
; bytes the program before it left behind.  Run before each compile,
; this program, loaded where the compiler loads, zeroes its whole block
; from 200h bytes past its PSP (past its own code) to the block's end,
; the PSP's word at 2, then ends with errorlevel 0.  Its stack, at the
; top of its first 64 KB, is zeroed too; nothing on it is used again.
; 8086 instructions only.

        cpu     8086
        org     100h

start:  cld
        xor     ax, ax
        mov     dx, cs
        add     dx, 20h         ; first paragraph to zero
        mov     bx, [2]         ; first paragraph past the block
next:   mov     cx, bx
        sub     cx, dx          ; paragraphs left
        jbe     done
        cmp     cx, 800h        ; at most 32 KB at a time
        jbe     part
        mov     cx, 800h
part:   mov     es, dx
        add     dx, cx
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1           ; words
        xor     di, di
        rep     stosw
        jmp     next
done:   mov     ax, 4C00h
        int     21h
