int sum_array(int *arr, int n) {
    int total = 0;
    int i;
    for (i = 0; i < n; i = i + 1) {
        total = total + *(arr + i);
    }
    return total;
}

void swap(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

int main() {
    int nums[4];
    nums[0] = 1;
    nums[1] = 2;
    nums[2] = 3;
    nums[3] = 4;
    print_int(sum_array(nums, 4));
    print("\n");

    int a = 100;
    int b = 200;
    swap(&a, &b);
    print_int(a);
    print(",");
    print_int(b);
    print("\n");

    int *p = nums;
    int *q = p + 2;
    print_int(*q);
    print("\n");
    return sum_array(nums, 4) % 256;
}
