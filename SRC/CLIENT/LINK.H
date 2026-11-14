/*
 * LINK.H - the client's only path to the inventory system.
 *
 * A link carries protocol request lines to the system and hands every
 * response line back to the caller.  The screens and the scripted mode
 * of INV.EXE use only this interface; they never see the engine or the
 * protocol layer.  Backends:
 *
 *   LOCAL   the engine runs inside the client on a data directory, and
 *           each request goes straight to the protocol dispatcher
 *           (LINKLOC.C); a program built without the engine links
 *           LINKNOL.C in its place and has no LOCAL backend
 *   NET     a NetBIOS session to the server (NBCLI.H)
 *   SERIAL  a serial line to a port the server serves (SERCLI.H)
 *   TCP     a TCP connection to the TCP bridge INVTCP, which relays it
 *           to the server; Windows programs only (LINKTCP.C)
 *   MBOX    the server's mailbox (MBXCLI.H), each request a request
 *           file of its own logged in with a login ticket; Windows
 *           programs only (LINKMBX.C)
 *
 * Every backend is reached through the same calls: only the link_open_
 * calls tell them apart.  The SERIAL backend's own declarations,
 * link_open_serial and its three result codes, are seen only by a file
 * that defines LINK_SERIAL before it includes this header (LINKSER.C,
 * which holds the backend, and the files that open a serial link), which
 * spares Watcom C 6.5's memory in the others; they need nothing of
 * COMPAT.H's serial section.  The TCP backend's, link_open_tcp and its
 * codes, are seen likewise only by a file that defines LINK_TCP, and the
 * MBOX backend's, link_open_mbox and its code, by one that defines
 * LINK_MBOX.
 *
 * Response lines reach the caller one at a time, as the protocol sends
 * them (DOC/PROTOCOL.md), without their CR LF: "+OK ...", "-ERR ...",
 * "+DATA", the data lines still dot-stuffed, and the "." terminator.
 * The line is NUL-terminated as well as counted; it holds no NUL, CR or
 * LF byte (TAB may occur in memo lines) and is at most LINK_MAXRESP
 * bytes.
 *
 * Request lines hold passwords (LOGIN, PASSWD, USER ADD, USER RESET).
 * The link keeps no copy of a request once the call that sent it has
 * returned, except the unfinished line link_feed holds until its LF
 * arrives, which is wiped when that line has been sent and by
 * link_close.  A caller that read the line into its own buffer clears
 * that buffer itself.  The MBOX backend keeps the login ticket its LOGIN
 * was given, never the password, and wipes it when the ticket ends and
 * at the close.
 */
#ifndef LINK_H
#define LINK_H

#include "COMPAT.H"

/* ---------------------------------------------------------------------
 * Limits.
 * --------------------------------------------------------------------- */

/* Request payload limit and response line limit, CR LF excluded; equal
   to the protocol's own limits (LINK.C checks this at compile time). */
#define LINK_MAXLINE    292
#define LINK_MAXRESP    512

/* Limits of the engine and protocol behind the link, mirrored here so
   client code needs no engine or protocol header; LINK.C checks each
   against its source at compile time. */
#define LINK_MAXCURSOR  136     /* longest paging cursor, NUL excluded  */
#define LINK_MAXDIR     64      /* longest data directory, separator
                                   included, NUL excluded               */
#define LINK_MAXMEMO    2000    /* memo bytes as stored                 */
#define LINK_PAGEMAX    25      /* largest PAGE=                        */
#define LINK_USERLEN    8       /* user name characters                 */
#define LINK_PWMIN      4       /* shortest password                    */
#define LINK_PWMAX      16      /* longest password                     */
#define LINK_STALEN     16      /* station name characters              */
#define LINK_MAXFIND    30      /* longest ITEM FIND text               */
#define LINK_MAXFSPEC   40      /* characters of a FIELDS= value        */
#define LINK_NAMELEN    10      /* field name characters                */
#define LINK_MAXCODES   50      /* rows of a code list: categories,
                                   location types                       */
