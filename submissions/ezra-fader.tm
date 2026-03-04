
states: [start, loop_start,
         max_erase,
         0a_counted, 1a_counted, 2a_counted, 3a_counted, 4a_counted, 5a_counted, 6a_counted, 7a_counted, 8a_counted, 9a_counted,
         mark_1b, mark_2b, mark_3b, mark_4b, mark_5b, mark_6b, mark_7b, mark_8b, mark_9b,
         1b_left, 2b_left, 3b_left, 4b_left, 5b_left, 6b_left, 7b_left, 8b_left, 9b_left,
         rewind, clear_cache,
         accept_check, accept, reject]
input_alphabet: [a, b]
tape_alphabet_extra: [x, \, 1, 2, 3, 4, 5, 6, 7, 8, 9, d, _]
start_state: start
accept_state: accept
reject_state: reject

delta:
  start:
    a: [1a_counted, x, R]
    b: [reject, b, S]
    _: [accept, _, S]

  loop_start:
    \ : [1a_counted, x, R]
  
  0a_counted:
    a: [1a_counted, 1, R]
    \: [1a_counted, x, R]
  1a_counted:
    a: [2a_counted, 2, R]
    \: [2a_counted, 2, R]
    b: [mark_1b, b, R]
  2a_counted:
    a: [3a_counted, 3, R]
    \: [3a_counted, 3, R]
    b: [mark_2b, b, R]
  3a_counted:
    a: [4a_counted, 4, R]
    \: [4a_counted, 4, R]
    b: [mark_3b, b, R]
  4a_counted:
    a: [5a_counted, 5, R]
    \: [5a_counted, 5, R]
    b: [mark_4b, b, R]
  5a_counted:
    a: [6a_counted, 6, R]
    \: [6a_counted, 6, R]
    b: [mark_5b, b, R]
  6a_counted:
    a: [7a_counted, 7, R]
    \: [7a_counted, 7, R]
    b: [mark_6b, b, R]
  7a_counted:
    a: [8a_counted, 8, R]
    \: [8a_counted, 8, R]
    b: [mark_7b, b, R]
  8a_counted:
    a: [9a_counted, 9, R]
    \: [9a_counted, 9, R]
    b: [mark_8b, b, R]
  9a_counted:
    a: [1a_counted, 1, R]
    \: [1a_counted, 1, R]
    b: [max_erase, b, L]

  # max count is always of the form:
    #a: [1a_counted, 1, R]
    #\: [1a_counted, 1, R]
    #b: [max_erase, b, L] # this must be added to the max number state

  max_erase:
    9: [mark_9b, \, R]

  # go on a question to mark n b's (go to end of tape first)
  mark_1b:
    b: [mark_1b, b, R]
    \: [mark_1b, \, R]
    _: [1b_left, _, L]
    d: [1b_left, _, L]
  mark_2b: # should only see b's in this state, go to end of tape
    b: [mark_2b, b, R]
    \: [mark_2b, \, R]
    _: [2b_left, _, L]
    d: [2b_left, _, L]
  mark_3b:
    b: [mark_3b, b, R]
    \: [mark_3b, \, R]
    _: [3b_left, _, L]
    d: [3b_left, _, L]
  mark_4b:
    b: [mark_4b, b, R]
    \: [mark_4b, \, R]
    _: [4b_left, _, L]
    d: [4b_left, _, L]
  mark_5b:
    b: [mark_5b, b, R]
    \: [mark_5b, \, R]
    _: [5b_left, _, L]
    d: [5b_left, _, L]
  mark_6b:
    b: [mark_6b, b, R]
    \: [mark_6b, \, R]
    _: [6b_left, _, L]
    d: [6b_left, _, L]
  mark_7b:
    b: [mark_7b, b, R]
    \: [mark_7b, \, R]
    _: [7b_left, _, L]
    d: [7b_left, _, L]
  mark_8b:
    b: [mark_8b, b, R]
    \: [mark_8b, \, R]
    _: [8b_left, _, L]
    d: [8b_left, _, L]
  mark_9b:
    b: [mark_9b, b, R]
    \: [mark_9b, \, R]
    _: [9b_left, _, L]
    d: [9b_left, _, L]
    

  # now mark the b's, from right to left
  9b_left:
    b: [8b_left, d, L]
  8b_left:
    b: [7b_left, d, L]
  7b_left:
    b: [6b_left, d, L]
  6b_left:
    b: [5b_left, d, L]
  5b_left:
    b: [4b_left, d, L]
  4b_left:
    b: [3b_left, d, L]
  3b_left:
    b: [2b_left, d, L]
  2b_left:
    b: [1b_left, d, L]
  1b_left:
    b: [accept_check, d, L]

  rewind:
    b: [rewind, b, L]
    \: [rewind, \, L]
    1: [clear_cache, \, L]
    2: [clear_cache, \, L]
    3: [clear_cache, \, L]
    4: [clear_cache, \, L] 
    5: [clear_cache, \, L] # max always has [mark_maxb, \ R]
    6: [clear_cache, \, L]
    7: [clear_cache, \, L]
    8: [clear_cache, \, L]
    9: [mark_9b, \, R]
    x: [0a_counted, x, R]

  clear_cache: # erase all values before the 2 that correspond to this one
    1: [clear_cache, \, L]
    2: [clear_cache, \, L]
    3: [clear_cache, \, L]
    4: [clear_cache, \, L] # max always has [mark_maxb, \, R]
    5: [clear_cache, \, L]
    6: [clear_cache, \, L]
    7: [clear_cache, \, L]
    8: [clear_cache, \, L]
    9: [mark_9b, \, R]
    x: [loop_start, x, R]

  accept_check:
    x: [accept, x, S]
    b: [rewind, b, L]
    1: [rewind, 1, L]
    \: [rewind, \, L]