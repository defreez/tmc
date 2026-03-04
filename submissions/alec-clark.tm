states:
  [qA, qR,
   init, shift_a, shift_b, go_start,
   find_a, add_c,
   find_b, eat_c, check_more,
   reset, verify]

input_alphabet: [a, b]
tape_alphabet_extra: ['$', X, Y, c, d]

start_state: init
accept_state: qA
reject_state: qR

delta:

# -------------------------------------------------
# Shift input right and place left marker
# -------------------------------------------------
  init:
    a: [shift_a, '$', R]
    b: [shift_b, '$', R]
    _: [find_a, '$', S]

  shift_a:
    a: [shift_a, a, R]
    b: [shift_b, a, R]
    _: [go_start, a, L]

  shift_b:
    a: [shift_a, b, R]
    b: [shift_b, b, R]
    _: [go_start, b, L]

  go_start:
    a: [go_start, a, L]
    b: [go_start, b, L]
    X: [go_start, X, L]
    Y: [go_start, Y, L]
    c: [go_start, c, L]
    d: [go_start, d, L]
    '$': [find_a, '$', R]
    _: [go_start, _, L]

# -------------------------------------------------
# Find next unmarked a
# -------------------------------------------------
  find_a:
    '$': [find_a, '$', R]
    X: [find_a, X, R]
    Y: [find_a, Y, R]
    c: [find_a, c, R]
    d: [find_a, d, R]
    a: [add_c, X, R]
    b: [verify, b, S]
    _: [verify, _, S]

# -------------------------------------------------
# Add one counter tick
# -------------------------------------------------
  add_c:
    a: [add_c, a, R]
    b: [add_c, b, R]
    X: [add_c, X, R]
    Y: [add_c, Y, R]
    c: [add_c, c, R]
    d: [add_c, d, R]
    _: [find_b, c, L]

# -------------------------------------------------
# Match each counter with one b
# -------------------------------------------------
  find_b:
    c: [find_b, c, L]
    d: [find_b, d, L]
    X: [find_b, X, L]
    Y: [find_b, Y, L]
    b: [eat_c, Y, R]
    '$': [qR, '$', S]
    a: [qR, a, S]

  eat_c:
    Y: [eat_c, Y, R]
    X: [eat_c, X, R]
    a: [eat_c, a, R]
    b: [eat_c, b, R]
    d: [eat_c, d, R]
    c: [check_more, d, R]
    _: [qR, _, S]

# -------------------------------------------------
# If more c remain, keep matching
# -------------------------------------------------
  check_more:
    d: [check_more, d, R]
    X: [check_more, X, R]
    Y: [check_more, Y, R]
    b: [check_more, b, R]
    c: [find_b, c, L]
    _: [reset, _, L]

# -------------------------------------------------
# Reset used ticks for next round (d → c)
# -------------------------------------------------
  reset:
    d: [reset, c, L]
    c: [reset, c, L]
    Y: [reset, Y, L]
    X: [reset, X, L]
    b: [reset, b, L]
    a: [reset, a, L]
    '$': [find_a, '$', R]

# -------------------------------------------------
# Final verification
# -------------------------------------------------
  verify:
    '$': [verify, '$', R]
    X: [verify, X, R]
    Y: [verify, Y, R]
    _: [qA, _, S]
    a: [qR, a, S]
    b: [qR, b, S]
    c: [qR, c, S]
    d: [qR, d, S]