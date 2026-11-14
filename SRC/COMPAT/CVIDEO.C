/*
 * CVIDEO.C - the 80 x 25 text screen: a far shadow with changed-cell
 * spans, written to the adapter directly or through INT 10h.
 *
 * Drawing changes only the shadow, a block of VID_CELLS character and
 * attribute pairs.  Each row keeps the first and last column changed
 * since the last flush (VS_CLEAN when none), and only a cell whose
 * character or attribute really differs marks its row.  vid_flush
 * copies those spans to the screen and clears the marks.  The host
 * build stops there: it has no screen.
 *
 * Under DOS the adapter, mode, page and row count come from the BIOS
 * data area and INT 10h, as COMPAT.H describes.  Direct output copies
 * words between far memory and video memory, through the CGA snow loops
 * below when snow avoidance applies; BIOS output uses INT 10h functions
 * 02h, 08h and 09h only.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include <stddef.h>
#ifndef COMPAT_HOST
#include <dos.h>
#include <string.h>
#endif
#ifdef COMPAT_OW
#include <i86.h>
#endif

#define VS_CLEAN        0xFFu   /* row mark: nothing changed            */
#define VS_SPACE        0x20u   /* character of a cleared cell          */
#define VS_NORMAL       0x07u   /* attribute of a cleared cell          */
#define VS_HDR          4       /* vid_save block header: row col h w   */

static cmem_blk *vs_sh;         /* the shadow, VID_CELLS * 2 bytes      */
static cmem_blk *vs_dos;        /* the DOS screen, unless in page 1     */
static u8 vs_lo[VID_ROWS];      /* first changed column, or VS_CLEAN    */
static u8 vs_hi[VID_ROWS];      /* last changed column                  */
static u8 vs_isopen;
static u8 vs_adapter;           /* VID_A_ code                          */
static u8 vs_mode;              /* text mode in use: 2, 3 or 7          */
static u8 vs_bios;              /* 1 with VID_F_BIOS                    */
static u8 vs_snowon;            /* snow avoidance asked for             */
static u8 vs_crow;              /* cursor as last set by vid_cursor     */
static u8 vs_ccol;
static u8 vs_cshape;
static u16 vs_flushed;          /* cells written by the last vid_flush  */

/* ---------------------------------------------------------------------
 * The shadow.
 * --------------------------------------------------------------------- */

/* The byte offset of cell row, col in the shadow and in video memory. */
static u16 vs_off(u8 row, u8 col)
{
    return (u16)(((unsigned)row * VID_COLS + col) * 2u);
}

static FARP(u8) vs_cell(u8 row, u8 col)
{
    return cmem_ptr(vs_sh) + vs_off(row, col);
}

static void vs_mark(u8 row, u8 col)
{
    if (vs_lo[row] == VS_CLEAN) {
        vs_lo[row] = col;
        vs_hi[row] = col;
        return;
    }
    if (col < vs_lo[row])
        vs_lo[row] = col;
    if (col > vs_hi[row])
        vs_hi[row] = col;
}

static void vs_markall(void)
{
    u8 r;

    for (r = 0; r < VID_ROWS; r++) {
        vs_lo[r] = 0;
        vs_hi[r] = VID_COLS - 1;
    }
}

/* Cut n cells at row, col to the screen; 0 when nothing is left or the
   screen is not open. */
static u16 vs_clip(u8 row, u8 col, u16 n)
{
    if (!vs_isopen || row >= VID_ROWS || col >= VID_COLS)
        return 0;
    if (n > (u16)(VID_COLS - col))
        n = (u16)(VID_COLS - col);
    return n;
}

void vid_put(u8 row, u8 col, const char *s, u16 n, u8 attr)
{
    FARP(u8) p;
    u8 ch;

    n = vs_clip(row, col, n);
    if (n == 0)
        return;
    p = vs_cell(row, col);
    while (n-- != 0) {
        ch = (u8)*s++;
        if (p[0] != ch || p[1] != attr) {
            p[0] = ch;
            p[1] = attr;
            vs_mark(row, col);
        }
        p += 2;
        col++;
    }
}

