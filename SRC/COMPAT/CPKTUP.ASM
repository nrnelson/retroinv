; CPKTUP.ASM - the receive upcall of the packet driver layer, CPKT.C.
;
; CPKT.C keeps these bytes in its data segment as cpkt_up and gives their
; address to the driver as the receiver of its frames; make lint
; assembles this file with "nasm -f bin" and compares the result with
; that array, and the value of CPKT_UP_HEAD here with the one there
; (TEST/ASMCHK.PY).  The driver calls the receiver twice for each frame,
; with a far call, usually from its interrupt handler:
;
;   AX = 0, CX = the frame's length: return in ES:DI the buffer to copy
;           the frame into, or 0:0 to drop it;
;   AX = 1, DS:SI = that buffer: the frame is in it.
;
; The receive buffers form a ring.  Each is a header and the frame:
;
;   +0         state, a word: CB_FREE, CB_BUSY (filling, or held by the
;              stack) or CB_FULL (received, not yet taken)
;   +CB_LEN    the frame's length, a word
;   +CB_NEXT   the next buffer of the ring
;   +CB_FRAME  the frame, at most CPKT_FRAME bytes
;
; The receiver offers only the buffer at the ring's head, so that the
; stack takes the frames in the order they came; when that buffer is not
; free, or the frame is too long, the frame is dropped.  The head pointer
; is CPKT.C's cpkt_head, whose address CPKT.C writes into the operand at
; CPKT_UP_HEAD when it opens the driver.
;
; The code runs in the data segment, so CS addresses the ring on the
; first call; on the second, DS is the segment returned in ES.  As the
; specification's own receivers do, the upcall keeps DS, SS, SP and the
; flags; it changes BX and, on the first call, AX, DI and ES, which the
; driver saves when it needs them.  It never enables interrupts.
; 8086 instructions only.

        cpu     8086
        org     0

CB_FREE         equ     0
CB_BUSY         equ     1
CB_FULL         equ     2
CB_LEN          equ     2
CB_NEXT         equ     4
CB_FRAME        equ     6
CPKT_FRAME      equ     1514

upcall: pushf
        test    ax, ax
        jnz     full
        cmp     cx, CPKT_FRAME
        ja      drop
CPKT_UP_HEAD    equ     $ + 1
        mov     bx, 0                   ; &cpkt_head, set by CPKT.C
        mov     di, [cs:bx]
        cmp     word [cs:di], CB_FREE
        jne     drop
        mov     word [cs:di], CB_BUSY
        mov     [cs:di+CB_LEN], cx
        mov     ax, [cs:di+CB_NEXT]
        mov     [cs:bx], ax
        add     di, CB_FRAME
        push    cs
        pop     es
        popf
        retf
drop:   xor     di, di
        mov     es, di
        popf
        retf
full:   mov     word [si-CB_FRAME], CB_FULL
        popf
        retf
