/*
 * TCPMAIN.C - the main of INVTCP.EXE, which runs invtcp_main
 * (INVTCP.H); the tests link INVTCP.C with a main of their own.
 */
#include "COMPAT.H"
#include "INVTCP.H"

int main(int argc, char **argv)
{
    return invtcp_main(argc, argv);
}
