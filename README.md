# MRON — Minimal Rich Object Notation

**MRON** is a human-friendly data format designed to replace JSON and YAML in configuration files, data exchange, and anywhere readability matters. It strips away the noise — no colons, no commas, no mandatory quoting of keys — while staying fully translatable to JSON.

```mron
name     "Alice"
age      25
is-admin yes

tags [ "staff" "uk" ]
```

That's valid MRON. No `:`, no `,`. Just keys and values.

## Why MRON?

| Pain point | How MRON helps |
|---|---|
| **JSON is noisy** — colons, commas and double-quoted keys everywhere | Keys are bare identifiers. Values follow directly. Commas and colons don't exist. |
| **YAML is fragile** — indentation errors silently change meaning | Braces and brackets make structure explicit. Indentation is optional cosmetics. |
| **CSV can't nest** — flat rows only | CSV-style headers + a value list give you tabular data *inside* a richer document. |
| **No comments in JSON** — hacks like `"_comment"` keys | `#` single-line and `### ... ###` multi-line comments are first-class. |

## Quick start

This repository includes **mronc**, a bidirectional translator between MRON and JSON.

```sh
# MRON → JSON  (input format detected from extension)
mronc config.mron                   # JSON to stdout
mronc config.mron -o config.json    # JSON to file

# JSON → MRON
mronc config.json                   # MRON to stdout
mronc config.json -o config.mron    # MRON to file

# Pipe-friendly — pair with jq for pretty-printed JSON
mronc config.mron | jq .
```

### Build from source

```sh
make          # produces mronc (or mronc.exe on Windows)
make test     # runs the test suite
```

Requires a C99 compiler (GCC, Clang, MSVC).

## Examples

Every MRON snippet below is shown alongside its JSON equivalent.

### Simple key-value pairs

```mron
name     "Alice"
age      25
is-adult yes
```

```json
{
  "name": "Alice",
  "age": 25,
  "is-adult": true
}
```

### Nested records

Use `{ }` to create nested objects — just like JSON, but without colons or commas.

```mron
server {
    host "localhost"
    port 8080
    ssl  no
    cors {
        origins [ "http://localhost:3000" "https://example.com" ]
        methods [ "GET" "POST" "PUT" ]
    }
}
```

```json
{
  "server": {
    "host": "localhost",
    "port": 8080,
    "ssl": false,
    "cors": {
      "origins": ["http://localhost:3000", "https://example.com"],
      "methods": ["GET", "POST", "PUT"]
    }
  }
}
```

### Lists

Square brackets hold space-separated values — no commas needed.

```mron
hobbies [ "soccer" "violin" "cooking" ]
```

```json
{ "hobbies": ["soccer", "violin", "cooking"] }
```

Lists can also hold records:

```mron
databases [
    {
        name "primary"
        host "db1.example.com"
        port 5432
    }
    {
        name "replica"
        host "db2.example.com"
        port 5432
    }
]
```

### CSV-style lists (tabular data)

This is MRON's secret weapon. Define column headers in `( )`, then list the values row by row inside `[ ]`. MRON groups them into records automatically.

```mron
employees (name department salary) [
    "Alice"   "Engineering" 95_000
    "Bob"     "Design"      88_000
    "Charlie" "Marketing"   78_500
]
```

```json
{
  "employees": [
    { "name": "Alice",   "department": "Engineering", "salary": 95000 },
    { "name": "Bob",     "department": "Design",      "salary": 88000 },
    { "name": "Charlie", "department": "Marketing",   "salary": 78500 }
  ]
}
```

Think of it as embedding a spreadsheet inside your config file — columns stay aligned, and every row becomes a full object in JSON.

### Comments

```mron
# Single-line comments start with #

###
Multi-line comments are wrapped
in triple hashes.
###

timeout 30   # comments can follow values too
```

### Numbers

Numbers support underscores and commas as optional visual separators — they're stripped during parsing.

```mron
simple    42
negative  -7
decimal   3.14
big       1_000_000
european  1,500
```

All of the above are valid. `1_000_000` and `1,000,000` both parse to `1000000`.

### Booleans and null

```mron
enabled   true
verbose   yes      # synonym for true
disabled  false
quiet     no       # synonym for false
nothing   null
```

### Full example

```mron
# Application configuration
app-name    "My Service"
version     "2.1.0"
debug       no

server {
    host    "0.0.0.0"
    port    8080
    workers 4
}

database {
    url         "postgres://localhost:5432/mydb"
    pool-size   10
    timeout     30
}

features [ "auth" "logging" "rate-limit" ]

admins (name email role) [
    "Alice"   "alice@example.com"   "owner"
    "Bob"     "bob@example.com"     "editor"
]
```

## Use cases

- **Application config files** — cleaner than JSON, safer than YAML
- **Static data / seed files** — CSV-style lists are perfect for tabular data like users, products, translations
- **CI/CD pipelines** — human-readable definitions that are easy to diff
- **API fixture data** — write test payloads quickly without quote/comma fatigue
- **Anywhere you'd reach for JSON or YAML** — MRON round-trips losslessly to JSON

## Syntax reference

### Whitespace

All whitespace (spaces, tabs, newlines) is treated uniformly as a delimiter. Indentation is cosmetic — use whatever style you like.

### Keys

| Rule | Detail |
|---|---|
| Allowed characters | Letters, digits, `_`, `-` |
| Must start with | A letter or `_` |
| Minimum length | 1 character |

### Values

| Type | Syntax | JSON equivalent |
|---|---|---|
| **String** | `"hello"` | `"hello"` |
| **Number** | `42`, `3.14`, `1_000` | `42`, `3.14`, `1000` |
| **Boolean** | `true` / `yes` / `false` / `no` | `true` / `false` |
| **Null** | `null` | `null` |
| **Record** | `{ key value ... }` | `{ "key": value, ... }` |
| **List** | `[ value value ... ]` | `[value, value, ...]` |
| **CSV list** | `(col1 col2) [ v1 v2 ... ]` | `[{"col1":v1,"col2":v2}, ...]` |

### Strings

- Enclosed in double quotes `"`
- Must be on a single line
- Supports escape sequences: `\"` `\\` `\/` `\n` `\t` `\r` `\b` `\f`

### Comments

- **Single-line:** `# everything after the hash`
- **Multi-line:** `### ... ###`

## VS Code extension

A syntax highlighting extension for `.mron` files is included in the [`mron-syntax-highlight-vscode/`](mron-vscode//) directory.

## License

MIT
