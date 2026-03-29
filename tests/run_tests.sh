#!/bin/bash
# Test runner for mronc
# Run from the project root directory

set -e

MRONC="./mronc"
if [ ! -f "$MRONC" ]; then
    MRONC="./mronc.exe"
fi

if [ ! -f "$MRONC" ]; then
    echo "ERROR: mronc binary not found. Run 'make' first."
    exit 1
fi

PASS=0
FAIL=0
TOTAL=0

# --- Helper functions ---

run_test() {
    local desc="$1"
    local input="$2"
    local expected="$3"
    TOTAL=$((TOTAL + 1))

    local actual
    actual=$("$MRONC" "$input" 2>/dev/null) || true

    local expected_content
    expected_content=$(cat "$expected")

    if [ "$actual" = "$expected_content" ]; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc"
        echo "    Expected: $expected_content"
        echo "    Actual:   $actual"
        FAIL=$((FAIL + 1))
    fi
}

run_error_test() {
    local desc="$1"
    local input="$2"
    TOTAL=$((TOTAL + 1))

    if "$MRONC" "$input" >/dev/null 2>&1; then
        echo "  FAIL: $desc (expected error, but succeeded)"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $desc (correctly reported error)"
        PASS=$((PASS + 1))
    fi
}

# --- MRON to JSON tests ---

echo "=== MRON -> JSON tests ==="
run_test "Basic key-value pairs"    tests/test_basic.mron         tests/test_basic.expected.json
run_test "Comments"                 tests/test_comments.mron      tests/test_comments.expected.json
run_test "All value types"          tests/test_types.mron         tests/test_types.expected.json
run_test "CSV-style lists"          tests/test_csv.mron           tests/test_csv.expected.json
run_test "Nested structures"        tests/test_nested.mron        tests/test_nested.expected.json
run_test "Empty structures"         tests/test_empty.mron         tests/test_empty.expected.json
run_test "Full README example"      tests/test_full_example.mron  tests/test_full_example.expected.json

# --- JSON to MRON tests ---

echo ""
echo "=== JSON -> MRON tests ==="
run_test "JSON to MRON conversion"  tests/test_json_to_mron.json  tests/test_json_to_mron.expected.mron

# --- Error handling tests ---

echo ""
echo "=== Error handling tests ==="
run_error_test "Unterminated string"    tests/test_err_unterminated_string.mron
run_error_test "Invalid key (number)"   tests/test_err_invalid_key.mron
run_error_test "Bad value (identifier)" tests/test_err_bad_value.mron
run_error_test "CSV value mismatch"     tests/test_err_csv_mismatch.mron
run_error_test "Unclosed brace"         tests/test_err_unclosed_brace.mron

# --- Summary ---

echo ""
echo "=== Results: $PASS/$TOTAL passed, $FAIL failed ==="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
