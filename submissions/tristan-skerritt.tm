states: [s, mbh, rwfa, fa, rwsv, sv, rwsv2, sv2, mbrw, mbu, conv, fcr, fc, qA, qR]

input_alphabet: [a, b]
tape_alphabet_extra: [h, p, q, r, u, _]
start_state: s
accept_state: qA
reject_state: qR

delta:

  s:
    a: [mbh,  h, R]
    b: [qR,   b, R]
    _: [qA,   _, R]
    h: [qR,   h, R]
    p: [qR,   p, R]
    q: [qR,   q, R]
    r: [qR,   r, R]
    u: [qR,   u, R]

  mbh:
    a: [mbh,  a, R]
    q: [mbh,  q, R]
    p: [mbh,  p, R]
    r: [mbh,  r, R]
    u: [mbh,  u, R]
    h: [mbh,  h, R]
    b: [rwfa, q, L]
    _: [qR,   _, R]

  rwfa:
    a: [rwfa, a, L]
    b: [rwfa, b, L]
    p: [rwfa, p, L]
    q: [rwfa, q, L]
    r: [rwfa, r, L]
    u: [rwfa, u, L]
    h: [fa,   h, R]
    _: [rwfa, _, L]

  fa:
    h: [fa,   h, R]
    p: [fa,   p, R]
    q: [fa,   q, R]
    r: [fa,   r, R]
    a: [rwsv, u, L]
    b: [fcr,  b, L]
    _: [fcr,  _, L]
    u: [qR,   u, R]

  rwsv:
    a: [rwsv, a, L]
    b: [rwsv, b, L]
    p: [rwsv, p, L]
    q: [rwsv, q, L]
    r: [rwsv, r, L]
    u: [rwsv, u, L]
    h: [sv,   h, R]
    _: [rwsv, _, L]

  sv:
    a: [sv,    a, R]
    p: [sv,    p, R]
    r: [sv,    r, R]
    q: [sv,    q, R]
    u: [sv,    u, R]
    h: [sv,    h, R]
    b: [rwsv2, q, L]
    _: [qR,    _, R]

  rwsv2:
    a: [rwsv2, a, L]
    b: [rwsv2, b, L]
    p: [rwsv2, p, L]
    q: [rwsv2, q, L]
    r: [rwsv2, r, L]
    u: [rwsv2, u, L]
    h: [sv2,   h, R]
    _: [rwsv2, _, L]

  sv2:
    h: [sv2,  h, R]
    r: [sv2,  r, R]
    q: [sv2,  q, R]
    u: [sv2,  u, R]
    a: [sv2,  a, R]
    p: [mbrw, r, R]
    b: [mbu,  b, L]
    _: [mbu,  _, L]

  mbrw:
    a: [mbrw,  a, R]
    p: [mbrw,  p, R]
    r: [mbrw,  r, R]
    q: [mbrw,  q, R]
    u: [mbrw,  u, R]
    h: [mbrw,  h, R]
    b: [rwsv2, q, L]
    _: [qR,    _, R]

  mbu:
    a: [mbu,  a, R]
    p: [mbu,  p, R]
    r: [mbu,  r, R]
    q: [mbu,  q, R]
    u: [mbu,  u, R]
    h: [mbu,  h, R]
    b: [conv, q, L]
    _: [qR,   _, R]

  conv:
    q: [conv,  q, L]
    p: [conv,  p, L]
    r: [conv,  p, L]
    u: [conv,  p, L]
    a: [conv,  a, L]
    b: [conv,  b, L]
    h: [rwfa,  h, R]
    _: [conv,  _, L]

  fcr:
    a: [fcr, a, L]
    b: [fcr, b, L]
    p: [fcr, p, L]
    q: [fcr, q, L]
    r: [fcr, r, L]
    u: [fcr, u, L]
    h: [fc,  h, R]
    _: [fcr, _, L]

  fc:
    h: [fc,  h, R]
    p: [fc,  p, R]
    q: [fc,  q, R]
    a: [qR,  a, R]
    b: [qR,  b, R]
    _: [qA,  _, R]
    r: [qR,  r, R]
    u: [qR,  u, R]