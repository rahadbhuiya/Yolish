# Yolish for VS Code

Official VS Code extension for the [Yolish](https://github.com/rahadbhuiya/yolish) programming language.

## Features

- Syntax highlighting for `.y` files
- Run button in the editor title bar (`F5`)
- Compile button (`Ctrl+Shift+B`)
- Right-click menu: Run / Compile
- Status bar: Run Yolish / Compile buttons
- Code snippets (`fn`, `if`, `while`, `for`, `match`, `struct`, `impl`, `try`, `main`, ...)
- Auto-closing brackets and quotes
- Yolish Dark theme

## Requirements

Install Yolish first:

**Windows**: download `ys.exe` from [Releases](https://github.com/rahadbhuiya/yolish/releases) and add to PATH,
or run the installer:
```powershell
powershell -ExecutionPolicy Bypass -File install.ps1
```

**Linux / macOS:**
```sh
curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh
```

## Usage

**Run a file:**
- Open any `.y` file
- Press `F5` or click the `Run Yolish` button in the title bar
- Output appears in the integrated terminal

**Compile to native binary:**
- Press `Ctrl+Shift+B` or click the `Compile` button
- Produces a native executable next to the source file

**CMD / PowerShell (without VS Code):**
```cmd
ys hello.y            -- run
ys -c hello.y         -- compile
ys                    -- REPL
```

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `yolish.executablePath` | `ys` | Path to ys executable |
| `yolish.compileTarget` | `auto` | Compile target: `auto`, `linux`, `windows`, `macos` |

## Snippets

| Prefix | Expands to |
|--------|-----------|
| `fn` | Function definition |
| `main` | Main function + call |
| `if` | If statement |
| `ife` | If/else |
| `while` | While loop |
| `for` | For range loop |
| `fora` | For array loop |
| `match` | Match expression |
| `struct` | Struct definition |
| `impl` | Impl block |
| `try` | Try/catch |
| `let` | Let variable |
| `var` | Var variable |
| `pl` | y.println |
| `pr` | y.print |
| `map` | y.map |
| `filter` | y.filter |
| `reduce` | y.reduce |
| `fnx` | Closure / anonymous fn |

## Theme

Open Command Palette (`Ctrl+Shift+P`) → `Color Theme` → `Yolish Dark`.