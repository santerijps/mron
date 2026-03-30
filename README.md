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

This repository includes **mronc**, a bidirectional translator between MRON, JSON, YAML, and TOML.

```sh
# MRON → JSON  (input format detected from extension)
mronc config.mron                   # JSON to stdout
mronc config.mron -o config.json    # JSON to file

# JSON → MRON
mronc config.json                   # MRON to stdout
mronc config.json -o config.mron    # MRON to file

# Convert to any format
mronc config.mron -t yaml           # YAML to stdout
mronc config.mron -t toml           # TOML to stdout

# Pipe-friendly
mronc config.mron -p | jq .         # pretty JSON
echo '{"key":"val"}' | mronc -      # auto-detects JSON from stdin
```

### Build from source

```sh
make          # produces mronc (or mronc.exe on Windows)
make release  # optimized build (-O3, LTO, stripped)
make test     # runs the test suite
```

Requires a C99 compiler (GCC, Clang, MSVC).

### Build the language server

```sh
make lsp     # produces mron-lsp (or mron-lsp.exe on Windows)
```

The language server speaks the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) over stdio. Any editor that supports LSP can use it — VS Code, Neovim, Helix, Sublime Text, etc.

## CLI reference

```
mronc [options] <input-file> [<input-file2> ...]
mronc [options] -                (read from stdin)
mronc --diff <file1> <file2>     (semantic diff)
mronc --merge <file1> <file2>    (deep merge)
```

### Conversion options

| Flag | Description |
|---|---|
| `-o, --output <file>` | Write output to a file (or directory for batch mode) |
| `-t, --to <format>` | Force output format: `mron`, `json`, `yaml`, `toml` |
| `--from <format>` | Force input format: `mron` or `json` |
| `-p, --pretty` | Pretty-print JSON output (2-space indent) |
| `-m, --minify` | Minify output (compact MRON or compact JSON) |
| `--sort-keys` | Sort record keys alphabetically in output |
| `--indent <n>` | Set indentation width (0–16 spaces) |
| `--strip-comments` | Re-emit MRON with all comments removed |

### Query & transform

| Flag | Description |
|---|---|
| `-q, --query <path>` | Extract a value by dot-path (e.g. `server.port`, `items.0.name`) |
| `--merge <file>` | Deep-merge a second file over the input |
| `--diff <file>` | Semantic diff between input and this file |

### Validation

| Flag | Description |
|---|---|
| `-c, --check` | Validate input without producing output |
| `--strict` | Warn about empty records, empty lists, and null values (with `--check`) |
| `--schema <file>` | Validate input against a schema file |

### Other

| Flag | Description |
|---|---|
| `-w, --watch` | Watch input file and re-convert on change |
| `-v, --verbose` | Show processing details on stderr |
| `-V, --version` | Print version and exit |
| `-h, --help` | Show help message |

### Feature details

#### Multi-format output (`--to`)

By default, mronc converts MRON→JSON and JSON→MRON. Use `--to` to target any format:

```sh
mronc config.mron -t yaml       # MRON → YAML
mronc config.mron -t toml       # MRON → TOML
mronc data.json -t mron         # JSON → MRON (same as default)
mronc config.mron -t json -p    # MRON → pretty JSON
```

#### Minify (`-m`)

Produces the most compact representation possible:

```sh
mronc config.mron -m -t mron
```

```mron
name "Alice"
age 25
address {city "London" zip "SW1A"}
```

#### Sort keys (`--sort-keys`)

Outputs records with keys in alphabetical order — useful for diffing or canonical output:

```sh
mronc config.mron --sort-keys -p
```

#### Custom indentation (`--indent`)

```sh
mronc config.mron -p --indent 4     # 4-space JSON
mronc config.mron -t mron --indent 8  # 8-space MRON
```

#### Query (`-q`)

Extract a single value by dot-path. Supports record keys and list indices:

```sh
mronc config.mron -q server.port       # → 8080
mronc config.mron -q admins.0.name     # → Alice
mronc config.mron -q features          # → ["auth","logging","rate-limit"]
```

Records and lists at the queried path are printed as JSON. Scalars are printed as plain text.

#### Deep merge (`--merge`)

Combines two files by deep-merging the second over the first. Records are merged key-by-key; non-record values are replaced entirely:

```sh
mronc base.mron --merge overrides.mron -o merged.json
```

#### Semantic diff (`--diff`)

Compares two files structurally (ignoring formatting, comments, and key order) and reports added, removed, and changed values:

```sh
mronc old.mron --diff new.mron
```

```
  name: changed ("Alice" -> "Bob")
  age: removed (was 25)
  email: added ("bob@example.com")
2 difference(s) found.
```

