
states: [s, s1, s2, s3, s4, s5, t1, t2, t3, t35, t4, t5, t6, t7, t8, qA, qR]
input_alphabet: [a, b]
tape_alphabet_extra: [c, x, y, _]
start_state: s
accept_state: qA
reject_state: qR

delta:
  s:
    a: [s1, a, S]
    b: [qR, b, S]
    _: [qA, _, S]
  s1:
    a: [s2, c, R]
    b: [s3, b, R]
  s2:
    a: [s2, a, R]
    b: [s3, _, R]
    _: [s2, _, R]
  s3:
    a: [qR, a, S]
    b: [s3, b, R]
    _: [s4, b, L]
  s4:
    a: [s4, a, L]
    b: [s4, b, L]
    c: [s5, c, R]
    _: [s4, _, L]
  s5:
    a: [s2, c, R]
    b: [qR, b, S]
    _: [t1, _, S]
  t1:
    a: [t1, a, L]
    c: [t2, a, R]
    _: [t1, _, L]
  t2:
    a: [t2, a, R]
    x: [t2, x, R]
    _: [t3, x, L]
  t3:
    a: [t4, a, R]
    c: [t3, c, L]
    x: [t3, x, L]
    y: [t7, y, R]
    _: [t35, _, L]
  t35:
    a: [t4, a, R]
    c: [t35, c, L]
    x: [t35, x, L]
    y: [t4, y, R]
    _: [t35, _, L]
  t4:
    c: [t4, c, L]
    x: [t5, y, R]
    y: [t6, x, L]
    _: [t4, _, L]
  t5:
    c: [t5, c, R]
    b: [t3, c, L]
    x: [t5, x, R]
    _: [t5, _, R]
  t6:
    a: [t1, a, L]
    x: [t6, y, L]
    y: [t6, x, L]
  t7:
    c: [t8, c, R]
    x: [t5, y, R]
    y: [t6, x, L]
    _: [t4, _, L]
  t8:
    c: [t8, c, R]
    _: [qA, _, S]
