; CUARTISR.ASM - the interrupt handler of a serial port, CUART.C.
;
; CUART.C keeps these bytes as cuart_isr and copies them into each open
; port's far block at UB_ISR, the block starting at offset 0 of its own
; segment; make lint assembles this file with "nasm -f bin" and compares
; the result with that array, and the values of the constants it names
; here with those of CUART.C (TEST/ASMCHK.PY).  The block:
;
;   +UB_RXHEAD  the next receive-ring byte the program takes, a word
;   +UB_RXTAIL  the next receive-ring byte the handler fills
;   +UB_TXHEAD  the next transmit-ring byte the handler sends
;   +UB_TXTAIL  the next transmit-ring byte the program fills
;   +UB_BASE    the port's uart_info: its I/O base, IRQ and chip, then
;               the counts UB_OVR, UB_FRM and UB_PAR, and that of the
;               lost bytes, which stays 0 here
;   +UB_OLDVEC  the vector uart_open found, offset then segment
;   +UB_IER     the chip's interrupt enables, the transmitter's aside
;   +UB_FLAGS   uart_open's flags, and UB_SHUT once the port is closed
;   +UB_LSR     the LSR the last pass read, a word whose high byte is 0;
;               then two bytes that only CUART.C uses
;   +UB_ISR     this code
;   +UB_RXBUF   the receive ring, UB_RXMASK + 1 bytes
;   +UB_TXBUF   the transmit ring, UB_TXMASK + 1 bytes
;
; The chip is served in passes.  A pass reads MSR, which clears a
; modem-status interrupt; moves every byte received into the receive
; ring; and loads one byte of the transmit ring into THR when THR is
; empty, that ring is not, and, under UART_F_RTSCTS, CTS is up, zeroing
; the byte in the ring.  It leaves the transmitter's interrupt (IER_TX)
; on while a byte waits for THR and off otherwise, so that an empty ring
; or CTS down raises nothing.  A receive ring with no room for the
; next byte holds the receiver: IER_RX is cleared in UB_IER, and so in
; the chip, and the pass takes no more bytes from the chip, which keeps
; them (in its FIFO, if it has one) until CUART.C, having emptied the
; ring to UART_RXLOW bytes, sets IER_RX again; no byte is taken and
; dropped.  Under UART_F_RTSCTS a ring that reaches UB_RXHIGH bytes drops
; RTS (MCR bit 1).  Overruns, and the parity and framing errors of the
; byte waiting in RBR, are counted as LSR reports them, held or not, so
; before the byte is stored; the last LSR the pass read is left in
; UB_LSR.
;
; isr is the port's interrupt handler.  It sends the end of interrupt to
; the PIC first and keeps interrupts disabled throughout, so that the
; interrupt cannot come in again before it returns, and makes passes
; until the chip's IIR reports no interrupt pending.  Once uart_close
; has set UB_SHUT it serves nothing and passes every interrupt on to
; UB_OLDVEC.  poll is a far procedure that makes one pass, without the
; end of interrupt; CUART.C calls it with interrupts disabled.
;
; The code runs in the block's segment, CS = DS there.  It keeps every
; register it uses and never enables interrupts.  8086 instructions
; only.

        cpu     8086

UB_RXHEAD       equ     0
UB_RXTAIL       equ     2
UB_TXHEAD       equ     4
UB_TXTAIL       equ     6
UB_BASE         equ     8
UB_OVR          equ     12
UB_FRM          equ     14
UB_PAR          equ     16
UB_OLDVEC       equ     20
UB_IER          equ     24
UB_FLAGS        equ     25
UB_LSR          equ     26
UB_ISR          equ     30
UB_SHUT         equ     80h
UB_RTSCTS       equ     01h             ; UART_F_RTSCTS
UB_RXMASK       equ     1023            ; UART_RXSZ - 1
UB_TXMASK       equ     1023            ; UART_TXSZ - 1
UB_RXHIGH       equ     768             ; UART_RXHIGH

UR_IER          equ     1               ; the chip's registers
UR_MCR          equ     4
UR_LSR          equ     5
UR_MSR          equ     6
IER_RX          equ     01h
IER_TX          equ     02h
IIR_NONE        equ     01h
MCR_RTS         equ     02h
LSR_DR          equ     01h
LSR_OE          equ     02h
LSR_PE          equ     04h
LSR_FE          equ     08h
LSR_THRE        equ     20h
MSR_CTS         equ     10h
PIC_CMD         equ     20h
PIC_EOI         equ     20h

        org     UB_ISR

isr:    test    byte [cs:UB_FLAGS], UB_SHUT
        jz      eoi
        jmp     far [cs:UB_OLDVEC]
eoi:    push    ax
        mov     al, PIC_EOI
        out     PIC_CMD, al             ; AL nonzero: pass until done
        jmp     short serve
poll:   pushf
        push    cs
        call    once                    ; returns through the IRET
        retf
once:   push    ax
        mov     al, 0                   ; AL 0: one pass
serve:  push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    cs
        pop     ds
        mov     cl, al
        mov     di, [UB_BASE]
pass:   lea     dx, [di+UR_MSR]
        in      al, dx
        mov     ah, al                  ; AH = MSR for the transmitter
rx:     lea     dx, [di+UR_LSR]
        in      al, dx
        test    al, LSR_OE              ; first: this read clears OE
        jz      .dr
        inc     word [UB_OVR]
.dr:    test    al, LSR_DR
        jz      tx
        test    al, LSR_PE              ; now: the read cleared PE and FE
        jz      .fe
        inc     word [UB_PAR]
.fe:    test    al, LSR_FE
        jz      .held
        inc     word [UB_FRM]
.held:  test    byte [UB_IER], IER_RX
        jz      tx                      ; held: the bytes wait in the chip
        mov     bx, [UB_RXTAIL]
        lea     si, [bx+1]
        and     si, UB_RXMASK
        cmp     si, [UB_RXHEAD]
        jne     .get
        and     byte [UB_IER], ~IER_RX & 0FFh   ; full: hold the receiver
        jmp     tx
.get:   mov     dx, di
        in      al, dx
        mov     [bx+UB_RXBUF], al
        mov     [UB_RXTAIL], si
        test    byte [UB_FLAGS], UB_RTSCTS
        jz      rx
        sub     si, [UB_RXHEAD]
        and     si, UB_RXMASK
        cmp     si, UB_RXHIGH
        jb      rx
        lea     dx, [di+UR_MCR]
        in      al, dx
        and     al, ~MCR_RTS & 0FFh
        out     dx, al
        jmp     rx
tx:     mov     [UB_LSR], al
        mov     bx, [UB_TXHEAD]
        cmp     bx, [UB_TXTAIL]
        je      txoff
        test    byte [UB_FLAGS], UB_RTSCTS
        jz      .thr
        test    ah, MSR_CTS
        jz      txoff
.thr:   test    al, LSR_THRE
        jz      txon
        mov     al, 0
        xchg    al, [bx+UB_TXBUF]
        mov     dx, di
        out     dx, al
        inc     bx
        and     bx, UB_TXMASK
        mov     [UB_TXHEAD], bx
txon:   mov     al, IER_TX
        jmp     short setier
txoff:  mov     al, 0
setier: or      al, [UB_IER]
        lea     dx, [di+UR_IER]
        out     dx, al
        test    cl, cl
        jz      done
        inc     dx                      ; IIR
        in      al, dx
        test    al, IIR_NONE
        jnz     done
        jmp     pass
done:   pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

UB_POLL         equ     poll
UB_RXBUF        equ     $
UB_TXBUF        equ     UB_RXBUF + UB_RXMASK + 1
