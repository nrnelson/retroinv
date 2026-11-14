/*
 * CWSOCK.C - the TCP connection of a Windows program through Windows
 * Sockets 1.1 (COMPAT.H, TCP connections).
 *
 * The program has one connection at a time, sk_conn.  sk_open loads
 * WINSOCK.DLL by its path, which OpenFile finds, finds each entry by
 * name (GetProcAddress) and starts it (WSAStartup), then makes the
 * connection's hidden window and its socket, which WSAAsyncSelect makes
 * non-blocking and whose events it posts to the window as SK_WM.  The
 * window's procedure adds each event to sk_conn.ev, and the error an
 * FD_CONNECT carries to sk_conn.err; the waits take an event they need
 * out of ev before the call that makes Winsock post it again, so none
 * is missed.  Each wait idles (dos_idle) between its looks at ev, which
 * only the program's dos_yield, dispatching the messages, changes.
 * sk_close undoes whatever sk_open did, in the reverse order, also
 * after a failed sk_open.
 *
 * A connection the peer has closed may still hold bytes to read: its
 * FD_CLOSE ends a wait, but sk_recv reads on until Winsock has none.
 *
 * The Windows programs run in protected mode only (DOC/DESIGN.md), as
 * Winsock does, so sk_open need not ask for the mode.
 */
#define COMPAT_SOCKET
#include "COMPAT.H"
#include <string.h>
#include "CWIN.H"
#ifdef COMPAT_WC95
/* Watcom C 9.5 (1993) has no winsock.h: the part of Windows Sockets 1.1
   that this layer uses, as the specification lays it out (in_addr reduced
   to its 32-bit address). */
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef u_int SOCKET;
struct in_addr { unsigned long s_addr; };
struct sockaddr { u_short sa_family; char sa_data[14]; };
struct sockaddr_in {
    short sin_family;
    u_short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
#define WSADESCRIPTION_LEN      256
#define WSASYS_STATUS_LEN       128
typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
    u_short iMaxSockets;
    u_short iMaxUdpDg;
    char FAR *lpVendorInfo;
} WSADATA, FAR *LPWSADATA;
#define INVALID_SOCKET  ((SOCKET)~0)
#define SOCKET_ERROR    (-1)
#define AF_INET         2
#define SOCK_STREAM     1
#define FD_READ         0x01
#define FD_WRITE        0x02
#define FD_CONNECT      0x10
#define FD_CLOSE        0x20
#define WSAGETSELECTEVENT(lp)   LOWORD(lp)
#define WSAGETSELECTERROR(lp)   HIWORD(lp)
#define WSAEWOULDBLOCK  10035
#define WSAECONNREFUSED 10061
#else
#include <winsock.h>
#endif

#define SK_VERSION      0x0101  /* the Winsock asked for, 1.1           */
#define SK_WM           (WM_USER + 1)   /* the window's socket message  */
#define SK_EVENTS       (FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE)
#define SK_ESCDOWN      0x8001  /* GetAsyncKeyState: down now, or pressed
                                   since its last call                  */

static const char k_dll[] = "WINSOCK.DLL";
static const char k_class[] = "RISOCKET";

/* The entries of WINSOCK.DLL the layer calls. */
typedef int (FAR PASCAL *ws_startup_fn)(WORD, LPWSADATA);
typedef int (FAR PASCAL *ws_void_fn)(void);
typedef int (FAR PASCAL *ws_select_fn)(SOCKET, HWND, u_int, long);
typedef SOCKET (FAR PASCAL *ws_socket_fn)(int, int, int);
typedef int (FAR PASCAL *ws_connect_fn)(SOCKET, const struct sockaddr FAR *,
                                        int);
typedef int (FAR PASCAL *ws_send_fn)(SOCKET, const char FAR *, int, int);
typedef int (FAR PASCAL *ws_recv_fn)(SOCKET, char FAR *, int, int);
typedef int (FAR PASCAL *ws_close_fn)(SOCKET);

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
} sk_fn;

