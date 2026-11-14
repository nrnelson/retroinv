/*
 * CLABEL.C - the volume label and the cluster size of a disk
 * (dos_volume_label and dos_disk_cluster of COMPAT.H).
 *
 * A file of its own, so that only the programs that read them, the
 * installer and its tests, link it: Watcom C 6.5 links every function
 * of a module it links.  The label search is CDOS.C's dos_find_entry;
 * the host build gives what a test sets.
 */
#define COMPAT_INTERNAL
#define COMPAT_FIND
#define COMPAT_LABEL
#include "COMPAT.H"

#ifdef COMPAT_HOST
#include <string.h>

const char *dos_label_host = NULL;
u32 dos_cluster_host = 512;

int dos_volume_label(u8 drive, char *label)
{
    (void)drive;
    label[0] = '\0';
    if (dos_label_host != NULL) {
        strncpy(label, dos_label_host, DOS_LABELLEN);
        label[DOS_LABELLEN] = '\0';
    }
    return 0;
}

int dos_disk_cluster(u8 drive, u32 *bytes)
{
    (void)drive;
    *bytes = dos_cluster_host;
    return 0;
}

#else
#include <string.h>
#include <dos.h>

/* The search pattern "X:\*.*" is built in place, so that no program
   pays for it in DGROUP.  The label is the one entry a search with
   CL_ATTR gives, but for an entry with another attribute, which a DOS
   that also gives files would; the '.' DOS puts after the label's
   eighth character is the label's own padding taken out, put back as
   spaces up to that eighth character. */
#define CL_ATTR         0x08    /* volume-label attribute               */
#define CL_BASE         8       /* characters before DOS's '.'          */

int dos_volume_label(u8 drive, char *label)
{
    u8 dta[DOS_DTASIZE];
    char pat[7];
    const u8 *s;
    int n;
    int rc;

    label[0] = '\0';
    n = 0;
    if (drive != 0) {
        pat[n++] = (char)('A' + drive - 1);
        pat[n++] = ':';
    }
    pat[n++] = '\\';
    pat[n++] = '*';
    pat[n++] = '.';
    pat[n++] = '*';
    pat[n] = '\0';
    dos_disk_reset();
    rc = dos_find_entry(pat, CL_ATTR, dta);
    if (rc != 0)
        return rc == CF_ENOENT ? 0 : rc;
    if ((dta[DOS_DTAATTR] & CL_ATTR) == 0)
        return 0;
    n = 0;
    for (s = dta + DOS_DTANAME; *s != '\0' && n < DOS_LABELLEN; s++) {
        if (*s == '.' && n <= CL_BASE) {
            while (n < CL_BASE)
                label[n++] = ' ';
        } else {
            label[n++] = (char)*s;
        }
    }
    while (n > 0 && label[n - 1] == ' ')
        n--;
    label[n] = '\0';
    return 0;
}

/* Function 36h (DOS 2.0): DL the drive, AX sectors per cluster (FFFFh
   for a drive that does not exist), CX bytes per sector. */
int dos_disk_cluster(u8 drive, u32 *bytes)
{
    union REGS r;

    memset(&r, 0, sizeof r);
    r.x.ax = 0x3600;
    r.h.dl = drive;
    intdos(&r, &r);
    *bytes = 0;
    if ((u16)r.x.ax == 0xFFFFu)
        return CF_EINVAL;
    *bytes = (u32)(u16)r.x.ax * (u32)(u16)r.x.cx;
    return 0;
}

#endif
