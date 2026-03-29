"""
Generate a large .mron stress-test file.

The generated file exercises every syntax feature:
  - strings, numbers (plain, underscore, comma, decimal, negative), booleans, null
  - nested records (multiple levels deep)
  - plain lists, lists of records, CSV-style lists
  - single-line and multi-line comments

Usage:  python _gen_stress.py <output-path>
"""

import sys
import os

# --- Tunables -----------------------------------------------------------
NUM_TOP_LEVEL_ENTRIES = 500       # simple key-value pairs at the top
NUM_NESTED_RECORDS    = 200       # records with sub-records
NESTING_DEPTH         = 8         # max depth for the deep-nesting test
NUM_LIST_ITEMS        = 300       # items in a flat list
CSV_COLUMNS           = 6         # columns in csv-style tables
CSV_ROWS              = 500       # rows per csv-style table
NUM_CSV_TABLES        = 3         # number of csv tables
COMMENT_INTERVAL      = 25        # insert a comment every N entries
# -------------------------------------------------------------------------


def main():
    if len(sys.argv) < 2:
        print("Usage: python _gen_stress.py <output-path>", file=sys.stderr)
        sys.exit(1)

    out_path = sys.argv[1]
    lines = []
    w = lines.append

    w("###")
    w("Auto-generated MRON stress test file.")
    w("This file exercises every MRON syntax feature at scale.")
    w("###")
    w("")

    # --- 1. Top-level key-value pairs ------------------------------------
    w("# ── Top-level key-value pairs ──")
    for i in range(NUM_TOP_LEVEL_ENTRIES):
        if i > 0 and i % COMMENT_INTERVAL == 0:
            w(f"# batch marker {i}")
        typ = i % 7
        if typ == 0:
            w(f'entry_{i} "value_{i}_string_data_with_some_length_to_it"')
        elif typ == 1:
            w(f"entry_{i} {i * 37}")
        elif typ == 2:
            w(f"entry_{i} {i * 1000 + 1}_000")
        elif typ == 3:
            w(f"entry_{i} {i}.{i % 100:02d}")
        elif typ == 4:
            w(f"entry_{i} {'yes' if i % 2 == 0 else 'no'}")
        elif typ == 5:
            w(f"entry_{i} {'true' if i % 3 == 0 else 'false'}")
        else:
            w(f"entry_{i} null")
    w("")

    # --- 2. Nested records -----------------------------------------------
    w("# ── Nested records ──")
    for i in range(NUM_NESTED_RECORDS):
        if i > 0 and i % COMMENT_INTERVAL == 0:
            w("")
            w(f"# nested block {i}")
        w(f"record_{i} {{")
        w(f'    name "record_{i}"')
        w(f"    index {i}")
        w(f"    active {'yes' if i % 2 == 0 else 'no'}")
        w(f"    meta {{")
        w(f'        tag "tag_{i % 50}"')
        w(f"        score {i * 3}.{i % 10}")
        w(f"    }}")
        w(f"}}")
    w("")

    # --- 3. Deep nesting -------------------------------------------------
    w("# ── Deep nesting test ──")
    w("deep {")
    indent = 1
    for d in range(NESTING_DEPTH):
        pad = "    " * indent
        w(f"{pad}level_{d} {{")
        w(f'{pad}    label "depth_{d}"')
        w(f"{pad}    value {d}")
        indent += 1
    # innermost data
    pad = "    " * indent
    w(f'{pad}leaf "bottom"')
    # close all braces
    for d in range(NESTING_DEPTH, -1, -1):
        pad = "    " * d
        w(f"{pad}}}")
    w("")

    # --- 4. Flat lists ---------------------------------------------------
    w("# ── Large flat list ──")
    items = " ".join(f'"item_{i}"' for i in range(NUM_LIST_ITEMS))
    w(f"big_list [ {items} ]")
    w("")

    w("# ── Large number list ──")
    nums = " ".join(str(i * 7) for i in range(NUM_LIST_ITEMS))
    w(f"number_list [ {nums} ]")
    w("")

    # --- 5. List of records ----------------------------------------------
    w("# ── List of records ──")
    w("record_list [")
    for i in range(200):
        w(f'    {{ id {i} name "item_{i}" ok {"yes" if i % 2 == 0 else "no"} }}')
    w("]")
    w("")

    # --- 6. CSV-style tables ---------------------------------------------
    w("# ── CSV-style tables ──")
    for t in range(NUM_CSV_TABLES):
        headers = " ".join(f"col_{c}" for c in range(CSV_COLUMNS))
        w(f"table_{t} ({headers}) [")
        for r in range(CSV_ROWS):
            vals = []
            for c in range(CSV_COLUMNS):
                cell = (r * CSV_COLUMNS + c) % 5
                if cell == 0:
                    vals.append(f'"r{r}_c{c}"')
                elif cell == 1:
                    vals.append(str(r * 100 + c))
                elif cell == 2:
                    vals.append("yes" if r % 2 == 0 else "no")
                elif cell == 3:
                    vals.append(f"{r}.{c}")
                else:
                    vals.append("null")
            w(f"    {' '.join(vals)}")
        w("]")
        w("")

    # --- 7. Multi-line comments ------------------------------------------
    w("###")
    w("This is a large multi-line comment block")
    for i in range(50):
        w(f"  Comment line {i}: {'x' * 60}")
    w("###")
    w("")

    # --- 8. Strings with escape sequences --------------------------------
    w("# ── Escaped strings ──")
    for i in range(100):
        w(f'escaped_{i} "line\\n\\ttab\\\\slash\\\\\\"quoted\\\" end_{i}"')
    w("")

    w("# ── End of stress test ──")

    content = "\n".join(lines) + "\n"
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)

    size_kb = len(content.encode("utf-8")) / 1024
    print(f"Generated {out_path} ({size_kb:.1f} KB, {len(lines)} lines)")


if __name__ == "__main__":
    main()
