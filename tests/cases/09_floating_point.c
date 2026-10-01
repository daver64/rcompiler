double square(double x) {
    return x * x;
}

double add_doubles(double a, double b) {
    return a + b;
}

int main() {
    double pi = 3.14159;
    double r = 2.0;
    double area = pi * square(r);
    float f1 = 1.5;
    float f2 = 2.5;
    float f3 = f1 + f2;
    double df3 = f3;
    double sum = add_doubles(area, df3);

    print_float(f3);
    print("\n");
    print_double(sum);
    print("\n");

    int result = sum;
    return result;
}
