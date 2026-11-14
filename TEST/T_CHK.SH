#!/bin/sh
# T_CHK.SH - checks the source checkers TEST/msgchk.py and TEST/hlpchk.py,
# fourteen checks of TEST/lint.py (user-visible text, cmem_heapsz, WATTCP's
# headers and kernel list, the private headers, line length, the keywords
# of the Windows sources, const pointers to incomplete structs,
# initializers in block declarations, the screen-test tables, passages
# repeated in or across documents, references to documents, program names
# in a document, a string table's sizes), the map checker TEST/mapchk.sh
# and WC65/toolarge.sh on small faulty fixtures, so that a check they make
# cannot silently stop working.
#
# usage: sh TEST/T_CHK.SH DIR      (run from the repository root)
#
# The fixtures go in a directory of their own made in DIR, so that runs
# side by side (make lint for lint and for test in one gate) never meet;
# it is removed when every check behaves and kept for the reader when
# one does not.  A copy of MSG.H, MSG.C, TUI.H and TUI.C goes there, with
# a SCRITEM.MSG and SCRITEM.C of hand-counted texts: msgchk must accept
# their page at exactly MSG_PAGE bytes and refuse it at one more, refuse
# it beside an MSG.C whose msg_rlo has two ranges swapped, and refuse
# each kind of bad escape and placeholder in a text; and a server's
# source beside it, whose constants --consts must name.  msgchk without
# a FILE.MSG is a usage error.  The files msgchk builds from them, and
# from the real sources of SRCS.MK, must hold each message line without
# its name ("nnn text", "nnn" for an empty text, the text as written),
# in source order and CR LF ended, the server's group too; a built line
# of MSG_MAXRAW bytes must build and one more must not, however long the
# source line; and the stamp must be the CRC of the format line and the
# messages, kept by a rewording and changed by a name, a number or a
# placeholder, the same in both groups and in --header.  A second run on
# the same sources must leave the --header file as it was (its inode and
# its time) and put a new INV.MSG in place by renaming it over the old
# one (a new inode), leaving no temporary file beside either.
# Beside it go MSG.H and a TUI.C of the help limits and two topics, for
# help files that hlpchk must refuse: an empty section, a line before
# the first section starting with '=', a topic too long for an index
# line of MSG_IDXLINE bytes.  A good one must build: "@A " with a
# trailing blank, its index naming A without the blank, and a topic
# whose index line is exactly MSG_IDXLINE bytes.  A section one line
# over TUI_HELPLINES must be refused, and taken beside limits without
# it; limits that define two <P>_HELPW are a usage error.
# lint.py must refuse "Hello" in a CLIENT/SCRX.C and a SERVER/SRVX.C,
# and "%s items" there; accept "ITEM GET\n", "\x1B" and "ITEM %lu";
# and accept "Hello" in a CLIENT/INV.C, which writes its start-up
# errors, and in the function start of a SERVER/SRVRUN.C, but not in a
# function before or after it there (its prototype first).  It must refuse
# cmem_heapsz outside SRC/COMPAT/, an include of WATTCP.H in a TCP/TSX.C
# unless --wattcp names it, of SCRSHR.H (in either case) in a
# CLIENT/INV.C, LISTV.C or SCR.H or a SERVER/SCRX.C but not in a
# CLIENT/SCRITEM.C, and of DBFINT.H behind a directory part in an
# ENGINE/ENGX.C but not in a DBF/DBFX.C; neither in a TEST/ file.
# With --win, it must refuse far, __far __pascal, _export,
# WINAPI, CALLBACK, FAR PASCAL on a function other than WinMain and a //
# comment in a WIN/WX.C, and accept WCALLBACK, FAR, NEAR, CDECL, "int
# PASCAL WinMain", "FAR PASCAL *", "far" in a comment and "(void)x;"; with
# --kernel, a list of modules that lacks one of a KERNEL tuple or adds one
# to it; with --docs, a document that repeats a paragraph, list item
# (nested, numbered or "* "), table row or heading of DOC_UNIT characters
# (one after a closing fence, an underline or a heading, a row inside
# prose; a copy with other blanks or CR LF ends, one after a paragraph
# that starts with inline code), or DOC_RUN long lines, one of exactly
# DOC_LINE characters and the first copy with a short one between them,
# reporting two runs apart and a run whose copy repeats two places as two,
# or leaves a code fence open; but not one that repeats a paragraph one
# character shorter, a table header and row, a code fence, indented or
# not, or DOC_RUN - 1 long lines, beside a near copy of a paragraph; and a
# missing file is exit status 2.  Across two documents it must refuse a
# paragraph and DOC_RUN long lines of the first repeated in the second,
# but not in a document of another extension, nor a run that only reads
# on from the end of one document into the next.  With --refs it must
# accept references to a heading (with or without the extra after its
# name or its backticks), a bold label and a table row's label: quoted,
# in parentheses, after a comma, after a Markdown link, after a few words
# and "in", before the document as '"Label" in' or "(Label of", wrapped
# in a comment, a long run of them joined by commas and "and", a quoted
# one after "and", after a document named twice, and prose: a digit, an
# "(X of" the document does not close, a Markdown list item after a
# comma; and a document's labels of its own above and below, as a path
# and after a lead-in.  It must refuse a missing document; a missing
# label in parentheses, after a Markdown link, after "in", second after
# "and", quoted second, before the document either way, and on the
# comment's next line; a part of a label, quoted, a word shorter, or
# with more words in parentheses; a heading inside a code fence; and a
# label of its own on the wrong side, missing in a path or after a
# lead-in.
# With --names it must accept a document naming the network-only client
# and only programs of its list (INV.MSG, RETROINV and INV_FREEKB being no
# program names), one naming source modules (INVMOD.C, INVHDR.H) and one
# writing INVALID and INVENTORY, and refuse one that does not name the
# client or names INVOLD, a name not in the list: alone, as INVOLD.HLP, or
# alone after naming INVOLD.H.
# With --strings it must accept a string table whose longest string is
# one byte shorter than the size its source defines, escapes and a
# doubled quote counted as one, and refuse one a byte longer, a string
# split over two lines, and a size its source does not define.
# It must refuse a const pointer to a struct still incomplete in a
# prototype, a function pointer (in a typedef or a struct member) or a
# typedef, and accept one after the struct's body, completed by a header
# beside it, in a struct member, a cast or a macro.  It must refuse, with
# or without --compat or --win, an initializer in a block-scope
# declaration, in a function's block or a loop's, of a parenthesised
# declarator too, and accept one at file scope, after a #define that
# opens a brace, or of a static, static const included, beside
# comparisons, assignments, a statement of each keyword an assignment
# may follow (if, else, for, while, switch, case, do, return), an enum,
# a struct's members and a continued #define holding a block.  With
# --scripts, on tables of three rows each (one ending in the literal
# "a{", one of SCRE's with SF_MONO), it must accept lists naming the rows
# and mono lists naming SCRE3.SNM, and report a list lacking rows of a
# table in one line, a key with no row, a report golden that T_RPT.C
# does not name, and a mono list lacking SCRE3.SNM, naming it twice or
# naming the .SNM of a row without SF_MONO.
#
# mapchk.sh gets maps of each of the three formats, of two DGROUP pieces,
# and must refuse one that links CMEM.C with no near-heap module or with
# two; take CMEMHT.C's heap as CMEM_TCPHEAP, CMEMHI.C's and CMEMHR.C's as
# CMEM_INVHEAP and CMEM_REMHEAP in a TLINK map and as CMEM_NEARHEAP in the
# others; hold the near data to exactly the -n budget, and that budget,
# the stack and CMEM_NEARHEAP to exactly 64 KB; with -f, take near data
# over the budget but not over 64 KB beside the stack; and count the
# padding between the pieces.
# With -w, a Windows map, it must take CMEM.C without a near-heap module
# and hold the near data and the stack -w gives to exactly 64 KB.
#
# toolarge.sh gets a compile log in which WCC calls too large a listed
# function of a program module and a function of a test, and must accept
# it, naming the listed function the log does not, but not the listed
# function of an object no log compiles; then the log calls an unlisted
# program function too large, which it must refuse by name.
#
# Exit status: 0 when every check behaves, 1 when one does not.
set -eu

. TEST/shlib.sh

mkdir -p "$1"
dir=$(mktemp -d "$1/CHK.XXXXXX")
fails=0