struct sk {
    SOCKET sock;                /* INVALID_SOCKET while there is none   */
    HWND wnd;                   /* its window, NULL while there is none */
    u16  ev;                    /* the FD_ events posted, not yet taken */
    u16  err;                   /* the error of the last FD_CONNECT     */
};

static HINSTANCE sk_lib;        /* WINSOCK.DLL, NULL while not loaded   */
static u8 sk_started;           /* 1 once WSAStartup succeeded          */
static u8 sk_missing;           /* an entry GetProcAddress lacked       */
static sk sk_conn = { INVALID_SOCKET, NULL, 0, 0 };

/* The connection's window procedure: notes the events of its socket. */
LRESULT WCALLBACK sk_wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg != SK_WM)
        return DefWindowProc(w, msg, wp, lp);
    if ((SOCKET)wp == sk_conn.sock) {
        sk_conn.ev |= WSAGETSELECTEVENT(lp);
        if (WSAGETSELECTEVENT(lp) == FD_CONNECT)
            sk_conn.err = WSAGETSELECTERROR(lp);
    }
    return 0;
}

/* GetProcAddress of name in WINSOCK.DLL, noting one that is missing. */
static FARPROC entry(const char *name)
{
    FARPROC p;

    p = GetProcAddress(sk_lib, name);
    if (p == NULL)
        sk_missing = 1;
    return p;
}

/* Load WINSOCK.DLL, find its entries and start it: SK_OK, or
   SK_ENOSTACK with *err WSAStartup's error. */
static int load(u16 *err)
{
    OFSTRUCT of;
    WSADATA wd;

    if (OpenFile(k_dll, &of, OF_EXIST) == HFILE_ERROR)
        return SK_ENOSTACK;
    sk_lib = LoadLibrary(of.szPathName);
    if ((UINT)sk_lib < (UINT)HINSTANCE_ERROR) {
        sk_lib = NULL;
        return SK_ENOSTACK;
    }
    sk_missing = 0;
    sk_fn.startup = (ws_startup_fn)entry("WSAStartup");
    sk_fn.cleanup = (ws_void_fn)entry("WSACleanup");
    sk_fn.last_error = (ws_void_fn)entry("WSAGetLastError");
    sk_fn.async_select = (ws_select_fn)entry("WSAAsyncSelect");
    sk_fn.socket = (ws_socket_fn)entry("socket");
    sk_fn.connect = (ws_connect_fn)entry("connect");
    sk_fn.send = (ws_send_fn)entry("send");
    sk_fn.recv = (ws_recv_fn)entry("recv");
    sk_fn.close = (ws_close_fn)entry("closesocket");
    if (sk_missing)
        return SK_ENOSTACK;
    *err = (u16)sk_fn.startup(SK_VERSION, &wd);
    if (*err != 0)
        return SK_ENOSTACK;
    sk_started = 1;
    return SK_OK;
}

/* Make k's window and its socket, whose events go to the window:
   SK_OK, or SK_ENOMEM with *err Winsock's error. */
static int make(sk *k, u16 *err)
{
    WNDCLASS wc;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = sk_wndproc;
    wc.hInstance = (HINSTANCE)dos_inst;
    wc.lpszClassName = k_class;
    /* A class made by an earlier connection stays registered. */
    RegisterClass(&wc);
    k->wnd = CreateWindow(k_class, "", 0, 0, 0, 0, 0, NULL, NULL,
                          (HINSTANCE)dos_inst, NULL);
    if (k->wnd == NULL)
        return SK_ENOMEM;
    k->ev = 0;
    k->err = 0;
    k->sock = sk_fn.socket(AF_INET, SOCK_STREAM, 0);
    if (k->sock == INVALID_SOCKET ||
        sk_fn.async_select(k->sock, k->wnd, SK_WM, SK_EVENTS) != 0) {
        *err = (u16)sk_fn.last_error();
        return SK_ENOMEM;
    }
    return SK_OK;
}

/* 1 when Esc is down, or was pressed since the last call, while a window
   of the program is active; else 0. */
static int esc_pressed(void)
{
    HWND a;

    a = GetActiveWindow();
    return (GetAsyncKeyState(VK_ESCAPE) & SK_ESCDOWN) != 0 && a != NULL &&
           GetWindowTask(a) == GetCurrentTask();
}

