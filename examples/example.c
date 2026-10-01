// example.c - a tour of every feature this compiler currently supports.
// Build & run as standalone executable:
//   make && ./compile examples/example.c -o example.elf && ./example.elf
//
// Demonstrates:
// - Preprocessor (#define macros, recursive expansion)
// - Forward declarations / function prototypes
// - Functions, recursion, System V calling convention
// - Control flow: if / else, while, for, break, continue
// - Floating point: float & double, arithmetic, comparisons, int casts
// - Pointers & arrays: indexing, address-of (&), dereference (*), pointer arithmetic
// - Structs: declaration, member access (.), pointer access (->)
// - Inline assembly: asm { ... } blocks for low-level OS operations
// - Globals: initialized & zero-initialized
// - String literals with escape sequences
// - Builtin runtime: print(), print_int(), print_float(), print_double()

#define APP_TITLE "== rcompiler feature tour ==\n"
#define PI_APPROX 3.14159
#define CIRCLE_RADIUS 2.0

int add(int a, int b);          // forward declaration (prototype)
double circle_area(double radius);

struct Point {
    int x;
    int y;
};

int global_counter = 100;       // global variable with a constant initializer
int global_uninitialized;       // global variable with no initializer (zeroed)

// --- functions, recursion, control flow ---

int add(int a, int b) {
    return a + b;
}

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int sum_1_to_n(int n) {
    int total = 0;
    int i;
    for (i = 1; i <= n; i = i + 1) {
        total = total + i;
    }
    return total;
}

int count_even_below(int n) {
    int count = 0;
    int i = 0;
    while (i < n) {
        if (i % 2 != 0) {
            i = i + 1;
            continue;
        }
        if (i == 10) {
            break;
        }
        count = count + 1;
        i = i + 1;
    }
    return count;
}

// --- pointers & arrays ---

int sum_array(int *arr, int len) {
    int total = 0;
    int i;
    for (i = 0; i < len; i = i + 1) {
        total = total + *(arr + i); // pointer arithmetic, equivalent to arr[i]
    }
    return total;
}

void swap(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

// --- structs ---

int manhattan_distance(struct Point *p) {
    return p->x + p->y;
}

// --- floating point calculations ---

double circle_area(double radius) {
    return PI_APPROX * radius * radius;
}

float average3(float a, float b, float c) {
    return (a + b + c) / 3.0;
}

// --- inline assembly (useful for OS development: flags, CPUID, port I/O, control regs) ---

int cpu_has_cpuid() {
    int supported = 0;
    asm {
        ; Check if CPUID is supported by attempting to flip the ID bit (bit 21) in EFLAGS
        pushfq
        pop rax
        mov ecx, eax
        xor eax, 0x00200000
        push rax
        popfq
        pushfq
        pop rax
        push rcx
        popfq
        xor eax, ecx
        test eax, 0x00200000
        jz .no_cpuid
        mov dword [rbp-4], 1
    .no_cpuid:
    }
    return supported;
}

int read_tsc_low() {
    int tsc = 0;
    asm {
        rdtsc
        mov [rbp-4], eax
    }
    return tsc;
}

// --- entry point ---

int main() {
    print(APP_TITLE);

    print("== functions & recursion ==\n");
    print_int(add(3, 4));
    print("\n");
    print_int(fib(10));
    print("\n");

    print("== loops, if/else, break/continue ==\n");
    print_int(sum_1_to_n(5));   // 1+2+3+4+5 = 15
    print("\n");
    print_int(count_even_below(20)); // stops early via break
    print("\n");

    print("== floating point (float & double) ==\n");
    double area = circle_area(CIRCLE_RADIUS); // 3.14159 * 2 * 2 = 12.56636
    print("circle area: ");
    print_double(area);
    print("\n");

    float f1 = 10.5;
    float f2 = 20.25;
    float f3 = 30.75;
    float avg = average3(f1, f2, f3); // (10.5 + 20.25 + 30.75) / 3 = 20.5
    print("average: ");
    print_float(avg);
    print("\n");

    if (area > 12.0 && area < 13.0) {
        print("fp comparison: OK\n");
    } else {
        print("fp comparison: FAIL\n");
    }

    print("== arrays & pointers ==\n");
    int nums[5];
    nums[0] = 10;
    nums[1] = 20;
    nums[2] = 30;
    nums[3] = 40;
    nums[4] = 50;
    print_int(sum_array(nums, 5));
    print("\n");

    int a = 1;
    int b = 2;
    swap(&a, &b);
    print_int(a);
    print(",");
    print_int(b);
    print("\n");

    print("== structs ==\n");
    struct Point pt;
    pt.x = 3;
    pt.y = 4;
    struct Point *pp = &pt;
    pp->x = pp->x + 1;
    print_int(manhattan_distance(pp));
    print("\n");

    print("== inline assembly (asm {}) ==\n");
    if (cpu_has_cpuid()) {
        print("CPUID supported: YES\n");
    } else {
        print("CPUID supported: NO\n");
    }
    int tsc = read_tsc_low();
    print("RDTSC low 32 bits: ");
    print_int(tsc);
    print("\n");

    print("== globals ==\n");
    print_int(global_counter);
    print(",");
    print_int(global_uninitialized);
    print("\n");

    return 0;
}