/* Store n character and attribute pairs along row from col, marking
   the cells that change; the pairs are read at src, advancing step
   bytes after each (0 repeats one pair).  The n cells lie on the
   screen. */
static void vs_store(u8 row, u8 col, FARP(u8) src, u16 n, u16 step)
{
    FARP(u8) p;

    p = vs_cell(row, col);
    while (n-- != 0) {
        if (p[0] != src[0] || p[1] != src[1]) {
            p[0] = src[0];
            p[1] = src[1];
            vs_mark(row, col);
        }
        p += 2;
        src += step;
        col++;
    }
}

void vid_fill(u8 row, u8 col, u16 n, u8 ch, u8 attr)
{
    u8 cell[2];

    n = vs_clip(row, col, n);
    if (n == 0)
        return;
    cell[0] = ch;
    cell[1] = attr;
    vs_store(row, col, (FARP(u8))cell, n, 0);
}

void vid_getrow(u8 row, u8 *cells)
{
    if (!vs_isopen || row >= VID_ROWS)
        return;
    cmem_get(vs_sh, vs_off(row, 0), cells, VID_ROWBYTES);
}

cmem_blk *vid_save(u8 row, u8 col, u8 h, u8 w)
{
    cmem_blk *b;
    FARP(u8) d;
    u16 n;
    u8 r;

    if (!vs_isopen)
        return NULL;
    if (row >= VID_ROWS || col >= VID_COLS) {
        h = 0;
        w = 0;
    } else {
        if (h > VID_ROWS - row)
            h = (u8)(VID_ROWS - row);
        if (w > VID_COLS - col)
            w = (u8)(VID_COLS - col);
    }
    n = (u16)((u16)w * 2u);
    b = cmem_alloc((u16)(VS_HDR + h * n));
    if (b == NULL)
        return b;
    d = cmem_ptr(b);
    d[0] = row;
    d[1] = col;
    d[2] = h;
    d[3] = w;
    for (r = 0; r < h; r++)
        cmem_copy(b, (u16)(VS_HDR + r * n), vs_sh,
                  vs_off((u8)(row + r), col), n);
    return b;
}

void vid_restore(cmem_blk *b)
{
    FARP(u8) s;
    u8 row;
    u8 col;
    u8 h;
    u8 w;
    u8 r;

    if (b == NULL)
        return;
    if (vs_isopen) {
        s = cmem_ptr(b);
        row = s[0];
        col = s[1];
        h = s[2];
        w = s[3];
        s += VS_HDR;
        /* vid_save cut the box to the screen. */
        for (r = 0; r < h; r++) {
            vs_store((u8)(row + r), col, s, w, 2);
            s += (u16)w * 2u;
        }
    }
    cmem_free(b);
}

void vid_invalidate(void)
{
    if (vs_isopen)
        vs_markall();
}

u16 vid_flushed(void)
{
    return vs_flushed;
}

void vid_cursor_get(u8 *row, u8 *col, u8 *shape)
{
    *row = vs_crow;
    *col = vs_ccol;
    *shape = vs_cshape;
}

u8 vid_adapter(void)
{
    return vs_adapter;
}

int vid_mono(void)
{
    return (vs_mode == 7 || vs_mode == 2) ? 1 : 0;
}

void vid_snow(int on)
{
    vs_snowon = (u8)(on ? 1 : 0);
}

/* Free the shadow and the block that saves the DOS screen, and mark
   the screen closed. */
static void vs_free(void)
{
    cmem_free(vs_sh);
    cmem_free(vs_dos);
    vs_sh = NULL;
    vs_dos = NULL;
    vs_isopen = 0;
}

/* Allocate the shadow, cleared, every cell marked, and with keep the
   block that saves the DOS screen.  Returns 0 or CF_ENOMEM with nothing
   allocated. */
