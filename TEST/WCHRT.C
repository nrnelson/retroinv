/*
 * WCHRT.C - the test of the frame chrome, the tool bar (SRC/WIN/WTOOL.C),
 * the status bar (SRC/WIN/WSTAT.C), the splitter (SRC/WIN/WSPLIT.C) and
 * the preview panel (SRC/WIN/WPREV.C): a frame holding them, which a key
 * script drives by posted clicks and calls and snaps.
 *
 * usage: WIN WCHRT /KEYS:WCHRT.KEY /SNAP:WCHRT.SNP
 *
 * The frame opens WCH_WIDE by WCH_TALL pixels.  Its children, in the
 * order made: the tool bar, of string WCH_S_TOOL and the commands
 * WCH_CMD + k, whose slot is reserved WCH_SLOTW wide and given an edit
 * control as the bar's child; an edit control, which has the focus; a
 * static "SEEN n CMD c CODE k", n the WM_COMMAND messages the frame has
 * had from the bar, the splitter and the panel, c the last one's wParam,
 * but 0 for the splitter's when its LOWORD(lParam) is not the splitter,
 * and k its HIWORD(lParam), the edit control's own left out; a static
 * "ANS ...",
 * what the last call a key made answered, at first the slot's
 * reservation ("ANS 1 l t r b"); the status bar, of one panel; the
 * splitter, its left edge at WCH_SPX; and the preview panel, empty.  The
 * frame lays them out to its width: the bars along its top and bottom,
 * the others between, one under the other, the splitter WSP_WIDTH wide
 * in a row of its own under the answer, keeping its left edge, and the
 * panel from under that row down to the status bar.  A key the panel
 * leaves to its owner (WPVN_KEY) counts as a WM_COMMAND in "SEEN", and
 * the answer says it, "ANS vk".  The message loop takes these keys
 * before they are dispatched:
 *
 *   F2   a click on Open (button 1); with SHIFT on Edit (button 2); with
 *        CTRL on New (button 0) let go off it, the mouse having left it
 *   F3   a click on Lend (button 6), the mouse leaving and coming back
 *        before it is let go; with SHIFT, WTBM_SLOT WCH_SLOTW2, the
 *        slot's edit moved to the answer ("ANS r l t r b", or "ANS 0");
 *        with CTRL, WTBM_BUTTON 7 and 8 ("ANS a b", each 1 for a window,
 *        0 for none)
 *   F4   Edit and Delete grayed; with SHIFT made usable again; with
 *        CTRL Open pressed, grayed, which sends it WM_CANCELMODE as it
 *        holds the mouse, let go and made usable again ("ANS p c q f":
 *        p and q its state's pressed bit, 4 or 0, before and after the
 *        gray, c 1 while it held the mouse, f 1 once nothing held it);
 *        with CTRL and SHIFT Open pressed, the bar sent WM_CANCELMODE in
 *        place of the gray, and let go, the same answer
 *   F5   the bar sent its slot edit's WM_COMMAND, EN_CHANGE; with SHIFT
 *        a click on the bar itself, at (1, 1); with CTRL a click on Find
 *        (button 7, after the slot)
 *   F6   WSBM_PANELS 4 of k_four ("ANS n"); with SHIFT 2 of k_two; with
 *        CTRL 5; with CTRL and SHIFT 1 of none (NULL); with ALT 0
 *   F7   WSBM_SETTEXT of k_texts on panels 0 to 3 ("ANS r", the last
 *        one's answer); with SHIFT panel 3 k_tab; with CTRL panel 4
 *        "X"; with CTRL and SHIFT panel 1 NULL
 *   F8   WSBM_SETTEXT panel 3 k_long; with SHIFT panel 0 k_long; with
 *        CTRL the colour of WCH_PIXX, WCH_PIXY of Open, usable then
 *        grayed ("ANS rgb rgb", each GetPixel's six hex digits)
 *   F9   the frame made WCH_NARROW wide; with SHIFT WCH_WIDE again; with
 *        CTRL Open pressed, the frame disabled, Open let go, the frame
 *        made usable again and the edit control given the focus ("ANS p
 *        c", as CTRL F4's first two); with CTRL and SHIFT a double click
 *        on Open, down, up, a double click's down and up
 *   F1   the splitter dragged WCH_STEP right; with SHIFT its limits set
 *        to WCH_SPLO..WCH_SPHI (WSPM_LIMITS) and it dragged WCH_FAR
 *        right; with CTRL dragged WCH_FAR left; with CTRL and SHIFT
 *        dragged WCH_STEP right and back and let go where it was taken;
 *        with ALT a double click's press, dragged to WCH_SPX
 *   CTRL G  the splitter pressed and its guide moved to WCH_GX1, then
 *        WCH_GX2, the drag held ("ANS a c f p1 p2", below)
 *   CTRL U  the splitter let go at the guide's WCH_GX2 (a 0)
 *   CTRL K  the splitter pressed, moved to WCH_GX2 and sent
 *        WM_CANCELMODE, then let go
 *   CTRL B  the splitter disabled, pressed, moved to WCH_GX2, let go and
 *        made usable again
 *   CTRL T  the panel given the text of WCH_S_LOAD for k_item (wpv_text)
 *   CTRL P  the panel emptied and filled with k_fields, a label and a
 *        value each, and the notes k_note1, k_note2, k_note3 and
 *        "Line 1" to "Line WCH_NLINES", then shown (wpv_done): "ANS n",
 *        the lines the panel took
 *   CTRL L  the panel emptied, filled with fields of WCH_RECSZ bytes
 *        each until it takes no more, and emptied again: "ANS n a b", n
 *        the fields it took, a WPVM_LINE's answer for the last of them
 *        and b for the one after; with SHIFT a field of one byte more
 *        (k_odd) goes in first, uncounted, which leaves the block a
 *        byte short of the last field it took without
 *   CTRL M  the panel sent the presses of SHIFT and of CTRL alone
 *   CTRL R  "ANS n", the black pixels of the panel's two at (1, 1) and
 *        (2, 1), where its focus rectangle's dotted line runs
 *   CTRL H  the panel scrolled to row WCH_THUMB, as its thumb let go
 *        there scrolls it
 *
 * The splitter's mouse comes at WCH_ROWH / 2 down its client area, a
 * move to the guide's G at G less its left edge as the key starts plus
 * WCH_GRAB, F1's posted, the CTRL letters' sent; its state is said as
 * "ANS a c f p1 p2": a 1 when it held the mouse right after the key's
 * press, c when it holds the mouse now, f when it has the focus, and p1
 * and p2 the colours of the frame at WCH_GX1 and WCH_GX2 (plus half its
 * width) in its row.  ESC is not taken: during a drag it reaches the
 * splitter, which has the focus.
 *
 * A click is the mouse's down and up posted to the button (WTBM_BUTTON)
 * at its middle, its moves off it posted below it; the presses of CTRL
 * F4 and F9 send their down and up, so that each is handled before the
 * gray, cancel or disable that follows it.  TEST/WCHRT.KEY types
 * into it; every mode of Windows must give TEST/GOLDEN/WIN/WCHRT.SNP
 * byte for byte.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "TDRV.H"
#include "WTOOL.H"
#include "WSTAT.H"
#include "WSPLIT.H"
#include "WPREV.H"
#include "WCHRT.H"

#define WCH_CMDMAX      128     /* a DOS command tail and its NUL       */
#define WCH_TEXTSZ      40      /* a static's text and its NUL          */
#define WCH_WIDE        480     /* the frame's width at first           */
#define WCH_NARROW      160     /* its width made narrow                */
#define WCH_TALL        400     /* its height                           */
#define WCH_M           4       /* pixels between the children          */
#define WCH_ROWH        24      /* the height of each between the bars  */
#define WCH_SLOTW       80      /* the slot's width at first            */
#define WCH_SLOTW2      40      /* its width made narrow                */
#define WCH_OFF         30      /* a point's y below a button           */
#define WCH_BUTTONS     8       /* the bar's buttons                    */
#define WCH_NTEXTS      4       /* the texts F7 sets                    */
#define WCH_NFIELDS     6       /* the fields CTRL P adds (k_fields)    */
/* The point of Open that CTRL F8 reads: a black pixel of its glyph
   (SRC/WIN/INVTOOL.PY), dark grey grayed, 2 right and down of the
   glyph's corner. */
