# TMC Virtual Machine Architecture

TMC compiles a high-level DSL to single-tape deterministic Turing Machines. The compiler operates as a virtual machine (VM) layered on top of the raw TM tape, providing structured memory and composable operations.

## Target Machine

- Single-tape deterministic Turing Machine (DTM)
- Left-bounded tape (Sipser model): cell 0 is leftmost, moving L from cell 0 stays in place
- Alphabet: input symbols + tape-only symbols + blank (`_`)
- Output: YAML compatible with the Doty/Automaton Simulator

## Tape Layout

```
> [input region] # [var0] # [var1] # [var2] # _ _ _ ...
^
cell 0 = left-end marker
```

- **Cell 0**: Always contains `>` (left-end marker). The head rests here between high-level operations.
- **Input region**: The original input string, starting at cell 1. Immutable across high-level operations (temporarily marked during `count`, but always restored).
- **`#` separators**: Delimit variable regions.
- **Variable regions**: Unary-encoded integers. Value 3 = `111`. Empty region = 0.

### Preamble

The Doty simulator places input starting at cell 0. The compiler emits a **shift-right preamble** as the very first TM states to create the left-end marker:

1. At cell 0, pick up current symbol into a carry state
2. Write `>` at cell 0, move right
3. Carry loop: write carried symbol, pick up displaced symbol, move right
4. When writing carried symbol to a blank cell: done — rewind to `>`, move right

```
Input "ab":

Before:  [a] b _          head=0
Step 1:  [>] b _          write >, carry a, move R
Step 2:  > [a] _          write a, carry b, move R
Step 3:  > a [b] _        write b, carry blank, done
Rewind:  > [a] b _        head=1, at first input char
```

For empty input: write `>` at cell 0 → tape is `> _`, head moves to cell 1 (blank).

## Memory Model

### Input (immutable)

The input string occupies the first region of the tape (cells 1..n). High-level operations may temporarily mark input symbols (e.g., `a` -> `A` during counting) but must restore them before returning. This guarantees that sequential operations see the same input.

### Variables (mutable unary regions)

Variables are named integers stored as unary strings in `#`-separated regions. Each variable gets a region index assigned at declaration time. Operations on variables:

- **Declare** (`n = ...`): Appends a new `#`-separated region to the tape.
- **Count** (`count(a)`): Scans input, marks each `a`, writes a `1` to the variable's region for each one found, then restores all marks.
- **Increment**: Appends a `1` to the end of a region.
- **Copy**: One-by-one marking transfer from source to destination region.
- **Compare**: One-to-one matching between two regions (mark pairs of `1`s, check which exhausts first).

## Head Position Contract

Every high-level operation enters and exits with the head at **cell 0** (the `>` left-end marker). This is enforced by `EmitRewindToStart()` which scans left until `>` is found, then stays (since `>` is at cell 0 and L from cell 0 stays in place, `>` always terminates the scan).

```
Entry:  head at cell 0
        |
        v
  > a a b b # 1 1 1 # _
  ^
  head here (on >)

Exit:   head at cell 0 (same)
```

This contract makes high-level operations composable. Any sequence of `let`, `if`, `loop` statements can be chained without worrying about head position.

## Two Operation Tiers

### High-Level Operations

These follow the VM contract (head at cell 0, input immutable):

| Operation | DSL Syntax | Behavior |
|-----------|-----------|----------|
| Let | `n = count(a)` | Declare variable, evaluate expression, store result |
| Inc | `inc i` | Append a `1` to variable's region |
| Append | `append i -> sum` | Non-destructive copy of source into destination |
| If-Eq | `if a == b { ... }` | Compare two variable regions for equality |
| Loop/Break | `loop { ... break }` | Infinite loop with break |
| Accept | `accept` | Halt and accept |
| Reject | `reject` | Halt and reject |

### Imperative Operations

These are raw tape operations with **no auto-rewind**. The head stays wherever the operation leaves it:

| Operation | DSL Syntax | Behavior |
|-----------|-----------|----------|
| Scan | `scan right for [b, _]` | Move head until stop symbol found |
| Write | `write X` | Overwrite current cell |
| Move | `left` / `right` | Move head one cell |
| Loop | `loop { ... }` | Infinite loop, exit via accept/reject/break |
| If-current | `if a { ... } else if b { ... }` | Branch on current cell symbol |

