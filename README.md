# TMC - Turing Machine Compiler

TMC compiles high-level programs into Turing Machines. Write readable code with variables, loops, and arithmetic -- TMC generates a formal TM specification that can be simulated or exported to YAML.

## Quick Start

```bash
# Build
cmake -B build && cmake --build build

# Compile a program to YAML
./build/tmc examples/anbn.tmc

# Test on an input string
./build/tmc -t "aabb" examples/anbn.tmc

# Run tests
./build/tmc_tests
```

Requires CMake 3.14+ and a C++17 compiler.

## The Language

TMC supports two styles of programming.

### High-Level DSL

Write programs with variables, counting, loops, and comparisons. The compiler handles all the tape management.

```
# Decides: { a^n b^m | m = n*(n+1)/2 }
alphabet input: [a, b]

n = count(a)
sum = 0
for i in 1..n {
  sum = sum + i
}
return count(b) == sum
```

**Available constructs:**

- `count(sym)` -- count occurrences of a symbol in the input
- `var = expr` -- variable assignment
- `for var in start..end { body }` -- bounded loop
- `if expr { then } else { else }` -- conditional
- `return expr` -- accept if true, reject if false
- Arithmetic: `+`, `-`
- Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`

### Imperative Style

For direct control over the tape head, use scan/write/move operations.

```
# Decides: { a^n b^n }
alphabet input: [a, b]
markers: [A, B]

loop {
  scan right for [a, _]
  if _ {
    scan right for [b, _]
    if b { reject }
    accept
  }
  write A
  scan right for [b, _]
  if _ { reject }
  write B
  scan left for _
  right
}
```

**Available operations:**

- `scan left/right for [symbols]` -- move until reaching a listed symbol
- `write SYM` -- write a symbol at the current position
- `left` / `right` -- move the tape head
- `loop { body }` -- repeat until accept or reject
- `if SYM { then }` -- branch on the current tape symbol
- `accept` / `reject` -- halt

## CLI Usage

```
tmc [options] <source.tmc>
```

| Flag | Description |
|------|-------------|
| `-o <file>` | Output YAML to file (default: stdout) |
| `-t <string>` | Simulate TM on input string |
| `-v` | Verbose output (parsing, compilation stats) |
| `--no-opt` | Disable optimization passes |
| `--precompute <n>` | Precompute results for inputs up to length n |
| `--max-states <n>` | Limit number of generated states |
| `--max-symbols <n>` | Limit tape alphabet size |

## Output Format

TMC outputs YAML compatible with [Doty's TM simulator](https://morphett.info/turing/turing.html). The YAML includes states, alphabets, start/accept/reject states, and the full transition function.

## How It Works

```
Source (.tmc)
    |
    v
  Parser -----> AST / IR
    |
    v
  Compiler ---> Turing Machine (states + transitions)
    |
    v
  Optimizer --> Merged states, dead state elimination
    |
    v
  Output -----> YAML file or simulation result
```

The high-level compiler uses a multi-region tape layout:

```
[input] # [var0] # [var1] # ... # _
```

Variables are stored in unary (value 3 = `111`). All arithmetic is performed by copying and comparing these unary regions on the tape.

## Project Structure

```
include/tmc/   Public headers
src/           Implementation (parser, compilers, optimizer, simulator)
tests/         GoogleTest suite
examples/      Example .tmc programs
```

## Testing

Tests use oracle-based exhaustive verification: generate all strings up to a given length, run the compiled TM on each, and compare against a known-correct function.

```bash
./build/tmc_tests
```
