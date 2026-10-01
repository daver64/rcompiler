#include "symtab.h"

static int current_scope_level = 0;
static int local_frame_offset = 0;
static Symbol *symbol_head = NULL;
static StructDef *struct_head = NULL;

static int align_up(int n, int align)
{
    if(align <= 1)
    {
        return n;
    }
    return (n + align - 1) / align * align;
}

static Type *type_new(TypeKind kind)
{
    Type *t = malloc(sizeof(Type));
    t->kind = kind;
    t->base = NULL;
    t->array_length = 0;
    t->struct_def = NULL;
    return t;
}

Type *type_void()
{
    static Type *t = NULL;
    if(!t) t = type_new(TYPE_VOID);
    return t;
}

Type *type_int()
{
    static Type *t = NULL;
    if(!t) t = type_new(TYPE_INT);
    return t;
}

Type *type_char()
{
    static Type *t = NULL;
    if(!t) t = type_new(TYPE_CHAR);
    return t;
}

Type *type_float()
{
    static Type *t = NULL;
    if(!t) t = type_new(TYPE_FLOAT);
    return t;
}

Type *type_double()
{
    static Type *t = NULL;
    if(!t) t = type_new(TYPE_DOUBLE);
    return t;
}

Type *type_pointer_to(Type *base)
{
    Type *t = type_new(TYPE_POINTER);
    t->base = base;
    return t;
}

Type *type_array_of(Type *base, int length)
{
    Type *t = type_new(TYPE_ARRAY);
    t->base = base;
    t->array_length = length;
    return t;
}

Type *type_struct(StructDef *def)
{
    Type *t = type_new(TYPE_STRUCT);
    t->struct_def = def;
    return t;
}

int type_size(Type *t)
{
    switch(t->kind)
    {
        case TYPE_VOID: return 0;
        case TYPE_CHAR: return 1;
        case TYPE_INT: return 4;
        case TYPE_FLOAT: return 4;
        case TYPE_DOUBLE: return 8;
        case TYPE_POINTER: return 8;
        case TYPE_ARRAY: return type_size(t->base) * t->array_length;
        case TYPE_STRUCT: return t->struct_def->size;
    }
    return 0;
}

int type_align(Type *t)
{
    switch(t->kind)
    {
        case TYPE_VOID: return 1;
        case TYPE_CHAR: return 1;
        case TYPE_INT: return 4;
        case TYPE_FLOAT: return 4;
        case TYPE_DOUBLE: return 8;
        case TYPE_POINTER: return 8;
        case TYPE_ARRAY: return type_align(t->base);
        case TYPE_STRUCT: return t->struct_def->align;
    }
    return 1;
}

StructDef *struct_define(char *name)
{
    StructDef *def = malloc(sizeof(StructDef));
    strncpy(def->name, name, sizeof(def->name) - 1);
    def->name[sizeof(def->name) - 1] = '\0';
    def->fields = NULL;
    def->size = 0;
    def->align = 1;
    def->next = struct_head;
    struct_head = def;
    return def;
}

StructDef *struct_lookup(char *name)
{
    for(StructDef *d = struct_head; d; d = d->next)
    {
        if(strcmp(d->name, name) == 0)
        {
            return d;
        }
    }
    return NULL;
}

StructField *struct_add_field(StructDef *def, char *name, Type *type)
{
    StructField *f = malloc(sizeof(StructField));
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = '\0';
    f->type = type;
    f->next = NULL;

    int align = type_align(type);
    int offset = align_up(def->size, align);
    f->offset = offset;
    def->size = offset + type_size(type);
    if(align > def->align)
    {
        def->align = align;
    }

    if(!def->fields)
    {
        def->fields = f;
    }
    else
    {
        StructField *last = def->fields;
        while(last->next)
        {
            last = last->next;
        }
        last->next = f;
    }
    return f;
}

StructField *struct_find_field(StructDef *def, char *name)
{
    for(StructField *f = def->fields; f; f = f->next)
    {
        if(strcmp(f->name, name) == 0)
        {
            return f;
        }
    }
    return NULL;
}

void struct_finalize(StructDef *def)
{
    def->size = align_up(def->size, def->align);
}

void symtab_init()
{
    current_scope_level = 0;
    local_frame_offset = 0;
    symbol_head = NULL;
    struct_head = NULL;
}

void symtab_enter_scope()
{
    current_scope_level++;
}

void symtab_exit_scope()
{
    while(symbol_head && symbol_head->scope_level == current_scope_level)
    {
        Symbol *dead = symbol_head;
        symbol_head = symbol_head->next;
        free(dead);
    }
    current_scope_level--;
}

void symtab_begin_function()
{
    local_frame_offset = 0;
    symtab_enter_scope();
}

int symtab_frame_size()
{
    return align_up(local_frame_offset, 16);
}

static Symbol *symbol_new(char *name, Type *type, SymbolKind kind)
{
    Symbol *s = malloc(sizeof(Symbol));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->type = type;
    s->kind = kind;
    s->offset = 0;
    s->scope_level = current_scope_level;
    s->param_count = 0;
    for(int i = 0; i < 6; i++) s->param_types[i] = NULL;
    s->next = symbol_head;
    symbol_head = s;
    return s;
}

Symbol *symtab_declare_global(char *name, Type *type)
{
    return symbol_new(name, type, SYM_GLOBAL_VAR);
}

Symbol *symtab_declare_function(char *name, Type *return_type)
{
    return symbol_new(name, return_type, SYM_FUNCTION);
}

void symtab_set_param_types(Symbol *sym, int count, Type **types)
{
    if(!sym) return;
    sym->param_count = count > 6 ? 6 : count;
    for(int i = 0; i < sym->param_count; i++)
    {
        sym->param_types[i] = types[i];
    }
}

Symbol *symtab_declare_local(char *name, Type *type)
{
    Symbol *s = symbol_new(name, type, SYM_LOCAL_VAR);
    int align = type_align(type);
    local_frame_offset = align_up(local_frame_offset, align) + type_size(type);
    s->offset = -local_frame_offset;
    return s;
}

Symbol *symtab_declare_param(char *name, Type *type)
{
    Symbol *s = symbol_new(name, type, SYM_PARAM);
    int align = type_align(type);
    local_frame_offset = align_up(local_frame_offset, align) + type_size(type);
    s->offset = -local_frame_offset;
    return s;
}

Symbol *symtab_lookup(char *name)
{
    for(Symbol *s = symbol_head; s; s = s->next)
    {
        if(strcmp(s->name, name) == 0)
        {
            return s;
        }
    }
    return NULL;
}
