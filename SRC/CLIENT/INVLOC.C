/*
 * INVLOC.C - INV's connections (INVCONN.H): every one of ARGS.H, the
 * LOCAL backend's data directory among them.
 */
#include "COMPAT.H"
#include "INVCONN.H"

const char inv_usage_head[] =
    "usage: INV [/LOCAL [dir] | /NET name | /COMn] [/BIOS] [/CFG:file]\n"
    "       INV /CMD [/LOCAL [dir] | /NET name | /COMn [/CFG:file]]\n"
    "  /LOCAL  use the data directory dir (default ";
const char inv_usage_dir[] = ") without a server\n";
const char inv_tag[] = "INV: ";
const u8 inv_lacks = 0;
