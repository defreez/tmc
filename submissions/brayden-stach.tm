# TM deciding A = { a^n b^m | n >= 0, m = T(n) = n(n+1)/2 }
#
# Extra tape symbols:
#   $ = left-end marker (written at pos 0, input shifted right to make room)
#   X = used-up 'a'
#   Y = used-up 'b'
#   c = counter tick (one added per round, never erased between rounds)
#   d = counter tick used THIS round (reset back to 'c' after each round)
#
# States:
#   init      - read pos 0, write '$', carry symbol rightward
#   shift_a   - carrying an 'a' rightward during startup shift
#   shift_b   - carrying a  'b' rightward during startup shift
#   go_start  - after shift done, scan left back to '$' then enter find_a
#   find_a    - scan right for next 'a', mark X. If none found, go verify.
#   go_add_c  - scan right to blank, write one 'c' (grows counter by 1)
#   find_b    - scan LEFT for next unmarked 'b', mark Y. If none, reject.
#   eat_c     - scan right for a 'c', mark it 'd' (used this round)
#   check_more_c - if another 'c' exists go back to find_b, else go reset_d
#   reset_d   - scan left converting d->c. Stop at '$', then go to find_a.
#   verify    - no more a's: scan right, accept if only X/Y/c then blank

states: [qA, qR, init, shift_a, shift_b, go_start, find_a, go_add_c, find_b, eat_c, check_more_c, reset_d, verify]
input_alphabet: [a, b]
tape_alphabet_extra: ['$', X, Y, c, d]
start_state: init
accept_state: qA
reject_state: qR

delta:

  # Read pos 0, write '$', carry original symbol one step right
  init:
    a: [shift_a, '$', R]
    b: [shift_b, '$', R]
    _: [find_a,  '$', S]    # empty input: write '$', stay, find_a sees '$' and skips right

  # Carrying 'a': write it here, read next symbol and keep shifting
  shift_a:
    a: [shift_a, a, R]      # write 'a', still carrying 'a' (next is also 'a')
    b: [shift_b, a, R]      # write 'a', now carrying 'b'
    _: [go_start, a, L]     # write 'a' at end, shift done, go back to start

  # Carrying 'b': write it here, read next symbol and keep shifting
  shift_b:
    a: [shift_a, b, R]      # write 'b', now carrying 'a'
    b: [shift_b, b, R]      # write 'b', still carrying 'b'
    _: [go_start, b, L]     # write 'b' at end, shift done, go back to start

  # Scan left until we see '$', then move right into find_a
  go_start:
    '$': [find_a, '$', R]
    a:   [go_start, a, L]
    b:   [go_start, b, L]
    X:   [go_start, X, L]
    Y:   [go_start, Y, L]
    c:   [go_start, c, L]
    d:   [go_start, d, L]
    _:   [go_start, _, L]

  # Scan right for next unmarked 'a'
  find_a:
    '$': [find_a, '$', R]
    X:   [find_a, X, R]
    a:   [go_add_c, X, R]   # found 'a': mark X, go add a counter tick
    b:   [verify, b, S]     # no more a's: go verify (stay to re-read symbol)
    Y:   [verify, Y, S]
    c:   [verify, c, S]
    _:   [verify, _, S]

  # Scan right to blank, write one 'c' to grow the counter
  go_add_c:
    a: [go_add_c, a, R]
    X: [go_add_c, X, R]
    b: [go_add_c, b, R]
    Y: [go_add_c, Y, R]
    c: [go_add_c, c, R]
    d: [go_add_c, d, R]
    _: [find_b,  c, L]     # write 'c', then scan left for a b to cross off

  # Scan LEFT for next unmarked 'b'
  find_b:
    X:   [find_b, X, L]
    Y:   [find_b, Y, L]
    d:   [find_b, d, L]
    c:   [find_b, c, L]
    b:   [eat_c, Y, R]      # found 'b': mark Y, go find a counter tick to match it
    '$': [qR, '$', R]       # hit left end with no b - reject (too few b's)
    a:   [qR, a, R]

  # Scan right for a 'c', mark it 'd' (used this round)
  eat_c:
    Y: [eat_c, Y, R]
    X: [eat_c, X, R]
    d: [eat_c, d, R]
    b: [eat_c, b, R]
    a: [eat_c, a, R]
    c: [check_more_c, d, R]  # mark tick used, check if more ticks remain
    _: [qR, _, R]            # no tick found - reject

  # Check whether any 'c' ticks are still available
  check_more_c:
    d: [check_more_c, d, R]
    Y: [check_more_c, Y, R]
    X: [check_more_c, X, R]
    c: [find_b, c, L]        # more ticks remain - go cross off another b
    _: [reset_d, _, L]       # no more ticks - round done, go reset d's back to c's

  # Scan LEFT converting d->c. Stop when we hit '$'.
  reset_d:
    d: [reset_d, c, L]       # convert used tick back to available
    c: [reset_d, c, L]
    Y: [reset_d, Y, L]
    X: [reset_d, X, L]
    b: [reset_d, b, L]
    a: [reset_d, a, L]
    '$': [find_a, '$', R]    # back at left end - start next round

  # No more a's: scan right and accept if tape is clean
  verify:
    '$': [verify, '$', R]
    X:   [verify, X, R]
    Y:   [verify, Y, R]
    c:   [verify, c, R]
    _:   [qA, _, R]          # only used symbols then blank - ACCEPT
    a:   [qR, a, R]          # leftover 'a' - reject
    b:   [qR, b, R]          # leftover 'b' - reject
    d:   [qR, d, R]