#define LINK_CATLEN     4       /* category code characters             */
#define LINK_LTYPELEN   8       /* location type code characters        */
#define LINK_CODENAMELEN 20     /* code list name characters            */
#define LINK_FULLNAMELEN 30     /* user's full name characters          */
#define LINK_IDLEN      8       /* item ID digits                       */
#define LINK_LOCLEN     10      /* location code characters             */
#define LINK_LOCNAMELEN 30      /* location name characters             */
#define LINK_LOCPATHLEN 78      /* characters of a location's PATH      */
#define LINK_MAXLOCDEPTH 8      /* levels of the location tree          */
#define LINK_TYPELEN    20      /* item type characters                 */
#define LINK_MFRLEN     30      /* item maker and model characters      */
#define LINK_SERIALLEN  30      /* item serial number characters        */
#define LINK_DESCRLEN   60      /* item description and tags characters */
#define LINK_ITAGLEN    12      /* characters of one item tag           */
#define LINK_TAGFILT    25      /* characters of a TAG= filter          */
#define LINK_CONDLEN    3       /* item condition code characters       */
#define LINK_WORKSLEN   1       /* item works state characters          */
#define LINK_NWORKS     4       /* characters of a WORKS= filter        */
#define LINK_QTYLEN     5       /* item quantity and on-loan digits     */
#define LINK_ACQSRCLEN  30      /* item source characters               */
#define LINK_PRICELEN   9       /* item price and value width           */
#define LINK_PRICEDEC   2       /* ... of which decimals                */
#define LINK_STATUSLEN  1       /* item and user status characters      */
#define LINK_GROUPLEN   8       /* user group characters                */
#define LINK_LEVELLEN   1       /* user access level digits             */
#define LINK_IMPERRS    10      /* refused lines an IMPORT answer lists */
#define LINK_MAXFAIL    3       /* failed logins that close a session   */
#define LINK_TKTLEN     32      /* hex digits of a login ticket         */

/* Seconds the NET backend waits for each message of a response, SERIAL
   and TCP for each byte and MBOX for the response file, before it gives
   the connection up as lost; TCP waits as long for its connection to be
   made. */
#define LINK_NETWAIT    60

/* ---------------------------------------------------------------------
 * Results.
 * --------------------------------------------------------------------- */

/* Results of link_send and link_feed. */
#define LINK_OK         0       /* the session goes on                  */
#define LINK_CLOSED     1       /* the session has ended (BYE, too many
                                   failed logins, or on a remote backend
                                   the server shutting down); further
                                   requests are not sent and get no
                                   response                             */
#define LINK_EOUTPUT    2       /* the line callback reported a failure;
                                   the rest of that response was dropped */
#define LINK_ELOST      3       /* remote: the connection to the
                                   server was lost; further requests are
                                   not sent and get no response         */

/* Results of the link_open_ calls of the remote backends besides 0. */
#define LINK_ENONB      4       /* no NetBIOS provider                  */
#define LINK_ENAME      5       /* the local name could not be added    */
#define LINK_ECALL      6       /* the call went unanswered or failed,
                                   or TCP's connection was not made     */
#define LINK_ENOMEM     7       /* no memory for the link               */
#define LINK_EREFUSED   8       /* the server's name refused the call:
                                   it had no LISTEN posted; on TCP,
                                   nothing listens on the port          */
#ifdef LINK_SERIAL
#define LINK_ENOPORT    9       /* no such serial port                  */
#define LINK_EBUSY      10      /* the serial port is open already      */
#define LINK_ESETUP     11      /* the port refused its settings        */
#endif
#ifdef LINK_TCP
#define LINK_ENOTCP     12      /* no TCP/IP: no Windows Sockets        */
#define LINK_ECANCEL    13      /* Esc gave the connection up           */
#define LINK_TCPPORT    7000    /* the bridge's port, BR_PORT of
                                   SRC/TCP/BRIDGE.H (T_LINKT checks it) */
#endif
#ifdef LINK_MBOX
#define LINK_EMBXARG    14      /* the mailbox's directory is too long,
                                   or the station is not a mailbox's    */