static int vs_alloc(int keep)
{
    FARP(u8) p;
    u16 i;

    vs_sh = cmem_alloc(VID_CELLS * 2);
    if (keep)
        vs_dos = cmem_alloc(VID_CELLS * 2);
    if (vs_sh == NULL || (keep && vs_dos == NULL)) {
        vs_free();
        return CF_ENOMEM;
    }
    p = cmem_ptr(vs_sh);
    for (i = 0; i < VID_CELLS; i++) {
        *p++ = VS_SPACE;
        *p++ = VS_NORMAL;
    }
    vs_markall();
    return 0;
}

#ifdef COMPAT_HOST

int vid_open(u8 flags)
{
    if (vs_isopen)
        return 0;
    if (vs_alloc(0) != 0)
        return CF_ENOMEM;
    if (flags & VID_F_MDA) {
        vs_adapter = VID_A_MDA;
        vs_mode = 7;
    } else {
        vs_adapter = VID_A_CGA;
        vs_mode = 3;
    }
    vs_bios = (u8)((flags & VID_F_BIOS) ? 1 : 0);
    vs_snowon = (u8)((flags & VID_F_SNOW) ? 1 : 0);
    vs_isopen = 1;
    vid_cursor(0, 0, VID_CUR_HIDE);
    return 0;
}

void vid_close(void)
{
    if (vs_isopen)
        vs_free();
}

void vid_flush(void)
{
    u8 r;

    vs_flushed = 0;
    if (!vs_isopen)
        return;
    for (r = 0; r < VID_ROWS; r++) {
        if (vs_lo[r] != VS_CLEAN)
            vs_flushed = (u16)(vs_flushed + vs_hi[r] - vs_lo[r] + 1);
        vs_lo[r] = VS_CLEAN;
    }
}

void vid_cursor(u8 row, u8 col, u8 shape)
{
    vs_crow = row;
    vs_ccol = col;
    vs_cshape = shape;
}

int vid_verify(u8 *row, u8 *col)
{
    *row = 0;
    *col = 0;
    return 0;
}

int vid_snow_active(void)
{
    return vs_isopen && vs_snowon && !vs_bios && vs_adapter == VID_A_CGA;
}

#else

