# Yolish — Roadmap

---

## Vision

Yolish is the scripting and automation language of Exploidus OS.

The goal is simple: a lightweight, capability-aware language that makes
it easy to write OS tools, config scripts, and system utilities — with
security built in from the start.

Yolish is not trying to replace Rust or C. It is the glue language of
Exploidus — readable, safe, and practical.

---

## Completed

| Version | What shipped |
|---------|-------------|
| v0.1 | Variables, functions, loops |
| v0.2 | Capability system (`cap.open`, `cap.read`, `cap.write`, `cap.close`) |
| v0.3 | Arrays, structs, match, for-in, string builtins, import |
| v0.4 | Annotations (`@intent`, `@audit`) |
| v0.5 | Closures, `try`/`catch`/`throw`, type system, REPL |
| v0.6 | String interpolation, error objects, module system, stdlib |
| v0.7 | `impl` methods, `y.input`, functional array builtins, dynamic allocation |
| v0.8 | Match guards and pattern binding |
| **v1.0** | **Native x86-64 compiler — Linux, Windows, macOS; logo; colored REPL** |

---

## Upcoming

### v1.1 — Native Compiler Expansion
- Float, arrays, and structs in native compiler
- Exploidus OS native binary target
- `y.print`/`y.println` for all types natively

### v1.2 — File I/O
- File read/write in native compiler
- `y.fs.read(path)`, `y.fs.write(path, data)`, `y.fs.exists(path)`
- Mapped to capability system — no file access without `cap.open`

### v1.3 — Process and System
- `process.spawn(cmd)` — run a subprocess
- `process.env(key)` — read environment variables
- `sys.exit(code)`
- Exploidus OS system call integration

### v1.4 — Error Messages & Diagnostics
- Source locations in all error messages (file, line, column)
- Better parse error recovery — report multiple errors in one run
- Warning system for common mistakes (unused variables, unreachable code)

### v1.5 — Runtime Improvements
- Garbage collector (mark-and-sweep)
- Better error messages with file, line, and column:
  ```
  main.y:15:8 — unexpected token '}'
               expected expression
  ```
- Runtime stack traces on uncaught throws

### v1.6 — Module System
- Relative imports (`import "./utils.y"`)
- Circular import detection
- Import caching (each file loaded once)

### v1.7 — Standard Library Expansion
| Library | What it adds |
|---------|-------------|
| `y.fs` | `read`, `write`, `exists`, `list`, `mkdir`, `delete` |
| `y.json` | `parse()`, `stringify()` |
| `y.time` | `now()`, `sleep()`, `format()` |
| `y.env` | `get(key)`, `set(key, val)`, `all()` |
| `y.path` | `join()`, `basename()`, `dirname()`, `ext()` |

### v2.0 — Self-Hosting
- Yolish compiles itself
- Bytecode VM for faster interpretation
- Constant folding and dead code elimination

### v2.1 — Tooling
- Built-in test runner:
  ```yolish
  test "addition works" {
      assert(add(2, 3) == 5)
  }
  ```
- `ys test` — run all `test` blocks in a file
- `ys fmt` — auto-format `.y` files
- `ys check` — type-check and lint without running

### v2.2 — Enums
- Simple enums for better pattern matching:
  ```yolish
  enum Status { Ok  NotFound  Error }

  match response.status {
      Status.Ok       => y.println("success")
      Status.NotFound => y.println("not found")
      Status.Error    => y.println("error")
  }
  ```

### v3.0 — Exploidus Integration
- Deep Exploidus OS kernel integration
- Capability tokens validated by kernel (not just runtime)
- `@intent` annotations wired to OS scheduler directly
- Sandboxed script execution model
- Yolish as the official shell scripting language of Exploidus

---

## What Yolish will NOT do

These are intentionally out of scope. Yolish stays focused.

- No generics or traits — use structs and closures instead
- No async/await or threading — Exploidus handles concurrency at the OS level
- No macros or metaprogramming — keep the language readable
- No GUI toolkit — that belongs in a separate Exploidus UI framework
- No package registry — modules are plain `.y` files, no dependency hell
- No FFI / C interop — capabilities handle OS access; Yolish is not a systems language

---

## Development Philosophy

- Simplicity over features
- Security by default — no resource access without a capability
- Lightweight — fast startup, low memory, no VM overhead for simple scripts
- Readable — someone unfamiliar with Yolish can still read it
- Focused — does one thing well: scripting for Exploidus OS