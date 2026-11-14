; MBR.ASM - the master boot code of the Windows 3.0 test machine's disk.
;
; Moves itself from 0:7C00h to 0:0600h, finds the active entry of the
; partition table, reads that partition's first sector to 0:7C00h with
; INT 13h, by the CHS address the entry gives (five tries, a disk reset
; between them), and jumps to it with DL the boot drive and DS:SI the
; entry, as MS-DOS's boot sector expects.  No active entry, a failed read
; or a sector without the 55h AAh mark prints "No boot" and halts.  8086
; instructions only, so that an 8086 machine boots the disk; the bytes
; fill the 440 before the disk signature and the partition table.

        cpu     8086
        org     0600h

PART_TABLE  equ     07BEh       ; the table, once moved to 0600h
BOOT_AT     equ     7C00h       ; where a boot sector runs
TRIES       equ     5

start:  cli
        xor     ax, ax
        mov     ss, ax
        mov     sp, BOOT_AT
        mov     ds, ax
        mov     es, ax
        sti
        cld
        mov     si, BOOT_AT
        mov     di, 0600h
        mov     cx, 256
        rep     movsw
        jmp     0:moved
moved:  mov     si, PART_TABLE
        mov     cx, 4
find:   cmp     byte [si], 80h
        je      found
        add     si, 16
        loop    find
        jmp     fail
found:  mov     di, TRIES
read:   mov     bx, BOOT_AT
        mov     ax, 0201h
        mov     dh, [si + 1]
        mov     cx, [si + 2]
        int     13h
        jnc     check
        xor     ax, ax
        int     13h
        dec     di
        jnz     read
        jmp     fail
check:  cmp     word [BOOT_AT + 510], 0AA55h
        jne     fail
        jmp     0:BOOT_AT
fail:   mov     si, msg
print:  lodsb
        or      al, al
        jz      halt
        mov     ah, 0Eh
        mov     bx, 7
        int     10h
        jmp     print
halt:   hlt
        jmp     halt

msg     db      'No boot', 0

        times   440 - ($ - $$) db 0
