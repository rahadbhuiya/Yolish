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
| **v1.1** | **Float arithmetic (SSE2), array literals in native compiler** |
| **v1.2** | **File I/O — `y.fs.read/write/append/exists/list/mkdir/delete/rename/size/is_dir`** |
| **v1.3** | **Process & system — `process.spawn`, `process.env`, `sys.exit`, `sys.platform`** |
| **v1.4** | **Error messages — `file:line:col` format, typo suggestions (Levenshtein)** |
| **v1.6** | **Module system — relative imports (`./utils.y`), circular detection, import caching** |
| **v1.5** | **Garbage Collector — mark-and-sweep; `gc.collect()`, `gc.stats()`** |
| **v1.7** | **Stdlib expansion — `y.json`, `y.time`, `y.env`, `y.path`** |
| **v2.2** | **Enums — `enum Status { Ok NotFound Error }` with match integration** |

---

## Upcoming

### v1.8 — Native Compiler Expansion
- Exploidus OS native binary target
- Structs in native compiler
- `y.print`/`y.println` for all types natively compiled

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