# net330.sh - shared settings for the MS-DOS 3.30 + MS Network Client 3.0
# QEMU images.  Sourced by mkimg.sh, run2vm.sh, tcp330.sh, wga500.sh and
# the gates of this directory after they set $here to this directory, and
# $repo to the repository when they call out_check; not run on its own.
# It sources TEST/shlib.sh, whose helpers (die, is_uint, is_83, dos_text,
# on_signals, errlevel_bat, tool_out, need_tools, img_expand) those
# scripts use too.
#
# Both hard disk images (the system disk C: and the per-run payload disk D:)
# use the same fixed geometry so the partition table written on the host and
# the CHS geometry the BIOS reports to DOS always agree.  One primary
# partition of type 04h (FAT16, under 32 MB, the only FAT16 type DOS 3.30
# knows) starts at cylinder 0, head 1, sector 1.
#
# mkimg.sh records the hash of its inputs (image_inputs) in
# NET330_DIR/INPUTS.SHA; an image whose record differs from the current
# inputs is out of date (image_current).
#
# Each image directory DIR has the lock DIR.lock beside it (image_lock):
# the runs that boot the image hold it shared, the build that replaces it
# exclusive, so that no image is replaced under a run.
#
# env: NET330_DIR  directory holding the built images (default
#                  TEST/INTEG/vm/net330)

HD_CYLS=61
HD_HEADS=16
HD_SECS=63
HD_SECTORS=$((HD_CYLS * HD_HEADS * HD_SECS))
PART_START=$HD_SECS
PART_SECTORS=$((HD_SECTORS - PART_START))
PART_OFFSET=$((PART_START * 512))

NET330_DIR=${NET330_DIR:-"$here/vm/net330"}
BASE_IMG="$NET330_DIR/BASE.IMG"
PAYLOAD_TEMPLATE="$NET330_DIR/PAYLOAD.IMG"
INPUTS_SHA="$NET330_DIR/INPUTS.SHA"

# NetBEUI's LLC response timer T1 of VM A and VM B, in milliseconds
# (PROTOCOL.INI "T1=", counted in 55 ms clock ticks).  A station whose
# I-frames stay unacknowledged for T1 polls its peer, and doubles the wait
# before each further poll.  Under QEMU NetBEUI can ignore a frame that
# arrives while it is still dealing with its own transmit (the card takes
# it in).  After a loss that leaves both stations waiting from the same
# moment, equal timers make the two polls cross every time; when both are
# lost each time, both stations drop the link (the sender's SEND ends with
# NB_RC_TIMEOUT, the receiver's RECEIVE with NB_RC_ABORTED).
# Unequal timers keep the polls apart, so the first poll is answered.
# run2vm.sh gives each VM its own (T1_A, T1_B); the image carries VM A's.
# "nbgate.sh -x" provokes the loss through LOSSY.PY.
NB_T1_A=500
NB_T1_B=800

# The longest wait for an image's lock, in seconds: a build waits for the
# runs booting the image, a run for the build.
IMAGE_LOCK_WAIT=1800

# The line NET prints when a command succeeded, under Client 3.0 and the
# Workgroup Add-On alike.
net_ok='The command completed successfully.'

. "$here/../shlib.sh"

export MTOOLS_SKIP_CHECK=1
export MTOOLS_NO_VFAT=1

# hd_device ID UNIT - value of a QEMU -device option attaching drive ID as
# IDE disk UNIT (0 = C:, 1 = D:) on the primary channel with the fixed
# geometry above.
hd_device() {
    printf 'ide-hd,drive=%s,bus=ide.0,unit=%s,cyls=%s,heads=%s,secs=%s,bios-chs-trans=none\n' \
        "$1" "$2" "$HD_CYLS" "$HD_HEADS" "$HD_SECS"
}

# part_img FILE - the mtools image spec of the partition inside FILE.
part_img() {
    printf '%s@@%s\n' "$1" "$PART_OFFSET"
}

# out_check OUT MARKER... - print OUT as an absolute path when it may be
# used as a result directory that is emptied or moved: a new path, or an
# existing directory holding one of the MARKER files.  /, $HOME and the
# repository ($repo) are refused; a refusal stops with die.
out_check() {
    oc_o=$(realpath -m "$1")
    case $oc_o in
        /|"$HOME"|"$(realpath -m "$HOME")"|"$repo") die "refusing OUT=$1" ;;
    esac
    shift
    if [ -e "$oc_o" ]; then
        for oc_m in "$@"; do
            [ ! -f "$oc_o/$oc_m" ] || { printf '%s\n' "$oc_o"; return 0; }
        done
        die "OUT=$oc_o exists and holds none of: $*"
    fi
    printf '%s\n' "$oc_o"
}

# image_inputs - print one SHA-256 over the names and hashes of the files
# mkimg.sh builds and proves the image with.
image_inputs() {
    (cd "$here" && sha256sum mkimg.sh imglib.sh net330.sh ../shlib.sh qmon.py \
        SHELLCHK.ASM CONFIG.SYS AUTOEXEC.BAT SYSTEM.INI PROTOCOL.INI PCNET.SCR) |
        sha256sum | cut -d' ' -f1
}

# image_current - succeed when both images exist and were built from the
# current inputs.
image_current() {
    [ -f "$BASE_IMG" ] && [ -f "$PAYLOAD_TEMPLATE" ] &&
        [ "$(cat "$INPUTS_SHA" 2>/dev/null)" = "$(image_inputs)" ]
}

# image_lock FD s|x DIR - take the lock DIR.lock of the image in DIR on
# descriptor FD, which the QEMUs the script starts inherit: shared (s) for
# a run that boots the image, exclusive (x) for a build that replaces it.
# A script that boots both images takes the WGA image's first, as mkwga.sh
# does.  It waits up to IMAGE_LOCK_WAIT seconds for the other holders,
# saying so, and dies when the lock is not had; il_waited is 1 when it
# waited, else 0.
image_lock() {
    mkdir -p "$(dirname "$3")"
    eval "exec $1>>\"\$3.lock\""
    il_waited=0
    flock -n "-$2" "$1" && return 0
    il_waited=1
    echo "$prog: waiting for the lock $3.lock" >&2
    flock -w "$IMAGE_LOCK_WAIT" "-$2" "$1" ||
        die "the lock $3.lock was not free within $IMAGE_LOCK_WAIT s"
}

# kill_pidfiles FILE... - stop the QEMU processes recorded by -pidfile.
kill_pidfiles() {
    for p in "$@"; do
        [ -s "$p" ] || continue
        kill "$(cat "$p")" 2>/dev/null || :
    done
}
