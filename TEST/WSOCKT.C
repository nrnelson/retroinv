/*
 * WSOCKT.C - the Winsock test: a Win16 program that loads WINSOCK.DLL at
 * run time, talks to the RETROINV server through the TCP bridge INVTCP
 * and reports what the server answers.
 *
 * usage: WIN WSOCKT ADDR PORT   (from the directory the report is to be
 *                               written in)
 *
 * ADDR is the bridge's IPv4 address, dotted, PORT its TCP port.  Winsock
 * is reached only in protected mode, and only when WINSOCK.DLL is on
 * Windows' search path (OpenFile), which spares Windows 3.0 the box it
 * shows for a library that is missing; then through LoadLibrary, each
 * entry found by name with GetProcAddress, so that the program loads and
 * ends cleanly where there is no Winsock.  It sends k_request, three
 * requests, in as few writes as the stack takes, and reads their
 * WST_RESPONSES responses, after the last of which the server closes
 * the session.  The socket is non-blocking, driven by the messages
 * WSAAsyncSelect posts, and a timer bounds the exchange to WST_SECS
 * seconds, so no call blocks the message loop.  It writes WSOCKT.OUT in
 * its current directory, lines ending CR LF:
 *
 *   WSOCKT MODE ENHANCED     REAL, STANDARD or ENHANCED, from GetWinFlags
 *   WSOCKT WINSOCK 1.1 Microsoft Windows Sockets Version 1.1.
 *                            the version WSAStartup agreed and the
 *                            stack's description; or NONE and why:
 *                            REALMODE, NOFILE, NOLOAD and the error
 *                            LoadLibrary gave, or NOENTRY and the entry
 *   WSOCKT RECV +OK          each response line, in order
 *   WSOCKT RESULT PASS       PASS: every response came; NOWINSOCK: this
 *                            Windows has none; FAIL, the step and the
 *                            Winsock error: USAGE, STARTUP, SOCKET,
 *                            CONNECT, SEND, RECV, CLOSED or TIMEOUT
 *   WSOCKT EXIT 0            0 for PASS and NOWINSOCK, else 1
 *
 * It then ends the Windows session with ExitWindows, so that WIN.COM
 * returns to DOS; should another program refuse to end, it returns
 * alone.  The program is real-mode safe as TEST/WSMOKE.C is: the socket's
 * and the timer's messages come to the procedure of its window's class,
 * and it keeps no pointer into movable memory.  Windows' wsprintf takes
 * no "*" precision, so every text it formats is bounded beforehand.
 */
#include <string.h>
#include <winsock.h>
#include "COMPAT.H"
#include "CWIN.H"

#define WST_VERSION     0x0101  /* the Winsock asked for, 1.1           */
#define WST_SECS        60      /* the whole exchange's limit, seconds  */
#define WST_RESPONSES   3       /* the response lines k_request gets    */
#define WST_LINEMAX     128     /* a response line kept, its NUL in     */
#define WST_ARGMAX      16      /* "255.255.255.255" and its NUL        */
#define WST_DESCMAX     64      /* the stack's description reported     */
/* The report: at most eight lines, none longer than a response line
   after "WSOCKT RECV " and before CR LF. */
#define WST_REPORTLEN   (8 * (WST_LINEMAX + 16))
#define WST_WM_SOCKET   (WM_USER + 1)   /* WSAAsyncSelect's message     */
#define WST_EVENTS      (FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE)
#define WST_TIMER       1       /* the timer's ID                       */
#define WST_TIMERMS     1000    /* its period: a second                 */

/* The outcomes of the RESULT line. */
#define WR_PASS         0
#define WR_NOWINSOCK    1
#define WR_FAIL         2

/* What read_lines found. */
#define RL_MORE         0       /* bytes came; more may follow          */
#define RL_NONE         1       /* none: the peer closed, or none yet   */
#define RL_STOP         2       /* every response came, or recv failed  */

static const char k_class[] = "WSOCKT";
static const char k_outname[] = "WSOCKT.OUT";
static const char k_dll[] = "WINSOCK.DLL";
static const char k_request[] = "HELLO WSOCKT\r\nVER\r\nBYE\r\n";