#define WCH_PIXX        ((WTB_BTNW - WTB_GLYPH) / 2 + 2)
#define WCH_PIXY        ((WTB_BTNH - WTB_GLYPH) / 2 + 2)
/* How CTRL F4 and F9 let a press of Open lose the mouse (press). */
#define WCH_GRAY        0       /* Open grayed                          */
#define WCH_CANCEL      1       /* the bar sent WM_CANCELMODE           */
#define WCH_OWNER       2       /* the frame disabled                   */

static const char k_class[] = "WCHRT";
static const char k_edit[] = "Edit";
static const char k_static[] = "Static";
static const char k_keys[] = "/KEYS:";
static const char k_snap[] = "/SNAP:";
static const char k_seen[] = "SEEN %d CMD %u CODE %u";
static const char k_ans1[] = "ANS %d";
static const char k_ans2[] = "ANS %d %d";
static const char k_ans4[] = "ANS %d %d %d %d";
static const char k_pix[] = "ANS %06lX %06lX";
static const char k_ans5[] = "ANS %d %d %d %d %d";
static const char k_split[] = "ANS %d %d %d %06lX %06lX";
static const char k_ans3[] = "ANS %d %d %d";
static const char k_item[] = "12";
static const char k_line[] = "Line %d";
/* CTRL P's fields: one without a value, one without a label, one whose
   label and value are longer than a line keeps, one of CP437 letters. */
