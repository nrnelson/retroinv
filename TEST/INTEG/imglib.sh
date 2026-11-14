# imglib.sh - the steps the QEMU system-disk builders share.  Sourced by
# mkimg.sh and mkwga.sh after net330.sh, whose geometry and helpers it
# uses; not run on its own.
#
# A build happens in DEST.new, made by img_begin, and replaces DEST only
# in img_finish, so a failed build leaves the previous image untouched.
# img_begin sets dest, new and work (DEST.new/build, the scratch files and
# the install VMs' logs); tpid and qmonpid hold the install VM and qmon
# while they run.  SIGHUP, SIGINT and SIGTERM kill both and leave DEST.new
# behind for inspection.
#
# The callers set TIMEOUT (the host-side limit in seconds of each install
# VM), qexit (BIN/QEXIT.COM) and shellchk (BIN/SHELLCHK.COM) before they
# call img_run_vm, img_setup or img_finish.

# The largest file taken from a period archive or disk.
IMG_MAXFILE=$((2 * 1024 * 1024))

# img_begin DEST MARKER - check DEST and DEST.new: each may be new or a
# directory holding MARKER or BASE.IMG, never /, $HOME or the repository.
# Then make an empty DEST.new holding MARKER and its build directory, and
# set the traps.
img_begin() {
    dest=$(out_check "$1" "$2" BASE.IMG) || exit 2
    new=$(out_check "$dest.new" "$2" BASE.IMG) || exit 2
    work="$new/build"
    rm -rf "$new"
    mkdir -p "$work"
    : >"$new/$2"
    # QEMU monitor sockets live in a short private directory: a UNIX
    # socket path is limited to 108 bytes, which a deep checkout can
    # exceed.
    sockdir=$(mktemp -d)
    tpid=
    qmonpid=
    trap 'rm -rf "$sockdir"' EXIT
    on_signals img_stop
}

# img_stop SIGNAL - the signal handler (on_signals): kill the install VM
# and qmon.
img_stop() {
    [ -z "$qmonpid" ] || kill "$qmonpid" 2>/dev/null || :
    [ -z "$tpid" ] || kill "$tpid" 2>/dev/null || :
    kill_pidfiles "$work/qemu.pid"
    rm -rf "$sockdir"
    echo "$prog: stopped by SIG$1; install VM killed," \
        "partial build in $new" >&2
}

# img_shell - make the shells of DEST.new/BASE.IMG, C:\COMMAND.COM and
# C:\DOS\COMMAND.COM, reload their transient part after every program, and
# prove it.  COMMAND.COM keeps that part in the free memory at the top,
# which a program may use, and on the program's return reloads it from disk
# only when a 16-bit sum of its words has changed; a program that leaves
# bytes there with the same sum, by chance about once in 65,535 layouts,
# makes it jump into them, and the VM hangs after the program has ended.
# A shell listed in DOS_SHELLS of TEST/shlib.sh gets NOPs for its JZ
# (shell_patch); any other is left as it is, and named on standard error.
# When both are patched, a scratch copy of the disk boots once with
# DEST.new/PAYLOAD.IMG as D:, where the boot's AUTOEXEC.BAT finds RUN.BAT,
# which runs SHELLCHK (it garbles the transient part and keeps its sum),
# then writes the marker D:\DONE.TXT with ECHO, an internal command that
# only a reloaded shell runs, and ends the VM; img_end_vm fails the build
# without the marker.
img_shell() {
    ish_c=$(part_img "$new/BASE.IMG")
    ish_known=1
    for ish_f in COMMAND.COM DOS/COMMAND.COM; do
        mcopy -n -i "$ish_c" "::$ish_f" "$work/SHELL"
        if shell_patch "$work/SHELL"; then
            mcopy -o -i "$ish_c" "$work/SHELL" "::$ish_f"
        else
            echo "$prog: $ish_f of C: is not a shell this script patches" \
                "(SHA-256 $shell_sum); it reloads its transient part only" \
                "when that part's sum has changed" >&2
            ish_known=0
        fi
    done
    [ "$ish_known" -eq 1 ] || return 0
    cp --sparse=always "$new/BASE.IMG" "$work/SHELL.IMG"
    cp --sparse=always "$new/PAYLOAD.IMG" "$work/SHELLD.IMG"
    chmod u+w "$work/SHELLD.IMG"
    ish_d=$(part_img "$work/SHELLD.IMG")
    dos_text >"$work/RUN.BAT" <<'BAT'
@ECHO OFF
D:\SHELLCHK
IF NOT ERRORLEVEL 1 ECHO OK> D:\DONE.TXT
D:\QEXIT 0
BAT
    mcopy -o -i "$ish_d" "$work/RUN.BAT" ::RUN.BAT
    mcopy -o -i "$ish_d" "$qexit" ::QEXIT.COM
    mcopy -o -i "$ish_d" "$shellchk" ::SHELLCHK.COM
    img_run_vm shellchk \
        -drive file="$work/SHELL.IMG",if=none,id=hd0,format=raw \
        -device "$(hd_device hd0 0)" \
        -drive file="$work/SHELLD.IMG",if=none,id=hd1,format=raw \
        -device "$(hd_device hd1 1)" \
        -boot c -net none -monitor none
    img_end_vm shellchk "$ish_d" ::DONE.TXT
}

