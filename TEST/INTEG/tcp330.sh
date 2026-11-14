# tcp330.sh - shared settings for the MS-DOS 3.30 + MS Network Client 3.0
# TCP/IP image and the host's Samba mailbox share it reaches.  Sourced by
# mktcpc.sh, smbd.sh and smbprobe.sh after they set $here to this
# directory; not run on its own.  It sources net330.sh (and through it
# TEST/shlib.sh), whose disk geometry, helpers and NetBEUI image the
# TCP/IP image is built from.
#
# The VM runs on QEMU's user network, where it is 10.0.2.15 and the
# gateway 10.0.2.2 (SMB/PROTOCOL.INI).  The host's SMB server appears to
# the VM as SMB_NAME at SMB_ADDR: the image's LMHOSTS maps the name to the
# address, and a run forwards that address's NetBIOS session port 139 to
# the smbd that smbd.sh starts.  The share it serves is SMB_SHARE.
#
# mktcpc.sh records the hash of its inputs (tcp_inputs) in
# TCP330_DIR/INPUTS.SHA; an image whose record differs from the current
# inputs is out of date (tcp_current).
#
# env: TCP330_DIR  directory holding the built images (default
#                  TEST/INTEG/vm/tcp330)

. "$here/net330.sh"

TCP330_DIR=${TCP330_DIR:-"$here/vm/tcp330"}
TCP_BASE_IMG="$TCP330_DIR/BASE.IMG"
TCP_PAYLOAD="$TCP330_DIR/PAYLOAD.IMG"
TCP_INPUTS_SHA="$TCP330_DIR/INPUTS.SHA"

# The VM's computer name, which it also logs on as.
TCP_COMPUTER=RIVMT

SMB_NAME=RIHOST
SMB_ADDR=10.0.2.100
SMB_SHARE=MBOX

# tcp_inputs - print one SHA-256 over the inputs of the NetBEUI image
# (image_inputs) and the files mktcpc.sh adds to it.
tcp_inputs() {
    { image_inputs
      (cd "$here" && sha256sum tcp330.sh mktcpc.sh SMB/AUTOEXEC.BAT \
          SMB/NETUP.BAT SMB/SYSTEM.INI SMB/PROTOCOL.INI SMB/LMHOSTS)
    } | sha256sum | cut -d' ' -f1
}

# tcp_current - succeed when both images exist and were built from the
# current inputs.
tcp_current() {
    [ -f "$TCP_BASE_IMG" ] && [ -f "$TCP_PAYLOAD" ] &&
        [ "$(cat "$TCP_INPUTS_SHA" 2>/dev/null)" = "$(tcp_inputs)" ]
}

# tcp_payload DIR - make DIR/D.IMG, a fresh copy of the blank payload
# disk, and print its partition as an mtools image name.
tcp_payload() {
    cp --sparse=always "$TCP_PAYLOAD" "$1/D.IMG"
    chmod u+w "$1/D.IMG"
    part_img "$1/D.IMG"
}

# tcp_boot DIR PORT SECS - start QEMU in the background, for at most SECS
# seconds, booting a copy-on-write C: of the TCP/IP image with DIR/D.IMG
# as D:, its messages in DIR/QEMU.ERR, and set qpid to it.  QEMU's
# user network, restricted to that one forward, joins the guest's
# connections to SMB_ADDR:139 to 127.0.0.1:PORT, each through its own nc
# (which must take -q, as the traditional and the OpenBSD netcat do).
tcp_boot() {
    tb_user="user,id=n0,restrict=on"
    tb_user="$tb_user,guestfwd=tcp:$SMB_ADDR:139-cmd:nc -q 0 127.0.0.1 $2"
    # -d nochain works around a QEMU bug in 16-bit near-call wrapping;
    # see boot310.sh.
    timeout "$3" qemu-system-i386 -display none -no-reboot -m 4 \
        -accel "$QEMU_ACCEL" -d nochain -D /dev/null \
        -rtc base=localtime \
        -drive file="$TCP_BASE_IMG",if=none,id=hd0,format=raw,snapshot=on \
        -device "$(hd_device hd0 0)" \
        -drive file="$1/D.IMG",if=none,id=hd1,format=raw \
        -device "$(hd_device hd1 1)" \
        -boot c \
        -netdev "$tb_user" -device pcnet,addr=5,netdev=n0 \
        -device isa-debug-exit,iobase=0x501,iosize=0x01 \
        -serial none -parallel none -monitor none \
        >"$1/QEMU.ERR" 2>&1 &
    qpid=$!
}

# The run of a TCP/IP VM script (smbprobe.sh, smbgate.sh): vm, its
# scratch directory once made, and qpid, its QEMU in the background,
# whose smbd.sh server is vm/smbd.  tcp_traps sets them empty and makes
# die, the exit and the signals stop both.

# tcp_stop_children - stop QEMU and smbd.
tcp_stop_children() {
    [ -z "$qpid" ] || kill "$qpid" 2>/dev/null || :
    qpid=
    [ -z "$vm" ] || [ ! -f "$vm/smbd/PID" ] ||
        sh "$here/smbd.sh" stop "$vm/smbd" || :
}

# tcp_die MESSAGE - die (a setup error, status 2), but first stop the
# children and remove the scratch directory if one was made.
tcp_die() {
    echo "$prog: $*" >&2
    tcp_stop_children
    if [ -n "$vm" ]; then
        rm -rf "$vm"
        echo "$prog: scratch directory $vm removed" >&2
    fi
    exit 2
}

# tcp_on_exit - the exit trap: stop the children, keeping the script's
# own exit status.
tcp_on_exit() {
    tc_st=$?
    set +e
    tcp_stop_children
    wait 2>/dev/null
    exit $tc_st
}

# tcp_stop SIGNAL - the signal handler (on_signals): stop the children
# and remove the scratch directory.
tcp_stop() {
    set +e
    echo "$prog: stopped by SIG$1" >&2
    tcp_stop_children
    wait 2>/dev/null || :
    [ -z "$vm" ] || rm -rf "$vm"
}

# tcp_traps - start the run: vm and qpid empty, die as tcp_die, and the
# exit trap and the signal handler above.
tcp_traps() {
    vm=
    qpid=
    die() { tcp_die "$@"; }
    trap tcp_on_exit EXIT
    on_signals tcp_stop
}
