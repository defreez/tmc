# TM that decides A = { a^n b^m | n >= 0 and m = T(n) }
# T(n) = n(n+1) / 2
#
# Tape symbols:
#   >  = left-end marker (written at position 0 during startup)
#   X  = consumed a (persists as round counter)
#   A  = X currently being processed in the inner loop
#   Y  = consumed b
#
# Algorithm (after shift):
#   Outer loop, round k: find next a, mark it X (now k X-markers total).
#   Inner loop: for each X on tape, temporarily mark it A, sweep right to
#   the first b, mark it Y, return to A, restore A->X, repeat.
#   This crosses off k b's in round k => total 1+2+...+n = T(n).
#   Reject if inner loop finds no b (too few). Reject if b remains after
#   all a's consumed (too many).
#
# Phases:
#   0. Shift    : q_init, q_carry_a, q_carry_b, q_rewind0   
#   1-7. Main   : q_outer_find, q_rewind_inner, q_inner_scan, q_mark_inner, q_return_inner, q_restore_inner, q_rewind_outer           
#   8. Verify   : q_verify                                  
#   Accept/Reject: qA, qR                             
#
states:
- q_init
- q_carry_a
- q_carry_b
- q_rewind0
- q_outer_find
- q_rewind_inner
- q_inner_scan
- q_mark_inner
- q_return_inner
- q_restore_inner
- q_rewind_outer
- q_verify
- qA
- qR
input_alphabet:
- a
- b
tape_alphabet_extra:
- X
- A
- Y
- '>'
- _
start_state: q_init
accept_state: qA
reject_state: qR
delta:
  q_init:
    a:
    - q_carry_a
    - '>'
    - R
    b:
    - q_carry_b
    - '>'
    - R
    _:
    - q_outer_find
    - '>'
    - R
  q_carry_a:
    a:
    - q_carry_a
    - a
    - R
    b:
    - q_carry_b
    - a
    - R
    _:
    - q_rewind0
    - a
    - L
  q_carry_b:
    a:
    - q_carry_a
    - b
    - R
    b:
    - q_carry_b
    - b
    - R
    _:
    - q_rewind0
    - b
    - L
  q_rewind0:
    a:
    - q_rewind0
    - a
    - L
    b:
    - q_rewind0
    - b
    - L
    '>':
    - q_outer_find
    - '>'
    - R
  q_outer_find:
    '>':
    - q_outer_find
    - '>'
    - R
    X:
    - q_outer_find
    - X
    - R
    a:
    - q_rewind_inner
    - X
    - L
    b:
    - q_verify
    - b
    - S
    Y:
    - q_verify
    - Y
    - S
    _:
    - qA
    - _
    - S
  q_rewind_inner:
    a:
    - q_rewind_inner
    - a
    - L
    X:
    - q_rewind_inner
    - X
    - L
    A:
    - q_rewind_inner
    - A
    - L
    Y:
    - q_rewind_inner
    - Y
    - L
    b:
    - q_rewind_inner
    - b
    - L
    '>':
    - q_inner_scan
    - '>'
    - R
  q_inner_scan:
    '>':
    - q_inner_scan
    - '>'
    - R
    X:
    - q_mark_inner
    - A
    - R
    A:
    - q_inner_scan
    - A
    - R
    a:
    - q_inner_scan
    - a
    - R
    Y:
    - q_inner_scan
    - Y
    - R
    b:
    - q_rewind_outer
    - b
    - L
    _:
    - q_rewind_outer
    - _
    - L
  q_mark_inner:
    a:
    - q_mark_inner
    - a
    - R
    X:
    - q_mark_inner
    - X
    - R
    A:
    - q_mark_inner
    - A
    - R
    Y:
    - q_mark_inner
    - Y
    - R
    b:
    - q_return_inner
    - Y
    - L
    _:
    - qR
    - _
    - S
  q_return_inner:
    a:
    - q_return_inner
    - a
    - L
    X:
    - q_return_inner
    - X
    - L
    Y:
    - q_return_inner
    - Y
    - L
    b:
    - q_return_inner
    - b
    - L
    A:
    - q_restore_inner
    - X
    - R
    '>':
    - qR
    - '>'
    - S
  q_restore_inner:
    '>':
    - q_inner_scan
    - '>'
    - R
    a:
    - q_inner_scan
    - a
    - R
    X:
    - q_mark_inner
    - A
    - R
    A:
    - q_inner_scan
    - A
    - R
    Y:
    - q_inner_scan
    - Y
    - R
    b:
    - q_rewind_outer
    - b
    - L
    _:
    - q_rewind_outer
    - _
    - L
  q_rewind_outer:
    a:
    - q_rewind_outer
    - a
    - L
    X:
    - q_rewind_outer
    - X
    - L
    A:
    - q_rewind_outer
    - A
    - L
    Y:
    - q_rewind_outer
    - Y
    - L
    b:
    - q_rewind_outer
    - b
    - L
    '>':
    - q_outer_find
    - '>'
    - R
  q_verify:
    '>':
    - q_verify
    - '>'
    - R
    X:
    - q_verify
    - X
    - R
    Y:
    - q_verify
    - Y
    - R
    a:
    - qR
    - a
    - S
    b:
    - qR
    - b
    - S
    _:
    - qA
    - _
    - S