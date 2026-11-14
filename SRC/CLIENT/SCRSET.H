/*
 * SCRSET.H - the texts of the settings that both clients show: INV's
 * settings screen (SCRSET.C) and the Windows client's settings dialog.
 *
 * The texts are messages of SCRSET.MSG.  The two edit different keys of
 * the settings file (CFG.H): INV what its screen and printer use, the
 * Windows client what it uses, the serial link's BAUD and RTSCTS among
 * them, so each has its own form and note of a save.
 */
#ifndef SCRSET_H
#define SCRSET_H

#include "COMPAT.H"

#define MC_TITLE        680     /* form title                           */
#define MC_DATA         681     /* field labels                         */
#define MC_STATION      682
#define MC_USER         683
#define MC_ORDER        684
#define MC_CFGFILE      689
#define MC_PROGRAM      690
#define MC_SAVEFAIL     699     /* ^1 the file, ^2 rqe_doserr's text    */
#define MC_BAUD         706     /* the Windows client's field labels    */
#define MC_RTSCTS       707
#define MC_WSAVED       708     /* its note of a save                   */

#endif