/* The entries of WINSOCK.DLL the test calls. */
typedef int (FAR PASCAL *ws_startup_fn)(WORD, LPWSADATA);
typedef int (FAR PASCAL *ws_void_fn)(void);
typedef int (FAR PASCAL *ws_select_fn)(SOCKET, HWND, u_int, long);
typedef SOCKET (FAR PASCAL *ws_socket_fn)(int, int, int);
typedef int (FAR PASCAL *ws_connect_fn)(SOCKET, const struct sockaddr FAR *,
                                        int);
typedef int (FAR PASCAL *ws_send_fn)(SOCKET, const char FAR *, int, int);
typedef int (FAR PASCAL *ws_recv_fn)(SOCKET, char FAR *, int, int);
typedef int (FAR PASCAL *ws_close_fn)(SOCKET);
typedef u_long (FAR PASCAL *ws_addr_fn)(const char FAR *);

static struct {
    ws_startup_fn startup;
    ws_void_fn cleanup;
    ws_void_fn last_error;
    ws_select_fn async_select;
    ws_socket_fn socket;
    ws_connect_fn connect;
    ws_send_fn send;
    ws_recv_fn recv;
    ws_close_fn close;
    ws_addr_fn inet_addr;
} wst_fn;

static char wst_report[WST_REPORTLEN];
static int wst_len;
static int wst_result = WR_PASS;
static HINSTANCE wst_lib;       /* WINSOCK.DLL, once loaded             */
static const char *wst_missing; /* the first entry GetProcAddress lacks */
static int wst_started;         /* 1 once WSAStartup succeeded          */
static SOCKET wst_sock = INVALID_SOCKET;
static int wst_connected;       /* 1 once the connection is made        */
static int wst_sent;            /* bytes of k_request sent              */
static char wst_line[WST_LINEMAX];
static int wst_linelen;
static int wst_lines;           /* response lines received              */
static int wst_secs = WST_SECS; /* seconds left                         */

/* Add "WSOCKT text" and CR LF to the report. */
static void add(const char *text)
{
    wst_len += wsprintf(wst_report + wst_len, "WSOCKT %s\r\n", (LPCSTR)text);
}

/* End the exchange as failed at step, with err, or when it is 0
   Winsock's last error, which only a ws_open that succeeded can ask. */
static void fail(const char *step, int err)
{
    char text[WST_LINEMAX];

    if (err == 0)
        err = wst_fn.last_error();
    wsprintf(text, "RESULT FAIL %s %d", (LPCSTR)step, err);
    add(text);
    wst_result = WR_FAIL;
}

/* Report that there is no Winsock, and why. */
static void no_winsock(const char *why)
{
    char text[WST_LINEMAX];

    wsprintf(text, "WINSOCK NONE %s", (LPCSTR)why);
    add(text);
    wst_result = WR_NOWINSOCK;
}

/* GetProcAddress of name in WINSOCK.DLL, noting the first one missing. */
static FARPROC entry(const char *name)
{
    FARPROC p;

    p = GetProcAddress(wst_lib, name);
    if (p == NULL && wst_missing == NULL)
        wst_missing = name;
    return p;
}

/* Load WINSOCK.DLL and its entries and start it; 1 when it is ready,
   0 with the reason reported. */
static int ws_open(void)
{
    char text[WST_LINEMAX];
    OFSTRUCT of;
    WSADATA wd;
    int n;
    int i;

    if (!(GetWinFlags() & WF_PMODE)) {
        no_winsock("REALMODE");
        return 0;
    }
    if (OpenFile(k_dll, &of, OF_EXIST) == HFILE_ERROR) {
        no_winsock("NOFILE");
        return 0;
    }
    wst_lib = LoadLibrary(of.szPathName);
    if ((UINT)wst_lib < (UINT)HINSTANCE_ERROR) {
        wsprintf(text, "WINSOCK NONE NOLOAD %u", (UINT)wst_lib);
        add(text);
        wst_lib = NULL;
        fail("STARTUP", WSASYSNOTREADY);
        return 0;
    }
    wst_fn.startup = (ws_startup_fn)entry("WSAStartup");
    wst_fn.cleanup = (ws_void_fn)entry("WSACleanup");
    wst_fn.last_error = (ws_void_fn)entry("WSAGetLastError");
    wst_fn.async_select = (ws_select_fn)entry("WSAAsyncSelect");
    wst_fn.socket = (ws_socket_fn)entry("socket");
    wst_fn.connect = (ws_connect_fn)entry("connect");
    wst_fn.send = (ws_send_fn)entry("send");
    wst_fn.recv = (ws_recv_fn)entry("recv");
    wst_fn.close = (ws_close_fn)entry("closesocket");
    wst_fn.inet_addr = (ws_addr_fn)entry("inet_addr");
    if (wst_missing != NULL) {
        wsprintf(text, "WINSOCK NONE NOENTRY %s", (LPCSTR)wst_missing);
        add(text);
        fail("STARTUP", WSASYSNOTREADY);
        return 0;
    }
    i = wst_fn.startup(WST_VERSION, &wd);
    if (i != 0) {
        fail("STARTUP", i);
        return 0;
    }
    wst_started = 1;
    n = wsprintf(text, "WINSOCK %u.%u ", LOBYTE(wd.wVersion),
                 HIBYTE(wd.wVersion));
    for (i = 0; i < WST_DESCMAX && wd.szDescription[i] != '\0'; i++)
        text[n++] = wd.szDescription[i];
    text[n] = '\0';
    add(text);
    return 1;
}