/* ---------------------------------------------------------------------
 * CGA snow avoidance.
 *
 * Both loops take a near pointer in BX to vs_xp, six words: source
 * offset and segment, destination offset and segment, the word count
 * (1 or more) and the status port.  They save every register they
 * change except AX, BX, CX and DX, so the same bytes serve the Watcom
 * aux pragmas and Turbo C's __emit__.  BP counts down the words of the
 * current vertical-retrace burst; it is set to VID_VBURST at the start
 * and whenever bit 3 reads 0, and once it goes negative every word waits
 * again.  Each wait for bit 0 polls the port at most VS_SPIN times (CX,
 * saved around the wait, counts the polls with LOOPNZ and LOOPZ), so an
 * adapter whose status port never changes cannot hold the machine,
 * interrupts disabled, for ever: when a wait runs out the loop enables
 * interrupts and returns the number of words not yet stored in CX, the
 * current one included; it returns 0 when all are stored.  Watcom C 6.5
 * corrupts aux pragma code longer than 79 bytes, so both loops stay
 * below that.
 *
 * vs_xsnw, memory to screen (the word is loaded before the wait):
 *     PUSH SI DI DS ES BP     56 57 1E 06 55
 *     MOV CX,[BX+8]           8B 4F 08        words
 *     MOV DX,[BX+10]          8B 57 0A        status port
 *     LES DI,[BX+4]           C4 7F 04        destination
 *     LDS SI,[BX]             C5 37           source (DS last)
 *     CLD                     FC
 *     MOV BP,VID_VBURST       BD 20 00
 *  next:
 *     LODSW                   AD
 *     XCHG AX,BX              93              BX = the word
 *     IN AL,DX                EC
 *     TEST AL,8               A8 08           vertical retrace?
 *     JZ reset                74 09
 *     DEC BP                  4D              burst used up?
 *     JS hwait                78 09
 *     XCHG AX,BX              93
 *     STOSW                   AB              store without waiting
 *     LOOP next               E2 F2
 *     JMP done                EB 23
 *  reset:
 *     MOV BP,VID_VBURST       BD 20 00
 *  hwait:
 *     PUSH CX                 51
 *     MOV CX,VS_SPIN          B9 00 40
 *  w0:
 *     IN AL,DX                EC              wait while bit 0 = 1
 *     TEST AL,1               A8 01
 *     LOOPNZ w0               E0 FB
 *     JNZ tout                75 14
 *     MOV CX,VS_SPIN          B9 00 40
 *     CLI                     FA
 *  w1:
 *     IN AL,DX                EC              wait for bit 0 = 1
 *     TEST AL,1               A8 01
 *     LOOPZ w1                E1 FB
 *     JZ tout1                74 08
 *     XCHG AX,BX              93
 *     STOSW                   AB
 *     STI                     FB
 *     POP CX                  59
 *     LOOP next               E2 D1
 *     JMP done                EB 02
 *  tout1:
 *     STI                     FB
 *  tout:
 *     POP CX                  59              words left
 *  done:
 *     POP BP ES DS DI SI      5D 07 1F 5F 5E
 *
 * vs_xsnr, screen to memory, is the same loop with the read moved after
 * the wait, so that it is the screen access that falls in the retrace:
 *  next:   IN AL,DX; TEST AL,8; JZ reset (74 09); DEC BP;
 *          JS hwait (78 09); LODSW; STOSW; LOOP next (E2 F4);
 *          JMP done (EB 23)
 *  reset:  MOV BP,VID_VBURST
 *  hwait:  PUSH CX; MOV CX,VS_SPIN
 *  w0:     IN AL,DX; TEST AL,1; LOOPNZ w0; JNZ tout (75 14);
 *          MOV CX,VS_SPIN; CLI
 *  w1:     IN AL,DX; TEST AL,1; LOOPZ w1; JZ tout1 (74 08); LODSW; STI;
 *          STOSW; POP CX; LOOP next (E2 D3); JMP done (EB 02)
 *  tout1:  STI
 *  tout:   POP CX
 *  done:   POP BP ES DS DI SI
 *
 * VS_SPIN polls take about 120 ms on a 4.77 MHz 8088 and more than
 * 15 ms on any later machine with an ISA bus, well over the 4 ms of
 * the longest wait a working CGA needs (the vertical blank).
 * --------------------------------------------------------------------- */
#define VS_SPIN         0x4000u
/* The machine code listed above, and emitted below, holds VID_VBURST
   and VS_SPIN as immediates: the build fails if either constant no
   longer matches them. */
extern char vs_vburst_is_32[1];
extern char vs_vburst_is_32[(VID_VBURST == 32) ? 1 : 2];
extern char vs_spin_is_4000h[1];
extern char vs_spin_is_4000h[(VS_SPIN == 0x4000u) ? 1 : 2];

#ifdef COMPAT_TC
static u16 vs_xsnw(u16 *p)
{
    _BX = (unsigned)p;
    __emit__(0x56, 0x57, 0x1E, 0x06, 0x55, 0x8B, 0x4F, 0x08,
             0x8B, 0x57, 0x0A, 0xC4, 0x7F, 0x04, 0xC5, 0x37,
             0xFC, 0xBD, 0x20, 0x00, 0xAD, 0x93, 0xEC, 0xA8,
             0x08, 0x74, 0x09, 0x4D, 0x78, 0x09, 0x93, 0xAB,
             0xE2, 0xF2, 0xEB, 0x23, 0xBD, 0x20, 0x00, 0x51,
             0xB9, 0x00, 0x40, 0xEC, 0xA8, 0x01, 0xE0, 0xFB,
             0x75, 0x14, 0xB9, 0x00, 0x40, 0xFA, 0xEC, 0xA8,
             0x01, 0xE1, 0xFB, 0x74, 0x08, 0x93, 0xAB, 0xFB,
             0x59, 0xE2, 0xD1, 0xEB, 0x02, 0xFB, 0x59, 0x5D,
             0x07, 0x1F, 0x5F, 0x5E);
    return (u16)_CX;
}