# bad_check WHAT - report a failed check.
bad_check() {
    echo "T_CHK: $1" >&2
    fails=$((fails + 1))
}

mkdir -p "$dir/CLIENT" "$dir/COMPAT"
cp SRC/CLIENT/MSG.H SRC/CLIENT/MSG.C SRC/CLIENT/TUI.H SRC/CLIENT/TUI.C \
    "$dir/CLIENT/"
cp SRC/COMPAT/COMPAT.H "$dir/COMPAT/"

# page N - run msgchk on a SCRITEM.MSG of seven texts, numbered 450 to
# 455 and 457, six of 143 bytes and the last of N, with a SCRITEM.C
# defining them; its report in $dir/msg.out, $st its exit status.  The
# page takes 2 x (457 - 450 + 1) = 16 bytes of table and 6 x (143 + 1)
# + (N + 1) of texts: 1024 bytes for N = 143, 1025 for N = 144.
page() {
    : > "$dir/CLIENT/SCRITEM.C"
    : > "$dir/CLIENT/SCRITEM.MSG"
    for n in 450 451 452 453 454 455 457; do
        len=143
        [ "$n" -ne 457 ] || len=$1
        echo "#define MI_N$n $n" >> "$dir/CLIENT/SCRITEM.C"
        printf '%s MI_N%s %s\n' "$n" "$n" \
            "$(printf "%0${len}d" 0 | tr 0 x)" >> "$dir/CLIENT/SCRITEM.MSG"
    done
    st=0
    python3 -I TEST/msgchk.py "$dir/CLIENT/SCRITEM.MSG" \
        > "$dir/msg.out" 2>&1 || st=$?
}

grep -q '^#define MSG_PAGE  *1024 ' SRC/CLIENT/MSG.H ||
    bad_check "the page fixture is counted for a MSG_PAGE of 1024"
page 143
[ "$st" -eq 0 ] || bad_check "msgchk: a page of MSG_PAGE bytes: exit $st"
page 144
[ "$st" -eq 1 ] || bad_check "msgchk: a page of MSG_PAGE + 1 bytes: exit $st"
grep -q 'SCRITEM.MSG: page of 1025 bytes, more than MSG_PAGE' \
    "$dir/msg.out" || bad_check "msgchk: no page of 1025 bytes reported"

mv "$dir/CLIENT/MSG.C" "$dir/MSG.C"
sed 's/MSG_R_TUI, MSG_R_INV,/MSG_R_INV, MSG_R_TUI,/' "$dir/MSG.C" \
    > "$dir/CLIENT/MSG.C"
cmp -s "$dir/MSG.C" "$dir/CLIENT/MSG.C" &&
    bad_check "the msg_rlo fixture swaps no ranges"
page 143
[ "$st" -eq 1 ] || bad_check "msgchk: msg_rlo out of order: exit $st"
grep -q 'msg_rlo entry 1 is MSG_R_INV, where MSG.H puts MSG_R_TUI' \
    "$dir/msg.out" || bad_check "msgchk: msg_rlo out of order not reported"
mv "$dir/MSG.C" "$dir/CLIENT/MSG.C"

# msg_bad TEXT MESSAGE - msgchk must refuse a SCRITEM.MSG whose one text
# is TEXT with exit status 1 and a report holding MESSAGE.
msg_bad() {
    echo '#define MI_N450 450' > "$dir/CLIENT/SCRITEM.C"
    printf '450 MI_N450 %s\n' "$1" > "$dir/CLIENT/SCRITEM.MSG"
    st=0
    python3 -I TEST/msgchk.py "$dir/CLIENT/SCRITEM.MSG" \
        > "$dir/msg.out" 2>&1 || st=$?
    [ "$st" -eq 1 ] || bad_check "msgchk: \"$1\": exit $st, not 1"
    grep -qF "$2" "$dir/msg.out" || bad_check "msgchk: \"$1\": no \"$2\""
}

msg_bad 'a\xZZ' 'bad \x escape'
msg_bad 'a\x01' '\x01 is reserved'
msg_bad 'a\q' 'bad escape \q'
msg_bad 'a^3' 'bad placeholder ^3'
st=0
python3 -I TEST/msgchk.py --header "$dir/X.H" > "$dir/msg.out" 2>&1 || st=$?
[ "$st" -eq 2 ] && grep -q '^msgchk: no FILE.MSG' "$dir/msg.out" ||
    bad_check "msgchk: no FILE.MSG: exit $st, or no reason given"

# The server's message file: SERVER/INVSERV.MSG, eight texts of 140
# bytes from MSG_R_SRV, defined in the module's two C files, SRVSCR.C
# and SRVRUN.C (the last), more than MSG_PAGE together, which is right
# for a range that stays loaded.  Its directory has no MSG.H, so msgchk
# must refuse it (exit 2) without --consts and build it with --consts
# naming CLIENT.
srv=$(sed -n 's/^#define MSG_R_SRV  *\([0-9]*\).*/\1/p' SRC/CLIENT/MSG.H)
mkdir -p "$dir/SERVER"
: > "$dir/SERVER/SRVSCR.C"
: > "$dir/SERVER/SRVRUN.C"
: > "$dir/SERVER/INVSERV.MSG"
for i in 0 1 2 3 4 5 6 7; do
    c=SRVSCR.C
    [ "$i" -lt 7 ] || c=SRVRUN.C
    echo "#define MD_N$i $((srv + i))" >> "$dir/SERVER/$c"
    printf '%s MD_N%s %s\n' "$((srv + i))" "$i" \
        "$(printf '%0140d' 0 | tr 0 x)" >> "$dir/SERVER/INVSERV.MSG"
done
st=0
python3 -I TEST/msgchk.py "$dir/SERVER/INVSERV.MSG" > "$dir/srv.out" 2>&1 ||
    st=$?
[ "$st" -eq 2 ] ||
    bad_check "msgchk: a server source without --consts: exit $st"
st=0
python3 -I TEST/msgchk.py --out "$dir/INVSERV.MSG" --consts "$dir/CLIENT" \
    "$dir/SERVER/INVSERV.MSG" > "$dir/srv.out" 2>&1 || st=$?
[ "$st" -eq 0 ] ||
    bad_check "msgchk: the server's texts with --consts: exit $st"
[ "$(grep -c '^[0-9]' "$dir/INVSERV.MSG" 2>/dev/null)" = 8 ] ||
    bad_check "msgchk: the server's file not built with its eight texts"

# The built file: "=STAMP hhhh", then per message line of the sources its
# number and, for a text that is not empty, one space and the text as the
# source has it, every line ended CR LF; no constant name.  The stamp is
# the CRC-16 (1021h, from FFFFh, no final inversion) of "=FORMAT 2\n" and
# then "number NAME ones twos\n" per message in the order of their
# numbers, so that an INV.MSG of the earlier format, whose lines carry
# the names and whose stamp is that CRC without the format line, does
# not load.
cr=$(printf '\r')
maxraw=$(sed -n 's/^#define MSG_MAXRAW  *\([0-9]*\).*/\1/p' SRC/CLIENT/MSG.H)

# build LINE... - msgchk --out $dir/INV.MSG --header $dir/STAMP.H on a
# SCRITEM.MSG of the LINEs, with a SCRITEM.C defining each message line's
# constant; the built file of an earlier run removed first; $st the exit
# status, the report in $dir/msg.out.
build() {
    : > "$dir/CLIENT/SCRITEM.MSG"
    rm -f "$dir/INV.MSG"
    for l; do
        printf '%s\n' "$l" >> "$dir/CLIENT/SCRITEM.MSG"
    done
    awk '/^[0-9]/ { print "#define " $2 " " $1 }' \
        "$dir/CLIENT/SCRITEM.MSG" > "$dir/CLIENT/SCRITEM.C"
    st=0
    python3 -I TEST/msgchk.py --out "$dir/INV.MSG" --header "$dir/STAMP.H" \
        "$dir/CLIENT/SCRITEM.MSG" > "$dir/msg.out" 2>&1 || st=$?
}

# built FILE LINE... - FILE after its stamp line must be the LINEs, each
# ended CR LF, byte for byte.
built() {
    bf=$1
    shift
    : > "$dir/want.msg"
    for l; do
        printf '%s\r\n' "$l" >> "$dir/want.msg"
    done
    tail -n +2 "$bf" 2>/dev/null | cmp -s - "$dir/want.msg" ||
        bad_check "msgchk: $(basename "$bf") not built as \"$*\""
}