/* Idle until an event of mask has come for k, at most secs seconds,
   and, with esc 1, until Esc is pressed (esc_pressed): SK_OK, SK_ECONN
   when the time ran out, or SK_ECANCEL. */
static int wait_for(sk *k, u16 mask, u16 secs, int esc)
{
    u32 start;

    start = tmr_ticks();
    while (!(k->ev & mask)) {
        if (esc && esc_pressed())
            return SK_ECANCEL;
        if (tmr_since(start) >= (u32)secs * TMR_10SEC / 10)
            return SK_ECONN;
        dos_idle();
    }
    return SK_OK;
}

/* The SK_ code of a connection that failed with Winsock's error e,
   which goes into *err as well. */
static int failed(u16 e, u16 *err)
{
    *err = e;
    return e == WSAECONNREFUSED ? SK_EREFUSED : SK_ECONN;
}

/* Connect k to port of ip, waiting as sk_open does: an SK_ code. */
static int connect_to(sk *k, const u8 *ip, u16 port, u16 secs, u16 *err)
{
    struct sockaddr_in sa;
    u16 e;
    int rc;

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    /* Network order: the high byte first. */
    sa.sin_port = (u_short)((port & 0xFF) << 8 | port >> 8);
    memcpy(&sa.sin_addr, ip, sizeof sa.sin_addr);
    /* A press of Esc before the wait does not end it. */
    esc_pressed();
    if (sk_fn.connect(k->sock, (const struct sockaddr FAR *)&sa,
                      sizeof sa) == 0)
        return SK_OK;
    e = (u16)sk_fn.last_error();
    if (e != WSAEWOULDBLOCK)
        return failed(e, err);
    rc = wait_for(k, FD_CONNECT, secs, 1);
    if (rc == SK_OK && k->err != 0)
        rc = failed(k->err, err);
    return rc;
}

int sk_open(sk **out, const u8 *ip, u16 port, u16 secs, u16 *err)
{
    int rc;

    *out = NULL;
    *err = 0;
    rc = load(err);
    if (rc == SK_OK)
        rc = make(&sk_conn, err);
    if (rc == SK_OK)
        rc = connect_to(&sk_conn, ip, port, secs, err);
    if (rc != SK_OK) {
        sk_close(&sk_conn);
        return rc;
    }
    *out = &sk_conn;
    return SK_OK;
}

int sk_send(sk *k, const char *buf, u16 n, u16 secs)
{
    u16 off;
    int m;

    for (off = 0; off < n; off = (u16)(off + m)) {
        k->ev &= ~FD_WRITE;
        m = sk_fn.send(k->sock, buf + off, (int)(n - off), 0);
        if (m != SOCKET_ERROR)
            continue;
        if (sk_fn.last_error() != WSAEWOULDBLOCK || (k->ev & FD_CLOSE) ||
            wait_for(k, FD_WRITE | FD_CLOSE, secs, 0) != SK_OK)
            return SK_ELOST;
        m = 0;
    }
    return SK_OK;
}

u16 sk_recv(sk *k, char *buf, u16 room, u16 secs)
{
    int n;

    for (;;) {
        k->ev &= ~FD_READ;
        n = sk_fn.recv(k->sock, buf, (int)room, 0);
        if (n != SOCKET_ERROR)
            return (u16)n;
        if (sk_fn.last_error() != WSAEWOULDBLOCK || (k->ev & FD_CLOSE) ||
            wait_for(k, FD_READ | FD_CLOSE, secs, 0) != SK_OK)
            return 0;
    }
}

void sk_close(sk *k)
{
    if (k == NULL)
        return;
    if (k->sock != INVALID_SOCKET)
        sk_fn.close(k->sock);
    k->sock = INVALID_SOCKET;
    if (k->wnd != NULL)
        DestroyWindow(k->wnd);
    k->wnd = NULL;
    if (sk_started)
        sk_fn.cleanup();
    sk_started = 0;
    if (sk_lib != NULL)
        FreeLibrary(sk_lib);
    sk_lib = NULL;
}