static u16 vs_xsnr(u16 *p)
{
    _BX = (unsigned)p;
    __emit__(0x56, 0x57, 0x1E, 0x06, 0x55, 0x8B, 0x4F, 0x08,
             0x8B, 0x57, 0x0A, 0xC4, 0x7F, 0x04, 0xC5, 0x37,
             0xFC, 0xBD, 0x20, 0x00, 0xEC, 0xA8, 0x08, 0x74,
             0x09, 0x4D, 0x78, 0x09, 0xAD, 0xAB, 0xE2, 0xF4,
             0xEB, 0x23, 0xBD, 0x20, 0x00, 0x51, 0xB9, 0x00,
             0x40, 0xEC, 0xA8, 0x01, 0xE0, 0xFB, 0x75, 0x14,
             0xB9, 0x00, 0x40, 0xFA, 0xEC, 0xA8, 0x01, 0xE1,
             0xFB, 0x74, 0x08, 0xAD, 0xFB, 0xAB, 0x59, 0xE2,
             0xD3, 0xEB, 0x02, 0xFB, 0x59, 0x5D, 0x07, 0x1F,
             0x5F, 0x5E);
    return (u16)_CX;
}
#else
/* Not static: Watcom C 6.5 rejects a static function whose body is an
   aux pragma. */
u16 vs_xsnw(u16 *p);
#pragma aux vs_xsnw = \
    0x56 0x57 0x1E 0x06 0x55 0x8B 0x4F 0x08 \
    0x8B 0x57 0x0A 0xC4 0x7F 0x04 0xC5 0x37 \
    0xFC 0xBD 0x20 0x00 0xAD 0x93 0xEC 0xA8 \
    0x08 0x74 0x09 0x4D 0x78 0x09 0x93 0xAB \
    0xE2 0xF2 0xEB 0x23 0xBD 0x20 0x00 0x51 \
    0xB9 0x00 0x40 0xEC 0xA8 0x01 0xE0 0xFB \
    0x75 0x14 0xB9 0x00 0x40 0xFA 0xEC 0xA8 \
    0x01 0xE1 0xFB 0x74 0x08 0x93 0xAB 0xFB \
    0x59 0xE2 0xD1 0xEB 0x02 0xFB 0x59 0x5D \
    0x07 0x1F 0x5F 0x5E \
    parm [bx] value [cx] modify [ax bx cx dx];

u16 vs_xsnr(u16 *p);
#pragma aux vs_xsnr = \
    0x56 0x57 0x1E 0x06 0x55 0x8B 0x4F 0x08 \
    0x8B 0x57 0x0A 0xC4 0x7F 0x04 0xC5 0x37 \
    0xFC 0xBD 0x20 0x00 0xEC 0xA8 0x08 0x74 \
    0x09 0x4D 0x78 0x09 0xAD 0xAB 0xE2 0xF4 \
    0xEB 0x23 0xBD 0x20 0x00 0x51 0xB9 0x00 \
    0x40 0xEC 0xA8 0x01 0xE0 0xFB 0x75 0x14 \
    0xB9 0x00 0x40 0xFA 0xEC 0xA8 0x01 0xE1 \
    0xFB 0x74 0x08 0xAD 0xFB 0xAB 0x59 0xE2 \
    0xD3 0xEB 0x02 0xFB 0x59 0x5D 0x07 0x1F \
    0x5F 0x5E \
    parm [bx] value [cx] modify [ax bx cx dx];
#endif

/* ---------------------------------------------------------------------
 * The adapter.
 * --------------------------------------------------------------------- */
