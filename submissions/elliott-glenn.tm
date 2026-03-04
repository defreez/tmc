states: [start, check_format_a, check_format_b, go_back, ping_pong, clear_a, find_last_b, clear_b, go_to_start, accept, reject]
input_alphabet: [a, b]
tape_alphabet_extra: [X, Y, Z, S, _]
start_state: start
accept_state: accept
reject_state: reject

delta:
  # --- Phase 1: Format Verification ---
  start:
    a: [check_format_a, S, R]   # Mark the first 'a' as S (Start Marker)
    b: [reject, b, R]
    _: [accept, _, S]           # n=0, T(n)=0

  check_format_a:
    a: [check_format_a, a, R]
    b: [check_format_b, b, R]
    _: [reject, _, S]

  check_format_b:
    b: [check_format_b, b, R]
    a: [reject, a, S]
    _: [go_back, _, L]

  go_back:
    a: [go_back, a, L]
    b: [go_back, b, L]
    S: [ping_pong, a, S]

  # --- Phase 2: pingpong ---
  
  ping_pong:
    a: [clear_a, X, R]
    Y: [clear_a, X, R]
    X: [accept, X, R]        
  
  clear_a:
    a: [clear_a, Z, R]
    Y: [clear_a, Z, R]
    b: [find_last_b, b, R]
    
  find_last_b:
    a: [find_last_b, Z, R] 
    Y: [find_last_b, Y, R] 
    b: [find_last_b, b, R]
    _: [clear_b, _, L]
    X: [clear_b, X, L]
    
  clear_b:
    b: [go_to_start, X, L]
    
  go_to_start:
    b: [go_to_start, b, L]
    a: [go_to_start, a, L]
    Y: [go_to_start, Y, L]
    Z: [find_last_b, Y, R] 
    X: [ping_pong, X, R]