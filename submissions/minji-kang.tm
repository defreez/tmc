states: [qStart, qCheckA, qCheckB, qInitBarGoEnd, qInitBarWrite, qGoLeftBoundary, qScanToBarForA, qAfterMarkA, qGoEndBlank, qAppend1, qSeekRightBlank, qScanCounterLeft, qEraseOneBLeft, qBackToRightBlank, qRestoreCounterRight, qBackToLeftBoundary, qFinalCheckRight, qA, qR]
input_alphabet: [a, b]
tape_alphabet_extra: [A, B, X, Y, '|', '1', T, _]
start_state: qStart
accept_state: qA
reject_state: qR

delta:
  qStart:
    '_': [qA, '_', S]
    'b': [qR, 'b', S]
    'a': [qCheckA, 'A', R]

  qCheckA:
    'a': [qCheckA, 'a', R]
    'b': [qCheckB, 'b', R]
    '_': [qInitBarGoEnd, '_', L]

  qCheckB:
    'b': [qCheckB, 'b', R]
    'a': [qR, 'a', S]
    '_': [qInitBarGoEnd, '_', L]

  qInitBarGoEnd:
    'A': [qInitBarGoEnd, 'A', R]
    'a': [qInitBarGoEnd, 'a', R]
    'b': [qInitBarGoEnd, 'b', R]
    '_': [qInitBarWrite, '|', R]

  qInitBarWrite:
    '_': [qGoLeftBoundary, '_', L]

  qGoLeftBoundary:
    'a': [qGoLeftBoundary, 'a', L]
    'b': [qGoLeftBoundary, 'b', L]
    'X': [qGoLeftBoundary, 'X', L]
    'Y': [qGoLeftBoundary, 'Y', L]
    '|': [qGoLeftBoundary, '|', L]
    '1': [qGoLeftBoundary, '1', L]
    'T': [qGoLeftBoundary, 'T', L]
    'A': [qScanToBarForA, 'A', R]
    '_': [qScanToBarForA, '_', R]

  qScanToBarForA:
    'X': [qScanToBarForA, 'X', R]
    'a': [qAfterMarkA, 'X', R] 
    'b': [qScanToBarForA, 'b', R]
    'Y': [qScanToBarForA, 'Y', R]
    '|': [qFinalCheckRight, '|', R] # 모든 a를 처리했다면 오른쪽으로 가며 남은 b 검사

  qAfterMarkA:
    'a': [qAfterMarkA, 'a', R]
    'b': [qAfterMarkA, 'b', R]
    'X': [qAfterMarkA, 'X', R]
    'Y': [qAfterMarkA, 'Y', R]
    '|': [qAfterMarkA, '|', R]
    '1': [qAfterMarkA, '1', R]
    '_': [qAppend1, '1', R]

  qAppend1:
    '_': [qSeekRightBlank, '_', L]

  qSeekRightBlank:
    '1': [qSeekRightBlank, '1', R]
    '_': [qScanCounterLeft, '_', L]

  qScanCounterLeft:
    '1': [qEraseOneBLeft, 'T', L] 
    'T': [qScanCounterLeft, 'T', L]
    '|': [qRestoreCounterRight, '|', R] 

  qEraseOneBLeft:
    '1': [qEraseOneBLeft, '1', L]
    'T': [qEraseOneBLeft, 'T', L]
    '|': [qEraseOneBLeft, '|', L]
    'Y': [qEraseOneBLeft, 'Y', L]
    'b': [qBackToRightBlank, 'Y', R]
    'X': [qR, 'X', S] 
    'A': [qR, 'A', S]

  qBackToRightBlank:
    'Y': [qBackToRightBlank, 'Y', R]
    '|': [qBackToRightBlank, '|', R]
    '1': [qBackToRightBlank, '1', R]
    'T': [qBackToRightBlank, 'T', R]
    '_': [qScanCounterLeft, '_', L]

  qRestoreCounterRight:
    'T': [qRestoreCounterRight, '1', R]
    '1': [qRestoreCounterRight, '1', R]
    '_': [qBackToLeftBoundary, '_', L]

  qBackToLeftBoundary:
    'a': [qBackToLeftBoundary, 'a', L]
    'b': [qBackToLeftBoundary, 'b', L]
    'X': [qBackToLeftBoundary, 'X', L]
    'Y': [qBackToLeftBoundary, 'Y', L]
    '|': [qBackToLeftBoundary, '|', L]
    '1': [qBackToLeftBoundary, '1', L]
    'T': [qBackToLeftBoundary, 'T', L]
    'A': [qScanToBarForA, 'A', R]
    '_': [qScanToBarForA, '_', R]

  # 10) 최종 검수: 오른쪽으로 가며 남아있는 b가 있는지 확인
  qFinalCheckRight:
    '1': [qFinalCheckRight, '1', R]
    'T': [qFinalCheckRight, 'T', R]
    'Y': [qFinalCheckRight, 'Y', R] # 이미 지워진 b 구역 통과
    'b': [qR, 'b', S]              # 만약 지워지지 않은 b가 남아있다면 Reject
    '_': [qA, '_', S]              # 테이프 끝까지 아무 문제 없으면 Accept