/* Offsets in the BIOS data area, segment BDA_SEG. */
#define VS_MODE         0x49    /* byte: video mode                     */
#define VS_COLS         0x4A    /* word: columns                        */
#define VS_CURPOS       0x50    /* word: page 0 cursor, col lo, row hi  */
#define VS_CURSHAPE     0x60    /* word: cursor start (high), end (low) */
#define VS_PAGE         0x62    /* byte: active display page            */
#define VS_CRTC         0x63    /* word: CRT controller base port       */
#define VS_EGAROWS      0x84    /* byte: rows - 1, EGA and later only   */
#define VS_MONOPORT     0x3B4u  /* VS_CRTC of a monochrome adapter      */
#define VS_HIDE         0x2000u /* cursor start line with bit 5: hidden */
#define VS_PAGE1        0x1000u /* video page 1: 80 x 25 colour pages
                                   are 4,096 bytes apart                */

static u16 vs_seg;              /* B000h or B800h                       */
static u16 vs_status;           /* CRT controller base + 6              */
static u8 vs_setmode;           /* 1 when vid_open changed the mode     */
static u8 vs_oldmode;
static u8 vs_oldpage;
static u16 vs_oldpos;
static u16 vs_oldshape;
static u16 vs_xp[6];            /* snow loop arguments                  */
static u8 vs_snowdead;          /* a snow wait ran out: no more waits   */

/* The byte at off in the BIOS data area. */
static u8 vs_bdab(u16 off)
{
    return *CFP(u8, BDA_SEG, off);
}

/* The word at off in the BIOS data area. */
static u16 vs_bdaw(u16 off)
{
    FARP(u8) p;

    p = CFP(u8, BDA_SEG, off);
    return get_le16(p);
}

static void vs_int10(union REGS *r)
{
    int86(0x10, r, r);
}

/* 1 when snow avoidance applies to the next transfer: asked for, not
   given up, direct output, a CGA. */
static int vs_snowy(void)
{
    return vs_snowon && !vs_snowdead && !vs_bios &&
           vs_adapter == VID_A_CGA;
}

int vid_snow_active(void)
{
    return vs_isopen && vs_snowy();
}

/* Copy words between far memory and video memory; toscreen tells the
   direction, which picks the snow loop.  When a snow wait runs out, the
   words left are copied without waiting, and so is everything after. */
static void vs_xfer(FARP(u8) src, FARP(u8) dst, u16 words, int toscreen)
{
    FARP(u16) s;
    FARP(u16) d;
    u16 left;

    if (words == 0)
        return;
    s = (FARP(u16))src;
    d = (FARP(u16))dst;
    if (vs_snowy()) {
        vs_xp[0] = (u16)FP_OFF(src);
        vs_xp[1] = (u16)FP_SEG(src);
        vs_xp[2] = (u16)FP_OFF(dst);
        vs_xp[3] = (u16)FP_SEG(dst);
        vs_xp[4] = words;
        vs_xp[5] = vs_status;
        left = toscreen ? vs_xsnw(vs_xp) : vs_xsnr(vs_xp);
        if (left == 0)
            return;
        vs_snowdead = 1;
        s += words - left;
        d += words - left;
        words = left;
    }
    movedata(FP_SEG(s), FP_OFF(s), FP_SEG(d), FP_OFF(d), (u16)(words * 2u));
}

/* Cell row, col in video memory. */
static FARP(u8) vs_vram(u8 row, u8 col)
{
    return CFP(u8, vs_seg, vs_off(row, col));
}

/* INT 10h function 02h on page 0. */
static void vs_setpos(u8 row, u8 col)
{
    union REGS r;

    r.x.ax = 0x0200;
    r.x.bx = 0;
    r.h.dh = row;
    r.h.dl = col;
    vs_int10(&r);
}

/* The cursor vid_cursor last set, or parked below the last row. */
static void vs_hwpos(void)
{
    if (vs_cshape == VID_CUR_HIDE)
        vs_setpos(VID_ROWS, 0);
    else
        vs_setpos(vs_crow, vs_ccol);
}

/* Write n cells from p at row, col through INT 10h: one function 09h
   call per run of equal cells. */
