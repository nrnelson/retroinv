/*
 * ENGTX.H - the numbers of the engine's texts: the reasons it records
 * with a failure, the why texts of an open and the standard texts of
 * the protocol's error codes.
 *
 * ENG_TX_name is the position of the row TX(name, text) in TXTAB.H,
 * counted from 1.  A number names one text for good, so a client may
 * map it to a text of its own language.  The texts lie outside DGROUP
 * with the schema's far data (SRC/COMPAT/CSCHDAT.C), and eng_tx_copy
 * copies one out only when it is shown, so a failure costs no near
 * data.
 *
 * Only the files that name a text include this header, after ENGINE.H.
 * A file that defines ENGTX_TXTONLY first reads the ENG_TXT_ texts
 * alone (Watcom C 6.5 runs out of memory on LINK.C otherwise).
 */
#ifndef ENGTX_H
#define ENGTX_H

/* The text of row LONGLINE, the standard text of ENG_ETOOLONG, by itself:
   the client's link answers an over-long request line with it, without
   the engine's texts (SRC/CLIENT/LINK.C). */
#define ENG_TXT_LONGLINE "line too long"

#ifndef ENGTX_TXTONLY

#define TX(name, text) ENG_TX_##name,
enum eng_txnum {
    ENG_TX_NONE,                /* no text                              */
#include "TXTAB.H"
    ENG_TX_END
};
#undef TX

#define ENG_NTX         (ENG_TX_END - 1)    /* the last text number     */
#define ENG_TXMAX       45      /* longest text, without the NUL        */

/* With ENGTX_STORE defined before this header is included (CSCHDAT.C,
   which defines the texts, and CSCHEMA.C, which reads them): struct
   eng_txs, the texts packed in number order with their NULs, one char
   array m_name per row, so that no padding lies between them, and
   ENG_TXNUM, with which a text states a limit; those two files include
   the headers of the constants the texts name. */
#ifdef ENGTX_STORE
#define ENG_TXNUM(n)    ENG_TXSTR(n)    /* the digits of constant n   */
#define ENG_TXSTR(n)    #n
#define TX(name, text) char m_##name[sizeof(text)];
struct eng_txs {
#include "TXTAB.H"
};
#undef TX
#endif

/* CSCHEMA.C.  Text tx (1..ENG_NTX) into out (ENG_TXMAX + 1 bytes); ""
   for any other number, and in a Windows program when its resource
   cannot be read, so that an error may then go out without its text. */
void eng_tx_copy(u16 tx, char *out);

#endif

#endif