static const char *const k_fields[WCH_NFIELDS][2] = {
    { "Item ID", "1" },
    { "Maker", "International Business Machines Corporation of Armonk" },
    { "Part of", "" },
    { "", "Available 1" },
    { "A label of forty characters, cut at 31..",
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789012345678" },
    { "Caf\x82", "na\x8Bve & co" }
};
static const char k_note1[] = "Bought at a swap meet in 1991.";
static const char k_note2[] = "";
/* A note that fits the panel WCH_WIDE gives whole, but not beside the
   scroll bar that panel always leaves room for: two rows there. */
static const char k_note3[] =
    "0123456789012345678901234567890123456789"
    "012345678901234567890123456789012 9";
/* CTRL L's label and value, WCH_RECSZ bytes with their NULs. */
static const char k_label[] = "123456789012345678901234567890";
static const char k_value[] = "12345678901234567890123456789012";
static const char k_odd[] = "123456789012345678901234567890123";
extern char wch_rec_fits[1];
extern char wch_rec_fits[(sizeof k_label + sizeof k_value == WCH_RECSZ &&
                          sizeof k_label + sizeof k_odd == WCH_RECSZ + 1 &&
                          WPV_BYTES % WCH_RECSZ == 0) ? 1 : 2];
static const char k_x[] = "X";
static const char k_tab[] = "Tab\there";
static const char k_long[] =
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "012345678901234567890123456789";
static const char *const k_texts[WCH_NTEXTS] = {
    "Local", "ADMIN (Admin)", "125 items", "Ready"
};
static const int k_four[] = { 60, 90, 70 };
static const int k_two[] = { 60 };
static const UINT k_cmds[WCH_BUTTONS] = {
    WCH_CMD, WCH_CMD + 1, WCH_CMD + 2, WCH_CMD + 3, WCH_CMD + 4,
    WCH_CMD + 5, WCH_CMD + 6, WCH_CMD + 7
};

static HWND g_frame;
static int g_seen;              /* the WM_COMMAND messages so far       */

/* Child id of the frame, or of the bar for WCH_FIND. */
static HWND child(int id)
{
    HWND bar;

    bar = GetDlgItem(g_frame, WCH_TOOL);
    return id == WCH_FIND ? GetDlgItem(bar, id) : GetDlgItem(g_frame, id);
}

/* Say t in the static WCH_ANS. */
static void ans(const char *t)
{
    SetWindowText(child(WCH_ANS), t);
}

/* Reserve the slot w pixels wide, move the slot's edit there and say
   the answer. */
static void slot(int w)
{
    char t[WCH_TEXTSZ];
    RECT r;
    int a;

    SetRectEmpty(&r);
    a = (int)SendMessage(child(WCH_TOOL), WTBM_SLOT, (WPARAM)w,
                         (LPARAM)(RECT FAR *)&r);
    if (a) {
        MoveWindow(child(WCH_FIND), r.left, r.top, r.right - r.left,
                   r.bottom - r.top, TRUE);
        wsprintf(t, k_ans5, a, r.left, r.top, r.right, r.bottom);
    } else {
        wsprintf(t, k_ans1, a);
    }
    ans(t);
}

/* Button k of the bar. */
static HWND button(int k)
{
    return (HWND)(UINT)SendMessage(child(WCH_TOOL), WTBM_BUTTON, (WPARAM)k,
                                   0L);
}

/* The pressed bit of button b's state. */
static int pressed(HWND b)
{
    return (int)SendMessage(b, BM_GETSTATE, 0, 0L) & 4;
}

/* Send Open the mouse's down, let the press lose the mouse as how says
   (WCH_GRAY, WCH_CANCEL or WCH_OWNER), send it the mouse's up and say
   the state before and, but for WCH_OWNER, after the loss. */
static void press(int how)
{
    char t[WCH_TEXTSZ];
    LPARAM in;
    HWND b;
    int p;
    int c;

    b = button(1);
    in = MAKELPARAM(WTB_BTNW / 2, WTB_BTNH / 2);
    SendMessage(b, WM_LBUTTONDOWN, MK_LBUTTON, in);
    p = pressed(b);
    c = GetCapture() == b;
    if (how == WCH_OWNER) {
        EnableWindow(g_frame, FALSE);
        SendMessage(b, WM_LBUTTONUP, 0, in);
        EnableWindow(g_frame, TRUE);
        SetFocus(child(WCH_EDIT));
        wsprintf(t, k_ans2, p, c);
    } else {
        if (how == WCH_GRAY)
            EnableWindow(b, FALSE);
        else
            SendMessage(child(WCH_TOOL), WM_CANCELMODE, 0, 0L);
        wsprintf(t, k_ans4, p, c, pressed(b), GetCapture() == NULL);
        SendMessage(b, WM_LBUTTONUP, 0, in);
        EnableWindow(b, TRUE);
    }
    ans(t);
}

/* The colour of Open's point WCH_PIXX, WCH_PIXY as drawn. */
static COLORREF pixel(HWND b)
{
    COLORREF c;
    HDC dc;

    UpdateWindow(b);
    dc = GetDC(b);
    c = GetPixel(dc, WCH_PIXX, WCH_PIXY);
    ReleaseDC(b, dc);
    return c;
}

/* Say the colour of Open's point usable, then grayed. */
static void pixels(void)
{
    char t[WCH_TEXTSZ];
    COLORREF on;
    HWND b;

    b = button(1);
    on = pixel(b);
    EnableWindow(b, FALSE);
    wsprintf(t, k_pix, (DWORD)on, (DWORD)pixel(b));
    EnableWindow(b, TRUE);
    ans(t);
}

/* Post Open a double click at its middle: down, up, down, up. */
static void dblclk(void)
{
    LPARAM in;
    HWND b;

    b = button(1);
    in = MAKELPARAM(WTB_BTNW / 2, WTB_BTNH / 2);
    PostMessage(b, WM_LBUTTONDOWN, MK_LBUTTON, in);
    PostMessage(b, WM_LBUTTONUP, 0, in);
    PostMessage(b, WM_LBUTTONDBLCLK, MK_LBUTTON, in);
    PostMessage(b, WM_LBUTTONUP, 0, in);
}

/* Post button k of the bar the mouse: down at its middle, then, by
   how, moved off it and let go there (0), moved off and back and let
   go at its middle (1), or let go at once (2). */
static void click(int k, int how)
{
    HWND b;
    LPARAM in;
    LPARAM off;

    b = button(k);
    in = MAKELPARAM(WTB_BTNW / 2, WTB_BTNH / 2);
    off = MAKELPARAM(WTB_BTNW / 2, WCH_OFF);
    PostMessage(b, WM_LBUTTONDOWN, MK_LBUTTON, in);
    if (how < 2)
        PostMessage(b, WM_MOUSEMOVE, MK_LBUTTON, off);
    if (how == 1)
        PostMessage(b, WM_MOUSEMOVE, MK_LBUTTON, in);
    PostMessage(b, WM_LBUTTONUP, 0, how == 0 ? off : in);
}

/* The splitter's left edge and top in the frame's client area, into
   *r. */
static void split_at(RECT *r)
{
    GetWindowRect(child(WCH_SPLIT), r);
    ScreenToClient(g_frame, (POINT FAR *)&r->left);
}

/* The splitter's mouse at x in its client area, half way down. */
static LPARAM at(int x)
{
    return MAKELPARAM((UINT)x, WCH_ROWH / 2);
}

/* The splitter's mouse at the guide's left edge g, the splitter's left
   edge being left. */
static LPARAM guide_at(int g, int left)
{
    return at(g - left + WCH_GRAB);
}

/* Post the splitter a press at WCH_GRAB (a double click's with dbl), a
   move to m1, one to m2 unless it is m1, and the release at the last. */
static void drag(int dbl, LPARAM m1, LPARAM m2)
{
    HWND s;

    s = child(WCH_SPLIT);
    PostMessage(s, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON,
                at(WCH_GRAB));
    PostMessage(s, WM_MOUSEMOVE, MK_LBUTTON, m1);
    if (m2 != m1)
        PostMessage(s, WM_MOUSEMOVE, MK_LBUTTON, m2);
    PostMessage(s, WM_LBUTTONUP, 0, m2);
}

/* Say the splitter's state, a whether it held the mouse after the
   key's press (header). */
static void split_state(int a)
{
    char t[WCH_TEXTSZ];
    COLORREF p1;
    COLORREF p2;
    RECT r;
    HWND s;
    HDC dc;

    s = child(WCH_SPLIT);
    split_at(&r);
    dc = GetDC(g_frame);
    p1 = GetPixel(dc, WCH_GX1 + WSP_WIDTH / 2, r.top + WCH_ROWH / 2);
    p2 = GetPixel(dc, WCH_GX2 + WSP_WIDTH / 2, r.top + WCH_ROWH / 2);
    ReleaseDC(g_frame, dc);
    wsprintf(t, k_split, a, GetCapture() == s, GetFocus() == s, (DWORD)p1,
             (DWORD)p2);
    ans(t);
}

/* The splitter's CTRL keys, letter vk (header); 1 when it is one. */
static int split_key(UINT vk)
{
    RECT r;
    HWND s;
    int a;

    s = child(WCH_SPLIT);
    split_at(&r);
    a = 0;
    switch (vk) {
    case 'G':
        SendMessage(s, WM_LBUTTONDOWN, MK_LBUTTON, at(WCH_GRAB));
        a = GetCapture() == s;
        SendMessage(s, WM_MOUSEMOVE, MK_LBUTTON, guide_at(WCH_GX1, r.left));
        SendMessage(s, WM_MOUSEMOVE, MK_LBUTTON, guide_at(WCH_GX2, r.left));
        break;
    case 'U':
        SendMessage(s, WM_LBUTTONUP, 0, guide_at(WCH_GX2, r.left));
        break;
    case 'K':
    case 'B':
        if (vk == 'B')
            EnableWindow(s, FALSE);
        SendMessage(s, WM_LBUTTONDOWN, MK_LBUTTON, at(WCH_GRAB));
        a = GetCapture() == s;
        SendMessage(s, WM_MOUSEMOVE, MK_LBUTTON, guide_at(WCH_GX2, r.left));
        if (vk == 'K')
            SendMessage(s, WM_CANCELMODE, 0, 0L);
        split_state(a);
        SendMessage(s, WM_LBUTTONUP, 0, guide_at(WCH_GX2, r.left));
        EnableWindow(s, TRUE);
        return 1;
    default:
        return 0;
    }
    split_state(a);
    return 1;
}

/* The preview panel's CTRL keys, letter vk (header); 1 when it is
   one. */
static int prev_key(UINT vk)
{
    char t[WCH_TEXTSZ];
    char l[WPV_LINESZ];
    HWND p;
    HDC dc;
    int odd;
    int n;
    int i;

    p = child(WCH_PREV);
    switch (vk) {
    case 'T':
        wpv_text(p, WCH_S_LOAD, k_item);
        return 1;
    case 'P':
        wpv_text(p, 0, NULL);
        n = 0;
        for (i = 0; i < WCH_NFIELDS; i++)
            n += wpv_add(p, k_fields[i][0], k_fields[i][1]);
        n += wpv_note(p, k_note1) + wpv_note(p, k_note2) +
             wpv_note(p, k_note3);
        for (i = 1; i <= WCH_NLINES; i++) {
            wsprintf(t, k_line, i);
            n += wpv_note(p, t);
        }
        wpv_done(p);
        wsprintf(t, k_ans1, n);
        break;
    case 'L':
        wpv_text(p, 0, NULL);
        odd = GetKeyState(VK_SHIFT) < 0 && wpv_add(p, k_label, k_odd);
        for (n = 0; wpv_add(p, k_label, k_value); n++)
            ;
        i = (int)SendMessage(p, WPVM_LINE, (WPARAM)(odd + n - 1),
                             (LPARAM)(LPSTR)l);
        wsprintf(t, k_ans3, n, i,
                 (int)SendMessage(p, WPVM_LINE, (WPARAM)(odd + n),
                                  (LPARAM)(LPSTR)l));
        wpv_text(p, 0, NULL);
        break;
    case 'M':
        SendMessage(p, WM_KEYDOWN, VK_SHIFT, 0L);
        SendMessage(p, WM_KEYDOWN, VK_CONTROL, 0L);
        return 1;
    case 'R':
        UpdateWindow(p);
        dc = GetDC(p);
        wsprintf(t, k_ans1, (GetPixel(dc, 1, 1) == 0) +
                            (GetPixel(dc, 2, 1) == 0));
        ReleaseDC(p, dc);
        break;
    case 'H':
        SendMessage(p, WM_VSCROLL, SB_THUMBPOSITION,
                    MAKELPARAM(WCH_THUMB, 0));
        return 1;
    default:
        return 0;
    }
    ans(t);
    return 1;
}

/* Set panel i's text t and say the answer. */
static void settext(int i, LPCSTR t)
{
    char a[WCH_TEXTSZ];

    wsprintf(a, k_ans1, (int)SendMessage(child(WCH_STAT), WSBM_SETTEXT,
                                         (WPARAM)i, (LPARAM)t));
    ans(a);
}

/* Make the bar n panels of widths w and say the answer. */
static void panels(int n, const int *w)
{
    char a[WCH_TEXTSZ];

    wsprintf(a, k_ans1, (int)SendMessage(child(WCH_STAT), WSBM_PANELS,
                                         (WPARAM)n,
                                         (LPARAM)(const int FAR *)w));
    ans(a);
}

/* The frame's own keys, as this file's header gives them; 1 when vk is
   one. */
static int key(UINT vk)
{
    char t[WCH_TEXTSZ];
    RECT r;
    HWND bar;
    int shift;
    int ctrl;
    int i;

    bar = child(WCH_TOOL);
    shift = GetKeyState(VK_SHIFT) < 0;
    ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && vk >= 'A' && vk <= 'Z')
        return split_key(vk) || prev_key(vk);
    switch (vk) {
    case VK_F1:
        if (GetKeyState(VK_MENU) < 0) {
            split_at(&r);
            drag(1, guide_at(WCH_SPX, r.left), guide_at(WCH_SPX, r.left));
        } else if (ctrl && shift) {
            drag(0, at(WCH_GRAB + WCH_STEP), at(WCH_GRAB));
        } else if (ctrl) {
            drag(0, at(WCH_GRAB - WCH_FAR), at(WCH_GRAB - WCH_FAR));
        } else if (shift) {
            SendMessage(child(WCH_SPLIT), WSPM_LIMITS, WCH_SPLO,
                        MAKELPARAM(WCH_SPHI, 0));
            drag(0, at(WCH_GRAB + WCH_FAR), at(WCH_GRAB + WCH_FAR));
        } else {
            drag(0, at(WCH_GRAB + WCH_STEP), at(WCH_GRAB + WCH_STEP));
        }
        return 1;
    case VK_F2:
        if (ctrl)
            click(0, 0);
        else
            click(shift ? 2 : 1, 2);
        return 1;
    case VK_F3:
        if (shift) {
            slot(WCH_SLOTW2);
        } else if (ctrl) {
            wsprintf(t, k_ans2,
                     SendMessage(bar, WTBM_BUTTON, 7, 0L) != 0,
                     SendMessage(bar, WTBM_BUTTON, 8, 0L) != 0);
            ans(t);
        } else {
            click(6, 1);
        }
        return 1;
    case VK_F4:
        if (ctrl) {
            press(shift ? WCH_CANCEL : WCH_GRAY);
            return 1;
        }
        EnableWindow(GetDlgItem(bar, WCH_CMD + 2), shift);
        EnableWindow(GetDlgItem(bar, WCH_CMD + 3), shift);
        return 1;
    case VK_F5:
        if (ctrl) {
            click(7, 2);
        } else if (shift) {
            PostMessage(bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 1));
            PostMessage(bar, WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
        } else {
            SendMessage(bar, WM_COMMAND, WCH_FIND,
                        MAKELPARAM(child(WCH_FIND), EN_CHANGE));
        }
        return 1;
    case VK_F6:
        if (GetKeyState(VK_MENU) < 0)
            panels(0, NULL);
        else if (ctrl && shift)
            panels(1, NULL);
        else if (ctrl)
            panels(WSB_MAXPANELS + 1, k_four);
        else if (shift)
            panels(2, k_two);
        else
            panels(4, k_four);
        return 1;
    case VK_F7:
        if (ctrl && shift)
            settext(1, NULL);
        else if (ctrl)
            settext(4, k_x);
        else if (shift)
            settext(3, k_tab);
        else
            for (i = 0; i < WCH_NTEXTS; i++)
                settext(i, k_texts[i]);
        return 1;
    case VK_F8:
        if (ctrl)
            pixels();
        else
            settext(shift ? 0 : 3, k_long);
        return 1;
    case VK_F9:
        if (ctrl && shift) {
            dblclk();
            return 1;
        }
        if (ctrl) {
            press(WCH_OWNER);
            return 1;
        }
        SetWindowPos(g_frame, NULL, 0, 0, shift ? WCH_WIDE : WCH_NARROW,
                     WCH_TALL, SWP_NOMOVE | SWP_NOZORDER);
        return 1;
    default:
        return 0;
    }
}

