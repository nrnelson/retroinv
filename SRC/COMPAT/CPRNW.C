/*
 * CPRNW.C - printer output for Windows (COMPAT.H): a job goes to the
 * default printer through GDI.
 *
 * prn_open makes a device context of the printer that the device line of
 * WIN.INI's [windows] section names, "name,driver,port" (the port prn_open is
 * given is not used), gives it the abort procedure (SETABORTPROC) and starts a
 * document named k_doc, RETROINV (STARTDOC).  A device line that names no
 * printer is PRN_ENODEV; one whose printer GDI makes no device context of,
 * its driver missing or not loading, PRN_EOFFLINE.  The bytes of a job are
 * those PRINT.H sends a printer: text, CR LF line ends, FF page ends and the
 * ESC/P codes it lists.  They are laid out as a printer lays them out, in the
 * printer's fixed-pitch font: each run of text drawn (TextOut) at the column
 * the line has reached, in the styles of ESC E and ESC F (bold) and ESC W n
 * (double width, each character taking two columns), CR back to the line's
 * start, LF a line down, FF a new page (NEWFRAME) once something is drawn on
 * this one, and a line that would pass the page's bottom on a new page too;
 * ESC @ ends the styles, and ESC C n, the form length, changes nothing, the
 * page being the paper's.  A run is drawn in the Windows character set
 * (OemToAnsi).  The font is the one the driver gives for a fixed pitch, made
 * narrower when PW_COLS of its characters do not fit across the page; its
 * bold form is made when first used, all fonts freed at the end of the job.
 *
 * Double width is printed only in a font the printer really gives twice as
 * wide (wide): GDI answers a request for a width the printer's fonts lack
 * with a font of the plain width.  On a printer without one, the Generic /
 * Text Only driver among them, ESC W 1 changes nothing, so the text prints
 * one column a character in the other styles in effect and what follows it
 * on the line stays beside it.
 *
 * A run ends at a control byte or when it holds a line's PW_RUN bytes, never
 * where a write ends: the driver gets a line's text of one style whole,
 * however the job's buffer divided it.
 *
 * GDI calls the abort procedure while it prints a page: it lets the
 * other programs run (dos_idle) and ends the job once prn_cancel has
 * been called.  A job that GDI has ended, after an error or a cancel,
 * is not ended again; a cancel between pages ends it with ABORTDOC at
 * the next write or at prn_close.  From then on every write fails with
 * PRN_EIO, the bytes taken but dropped.
 */
#include "COMPAT.H"
#include "CWIN.H"
#include <stddef.h>
#include <string.h>

#define PW_DEVSZ        128     /* WIN.INI's device line and its NUL    */
#define PW_COLS         80      /* columns a line of a printer holds    */
#define PW_RUN          80      /* bytes of text drawn at a time        */
#define PW_BOLD         0x01    /* styles, the index of their font      */
#define PW_WIDE         0x02
#define PW_NFONT        4

/* What the next byte is, after an ESC. */
#define PE_TEXT         0       /* text or a control byte               */
#define PE_CODE         1       /* the code after ESC                   */
#define PE_ARG          2       /* the argument of ESC C or ESC W       */

static const char k_doc[] = "RETROINV";

static HDC pw_dc;               /* the job's printer, NULL with no job  */
static FARPROC pw_abort;        /* the abort procedure's instance thunk */
static HFONT pw_font[PW_NFONT]; /* by style, NULL until made            */
static int pw_cw;               /* the font's width and height, pixels  */
static int pw_ch;
static int pw_lh;               /* a line, external leading included    */
static int pw_bottom;           /* the page's height                    */
static int pw_y;                /* the top of the line being printed    */
static u16 pw_col;              /* the column the line has reached      */
static u16 pw_runcol;           /* the column of run's first character  */
static u16 pw_n;                /* bytes in run                         */
static u8 pw_live;              /* the job still prints                 */
static u8 pw_cancel;            /* prn_cancel was called                */
static u8 pw_ink;               /* something is drawn on this page      */
static u8 pw_narrow;            /* the printer has no double-width font */
static u8 pw_style;             /* PW_ styles in effect                 */
static u8 pw_esc;               /* PE_                                  */
static u8 pw_code;              /* the code whose argument comes        */
static char pw_run[PW_RUN + 1];

/* The job's abort procedure (SETABORTPROC), which GDI calls while it
   prints a page: let the other programs run; FALSE ends the job once
   prn_cancel has been called. */
BOOL WCALLBACK prn_abortproc(HDC dc, int code)
{
    (void)dc;
    (void)code;
    dos_idle();
    return !pw_cancel;
}

/* The request for the job's plain font: of a fixed pitch and as wide
   and high as its characters, once base_font has measured them. */
