#!/usr/bin/env bash
# Builds the compiler, then compiles+runs every tests/cases/*.c fixture and
# checks its stdout (against NAME.out) and exit code (against NAME.exit).
set -uo pipefail

cd "$(dirname "$0")/.."

echo "Building compiler..."
if ! make >/dev/null; then
    echo "compiler build failed"
    exit 1
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

pass=0
fail=0

for src in tests/cases/*.c; do
    name=$(basename "$src" .c)
    expected_out_file="tests/cases/$name.out"
    expected_exit_file="tests/cases/$name.exit"
    bin="$tmpdir/$name"

    if ! ./compile "$src" -o "$bin" > "$tmpdir/$name.build.log" 2>&1; then
        echo "FAIL $name (build error)"
        cat "$tmpdir/$name.build.log"
        fail=$((fail + 1))
        continue
    fi

    actual_out=$("$bin")
    actual_exit=$?

    expected_out=""
    [ -f "$expected_out_file" ] && expected_out=$(cat "$expected_out_file")
    expected_exit=0
    [ -f "$expected_exit_file" ] && expected_exit=$(cat "$expected_exit_file")

    if [ "$actual_out" = "$expected_out" ] && [ "$actual_exit" = "$expected_exit" ]; then
        echo "PASS $name"
        pass=$((pass + 1))
    else
        echo "FAIL $name (exit expected=$expected_exit got=$actual_exit)"
        diff <(printf '%s\n' "$expected_out") <(printf '%s\n' "$actual_out")
        fail=$((fail + 1))
    fi
done

echo "----"
echo "$pass passed, $fail failed"

echo "Checking -c object-file mode..."
if ./compile -c tests/cases/01_arithmetic.c -o "$tmpdir/01_arithmetic.o" > "$tmpdir/objmode.log" 2>&1 \
    && file "$tmpdir/01_arithmetic.o" | grep -q "relocatable"; then
    echo "PASS object-file mode"
    pass=$((pass + 1))
else
    echo "FAIL object-file mode"
    cat "$tmpdir/objmode.log"
    fail=$((fail + 1))
fi

[ "$fail" -eq 0 ]

