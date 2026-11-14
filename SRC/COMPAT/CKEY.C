/*
 * CKEY.C - keys from INT 16h and clicks from the mouse (CMOUSE.C), or
 * keys from a key source for tests.
 *
 * INT 16h function 01h reports whether a key is waiting through the
 * zero flag, which the compilers' int86 wrappers do not return, so the
 * poll is a few bytes of machine code:
 *     MOV AH,01h      B4 01
 *     INT 16h         CD 16
 *     MOV [BX],AX     89 07       the waiting key, if any
 *     MOV AX,0        B8 00 00    (MOV leaves the flags alone)
 *     JZ none         74 01
 *     INC AX          40
 *  none:
 * BX points at a near word that receives the key; AX is 1 when a key is
 * waiting, else 0.  A waiting Ctrl-Break (code 0000h) is told from no key
 * by that flag.  Function 00h, which reads the key, goes through int86.
 *
 * The mouse is reached only through key_msfn, which key_mouse sets, so
 * only the programs that call key_mouse link CMOUSE.C.
 *
 * key_stuff writes into the BIOS keyboard buffer itself, as the keyboard
 * interrupt does, since INT 16h function 05h (store a key) exists only
 * in the BIOSes that support the enhanced keyboard, which the PC, XT and
 * early AT lack.  It leaves the interrupt flag as it found it.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include "CMOUSE.H"
#include <stddef.h>
#ifndef COMPAT_HOST
#include <dos.h>
#endif

static key_src_fn ky_src;
static void *ky_arg;

void (*key_idle)(void);

void key_set_source(key_src_fn fn, void *arg)
{
    ky_src = fn;
    ky_arg = arg;
}

/* A waiting Ctrl-Break is KEY_BREAK, never KEY_NONE, so a key is
   waiting exactly when key_peek gives one. */
int key_ready(void)
{
    return key_peek() != KEY_NONE;
}

/* Scan codes 10h..32h of the letter keys, in keyboard order; '.' marks
   the codes that are not letters. */
static const char ky_letters[] = "QWERTYUIOP....ASDFGHJKL.....ZXCVBNM";
#define KY_FIRST        0x10

char key_alt_letter(u16 key)
{
    u16 scan;
    char c;

    if (key < KEY_EXT + KY_FIRST)
        return 0;
    scan = (u16)(key - KEY_EXT - KY_FIRST);
    if (scan >= sizeof ky_letters - 1)
        return 0;
    c = ky_letters[scan];
    return (char)(c == '.' ? 0 : c);
}

#ifdef COMPAT_HOST

u16 key_read(void)
{
    if (ky_src != NULL)
        return (*ky_src)(ky_arg, 0);
    return KEY_EOS;
}

int key_stuff(u16 code)
{
    (void)code;
    return 1;
}

u16 key_peek(void)
{
    if (ky_src != NULL)
        return (*ky_src)(ky_arg, 1);
    return KEY_NONE;
}

#else

#ifdef COMPAT_TC
static u16 ky_poll(u16 *k)
{
    _BX = (unsigned)k;
    __emit__(0xB4, 0x01, 0xCD, 0x16, 0x89, 0x07,
             0xB8, 0x00, 0x00, 0x74, 0x01, 0x40);
    return (u16)_AX;
}
#else
/* Not static: Watcom C 6.5 rejects a static function whose body is an
   aux pragma. */
u16 ky_poll(u16 *k);
#pragma aux ky_poll = \
    0xB4 0x01 0xCD 0x16 0x89 0x07 \
    0xB8 0x00 0x00 0x74 0x01 0x40 \
    parm [bx] value [ax] modify [ax];
#endif

/* The KEY_ code of the BIOS key word ax: the ASCII code in AL, or
   KEY_EXT plus the scan code in AH when AL is 00h, or E0h with a
   nonzero scan code (the grey keys of an enhanced keyboard). */