/* Lay the frame's children out to its client area, cx by cy. */
static void layout(int cx, int cy)
{
    RECT r;
    int y;
    int id;

    MoveWindow(child(WCH_TOOL), 0, 0, cx, WTB_HEIGHT, TRUE);
    GetWindowRect(child(WCH_STAT), &r);
    MoveWindow(child(WCH_STAT), 0, cy - (r.bottom - r.top), cx,
               r.bottom - r.top, TRUE);
    y = WTB_HEIGHT + WCH_M;
    for (id = WCH_EDIT; id <= WCH_ANS; id++) {
        MoveWindow(child(id), WCH_M, y, max(cx - 2 * WCH_M, 0), WCH_ROWH,
                   TRUE);
        y += WCH_ROWH + WCH_M;
    }
    split_at(&r);
    MoveWindow(child(WCH_SPLIT), r.left, y, WSP_WIDTH, WCH_ROWH, TRUE);
    y += WCH_ROWH + WCH_M;
    GetWindowRect(child(WCH_STAT), &r);
    ScreenToClient(g_frame, (POINT FAR *)&r.left);
    MoveWindow(child(WCH_PREV), WCH_M, y, max(cx - 2 * WCH_M, 0),
               max(r.top - WCH_M - y, 0), TRUE);
}

