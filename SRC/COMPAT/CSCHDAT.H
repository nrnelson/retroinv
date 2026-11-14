/*
 * CSCHDAT.H - the layout of the schema data a Windows program binds as
 * its resource CSCH_RES, read by SRC/COMPAT/CSCHEMA.C.
 *
 * The host tool SRC/COMPAT/CSCHBIN.C writes the data from the arrays of
 * SRC/COMPAT/CSCHDAT.C: a header of CSCH_NPART entries of CSCH_ENTRY
 * bytes, one per part in the order below, each the part's offset from
 * the start of the data, the size of one of its elements and the number
 * of its elements, little-endian u16 values; then the parts themselves,
 * back to back.  Part CSCH_X0 + x is the key expression of index x
 * (ENG_X_), its characters and the NUL as elements of one byte.  The
 * seed rows' tables are no part: they stay in DGROUP (CSCHDAT.C says
 * why).
 *
 * The resource compiler reads it too, so it holds #define lines only.
 */
#ifndef CSCHDAT_H
#define CSCHDAT_H

#define CSCH_RES        1       /* the resource's number                */
/* Its type, a number of our own: the resource compiler of Watcom C 9.5
   reads an RCDATA resource only from the script, not from a file. */
#define CSCH_TYPE       256

#define CSCH_FNAME      0       /* field names, DBF_NAMELEN + 1 each    */
#define CSCH_FTYPE      1       /* their dBASE types                    */
#define CSCH_FLEN       2       /* their widths                         */
#define CSCH_FDEC       3       /* their decimals                       */
#define CSCH_SCODE      4       /* seed codes, ENG_LTYPELEN + 1 each    */
#define CSCH_SNAME      5       /* seed names, ENG_CODENAMELEN + 1 each */
#define CSCH_TX         6       /* the texts, one struct eng_txs        */
#define CSCH_TXLEN      7       /* the size of each text with its NUL   */
#define CSCH_X0         8       /* the key expression of index 0        */
#define CSCH_NPART      (CSCH_X0 + ENG_NINDEX)
#define CSCH_ENTRY      6       /* bytes of a header entry              */

#endif