# img_finish INPUTS - patch and prove the shells (img_shell), list C: and
# C:\NET of DEST.new/BASE.IMG, make the disk read-only, record INPUTS (the
# hash of the build's inputs) in INPUTS.SHA and move DEST.new into place as
# DEST.
img_finish() {
    img_shell
    mdir -i "$(part_img "$new/BASE.IMG")" ::
    mdir -i "$(part_img "$new/BASE.IMG")" ::NET
    chmod a-w "$new/BASE.IMG"
    rm -rf "$work"
    printf '%s\n' "$1" >"$new/INPUTS.SHA"
    rm -rf "$dest"
    mv "$new" "$dest"
    echo "$prog: built $dest/BASE.IMG and $dest/PAYLOAD.IMG"
}

# img_disk FILE - make FILE an empty disk of the geometry of net330.sh
# with one bootable FAT16 partition (type 04h) and the SYSLINUX MBR boot
# code (SYSLINUX_MBR), which boots the active partition.
img_disk() {
    rm -f "$1"
    truncate -s $((HD_SECTORS * 512)) "$1"
    printf 'label: dos\nstart=%s, size=%s, type=4, bootable\n' \
        "$PART_START" "$PART_SECTORS" | sfdisk -q --no-reread "$1" >/dev/null
    dd if="$SYSLINUX_MBR" of="$1" bs=440 count=1 conv=notrunc 2>/dev/null
}

# img_run_vm NAME QEMU_ARGS... - run an install VM in the background
# under timeout, its output in build/NAME.log; sets tpid.
img_run_vm() {
    ir_log="$work/$1.log"
    shift
    rm -f "$work/qemu.pid"
    # -d nochain works around a QEMU bug in 16-bit near-call wrapping;
    # see boot310.sh.
    timeout "$TIMEOUT" qemu-system-i386 -display none -no-reboot -m 4 \
        -accel "$QEMU_ACCEL" -d nochain -D /dev/null \
        -rtc base=localtime \
        -device isa-debug-exit,iobase=0x501,iosize=0x01 \
        -serial none -parallel none -pidfile "$work/qemu.pid" "$@" \
        >"$ir_log" 2>&1 &
    tpid=$!
}

# img_end_vm NAME MARKER_IMAGE MARKER_PATH - wait for the VM, then require
# QEMU status 1 (QEXIT 0; QEMU also exits 1 when it cannot start) and the
# marker file the VM writes as its last act.
img_end_vm() {
    set +e
    wait "$tpid"
    ie_rc=$?
    set -e
    tpid=
    if [ "$ie_rc" -eq 1 ] && mtype -i "$2" "$3" 2>/dev/null | grep -q OK
    then
        return 0
    fi
    [ ! -s "$work/$1.log" ] || sed 's/^/qemu: /' "$work/$1.log" >&2
    case $ie_rc in
        124) die "$1 VM timed out after ${TIMEOUT}s" ;;
        *) die "$1 VM did not complete (QEMU status $ie_rc, no $3 marker)" ;;
    esac
}