static void vs_biosput(FARP(u8) p, u8 row, u8 col, u16 n)
{
    union REGS r;
    u16 run;

    while (n != 0) {
        run = 1;
        while (run < n && p[run * 2] == p[0] && p[run * 2 + 1] == p[1])
            run++;
        vs_setpos(row, col);
        r.h.ah = 0x09;
        r.h.al = p[0];
        r.h.bh = 0;
        r.h.bl = p[1];
        r.x.cx = run;
        vs_int10(&r);
        p += run * 2;
        col = (u8)(col + run);
        n = (u16)(n - run);
    }
}

/* Read one row of the screen into cells (VID_ROWBYTES bytes). */
static void vs_readrow(u8 row, FARP(u8) cells)
{
    union REGS r;
    u8 c;

    if (!vs_bios) {
        vs_xfer(vs_vram(row, 0), cells, VID_COLS, 0);
        return;
    }
    for (c = 0; c < VID_COLS; c++) {
        vs_setpos(row, c);
        r.x.ax = 0x0800;
        r.x.bx = 0;
        vs_int10(&r);
        *cells++ = r.h.al;
        *cells++ = r.h.ah;
    }
}

/* Copy a whole screen of cells from p to the screen (toscreen), or from
   the screen to p. */
static void vs_screen(FARP(u8) p, int toscreen)
{
    u8 row;

    if (!vs_bios) {
        if (toscreen)
            vs_xfer(p, vs_vram(0, 0), VID_CELLS, 1);
        else
            vs_xfer(vs_vram(0, 0), p, VID_CELLS, 0);
        return;
    }
    for (row = 0; row < VID_ROWS; row++) {
        if (toscreen)
            vs_biosput(p, row, 0, VID_COLS);
        else
            vs_readrow(row, p);
        p += VID_ROWBYTES;
    }
}

/* Save the DOS screen (save) or put it back: in the far block vs_dos
   when there is one, else in video page 1.  The page copy goes a row
   at a time through a buffer, since a snow loop has video memory on
   one side only. */
static void vs_dosscr(int save)
{
    u8 cells[VID_ROWBYTES];
    FARP(u8) src;
    FARP(u8) dst;
    u8 row;

    if (vs_dos != NULL) {
        vs_screen(cmem_ptr(vs_dos), !save);
        return;
    }
    src = CFP(u8, vs_seg, save ? 0 : VS_PAGE1);
    dst = CFP(u8, vs_seg, save ? VS_PAGE1 : 0);
    for (row = 0; row < VID_ROWS; row++) {
        vs_xfer(src, (FARP(u8))cells, VID_COLS, 0);
        vs_xfer((FARP(u8))cells, dst, VID_COLS, 1);
        src += VID_ROWBYTES;
        dst += VID_ROWBYTES;
    }
}

void vid_cursor(u8 row, u8 col, u8 shape)
{
    union REGS r;

    vs_crow = row;
    vs_ccol = col;
    vs_cshape = shape;
    if (!vs_isopen)
        return;
    r.x.ax = 0x0100;
    if (shape == VID_CUR_HIDE)
        r.x.cx = VS_HIDE;
    else if (shape == VID_CUR_BLOCK)
        r.x.cx = (vs_mode == 7) ? 0x000C : 0x0007;
    else
        r.x.cx = (vs_mode == 7) ? 0x0B0C : 0x0607;
    vs_int10(&r);
    vs_hwpos();
}