/* Make the frame w's children, as this file's header says, the slot
   reserved once the static that says the answer is there; 0 when one
   is missing. */
static int make(HWND w)
{
    char t[WCH_TEXTSZ];
    HINSTANCE inst;
    RECT r;

    inst = (HINSTANCE)GetWindowWord(w, GWW_HINSTANCE);
    g_frame = w;
    wsprintf(t, k_seen, 0, 0, 0);
    SetRectEmpty(&r);
    if (wtb_create(w, &r, WCH_TOOL, WCH_S_TOOL, k_cmds) == NULL ||
        CreateWindow(k_edit, NULL,
                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                     0, 0, 0, 0, child(WCH_TOOL), (HMENU)WCH_FIND, inst,
                     NULL) == NULL ||
        CreateWindow(k_edit, NULL,
                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                     0, 0, 0, 0, w, (HMENU)WCH_EDIT, inst, NULL) == NULL ||
        CreateWindow(k_static, t, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, w,
                     (HMENU)WCH_SEEN, inst, NULL) == NULL ||
        CreateWindow(k_static, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, w,
                     (HMENU)WCH_ANS, inst, NULL) == NULL ||
        wsb_create(w, &r, WCH_STAT, NULL) == NULL ||
        wsp_create(w, WCH_SPLIT) == NULL ||
        wpv_create(w, WCH_PREV) == NULL)
        return 0;
    MoveWindow(child(WCH_SPLIT), WCH_SPX, 0, WSP_WIDTH, 0, FALSE);
    slot(WCH_SLOTW);
    return 1;
}

