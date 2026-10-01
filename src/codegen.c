#include "codegen.h"
#include <stdarg.h>

static FILE *out_file = NULL;
static int label_counter = 0;

// Prefix user C symbols so they can't collide with FASM reserved words
// (instruction mnemonics, register names, etc. can't be used as labels).
static const char *asm_name(const char *c_name)
{
    static char buf[1024];
    snprintf(buf, sizeof(buf), "u_%s", c_name);
    return buf;
}

void codegen_init(FILE *out)
{
    out_file = out;
    label_counter = 0;
}

int codegen_new_label()
{
    return label_counter++;
}

typedef struct StringLiteral {
    char label[32];
    char text[1024];
    struct StringLiteral *next;
} StringLiteral;

static StringLiteral *string_list = NULL;
static StringLiteral *string_list_tail = NULL;

// Registers a string literal for later emission and returns its asm label.
static const char *register_string_literal(const char *text)
{
    StringLiteral *s = malloc(sizeof(StringLiteral));
    snprintf(s->label, sizeof(s->label), "str_%d", codegen_new_label());
    strncpy(s->text, text, sizeof(s->text) - 1);
    s->text[sizeof(s->text) - 1] = '\0';
    s->next = NULL;
    if(!string_list)
    {
        string_list = s;
    }
    else
    {
        string_list_tail->next = s;
    }
    string_list_tail = s;
    return s->label;
}

void codegen_emit_strings()
{
    for(StringLiteral *s = string_list; s; s = s->next)
    {
        emit("%s: db ", s->label);
        size_t len = strlen(s->text);
        for(size_t i = 0; i < len; i++)
        {
            emit("%d,", (unsigned char)s->text[i]);
        }
        emit("0\n");
    }
}

typedef struct FloatLiteral {
    char label[32];
    double value;
    int is_float32;
    struct FloatLiteral *next;
} FloatLiteral;

static FloatLiteral *float_list = NULL;
static FloatLiteral *float_list_tail = NULL;

static const char *register_float_literal(double val, int is_float32)
{
    FloatLiteral *f = malloc(sizeof(FloatLiteral));
    snprintf(f->label, sizeof(f->label), "flt_%d", codegen_new_label());
    f->value = val;
    f->is_float32 = is_float32;
    f->next = NULL;
    if(!float_list)
    {
        float_list = f;
    }
    else
    {
        float_list_tail->next = f;
    }
    float_list_tail = f;
    return f->label;
}

void codegen_emit_floats()
{
    for(FloatLiteral *f = float_list; f; f = f->next)
    {
        if(f->is_float32)
        {
            float fval = (float)f->value;
            unsigned int bits;
            memcpy(&bits, &fval, sizeof(bits));
            emit("%s: dd 0x%08x\n", f->label, bits);
        }
        else
        {
            unsigned long long bits;
            memcpy(&bits, &f->value, sizeof(bits));
            emit("%s: dq 0x%016llx\n", f->label, bits);
        }
    }
}

// Tracks which C-level function names got a body emitted vs. were only
// called, so object-file mode can tell `public` from `extrn` symbols.
typedef struct NameList {
    char name[64];
    struct NameList *next;
} NameList;

static NameList *defined_functions = NULL;
static NameList *called_functions = NULL;

