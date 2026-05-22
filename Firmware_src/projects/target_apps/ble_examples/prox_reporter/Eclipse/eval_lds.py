#!/usr/bin/env python3
"""
Post-process a preprocessed GNU ld linker script, evaluating all constant
arithmetic expressions in MEMORY block ORIGIN and LENGTH fields.

Old ld versions (e.g. binutils bundled with GCC 5.4.1) cannot handle complex
bitwise expressions such as (x & (~3)) in MEMORY region specs, even though
they are legal in modern ld. This script pre-computes them to plain hex values.

Usage: python3 eval_lds.py <input.lds> > <output.lds>
"""
import re, sys

def safe_eval(expr):
    expr = expr.strip()
    # All constants in these linker scripts are integers; convert / to integer
    # division (//) so Python 3 doesn't produce floats that break bitwise ops.
    int_expr = re.sub(r'(?<!/)/(?!/)', '//', expr)
    try:
        val = eval(int_expr, {"__builtins__": {}})
        if isinstance(val, int):
            return hex(val & 0xFFFFFFFF)
    except Exception:
        pass
    return expr

content = open(sys.argv[1]).read()

# Evaluate ORIGIN = <expr>  (expression ends at the comma before LENGTH)
content = re.sub(
    r'(ORIGIN\s*=\s*)([^,\n]+)',
    lambda m: m.group(1) + safe_eval(m.group(2)),
    content
)

# Evaluate LENGTH = <expr>  (expression ends at end of line)
content = re.sub(
    r'(LENGTH\s*=\s*)([^\n]+)',
    lambda m: m.group(1) + safe_eval(m.group(2)),
    content
)

# Evaluate section VMA address expressions: "  SECTION_NAME <expr> (NOLOAD)"
# After preprocessing, macros like RET_MEM_BASE_ADDR expand to complex arithmetic
# that old ld cannot evaluate as a constant section address.
# The greedy (.+) backtracks to leave (NOLOAD)/(COPY) for the trailing group.
content = re.sub(
    r'(\n\s+\w+\s+)(\(.+\))(\s+\((?:NOLOAD|COPY)\))',
    lambda m: m.group(1) + safe_eval(m.group(2)) + m.group(3),
    content
)

sys.stdout.write(content)