# crc TEXT - "=STAMP hhhh", the stamp line of the CRC of TEXT (printf %b).
crc() {
    printf '%b' "$1" | python3 -I -c '
import sys
c = 0xFFFF
for b in sys.stdin.buffer.read():
    c ^= b << 8
    for _ in range(8):
        c = ((c << 1) ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
print("=STAMP %04X" % c)'
}

# stamp - the stamp line of $dir/INV.MSG without its CR.
stamp() {
    head -n 1 "$dir/INV.MSG" 2>/dev/null | tr -d '\r'
}

fmt='=FORMAT 2\n'
build '450 MI_N450 hello ^1'
[ "$st" -eq 0 ] || bad_check "msgchk: \"450 MI_N450 hello ^1\": exit $st"
built "$dir/INV.MSG" '450 hello ^1'
[ "$(head -n 1 "$dir/INV.MSG")" = "$(crc "${fmt}450 MI_N450 1 0\n")$cr" ] ||
    bad_check "msgchk: stamp line not the CRC of the format line"
s1=$(stamp)
[ "$s1" != "$(crc '450 MI_N450 1 0\n')" ] ||
    bad_check "msgchk: the stamp is the earlier format's"
grep -q "^#define MSG_STAMP 0x${s1#=STAMP }U\$" "$dir/STAMP.H" ||
    bad_check "msgchk: --header does not hold the file's stamp"
build '450 MI_N450 goodbye ^1'
[ "$(stamp)" = "$s1" ] || bad_check "msgchk: rewording changed the stamp"
build '450 MI_M450 hello ^1'
[ "$(stamp)" != "$s1" ] &&
    [ "$(stamp)" = "$(crc "${fmt}450 MI_M450 1 0\n")" ] ||
    bad_check "msgchk: renaming did not change the stamp so"
build '451 MI_N450 hello ^1'
[ "$(stamp)" != "$s1" ] || bad_check "msgchk: renumbering kept the stamp"
build '450 MI_N450 hello ^1 ^1'
[ "$(stamp)" = "$(crc "${fmt}450 MI_N450 2 0\n")" ] ||
    bad_check "msgchk: a ^1 more did not change the stamp so"
build '450 MI_N450 a^^1 ^2'
[ "$(stamp)" = "$(crc "${fmt}450 MI_N450 0 1\n")" ] ||
    bad_check "msgchk: ^^1 counted as a placeholder, or ^2 not"

build '450 MI_N450'
built "$dir/INV.MSG" '450'
build '450 MI_N450 '
built "$dir/INV.MSG" '450'
build '450 MI_N450   x'
built "$dir/INV.MSG" '450   x'
build '450 MI_N450 x  '
built "$dir/INV.MSG" '450 x  '
build '450 MI_N450 a\x41\n^^'
built "$dir/INV.MSG" '450 a\x41\n^^'
build '452 MI_C c' '# comment' '' '450 MI_A a' '451 MI_B'
[ "$st" -eq 0 ] || bad_check "msgchk: three messages out of order: exit $st"
built "$dir/INV.MSG" '452 c' '450 a' '451'
[ "$(stamp)" = "$(crc "${fmt}450 MI_A 0 0\n451 MI_B 0 0\n452 MI_C 0 0\n")" ] ||
    bad_check "msgchk: the stamp not in number order"
! grep -q 'MI_' "$dir/INV.MSG" || bad_check "msgchk: a name in the built file"

# A second run on the same sources; find -newer compares the times finer
# than ls -l shows them.
build '450 MI_N450 hello ^1'
hi=$(ls -i "$dir/STAMP.H")
mi=$(ls -i "$dir/INV.MSG")
: > "$dir/before"
st=0
python3 -I TEST/msgchk.py --out "$dir/INV.MSG" --header "$dir/STAMP.H" \
    "$dir/CLIENT/SCRITEM.MSG" > "$dir/msg.out" 2>&1 || st=$?
[ "$st" -eq 0 ] || bad_check "msgchk: a second run: exit $st"
[ "$(ls -i "$dir/STAMP.H")" = "$hi" ] &&
    [ -z "$(find "$dir/STAMP.H" -newer "$dir/before")" ] ||
    bad_check "msgchk: an unchanged --header file written again"
[ "$(ls -i "$dir/INV.MSG")" != "$mi" ] ||
    bad_check "msgchk: INV.MSG written in place, not renamed"
for f in "$dir"/*.MSG.* "$dir"/*.H.*; do
    [ ! -e "$f" ] || bad_check "msgchk: $(basename "$f") left behind"
done

# MSG_MAXRAW bounds the built line, the source line being 8 bytes longer
# ("MI_N450 "); 60 escapes \x41 keep the text within MSG_MAXLEN.
esc=$(printf '%060d' 0 | sed 's/0/\\x41/g')
long=$esc$(printf "%0$((maxraw - 4 - 240))d" 0 | tr 0 x)
build "450 MI_N450 $long"
[ "$st" -eq 0 ] ||
    bad_check "msgchk: a built line of MSG_MAXRAW bytes: exit $st"
built "$dir/INV.MSG" "450 $long"
build "450 MI_N450 ${long}x"
[ "$st" -eq 1 ] || bad_check "msgchk: a built line of MSG_MAXRAW + 1: exit $st"
grep -q 'SCRITEM.MSG:1: line longer than MSG_MAXRAW' "$dir/msg.out" ||
    bad_check "msgchk: a built line of MSG_MAXRAW + 1 not reported"
[ ! -e "$dir/INV.MSG" ] ||
    bad_check "msgchk: INV.MSG written with a line of MSG_MAXRAW + 1 bytes"

# The server's group of the same run: nameless lines, the same stamp.
build '450 MI_N450 hello ^1'
st=0
python3 -I TEST/msgchk.py --out "$dir/INV.MSG" "$dir/CLIENT/SCRITEM.MSG" \
    --out "$dir/INVSERV.MSG" --consts "$dir/CLIENT" \
    "$dir/SERVER/INVSERV.MSG" > "$dir/srv.out" 2>&1 || st=$?
[ "$st" -eq 0 ] || bad_check "msgchk: a client and a server group: exit $st"
x140=$(printf '%0140d' 0 | tr 0 x)
want=
set --
for i in 0 1 2 3 4 5 6 7; do
    want="$want$((srv + i)) MD_N$i 0 0\n"
    set -- "$@" "$((srv + i)) $x140"
done
built "$dir/INVSERV.MSG" "$@"
[ "$(head -n 1 "$dir/INVSERV.MSG")" = "$(head -n 1 "$dir/INV.MSG")" ] &&
    [ "$(stamp)" = "$(crc "${fmt}450 MI_N450 1 0\n$want")" ] ||
    bad_check "msgchk: the two groups' stamps differ, or not over both"

# The real sources, built as the MAKEFILE does: each built line is its
# source line without the name, CR LF ended, in the order given.
vars=$(srcs_vars SRCS.MK)
mkdir -p "$dir/REAL"
# shellcheck disable=SC2046
python3 -I TEST/msgchk.py --out "$dir/REAL/INV.MSG" $(getvar INV_MSGS) \
    --out "$dir/REAL/INVSERV.MSG" --consts SRC/CLIENT $(getvar SRV_MSGS) \
    > "$dir/real.out" 2>&1 || bad_check "msgchk: the real sources do not build"
for f in INV:INV_MSGS INVSERV:SRV_MSGS; do
    # shellcheck disable=SC2046
    sed -n 's/^\([1-9][0-9]*\) [A-Z0-9_]* \{0,1\}$/\1/p
        s/^\([1-9][0-9]*\) [A-Z0-9_]* \(..*\)$/\1 \2/p' \
        $(getvar "${f#*:}") | sed 's/$/\r/' > "$dir/REAL/want.msg"
    tail -n +2 "$dir/REAL/${f%:*}.MSG" 2>/dev/null |
        cmp -s - "$dir/REAL/want.msg" ||
        bad_check "msgchk: the real ${f%:*}.MSG is not its sources unnamed"
done

# hlpchk_bad NAME TEXT MESSAGE - hlpchk must refuse the help file TEXT
# with exit status 1 and a report holding MESSAGE.
hlpchk_bad() {
    printf '%b' "$2" > "$dir/$1.HLP"
    st=0
    python3 -I TEST/hlpchk.py "$dir/$1.HLP" "$dir/HLP/TUI.C" \
        "$dir/HLP/MSG.H" "$dir/HLP/TUI.C" > "$dir/$1.out" || st=$?
    [ "$st" -eq 1 ] || bad_check "hlpchk: $1: exit $st, not 1"
    grep -q "$3" "$dir/$1.out" || bad_check "hlpchk: $1: no \"$3\""
}

mkdir -p "$dir/HLP"
cp SRC/CLIENT/MSG.H "$dir/HLP/"
# fit: a topic whose index line "=NAME hhhh hhhh" is MSG_IDXLINE bytes.
idx=$(sed -n 's/^#define MSG_IDXLINE  *\([0-9]*\).*/\1/p' SRC/CLIENT/MSG.H)
fit=$(printf "%0$((idx - 11))d" 0 | tr 0 T)
cat > "$dir/HLP/TUI.C" <<TUIC
#define TUI_HELPW       70
#define TUI_HELPLINES   48
#define TUI_HELPKEYS    "KEYS"
static void help(void) { tui_topic("A"); tui_topic("$fit"); }
TUIC
hlpchk_bad EMPTY "pre\\n@A \\nline\\n@${fit}\\nf\\n@KEYS\\n" \
    'section KEYS is empty'
hlpchk_bad EQUALS "=pre\\n@A\\nline\\n@${fit}\\nf\\n@KEYS\\nk\\n" \
    '"=" starts a line'
hlpchk_bad LONG "@A\\nl\\n@${fit}U\\nf\\n@KEYS\\nk\\n" \
    'too long for an index line'
printf 'pre\n@A \nline\n@%s\nf\n@KEYS\nk\n' "$fit" > "$dir/GOOD.HLP"
python3 -I TEST/hlpchk.py --out "$dir/GOOD.BLT" "$dir/GOOD.HLP" \
    "$dir/HLP/TUI.C" "$dir/HLP/MSG.H" "$dir/HLP/TUI.C" ||
    bad_check "hlpchk: GOOD refused"
head -n 1 "$dir/GOOD.BLT" | grep -q '^=A [0-9A-F]\{4\} 0006.$' ||
    bad_check "hlpchk: GOOD: index line not \"=A hhhh 0006\""
# A section one line over TUI_HELPLINES is refused; without the limit,
# which a viewer that scrolls does not define, it is taken.
long="@A\\nl\\n@${fit}\\nf\\n@KEYS\\n$(printf 'k\\n%.0s' $(seq 49))"
hlpchk_bad LINES "$long" 'section KEYS has 49 lines (TUI_HELPLINES 48)'
grep -v '_HELPLINES' "$dir/HLP/TUI.C" > "$dir/HLP/NOLINES.C"
printf '%b' "$long" > "$dir/NOLINES.HLP"
python3 -I TEST/hlpchk.py "$dir/NOLINES.HLP" "$dir/HLP/NOLINES.C" \
    "$dir/HLP/MSG.H" "$dir/HLP/TUI.C" > "$dir/NOLINES.out" 2>&1 ||
    bad_check "hlpchk: NOLINES: a long section refused without a limit"
# Limits of two prefixes are refused as a usage error.
{ echo '#define WH_HELPW 70'; cat "$dir/HLP/TUI.C"; } > "$dir/HLP/TWO.C"
st=0
python3 -I TEST/hlpchk.py "$dir/GOOD.HLP" "$dir/HLP/TWO.C" "$dir/HLP/MSG.H" \
    "$dir/HLP/TUI.C" > "$dir/TWO.out" 2>&1 || st=$?
[ "$st" -eq 2 ] || bad_check "hlpchk: TWO: exit $st, not 2"
grep -q 'not one <P>_HELPW defined' "$dir/TWO.out" ||
    bad_check "hlpchk: TWO: two <P>_HELPW not reported"

# lint_prose DIR NAME TEXT STATUS - lint.py on a DIR/NAME holding the
# literal TEXT must exit with STATUS.
lint_prose() {
    mkdir -p "$dir/LINT/SRC/$1"
    printf 'static const char k_t[] = "%s";\n' "$3" > "$dir/LINT/SRC/$1/$2"
    st=0
    python3 -I TEST/lint.py "$dir/LINT/SRC/$1/$2" > "$dir/lint.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$4" ] || bad_check "lint: $1/$2 with \"$3\": exit $st, not $4"
}

