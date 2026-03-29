#!/bin/bash
# Stress test for mronc (Linux/macOS)
# Generates a large .mron file, converts to JSON, converts back, and checks timing.
# Run from the project root directory.

set -e

MRONC="./mronc"
if [ ! -f "$MRONC" ]; then
    MRONC="./mronc.exe"
fi
if [ ! -f "$MRONC" ]; then
    echo "ERROR: mronc binary not found. Run 'make' first."
    exit 1
fi

GENSCRIPT="tests/_gen_stress.py"
MRON_FILE="tests/_stress.mron"
JSON_FILE="tests/_stress.json"
ROUNDTRIP_FILE="tests/_stress_rt.mron"
ROUNDTRIP_JSON="tests/_stress_rt.json"

cleanup() {
    rm -f "$MRON_FILE" "$JSON_FILE" "$ROUNDTRIP_FILE" "$ROUNDTRIP_JSON"
}
trap cleanup EXIT

echo "=== Stress Test ==="
echo ""

# --- Generate ---
echo "[1/5] Generating large MRON file..."
python3 "$GENSCRIPT" "$MRON_FILE"
FILESIZE=$(wc -c < "$MRON_FILE" | tr -d ' ')
echo "      Generated $MRON_FILE ($FILESIZE bytes)"

# --- MRON to JSON ---
echo "[2/5] MRON -> JSON ..."
T0=$(date +%s%N)
"$MRONC" "$MRON_FILE" -o "$JSON_FILE"
T1=$(date +%s%N)
MS_MRON=$(( (T1 - T0) / 1000000 ))
JSONSIZE=$(wc -c < "$JSON_FILE" | tr -d ' ')
echo "      Completed in ${MS_MRON} ms"
echo "      Produced $JSON_FILE ($JSONSIZE bytes)"

# --- JSON to MRON ---
echo "[3/5] JSON -> MRON ..."
T0=$(date +%s%N)
"$MRONC" "$JSON_FILE" -o "$ROUNDTRIP_FILE"
T1=$(date +%s%N)
MS_JSON=$(( (T1 - T0) / 1000000 ))
echo "      Completed in ${MS_JSON} ms"

# --- Round-trip check ---
echo "[4/5] Round-trip verification (MRON->JSON->MRON->JSON) ..."
"$MRONC" "$ROUNDTRIP_FILE" -o "$ROUNDTRIP_JSON"
if diff -q "$JSON_FILE" "$ROUNDTRIP_JSON" > /dev/null 2>&1; then
    echo "      Round-trip OK - JSON outputs match"
else
    echo "FAIL: round-trip JSON output differs"
    exit 1
fi

# --- Summary ---
echo "[5/5] Summary"
echo "      MRON->JSON : ${MS_MRON} ms  ($MRON_FILE $FILESIZE bytes)"
echo "      JSON->MRON : ${MS_JSON} ms  ($JSON_FILE $JSONSIZE bytes)"
echo ""
echo "PASS: stress test completed successfully"
