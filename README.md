# Yolish

The official programming language of Exploidus OS.
Fast, secure, capability-aware — runs natively on Linux, macOS, and Windows (WSL).

Author: .Bhuiya
Version: v0.6

---

## Install

```bash
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make
sudo make install
```

## Run

```bash
ys hello.y    -- run a file
ys            -- start REPL
```

## Hello World

```yolish
fn main() {
    y.println("Hello from Yolish!")
}
```

---

## What makes Yolish different?

**Capability system** — every resource access requires an explicit capability token, enforced at the language level.

**Annotations** — `@intent` and `@audit` give the Exploidus OS scheduler and auditor full visibility before execution.

**First-class functions** — closures, higher-order functions (`y.map`, `y.filter`, `y.reduce`), and anonymous function literals.

**Error handling** — `try/catch/throw` with full propagation through function calls.

**Standard library** — `y.math.*`, `y.string.*`, `y.array.*` namespaced builtins, plus string interpolation and a module system.

---

## Feature overview

| Feature | Status |
|---------|--------|
| Variables (let / var) | done |
| Functions + recursion | done |
| if / else if / else | done |
| while loop | done |
| for item in array / for i in range | done |
| Match expressions | done |
| Arrays | done |
| Structs | done |
| String builtins | done |
| y.format interpolation | done |
| import (multi-file) | done |
| Capability system | done |
| Error messages with line numbers | done |
| @intent and @audit annotations | done |
| Closures / first-class functions | done |
| y.map / y.filter / y.reduce / y.each | done |
| try / catch / throw | done |
| Type system (y.typeof, y.is_*) | done |
| REPL mode | done |
| String interpolation (`"Hello {name}!"`) | done |
| Error objects (y.error, message/code fields) | done |
| Module system (import as namespace) | done |
| Standard library (y.math, y.string, y.array) | done |
| Native ELF compiler | planned v1.0 |

---


---

## Annotation logs

```bash
./ys examples/ann_test.y               -- both in terminal
./ys examples/ann_test.y 2>/dev/null   -- program output only
./ys examples/ann_test.y 2>audit.log   -- save annotation logs separately
```

---

## Debug build

```bash
make debug
./ys_debug myfile.y
```

---

## Platforms

| Platform | Status |
|----------|--------|
| Exploidus OS | Native |
| Linux | Native |
| macOS | Native |
| Windows (WSL) | Works |

---

## Roadmap

- [x] v0.1 — Variables, functions, loops
- [x] v0.2 — Capability system
- [x] v0.3 — Arrays, structs, match, for-in, string builtins, import, y.format
- [x] v0.4 — @intent and @audit annotations
- [x] v0.5 — Closures, try/catch/throw, type system, REPL
- [x] v0.6 — String interpolation, error objects, module system, standard library (y.math/y.string/y.array)
- [x] v0.6.1 — Bug fixes: `y.format` positional placeholders, segfault on recursive functions, `throw` not propagating through `for` loops, duplicate `#include`, AST pool corruption in string interpolation, REPL version string
- [ ] v0.7 — Match guards and binding
- [ ] v1.0 — Native ELF compiler

## Known Limitations (v0.6)

- `match` as a **direct top-level assignment** returns `nil` — use a wrapper function instead. `match` works correctly as a return value inside a function body.
- Strings are capped at 255 characters; arrays at 64 elements at creation.
- Floats are fixed-point ×1000 (3 decimal places).

See [DOCS.md](DOCS.md) for full details and workarounds.

---

Full language reference: [DOCS.md](DOCS.md)