static u16 ky_code(u16 ax)
{
    u16 al;
    u16 ah;

    al = (u16)(ax & 0xFFu);
    ah = (u16)(ax >> 8);
    if (al == 0 || (al == 0xE0u && ah != 0))
        return (u16)(KEY_EXT + ah);
    return al;
}

key_ms_fn key_msfn;

/* The key waiting, else the mouse's press, or KEY_NONE; read when op is
   KEY_MS_TAKE (else KEY_MS_PEEK). */
static u16 ky_next(int op)
{
    union REGS r;
    u16 k;

    if (ky_poll(&k)) {
        if (op == KEY_MS_TAKE) {
            r.x.ax = 0x0000;
            int86(0x16, &r, &r);
            k = (u16)r.x.ax;
        }
        return ky_code(k);
    }
    return key_msfn == NULL ? KEY_NONE : (*key_msfn)(op);
}

u16 key_read(void)
{
    if (ky_src != NULL)
        return (*ky_src)(ky_arg, 0);
    if (key_msfn != NULL)
        (*key_msfn)(KEY_MS_SHOW);
    while (ky_next(KEY_MS_PEEK) == KEY_NONE) {
        dos_idle();
        if (key_idle != NULL)
            (*key_idle)();
    }
    if (key_msfn != NULL)
        (*key_msfn)(KEY_MS_HIDE);
    return ky_next(KEY_MS_TAKE);
}

u16 key_peek(void)
{
    if (ky_src != NULL)
        return (*ky_src)(ky_arg, 1);
    return ky_next(KEY_MS_PEEK);
}

/* The BIOS keyboard buffer: a ring of key words in segment 0040h, from
   the offset at 0040:0080 up to the one at 0040:0082 (excluded), with
   the next key to read at the offset in 0040:001A (head) and the next
   free slot at 0040:001C (tail); it is full when advancing the tail
   would reach the head.  The PC and XT BIOSes keep no bounds at 0080h
   and 0082h and always use 001Eh..003Eh, so bounds that do not describe
   a ring holding the head and tail fall back to those; a head or tail
   that lies outside that ring too, or not on a word of it, is refused,
   since writing there could damage the BIOS data. */
#define KY_HEAD         0x1A
#define KY_TAIL         0x1C
#define KY_START        0x80
#define KY_END          0x82
#define KY_DEFSTART     0x1Eu
#define KY_DEFEND       0x3Eu

static u16 ky_bdaw(u16 off)
{
    return *CFP(volatile u16, BDA_SEG, off);
}

/* 1 when head and tail are word slots of the ring start..end. */
static int ky_inring(u16 start, u16 end, u16 head, u16 tail)
{
    if (start < KY_DEFSTART || end < start + 4 || ((end - start) & 1))
        return 0;
    if (head < start || head >= end || ((head - start) & 1))
        return 0;
    if (tail < start || tail >= end || ((tail - start) & 1))
        return 0;
    return 1;
}

int key_stuff(u16 code)
{
    u16 start;
    u16 end;
    u16 head;
    u16 tail;
    u16 next;
    u16 fl;
    int rc;

    rc = 1;
    fl = IRQ_SAVE();
    IRQ_OFF();
    start = ky_bdaw(KY_START);
    end = ky_bdaw(KY_END);
    head = ky_bdaw(KY_HEAD);
    tail = ky_bdaw(KY_TAIL);
    if (!ky_inring(start, end, head, tail)) {
        start = KY_DEFSTART;
        end = KY_DEFEND;
    }
    if (ky_inring(start, end, head, tail)) {
        next = (u16)(tail + 2);
        if (next >= end)
            next = start;
        if (next != head) {
            *CFP(volatile u16, BDA_SEG, tail) = code;
            *CFP(volatile u16, BDA_SEG, KY_TAIL) = next;
            rc = 0;
        }
    }
    if (fl & IRQ_IF)
        IRQ_ON();
    return rc;
}

#endif
