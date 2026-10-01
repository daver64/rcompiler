#pragma once

#include "support.h"
#include "symtab.h"

void codegen_init(FILE *out);
int codegen_new_label();
void emit(const char *fmt, ...);

// Result of parsing an expression: the C type it evaluates to, and whether
// %rax currently holds its ADDRESS (is_lvalue=1) or its VALUE (is_lvalue=0).
typedef struct {
    Type *type;
    int is_lvalue;
} ExprResult;

// Entry point: parses one assignment-level expression, emitting FASM (Intel
// syntax) that leaves the final value/address in rax as described above.
ExprResult parse_expr();

// Statements & control flow (Phase 4). A "return" statement jumps to the
// label set by codegen_set_return_label(); the caller (Phase 5) is
// responsible for placing that label at the function epilogue.
void codegen_set_return_label(int label);
void parse_statement();
void parse_block();

// Phase 5: whole-program parsing (function definitions/prototypes and global
// variable declarations). codegen_emit_globals() writes out the .data/.bss
// style declarations collected while parsing; call it once, inside a
// writable segment, after parse_translation_unit() has finished.
void parse_translation_unit();
void codegen_emit_globals();

// Emits `public`/`extrn` linkage directives (object_mode selects whether
// undefined-but-called functions become `extrn`); see codegen.c for details.
void codegen_emit_linkage(int object_mode);

// Phase 7: string literals seen during parsing are collected and written out
// (as byte lists) by codegen_emit_strings(), called alongside codegen_emit_globals().
void codegen_emit_strings();

// Floating point constants emitted as IEEE-754 bit representations.
void codegen_emit_floats();


