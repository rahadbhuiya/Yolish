# Yolish Programming Language 
# Version: v0.2

A fast, secure, capability-aware language — built for **Exploidus OS**, also runs natively on Linux/Mac/Windows.

Author: .Bhuiya

## Install

```bash
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make
sudo make install
```

## Run

```bash
./ys hello.y
```

---

## Syntax

### Hello World
```yolish
fn main() {
    y.print("Hello from Yolish!\n")
}
```

### Variables
```yolish
let x = 42        -- immutable
var count = 0     -- mutable
let name = "Oro"
```

### Arithmetic
```yolish
let sum  = 10 + 20
let diff = 50 - 30
let prod = 6 * 7
let div  = 100 / 4
let rem  = 17 % 5
```

### If / Else
```yolish
fn main() {
    let x = 15
    if x > 10 {
        y.print("big\n")
    } else {
        y.print("small\n")
    }
}
```

### While Loop
```yolish
fn main() {
    var i = 0
    while i < 5 {
        y.print(i)
        y.print("\n")
        i = i + 1
    }
}
```

### Functions
```yolish
fn add(a, b) {
    return a + b
}

fn main() {
    let result = add(10, 20)
    y.print("Result: ")
    y.print(result)
    y.print("\n")
}
```

---

## Capability System

Yolish has a **built-in capability system** — the only language designed specifically for Exploidus OS security model.

Every file access requires an explicit capability token. You cannot accidentally access a file without declaring intent.

### How it works

```yolish
fn main() {
    -- Open with READ capability
    let r = cap.open("/data.txt", 1)

    -- Open with WRITE capability  
    let w = cap.open("/log.txt", 2)

    -- Read data (only works if cap has READ permission)
    let data = cap.read(r)
    y.print(data)

    -- Write data (only works if cap has WRITE permission)
    cap.write(w, "Log entry\n")

    -- Always close when done
    cap.close(r)
    cap.close(w)
}
```

### Permission flags

| Value | Constant | Meaning |
|-------|----------|---------|
| `1` | `CAP_READ` | Read access |
| `2` | `CAP_WRITE` | Write access |
| `3` | `CAP_READ + CAP_WRITE` | Read and write |
| `4` | `CAP_EXEC` | Execute |

### Why this matters

In C or Python, you can open any file anywhere — there's no enforcement at language level. In Yolish:

- Every resource access is **explicit**
- Capabilities are **typed values** — you can pass them, return them, store them
- The OS (Exploidus) **validates** the capability before granting access
- When a capability goes out of scope, it is **automatically revoked**

### Capability examples

```yolish
-- Safe file copy
fn copy_file(src: str, dst: str) {
    let r = cap.open(src, 1)
    let w = cap.open(dst, 2)
    let data = cap.read(r)
    cap.write(w, data)
    cap.close(r)
    cap.close(w)
    y.print("Copied!\n")
}

-- Check permission before use
fn safe_write(path: str, data: str) {
    let f = cap.open(path, 2)
    let perm = cap.perm(f)
    if perm > 0 {
        cap.write(f, data)
        y.print("Written!\n")
    } else {
        y.print("No permission!\n")
    }
    cap.close(f)
}

fn main() {
    copy_file("/input.txt", "/output.txt")
    safe_write("/log.txt", "Hello from Yolish!\n")
}
```

---

## Builtin Functions

### I/O
| Function | Description |
|----------|-------------|
| `y.print(val)` | Print value |
| `y.println(val)` | Print with newline |
| `y.input()` | Read line from stdin |

### Utilities
| Function | Description |
|----------|-------------|
| `y.len(str)` | String length |
| `y.abs(n)` | Absolute value |
| `y.str(n)` | Convert to string |
| `y.int(s)` | Convert to integer |
| `y.exit(code)` | Exit program |

### Capabilities
| Function | Description |
|----------|-------------|
| `cap.open(path, perm)` | Open resource with permission |
| `cap.read(cap)` | Read from capability |
| `cap.write(cap, data)` | Write to capability |
| `cap.close(cap)` | Close and revoke capability |
| `cap.perm(cap)` | Get permission flags |

---

## Platforms

| Platform | Status |
|----------|--------|
| Exploidus OS | Native |
| Linux |  Native |
| macOS |  Native |
| Windows (WSL) |  Works |

---

## About

Yolish is the official programming language of **Exploidus OS** — a security-first, capability-based operating system built from scratch.

- Extension: `.y`
- Interpreter: `ys`
- Design: Secure by default, capability-aware, low-level + high-level
- Version: v0.2

## Roadmap

- [x] v0.1 — Basic interpreter (variables, functions, loops)
- [x] v0.2 — Capability system (cap.open/read/write/close)
- [ ] v0.3 — @intent annotations (Exploidus scheduler integration)
- [ ] v0.4 — @audit annotations (provenance tracking)
- [ ] v0.5 — Arrays and structs
- [ ] v0.6 — Error handling
- [ ] v1.0 — Compiler (native ELF output)
