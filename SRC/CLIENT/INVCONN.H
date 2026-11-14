/*
 * INVCONN.H - the connections of the client of INV.C, which differ by
 * program: INVLOC.C gives INV's, the LOCAL backend among them, and
 * INVNOL.C those of the network-only client (THIN of SRCS.MK), which is
 * built without the engine.  INV.C is one object in both programs; this
 * data is all it takes of the difference, so that INV carries no code
 * for the other program.
 */
#ifndef INVCONN_H
#define INVCONN_H

#include "COMPAT.H"

/* The first usage lines: the synopsis, then those of the connections
   that the lines every program shares do not give.  When inv_usage_dir
   is not "", inv_usage_head ends inside the line of /LOCAL, which
   LOCAL's default data directory and inv_usage_dir complete. */
extern const char inv_usage_head[];
extern const char inv_usage_dir[];

/* The program's name and ": ", which begin every message it writes on
   standard error. */
extern const char inv_tag[];

/* The connections, ARGS_ kinds (ARGS.H), the program lacks: naming one
   of them, or none when it lacks ARGS_LOCAL, is a usage error. */
extern const u8 inv_lacks;

#endif
