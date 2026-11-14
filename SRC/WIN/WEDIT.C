/*
 * WEDIT.C - the password, read-only and OEM text edit controls of the
 * Windows programs (WEDIT.H).
 *
 * Every subclassed control shares one procedure and one thunk, made
 * with the instance of the control's parent, as a control of a dialog
 * whose edit controls keep their text outside DGROUP has an instance of
 * its own; the edit controls' procedure, which each gives up, is the
 * class's, the same for all of them.  A control is subclassed when its
 * procedure is the thunk, and its kind is in its style.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "WEDIT.H"

#define WE_CTRLBS       0x7F    /* the WM_CHAR of CTRL+BS, DEL          */

static const char k_empty[] = "";

static FARPROC we_thunk;        /* we_proc's thunk, or NULL             */
static WNDPROC we_old;          /* the edit controls' own procedure     */
static u16 we_count;            /* controls subclassed                  */

/* 1 when e is of style ES_PASSWORD. */
static int is_password(HWND e)
{
    return (GetWindowLong(e, GWL_STYLE) & ES_PASSWORD) != 0;
}

/* 1 when we_attach subclassed e. */
static int attached(HWND e)
{
    return we_thunk != NULL &&
           (FARPROC)GetWindowLong(e, GWL_WNDPROC) == we_thunk;
}

/* 1 when wm is one a control of style st, a password or read-only one,
   drops: cut, paste, clear and undo, the INS key but a read-only
   control's CTRL+INS, its copy, ALT+BS, which the control would undo by
   itself without WM_UNDO, and CTRL+BS, which it would type as the
   character WE_CTRLBS. */
static int dropped(DWORD st, UINT wm, WPARAM wp)
{
    return wm == WM_CUT || wm == WM_PASTE || wm == WM_CLEAR ||
           wm == WM_UNDO ||
           (wm == WM_KEYDOWN && wp == VK_INSERT &&
            ((st & ES_PASSWORD) || GetKeyState(VK_SHIFT) < 0 ||
             GetKeyState(VK_CONTROL) >= 0)) ||
           ((wm == WM_SYSKEYDOWN || wm == WM_SYSCHAR) && wp == VK_BACK) ||
           (wm == WM_CHAR && wp == WE_CTRLBS);
}

/* A password control: BS and DEL delete the selection, or the character
   before or after the caret, and a character typed over a selection
   replaces it, on the copy t; the control then counts as changed
   (EM_GETMODIFY), as SetWindowText clears that. */
static LRESULT password(HWND e, UINT wm, WPARAM wp, LPARAM lp)
{
    char t[LINK_PWMAX + 1];
    DWORD sel;
    int from;
    int to;
    int n;

    if (wm != WM_CHAR && !(wm == WM_KEYDOWN && wp == VK_DELETE))
        return CallWindowProc(we_old, e, wm, wp, lp);
    sel = (DWORD)SendMessage(e, EM_GETSEL, 0, 0L);
    from = LOWORD(sel);
    to = HIWORD(sel);
    if (wm == WM_CHAR && wp != '\b' && (wp < ' ' || from == to))
        return CallWindowProc(we_old, e, wm, wp, lp);
    n = GetWindowText(e, t, (int)sizeof t);
    if (from == to && wp == '\b' && wm == WM_CHAR && from > 0)
        from--;
    else if (from == to && wm == WM_KEYDOWN && to < n)
        to++;
    memmove(t + from, t + to, (size_t)(n - to + 1));
    if (wm == WM_CHAR && wp != '\b') {
        memmove(t + from + 1, t + from, (size_t)(n - to + 1));
        t[from++] = (char)wp;
    }
    SetWindowText(e, t);
    txt_wipe(t, (u16)sizeof t);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(from, from));
    SendMessage(e, EM_SETMODIFY, TRUE, 0L);
    return 0;
}

/* The clipboard of an OEM text control e: WM_COPY and WM_CUT put the
   selection there as CF_OEMTEXT, leaving the clipboard as it was when
   nothing is selected, as an edit control does, and WM_PASTE puts
   CF_OEMTEXT in place of the selection. */
