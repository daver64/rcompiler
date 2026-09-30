#include "support.h"
#include "symtab.h"
#include "codegen.h"
#include "preprocessor.h"
#include <stdlib.h>
#include <sys/stat.h>

// Raw-syscall runtime (no libc): `int print(char *s);` / `int print_int(int n);`.
// Only linked into standalone executables; object-file builds extrn them instead.
static void emit_runtime_prelude(FILE *out)
{
    fprintf(out, "u_print:\n");
    fprintf(out, "    push rbx\n");
    fprintf(out, "    mov rbx, rdi\n");
    fprintf(out, "    xor rdx, rdx\n");
    fprintf(out, ".u_print_count:\n");
    fprintf(out, "    cmp byte [rbx+rdx], 0\n");
    fprintf(out, "    je .u_print_done\n");
    fprintf(out, "    inc rdx\n");
    fprintf(out, "    jmp .u_print_count\n");
    fprintf(out, ".u_print_done:\n");
    fprintf(out, "    mov rsi, rbx\n");
    fprintf(out, "    mov rax, 1\n");
    fprintf(out, "    mov rdi, 1\n");
    fprintf(out, "    syscall\n");
    fprintf(out, "    pop rbx\n");
    fprintf(out, "    mov rax, 0\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, "u_print_int:\n");
    fprintf(out, "    push rbx\n");
    fprintf(out, "    push r12\n");
    fprintf(out, "    mov rax, rdi\n");
    fprintf(out, "    xor r12, r12\n");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    jge .u_print_int_pos\n");
    fprintf(out, "    neg rax\n");
    fprintf(out, "    mov r12, 1\n");
    fprintf(out, ".u_print_int_pos:\n");
    fprintf(out, "    lea rbx, [print_int_buf+23]\n");
    fprintf(out, "    mov byte [rbx], 0\n");
    fprintf(out, "    mov rcx, 10\n");
    fprintf(out, ".u_print_int_loop:\n");
    fprintf(out, "    xor rdx, rdx\n");
    fprintf(out, "    div rcx\n");
    fprintf(out, "    add dl, '0'\n");
    fprintf(out, "    dec rbx\n");
    fprintf(out, "    mov [rbx], dl\n");
    fprintf(out, "    test rax, rax\n");
    fprintf(out, "    jnz .u_print_int_loop\n");
    fprintf(out, "    cmp r12, 0\n");
    fprintf(out, "    je .u_print_int_no_sign\n");
    fprintf(out, "    dec rbx\n");
    fprintf(out, "    mov byte [rbx], '-'\n");
    fprintf(out, ".u_print_int_no_sign:\n");
    fprintf(out, "    lea rdx, [print_int_buf+23]\n");
    fprintf(out, "    sub rdx, rbx\n");
    fprintf(out, "    mov rsi, rbx\n");
    fprintf(out, "    mov rax, 1\n");
    fprintf(out, "    mov rdi, 1\n");
    fprintf(out, "    syscall\n");
    fprintf(out, "    pop r12\n");
    fprintf(out, "    pop rbx\n");
    fprintf(out, "    mov rax, 0\n");
    fprintf(out, "    ret\n\n");
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-c] <file.c> -o <output> (reads stdin if no file given)\n", prog);
}

int main(int argc, char *argv[])
{
    int object_mode = 0;
    const char *input_path = NULL;
    const char *output_path = NULL;

    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "-c") == 0)
        {
            object_mode = 1;
        }
        else if(strcmp(argv[i], "-o") == 0)
        {
            if(i + 1 >= argc)
            {
                fprintf(stderr, "-o requires an argument\n");
                usage(argv[0]);
                return 1;
            }
            output_path = argv[++i];
        }
        else if(argv[i][0] != '-')
        {
            input_path = argv[i];
        }
        else
        {
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if(!output_path)
    {
        output_path = object_mode ? "a.o" : "a.out";
    }

    char *preprocessed = preprocess_file(input_path);

    // Pass 1: parse the whole program into an in-memory buffer. Parsing also
    // populates the globals/strings/linkage lists that the real header
    // (format/public/extrn/entry) needs to know about before it's written.
    char *body_code = NULL;
    size_t body_size = 0;
    FILE *body_buf = open_memstream(&body_code, &body_size);
    codegen_init(body_buf);
    symtab_init();
    symtab_declare_function("print", type_int());
    symtab_declare_function("print_int", type_int());

    lexer_set_source(preprocessed);
    next_token();
    parse_translation_unit();
    fflush(body_buf);
    fclose(body_buf);
    free(preprocessed);

    // Pass 2: now that linkage is known, write the real assembly file.
    const char *asm_path = "out.asm";
    FILE *out = fopen(asm_path, "w");
    if(!out)
    {
        fprintf(stderr, "cannot open '%s' for writing\n", asm_path);
        free(body_code);
        return 1;
    }
    codegen_init(out);

    if(object_mode)
    {
        fprintf(out, "format ELF64\n\n");
    }
    else
    {
        fprintf(out, "format ELF64 executable 3\n");
        fprintf(out, "entry start\n\n");
    }

    fprintf(out, object_mode ? "section '.text' executable\n\n" : "segment readable executable\n\n");
    if(object_mode)
    {
        codegen_emit_linkage(object_mode);
        fprintf(out, "\n");
    }

    if(!object_mode)
    {
        fprintf(out, "start:\n");
        fprintf(out, "    call u_main\n");
        fprintf(out, "    mov rdi, rax\n");
        fprintf(out, "    mov rax, 60\n");
        fprintf(out, "    syscall\n\n");
        emit_runtime_prelude(out);
    }

    fwrite(body_code, 1, body_size, out);
    free(body_code);

    fprintf(out, object_mode ? "\nsection '.data' writeable\n\n" : "\nsegment readable writeable\n\n");
    codegen_emit_globals();
    codegen_emit_strings();
    if(!object_mode)
    {
        fprintf(out, "print_int_buf: rb 24\n");
    }
    fclose(out);

    char command[1024];
    snprintf(command, sizeof(command), "fasm %s %s", asm_path, output_path);
    int status = system(command);
    if(status != 0)
    {
        fprintf(stderr, "fasm failed\n");
        return 1;
    }
    if(!object_mode)
    {
        chmod(output_path, 0755);
    }
    fprintf(stdout, "built %s\n", output_path);
    return 0;
}

