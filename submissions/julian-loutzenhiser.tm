states: [init, shift_a, shift_b, scan_to_start, main_loop, findB_A, reset, findB_Y, seekAnchor, check, qAccept, qReject]
input_alphabet: [a, b]
tape_alphabet_extra: [x, y, B, $, _]
start_state: init
accept_state: qAccept
reject_state: qReject

delta:
  init:
    a: [shift_a, $, R]
    b: [shift_b, $, R]
    _: [main_loop, $, R]

  shift_a:
    a: [shift_a, a, R]
    b: [shift_b, a, R]
    _: [scan_to_start, a, L]

  shift_b:
    a: [shift_a, b, R]
    b: [shift_b, b, R]
    _: [scan_to_start, b, L]

  scan_to_start:
    a: [scan_to_start, a, L]
    b: [scan_to_start, b, L]
    $: [main_loop, $, R]

  main_loop:
    a: [findB_A, x, R]
    y: [findB_Y, x, R]
    _: [qAccept, _, S]
    b: [qReject, b, S]
    x: [qReject, x, S]
    B: [qReject, B, S]
    $: [qReject, $, S]

  findB_A:
    a: [findB_A, a, R]
    B: [findB_A, B, R]
    b: [reset, B, R]
    x: [qReject, x, S]
    y: [qReject, y, S]
    _: [qReject, _, S]
    $: [qReject, $, S]

  reset:
    a: [reset, a, L]
    x: [reset, y, L]
    y: [reset, y, L]
    b: [reset, b, L]
    B: [reset, B, L]
    $: [main_loop, $, R]
    _: [check, _, L]

  findB_Y:
    a: [findB_Y, a, R]
    y: [findB_Y, y, R]
    B: [findB_Y, B, R]
    b: [seekAnchor, B, L]
    x: [qReject, x, S]
    _: [qReject, _, S]
    $: [qReject, $, S]

  seekAnchor:
    a: [seekAnchor, a, L]
    x: [main_loop, x, R]
    y: [seekAnchor, y, L]
    b: [seekAnchor, b, L]
    B: [seekAnchor, B, L]
    $: [qReject, $, S]
    _: [qReject, _, S]

  check:
    B: [check, B, L]
    y: [check, y, L]
    x: [qAccept, x, S]
    a: [qReject, a, S]
    b: [qReject, b, S]
    $: [qReject, $, S]
    _: [qReject, _, S]