Exit code is 0 if identical, 1 if differences were found.

#### Strip comments (`--strip-comments`)

Re-emits MRON with all comments removed — useful for producing clean output:

```sh
mronc config.mron --strip-comments > clean.mron
```

#### Stdin auto-detection

When reading from `-` (stdin) without `--from`, mronc examines the first non-whitespace character to guess the format: `{` or `[` → JSON, anything else → MRON.

```sh
echo '{"hello":"world"}' | mronc -             # detected as JSON → MRON
cat config.mron | mronc - -t json              # detected as MRON → JSON
```

#### Batch mode

Pass multiple input files. With `-o` pointing to a directory (trailing `/`), each file gets its own output:

```sh
mronc a.mron b.mron c.mron -o output/
# produces output/a.json, output/b.json, output/c.json
```

#### Watch mode (`-w`)

Watches a single file and re-converts whenever it changes:

```sh
mronc config.mron -w -o config.json    # re-converts on every save
```

Polls every 500ms. Press Ctrl+C to stop.

#### Strict validation (`--strict`)

Use with `--check` to get additional warnings about stylistic issues:

```sh
mronc config.mron -c --strict
```

Warns about:
- Empty records (`{}`)
- Empty lists (`[]`)
- Null values

#### Schema validation (`--schema`)

Validate that a data file conforms to a schema. The schema is itself an MRON (or JSON) file where leaf string values act as type constraints:

```mron
# schema.mron
name    "string"
age     "number"
active  "bool"
address {
    city "string"
    zip  "string"
}
```

```sh
mronc data.mron --schema schema.mron -c
```

Schema rules:
- `"string"`, `"number"`, `"bool"`, `"any"` — leaf type constraints
- Records in the schema require matching records in data (keys are checked)
- A list with one element means all data items must match that element's type

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
- **CI/CD pipelines** — human-readable definitions that are easy to diff; use `--diff` to compare configs semantically
- **API fixture data** — write test payloads quickly without quote/comma fatigue
- **Config management** — `--merge` overlays let you maintain base + environment overrides
- **Format migration** — convert freely between MRON, JSON, YAML, and TOML
- **Schema enforcement** — validate configs against a schema before deployment
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

## Editor support

### VS Code extension

A full-featured extension for `.mron` files is included in the [`mron-vscode/`](mron-vscode/) directory. It bundles syntax highlighting via TextMate grammar and an LSP client that connects to `mron-lsp`.

**Setup:**
1. `make lsp` to build `mron-lsp` (place the binary on your PATH or next to the extension folder).
2. Copy or symlink `mron-vscode/` into `~/.vscode/extensions/mron`.
3. `cd mron-vscode && npm install` to fetch the `vscode-languageclient` dependency.
4. Restart VS Code and open any `.mron` file.

### Language server features

The `mron-lsp` binary provides the following LSP capabilities:

| Feature | Description |
|---|---|
| **Diagnostics** | Real-time error reporting — lexer errors, unmatched delimiters, CSV column count mismatches, and schema violations |
| **Document Symbols** | Hierarchical outline of all keys with type-aware icons |
| **Folding Ranges** | Fold `{}`/`[]`/`()` blocks and `### ###` comment blocks |
| **Hover** | Type information for keys, values, and CSV columns |
| **Go to Definition** | Jump from a CSV value to its column header |
| **Completions** | Boolean/null keywords, sibling keys, and existing key names |
| **Code Actions** | Normalize `yes`/`no` → `true`/`false`, sort keys alphabetically, expand CSV to record syntax |
| **Semantic Tokens** | Rich token classification — keys, CSV headers, strings, numbers, keywords, operators |
| **Formatting** | Whole-document reformat via parse→re-emit |
| **Rename** | Rename keys or CSV column headers across the document |
| **Document Links** | Clickable file paths (`.mron`, `.json`, `.yaml`, `.toml`) in string values |
| **Selection Range** | Smart expand selection: token → enclosing block → document |
| **Inlay Hints** | Ghost-text column labels on CSV values (MRON-specific) |
| **Color Provider** | Inline color swatches for hex colors (`#RGB`, `#RRGGBB`, `#RRGGBBAA`) in strings |
| **Code Lens** | Top-of-file key count + "Preview as JSON/YAML/TOML" commands |

### Configuration

The VS Code extension exposes two settings:

| Setting | Default | Description |
|---|---|---|
| `mron.serverPath` | `""` | Custom path to the `mron-lsp` binary. If empty, the extension looks next to the extension folder and then on PATH. |
| `mron.schema` | `""` | Path to an MRON schema file for live validation. |

## License

MIT