lint_prose CLIENT SCRX.C 'Hello' 1
grep -q 'user-visible text outside the message files' "$dir/lint.out" ||
    bad_check "lint: SCRX.C: no user-visible text reported"
lint_prose SERVER SRVX.C 'Hello' 1
lint_prose CLIENT SCRX.C 'ITEM GET\n' 0
lint_prose CLIENT SCRX.C '\x1B' 0
lint_prose CLIENT SCRX.C 'ITEM %lu' 0
lint_prose CLIENT SCRX.C '%s items' 1
lint_prose CLIENT INV.C 'Hello' 0
# lint_srvrun WHAT STATUS FUNCTION... - lint.py on a SERVER/SRVRUN.C of
# the prototype of start, then each FUNCTION ("start" or "other", with
# "Hello" when written "start+" or "other+"), must exit with STATUS.
lint_srvrun() {
    what=$1
    want=$2
    shift 2
    lx=$dir/LINT/SRC/SERVER/SRVRUN.C
    printf 'int start(void);\n' > "$lx"
    for fn; do
        t=
        [ "${fn%+}" = "$fn" ] || t=Hello
        printf 'static int %s(void)\n{\n    return f("%s");\n}\n' \
            "${fn%+}" "$t" >> "$lx"
    done
    st=0
    python3 -I TEST/lint.py "$lx" > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$want" ] ||
        bad_check "lint: SRVRUN.C, $what: exit $st, not $want"
}
lint_srvrun '"Hello" in start' 0 start+
lint_srvrun '"Hello" before start' 1 other+ start
lint_srvrun '"Hello" after start' 1 start+ other+

mkdir -p "$dir/LINT/SRC/SERVER"
lx=$dir/LINT/SRC/SERVER/SRVX.C
printf 'unsigned f(void) { return cmem_heapsz; }\n' > "$lx"
python3 -I TEST/lint.py "$lx" > "$dir/lint.out" 2>&1 &&
    bad_check "lint: cmem_heapsz outside SRC/COMPAT/ accepted"

mkdir -p "$dir/LINT/SRC/TCP"
lx=$dir/LINT/SRC/TCP/TSX.C
printf '#include "WATTCP.H"\n' > "$lx"
python3 -I TEST/lint.py "$lx" > "$dir/lint.out" 2>&1 &&
    bad_check "lint: WATTCP.H in a file not named by --wattcp accepted"
python3 -I TEST/lint.py --wattcp 'SRC/A.C SRC/TCP/TSX.C' "$lx" \
    > "$dir/lint.out" 2>&1 ||
    bad_check "lint: WATTCP.H in a file named by --wattcp refused"

# lint_private STATUS FILE HEADER - lint.py on FILE (under LINT/) that
# includes HEADER must exit with STATUS.
lint_private() {
    lx=$dir/LINT/$2
    mkdir -p "${lx%/*}"
    printf '#include "%s"\n' "$3" > "$lx"
    st=0
    python3 -I TEST/lint.py "$lx" > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$1" ] ||
        bad_check "lint: $3 in $2: exit $st, not $1"
}
lint_private 1 SRC/CLIENT/INV.C SCRSHR.H
lint_private 1 SRC/CLIENT/LISTV.C scrshr.h
lint_private 1 SRC/CLIENT/SCR.H SCRSHR.H
lint_private 1 SRC/SERVER/SCRX.C SCRSHR.H
lint_private 0 SRC/CLIENT/SCRITEM.C SCRSHR.H
lint_private 0 TEST/T_SCRR.C SCRSHR.H
lint_private 1 SRC/ENGINE/ENGX.C ../DBF/DBFINT.H
lint_private 0 SRC/DBF/DBFX.C DBFINT.H
lint_private 0 TEST/T_DBF.C DBFINT.H

