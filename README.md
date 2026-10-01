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

`-c` produces a genuine relocatable ELF64 object (`.o`, inspectable with `file` / `objdump -t`).
Using FASM's symbol alias syntax (`public u_name as 'name'`, `extrn 'name' as _name`,
and `name = PLT _name`), symbols are exported and imported using standard unadorned
C names, and external function calls generate `R_X86_64_PLT32` relocations.
This makes object files fully compatible with Position-Independent Executables (PIE).

You can link `.o` files directly with `gcc` (or `ld` on your own OS) without needing `-no-pie`:

```sh
./compile -c myprog.c -o myprog.o
gcc myprog.o -o myprog
./myprog
```

## Using Standard C Library (libc) & External Libraries

Because standard system header files (such as `<stdio.h>` or `<stdlib.h>`) are tens of
thousands of lines long and rely on compiler-specific GCC/Clang extensions, `rcompiler`
does not parse standard glibc headers directly.

Instead, you simply **declare the prototypes** of the libc or third-party functions you
wish to call. When compiled with `-c` and linked with `gcc` (or your OS linker), standard
libraries are linked automatically.

### Common Libc Function Declarations

You can place these prototypes directly at the top of your `.c` file or put them in a
custom header (e.g. `#include "libc.h"`):

#### Memory Allocation (`<stdlib.h>`)
```c
void *malloc(int size);
void free(void *ptr);
void *calloc(int nmemb, int size);
void *realloc(void *ptr, int size);
```

#### Console I/O & Formatted Output (`<stdio.h>`)
```c
int puts(char *s);
int putchar(int c);
int getchar();
int printf(char *fmt, ...); // Supports integer, string, pointer, and float/double arguments
```

*Note on `printf` and varargs:* The x86-64 System V ABI requires the `%al` register to store
the count of floating-point arguments passed in SSE registers when calling variable-argument
functions. `rcompiler` calculates and sets this automatically, so `printf("%s: %f\n", label, val)`
formats floating-point values properly.

#### File I/O (`<stdio.h>`)
```c
void *fopen(char *path, char *mode);
int fclose(void *stream);
int fgetc(void *stream);
int fputc(int c, void *stream);
int fread(void *ptr, int size, int nmemb, void *stream);
int fwrite(void *ptr, int size, int nmemb, void *stream);
```

#### String & Memory Utilities (`<string.h>`)
```c
int strlen(char *s);
int strcmp(char *s1, char *s2);
char *strcpy(char *dest, char *src);
char *strcat(char *dest, char *src);
void *memset(void *s, int c, int n);
void *memcpy(void *dest, void *src, int n);
```

#### Math Library (`<math.h>`, link with `-lm`)
```c
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double pow(double x, double y);
double floor(double x);
double ceil(double x);
```

#### Process Control (`<stdlib.h>`)
```c
void exit(int status);
int system(char *command);
```

### Complete Libc Example

Here is a complete program demonstrating dynamic memory (`malloc`/`free`), string inspection (`strlen`),
formatted printing (`printf`), and floating-point math (`sqrt`):

```c
// demo_libc.c
int printf(char *fmt, ...);
void *malloc(int size);
void free(void *ptr);
int strlen(char *s);
double sqrt(double x);

int main() {
    char *greeting = "Hello, libc!";
    int len = strlen(greeting);

    // Allocate memory dynamically
    char *buffer = malloc(len + 1);
    int i;
    for (i = 0; i <= len; i = i + 1) {
        buffer[i] = greeting[i];
    }

    double val = 49.0;
    double root = sqrt(val);

    // Formatted output with strings, integers, and doubles
    printf("Message: %s (length %d)\n", buffer, len);
    printf("Square root of %f is %f\n", val, root);

    free(buffer);
    return 0;
}
```

To compile and link:
```sh
./compile -c demo_libc.c -o demo_libc.o
gcc demo_libc.o -lm -o demo_libc
./demo_libc
```

Output:
```
Message: Hello, libc! (length 12)
Square root of 49.000000 is 7.000000
```

### Builtin Runtime (Standalone Mode)

When compiling directly to a standalone executable (without `-c`), there is no libc
linked. Four lightweight syscall-based builtins are implemented in pure assembly and
automatically provided:

```c
int print(char *s);         // writes a null-terminated string to stdout
int print_int(int n);       // writes a (possibly negative) decimal integer
int print_float(float f);   // writes a 32-bit float (up to 6 decimal places)
int print_double(double d); // writes a 64-bit double (up to 6 decimal places)
```

## Inline Assembly (`asm { ... }`)

`rcompiler` supports verbatim Intel-syntax inline assembly using `asm { ... }` (as well as
`__asm { ... }` and `__asm__ { ... }`). Because the compiler targets FASM directly, you have
unrestricted access to any x86/x86-64 instructions, CPU control registers, and FASM directives.

This is particularly useful when developing an operating system (bootloader, kernel, interrupt
handlers, descriptor tables, port I/O, paging, and CPU state inspection):

### In Statement Scope
Inside function bodies, `asm { ... }` blocks can read and write function parameters and local
variables via their stack offsets relative to `rbp`:

```c
// Port I/O helper
void outb(int port, int val) {
    asm {
        ; port is at [rbp-4], val is at [rbp-8]
        mov edx, [rbp-4]
        mov eax, [rbp-8]
        out dx, al
    }
}

// Read CPU timestamp counter
int rdtsc_low() {
    int tsc = 0;
    asm {
        rdtsc
        mov [rbp-4], eax
    }
    return tsc;
}

// Disable and enable interrupts
void disable_interrupts() {
    asm { cli }
}
void enable_interrupts() {
    asm { sti }
}
```

### In Global / Top-Level Scope
`asm { ... }` blocks can also be placed at top level in a file to emit raw assembly routines,
interrupt service routines (ISRs), naked labels, segment/mode directives (like `use16` for
real mode bootloader code), or custom tables:

```c
asm {
    ; Naked interrupt handler
    my_isr:
        push rax
        push rbx
        ; ... handle interrupt ...
        pop rbx
        pop rax
        iretq
}
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