#endif

/* ---------------------------------------------------------------------
 * Types and calls.
 * --------------------------------------------------------------------- */

typedef struct link link;       /* an open link (opaque)                */

/* Receives one response line (len bytes, NUL after them).  A nonzero
   return means the line could not be used (e.g. the output file is
   full): the rest of the response is dropped and the call that sent the
   request returns LINK_EOUTPUT. */
typedef int (*link_line_fn)(void *arg, const char *line, u16 len);

/* A fixed clock for the LOCAL engine, used by tests so that the dates
   and times written to the data files are reproducible: year
   1980..2079, month 1..12, day 1..31, hour 0..23, minute and second
   0..59. */
typedef struct {
    u16 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
} link_clock;

/* Open the LOCAL backend on the data directory dir: "" is the current
   directory; otherwise dir names a directory, with or without a final
   '\', '/' or ':'.  A missing separator is added: the one dir already
   uses ('/' when it holds a '/' and no '\', '\' when it holds a '\'),
   else DOS_DIRSEP.  The directory must exist; missing data files in it
   are created.  The process handle table is enlarged first for the
   files the engine holds.  now is the fixed clock, or NULL for the DOS
   clock.  The session starts afresh: the first requests are HELLO and
   LOGIN.

   Returns 0, or a protocol error number (RQ.H's RQ_E codes) with *out
   NULL.  The usual ones: RQ_EARG, dir with its separator is longer
   than LINK_MAXDIR; RQ_EIO, the directory does not exist, a table is
   damaged or a memo file is missing beside its table; RQ_EINDEX, an
   index is damaged; RQ_EHANDLES, the file handles ran out; RQ_EINUSE,
   another program holds the files; RQ_EINTERNAL, memory ran out.  When
   why is not NULL, it then holds a NUL-terminated explanation of at
   most whysz bytes; for a directory that does not exist, "no such
   directory" and how to make it or name another.  The explanation, a
   file name and the reason, is cut to fit; LINK_WHYSZ, the engine's
   ENG_WHYSZ (LINK.C checks this at compile time), is the size that fits
   a screen line.  In a program without the engine (LINKNOL.C) it opens
   nothing and returns RQ_ENOTAVAIL. */
#define LINK_WHYSZ      80
int link_open_local(link **out, const char *dir, const link_clock *now,
                    char *why, u16 whysz);

/* Open the NET backend: a NetBIOS session to the server named server,
   called from the DOS machine name or a name of the link's own (as
   nbcli_open does with no local name).  The session starts afresh: the
   first requests are HELLO and LOGIN; the server takes the calling
   name, not HELLO's, as the station.  Returns 0, or LINK_ENONB,
   LINK_ENAME, LINK_ECALL, LINK_EREFUSED (a CALL that gave
   NB_RC_REJECTED) or LINK_ENOMEM with *out NULL; *nbrc receives the
   NetBIOS return code of the command that failed, NB_OK when none
   did. */
int link_open_net(link **out, const char *server, u8 *nbrc);

/* Open the SERIAL backend: a session to the server over serial port
   (1..UART_MAXPORT) at baud with flags (UART_F_RTSCTS or 0), as
   uart_open (COMPAT.H) takes them.  Nothing is sent: the session starts
   afresh with the first request, HELLO, whose station the server takes,
   then LOGIN.  Returns 0, or LINK_ENOPORT, LINK_EBUSY, LINK_ESETUP
   (what sercli_open refuses as SERCLI_EARG: a port out of range, a
   rate UART_BAUDOK refuses or the Windows driver lacks, another flag)
   or LINK_ENOMEM with *out NULL. */
#ifdef LINK_SERIAL
int link_open_serial(link **out, u8 port, u16 baud, u8 flags);
#endif

