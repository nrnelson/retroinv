# wga500.sh - shared settings for the MS-DOS 5.00 + Workgroup Add-On for
# MS-DOS QEMU image, a NetBEUI peer server.  Sourced by the scripts that
# build or boot the WGA image after they set $here to this directory; not
# run on its own.  It sources net330.sh (and through it TEST/shlib.sh),
# whose disk geometry, helpers and NetBIOS image the WGA image is built
# from.
#
# mkwga.sh records the hash of its inputs (wga_inputs) in
# WGA500_DIR/INPUTS.SHA; an image whose record differs from the current
# inputs is out of date (wga_current).
#
# env: WGA500_DIR  directory holding the built images (default
#                  TEST/INTEG/vm/wga500)

. "$here/net330.sh"

WGA500_DIR=${WGA500_DIR:-"$here/vm/wga500"}
WGA_BASE_IMG="$WGA500_DIR/BASE.IMG"
WGA_PAYLOAD="$WGA500_DIR/PAYLOAD.IMG"
WGA_INPUTS_SHA="$WGA500_DIR/INPUTS.SHA"

# The server's computer name, which it also logs on as.
WGA_COMPUTER=RIWGA

# wga_inputs - print one SHA-256 over the inputs of the NetBIOS image
# (image_inputs), which lend the WGA image its blank payload disk, the
# build steps of imglib.sh and the templates CONFIG.SYS, AUTOEXEC.BAT,
# PROTOCOL.INI and PCNET.SCR, and the files mkwga.sh adds.
wga_inputs() {
    { image_inputs
      (cd "$here" && sha256sum wga500.sh mkwga.sh WGA/SYSTEM.INI)
    } | sha256sum | cut -d' ' -f1
}

# wga_current - succeed when both images exist and were built from the
# current inputs.
wga_current() {
    [ -f "$WGA_BASE_IMG" ] && [ -f "$WGA_PAYLOAD" ] &&
        [ "$(cat "$WGA_INPUTS_SHA" 2>/dev/null)" = "$(wga_inputs)" ]
}
