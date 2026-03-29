# MRON Language Support for VS Code

Syntax highlighting for `.mron` files (Minimal Rich Object Notation).

## Features

- Syntax highlighting for all MRON constructs:
  - **Keys** — highlighted as tags
  - **Strings** — with escape sequence support
  - **Numbers** — integers and decimals, with `_` and `,` separators
  - **Booleans** — `true`, `false`, `yes`, `no`
  - **`null`**
  - **Records** `{ }` and **Lists** `[ ]`
  - **CSV-style headers** `(key1 key2)` — header keys highlighted distinctly
  - **Comments** — single-line `#` and multi-line `### ... ###`
- Auto-closing pairs for `{}`, `[]`, `()`, `""`
- Bracket matching
- Code folding for records and lists

## Installation (Development)

1. Copy or symlink the `vscode-extension` folder into your VS Code extensions directory:
   - **Windows:** `%USERPROFILE%\.vscode\extensions\mron`
   - **macOS/Linux:** `~/.vscode/extensions/mron`
2. Restart VS Code.
3. Open any `.mron` file — syntax highlighting will activate automatically.

Alternatively, press `F5` in VS Code with this folder open to launch an Extension Development Host.