static int namelist_contains(NameList *list, const char *name)
{
    for(NameList *n = list; n; n = n->next)
    {
        if(strcmp(n->name, name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void namelist_add(NameList **list, const char *name)
{
    if(namelist_contains(*list, name))
    {
        return;
    }
    NameList *n = malloc(sizeof(NameList));
    strcpy(n->name, name);
    n->next = *list;
    *list = n;
}

void emit(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(out_file, fmt, args);
    va_end(args);
}

static void codegen_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "codegen error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static void expect_op(const char *op)
{
    if(token_type != TOKEN_OPERATOR || strcmp(token_text, op) != 0)
    {
        codegen_error("expected '%s' but got '%s'", op, token_text);
    }
    next_token();
}

static int is_op(const char *op)
{
    return token_type == TOKEN_OPERATOR && strcmp(token_text, op) == 0;
}

static int is_float_type(Type *t)
{
    return t && (t->kind == TYPE_FLOAT || t->kind == TYPE_DOUBLE);
}

// Converts value in accumulator (rax or xmm0) from from_type to to_type.
static void gen_cast(Type *from_type, Type *to_type)
{
    if(!from_type || !to_type || from_type->kind == to_type->kind)
    {
        return;
    }
    int from_flt = is_float_type(from_type);
    int to_flt = is_float_type(to_type);

    if(!from_flt && to_flt)
    {
        // int -> double/float
        if(to_type->kind == TYPE_FLOAT)
        {
            emit("    cvtsi2ss xmm0, rax\n");
        }
        else
        {
            emit("    cvtsi2sd xmm0, rax\n");
        }
    }
    else if(from_flt && !to_flt)
    {
        // double/float -> int
        if(from_type->kind == TYPE_FLOAT)
        {
            emit("    cvttss2si rax, xmm0\n");
        }
        else
        {
            emit("    cvttsd2si rax, xmm0\n");
        }
    }
    else if(from_flt && to_flt)
    {
        // float <-> double
        if(from_type->kind == TYPE_FLOAT && to_type->kind == TYPE_DOUBLE)
        {
            emit("    cvtss2sd xmm0, xmm0\n");
        }
        else if(from_type->kind == TYPE_DOUBLE && to_type->kind == TYPE_FLOAT)
        {
            emit("    cvtsd2ss xmm0, xmm0\n");
        }
    }
}

// If the result is currently an address, dereference it into a value (sized
// per the type), into rax (integers/pointers) or xmm0 (floats).
static void gen_load(ExprResult *r)
{
    if(!r->is_lvalue)
    {
        return;
    }
    if(r->type->kind == TYPE_ARRAY)
    {
        r->is_lvalue = 0; // arrays decay to their base address as a pointer value
        return;
    }
    if(r->type->kind == TYPE_FLOAT)
    {
        emit("    movss xmm0, dword [rax]\n");
    }
    else if(r->type->kind == TYPE_DOUBLE)
    {
        emit("    movsd xmm0, qword [rax]\n");
    }
    else
    {
        int size = type_size(r->type);
        if(size == 1)
        {
            emit("    movsx eax, byte [rax]\n");
            emit("    cdqe\n");
        }
        else if(size == 4)
        {
            emit("    mov eax, [rax]\n");
            emit("    cdqe\n");
        }
        else
        {
            emit("    mov rax, [rax]\n");
        }
    }
    r->is_lvalue = 0;
}

// Stores rax (integers) or xmm0 (floats) into the address held in addr_reg, sized per type.
static void gen_store(Type *type, const char *addr_reg)
{
    if(type->kind == TYPE_FLOAT)
    {
        emit("    movss dword [%s], xmm0\n", addr_reg);
    }
    else if(type->kind == TYPE_DOUBLE)
    {
        emit("    movsd qword [%s], xmm0\n", addr_reg);
    }
    else
    {
        int size = type_size(type);
        if(size == 1)
        {
            emit("    mov byte [%s], al\n", addr_reg);
        }
        else if(size == 4)
        {
            emit("    mov dword [%s], eax\n", addr_reg);
        }
        else
        {
            emit("    mov qword [%s], rax\n", addr_reg);
        }
    }
}

// Pushes the accumulator (rax or xmm0 depending on type) onto the stack.
static void gen_push(Type *type)
{
    if(is_float_type(type))
    {
        emit("    sub rsp, 8\n");
        if(type->kind == TYPE_FLOAT)
        {
            emit("    movss dword [rsp], xmm0\n");
        }
        else
        {
            emit("    movsd qword [rsp], xmm0\n");
        }
    }
    else
    {
        emit("    push rax\n");
    }
}

// Pops the stack into the primary accumulator (rax or xmm0 depending on type).
static void gen_pop_to_first(Type *type)
{
    if(is_float_type(type))
    {
        if(type->kind == TYPE_FLOAT)
        {
            emit("    movss xmm0, dword [rsp]\n");
        }
        else
        {
            emit("    movsd xmm0, qword [rsp]\n");
        }
        emit("    add rsp, 8\n");
    }
    else
    {
        emit("    pop rax\n");
    }
}

static ExprResult parse_assignment();
static ExprResult parse_logical_or();

static ExprResult parse_call(char *name)
{
    static const char *gp_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    static const char *xmm_regs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    expect_op("(");
    Symbol *fn = symtab_lookup(name);
    int argc = 0;
    Type *arg_types[16];
    if(!is_op(")"))
    {
        while(TRUE)
        {
            ExprResult arg = parse_assignment();
            gen_load(&arg);
            if(fn && argc < fn->param_count && fn->param_types[argc])
            {
                gen_cast(arg.type, fn->param_types[argc]);
                arg.type = fn->param_types[argc];
            }
            gen_push(arg.type);
            arg_types[argc] = arg.type;
            argc++;
            if(argc > 6)
            {
                codegen_error("more than 6 call arguments not supported");
            }
            if(is_op(","))
            {
                next_token();
                continue;
            }
            break;
        }
    }
    expect_op(")");

    // Pop arguments off the stack into their corresponding System V ABI registers.
    // Count how many floating point arguments are passed in XMM registers.
    int fp_count = 0;
    for(int i = 0; i < argc; i++)
    {
        if(is_float_type(arg_types[i]))
        {
            fp_count++;
        }
    }
    int cur_gp = 0;
    for(int i = 0; i < argc; i++)
    {
        if(!is_float_type(arg_types[i]))
        {
            cur_gp++;
        }
    }
    int cur_fp = fp_count;
    for(int i = argc - 1; i >= 0; i--)
    {
        if(is_float_type(arg_types[i]))
        {
            cur_fp--;
            const char *xreg = xmm_regs[cur_fp];
            if(arg_types[i]->kind == TYPE_FLOAT)
            {
                emit("    movss %s, dword [rsp]\n", xreg);
            }
            else
            {
                emit("    movsd %s, qword [rsp]\n", xreg);
            }
            emit("    add rsp, 8\n");
        }
        else
        {
            cur_gp--;
            emit("    pop %s\n", gp_regs[cur_gp]);
        }
    }
    emit("    mov eax, %d\n", fp_count); // Sys V ABI: AL = number of vector registers for varargs
    emit("    call %s\n", asm_name(name));
    namelist_add(&called_functions, name);

    ExprResult r;
    r.type = fn ? fn->type : type_int();
    r.is_lvalue = 0;
    return r;
}

static ExprResult parse_primary()
{
    ExprResult r;
    if(token_type == TOKEN_FLOAT_LITERAL)
    {
        const char *label = register_float_literal(token_float_value, 0);
        emit("    movsd xmm0, [%s]\n", label);
        r.type = type_double();
        r.is_lvalue = 0;
        next_token();
        return r;
    }
    if(token_type == TOKEN_NUMBER)
    {
        emit("    mov rax, %d\n", token_num_value);
        r.type = type_int();
        r.is_lvalue = 0;
        next_token();
        return r;
    }
    if(token_type == TOKEN_CHAR)
    {
        emit("    mov rax, %d\n", (int)(unsigned char)token_text[0]);
        r.type = type_char();
        r.is_lvalue = 0;
        next_token();
        return r;
    }
    if(token_type == TOKEN_STRING)
    {
        const char *label = register_string_literal(token_text);
        emit("    lea rax, [%s]\n", label);
        r.type = type_pointer_to(type_char());
        r.is_lvalue = 0;
        next_token();
        return r;
    }
    if(token_type == TOKEN_IDENTIFIER)
    {
        char name[1024];
        strcpy(name, token_text);
        next_token();
        if(is_op("("))
        {
            return parse_call(name);
        }
        Symbol *sym = symtab_lookup(name);
        if(!sym)
        {
            codegen_error("undeclared identifier '%s'", name);
        }
        if(sym->kind == SYM_GLOBAL_VAR || sym->kind == SYM_FUNCTION)
        {
            emit("    lea rax, [%s]\n", asm_name(name));
        }
        else
        {
            emit("    lea rax, [rbp%d]\n", sym->offset);
        }
        r.type = sym->type;
        r.is_lvalue = 1;
        return r;
    }
    if(is_op("("))
    {
        next_token();
        r = parse_assignment();
        expect_op(")");
        return r;
    }
    codegen_error("unexpected token '%s'", token_text);
    r.type = type_int();
    r.is_lvalue = 0;
    return r;
}

static ExprResult parse_postfix()
{
    ExprResult r = parse_primary();
    while(token_type == TOKEN_OPERATOR)
    {
        if(is_op("["))
        {
            next_token();
            if(r.type->kind == TYPE_ARRAY)
            {
                r.is_lvalue = 0; // array already decayed to its base address
            }
            else if(r.type->kind == TYPE_POINTER)
            {
                gen_load(&r); // fetch the pointer's value
            }
            else
            {
                codegen_error("subscript on non-array/pointer type");
            }
            Type *elem_type = r.type->base;
            emit("    push rax\n");
            ExprResult idx = parse_expr();
            gen_load(&idx);
            emit("    pop rcx\n");
            emit("    imul rax, rax, %d\n", type_size(elem_type));
            emit("    add rax, rcx\n");
            expect_op("]");
            r.type = elem_type;
            r.is_lvalue = 1;
            continue;
        }
        if(is_op("."))
        {
            next_token();
            if(r.type->kind != TYPE_STRUCT)
            {
                codegen_error("'.' used on non-struct type");
            }
            if(token_type != TOKEN_IDENTIFIER)
            {
                codegen_error("expected field name after '.'");
            }
            StructField *f = struct_find_field(r.type->struct_def, token_text);
            if(!f)
            {
                codegen_error("no such field '%s'", token_text);
            }
            next_token();
            if(f->offset != 0)
            {
                emit("    add rax, %d\n", f->offset);
            }
            r.type = f->type;
            r.is_lvalue = 1;
            continue;
        }
        if(is_op("->"))
        {
            next_token();
            gen_load(&r); // fetch the pointer's value (address of the struct)
            if(r.type->kind != TYPE_POINTER || r.type->base->kind != TYPE_STRUCT)
            {
                codegen_error("'->' used on non-struct-pointer type");
            }
            StructDef *sd = r.type->base->struct_def;
            if(token_type != TOKEN_IDENTIFIER)
            {
                codegen_error("expected field name after '->'");
            }
            StructField *f = struct_find_field(sd, token_text);
            if(!f)
            {
                codegen_error("no such field '%s'", token_text);
            }
            next_token();
            if(f->offset != 0)
            {
                emit("    add rax, %d\n", f->offset);
            }
            r.type = f->type;
            r.is_lvalue = 1;
            continue;
        }
        break;
    }
    return r;
}

static ExprResult parse_unary()
{
    ExprResult r;
    if(is_op("-"))
    {
        next_token();
        r = parse_unary();
        gen_load(&r);
        if(is_float_type(r.type))
        {
            if(r.type->kind == TYPE_FLOAT)
            {
                const char *lbl = register_float_literal(-0.0, 1);
                emit("    movss xmm1, [%s]\n", lbl);
                emit("    xorps xmm0, xmm1\n");
            }
            else
            {
                const char *lbl = register_float_literal(-0.0, 0);
                emit("    movsd xmm1, [%s]\n", lbl);
                emit("    xorpd xmm0, xmm1\n");
            }
        }
        else
        {
            emit("    neg rax\n");
        }
        r.is_lvalue = 0;
        return r;
    }
    if(is_op("!"))
    {
        next_token();
        r = parse_unary();
        gen_load(&r);
        if(is_float_type(r.type))
        {
            if(r.type->kind == TYPE_FLOAT)
            {
                emit("    xorps xmm1, xmm1\n");
                emit("    ucomiss xmm0, xmm1\n");
            }
            else
            {
                emit("    xorpd xmm1, xmm1\n");
                emit("    ucomisd xmm0, xmm1\n");
            }
            emit("    sete al\n");
            emit("    movzx rax, al\n");
        }
        else
        {
            emit("    cmp rax, 0\n");
            emit("    sete al\n");
            emit("    movzx rax, al\n");
        }
        r.type = type_int();
        r.is_lvalue = 0;
        return r;
    }
    if(is_op("&"))
    {
        next_token();
        r = parse_unary();
        if(!r.is_lvalue)
        {
            codegen_error("cannot take address of a non-lvalue");
        }
        r.type = type_pointer_to(r.type);
        r.is_lvalue = 0;
        return r;
    }
    if(is_op("*"))
    {
        next_token();
        r = parse_unary();
        gen_load(&r);
        if(r.type->kind != TYPE_POINTER)
        {
            codegen_error("cannot dereference a non-pointer type");
        }
        r.type = r.type->base;
        r.is_lvalue = 1;
        return r;
    }
    return parse_postfix();
}

static ExprResult parse_multiplicative()
{
    ExprResult l = parse_unary();
    while(is_op("*") || is_op("/") || is_op("%"))
    {
        char op = token_text[0];
        next_token();
        gen_load(&l);
        gen_push(l.type);
        ExprResult r = parse_unary();
        gen_load(&r);

        int l_flt = is_float_type(l.type);
        int r_flt = is_float_type(r.type);

        if(l_flt || r_flt)
        {
            if(op == '%')
            {
                codegen_error("modulo operator '%%' not supported for floating point types");
            }
            Type *res_type = (l.type->kind == TYPE_DOUBLE || r.type->kind == TYPE_DOUBLE) ? type_double() : type_float();
            gen_cast(r.type, res_type);
            emit("    movapd xmm1, xmm0\n"); // xmm1 = RHS
            gen_pop_to_first(l.type);
            gen_cast(l.type, res_type);   // xmm0 = LHS

            if(res_type->kind == TYPE_FLOAT)
            {
                emit(op == '*' ? "    mulss xmm0, xmm1\n" : "    divss xmm0, xmm1\n");
            }
            else
            {
                emit(op == '*' ? "    mulsd xmm0, xmm1\n" : "    divsd xmm0, xmm1\n");
            }
            l.type = res_type;
        }
        else
        {
            emit("    mov rcx, rax\n"); // rcx = RHS
            gen_pop_to_first(l.type);   // rax = LHS
            if(op == '*')
            {
                emit("    imul rax, rcx\n");
            }
            else
            {
                emit("    cqo\n");
                emit("    idiv rcx\n");
                if(op == '%')
                {
                    emit("    mov rax, rdx\n");
                }
            }
            l.type = type_int();
        }
        l.is_lvalue = 0;
    }
    return l;
}

// Handles pointer-scaled +/- (ptr+int, int+ptr, ptr-int, ptr-ptr) alongside
// floating-point and plain integer arithmetic; arrays behave as pointers here (see gen_load).
static ExprResult parse_additive()
{
    ExprResult l = parse_multiplicative();
    while(is_op("+") || is_op("-"))
    {
        char op = token_text[0];
        next_token();
        gen_load(&l);
        Type *l_type = l.type;
        gen_push(l_type);
        ExprResult r = parse_multiplicative();
        gen_load(&r);
        Type *r_type = r.type;

        int l_is_ptr = l_type->kind == TYPE_POINTER || l_type->kind == TYPE_ARRAY;
        int r_is_ptr = r_type->kind == TYPE_POINTER || r_type->kind == TYPE_ARRAY;
        int l_flt = is_float_type(l_type);
        int r_flt = is_float_type(r_type);

        if(l_flt || r_flt)
        {
            Type *res_type = (l_type->kind == TYPE_DOUBLE || r_type->kind == TYPE_DOUBLE) ? type_double() : type_float();
            gen_cast(r_type, res_type);
            emit("    movapd xmm1, xmm0\n"); // xmm1 = RHS
            gen_pop_to_first(l_type);
            gen_cast(l_type, res_type);   // xmm0 = LHS

            if(res_type->kind == TYPE_FLOAT)
            {
                emit(op == '+' ? "    addss xmm0, xmm1\n" : "    subss xmm0, xmm1\n");
            }
            else
            {
                emit(op == '+' ? "    addsd xmm0, xmm1\n" : "    subsd xmm0, xmm1\n");
            }
            l.type = res_type;
        }
        else if(l_is_ptr && r_is_ptr)
        {
            emit("    mov rcx, rax\n"); // rcx = right operand
            gen_pop_to_first(l_type);   // rax = left operand
            if(op != '-')
            {
                codegen_error("cannot add two pointers");
            }
            int elem_size = type_size(l_type->base);
            emit("    sub rax, rcx\n");
            emit("    mov rcx, %d\n", elem_size);
            emit("    cqo\n");
            emit("    idiv rcx\n");
            l.type = type_int();
        }
        else if(l_is_ptr)
        {
            emit("    mov rcx, rax\n"); // rcx = right operand
            gen_pop_to_first(l_type);   // rax = left operand
            int elem_size = type_size(l_type->base);
            emit("    imul rcx, rcx, %d\n", elem_size);
            emit(op == '+' ? "    add rax, rcx\n" : "    sub rax, rcx\n");
            l.type = l_type->kind == TYPE_ARRAY ? type_pointer_to(l_type->base) : l_type;
        }
        else if(r_is_ptr)
        {
            emit("    mov rcx, rax\n"); // rcx = right operand
            gen_pop_to_first(l_type);   // rax = left operand
            if(op != '+')
            {
                codegen_error("cannot subtract a pointer from an integer");
            }
            int elem_size = type_size(r_type->base);
            emit("    imul rax, rax, %d\n", elem_size);
            emit("    add rax, rcx\n");
            l.type = r_type->kind == TYPE_ARRAY ? type_pointer_to(r_type->base) : r_type;
        }
        else
        {
            emit("    mov rcx, rax\n"); // rcx = right operand
            gen_pop_to_first(l_type);   // rax = left operand
            emit(op == '+' ? "    add rax, rcx\n" : "    sub rax, rcx\n");
            l.type = type_int();
        }
        l.is_lvalue = 0;
    }
    return l;
}

static ExprResult parse_relational()
{
    ExprResult l = parse_additive();
    while(is_op("<") || is_op(">") || is_op("<=") || is_op(">="))
    {
        char op[3];
        strcpy(op, token_text);
        next_token();
        gen_load(&l);
        Type *l_type = l.type;
        gen_push(l_type);
        ExprResult r = parse_additive();
        gen_load(&r);
        Type *r_type = r.type;

        int l_flt = is_float_type(l_type);
        int r_flt = is_float_type(r_type);

        if(l_flt || r_flt)
        {
            Type *cmp_type = (l_type->kind == TYPE_DOUBLE || r_type->kind == TYPE_DOUBLE) ? type_double() : type_float();
            gen_cast(r_type, cmp_type);
            emit("    movapd xmm1, xmm0\n"); // xmm1 = RHS
            gen_pop_to_first(l_type);
            gen_cast(l_type, cmp_type);   // xmm0 = LHS

            if(cmp_type->kind == TYPE_FLOAT)
            {
                emit("    ucomiss xmm0, xmm1\n");
            }
            else
            {
                emit("    ucomisd xmm0, xmm1\n");
            }
            if(strcmp(op, "<") == 0) emit("    setb al\n");
            else if(strcmp(op, ">") == 0) emit("    seta al\n");
            else if(strcmp(op, "<=") == 0) emit("    setbe al\n");
            else emit("    setae al\n");
            emit("    movzx rax, al\n");
        }
        else
        {
            emit("    mov rcx, rax\n"); // rcx = RHS
            gen_pop_to_first(l_type);   // rax = LHS
            emit("    cmp rax, rcx\n");
            if(strcmp(op, "<") == 0) emit("    setl al\n");
            else if(strcmp(op, ">") == 0) emit("    setg al\n");
            else if(strcmp(op, "<=") == 0) emit("    setle al\n");
            else emit("    setge al\n");
            emit("    movzx rax, al\n");
        }
        l.type = type_int();
        l.is_lvalue = 0;
    }
    return l;
}

static ExprResult parse_equality()
{
    ExprResult l = parse_relational();
    while(is_op("==") || is_op("!="))
    {
        int is_eq = is_op("==");
        next_token();
        gen_load(&l);
        Type *l_type = l.type;
        gen_push(l_type);
        ExprResult r = parse_relational();
        gen_load(&r);
        Type *r_type = r.type;

        int l_flt = is_float_type(l_type);
        int r_flt = is_float_type(r_type);

        if(l_flt || r_flt)
        {
            Type *cmp_type = (l_type->kind == TYPE_DOUBLE || r_type->kind == TYPE_DOUBLE) ? type_double() : type_float();
            gen_cast(r_type, cmp_type);
            emit("    movapd xmm1, xmm0\n"); // xmm1 = RHS
            gen_pop_to_first(l_type);
            gen_cast(l_type, cmp_type);   // xmm0 = LHS

            if(cmp_type->kind == TYPE_FLOAT)
            {
                emit("    ucomiss xmm0, xmm1\n");
            }
            else
            {
                emit("    ucomisd xmm0, xmm1\n");
            }
            emit(is_eq ? "    sete al\n" : "    setne al\n");
            emit("    movzx rax, al\n");
        }
        else
        {
            emit("    mov rcx, rax\n");
            gen_pop_to_first(l_type);
            emit("    cmp rax, rcx\n");
            emit(is_eq ? "    sete al\n" : "    setne al\n");
            emit("    movzx rax, al\n");
        }
        l.type = type_int();
        l.is_lvalue = 0;
    }
    return l;
}

static ExprResult parse_logical_and()
{
    ExprResult l = parse_equality();
    while(is_op("&&"))
    {
        next_token();
        gen_load(&l);
        int false_label = codegen_new_label();
        int end_label = codegen_new_label();
        emit("    cmp rax, 0\n");
        emit("    je .L%d\n", false_label);
        ExprResult r = parse_equality();
        gen_load(&r);
        emit("    cmp rax, 0\n");
        emit("    je .L%d\n", false_label);
        emit("    mov rax, 1\n");
        emit("    jmp .L%d\n", end_label);
        emit(".L%d:\n", false_label);
        emit("    mov rax, 0\n");
        emit(".L%d:\n", end_label);
        l.type = type_int();
        l.is_lvalue = 0;
    }
    return l;
}

static ExprResult parse_logical_or()
{
    ExprResult l = parse_logical_and();
    while(is_op("||"))
    {
        next_token();
        gen_load(&l);
        int true_label = codegen_new_label();
        int end_label = codegen_new_label();
        emit("    cmp rax, 0\n");
        emit("    jne .L%d\n", true_label);
        ExprResult r = parse_logical_and();
        gen_load(&r);
        emit("    cmp rax, 0\n");
        emit("    jne .L%d\n", true_label);
        emit("    mov rax, 0\n");
        emit("    jmp .L%d\n", end_label);
        emit(".L%d:\n", true_label);
        emit("    mov rax, 1\n");
        emit(".L%d:\n", end_label);
        l.type = type_int();
        l.is_lvalue = 0;
    }
    return l;
}

static ExprResult parse_assignment()
{
    ExprResult l = parse_logical_or();
    if(is_op("="))
    {
        if(!l.is_lvalue)
        {
            codegen_error("left-hand side of assignment is not an lvalue");
        }
        next_token();
        Type *dest_type = l.type;
        emit("    push rax\n"); // save destination address
        ExprResult r = parse_assignment(); // right-associative
        gen_load(&r);
        gen_cast(r.type, dest_type);
        emit("    pop rcx\n");
        gen_store(dest_type, "rcx");
        l.type = dest_type;
        l.is_lvalue = 0;
        return l;
    }
    return l;
}

ExprResult parse_expr()
{
    return parse_assignment();
}

// ---- Phase 4: statements & control flow ----

static int current_return_label = -1;
static Type *current_return_type = NULL;

void codegen_set_return_label(int label)
{
    current_return_label = label;
}

void codegen_set_return_type(Type *type)
{
    current_return_type = type;
}

typedef struct LoopLabels {
    int continue_label;
    int break_label;
    struct LoopLabels *prev;
} LoopLabels;

static LoopLabels *loop_stack = NULL;

static void push_loop(int continue_label, int break_label)
{
    LoopLabels *l = malloc(sizeof(LoopLabels));
    l->continue_label = continue_label;
    l->break_label = break_label;
    l->prev = loop_stack;
    loop_stack = l;
}

static void pop_loop()
{
    LoopLabels *l = loop_stack;
    loop_stack = l->prev;
    free(l);
}

static int is_type_start()
{
    return token_type == TOKEN_KEYWORD &&
        (token_keyword == KEYWORD_INT || token_keyword == KEYWORD_CHAR ||
         token_keyword == KEYWORD_FLOAT || token_keyword == KEYWORD_DOUBLE ||
         token_keyword == KEYWORD_VOID || token_keyword == KEYWORD_STRUCT);
}

static Type *parse_declarator(Type *base, char *name_out);

static Type *parse_base_type()
{
    if(token_type != TOKEN_KEYWORD)
    {
        return NULL;
    }
    if(token_keyword == KEYWORD_INT)
    {
        next_token();
        return type_int();
    }
    if(token_keyword == KEYWORD_CHAR)
    {
        next_token();
        return type_char();
    }
    if(token_keyword == KEYWORD_FLOAT)
    {
        next_token();
        return type_float();
    }
    if(token_keyword == KEYWORD_DOUBLE)
    {
        next_token();
        return type_double();
    }
    if(token_keyword == KEYWORD_VOID)
    {
        next_token();
        return type_void();
    }
    if(token_keyword == KEYWORD_STRUCT)
    {
        next_token();
        if(token_type != TOKEN_IDENTIFIER)
        {
            codegen_error("expected struct tag name");
        }
        char tag[64];
        strcpy(tag, token_text);
        next_token();
        if(is_op("{"))
        {
            next_token();
            StructDef *def = struct_lookup(tag);
            if(!def)
            {
                def = struct_define(tag);
            }
            while(!is_op("}"))
            {
                Type *fbase = parse_base_type();
                if(!fbase)
                {
                    codegen_error("expected field type in struct '%s'", tag);
                }
                while(TRUE)
                {
                    char fname[64];
                    Type *ftype = parse_declarator(fbase, fname);
                    struct_add_field(def, fname, ftype);
                    if(is_op(","))
                    {
                        next_token();
                        continue;
                    }
                    break;
                }
                expect_op(";");
            }
            expect_op("}");
            struct_finalize(def);
            return type_struct(def);
        }
        StructDef *def = struct_lookup(tag);
        if(!def)
        {
            codegen_error("undefined struct '%s'", tag);
        }
        return type_struct(def);
    }
    return NULL;
}

// Consumes leading '*'s, the identifier, and an optional array suffix.
static Type *parse_declarator(Type *base, char *name_out)
{
    Type *t = base;
    while(is_op("*"))
    {
        next_token();
        t = type_pointer_to(t);
    }
    if(token_type != TOKEN_IDENTIFIER)
    {
        codegen_error("expected identifier in declaration");
    }
    strcpy(name_out, token_text);
    next_token();
    if(is_op("["))
    {
        next_token();
        if(token_type != TOKEN_NUMBER)
        {
            codegen_error("expected array size");
        }
        int len = token_num_value;
        next_token();
        expect_op("]");
        t = type_array_of(t, len);
    }
    return t;
}

// int a, *b = &a, c[4];  (each declarator may have its own initializer)
// Also handles a struct-only declaration with no variable, e.g. `struct Point { ... };`.
static void parse_declaration()
{
    Type *base = parse_base_type();
    if(is_op(";"))
    {
        next_token();
        return;
    }
    while(TRUE)
    {
        char name[64];
        Type *t = parse_declarator(base, name);
        Symbol *sym = symtab_declare_local(name, t);
        if(is_op("="))
        {
            next_token();
            emit("    lea rax, [rbp%d]\n", sym->offset);
            emit("    push rax\n");
            ExprResult r = parse_expr();
            gen_load(&r);
            gen_cast(r.type, t);
            emit("    pop rcx\n");
            gen_store(t, "rcx");
        }
        if(is_op(","))
        {
            next_token();
            continue;
        }
        break;
    }
    expect_op(";");
}

// Runs `body()` with emit() redirected into an in-memory buffer, returns the
// captured text (caller must free) instead of writing it to the real output.
static char *capture_emit(void (*body)(void *ctx), void *ctx)
{
    FILE *saved = out_file;
    char *buf = NULL;
    size_t size = 0;
    out_file = open_memstream(&buf, &size);
    body(ctx);
    fflush(out_file);
    fclose(out_file);
    out_file = saved;
    return buf;
}

static void capture_cond(void *ctx)
{
    ExprResult *cond_out = (ExprResult *)ctx;
    *cond_out = parse_expr();
    gen_load(cond_out);
}

static void capture_expr_stmt(void *ctx)
{
    (void)ctx;
    ExprResult r = parse_expr();
    (void)r;
}

void parse_block()
{
    expect_op("{");
    symtab_enter_scope();
    while(!is_op("}") && token_type != TOKEN_EOF)
    {
        parse_statement();
    }
    expect_op("}");
    symtab_exit_scope();
}

void parse_statement()
{
    if(is_op("{"))
    {
        parse_block();
        return;
    }
    if(is_op(";"))
    {
        next_token();
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_IF)
    {
        next_token();
        expect_op("(");
        ExprResult cond = parse_expr();
        gen_load(&cond);
        expect_op(")");
        int else_label = codegen_new_label();
        int end_label = codegen_new_label();
        emit("    cmp rax, 0\n");
        emit("    je .L%d\n", else_label);
        parse_statement();
        emit("    jmp .L%d\n", end_label);
        emit(".L%d:\n", else_label);
        if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_ELSE)
        {
            next_token();
            parse_statement();
        }
        emit(".L%d:\n", end_label);
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_WHILE)
    {
        next_token();
        expect_op("(");
        int start_label = codegen_new_label();
        int end_label = codegen_new_label();
        emit(".L%d:\n", start_label);
        ExprResult cond = parse_expr();
        gen_load(&cond);
        expect_op(")");
        emit("    cmp rax, 0\n");
        emit("    je .L%d\n", end_label);
        push_loop(start_label, end_label);
        parse_statement();
        pop_loop();
        emit("    jmp .L%d\n", start_label);
        emit(".L%d:\n", end_label);
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_FOR)
    {
        next_token();
        expect_op("(");
        symtab_enter_scope();
        if(is_type_start())
        {
            parse_declaration(); // consumes trailing ';'
        }
        else if(is_op(";"))
        {
            next_token();
        }
        else
        {
            capture_expr_stmt(NULL); // init expression, run immediately (order is already correct)
            expect_op(";");
        }

        // The condition and post-expression are parsed here (their natural
        // textual position) but must run AFTER the loop body, so their
        // generated code is captured into buffers and spliced in later.
        ExprResult cond = { type_int(), 0 };
        char *cond_code = NULL;
        int have_cond = !is_op(";");
        if(have_cond)
        {
            cond_code = capture_emit(capture_cond, &cond);
        }
        expect_op(";");

        char *post_code = NULL;
        if(!is_op(")"))
        {
            post_code = capture_emit(capture_expr_stmt, NULL);
        }
        expect_op(")");

        int body_label = codegen_new_label();
        int post_label = codegen_new_label();
        int check_label = codegen_new_label();
        int end_label = codegen_new_label();

        emit("    jmp .L%d\n", check_label);
        emit(".L%d:\n", body_label);
        push_loop(post_label, end_label);
        parse_statement();
        pop_loop();
        emit(".L%d:\n", post_label);
        if(post_code)
        {
            emit("%s", post_code);
            free(post_code);
        }
        emit(".L%d:\n", check_label);
        if(have_cond)
        {
            emit("%s", cond_code);
            free(cond_code);
            emit("    cmp rax, 0\n");
            emit("    jne .L%d\n", body_label);
        }
        else
        {
            emit("    jmp .L%d\n", body_label);
        }
        emit(".L%d:\n", end_label);
        symtab_exit_scope();
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_RETURN)
    {
        next_token();
        if(!is_op(";"))
        {
            ExprResult r = parse_expr();
            gen_load(&r);
            if(current_return_type)
            {
                gen_cast(r.type, current_return_type);
            }
        }
        else
        {
            emit("    xor eax, eax\n");
        }
        expect_op(";");
        if(current_return_label < 0)
        {
            codegen_error("'return' used outside of a function");
        }
        emit("    jmp .L%d\n", current_return_label);
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_BREAK)
    {
        next_token();
        expect_op(";");
        if(!loop_stack)
        {
            codegen_error("'break' used outside of a loop");
        }
        emit("    jmp .L%d\n", loop_stack->break_label);
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_CONTINUE)
    {
        next_token();
        expect_op(";");
        if(!loop_stack)
        {
            codegen_error("'continue' used outside of a loop");
        }
        emit("    jmp .L%d\n", loop_stack->continue_label);
        return;
    }
    if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_ASM)
    {
        char *asm_code = c_read_asm_block();
        if(!asm_code)
        {
            codegen_error("expected '{' after 'asm'");
        }
        emit("%s\n", asm_code);
        free(asm_code);
        if(is_op(";"))
        {
            next_token();
        }
        return;
    }
    if(is_type_start())
    {
        parse_declaration();
        return;
    }
    ExprResult r = parse_expr();
    (void)r;
    expect_op(";");
}

// ---- Phase 5: functions & globals ----

typedef struct GlobalInit {
    char name[64];
    Type *type;
    int has_init;
    int init_value;
    struct GlobalInit *next;
} GlobalInit;

static GlobalInit *global_list = NULL;
static GlobalInit *global_list_tail = NULL;

static void register_global(Symbol *sym, int has_init, int init_value)
{
    GlobalInit *g = malloc(sizeof(GlobalInit));
    strcpy(g->name, sym->name);
    g->type = sym->type;
    g->has_init = has_init;
    g->init_value = init_value;
    g->next = NULL;
    if(!global_list)
    {
        global_list = g;
    }
    else
    {
        global_list_tail->next = g;
    }
    global_list_tail = g;
}

void codegen_emit_globals()
{
    for(GlobalInit *g = global_list; g; g = g->next)
    {
        int size = type_size(g->type);
        if(g->has_init)
        {
            const char *directive = size == 1 ? "db" : (size == 4 ? "dd" : "dq");
            emit("%s: %s %d\n", asm_name(g->name), directive, g->init_value);
        }
        else
        {
            emit("%s: rb %d\n", asm_name(g->name), size);
        }
    }
}

// Emits `public` for every symbol this translation unit defines, and (in
// object-file mode) `extrn` for every function called but not defined here
// (prototypes-only, or builtins like print/print_int supplied elsewhere).
// Uses FASM alias syntax (`public internal as 'external'`, `extrn 'external' as _external`,
// and `internal = PLT _external`) so calls to external symbols use R_X86_64_PLT32
// relocations compatible with Position-Independent Executables (PIE).
void codegen_emit_linkage(int object_mode)
{
    for(NameList *n = defined_functions; n; n = n->next)
    {
        emit("public %s as '%s'\n", asm_name(n->name), n->name);
    }
    for(GlobalInit *g = global_list; g; g = g->next)
    {
        emit("public %s as '%s'\n", asm_name(g->name), g->name);
    }
    if(object_mode)
    {
        for(NameList *n = called_functions; n; n = n->next)
        {
            if(!namelist_contains(defined_functions, n->name))
            {
                emit("extrn '%s' as _%s\n", n->name, asm_name(n->name));
                emit("%s = PLT _%s\n", asm_name(n->name), asm_name(n->name));
            }
        }
    }
}

static Type *parse_type_with_stars(Type *base)
{
    Type *t = base;
    while(is_op("*"))
    {
        next_token();
        t = type_pointer_to(t);
    }
    return t;
}

// Consumes an identifier, and either reports it's a function (stopping right
// before the '(') or finishes parsing its optional array suffix.
static Type *parse_declarator_ex(Type *base, char *name_out, int *is_function_out)
{
    Type *t = parse_type_with_stars(base);
    if(token_type != TOKEN_IDENTIFIER)
    {
        codegen_error("expected identifier in declaration");
    }
    strcpy(name_out, token_text);
    next_token();
    if(is_op("("))
    {
        *is_function_out = 1;
        return t;
    }
    *is_function_out = 0;
    if(is_op("["))
    {
        next_token();
        if(token_type != TOKEN_NUMBER)
        {
            codegen_error("expected array size");
        }
        int len = token_num_value;
        next_token();
        expect_op("]");
        t = type_array_of(t, len);
    }
    return t;
}

static void capture_function_body(void *ctx)
{
    (void)ctx;
    parse_block();
}

static void parse_function(char *name, Type *return_type)
{
    static const char *reg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    static const char *reg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
    static const char *reg8[]  = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
    static const char *xmm_regs[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};

    expect_op("(");
    Symbol *fn_sym = symtab_lookup(name);
    if(!fn_sym)
    {
        fn_sym = symtab_declare_function(name, return_type);
    }

    char param_names[6][64];
    Type *param_types[6];
    int param_count = 0;
    if(!is_op(")"))
    {
        while(TRUE)
        {
            Type *pbase = parse_base_type();
            if(!pbase)
            {
                codegen_error("expected a parameter type");
            }
            Type *ptype = parse_type_with_stars(pbase);
            if(token_type != TOKEN_IDENTIFIER)
            {
                codegen_error("expected parameter name");
            }
            if(param_count >= 6)
            {
                codegen_error("more than 6 parameters is not supported");
            }
            strcpy(param_names[param_count], token_text);
            param_types[param_count] = ptype;
            param_count++;
            next_token();
            if(is_op(","))
            {
                next_token();
                continue;
            }
            break;
        }
    }
    expect_op(")");
    symtab_set_param_types(fn_sym, param_count, param_types);

    if(is_op(";"))
    {
        next_token(); // prototype only, no body
        return;
    }

    symtab_begin_function(); // resets local stack-frame offset counter, enters scope
    Symbol *param_syms[6];
    for(int i = 0; i < param_count; i++)
    {
        param_syms[i] = symtab_declare_param(param_names[i], param_types[i]);
    }

    int epilogue_label = codegen_new_label();
    codegen_set_return_label(epilogue_label);
    codegen_set_return_type(return_type);

    // The body is parsed/generated before the prologue so the final stack
    // frame size (which depends on locals declared inside it) is known.
    char *body_code = capture_emit(capture_function_body, NULL);
    int frame_size = symtab_frame_size();

    emit("%s:\n", asm_name(name));
    emit("    push rbp\n");
    emit("    mov rbp, rsp\n");
    if(frame_size > 0)
    {
        emit("    sub rsp, %d\n", frame_size);
    }
    int gp_idx = 0;
    int fp_idx = 0;
    for(int i = 0; i < param_count; i++)
    {
        if(is_float_type(param_types[i]))
        {
            const char *xreg = xmm_regs[fp_idx++];
            if(param_types[i]->kind == TYPE_FLOAT)
            {
                emit("    movss dword [rbp%d], %s\n", param_syms[i]->offset, xreg);
            }
            else
            {
                emit("    movsd qword [rbp%d], %s\n", param_syms[i]->offset, xreg);
            }
        }
        else
        {
            int size = type_size(param_types[i]);
            const char *reg = size == 1 ? reg8[gp_idx] : (size == 4 ? reg32[gp_idx] : reg64[gp_idx]);
            const char *width = size == 1 ? "byte" : (size == 4 ? "dword" : "qword");
            emit("    mov %s [rbp%d], %s\n", width, param_syms[i]->offset, reg);
            gp_idx++;
        }
    }
    emit("%s", body_code);
    free(body_code);
    emit(".L%d:\n", epilogue_label);
    emit("    leave\n");
    emit("    ret\n");
    namelist_add(&defined_functions, name);

    symtab_exit_scope(); // pop the function's params/locals
}

void parse_translation_unit()
{
    while(token_type != TOKEN_EOF)
    {
        if(token_type == TOKEN_KEYWORD && token_keyword == KEYWORD_ASM)
        {
            char *asm_code = c_read_asm_block();
            if(!asm_code)
            {
                codegen_error("expected '{' after 'asm'");
            }
            emit("%s\n", asm_code);
            free(asm_code);
            if(is_op(";"))
            {
                next_token();
            }
            continue;
        }

        Type *base = parse_base_type();
        if(!base)
        {
            codegen_error("expected a type at top level, got '%s'", token_text);
        }
        if(is_op(";"))
        {
            next_token(); // struct-only declaration, e.g. `struct Point { ... };`
            continue;
        }

        char name[64];
        int is_function = 0;
        Type *t = parse_declarator_ex(base, name, &is_function);

        if(is_function)
        {
            parse_function(name, t);
            continue;
        }

        while(TRUE)
        {
            Symbol *sym = symtab_declare_global(name, t);
            int has_init = 0;
            int init_value = 0;
            if(is_op("="))
            {
                next_token();
                if(token_type != TOKEN_NUMBER)
                {
                    codegen_error("only constant-integer initializers are supported for globals");
                }
                init_value = token_num_value;
                has_init = 1;
                next_token();
            }
            register_global(sym, has_init, init_value);
            if(!is_op(","))
            {
                break;
            }
            next_token();
            t = parse_declarator_ex(base, name, &is_function);
        }
        expect_op(";");
    }
}

