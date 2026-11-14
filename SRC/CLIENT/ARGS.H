/*
 * ARGS.H - the connection switches of the client programs (ARGS.C).
 *
 *   [/LOCAL [dir] | /NET name | /COM1 | /COM2 | /TCP:addr[:port] |
 *    /MBOX:dir] [/CFG:file]
 *
 *   /LOCAL   the engine inside the program, on the data directory dir;
 *            the connection when none is named
 *   /NET     a NetBIOS session to the server called name
 *   /COM1, /COM2  a session over that serial port
 *   /TCP:    a TCP connection to the bridge INVTCP at the IPv4 address
 *            addr, four decimal numbers 0..255 joined by dots, and the
 *            decimal port 1..65535, or the program's default without one
 *   /MBOX:   the server's mailbox in the directory dir, the text after
 *            the colon
 *   /CFG:    the settings file, the text after the colon
 *
 * A switch is a word that begins with '/' or '-'; the name after that
 * character is matched whole (/CFG:, /TCP: and /MBOX: up to the colon)
 * and without regard to case.  A bare '/' or '-' is a switch with an
 * empty name: never a dir or a name, and given to the caller's function
 * as "", as a word that is no switch is.  The word after /LOCAL is its
 * dir, and the word after /NET its name, when that word exists and is
 * no switch.
 *
 * Each program reads its own switches as well (a test switch, INV's
 * /CMD): args_parse hands every word it does not take to the caller's
 * function, so the program's switches and these may come in any order.
 * Every word is read, also after an error, so that the function sees
 * the whole line (a test switch is honoured on a line that is refused),
 * and the first error in the order of the words is the one reported.
 * The function, and the program's own rules between its switches and
 * these, are the caller's; the rules here are those every client keeps:
 * no switch given twice, at most one connection, and a /NET name that
 * txt_nbname_ok (COMPAT.H) takes.  Nothing is printed: the result names
 * the error and the word, and the program says it in its own words.
 *
 * Only the Windows programs, which have the TCP and MBOX links, read
 * /TCP: and /MBOX:.  They link ARGSW.C, which is ARGS.C built with
 * ARGS_WINSW defined, and define ARGS_WINSW before they include this
 * header, which then gives ARGS_TCP, ARGS_MBOX and the fields of the
 * two.  For any other program /TCP: and /MBOX: are words like any
 * other, handed to its function, so that INV, which links ARGS.C,
 * carries none of them.
 */
#ifndef ARGS_H
#define ARGS_H

#include "COMPAT.H"

/* The connection (args.kind), distinct bits. */
#define ARGS_LOCAL      0x01    /* /LOCAL, or no connection named       */
#define ARGS_NET        0x02    /* /NET                                 */
#define ARGS_COM        0x04    /* /COM1 or /COM2                       */
#ifdef ARGS_WINSW
#define ARGS_TCP        0x08    /* /TCP:                                */
#define ARGS_MBOX       0x10    /* /MBOX:                               */
#endif

/* Results of args_parse, and the word of argv, with its '/' or '-',
   that *word then points to (the element of argv itself). */
#define ARGS_OK         0       /* none: *word is NULL                  */
#define ARGS_EBAD       1       /* the word: a second /LOCAL, /NET, /COMn
                                   (/COM2 after /COM1 too), /TCP:,
                                   /MBOX: or /CFG:, a /CFG: or /MBOX:
                                   with nothing after the colon, a /TCP:
                                   whose address and port are not as
                                   above, or a word the caller's
                                   function refused                     */
#define ARGS_ENONAME    2       /* the /NET switch: no name follows it  */
#define ARGS_ECLASH     3       /* the first switch naming a connection
                                   after another connection was named   */
#define ARGS_EBADNAME   4       /* /NET's name, which txt_nbname_ok
                                   refuses                              */
#define ARGS_EOWN       5       /* the first code free for the caller's
                                   own (args_fn)                        */

#define ARGS_COMSZ      5       /* bytes of a port's name, NUL included */

/* The switches read; dir, net, cfg, tcp and mbox point into the words
   of argv. */
typedef struct {
    const char *dir;            /* /LOCAL's dir (an empty word gives
                                   ""), else NULL                       */
    const char *net;            /* /NET's name, else NULL               */
    const char *cfg;            /* /CFG:'s file, never "", else NULL    */
    char comname[ARGS_COMSZ];   /* "COM1" or "COM2" for /COMn, upper
                                   case whatever was typed, else ""     */
    u8   com;                   /* /COMn's port n, 1 or 2, else 0       */
    u8   kind;                  /* ARGS_LOCAL, ARGS_NET, ARGS_COM,
                                   ARGS_TCP or ARGS_MBOX                */
#ifdef ARGS_WINSW
    const char *tcp;            /* /TCP:'s text after the colon, else
                                   NULL                                 */
    u8   ip[4];                 /* /TCP:'s address, ip[0] first         */
    u16  port;                  /* /TCP:'s port, 0 when none is given   */
    const char *mbox;           /* /MBOX:'s dir, never "", else NULL    */
#endif
} args;

/* Receives a word args_parse does not take, with args_parse's arg: s is
   the word without its '/' or '-', a pointer into it that lives as long
   as argv, or "" for a word that is no switch, so that it matches no
   switch name.  Returns ARGS_OK when the word is taken (or skipped),
   else the error it finds in the word: ARGS_EBAD for a word refused, or
   a code of the caller's own from ARGS_EOWN up.  The function is called
   for every such word, also after an error, in the order of the words;
   its error counts as one of args_parse's at that word. */
typedef int (*args_fn)(void *arg, const char *s);

/* Read the words argv[1] .. argv[argc - 1] (argv[0], the program, is
   not read; argc 0 or 1 reads none) into *a, in order.  /LOCAL, /NET
   and /COMn with the words they take, /TCP:, /MBOX: and /CFG: are taken
   here; every other word goes to fn with arg, or with fn NULL is
   skipped.  A repeated /LOCAL or /NET still takes the word after it, as
   above.  Every word is read; the result is the first error in the
   order of the words, ARGS_EBAD, ARGS_ENONAME or fn's, and only when no
   word gave one ARGS_ECLASH, else ARGS_EBADNAME, else ARGS_OK.  *word
   is set as the comment of the result says.

   On ARGS_OK, *a holds the switches given: kind is the one connection
   named, ARGS_LOCAL when none is; net is set exactly for ARGS_NET, com
   and comname exactly for ARGS_COM, tcp exactly for ARGS_TCP, ip and
   port only for it, mbox exactly for ARGS_MBOX; dir only for an
   ARGS_LOCAL named with a dir.  After any other result *a is partly
   filled and not to be used. */
int args_parse(args *a, int argc, char **argv, args_fn fn, void *arg,
               const char **word);

#endif
