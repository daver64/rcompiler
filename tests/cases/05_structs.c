struct Point {
    int x;
    int y;
};

int manhattan(struct Point *p) {
    return p->x + p->y;
}

int main() {
    struct Point pt;
    pt.x = 3;
    pt.y = 4;
    struct Point *pp = &pt;
    pp->x = pp->x + 10;
    print_int(manhattan(pp));
    print("\n");
    return manhattan(pp) % 256;
}