static void ask(LOGFONT *lf)
{
    memset(lf, 0, sizeof *lf);
    lf->lfHeight = pw_ch;
    lf->lfWidth = pw_cw;
    lf->lfWeight = FW_NORMAL;
    lf->lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
}

/* The font of the styles st, the plain one or with PW_BOLD the heavy
   one made when first asked for; wide has made those of PW_WIDE. */
static HFONT font(u8 st)
{
    LOGFONT lf;

    if (pw_font[st] == NULL) {
        ask(&lf);
        if (st & PW_BOLD)
            lf.lfWeight = FW_BOLD;
        pw_font[st] = CreateFontIndirect(&lf);
    }
    return pw_font[st];
}

/* EnumFonts' procedure of wide: stop at the first font of a fixed pitch
   that is twice as wide as the job's characters, its description copied
   to the LOGFONT lp points to. */
int WCALLBACK prn_fontproc(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                           int type, LPARAM lp)
{
    (void)type;
    if (!(lf->lfPitchAndFamily & FIXED_PITCH) ||
        tm->tmAveCharWidth != 2 * pw_cw)
        return 1;
    *(LOGFONT FAR *)lp = *lf;
    return 0;
}

/* Make the double-width fonts, plain and bold, that lf describes; 1
   when the printer gives one twice as wide as the job's characters,
   else neither is kept. */
static int wide_make(LOGFONT *lf)
{
    TEXTMETRIC tm;

    lf->lfWeight = FW_NORMAL;
    pw_font[PW_WIDE] = CreateFontIndirect(lf);
    lf->lfWeight = FW_BOLD;
    pw_font[PW_WIDE | PW_BOLD] = CreateFontIndirect(lf);
    SelectObject(pw_dc, pw_font[PW_WIDE]);
    GetTextMetrics(pw_dc, &tm);
    if (tm.tmAveCharWidth == 2 * pw_cw)
        return 1;
    SelectObject(pw_dc, GetStockObject(SYSTEM_FONT));
    DeleteObject(pw_font[PW_WIDE]);
    DeleteObject(pw_font[PW_WIDE | PW_BOLD]);
    pw_font[PW_WIDE] = NULL;
    pw_font[PW_WIDE | PW_BOLD] = NULL;
    return 0;
}

/* 1 when the printer has a double-width font, found out when first
   asked, its fonts then made: the plain font asked for twice as wide,
   which a printer that scales its fonts gives, or else a font the
   printer lists at that width, as a matrix printer lists its expanded
   pitch, under a name of its own. */
static int wide(void)
{
    LOGFONT lf;
    FARPROC proc;

    if (pw_font[PW_WIDE] == NULL && !pw_narrow) {
        ask(&lf);
        lf.lfWidth = 2 * pw_cw;
        if (!wide_make(&lf)) {
            pw_narrow = 1;
            proc = MakeProcInstance((FARPROC)prn_fontproc,
                                    (HINSTANCE)dos_inst);
            if (proc != NULL) {
                if (EnumFonts(pw_dc, NULL, (OLDFONTENUMPROC)proc,
                              (LPARAM)(LOGFONT FAR *)&lf) == 0)
                    pw_narrow = (u8)!wide_make(&lf);
                FreeProcInstance(proc);
            }
        }
    }
    return !pw_narrow;
}

/* Make the plain font, the driver's of a fixed pitch, or when PW_COLS of
   its characters do not fit across the page one of the width that fits
   them, and take its metrics. */
static void base_font(void)
{
    TEXTMETRIC tm;
    int across;

    across = GetDeviceCaps(pw_dc, HORZRES);
    pw_ch = 0;
    pw_cw = 0;
    SelectObject(pw_dc, font(0));
    GetTextMetrics(pw_dc, &tm);
    if (tm.tmAveCharWidth * PW_COLS > across) {
        SelectObject(pw_dc, GetStockObject(SYSTEM_FONT));
        DeleteObject(pw_font[0]);
        pw_font[0] = NULL;
        pw_ch = tm.tmHeight;
        pw_cw = across / PW_COLS;
        SelectObject(pw_dc, font(0));
        GetTextMetrics(pw_dc, &tm);
    }
    pw_ch = tm.tmHeight;
    pw_cw = tm.tmAveCharWidth;
    pw_lh = tm.tmHeight + tm.tmExternalLeading;
}

/* End the page (NEWFRAME); the job ends when GDI has ended it. */
static void page_end(void)
{
    if (Escape(pw_dc, NEWFRAME, 0, NULL, NULL) < 0)
        pw_live = 0;
    pw_y = 0;
    pw_ink = 0;
}