int vid_open(u8 flags)
{
    union REGS r;
    u16 crtc;
    u8 mode;
    u8 want;
    u8 rows;
    u8 setmode;
    int ega;

    if (vs_isopen)
        return 0;
    mode = (u8)(vs_bdab(VS_MODE) & 0x7Fu);
    crtc = vs_bdaw(VS_CRTC);
    r.x.ax = 0x1200;
    r.x.bx = 0x0010;
    vs_int10(&r);
    ega = (r.h.bl != 0x10);
    rows = (u8)(ega ? vs_bdab(VS_EGAROWS) + 1 : VID_ROWS);
    if (crtc == VS_MONOPORT || mode == 7)
        want = 7;
    else
        want = (u8)(mode == 2 ? 2 : 3);
    setmode = (u8)((mode != want || rows != VID_ROWS ||
                    vs_bdaw(VS_COLS) != VID_COLS) ? 1 : 0);
    /* The DOS screen is saved only when the mode is kept, since a mode
       change clears it anyway.  It goes to video page 1 when the colour
       modes' memory is written directly and page 1 is not the page to
       show again at exit; else to a far block (an MDA has one page). */
    vs_oldpage = vs_bdab(VS_PAGE);
    if (vs_alloc(!setmode && ((flags & VID_F_BIOS) || want == 7 ||
                              vs_oldpage == 1)) != 0)
        return CF_ENOMEM;
    vs_bios = (u8)((flags & VID_F_BIOS) ? 1 : 0);
    vs_snowon = (u8)((flags & VID_F_SNOW) ? 1 : 0);
    vs_setmode = setmode;
    vs_oldmode = mode;
    if (vs_setmode) {
        r.x.ax = want;
        vs_int10(&r);
    } else if (vs_oldpage != 0) {
        r.x.ax = 0x0500;
        vs_int10(&r);
    }
    vs_mode = want;
    vs_seg = (want == 7) ? 0xB000u : 0xB800u;
    vs_status = (u16)(crtc + 6);
    if (ega)
        vs_adapter = VID_A_EGA;
    else if (crtc == VS_MONOPORT)
        vs_adapter = VID_A_MDA;
    else
        vs_adapter = VID_A_CGA;

    if (!vs_setmode) {
        vs_oldpos = vs_bdaw(VS_CURPOS);
        vs_oldshape = vs_bdaw(VS_CURSHAPE);
        vs_dosscr(1);
    }
    vs_isopen = 1;
    vid_cursor(0, 0, VID_CUR_HIDE);
    return 0;
}

void vid_close(void)
{
    union REGS r;

    if (!vs_isopen)
        return;
    if (vs_setmode) {
        r.x.ax = vs_oldmode;
        vs_int10(&r);
    } else {
        vs_dosscr(0);
        r.x.ax = 0x0100;
        r.x.cx = vs_oldshape;
        vs_int10(&r);
        r.x.ax = 0x0200;
        r.x.bx = 0;
        r.x.dx = vs_oldpos;
        vs_int10(&r);
    }
    if (vs_oldpage != 0) {
        r.h.ah = 0x05;
        r.h.al = vs_oldpage;
        vs_int10(&r);
    }
    vs_free();
}

void vid_flush(void)
{
    u8 row;
    u16 n;
    int wrote;

    vs_flushed = 0;
    if (!vs_isopen)
        return;
    wrote = 0;
    for (row = 0; row < VID_ROWS; row++) {
        if (vs_lo[row] == VS_CLEAN)
            continue;
        n = (u16)(vs_hi[row] - vs_lo[row] + 1);
        vs_flushed = (u16)(vs_flushed + n);
        if (vs_bios)
            vs_biosput(vs_cell(row, vs_lo[row]), row, vs_lo[row], n);
        else
            vs_xfer(vs_cell(row, vs_lo[row]), vs_vram(row, vs_lo[row]),
                    n, 1);
        vs_lo[row] = VS_CLEAN;
        wrote = 1;
    }
    if (wrote && vs_bios)
        vs_hwpos();
}

int vid_verify(u8 *row, u8 *col)
{
    u8 cells[VID_ROWBYTES];
    FARP(u8) p;
    u8 r;
    u16 i;
    int bad;

    *row = 0;
    *col = 0;
    if (!vs_isopen)
        return 0;
    bad = 0;
    for (r = 0; r < VID_ROWS && !bad; r++) {
        vs_readrow(r, (FARP(u8))cells);
        p = vs_cell(r, 0);
        for (i = 0; i < VID_ROWBYTES; i++) {
            if (cells[i] != p[i]) {
                *row = r;
                *col = (u8)(i / 2);
                bad = 1;
                break;
            }
        }
    }
    if (vs_bios)
        vs_hwpos();
    return bad;
}

#endif
