# Yolish

The official programming language of Exploidus OS.
Fast, secure, capability-aware — runs natively on Linux, macOS, and Windows (WSL).

Author: .Bhuiya  
Version: v0.3

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

```yolish
fn main() {
    let f = cap.open("/data.txt", 1)   -- explicit READ token
    let data = cap.read(f)
    y.print(data)
    cap.close(f)
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
| @intent annotations | planned v0.4 |
| @audit annotations | planned v0.5 |
| Native ELF compiler | planned v1.0 |

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

## Roadmap

- [x] v0.1 — Variables, functions, loops
- [x] v0.2 — Capability system
- [x] v0.3 — Arrays, structs, for..in, else if, match, string builtins, import, y.format, error line numbers
- [ ] v0.4 — @intent annotations (Exploidus scheduler)
- [ ] v0.5 — @audit annotations (provenance tracking)
- [ ] v0.6 — Closures / first-class functions
- [ ] v1.0 — Native ELF compiler

Full language reference: [DOCS.md](DOCS.md)