/* Draw the run, on a new page when its line would pass the bottom. */
static void draw(void)
{
    if (pw_n == 0)
        return;
    if (pw_live && pw_y > 0 && pw_y + pw_lh > pw_bottom)
        page_end();
    if (pw_live) {
        pw_run[pw_n] = '\0';
        OemToAnsi(pw_run, pw_run);
        SelectObject(pw_dc, font(pw_style));
        TextOut(pw_dc, pw_runcol * pw_cw, pw_y, pw_run, pw_n);
        pw_ink = 1;
    }
    pw_n = 0;
}

/* The byte b of the job. */
static void put(u8 b)
{
    if (pw_esc == PE_ARG) {
        if (pw_code == 'W')
            pw_style = (u8)((b & 1) && wide() ? pw_style | PW_WIDE :
                                                pw_style & ~PW_WIDE);
        pw_esc = PE_TEXT;
        return;
    }
    if (pw_esc == PE_CODE) {
        pw_code = b;
        pw_esc = PE_TEXT;
        if (b == 'C' || b == 'W')
            pw_esc = PE_ARG;
        else if (b == 'E')
            pw_style = (u8)(pw_style | PW_BOLD);
        else if (b == 'F')
            pw_style = (u8)(pw_style & ~PW_BOLD);
        else if (b == '@')
            pw_style = 0;
        return;
    }
    if (b >= 0x20) {
        if (pw_n == PW_RUN)
            draw();
        if (pw_n == 0)
            pw_runcol = pw_col;
        pw_run[pw_n++] = (char)b;
        pw_col = (u16)(pw_col + ((pw_style & PW_WIDE) ? 2 : 1));
        return;
    }
    draw();
    switch (b) {
    case 0x1B:
        pw_esc = PE_CODE;
        break;
    case '\r':
        pw_col = 0;
        break;
    case '\n':
        pw_y += pw_lh;
        break;
    case '\f':
        if (pw_live && pw_ink)
            page_end();
        pw_y = 0;
        break;
    default:
        break;
    }
}

/* End the job that still prints when it has been cancelled. */
static void take_cancel(void)
{
    if (pw_live && pw_cancel) {
        Escape(pw_dc, ABORTDOC, 0, NULL, NULL);
        pw_live = 0;
    }
}

int prn_open(u8 port)
{
    char dev[PW_DEVSZ];
    char *part[3];
    char *p;
    int i;

    (void)port;
    prn_close();
    GetProfileString("windows", "device", "", dev, (int)sizeof dev);
    p = dev;
    for (i = 0; i < 3; i++) {
        while (*p == ' ')
            p++;
        part[i] = p;
        while (*p != '\0' && *p != ',')
            p++;
        if (i < 2 && *p == '\0')
            return PRN_ENODEV;
        if (*p != '\0')
            *p++ = '\0';
    }
    pw_dc = CreateDC(part[1], part[0], part[2], NULL);
    if (pw_dc == NULL)
        return PRN_EOFFLINE;
    pw_abort = MakeProcInstance((FARPROC)prn_abortproc,
                                (HINSTANCE)dos_inst);
    pw_live = 1;
    pw_cancel = 0;
    pw_ink = 0;
    pw_narrow = 0;
    pw_style = 0;
    pw_esc = PE_TEXT;
    pw_y = 0;
    pw_col = 0;
    pw_n = 0;
    base_font();
    pw_bottom = GetDeviceCaps(pw_dc, VERTRES);
    if (pw_abort == NULL ||
        Escape(pw_dc, SETABORTPROC, 0, (LPCSTR)pw_abort, NULL) < 0 ||
        Escape(pw_dc, STARTDOC, (int)strlen(k_doc), k_doc, NULL) < 0) {
        pw_live = 0;
        prn_close();
        return PRN_EIO;
    }
    return PRN_OK;
}

int prn_write(const u8 *buf, u16 n, u16 *done)
{
    u16 i;

    *done = 0;
    if (pw_dc == NULL)
        return PRN_ENODEV;
    take_cancel();
    *done = n;
    for (i = 0; i < n && pw_live; i++)
        put(buf[i]);
    return pw_live ? PRN_OK : PRN_EIO;
}

void prn_close(void)
{
    int i;

    if (pw_dc == NULL)
        return;
    draw();
    take_cancel();
    if (pw_live && pw_ink)
        page_end();
    if (pw_live)
        Escape(pw_dc, ENDDOC, 0, NULL, NULL);
    SelectObject(pw_dc, GetStockObject(SYSTEM_FONT));
    for (i = 0; i < PW_NFONT; i++) {
        if (pw_font[i] != NULL)
            DeleteObject(pw_font[i]);
        pw_font[i] = NULL;
    }
    DeleteDC(pw_dc);
    pw_dc = NULL;
    if (pw_abort != NULL)
        FreeProcInstance(pw_abort);
    pw_abort = NULL;
    pw_live = 0;
}

void prn_cancel(void)
{
    pw_cancel = 1;
}
