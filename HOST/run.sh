#!/bin/sh
# run.sh - run make, or a command, in the RETROINV build container.
#
# usage: sh HOST/run.sh [MAKE ARGUMENT...]
#        sh HOST/run.sh -c COMMAND
#
# Runs "make MAKE ARGUMENT..." ("make gate" when none is given), or with
# -c the shell command line COMMAND ("sh" for a shell), in the image of
# HOST/Containerfile, which it first builds when this machine has none
# built from the current Containerfile for the calling user.  The
# repository is mounted read-write as the working directory and RETRO_DL
# read-write as the container user's ~/retro-dl, which RETRO_DL inside
# names too: the setup steps install the period compilers and dBASE there,
# and the build mounts those installs as writable DOS drives.
#
# What the container writes into either directory must belong to the
# caller.  Under rootful Docker or Podman the container runs under the
# caller's user and group numbers; under rootless Podman (also behind a
# "docker" command) its user namespace keeps the caller's number; under
# rootless Docker and Docker Desktop on Linux, whose root inside is the
# caller outside, it runs as root.
#
# env: CONTAINER_ENGINE  docker or podman (default: docker when installed,
#                        else podman)
#      RETRO_DL          the period software, laid out as DOC/BUILD.md
#                        says (default ~/retro-dl)
#
# Exit status: make's or COMMAND's; 2 on a setup error; the engine's when
# it fails to build the image or to start the container.
set -eu

die() { echo "run.sh: $*" >&2; exit 2; }

if [ "${1:-}" = -c ]; then
    [ $# -eq 2 ] || die "usage: run.sh [MAKE ARGUMENT...] | run.sh -c COMMAND"
    set -- sh -c "$2"
else
    [ $# -gt 0 ] || set -- gate
    set -- make "$@"
fi

# The directory as the engine's command line takes it: Git for Windows
# gives a Windows path with pwd -W, and every other shell has none.
hostdir() { (CDPATH='' cd -P "$1" && { pwd -W 2>/dev/null || pwd; }); }

uid=$(id -u)
gid=$(id -g)
[ "$uid" != 0 ] || die "run it as an ordinary user, not as root"
repo=$(CDPATH='' cd -P "$(dirname "$0")/.." && pwd)
[ -f "$repo/HOST/Containerfile" ] && [ -f "$repo/MAKEFILE" ] ||
    die "cannot find the repository"
retro=${RETRO_DL:-$HOME/retro-dl}
[ -d "$retro" ] ||
    die "no directory $retro for the period software: create it, or set" \
        "RETRO_DL to the one that holds it"

engine=${CONTAINER_ENGINE:-}
if [ -z "$engine" ]; then
    for e in docker podman; do
        if command -v $e >/dev/null; then engine=$e; break; fi
    done
fi
case $engine in
docker|podman)
    command -v "$engine" >/dev/null || die "$engine is not installed" ;;
'') die "neither docker nor podman is installed" ;;
*) die "CONTAINER_ENGINE must be docker or podman: $engine" ;;
esac

if $engine --version | grep -qi podman; then
    rootless=$($engine info --format '{{.Host.Security.Rootless}}')
    if [ "$rootless" = true ]; then
        user=--userns=keep-id
    else
        user="--user $uid:$gid"
    fi
else
    case $($engine info --format '{{.SecurityOptions}}{{.OperatingSystem}}') in
    *rootless*) user="--user 0:0" ;;
    *'Docker Desktop'*)
        if [ "$(uname -s)" = Linux ]; then
            user="--user 0:0"
        else
            user="--user $uid:$gid"
        fi ;;
    *) user="--user $uid:$gid" ;;
    esac
fi

# One image per Containerfile and user, so that an edited Containerfile
# or another user gets an image of its own.
sum=$(cksum <"$repo/HOST/Containerfile" | cut -d' ' -f1)
image=retroinv-build:$sum-$uid-$gid
# The build is tested with the x86-64 Linux tools; an ARM machine emulates.
platform=--platform=linux/amd64

# Git for Windows rewrites arguments that look like paths (/work); the
# paths below are already the engine's.  Its mintty window is no console
# for docker.exe, which takes a terminal there only through winpty.
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV
wrap=
tty=
if [ -t 0 ] && [ -t 1 ]; then
    tty=-it
    if [ -n "${MSYSTEM:-}" ] && command -v winpty >/dev/null; then
        wrap=winpty
    fi
fi

if ! $engine image inspect "$image" >/dev/null 2>&1; then
    echo "run.sh: building the image $image"
    ctx=$(hostdir "$repo/HOST")
    $engine build $platform -t "$image" \
        --build-arg UID="$uid" --build-arg GID="$gid" \
        -f "$ctx/Containerfile" "$ctx"
fi

exec $wrap $engine run --rm $tty $platform $user \
    -v "$(hostdir "$repo"):/work" \
    -v "$(hostdir "$retro"):/home/build/retro-dl" \
    -e HOME=/home/build -e RETRO_DL=/home/build/retro-dl \
    "$image" "$@"