LRESULT WCALLBACK wch_frame(HWND w, UINT wm, WPARAM wp, LPARAM lp)
{
    char t[WCH_TEXTSZ];

    switch (wm) {
    case WM_CREATE:
        return make(w) ? 0 : -1;
    case WM_SIZE:
        layout((int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_SETFOCUS:
        SetFocus(child(WCH_EDIT));
        return 0;
    case WM_COMMAND:
        if (wp == WCH_EDIT)
            return 0;
        if (wp == WCH_SPLIT && (HWND)LOWORD(lp) != child(WCH_SPLIT))
            wp = 0;
        wsprintf(t, k_seen, ++g_seen, wp, HIWORD(lp));
        SetWindowText(child(WCH_SEEN), t);
        if (wp == WCH_PREV) {
            wsprintf(t, k_ans1, (int)LOWORD(lp));
            ans(t);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(w, wm, wp, lp);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char words[WCH_CMDMAX];
    const char *keys;
    const char *snap;
    WNDCLASS wc;
    HWND w;
    char *p;
    MSG m;

    (void)prev;
    for (p = words; *cmd != '\0' && p < words + sizeof words - 1; )
        *p++ = *cmd++;
    *p = '\0';
    keys = NULL;
    snap = NULL;
    for (p = words; *p != '\0'; ) {
        while (*p == ' ')
            *p++ = '\0';
        if (txt_ieqn(p, k_keys, sizeof k_keys - 1))
            keys = p + sizeof k_keys - 1;
        else if (txt_ieqn(p, k_snap, sizeof k_snap - 1))
            snap = p + sizeof k_snap - 1;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    td_init(inst, keys, snap);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wch_frame;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = k_class;
    m.wParam = 0;
    w = RegisterClass(&wc) == 0 ? NULL :
        CreateWindow(k_class, k_class, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                     WCH_WIDE, WCH_TALL, NULL, NULL, inst, NULL);
    if (w != NULL) {
        ShowWindow(w, show);
        UpdateWindow(w);
        SetFocus(child(WCH_EDIT));
        while (GetMessage(&m, NULL, 0, 0)) {
            if ((m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) &&
                key(m.wParam))
                continue;
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    td_done();
    return (int)m.wParam;
}
