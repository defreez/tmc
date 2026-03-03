#!/usr/bin/env python3
"""Generate tests/fixtures/triangle_large.txt — stress test suite for the
triangular number language A = { a^n b^m | m = T(n) = n(n+1)/2 }.

82 test cases: 41 accept + 41 reject, with n values exponentially spaced
from 1 to 3400.  See tests/fixtures/triangle_large_readme.md for details.

Usage:
    python3 scripts/gen_triangle_large.py
"""

import sys
from pathlib import Path

N_VALUES = [
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 21, 26, 32, 39,
    48, 58, 71, 88, 107, 131, 161, 197, 242, 297, 363, 445, 546, 669, 819,
    1004, 1230, 1508, 1848, 2264, 2775, 3400,
]

# Sign of the reject offset for each n value (±n//2).
# Determined once and frozen so the script is fully deterministic.
REJECT_SIGNS = [
    -1, +1, +1, +1, -1, -1, -1, +1, +1, +1, +1, -1, +1, +1, -1, -1, +1,
    -1, -1, +1, -1, -1, +1, +1, -1, +1, +1, +1, -1, -1, -1, -1, -1, -1,
    +1, -1, -1, -1, -1, -1, -1,
]

OUTPUT_PATH = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "triangle_large.txt"


def triangular(n: int) -> int:
    return n * (n + 1) // 2


def main() -> None:
    assert len(N_VALUES) == 41
    assert len(REJECT_SIGNS) == 41

    lines: list[str] = [
        "# Triangular number large test suite",
        "# 41 accept + 41 reject = 82 cases",
        "# n range: 1 to 3400",
    ]

    for n, sign in zip(N_VALUES, REJECT_SIGNS):
        tn = triangular(n)
        offset = n // 2
        if offset == 0:
            offset = 1  # n=1: n//2 == 0 would duplicate the accept case

        # Accept: a^n b^T(n)
        lines.append("a" * n + "b" * tn)
        # Reject: a^n b^(T(n) + sign*offset)
        reject_m = tn + sign * offset
        assert reject_m >= 0, f"negative b count for n={n}"
        assert reject_m != tn, f"reject case equals accept for n={n}"
        lines.append("a" * n + "b" * reject_m)

    out = "\n".join(lines) + "\n"
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(out)

    total_chars = sum(len(l) for l in lines if not l.startswith("#"))
    print(f"Wrote {OUTPUT_PATH}")
    print(f"  {len(lines) - 3} test cases, {total_chars:,} total characters")
    print(f"  Largest accept: n={N_VALUES[-1]}, |w|={N_VALUES[-1] + triangular(N_VALUES[-1]):,}")


if __name__ == "__main__":
    main()