/* Parse "ADDR PORT" from cmd into *addr and *port, in network byte
   order; 1 when both are good. */
static int parse_args(LPSTR cmd, u_long *addr, u_short *port)
{
    char a[WST_ARGMAX];
    u32 p;
    char c;
    int n;

    for (n = 0; *cmd != ' ' && *cmd != '\0'; cmd++) {
        if (n == WST_ARGMAX - 1)
            return 0;
        a[n++] = *cmd;
    }
    a[n] = '\0';
    while (*cmd == ' ')
        cmd++;
    p = 0;
    for (c = *cmd; TXT_ISDIGIT(c); c = *++cmd) {
        p = p * 10 + (u32)(c - '0');
        if (p > 0xFFFFu)
            return 0;
    }
    while (*cmd == ' ')
        cmd++;
    if (n == 0 || p == 0 || *cmd != '\0')
        return 0;
    *addr = wst_fn.inet_addr(a);
    /* x86 order to network order, as htons would. */
    *port = (u_short)(((p & 0xFF) << 8) | (p >> 8));
    return *addr != INADDR_NONE;
}

/* Send what is left of k_request once connected; 1 while the exchange
   goes on, 0 with the failure reported. */
static int send_more(void)
{
    int n;

    if (!wst_connected || wst_sent == (int)sizeof k_request - 1)
        return 1;
    n = wst_fn.send(wst_sock, k_request + wst_sent,
                    (int)sizeof k_request - 1 - wst_sent, 0);
    if (n != SOCKET_ERROR) {
        wst_sent += n;
        return 1;
    }
    if (wst_fn.last_error() == WSAEWOULDBLOCK)
        return 1;
    fail("SEND", 0);
    return 0;
}

/* Read what the socket holds into response lines, reporting each:
   RL_MORE, RL_NONE or RL_STOP. */
static int read_lines(void)
{
    char buf[WST_LINEMAX];
    char text[WST_LINEMAX + 8];
    int n;
    int i;

    n = wst_fn.recv(wst_sock, buf, sizeof buf, 0);
    if (n == SOCKET_ERROR) {
        if (wst_fn.last_error() == WSAEWOULDBLOCK)
            return RL_NONE;
        fail("RECV", 0);
        return RL_STOP;
    }
    if (n == 0)
        return RL_NONE;
    for (i = 0; i < n; i++) {
        if (buf[i] != '\n') {
            if (wst_linelen == WST_LINEMAX - 1) {
                fail("RECV", WSAEMSGSIZE);
                return RL_STOP;
            }
            wst_line[wst_linelen++] = buf[i];
            continue;
        }
        if (wst_linelen != 0 && wst_line[wst_linelen - 1] == '\r')
            wst_linelen--;
        wst_line[wst_linelen] = '\0';
        wst_linelen = 0;
        wsprintf(text, "RECV %s", (LPCSTR)wst_line);
        add(text);
        if (++wst_lines == WST_RESPONSES)
            return RL_STOP;
    }
    return RL_MORE;
}

/* One socket event, with err its error; 1 while the exchange goes on. */
static int on_socket(int event, int err)
{
    int r;

    switch (event) {
    case FD_CONNECT:
        if (err != 0) {
            fail("CONNECT", err);
            return 0;
        }
        wst_connected = 1;
        return send_more();
    case FD_WRITE:
        return send_more();
    case FD_READ:
        return read_lines() != RL_STOP;
    case FD_CLOSE:
        do
            r = read_lines();
        while (r == RL_MORE);
        if (r == RL_NONE)
            fail("CLOSED", err != 0 ? err : WSAECONNRESET);
        return 0;
    default:
        return 1;
    }
}