# lint_cols WHAT STATUS OPTION LINE - lint.py, with OPTION when it is not
# empty, on an ENGINE/ENGX.H holding LINE must exit with STATUS.  A line
# of 79 columns passes and one of 80 does not, in a comment as in code,
# under every set of rules; a tab counts to the next multiple of 8.
c74='/* 34567890123456789012345678901234567890123456789012345678901234567890 */'
lint_cols() {
    lx=$dir/LINT/SRC/ENGINE/ENGX.H
    mkdir -p "${lx%/*}"
    printf '%b\n' "$4" > "$lx"
    st=0
    python3 -I TEST/lint.py $3 "$lx" > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: $1: exit $st, not $2"
}
lint_cols '79 columns' 0 '' "$c74 /**/"
lint_cols '80 columns' 1 '' "$c74 /* */"
grep -q 'ENGX.H:1: line over 79 columns' "$dir/lint.out" ||
    bad_check "lint: 80 columns not reported"
lint_cols '80 columns of code' 1 '' "int f(void);$(printf '%68s' '')"
lint_cols '79 columns, --compat' 0 --compat "$c74 /**/"
lint_cols '80 columns, --compat' 1 --compat "$c74 /* */"
lint_cols '80 columns, --win' 1 --win "$c74 /* */"
lint_cols 'a tab, 79 columns' 0 '' "\\tint f(void);$(printf '%59s' '')"
lint_cols 'a tab, 80 columns' 1 '' "\\tint f(void);$(printf '%60s' '')"
lint_cols 'a tab in a line, 79 columns' 0 '' \
    "ab\\tint f(void);$(printf '%59s' '')"
lint_cols 'a tab in a line, 80 columns' 1 '' \
    "ab\\tint f(void);$(printf '%60s' '')"

# lint_win WHAT STATUS LINE - lint.py --win on a WIN/WX.C holding LINE
# must exit with STATUS.
mkdir -p "$dir/LINT/SRC/WIN"
lint_win() {
    lx=$dir/LINT/SRC/WIN/WX.C
    printf '%s\n' "$3" > "$lx"
    st=0
    python3 -I TEST/lint.py --win "$lx" > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --win, $1: exit $st, not $2"
}
lint_win 'the macros' 0 'long WCALLBACK f(char NEAR *s, char FAR *t);'
lint_win 'CDECL' 0 'int CDECL g(void);'
lint_win 'WinMain' 0 'int PASCAL WinMain(HINSTANCE i, HINSTANCE p, LPSTR c, int s)'
lint_win 'a pointer' 0 'typedef int (FAR PASCAL *ws_start)(WORD v);'
lint_win 'FAR PASCAL' 1 'long FAR PASCAL f(void);'
lint_win '"far" in a comment' 0 'int f(void); /* a far pascal one */'
lint_win '"(void)x;"' 0 'int f(int x) { (void)x; return 0; }'
lint_win 'far' 1 'char far *p;'
lint_win '__far __pascal' 1 'long __far __pascal f(void);'
lint_win '_export' 1 'void _export f(void);'
lint_win 'WINAPI' 1 'long WINAPI f(void);'
lint_win 'CALLBACK' 1 'long CALLBACK f(void);'
lint_win 'a // comment' 1 'int f(void); // no'

printf "KERNEL = ('A.C', 'B.C')\n" > "$dir/PREP.PY"
python3 -I TEST/lint.py --kernel "$dir/PREP.PY" B.C A.C \
    > "$dir/lint.out" 2>&1 || bad_check "lint: --kernel refused the list"
python3 -I TEST/lint.py --kernel "$dir/PREP.PY" A.C > "$dir/lint.out" 2>&1 &&
    bad_check "lint: --kernel accepted a list lacking B.C"
python3 -I TEST/lint.py --kernel "$dir/PREP.PY" A.C B.C C.C \
    > "$dir/lint.out" 2>&1 &&
        bad_check "lint: --kernel accepted a list adding C.C"

# lint_str CASE STATUS NAME TABLE - lint.py --strings on RC.RC, whose
# string table holds the lines printf TABLE writes, against NAME of RC.C
# ("#define SZ 16"): exit STATUS.
lint_str() {
    { printf 'STRINGTABLE\nBEGIN\n'; printf "$4"; printf 'END\n'; } \
        > "$dir/RC.RC"
    st=0
    python3 -I TEST/lint.py --strings "$dir/RC.RC" "$dir/RC.C" "$3" \
        > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --strings, $1: exit $st, not $2"
}
printf '#define SZ 16\n' > "$dir/RC.C"
lint_str 'SZ - 1 bytes' 0 SZ '    S_A, "abcdefghijkl\\t\042\042\\n"\n'
lint_str 'SZ bytes' 1 SZ '    S_A + 1, "abcdefghijklm\\t\042\042\\n"\n'
lint_str 'a split line' 1 SZ '    S_A,\n        "split"\n'
lint_str 'an undefined size' 1 NOSZ '    S_A, "a"\n'
grep -q 'no "#define NOSZ n"' "$dir/lint.out" ||
    bad_check "lint: --strings did not name the undefined size"

# lint_doc CASE STATUS - lint.py --docs on CASE.MD, DOC.MD followed by
# $dir/add, must exit with STATUS; its report in $dir/lint.out.  Like
# every check function, it runs outside a pipeline, whose subshell would
# lose the failures it counts.
lint_doc() {
    cat "$dir/LINT/DOC.MD" "$dir/add" > "$dir/LINT/$1.MD"
    st=0
    python3 -I TEST/lint.py --docs "$dir/LINT/$1.MD" \
        > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --docs, $1: exit $st, not $2"
}

grep -q '^DOC_UNIT = 60$' TEST/lint.py &&
    grep -q '^DOC_LINE = 20$' TEST/lint.py &&
    grep -q '^DOC_RUN = 3$' TEST/lint.py ||
    bad_check "--docs fixture counted for DOC_UNIT 60, DOC_LINE 20, DOC_RUN 3"
# DOC.MD repeats only what --docs lets pass: a paragraph of DOC_UNIT - 1
# characters, a table header and row, a code fence of long lines, and
# DOC_RUN - 1 long lines in a row.  It holds a near copy of a paragraph
# of DOC_UNIT characters; a list whose long items, one of exactly
# DOC_LINE characters, are split by a shorter one; a list item that
# reaches DOC_UNIT only over its two lines; a numbered list; a table;
# paragraphs right after a closing fence, a plain-text underline and a
# heading; a nested item and a "* " item; and in an indented fence more
# than DOC_RUN equal long lines.
p60='A paragraph of sixty characters is long enough to compare it'
near='A paragraph of sixty characters is long enough to compare us'
p59='A paragraph of fifty-nine characters, too short to compare.'
q60='A paragraph right after a closing fence, compared on its own'
u60='A paragraph right after an underline, which is not text of it'
k1='- the first item of a short list'
k2='- the second item of a short list'
k3='- the third, shorter'
hdr='| Field | Type | Width | Dec | Notes |'
row='| ITEM_ID | N | 8 | 0 | FK ITEMS |'
wrap='- a list item of more than sixty characters, wrapped
  over two lines'
n60='2. a second step, long enough for the check to compare it alone'
r60='| a table row of more than sixty characters | compared alone |'
h60='A paragraph right under a heading, which is a passage of its own'
s60='- a nested list item, long enough to be compared all on its own'
t60='* a list item with a star, long enough to be compared on its own'
fence='```'
cat > "$dir/LINT/DOC.MD" <<EOF
# Doc

$p60

$near

$p59

$p59

$hdr
|---|---|---|---|---|
$row

$k1
- short
$k2
$k3

$fence
make lint test all tc wc65 dbcheck tctest
make lint test all tc wc65 dbcheck tctest
make lint test all tc wc65 dbcheck tctest
$fence
$q60

$hdr
|---|---|---|---|---|
$row

$fence
make lint test all tc wc65 dbcheck tctest
make lint test all tc wc65 dbcheck tctest
make lint test all tc wc65 dbcheck tctest
$fence

The list's first two items again:

$k1
$k2

$wrap
- the last item of the second list

1. a first step
$n60

$r60
| a short row | x |

Plain heading
====================
$u60

## One
$h60

- a first item
  $s60
$t60
* short

- an item that holds a fence:

  $fence
  make dist distcheck integ wgagate smbgate
  make dist distcheck integ wgagate smbgate
  make dist distcheck integ wgagate smbgate
  make dist distcheck integ wgagate smbgate
  $fence
