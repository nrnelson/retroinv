"""ASMCHK.PY - check machine code kept as a C byte array against its source.

usage: python3 -I TEST/ASMCHK.PY ASM CFILE ARRAY [NAME...]

Code that must run in the data segment (an interrupt handler or a driver
upcall) is kept in a C file as the initializer of a byte array, beside a
readable nasm source.  ASM is assembled with "nasm -f bin", and its bytes
must be exactly the elements of the initializer of ARRAY in CFILE
("ARRAY[] = { ... };", the bound optional).  Each element is an integer
constant expression: decimal, hexadecimal (0x) or octal literals, with
or without a u or l suffix, the names of the object-like #define
constants of CFILE and of the headers it includes by quoted names from
its own directory (CFILE's own definition of a name taking precedence),
parentheses and the operators + - * ~ & | ^ << >>; it is
evaluated on unbounded integers, as C evaluates it when nothing
overflows, and must lie in 0..255.  Each NAME must be a constant of ASM
(an equ) and such a #define with the same value, such as the offset
of an operand that the C code fills in at run time.

Exit status: 0 when they match, 1 when they differ, 2 on a usage error,
or when a file cannot be read, assembled or parsed.
"""
import ast
import os
import re
import subprocess
import sys
import tempfile

LITERAL = re.compile(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b')
OPS = (ast.Add, ast.Sub, ast.Mult, ast.BitAnd, ast.BitOr, ast.BitXor,
       ast.LShift, ast.RShift, ast.USub, ast.UAdd, ast.Invert)


class CheckError(Exception):
    pass


def strip_comments(text):
    """text without its C comments, each replaced by a blank."""
    return re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)


def literal(m):
    """The decimal form of the C integer literal m."""
    tok = m.group(1)
    if tok[:2] in ('0x', '0X'):
        return str(int(tok, 16))
    if len(tok) > 1 and tok[0] == '0':
        return str(int(tok, 8))
    return tok


def evaluate(expr, defines, seen=()):
    """The value of the C constant expression expr."""
    try:
        tree = ast.parse(LITERAL.sub(literal, expr.strip()), mode='eval')
    except SyntaxError:
        raise CheckError('not a constant expression: %s' % expr.strip())

    def value(node):
        if isinstance(node, ast.Expression):
            return value(node.body)
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name) and node.id in defines:
            if node.id in seen:
                raise CheckError('%s is defined by itself' % node.id)
            return evaluate(defines[node.id], defines, seen + (node.id,))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, OPS):
            return eval_op(node.op, value(node.operand))
        if isinstance(node, ast.BinOp) and isinstance(node.op, OPS):
            return eval_op(node.op, value(node.left), value(node.right))
        raise CheckError('not a constant expression: %s' % expr.strip())
    return value(tree)


def eval_op(op, a, b=None):
    """a op b, or op a when b is None."""
    return {ast.Add: lambda: a + b, ast.Sub: lambda: a - b,
            ast.Mult: lambda: a * b, ast.BitAnd: lambda: a & b,
            ast.BitOr: lambda: a | b, ast.BitXor: lambda: a ^ b,
            ast.LShift: lambda: a << b, ast.RShift: lambda: a >> b,
            ast.USub: lambda: -a, ast.UAdd: lambda: a,
            ast.Invert: lambda: ~a}[type(op)]()


def c_bytes(path, array):
    """The bytes of the initializer of array in the C file path, and the
    object-like #define constants of the headers it includes by quoted
    names from its own directory and of the file, whose own definitions
    replace the headers'."""
    with open(path, encoding='latin-1') as f:
        text = strip_comments(f.read())
    defines = {}
    sources = []
    for m in re.finditer(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', text,
                         re.M):
        header = os.path.join(os.path.dirname(path), m.group(1))
        if os.path.exists(header):
            with open(header, encoding='latin-1') as f:
                sources.append(strip_comments(f.read()))
    sources.append(text)
    for src in sources:
        for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)(?![\w(])(.*)$',
                             src, re.M):
            defines[m.group(1)] = m.group(2)
    m = re.search(r'\b%s\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}\s*;'
                  % re.escape(array), text)
    if not m:
        raise CheckError('%s: no initialized array %s' % (path, array))
    items = [e for e in m.group(1).split(',') if e.strip()]
    out = bytearray()
    for e in items:
        v = evaluate(e, defines)
        if not 0 <= v <= 255:
            raise CheckError('%s: %s: %s is %d, not a byte'
                             % (path, array, e.strip(), v))
        out.append(v)
    return bytes(out), defines


def nasm(src, out):
    """Assemble src into out: nasm's messages, or None when it succeeds."""
    try:
        r = subprocess.run(['nasm', '-f', 'bin', '-o', out, src],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        raise CheckError('cannot run nasm: %s' % e)
    if r.returncode == 0:
        return None
    return r.stdout.decode('latin-1').rstrip()


def assemble(path, names):
    """The bytes nasm makes of the source path, and the values of its
    constants names.  The source is assembled as it is, so that nasm's
    messages name it; the values come from a second run, of a file that
    includes it and appends each name as a dword."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'CHK.BIN')
        err = nasm(path, out)
        if err is not None:
            raise CheckError('%s: nasm failed:\n%s' % (path, err))
        with open(out, 'rb') as f:
            code = f.read()
        if not names:
            return code, {}
        src = os.path.join(tmp, 'CHK.ASM')
        with open(src, 'w') as f:
            f.write('%%include "%s"\n' % os.path.abspath(path))
            for name in names:
                f.write('dd %s\n' % name)
        if nasm(src, out) is not None:
            raise CheckError('not all of %s are constants of %s'
                             % (' '.join(names), path))
        with open(out, 'rb') as f:
            data = f.read()
    n = len(code)
    values = [int.from_bytes(data[n + 4 * i:n + 4 * i + 4], 'little',
                             signed=True) for i in range(len(names))]
    return code, dict(zip(names, values))


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    asm, cfile, array, names = argv[0], argv[1], argv[2], argv[3:]
    try:
        want, defines = c_bytes(cfile, array)
        got, values = assemble(asm, names)
        bad = []
        for name in names:
            if name not in defines:
                raise CheckError('%s: no #define %s' % (cfile, name))
            v = evaluate(defines[name], defines)
            if v != values[name]:
                bad.append('%s is %d in %s, %d in %s'
                           % (name, values[name], asm, v, cfile))
    except (OSError, CheckError) as e:
        sys.stderr.write('asmchk: %s\n' % e)
        return 2
    if got != want:
        i = next((k for k in range(min(len(got), len(want)))
                  if got[k] != want[k]), min(len(got), len(want)))
        bad.append('%s is %d bytes, %s %d; first difference at offset %d'
                   ' (%s, %s)'
                   % (asm, len(got), array, len(want), i,
                      '%02Xh' % got[i] if i < len(got) else 'end',
                      '%02Xh' % want[i] if i < len(want) else 'end'))
    for line in bad:
        sys.stderr.write('asmchk: %s\n' % line)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