/* Open the TCP backend, a Windows program's: a connection to the bridge
   at port (LINK_TCPPORT when 0) of the IPv4 address ip, its four bytes
   in the order written, made within LINK_NETWAIT seconds or before Esc
   is pressed (sk_open, COMPAT.H).  The session starts afresh, as on
   NET: the first requests are HELLO and LOGIN, and the server takes the
   bridge's name, not HELLO's, as the station.  Returns 0, or
   LINK_ENOTCP, LINK_ENOMEM, LINK_EREFUSED, LINK_ECALL (also when the
   time ran out) or LINK_ECANCEL with *out NULL; *err receives the
   Winsock error behind the failure, 0 when none or on success. */
#ifdef LINK_TCP
int link_open_tcp(link **out, const u8 *ip, u16 port, u16 *err);
#endif

/* Open the MBOX backend, a Windows program's: the mailbox of the
   server's MAILBOX= in the directory dir (MBXCLI.H: at most
   MBXCLI_MAXDIR characters with a separator), whose request files are
   named for station, 1..MBXCLI_STALEN letters, digits, '-' or '_', the
   station the server takes (mbxcli_open).  No file is touched: the
   first request posts the first file.  The session starts afresh, as on
   NET: the first requests are HELLO and LOGIN.  Returns 0, or
   LINK_EMBXARG or LINK_ENOMEM with *out NULL. */
#ifdef LINK_MBOX
int link_open_mbox(link **out, const char *dir, const char *station);
#endif

/* The NetBIOS name a NET link called from, or NULL for the other
   backends. */
const char *link_netname(link *l);

/* Send one request line of len bytes (no CR LF) and deliver every line
   of its response to fn.  A line of more than LINK_MAXLINE bytes is
   answered with error 103 without being sent; an empty line, or one of
   only spaces and TABs, gets no response.  Returns LINK_OK, LINK_CLOSED
   (after the response that ended the session, or at once when it had
   ended before), LINK_EOUTPUT, or on NET LINK_ELOST (when no message of
   the response came for LINK_NETWAIT seconds or the session was lost,
   the lines already delivered staying delivered; at once when it had
   been lost before); SERIAL and TCP likewise, with no byte of the
   response for LINK_NETWAIT seconds, and MBOX with no response file
   for as long, a mailbox file that could not be written or read, or a
   response file that ends before the response.  The remote backends
   judge the end of the session as the server does: +OK to BYE; the
   LINK_MAXFAIL-th failed LOGIN since the last successful one, a failure
   being an answer -ERR 401, 403, 3xx or 500; or -ERR 501 (shutting down)
   to any request.  On MBOX, whose server session is one request file,
   the link keeps that count across the files.  When the server refuses
   the login ticket MBOX sent a request with (it expired, or its user's
   password changed or the user was disabled), BYE, PING and VER are
   sent again without one, and any other request is answered -ERR 400,
   as the server answers a command that needs a login, without being
   sent again: a request line goes to the mailbox once. */
int link_send(link *l, const char *line, u16 len, link_line_fn fn,
              void *arg);

/* Feed n bytes of a request stream (lines ended by LF, a CR before the
   LF dropped, as a transport receives them): each complete line is sent
   as by link_send and its response delivered to fn.  An over-long line
   is consumed up to its LF and answered with error 103, so the stream
   stays in step.  A line left unfinished at the end of buf is kept for
   the next call.  Stops at the first result other than LINK_OK and
   returns it; bytes after a LINK_CLOSED or LINK_ELOST are ignored. */
int link_feed(link *l, const char *buf, u16 n, link_line_fn fn, void *arg);

/* Close the link: the LOCAL backend flushes and closes every data file,
   NET hangs up unless the session has ended, SERIAL sends BYE unless it
   has ended (sercli_close) and closes the port, TCP closes the
   connection, which ends a live session as NET's hang-up does, and MBOX
   ends its ticket with BYE TICKET=END while it holds one, waiting a few
   seconds for the answer.  Returns 0, or the protocol error number of a
   flush or close failure (the link is freed either way).  l may be
   NULL. */
int link_close(link *l);

/* The bytes of l itself, *size of them: an observation point for
   tests, which check that no password is left in them once the call
   that sent it has returned. */
const void *link_bytes(link *l, u16 *size);

#endif