EOF
[ "${#k3}" -eq 20 ] || bad_check "--docs fixture: k3 is not DOC_LINE long"
: > "$dir/add"
lint_doc GOOD 0
# doc_has CASE LINE... - the report of lint_doc CASE holds each LINE,
# "CASE.MD:" before it, and nothing more.
doc_has() {
    c=$1
    shift
    for ln; do
        grep -qF "$c.MD:$ln" "$dir/lint.out" ||
            bad_check "lint: --docs, $c: no \"$ln\""
    done
    grep -q "^lint: $# document problem(s)\$" "$dir/lint.out" ||
        bad_check "lint: --docs, $c: not $# problem(s)"
}
printf '\n  A paragraph\tof  sixty%s \n' "${p60#A paragraph of sixty}" \
    > "$dir/add"
lint_doc PARA 1
doc_has PARA '73: repeats the passage of line 3'
printf '\n- an item before it\n%s\n\n1. another first step\n%s\n\n%s\n%s\n' \
    "$wrap" "$n60" '| another row | y |' "$r60" > "$dir/add"
lint_doc ITEM 1
doc_has ITEM '74: repeats the passage of line 42' \
    '78: repeats the passage of line 47' '81: repeats the passage of line 49'
printf '\n%s\n%s\n%s\n' "$k1" "$k2" "$k3" > "$dir/add"
lint_doc RUN 1
doc_has RUN '73: lines 73-75 repeat lines 15-18'
printf '\n%s\n' "$p60" "$near" "$p59" "$p59" 'A line found in no other place.' \
    "$p59" "$p59" "$hdr" > "$dir/add"
lint_doc TWO 1
doc_has TWO '73: lines 73-79 repeat lines 3-9' \
    '83: lines 83-87 repeat lines 7-11'
printf '\n%s\n\n%s\n%s\n\n%s\n' "$q60" "$hdr" "$row" "$k1" > "$dir/add"
lint_doc SPLIT 1
doc_has SPLIT '73: lines 73-76 repeat lines 25-29' \
    '75: lines 75-78 repeat lines 11-15'
printf '\n%s\n\n%s\n' "$q60" "$u60" > "$dir/add"
lint_doc GLUE 1
doc_has GLUE '73: repeats the passage of line 25' \
    '75: repeats the passage of line 54'
printf '\n%s\n%s\n' "$fence" "$p60" > "$dir/add"
lint_doc OPEN 1
doc_has OPEN '73: code fence opened here is not closed'
printf '\r\n%s\r\n' "$p60" > "$dir/add"
lint_doc CRLF 1
doc_has CRLF '73: repeats the passage of line 3'
printf '\n  %s\n\n- x\n%s\n' "$s60" "$t60" > "$dir/add"
lint_doc LIST 1
doc_has LIST '73: repeats the passage of line 60' \
    '76: repeats the passage of line 61'
printf '\nSome prose before it\n%s\nand more prose.\n\n## Two\n%s\n' \
    "$r60" "$h60" > "$dir/add"
lint_doc ALONE 1
doc_has ALONE '74: repeats the passage of line 49' \
    '78: repeats the passage of line 57'
printf '\n%s is how a paragraph starts with code.\n\n%s\n' '```x```' "$p60" \
    > "$dir/add"
lint_doc INLINE 1
doc_has INLINE '75: repeats the passage of line 3'
st=0
python3 -I TEST/lint.py --docs "$dir/LINT/NONE.MD" > "$dir/lint.out" 2>&1 ||
    st=$?
[ "$st" -eq 2 ] || bad_check "lint: --docs on a missing file: exit $st, not 2"
# lint_two CASE STATUS EXT - lint.py --docs on DOC.MD and CASE.EXT, a copy
# of $dir/add, must exit with STATUS; its report in $dir/lint.out.
lint_two() {
    cp "$dir/add" "$dir/LINT/$1.$3"
    st=0
    python3 -I TEST/lint.py --docs "$dir/LINT/DOC.MD" "$dir/LINT/$1.$3" \
        > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --docs, $1: exit $st, not $2"
}
printf '# Two\n\n%s\n\n%s\n%s\n%s\n' "$p60" "$k1" "$k2" "$k3" > "$dir/add"
lint_two TWODOC 1 MD
doc_has TWODOC "3: repeats the passage of $dir/LINT/DOC.MD line 3" \
    "5: lines 5-7 repeat $dir/LINT/DOC.MD lines 15-18"
printf '%s\n' "$p60" > "$dir/add"
lint_two TEXT 0 TXT
printf '%s\n%s\n' "$k1" "$k2" > "$dir/LINT/END.MD"
printf '%s\n\n%s\n%s\n%s\n' "$k3" "$k1" "$k2" "$k3" > "$dir/LINT/NEXT.MD"
python3 -I TEST/lint.py --docs "$dir/LINT/END.MD" "$dir/LINT/NEXT.MD" \
    > "$dir/lint.out" 2>&1 ||
    bad_check "lint: --docs: a run read on from one document into the next"

# lint_ref CASE STATUS TEXT - lint.py, run in REF, --docs on DOC/X.md and
# CASE.md (empty unless written first), and --refs on CASE.C, a comment
# of TEXT, must exit with STATUS, and report one problem when it fails.
mkdir "$dir/LINT/REF" "$dir/LINT/REF/DOC"
cat > "$dir/LINT/REF/DOC/X.md" <<'EOF'
# X

## First part (`X.C`)

**Bold label.** A paragraph after a bold label, before (Second, below),
(First part, Second, below) and (its rows: Second, below).

| Row label | a row (First part, above) |

## The `-x` switch

```
## Fenced
```

## Second
EOF
lint_ref() {
    printf '/* %b */\n' "$3" > "$dir/LINT/REF/$1.C"
    [ -f "$dir/LINT/REF/$1.md" ] || : > "$dir/LINT/REF/$1.md"
    st=0
    lint=$PWD/TEST/lint.py
    (cd "$dir/LINT/REF" && python3 -I "$lint" \
        --docs DOC/X.md "$1.md" --refs "$1.C") > "$dir/lint.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --refs, $1: exit $st, not $2"
    [ "$st" -eq 0 ] || grep -q "^lint: 1 document problem(s)\$" \
        "$dir/lint.out" || bad_check "lint: --refs, $1: not 1 problem"
}
printf 'See DOC/X.md,\n* Nothing like a label.\n' > "$dir/LINT/REF/GOOD.md"
lint_ref GOOD 0 'See `DOC/X.md` (First part), DOC/X.md, "Bold label",
 * (`DOC/X.md`,\n * Row label and Second), DOC/X.md in "First part (`X.C`)",
 * DOC/X.md, DOC/X.md (Second), DOC/X.md (The -x switch), DOC/X.md,
 * Second and "Bold label", [`DOC/X.md`](DOC/X.md) (Second), "Second" in
 * DOC/X.md, (Bold label of `DOC/X.md`), DOC/X.md, 2 KB, and (`DOC/X.md`,
 * First part, Bold label, Row label, Second, The -x switch, First part,
 * Bold label, Row label and Second), (Not one of DOC/X.md but prose).'
for c in 'NODOC:DOC/Y.md' 'NOLABEL:DOC/X.md (Third part)' \
        'NOSECOND:DOC/X.md, Second and Third' 'QUOTED:DOC/X.md, "First"' \
        'WRAPPED:(`DOC/X.md`,\n * Third part)' 'WORD:DOC/X.md, Seconds' \
        'IN:DOC/X.md has in "Third"' 'FENCED:DOC/X.md (Fenced)' \
        'PAREN:DOC/X.md (Second part)' 'LEAD:"Third" in DOC/X.md' \
        'OF:(Third of `DOC/X.md`)' 'QUOTE2:DOC/X.md, Second and "Third"' \
        'LINK:[`DOC/X.md`](DOC/X.md) (Third)'; do
    lint_ref "${c%%:*}" 1 "${c#*:}"
done
for c in 'SIDE:(Top, below)' 'PATH:(Top, Nowhere, above)' \
        'LEADIN:(its rows: Top, below)'; do
    printf '## Top\n\n%s\n' "${c#*:}" > "$dir/LINT/REF/${c%%:*}.md"
    lint_ref "${c%%:*}" 1 ''
    grep -qF "${c%%:*}.md:3: no " "$dir/lint.out" ||
        bad_check "lint: --refs, ${c%%:*}: a label of its own not refused"
done

