# rcompiler

An experimental, cut-down C(ish) compiler, written by hand in C, targeting
real x86-64 machine code via [FASM](https://flatassembler.net/) (the flat
assembler, Intel syntax). It is **not** a production compiler — it implements
a small, pragmatic subset of C and exists mainly as a learning/experimentation
project for how a hand-written, single-pass compiler can go from source text
all the way down to a runnable ELF binary.

## What it is (and isn't)

- A hand-written lexer, recursive-descent parser, and code generator, all in
  plain C with no external parser/lexer-generator tools.
- Single-pass, Crenshaw-style: there's no separate AST — expressions and
  statements emit assembly directly as they're parsed.
- Targets **x86-64 Linux** (and portable to other x86-64 environments),
  emitting Intel-syntax assembly for **FASM 1.7x**, which is then invoked to
  produce a standalone ELF executable or a relocatable object file (`.o`).
- A genuinely useful subset of C: functions (with recursion), `int`/`char`/`float`/`double`
  types, pointers, arrays, structs, global variables, `if`/`else`/`while`/
  `for`/`break`/`continue`/`return`, string literals, and a simple
  preprocessor (`#include`, `#define`).
- Object files (`-c`) are standard ELF64 with proper symbol aliasing and PLT
  calls, making them linkable with standard `gcc` (PIE-compatible) or custom OS linkers.
- It is **not** a standards-compliant C compiler. There's no
  `union`/`enum`/`typedef`, no function-like macros or conditional
  compilation (`#ifdef`), no bitfields, and struct-by-value passing is only
  correct for structs up to 8 bytes. See [Limitations](#limitations).

## Why FASM?

Rather than emitting object code directly or shelling out to `as`/`ld`, this
compiler emits textual FASM assembly and calls the `fasm` binary to do the
actual assembling/linking. FASM was chosen because:

- It supports both a "flat" executable format (`format ELF64 executable 3`,
  handling ELF headers/segments for you) *and* a relocatable object format
  (`format ELF64`, using `section`/`public`/`extrn`), which map naturally onto
  this compiler's own executable-vs-object output modes.
- It uses Intel syntax, which is generally easier to read/write by hand than
  AT&T syntax.
- It's a single, dependency-free binary — no separate linker invocation is
  needed for standalone executables.

You'll need `fasm` installed and on your `PATH` to actually produce runnable
binaries (`sudo apt install fasm`, or build from https://flatassembler.net/).

## Building the compiler itself

```sh
make
```

This builds `./compile` from everything in `src/`. `make clean` removes it
(and the intermediate `out.asm`). `make test` builds and runs the test suite
(see [Testing](#testing)).

## Using the compiler

```sh
# Compile straight to an executable
./compile examples/example.c -o example.elf
./example.elf

# Compile to a relocatable object file instead (no entry point/runtime bundled)
./compile -c file.c -o file.o
```

If no input file is given, source is read from stdin. Internally, `./compile`
writes an intermediate `out.asm` file in the current directory and invokes
`fasm out.asm <output>` to produce the final binary.

### Object files (`-c`)

`-c` produces a genuine relocatable ELF64 object (inspectable with `file` /
`objdump -t`). Using FASM's symbol alias syntax (`public u_name as 'name'`,
`extrn 'name' as _name`, and `name = PLT _name`), all symbols are exported
and imported using standard unadorned C names, and external function calls
generate `R_X86_64_PLT32` relocations. This makes object files fully
compatible with Position-Independent Executables (PIE).

You can link `.o` files directly with `gcc` (or `ld` on your own OS) without
needing `-no-pie`:

```sh
./compile -c myprog.c -o myprog.o
gcc myprog.o -o myprog
./myprog
```

External libc functions (like `puts`, `printf`, `malloc`, `free`, etc.) can
be declared in your C source as prototypes and called directly when linked
with libc.

### Builtin runtime

There's no libc. Four tiny syscall-based builtins are always available to your
C code, implemented directly in hand-written asm and bundled into every
standalone executable:

```c
int print(char *s);       // writes a null-terminated string to stdout
int print_int(int n);     // writes a (possibly negative) decimal integer
int print_float(float f); // writes a 32-bit float
int print_double(double d); // writes a 64-bit double
```

### Preprocessor

A minimal preprocessor runs before lexing:

- `#include "file"` / `#include <file>` — inlines the named file (quoted
  paths resolve relative to the including file's directory; angle-bracket
  paths try the current directory, then fall back to the including file's
  directory, since there's no real system include path).
- `#define NAME value` — simple object-like macro substitution (no
  function-like/parameterized macros), applied recursively so a macro's
  replacement text can reference other macros.
- Anything else (`#ifdef`, `#pragma`, ...) is silently ignored.

## Examples

[examples/example.c](examples/example.c) is a tour of everything currently
supported — functions, recursion, loops, pointers, arrays, structs, globals,
and string output:

```sh
make
./compile examples/example.c -o example.elf
./example.elf
```

## Testing

[tests/](tests/) contains a small fixture-based regression suite:
each `tests/cases/NAME.c` has a matching `NAME.out` (expected stdout) and
`NAME.exit` (expected exit code), captured from real, verified compiler runs.

```sh
make test
# or directly:
./tests/run_tests.sh
```

## Project layout

```
src/            the compiler itself
  support.c/h     lexer (tokenizer) + character classification
  preprocessor.c/h  #include / #define handling, runs before lexing
  symtab.c/h      types (int/char/pointer/array/struct) + scoped symbol table
  codegen.c/h     recursive-descent parser + FASM code generation
  main.c          CLI, build orchestration (invokes fasm)
examples/       example.c, a feature tour
tests/          fixture-based regression tests + shell harness
Makefile        builds ./compile from src/*.c
```

## Limitations

This is intentionally a small subset of C. Notably missing:

- Standalone executable mode has no standard library / libc linkage (uses
  built-in raw syscall `print`/`print_int` functions). For libc access, compile
  to an object file (`-c`) and link with `gcc` or your target OS libc.
- No `union`, `enum`, or `typedef`.
- No variadic functions, no bitfields.
- Function calls support at most 6 integer/pointer arguments and 8 float arguments
  (SysV register convention).
- No function-like macros or conditional compilation (`#ifdef`) in the preprocessor.
- Struct-by-value parameter/return passing is only correct for structs that
  fit in 8 bytes (pass structs by pointer for larger structs).
- No multi-file link orchestration built into the compiler driver — compile each
  file with `-c` and link using `gcc` or your custom linker.

## License

MIT — see [LICENSE](LICENSE).
