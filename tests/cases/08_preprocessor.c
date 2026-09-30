#include "08_preprocessor.h"

int main() {
    int limit = LIMIT;
    print_int(limit);
    print("\n");
    print_int(BONUS);
    print("\n");
    return BONUS % 256;
}
