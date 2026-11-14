# free310.sh - the CONFIG.SYS of the MS-DOS 3.10 test boots, the VDISK RAM
# disk that leaves a program a chosen amount of free conventional memory
# on the Compaq MS-DOS 3.10 of TEST/INTEG/boot310.sh under QEMU (or the
# LOADFIX block that does so in DOSBox-X's own DOS), and the check that it
# did.  Sourced by TEST/INTEG/boot310m.sh, TEST/T_SCR.SH, TEST/T_INV.SH
# and TEST/T_PRN.SH, and by TEST/INTEG/srvgate.sh for free310_check
# alone; not run on its own.
#
# Booted with the CONFIG.SYS of free310_config, "FILES=45" and
# "DEVICE=VDISK.SYS n 512 128", a program run straight from one of the
# boot's command lines (a unit test) has FREE310_TOP - 1024 * n bytes of
# free conventional memory, and one that a batch file starts, the batch
# file being run by "A:\COMMAND /C" from the boot's command lines (INV in
# T_SCR.BAT and T_INV.BAT), FREE310_BASE - 1024 * n, as CHKDSK reports it
# when run in that same place; the difference is the second COMMAND.COM.
# The figures were found with CHKDSK at RAM disks of several sizes; every
# boot that asks for an amount checks it again, so a change of the boot
# set-up shows as a failed check rather than as a wrong amount.  CHKDSK's
# figure stands for the program's own within the kilobyte the check
# allows: the two differ only by the program's name and command line in
# its environment and PSP, a few bytes.  DOSBox-X, booting the same floppy
# for TEST/T_INV.SH -d dosbox, leaves 160 bytes less, which the check
# still accepts for every KB, as FREE310_BASE lies 576 bytes above a whole
# kilobyte.
FREE310_TOP=614096
FREE310_BASE=610880
# DOSBox-X's own DOS (memsize=1, ver=3.10), as TEST/T_SCR.SH -d cga and
# -d hercules run it, leaves a program FREEDBX_BASE bytes, less the block
# of a "LOADFIX -n" (n KB) run before it; the autoexec batch file chains
# to RUNS.BAT, which starts each program with "COMMAND /C".
FREEDBX_BASE=619008
# The smallest RAM disk the runs' data directories are given: the
# T_SCRE_DOSRAMKB of SRCS.MK (and T_SCRR_DOSRAMKB, its equal), which holds
# the largest T_SCRE script's data and snapshots.  It is the disk of the
# boots given no -m KB, so it must leave a batch-run program the ceiling
# of every build's need, DOS_MAXFREEKB of MAKEFILE, which make lint checks.
FREE310_MINKB=160

# free310_config FILE [KB]: write FILE, with CR LF line ends, as the
# CONFIG.SYS of an MS-DOS 3.10 test boot: "FILES=45" and, with KB, a
# VDISK.SYS RAM disk of KB kilobytes with 512-byte sectors and 128 root
# directory entries.
free310_config() {
    {
        printf 'FILES=45\r\n'
        [ $# -lt 2 ] || printf 'DEVICE=VDISK.SYS %s 512 128\r\n' "$2"
    } >"$1"
}

# free310_vdisk KB [TOP [MIN]]: the size in KB of the RAM disk (or
# LOADFIX block) that leaves at least KB kilobytes free and less than one
# kilobyte more, where the free memory is TOP (default FREE310_BASE)
# less the disk; fails when that size would be below MIN KB (default
# FREE310_MINKB, the RAM disk's least; 1 for a LOADFIX block).
free310_vdisk() {
    f310_n=$(( (${2:-$FREE310_BASE} - $1 * 1024) / 1024 ))
    [ "$f310_n" -ge "${3:-$FREE310_MINKB}" ] || return 1
    echo "$f310_n"
}

# free310_bytes FILE: the first free memory figure of CHKDSK's output in
# FILE, in bytes, or nothing when FILE holds none.
free310_bytes() {
    tr -d '\r' 2>/dev/null <"$1" |
        sed -n 's/^ *\([0-9][0-9]*\) bytes free$/\1/p' | head -n 1
}

# free310_check FILE KB: success when FILE, holding CHKDSK's output
# (free310_bytes), reports at least KB kilobytes free and less than one
# kilobyte more; otherwise the reason on standard output and failure.
free310_check() {
    f310_got=$(free310_bytes "$1")
    if [ -z "$f310_got" ]; then
        echo "no free memory figure from CHKDSK"
        return 1
    fi
    if [ "$f310_got" -lt $(($2 * 1024)) ] || [ "$f310_got" -ge $(($2 * 1024 + 1024)) ]; then
        echo "CHKDSK reports $f310_got bytes free, not $2 KB (see TEST/INTEG/free310.sh)"
        return 1
    fi
    return 0
}
