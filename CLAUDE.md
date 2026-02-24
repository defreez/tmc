# TMC - Claude Code Project Instructions

## Build

```bash
cmake -B build && cmake --build build
```

Requires CMake 3.14+ and a C++17 compiler. GoogleTest is fetched automatically.

## Test

```bash
./build/tmc_tests
```

## Run

```bash
./build/tmc examples/anbn.tmc                    # Print YAML to stdout
./build/tmc -o out.yaml examples/anbn.tmc         # Write to file
./build/tmc -t "aabb" examples/anbn.tmc           # Test input string
./build/tmc -v --no-opt examples/anbn.tmc         # Verbose, no optimization
```

## Project Structure

```
include/tmc/     Headers (public API)
  ir.hpp         TM data structures, high-level AST, imperative IR
  parser.hpp     Parser interface (ParseHL for DSL, Parse for IR)
  hlcompiler.hpp High-level DSL -> TM compiler
  codegen.hpp    Low-level IR -> TM compiler, YAML serialization
  optimizer.hpp  Optimization passes (state merging, dead elimination, precompute)
  simulator.hpp  TM execution engine

src/             Implementation
  main.cpp       CLI entry point
  parser.cpp     Lexer + recursive descent parser
  hlcompiler.cpp High-level compiler (unary tape layout)
  codegen.cpp    IR compiler + YAML output
  optimizer.cpp  Optimization passes
  simulator.cpp  Step-by-step TM simulator
  ir.cpp         TM struct utilities

tests/           GoogleTest test suite
examples/        Example .tmc programs
```

## Architecture

Two compilation paths:

- **High-level DSL**: `ParseHL()` -> `Program` AST -> `HLCompiler` -> `TM`
- **Low-level IR**: `Parse()` -> `IRProgram` -> `CompileIR()` -> `TM`

After compilation: `Optimize()` (optional) -> `ToYAML()` or `Simulator::Run()`

**Known limitation**: `main.cpp` only wires up the low-level IR path (`Parse()` + `CompileIR()`). The high-level DSL path (`ParseHL()` + `HLCompiler`) is only exercised through tests.

## Conventions

- C++17, `tmc::` namespace for all public symbols
- GoogleTest for testing; oracle-based exhaustive verification on small inputs
- Tape layout for high-level compiler: `[input]#[var0]#[var1]#...` with unary encoding
- Variables stored as repeated `1` symbols (value 3 = `111`)
- Markers use uppercase letters to track processed symbols

## DSL Syntax

**High-level** (parsed by `ParseHL`):
```
alphabet input: [a, b]
n = count(a)
sum = 0
for i in 1..n { sum = sum + i }
return count(b) == sum
```

**Imperative** (parsed by `Parse`):
```
alphabet input: [a, b]
markers: [A, B]
loop {
  scan right for [a, _]
  if _ { accept }
  write A
  scan right for [b, _]
  if _ { reject }
  write B
  scan left for _
  right
}
```