### Mixing Tiers

Imperative ops can precede high-level ops. The compiler auto-rewinds before the first high-level operation. Example pattern for structural check before counting:

```
alphabet input: [a, b]

# Imperative: structural check (a*b* form)
scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}

# High-level: counting (auto-rewind handles head position)
n = count(a)
m = count(b)
if n == m { accept }
reject
```

## Compilation Pipeline

```
DSL source
    |
    v
ParseHL() -----> Program AST
    |
    v
HLCompiler::Compile()
    |
    |--- SetupAlphabet()        # input + tape symbols + >
    |--- EmitPreamble()         # shift input right, write > at cell 0
    |--- CompileStmts()         # walk AST, emit TM fragments
    |       |
    |       |--- CompileLet()       # declare region, eval expr, rewind
    |       |--- CompileInc()       # insert 1 in region, rewind
    |       |--- CompileAppend()    # non-destructive copy, rewind
    |       |--- CompileIfEq()      # compare regions, rewind
    |       |--- CompileLoop()      # loop with break targets
    |       |--- CompileBreak()     # wire to loop exit
    |       |--- CompileScan()      # raw scan, no rewind
    |       |--- CompileMove()      # raw move, no rewind
    |       |--- CompileWrite()     # raw write, no rewind
    |       |--- CompileIfCurrent() # raw branch, no rewind
    |
    |--- Finalize()             # close alphabet, register states
    |
    v
TM (states, delta, start, accept, reject)
    |
    v
ToYAML() / Simulator
```

## Fragment Composition

Each `Compile*` method returns a `State` representing the exit state of the generated TM fragment. The next operation's entry connects to the previous operation's exit via unconditional transitions (read any symbol, write it back, stay, go to next state).

High-level operations emit a rewind epilogue:

```
[operation logic] --> EmitRewindToStart --> [next operation]
```

`EmitRewindToStart` generates:
1. Move left one cell (to start scanning)
2. Scan left until `>` found (L from cell 0 stays, so `>` always stops the scan)
3. Result: head at cell 0 on `>`

## Key Algorithms

### Count

Counts occurrences of a symbol in input, stores result as unary in a variable region.

1. Scan input left-to-right (starting from cell 1)
2. On finding target symbol: mark it (e.g., `a` -> `A`), scan to end of tape, write `1`, rewind to `>`, continue scanning
3. When input exhausted (hit `#` or `_`): enter restore phase
4. Restore: rewind to `>`, sweep right converting marks back to originals (`A` -> `a`)

### One-to-One Matching (Equality Check)

Compares two variable regions for equality.

1. Navigate to region A, find unmarked `1`, mark as `I`
2. Navigate to region B, find unmarked `1`, mark as `I`
3. Rewind to `>`, repeat
4. If region A exhausted: verify no unmarked `1`s remain in region B
5. If region B exhausted first: not equal
6. Restore: sweep both regions converting `I` -> `1`

### Mid-tape Insertion

Inserts a `1` into a non-last region, shifting subsequent data right.

1. Navigate to end of target region (the `#` separator)
2. Write `1`, pick up displaced `#`
3. Carry loop: write carried symbol, pick up next, move right
4. Stop when writing to blank
5. Rewind to `>`

### Non-destructive Append

Copies source region value into destination region without destroying source.

1. Navigate to source, find unmarked `1`, mark as `I`
2. Insert `1` into destination (via mid-tape insertion)
3. Rewind, repeat until source exhausted
4. Restore source: sweep `I` -> `1`

## Verification Strategy

Oracle-based exhaustive testing: generate all strings over the input alphabet up to length N, run each through both the compiled TM and a known-correct function, compare accept/reject decisions.

```cpp
auto inputs = AllStrings({'a', 'b'}, 10);
for (const auto& input : inputs) {
    bool oracle = IsAnBn(input);
    bool tm_result = sim.Run(input).accepted;
    EXPECT_EQ(oracle, tm_result);
}
```