# lint_names CASE STATUS TEXT - lint.py --names, the client INVNEW of the
# programs INV and INVNEW, on a document holding TEXT, must exit with
# STATUS.
lint_names() {
    printf '%s\n' "$3" > "$dir/LINT/N_$1.MD"
    st=0
    python3 -I TEST/lint.py --names INVNEW 'INV INVNEW' \
        "$dir/LINT/N_$1.MD" > "$dir/lint.out" 2>&1 || st=$?
    [ "$st" -eq "$2" ] || bad_check "lint: --names, $1: exit $st, not $2"
}
lint_names GOOD 0 'INV.EXE, INV.MSG and `INVNEW` of RETROINV, INV_FREEKB'
lint_names MODULE 0 'INVNEW links `INVMOD.C` and INVHDR.H'
lint_names WORDS 0 'INVNEW: INVALID input, INVENTORY'
lint_names NONEW 1 'INV.EXE alone'
for c in OLD:INVOLD.EXE OLDHLP:INVOLD.HLP OLDMOD:INVOLD.H,INVOLD; do
    n=${c%%:*}
    lint_names "$n" 1 "INVNEW.EXE, once ${c#*:}"
    grep -q "N_$n.MD:1: INVOLD is no program of the build" "$dir/lint.out" ||
        bad_check "lint: --names, $n: INVOLD not reported"
done

# lint_decl CASE STATUS TEXT - lint.py on a header D_CASE/X.H holding
# TEXT (printf %b), with the headers already in its directory, must exit
# with STATUS.
lint_decl() {
    mkdir -p "$dir/LINT/D_$1"
    printf '%b' "$3" > "$dir/LINT/D_$1/X.H"
    st=0
    python3 -I TEST/lint.py "$dir/LINT/D_$1/X.H" > "$dir/lint.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$2" ] ||
        bad_check "lint: incomplete struct, $1: exit $st, not $2"
}

fwd='typedef struct fwd fwd;\n'
lint_decl FNPTR 1 "$fwd"'typedef int (*fn)(const fwd *f);\n'\
'struct fwd { int a; };\n'
grep -q 'X.H:2: const pointer to incomplete struct fwd' "$dir/lint.out" ||
    bad_check "lint: incomplete struct, FNPTR: line 2 not reported"
lint_decl PROTO 1 'struct fwd;\nint f(const struct fwd *p);\n'
lint_decl POST 1 "$fwd"'int f(fwd const *p);\n'
lint_decl MEMBER 1 "$fwd"'struct s { int (*cb)(const fwd *p); };\n'
lint_decl TYPEDEF 1 "$fwd"'typedef const fwd *cfwd;\n'
lint_decl AFTER 0 "$fwd"'struct fwd { int a; };\nint f(const fwd *p);\n'
lint_decl OTHER 0 "$fwd"'typedef struct { const fwd *p; } s;\n'\
'static const fwd *g = (const fwd *)0;\n#define C(p) ((const fwd *)(p))\n'\
'int f(void *v)\n{\n    return ((const fwd *)v) != 0;\n}\n'
mkdir -p "$dir/LINT/D_BESIDE" "$dir/LINT/D_NAMED"
printf 'struct fwd { int a; };\n' > "$dir/LINT/D_BESIDE/Y.H"
lint_decl BESIDE 0 "$fwd"'typedef int (*fn)(const fwd *f);\n'
printf 'typedef struct fwd fwd;\n' > "$dir/LINT/D_NAMED/Y.H"
lint_decl NAMED 1 'int f(const fwd *p);\n'

# lint_init CASE STATUS OPTION TEXT - lint.py with OPTION (none when
# empty) on an I_CASE/X.C holding TEXT (printf %b) must exit with STATUS.
lint_init() {
    mkdir -p "$dir/LINT/I_$1"
    printf '%b' "$4" > "$dir/LINT/I_$1/X.C"
    st=0
    python3 -I TEST/lint.py $3 "$dir/LINT/I_$1/X.C" > "$dir/lint.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$2" ] ||
        bad_check "lint: block initializer, $1: exit $st, not $2"
}
lint_init FUNC 1 '' 'int f(void)\n{\n    int n = 0;\n\n    return n;\n}\n'
grep -q 'X.C:3: initializer in a block-scope declaration' "$dir/lint.out" ||
    bad_check "lint: block initializer, FUNC: line 3 not reported"
lint_init LOOP 1 --compat 'void f(int k)\n{\n    int i;\n\n'\
'    for (i = 0; i < k; i++) {\n        char *p = 0;\n\n        g(p);\n'\
'    }\n}\n'
lint_init WIN 1 --win 'void f(void)\n{\n    const rcol *c = t[1];\n}\n'
lint_init PAREN 1 '' 'void f(void)\n{\n    int (*fn)(void) = g;\n\n'\
'    fn();\n}\n'
lint_init PP 0 '' '#define OPEN {\nint g = 1;\n#define BODY(x) \\\n'\
'    { int n = (x); }\n'
lint_init GOOD 0 '' 'int g = 1;\nstatic const int t[2] = { 1, 2 };\n'\
'struct s { int a; int b; };\nint f(int k, int *p, struct s *q)\n{\n'\
'    static int calls = 0;\n    static const char k_x[] = "x";\n'\
'    enum { A = 1, B = 2 };\n    int i;\n    int n;\n\n'\
'    n = k == 1;\n    for (i = 0; i <= k; i++)\n        n += i != 2;\n'\
'    if (n >= 3)\n        n *= 2;\n    else\n        n = 0;\n'\
'    while ((n = k - 1) > 9)\n        k--;\n    switch (n = k) {\n'\
'    case A:\n        n = 1;\n        break;\n    default:\n'\
'        break;\n    }\n    do\n        n = 2;\n    while (n < 0);\n'\
'    *p = n;\n    p[1] = n;\n    q->a = n;\n    calls++;\n'\
'    if (k == 7)\n        n = 1;\n'\
'    return n == k_x[0] + calls + t[0] + g + A + B;\n}\n'\
'int h(int n)\n{\n    return n = 1;\n}\n'

# scripts STATUS LIST... - lint.py --scripts on $sd with the LISTs must
# exit with STATUS; its report in $dir/scr.out.
scripts() {
    want=$1
    shift
    st=0
    python3 -I TEST/lint.py --scripts "$sd" "$@" > "$dir/scr.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$want" ] || bad_check "lint --scripts $*: exit $st, not $want"
}

sd=$dir/SCR
mkdir -p "$sd/KEYS" "$sd/GOLDEN/SCR" "$sd/GOLDEN/PRT"
for p in SCRE SCRR; do
    {
        echo 'const script th_scripts[] = {'
        for n in 1 2 3; do
            last=NULL
            [ "$n" -ne 2 ] || last='"a{"'
            flags=0
            [ "$p$n" != SCRE3 ] || flags=SF_MONO
            echo "    { AD, { C }, $flags, 0, 0, $last },"
            : > "$sd/KEYS/$p$n.KEY"
            : > "$sd/GOLDEN/SCR/$p$n.SNC"
        done
        echo '};'
    } > "$sd/T_$p.C"
done
echo 'static const char k_f[] = "RPT1.PRN";' > "$sd/T_RPT.C"
: > "$sd/GOLDEN/PRT/RPT1.PRN"
scripts 0 'SCRE1.KEY SCRE2.KEY SCRE3.KEY' 'K/SCRR1.KEY K/SCRR2.KEY K/SCRR3.KEY'
scripts 0 'SCRE1.KEY SCRE2.KEY SCRE3.KEY' --mono 'G/SCRE3.SNM G/SCRE3.SNC' ''
scripts 1 'SCRE1.KEY SCRE2.KEY SCRE3.KEY' --mono '' ''
grep -q 'T_SCRE.C: lacks SCRE3.SNM' "$dir/scr.out" ||
    bad_check "lint --scripts: a mono list without SCRE3.SNM not reported"
scripts 1 'SCRE1.KEY SCRE2.KEY SCRE3.KEY' --mono 'SCRE3.SNM SCRE3.SNM' ''
grep -q 'T_SCRE.C: SCRE3.SNM 2 times' "$dir/scr.out" ||
    bad_check "lint --scripts: a mono list naming SCRE3.SNM twice not reported"
scripts 1 'SCRE1.KEY SCRE2.KEY SCRE3.KEY' --mono 'SCRE3.SNM' 'SCRR1.SNM'
grep -q 'T_SCRR.C: SCRR1.SNM, the golden of no SF_MONO row' "$dir/scr.out" ||
    bad_check "lint --scripts: a mono list naming SCRR1.SNM not reported"
