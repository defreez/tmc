# triangle_large.txt

Stress test suite for the triangular number language A = { a^n b^m | m = T(n) = n(n+1)/2 }.

Complements hw3a_public.txt (41 cases): 82 cases here, 41 accept + 41 reject. Combined total is 117 unique cases (6 strings overlap between the two files).

## Test case design

41 values of n, exponentially spaced from 1 to 3400:

    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 21, 26, 32, 39,
    48, 58, 71, 88, 107, 131, 161, 197, 242, 297, 363, 445, 546, 669, 819,
    1004, 1230, 1508, 1848, 2264, 2775, 3400

For each n, two test cases:

- **Accept**: a^n b^T(n) (exact triangular number of b's)
- **Reject**: a^n b^m where m = T(n) +/- n/2 (wrong number of b's, randomly too many or too few)

The largest string is n=3400, T(3400) = 5,782,700, so |w| = 5,786,100 characters.

## Why n maxes out at 3400

The sizing assumes the **naive O(n^3) TM** from test_triangular.cpp / triangular.tm. That TM works by marking one `a` at a time, then for each marked `a`, scanning back and marking one `b` per previous `a`. This is O(n) passes of O(n) work each, applied n times = O(n^3).

At ~1M simulated steps/sec (measured throughput of our C++ simulator), the budget for a 24-hour run is ~86 billion steps. The cubic cost of the largest case alone is 3400^3 ~ 39 billion. Total estimated cost across all 82 cases is ~86 billion steps, which fills the budget.

**This cap is specific to the naive TM.** A quadratic TM would allow n up to ~200,000 in the same time budget. A better algorithm means this test suite becomes easy and a larger one is needed.

## File size

~35 MB. The file is large because the largest accept string is 5.8M characters.

## How to run

```bash
./build/tmc --bench tests/fixtures/triangle_large.txt examples/triangular.tm
```

## Regenerating

```bash
python3 scripts/gen_triangle_large.py
```
