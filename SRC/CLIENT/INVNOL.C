/*
 * INVNOL.C - the connections of the network-only client (INVCONN.H): INV
 * built without the engine (THIN of SRCS.MK, which links LINKNOL.C in
 * place of LINKLOC.C) has /NET and /COMn alone.  It lacks LOCAL, the
 * connection also when none is named, so a server must be named.
 *
 * The usage lines and inv_tag name the program as THIN of SRCS.MK does;
 * TEST/T_INV.SH checks that they keep doing so.
 */
#include "COMPAT.H"
#include "ARGS.H"
#include "INVCONN.H"

const char inv_usage_head[] =
    "usage: INVTHIN {/NET name | /COMn} [/BIOS] [/CFG:file]\n"
    "       INVTHIN /CMD {/NET name | /COMn [/CFG:file]}\n"
    "  This program has no /LOCAL: name the server with /NET or /COMn.\n";
const char inv_usage_dir[] = "";
const char inv_tag[] = "INVTHIN: ";
const u8 inv_lacks = ARGS_LOCAL;
