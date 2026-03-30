# MRON Language Support for VS Code

Full language support for `.mron` files (Minimal Rich Object Notation) — syntax highlighting, diagnostics, completions, hover, and more.

## Features

### Syntax highlighting (TextMate grammar)

- **Keys** — highlighted as tags
- **Strings** — with escape sequence support
- **Numbers** — integers and decimals, with `_` and `,` separators
- **Booleans** — `true`, `false`, `yes`, `no`
- **`null`**
- **Records** `{ }` and **Lists** `[ ]`
- **CSV-style headers** `(key1 key2)` — header keys highlighted distinctly
- **Comments** — single-line `#` and multi-line `### ... ###`

### Language server features (via `mron-lsp`)

- **Diagnostics** — real-time error reporting: syntax errors, unmatched delimiters, CSV column count mismatches, schema violations
- **Document Symbols** — hierarchical outline of all keys (Ctrl+Shift+O)
- **Folding Ranges** — fold `{}`/`[]`/`()` blocks and `### ###` comment blocks
- **Hover** — type information for keys, values, and CSV columns
- **Go to Definition** — jump from a CSV value to its column header (F12)
- **Completions** — boolean/null keywords, sibling keys, existing key names
- **Code Actions** — normalize `yes`/`no` → `true`/`false`, sort keys, expand CSV to record syntax
- **Semantic Tokens** — rich highlighting beyond the TextMate grammar (keys, CSV headers, strings, numbers, keywords, operators)
- **Formatting** — format document (Shift+Alt+F) via parse → re-emit
- **Rename** — rename keys or CSV column headers across the document (F2)
- **Document Links** — clickable file paths in string values
- **Selection Range** — smart expand selection (Shift+Alt+→): token → block → document
- **Inlay Hints** — ghost-text column labels on CSV values
- **Color Provider** — inline color swatches for hex colors in strings
- **Code Lens** — top-of-file key count and "Preview as JSON/YAML/TOML" commands

### Editor integration

- Auto-closing pairs for `{}`, `[]`, `()`, `""`
- Bracket matching
- Code folding for records and lists

## Installation

### Prerequisites

Build the `mron-lsp` language server from the repository root:

```sh
make lsp    # produces mron-lsp (or mron-lsp.exe on Windows)
```

Place the binary on your PATH, or next to the extension folder.

### Steps

1. Copy or symlink this folder into your VS Code extensions directory:
   - **Windows:** `%USERPROFILE%\.vscode\extensions\mron`
   - **macOS/Linux:** `~/.vscode/extensions/mron`
2. Install the Node.js dependency:
   ```sh
   cd mron-vscode
   npm install
   ```
3. Restart VS Code.
4. Open any `.mron` file — syntax highlighting and language features will activate automatically.

Alternatively, press `F5` in VS Code with this folder open to launch an Extension Development Host.

## Configuration

| Setting | Default | Description |
|---|---|---|
| `mron.serverPath` | `""` | Custom path to the `mron-lsp` binary. If empty, looks next to the extension and then on PATH. |
| `mron.schema` | `""` | Path to an MRON schema file for live validation. |
