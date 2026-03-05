states: [
  qS,qA,qR,qLoop1,qLoop2,qLoop3,qLoop4,qLoop5,qLoop6,qMark,qFindB,qFindX,qShiftToB,qFlip,qShift,qShiftX,qShiftZ,qShiftS,qShiftV,qShiftQ,qShiftP,qProcess,qCheck
]

input_alphabet: [a,b]
tape_alphabet_extra: [X,Y,Z,_,Q,S,V,P]

start_state: qS
accept_state: qA
reject_state: qR

delta:
  qS:
    a: [qMark,S,R]
    b: [qR,b,S]
  qMark:
    a: [qMark,X,R]
    b: [qFindX,b,L]

  qFindB:
    b: [qFindX,b,L]
    X: [qFindB,X,R]
    Z: [qFindB,Z,R]
    S: [qFindB,S,R]
    V: [qProcess,V,S]
    Q: [qProcess,Q,R]
    P: [qFindB,P,R]
    
    
  qFindX:
    X: [qShiftToB,Z,R]
    V: [qShiftToB,Q,R]
    Z: [qFindX,Z,L]
    S: [qShiftToB,V,R]
    Q: [qProcess,Q,R]
    P: [qProcess,P,R]

  qShiftToB:
    Z: [qShiftToB,Z,R]
    X: [qShiftToB,X,R]
    b: [qShift,b,L]

  qProcess:
    Z: [qFlip,P,R]
    V: [qFlip,Q,R]
    P: [qProcess,P,R]
    X: [qFindB,X,R]

  qFlip:
    Z: [qFlip,X,R]
    b: [qFindB,b,L]
    P: [qFlip,P,R]
    _: [qCheck,_,L]
    
  qShift:
    S: [qShiftS,_,R]
    Q: [qShiftQ,_,R]
    P: [qShiftP,Y,R]
    X: [qShiftX,Y,R]
    Z: [qShiftZ,Y,R]
    Y: [qShift,Y,L]
    V: [qShiftV,_,R]
    _: [qFindB,_,R]

  qShiftX:
    ?: [qShift,X,L]

  qShiftZ:
    ?: [qShift,Z,L]

  qShiftS:
    ?: [qShift,S,L]

  qShiftV:
    ?: [qShift,V,L]

  qShiftQ:
    ?: [qShift,Q,L]
  
  qShiftP:
    ?: [qShift,P,L]
    
  qCheck:
    P: [qCheck,P,L]
    Q: [qA,Q,S]