LRESULT WCALLBACK wst_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WST_WM_SOCKET:
        if ((SOCKET)wp == wst_sock && wst_result == WR_PASS &&
            !on_socket(WSAGETSELECTEVENT(lp), WSAGETSELECTERROR(lp)))
            DestroyWindow(wnd);
        return 0;
    case WM_TIMER:
        if (--wst_secs == 0) {
            fail("TIMEOUT", WSAETIMEDOUT);
            DestroyWindow(wnd);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(wnd, WST_TIMER);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(wnd, msg, wp, lp);
    }
}

/* Show the window the socket's messages go to and start connecting to
   addr:port; 1 while the exchange goes on, 0 with the failure
   reported. */
static int start(HINSTANCE inst, HINSTANCE prev, int show, u_long addr,
                 u_short port)
{
    struct sockaddr_in sa;
    WNDCLASS wc;
    HWND wnd;

    if (prev == NULL) {
        wc.style = 0;
        wc.lpfnWndProc = wst_wndproc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = k_class;
        RegisterClass(&wc);
    }
    wnd = CreateWindow(k_class, k_class, WS_OVERLAPPEDWINDOW,
                       CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                       NULL, NULL, inst, NULL);
    if (wnd == NULL) {
        fail("STARTUP", WSAENOBUFS);
        return 0;
    }
    ShowWindow(wnd, show);
    if (SetTimer(wnd, WST_TIMER, WST_TIMERMS, NULL) == 0) {
        fail("STARTUP", WSAENOBUFS);
        DestroyWindow(wnd);
        return 0;
    }
    /* Windows 3.0's USER runs every timer from one system timer, which
       SetTimer starts before it enters the new timer in its table; a
       tick in between finds no timer and stops it, so a timer set while
       no other runs may never fire.  Setting and killing a second one
       counts this one and starts the system timer again, there and on
       Windows for Workgroups, whose USER may do the same. */
    KillTimer(wnd, SetTimer(wnd, WST_TIMER + 1, WST_TIMERMS, NULL));
    wst_sock = wst_fn.socket(AF_INET, SOCK_STREAM, 0);
    if (wst_sock == INVALID_SOCKET) {
        fail("SOCKET", 0);
        DestroyWindow(wnd);
        return 0;
    }
    sa.sin_family = AF_INET;
    sa.sin_port = port;
    sa.sin_addr.s_addr = addr;
    memset(sa.sin_zero, 0, sizeof sa.sin_zero);
    if (wst_fn.async_select(wst_sock, wnd, WST_WM_SOCKET, WST_EVENTS) != 0) {
        fail("SOCKET", 0);
        DestroyWindow(wnd);
        return 0;
    }
    if (wst_fn.connect(wst_sock, (const struct sockaddr FAR *)&sa,
                       sizeof sa) == 0 ||
        wst_fn.last_error() == WSAEWOULDBLOCK)
        return 1;
    fail("CONNECT", 0);
    DestroyWindow(wnd);
    return 0;
}

/* Close what ws_open and start opened, then write the report. */
static void finish(void)
{
    HFILE f;

    if (wst_sock != INVALID_SOCKET)
        wst_fn.close(wst_sock);
    if (wst_started)
        wst_fn.cleanup();
    if (wst_lib != NULL)
        FreeLibrary(wst_lib);
    if (wst_result == WR_PASS)
        add("RESULT PASS");
    else if (wst_result == WR_NOWINSOCK)
        add("RESULT NOWINSOCK");
    add(wst_result == WR_FAIL ? "EXIT 1" : "EXIT 0");
    f = _lcreat(k_outname, 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, wst_report, wst_len);
        _lclose(f);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    u_long addr;
    u_short port;
    u32 flags;
    MSG m;

    flags = GetWinFlags();
    add(!(flags & WF_PMODE) ? "MODE REAL" :
        (flags & WF_ENHANCED) ? "MODE ENHANCED" : "MODE STANDARD");
    if (ws_open()) {
        if (!parse_args(cmd, &addr, &port))
            fail("USAGE", WSAEINVAL);
        else if (start(inst, prev, show, addr, port))
            while (GetMessage(&m, NULL, 0, 0)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
    }
    finish();
    ExitWindows(0L, 0);
    return 0;
}