# img_config C SYSTEM_INI NAME - copy the configuration into the disk
# partition C (an mtools image name): SYSTEM_INI, a SYSTEM.INI template
# whose @COMPUTERNAME@ becomes NAME, as C:\NET\SYSTEM.INI, and from this
# directory PROTOCOL.INI (with VM A's NetBEUI timer NB_T1_A),
# PCNET.SCR, CONFIG.SYS and AUTOEXEC.BAT.
#
# PCNET.SCR is the PCI setup of the network card.  The card is QEMU's
# PCnet, a PCI bus master that reads its descriptors and frames from
# memory itself.  AM2100.DOS is the driver for the ISA NE2100 and never
# touches PCI configuration, and SeaBIOS leaves bus mastering off, so the
# test VMs turn it on themselves; this is test-VM setup only, not part of
# a real installation.  Every boot runs this DEBUG script from
# AUTOEXEC.BAT.  It finds the card through the PCI BIOS (INT 1Ah B102h,
# vendor 1022h, device 2000h), sets bus-master enable in its command
# register (register 04h, read with B109h, bit 2 set, written back with
# B10Ch) and then reads back the command register into DX, I/O base
# register 0 (10h) into SI and the interrupt line (3Ch) into CL.  DEBUG
# stops at 138h, before the final INT 20h, and prints the registers to
# C:\NET\PCNET.OUT, which run2vm.sh checks (pcnet_check); a missing card
# leaves the carry flag set from B102h.
img_config() {
    sed "s/@COMPUTERNAME@/$3/g" "$2" | dos_text >"$work/SYSTEM.INI"
    sed "s/@T1@/$NB_T1_A/g" "$here/PROTOCOL.INI" |
        dos_text >"$work/PROTOCOL.INI"
    for ic_f in PCNET.SCR CONFIG.SYS AUTOEXEC.BAT; do
        dos_text "$here/$ic_f" >"$work/$ic_f"
    done
    mcopy -o -i "$1" "$work/SYSTEM.INI" ::NET/SYSTEM.INI
    mcopy -o -i "$1" "$work/PROTOCOL.INI" ::NET/PROTOCOL.INI
    mcopy -o -i "$1" "$work/PCNET.SCR" ::NET/PCNET.SCR
    mcopy -o -i "$1" "$work/CONFIG.SYS" ::CONFIG.SYS
    mcopy -o -i "$1" "$work/AUTOEXEC.BAT" ::AUTOEXEC.BAT
}

# img_setup C_IMAGE SETUP_IMAGE STEP... - run a network SETUP.EXE once:
# boot the system disk C_IMAGE with SETUP_IMAGE, a scratch disk holding
# the SETUP files, as D:, where the boot's AUTOEXEC.BAT finds RUN.BAT,
# which runs D:\SETUP and then writes the marker D:\DONE.TXT and ends the
# VM.  qmon.py drives SETUP through the QEMU monitor with the qmon STEPs
# (it reads the screen from text memory and presses each key once more if
# SETUP lost it).  qmon runs in the background and is waited on, so a
# signal interrupts the wait at once and the trap can kill both it and
# the VM.  Then the backups SETUP makes of the files it rewrites, and the
# PCI setup's record of this boot, are removed.
img_setup() {
    is_s=$(part_img "$2")
    dos_text >"$work/RUN.BAT" <<'BAT'
@ECHO OFF
D:
SETUP
C:
ECHO OK> D:\DONE.TXT
D:\QEXIT 0
BAT
    mcopy -o -i "$is_s" "$work/RUN.BAT" ::RUN.BAT
    mcopy -o -i "$is_s" "$qexit" ::QEXIT.COM
    is_c=$1
    is_d=$2
    shift 2
    img_run_vm setup \
        -drive file="$is_c",if=none,id=hd0,format=raw \
        -device "$(hd_device hd0 0)" \
        -drive file="$is_d",if=none,id=hd1,format=raw \
        -device "$(hd_device hd1 1)" \
        -boot c -net none -monitor unix:"$sockdir/monitor",server,nowait
    python3 -I "$here/qmon.py" "$sockdir/monitor" "$@" &
    qmonpid=$!
    set +e
    wait "$qmonpid"
    is_rc=$?
    set -e
    qmonpid=
    if [ "$is_rc" -ne 0 ]; then
        kill "$tpid" 2>/dev/null || :
        wait "$tpid" 2>/dev/null || :
        tpid=
        [ ! -s "$work/setup.log" ] || sed 's/^/qemu: /' "$work/setup.log" >&2
        die "SETUP.EXE did not reach the expected screens"
    fi
    img_end_vm setup "$is_s" ::DONE.TXT
    mdel -i "$(part_img "$is_c")" ::CONFIG.001 ::AUTOEXEC.001 \
        ::NET/PROTOCOL.001 ::NET/PCNET.OUT 2>/dev/null || :
}
