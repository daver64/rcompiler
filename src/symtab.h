#pragma once

#include "support.h"

typedef enum {
    TYPE_VOID,
    TYPE_INT,
    TYPE_CHAR,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_STRUCT
} TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type *base;        // pointee/element type for POINTER/ARRAY
    int array_length;         // element count for ARRAY
    struct StructDef *struct_def; // for STRUCT
} Type;

typedef struct StructField {
    char name[64];
    Type *type;
    int offset;
    struct StructField *next;
} StructField;

typedef struct StructDef {
    char name[64];
    StructField *fields;
    int size;
    int align;
    struct StructDef *next;
} StructDef;

typedef enum {
    SYM_GLOBAL_VAR,
    SYM_LOCAL_VAR,
    SYM_PARAM,
    SYM_FUNCTION
} SymbolKind;

typedef struct Symbol {
    char name[64];
    Type *type;
    SymbolKind kind;
    int offset;        // stack offset (rbp-relative, negative) for locals/params
    int scope_level;
    int param_count;
    Type *param_types[6];
    struct Symbol *next;
} Symbol;

// Basic type singletons.
Type *type_void();
Type *type_int();
Type *type_char();
Type *type_float();
Type *type_double();
Type *type_pointer_to(Type *base);
Type *type_array_of(Type *base, int length);
Type *type_struct(StructDef *def);
int type_size(Type *t);
int type_align(Type *t);

// Struct tag namespace, separate from the symbol table.
StructDef *struct_define(char *name);
StructDef *struct_lookup(char *name);
StructField *struct_add_field(StructDef *def, char *name, Type *type);
StructField *struct_find_field(StructDef *def, char *name);
void struct_finalize(StructDef *def);

// Symbol table: scoped stack of declarations.
void symtab_init();
void symtab_enter_scope();
void symtab_exit_scope();
void symtab_begin_function();     // resets the local stack-frame offset counter
int symtab_frame_size();          // bytes needed for locals/params of current function
Symbol *symtab_declare_global(char *name, Type *type);
Symbol *symtab_declare_function(char *name, Type *return_type);
void symtab_set_param_types(Symbol *sym, int count, Type **types);
Symbol *symtab_declare_local(char *name, Type *type);
Symbol *symtab_declare_param(char *name, Type *type);
Symbol *symtab_lookup(char *name);
