int counter = 41;
int zeroed;

int increment() {
    counter = counter + 1;
    return counter;
}

int main() {
    print_int(increment());
    print("\n");
    print_int(zeroed);
    print("\n");
    return counter % 256;
}
