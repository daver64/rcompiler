int main() {
    int a = 7;
    int b = 3;
    print_int(a + b);
    print(",");
    print_int(a - b);
    print(",");
    print_int(a * b);
    print(",");
    print_int(a / b);
    print(",");
    print_int(a % b);
    print("\n");
    return (a + b) % 256;
}
