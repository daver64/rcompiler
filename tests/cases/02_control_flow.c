int main() {
    int i = 0;
    int sum = 0;
    while (i < 10) {
        if (i % 2 == 0) {
            sum = sum + i;
        }
        i = i + 1;
    }
    print_int(sum);
    print("\n");

    int count = 0;
    for (i = 0; i < 20; i = i + 1) {
        if (i == 15) {
            break;
        }
        if (i % 3 != 0) {
            continue;
        }
        count = count + 1;
    }
    print_int(count);
    print("\n");
    return sum % 256;
}
