// example.c - a tour of every feature this compiler currently supports.
// Build & run:
//   make && ./compile examples/example.c -o example.elf && ./example.elf
//
// This program doesn't rely on its exit code for correctness (though it does
// return one) - it prints as it goes via the builtin print()/print_int(),
// since strings + a tiny syscall-based runtime are supported (Phase 7).

int add(int a, int b);          // forward declaration (prototype)

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

// --- entry point ---

int main() {
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

    print("== globals ==\n");
    print_int(global_counter);
    print(",");
    print_int(global_uninitialized);
    print("\n");

    return 0;
}
