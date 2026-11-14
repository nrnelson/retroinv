/*
 * CWIN.H - windows.h for the Windows sources, and WCALLBACK, the
 * declaration of every function Windows calls.
 *
 * The Windows sources include it, and so do the modules of SRC/COMPAT/
 * with COMPAT_WIN16 variants, after COMPAT.H.
 *
 * A function Windows calls must load DS from the AX that Windows or a
 * MakeProcInstance thunk sets, and the loader makes its entry code do so
 * only for an exported entry.  The Windows build (MAKEFILE, -zW) gives
 * that entry code to every function declared __export, and wlink exports
 * each of them, so a window, dialog, timer or other callback procedure is
 * declared WCALLBACK.  The Windows sources may not write the keyword
 * themselves, and lint.py --win lets PASCAL stand only before WinMain and
 * in a pointer to a FAR PASCAL function, so no callback is declared FAR
 * PASCAL without the export; one declared neither and cast to FARPROC
 * passes lint and the compiler, and is left to review.  WINVER of
 * MAKEFILE keeps windows.h to the calls of Windows 3.0.
 */
#ifndef CWIN_H
#define CWIN_H

#include <windows.h>

#define WCALLBACK       FAR PASCAL __export

#endif