static void oem_clip(HWND e, UINT wm)
{
    HGLOBAL h;
    char FAR *p;
    DWORD sel;
    int from;
    int to;
    int k;

    sel = (DWORD)SendMessage(e, EM_GETSEL, 0, 0L);
    from = LOWORD(sel);
    to = HIWORD(sel);
    if ((wm != WM_PASTE && from == to) || !OpenClipboard(e))
        return;
    if (wm == WM_PASTE) {
        h = GetClipboardData(CF_OEMTEXT);
        p = h != NULL ? (char FAR *)GlobalLock(h) : NULL;
        if (p != NULL) {
            SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)p);
            GlobalUnlock(h);
        }
    } else {
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)to + 1);
        p = h != NULL ? (char FAR *)GlobalLock(h) : NULL;
        if (p != NULL) {
            GetWindowText(e, p, to + 1);
            for (k = from; k <= to; k++)
                p[k - from] = p[k];
            GlobalUnlock(h);
            EmptyClipboard();
            SetClipboardData(CF_OEMTEXT, h);
            if (wm == WM_CUT)
                SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)k_empty);
        } else if (h != NULL) {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

/* An OEM text control: a character above 7Fh comes in through
   AnsiToOemBuff, and the clipboard, by message or by SHIFT-INS, CTRL-INS
   and SHIFT-DEL, carries CF_OEMTEXT. */
static LRESULT oem(HWND e, UINT wm, WPARAM wp, LPARAM lp)
{
    char ch;

    if (wm == WM_KEYDOWN && (wp == VK_INSERT || wp == VK_DELETE) &&
        (GetKeyState(VK_SHIFT) < 0 ||
         (wp == VK_INSERT && GetKeyState(VK_CONTROL) < 0))) {
        oem_clip(e, wp == VK_DELETE ? WM_CUT :
                    GetKeyState(VK_SHIFT) < 0 ? WM_PASTE : WM_COPY);
        return 0;
    }
    if (wm == WM_COPY || wm == WM_CUT || wm == WM_PASTE) {
        oem_clip(e, wm);
        return 0;
    }
    if (wm == WM_CHAR && wp > 0x7F) {
        ch = (char)wp;
        AnsiToOemBuff(&ch, &ch, 1);
        wp = (u8)ch;
    }
    return CallWindowProc(we_old, e, wm, wp, lp);
}

/* The subclass of an edit control e, as WEDIT.H gives it. */
LRESULT WCALLBACK we_proc(HWND e, UINT wm, WPARAM wp, LPARAM lp)
{
    DWORD st;

    st = (DWORD)GetWindowLong(e, GWL_STYLE);
    if (!(st & (ES_PASSWORD | WE_READONLY)))
        return oem(e, wm, wp, lp);
    if (dropped(st, wm, wp))
        return 0;
    if (st & ES_PASSWORD)
        return password(e, wm, wp, lp);
    if (wm == WM_CHAR || (wm == WM_KEYDOWN && wp == VK_DELETE))
        return 0;
    return CallWindowProc(we_old, e, wm, wp, lp);
}

int we_attach(HWND e, int readonly)
{
    if (readonly)
        SetWindowLong(e, GWL_STYLE, GetWindowLong(e, GWL_STYLE) |
                                    WE_READONLY);
    if (attached(e))
        return 1;
    if (we_thunk == NULL) {
        we_thunk = MakeProcInstance((FARPROC)we_proc,
                                    (HINSTANCE)GetWindowWord(GetParent(e),
                                                             GWW_HINSTANCE));
        if (we_thunk == NULL)
            return 0;
    }
    if (is_password(e))
        SendMessage(e, EM_LIMITTEXT, LINK_PWMAX, 0L);
    we_old = (WNDPROC)SetWindowLong(e, GWL_WNDPROC, (LONG)we_thunk);
    we_count++;
    return 1;
}

void we_wipe(HWND e)
{
    char blanks[LINK_PWMAX + 1];
    int n;

    n = GetWindowTextLength(e);
    if (n > LINK_PWMAX)
        n = LINK_PWMAX;
    memset(blanks, ' ', (size_t)n);
    blanks[n] = '\0';
    SetWindowText(e, blanks);
    SetWindowText(e, blanks + n);
}

void we_detach(HWND e)
{
    if (is_password(e))
        we_wipe(e);
    if (!attached(e))
        return;
    SetWindowLong(e, GWL_WNDPROC, (LONG)we_old);
    if (--we_count == 0) {
        FreeProcInstance(we_thunk);
        we_thunk = NULL;
    }
}
