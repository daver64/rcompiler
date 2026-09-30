int add(int a, int b) {
    return a + b;
}

int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}

int main() {
    print_int(add(10, 32));
    print("\n");
    print_int(fact(5));
    print("\n");
    return fact(5) % 256;
}
