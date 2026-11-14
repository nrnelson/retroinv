/*
 * WFILESTR.H - the numbers of the file name dialog's resources (WFILE.H),
 * which WFILE.RC gives to each program linking WFILE.C.
 *
 * The resource compiler reads it too, so it holds #define lines only.
 */
#ifndef WFILESTR_H
#define WFILESTR_H

#define WFILE_R_DLG     1040    /* the dialog's template                */

/* Its controls. */
#define WFILE_C_NAME    1041    /* the name, typed or chosen            */
#define WFILE_C_DIR     1042    /* the directory listed                 */
#define WFILE_C_LIST    1043    /* its files, directories and drives    */
#define WFILE_C_MSG     1044    /* the message line                     */

/* Its refusals of a name. */
#define WFILE_S_NODIR   1040    /* no such directory or drive           */
#define WFILE_S_LONG    1041    /* %d: the longest path the owner takes */

#endif
