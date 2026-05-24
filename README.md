# Yolish Programming Language 
# Version: v0.1

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
ys hello.y
```

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

### String Operations
```yolish
fn main() {
    let s = "Hello Yolish"
    let l = y.len(s)
    y.print("Length: ")
    y.print(l)
    y.print("\n")
}
```

### User Input
```yolish
fn main() {
    y.print("Enter name: ")
    let name = y.input()
    y.print("Hello, ")
    y.print(name)
    y.print("!\n")
}
```

### Exit
```yolish
fn main() {
    y.print("Bye!\n")
    y.exit(0)
}
```

## Builtins

| Function | Description |
|----------|-------------|
| `y.print(val)` | Print value |
| `y.println(val)` | Print with newline |
| `y.input()` | Read line from stdin |
| `y.len(str)` | String length |
| `y.abs(n)` | Absolute value |
| `y.exit(code)` | Exit program |

## Platforms

| Platform | Status |
|----------|--------|
| Exploidus OS | Native |
| Linux |  Native |
| macOS |  Native |
| Windows (WSL) |  Works |

## About

Yolish is the official programming language of **Exploidus OS** — a security-first, capability-based operating system built from scratch.

- Extension: `.y`
- Interpreter: `ys`
- Design: Rust/Zig-inspired syntax, Python-like ease

## License MIT
