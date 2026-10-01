// tests/cases/10_inline_asm.c - tests inline asm { ... } blocks

int get_rflags_low() {
    int flags = 0;
    asm {
        pushfq
        pop rax
        mov [rbp-4], eax
    }
    return flags;
}

int asm_multiply_add(int x, int y, int z) {
    asm {
        ; x is at [rbp-4], y is at [rbp-8], z is at [rbp-12]
        mov eax, [rbp-4]
        imul eax, [rbp-8]
        add eax, [rbp-12]
        mov [rbp-4], eax
    }
    return x;
}

int main() {
    int f = get_rflags_low();
    if (f != 0) {
        print("FLAGS OK\n");
    } else {
        print("FLAGS ZERO\n");
    }

    int res = asm_multiply_add(6, 7, 5); // 6 * 7 + 5 = 47
    print("RES: ");
    print_int(res);
    print("\n");
    return res;
}
