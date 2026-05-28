# Yolish

The official programming language of Exploidus OS.
Fast, secure, capability-aware — runs natively on Linux, macOS, and Windows (WSL).

Author: .Bhuiya  
Version: v0.4

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
ys hello.y
```

## Hello World

```yolish
fn main() {
    y.println("Hello from Yolish!")
}
```

---

## What makes Yolish different?

**Capability system** — every file, network, or process access requires an explicit capability token. You cannot accidentally touch a resource you did not declare intent for. This is enforced at the language level, not the OS level — making Yolish uniquely suited for Exploidus OS's security model.

**Annotations** — functions can declare their resource intent and audit requirements directly in source code, giving the Exploidus OS scheduler and security auditor full visibility before execution.

```yolish
@intent("io")
fn read_config(path) {
    let f = cap.open(path, 1)
    let data = cap.read(f)
    cap.close(f)
    return data
}

@audit("auth")
fn login(user, pass) {
    if user == "admin" {
        if pass == "1234" { return true }
    }
    return false
}
```

---

## Feature overview

| Feature | Status |
|---------|--------|
| Variables (let / var) | done |
| Functions + recursion | done |
| if / else if / else | done |
| while loop | done |
| for item in array | done |
| for i in 0..10 range | done |
| Match expressions | done |
| Arrays | done |
| Structs | done |
| String builtins | done |
| y.format interpolation | done |
| import (multi-file) | done |
| Capability system | done |
| Error messages with line numbers | done |
| @intent annotations | done |
| @audit annotations | done |
| Closures / first-class functions | planned v0.5 |
| Native ELF compiler | planned v1.0 |

---

## Repository structure

```
yolish/
├── README.md               -- this file
├── DOCS.md                 -- full language reference
├── .gitignore
├── Makefile
├── yolish.h                -- types, structs, prototypes
├── lexer.c                 -- tokenizer
├── parser.c                -- AST parser
├── eval.c                  -- tree-walking interpreter
├── main.c                  -- entry point
└── examples/
    ├── hello.y             -- hello world
    ├── variables.y         -- variables and types
    ├── functions.y         -- functions and recursion
    ├── arrays.y            -- arrays
    ├── structs.y           -- structs
    ├── match.y             -- match expressions
    ├── strings.y           -- string builtins
    ├── loops.y             -- while, for, fizzbuzz
    ├── cap.y               -- capability system
    └── import_example/
        ├── mathlib.y       -- reusable math library
        └── main.y          -- imports mathlib.y
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

## Debug build (AddressSanitizer + UBSan)

```bash
make debug
./ys_debug myfile.y
```

---

## Annotation logs

Annotation output (`@intent`, `@audit`) goes to `stderr` — separate from program output:

```bash
./ys program.y               -- both in terminal
./ys program.y 2>/dev/null   -- program output only
./ys program.y 2>audit.log   -- save logs separately
```

---

## Roadmap

- [x] v0.1 — Variables, functions, loops
- [x] v0.2 — Capability system
- [x] v0.3 — Arrays, structs, for..in, else if, match, string builtins, import, y.format, error line numbers
- [x] v0.4 — @intent and @audit annotations
- [ ] v0.5 — Closures / first-class functions
- [ ] v0.6 — Match guards and binding
- [ ] v1.0 — Native ELF compiler

Full language reference: [DOCS.md](DOCS.md)