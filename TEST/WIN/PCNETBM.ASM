; PCNETBM.ASM - turn on bus mastering of the test machine's PCnet card.
;
; usage: PCNETBM        errorlevel 1 when the card is ready, 0 when there
;                       is none, so that a batch file starts the network
;                       with IF ERRORLEVEL 1 and a machine without the
;                       card goes on with errorlevel 0
;
; QEMU's AMD PCnet (vendor 1022h, device 2000h) is a PCI bus master, which
; Microsoft's AM2100.DOS, written for the ISA NE2100, never enables and
; the BIOS leaves off.  The PCI BIOS finds the first such card (INT 1Ah
; B102h), and bit 2 of its command register (04h) is set by a read
; (B109h) and a write (B10Ch) of that word.  B102h finds the card when it
; returns with the carry clear and AH 0; a machine without the card sets
; the carry, one without a PCI BIOS leaves AH as it was.  8086
; instructions only.

        cpu     8086
        org     100h

PCI_FIND    equ     0B102h      ; find a device: CX device, DX vendor
PCI_READW   equ     0B109h      ; read a word: BX device, DI register
PCI_WRITEW  equ     0B10Ch      ; write CX: BX device, DI register
PCNET_VEN   equ     1022h
PCNET_DEV   equ     2000h
PCI_CMD     equ     04h         ; the command register
CMD_MASTER  equ     4           ; its bus master enable bit

start:  mov     ax, PCI_FIND
        mov     cx, PCNET_DEV
        mov     dx, PCNET_VEN
        xor     si, si
        int     1Ah
        mov     al, 0
        jc      done
        test    ah, ah
        jnz     done
        mov     ax, PCI_READW
        mov     di, PCI_CMD
        int     1Ah
        or      cl, CMD_MASTER
        mov     ax, PCI_WRITEW
        int     1Ah
        mov     al, 1
done:   mov     ah, 4Ch
        int     21h