scripts 1 'SCRE1.KEY SCRE2.KEY SCRE3.KEY SCRR1.KEY'
[ "$(grep -c 'without their key script' "$dir/scr.out")" -eq 1 ] &&
    grep -q '1: 2 row(s) of .*T_SCRR.C without their key script, first SCRR2' \
        "$dir/scr.out" || bad_check "lint --scripts: SCRR1 alone not one line"
scripts 1 'SCRE1.KEY SCRE9.KEY'
grep -q '1: 2 row(s) of .*T_SCRE.C without their key script, first SCRE2' \
    "$dir/scr.out" && grep -q 'SCRE9.KEY has no row' "$dir/scr.out" ||
    bad_check "lint --scripts: missing rows and a stray key not reported"
: > "$sd/GOLDEN/PRT/RPT2.PRN"
scripts 1 'SCRE1.KEY SCRE2.KEY SCRE3.KEY'
grep -q 'RPT2.PRN: compared by no test' "$dir/scr.out" &&
    ! grep -q 'RPT1.PRN' "$dir/scr.out" ||
    bad_check "lint --scripts: report goldens not told apart"

stack=$(sed -n 's/^unsigned _stklen = \([0-9][0-9]*\);.*/\1/p' \
    SRC/COMPAT/CDOS.C)
nearheap=$(sed -n 's/^#define CMEM_NEARHEAP *\([0-9]*\).*/\1/p' \
    SRC/COMPAT/COMPAT.H)
tcpheap=$(sed -n 's/^#define CMEM_TCPHEAP *\([0-9]*\).*/\1/p' \
    SRC/COMPAT/COMPAT.H)
invheap=$(sed -n 's/^#define CMEM_INVHEAP *\([0-9]*\).*/\1/p' \
    SRC/COMPAT/CMEMHI.C)
remheap=$(sed -n 's/^#define CMEM_REMHEAP *\([0-9]*\).*/\1/p' \
    SRC/COMPAT/CMEMHR.C)

# seg_off BYTE - the paragraph:offset address of BYTE past 0100:0000.
seg_off() {
    printf '%04X:%04X' $((0x100 + $1 / 16)) $(($1 % 16))
}

# map FMT MODULES A B PAD - write $dir/T.MAP, a map in the format FMT
# (ow, wl41 or tlink) whose DGROUP holds a piece of A bytes and, PAD
# bytes after its end, one of B bytes; MODULES names the objects that
# the map shows linked beside the program's own (CMEM, CMEMHN, CMEMHT,
# CMEMHI, CMEMHR).
map() {
    a1=$(seg_off 0)
    a2=$(seg_off $(($3 + $5)))
    case $1 in
    ow)
        printf 'Segment  Class  Group  Address  Size\n=======\n\n'
        printf 'X_TEXT CODE AUTO 0000:0000 00000010\n'
        printf '_DATA DATA DGROUP %s %08X\n' "$a1" "$3"
        printf '_BSS BSS DGROUP %s %08X\n\n' "$a2" "$4"
        for m in $2; do
            printf 'Module: OBJ/%s.OBJ(OBJ/%s.OBJ)\n' "$m" "$m"
        done
        ;;
    wl41)
        printf 'Segment  Group  Class  Address  Size\n\n'
        printf 'X_TEXT CODE 0000:0000 0010\n'
        printf '_DATA DGROUP DATA %s %04X\n' "$a1" "$3"
        printf '_BSS DGROUP BSS %s %04X\n\n' "$a2" "$4"
        for m in $2; do
            printf '0000:0000  _x OBJ\\M\\%s.OBJ(%s)\n' "$m" "$m"
        done
        ;;
    tlink)
        printf ' Start  Stop   Length Name     Class\n'
        printf ' 00000H 0000FH 00010H X_TEXT   CODE\n\n'
        printf 'Detailed map of segments\n\n'
        printf '%s %04X C=DATA S=_DATA G=DGROUP M=x.c ACBP=48\n' "$a1" "$3"
        printf '%s %04X C=BSS S=_BSS G=DGROUP M=x.c ACBP=48\n' "$a2" "$4"
        for m in $2; do
            printf '0000:0000 0000 C=CODE S=X G=(none) M=%s.c ACBP=28\n' \
                "$(echo "$m" | tr A-Z a-z)"
        done
        ;;
    *)
        bad_check "map: format $1"
        ;;
    esac > "$dir/T.MAP"
}

# mapchk STATUS ARGS... - mapchk.sh with ARGS on $dir/T.MAP must exit
# with STATUS; its report in $dir/map.out.
mapchk() {
    want=$1
    shift
    st=0
    sh TEST/mapchk.sh "$@" "$dir/T.MAP" > "$dir/map.out" 2>&1 || st=$?
    [ "$st" -eq "$want" ] ||
        bad_check "mapchk $* on $(head -n 1 "$dir/T.MAP"): exit $st, not $want"
}

for f in ow wl41 tlink; do
    map $f "CMEM CMEMHN" 100 100 0
    mapchk 0
    map $f "CMEM" 100 100 0
    mapchk 1
    map $f "CMEM CMEMHN CMEMHT" 100 100 0
    mapchk 1
    map $f "CMEM CMEMHT" 100 100 0
    mapchk 0 -v
    grep -q "near heap $tcpheap," "$dir/map.out" ||
        bad_check "mapchk: $f: CMEMHT.C's heap is not $tcpheap"
    map $f "CMEM CMEMHI CMEMHR" 100 100 0
    mapchk 1
    for h in "CMEMHI $invheap" "CMEMHR $remheap"; do
        map $f "CMEM ${h% *}" 100 100 0
        mapchk 0 -v
        oheap=$nearheap
        [ $f != tlink ] || oheap=${h#* }
        grep -q "near heap $oheap," "$dir/map.out" ||
            bad_check "mapchk: $f: ${h% *}.C's heap is not $oheap"
    done
done
budget=$((65536 - stack - nearheap))
map tlink "CMEM CMEMHN" $((budget - 100)) 100 0
mapchk 0 -n $budget
mapchk 1 -n $((budget + 1))
mapchk 1 -n $((budget - 1))
map tlink "" 20000 10000 0
mapchk 1
mapchk 0 -f
map tlink "" $((65536 - stack - 100)) 100 0
mapchk 0 -f
map tlink "" $((65536 - stack - 100)) 101 0
mapchk 1 -f
map tlink "" $((65536 - stack - 116)) 100 16
mapchk 0 -f
map tlink "" $((65536 - stack - 116)) 100 17
mapchk 1 -f
map ow "" $((65536 - stack - 116)) 100 17
mapchk 1 -f
map ow "CMEM" $((65536 - 8192 - 100)) 100 0
mapchk 1 -f
mapchk 0 -f -w 8192
mapchk 1 -f -w 8193

# toolarge STATUS - WC65/toolarge.sh on the list and the build tree in
# $dir/TL must exit with STATUS; its report in $dir/tl.out.
toolarge() {
    st=0
    sh WC65/toolarge.sh "$dir/TL/LIST.TXT" "$dir/TL" > "$dir/tl.out" 2>&1 ||
        st=$?
    [ "$st" -eq "$1" ] || bad_check "toolarge: exit $st, not $1"
}

tl=$dir/TL
mkdir -p "$tl/SRC/X" "$tl/SRC/COMPAT"
: > "$tl/SRC/X/PROG.C"
: > "$tl/SRC/COMPAT/T_DOS.C"
printf '# a comment\nPROG.OBJ:okfn\nPROG.OBJ:gone\nNOTBUILT.OBJ:nb\n' \
    > "$tl/LIST.TXT"
printf 'CC1\n' > "$tl/CC.LST"
too=' too large: may affect optimizations\r\n'
printf "Procedure 'okfn'$too PROG.OBJ\r\nProcedure 'tfn'$too T_DOS.OBJ\r\n" \
    > "$tl/CC1.LOG"
toolarge 0
grep -q 'PROG.OBJ:gone is no longer' "$dir/tl.out" &&
    ! grep -q 'okfn\|tfn\|NOTBUILT' "$dir/tl.out" ||
    bad_check "toolarge: listed, test, stale, unbuilt functions not told apart"
printf "Procedure 'newfn'$too PROG.OBJ\r\n" >> "$tl/CC1.LOG"
toolarge 1
grep -q 'PROG.OBJ:newfn is too large' "$dir/tl.out" ||
    bad_check "toolarge: an unlisted function not named"

[ "$fails" -eq 0 ] || exit 1
rm -rf "$dir"
echo "T_CHK: ok"
