/*
 * WROWSTR.H - the numbers of the column headers of INVWIN's rows
 * (WROWS.H), of the names wrow_intl_read reads WIN.INI by and of the
 * item form's tabs and buttons, which WROWS.RC gives to the resources
 * of each program linking WROWS.C.
 *
 * The resource compiler reads it too, so it holds #define lines only.
 */
#ifndef WROWSTR_H
#define WROWSTR_H

/* The headers' words, each ended by WCOL_SEP of WCOLS.H but the last:
   the items', the items' in the serial number order, the parts', the
   location tree's, the service records' of every item and of one, the
   loans' of every item and of one, the users', the audit log's and the
   categories', numbered after the item form's strings. */
#define WROW_S_ITEM     1072
#define WROW_S_SER      1073
#define WROW_S_PART     1074
#define WROW_S_TREE     1075
#define WROW_S_SVC      1076
#define WROW_S_SVCI     1077
#define WROW_S_LOAN     1078
#define WROW_S_LOANI    1079
#define WROW_S_USER     1080
#define WROW_S_AUDIT    1081

/* WIN.INI's section of the International settings and its keys
   (wrow_intl_read), each string key followed by its value for a WIN.INI
   that has none, the default Windows 3.0 documents. */
#define WROW_S_INTL     1082
#define WROW_S_DATE     1083    /* sShortDate                           */
#define WROW_S_DATED    1084
#define WROW_S_DEC      1085    /* sDecimal                             */
#define WROW_S_DECD     1086
#define WROW_S_THOU     1087    /* sThousand                            */
#define WROW_S_THOUD    1088
#define WROW_S_LZERO    1089    /* iLZero, 0 when WIN.INI has none      */

/* The item form's pages (wrow_item_pages): its tabs, each ended by
   WT_SEP of WTAB.H but the last, and its record buttons' texts, one
   string each from WROW_S_IACT on, WROW_IACTS of them. */
#define WROW_S_IPAGES   1090
#define WROW_S_IACT     1091

/* The categories' column headings, the string after those buttons'
   texts (wrow_fits of WROWS.C). */
#define WROW_S_CODE     1095

#endif
