<p align="center">
  <img src="icons/logo.svg" width="120" height="120" alt="Yolish Logo"/>
</p>

<h1 align="center">Yolish</h1>

<p align="center">
  <strong>The official programming language of Exploidus OS.</strong><br/>
  Fast, expressive, capability-aware — with a native x86-64 compiler.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-v1.0-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-7b2fff?style=flat-square"/>
  <img src="https://img.shields.io/badge/compiler-x86--64%20native-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-MIT-gray?style=flat-square"/>
</p>

---

| | |
|--|--|
| **Author** | .Bhuiya |
| **Version** | v1.0 |
| **Extension** | `.y` |
| **Compiler/Interpreter** | `ys` / `ys.exe` |
| **Targets** | Linux ELF64 · Windows PE32+ · macOS Mach-O |

---

## Install

**No dependencies required.** Download the binary and run.

### Windows

1. Download [`ys.exe`](../../releases/latest/download/ys.exe)
2. Put it anywhere (e.g. `C:\Tools\ys.exe`)
3. Add that folder to your PATH — or run the auto-installer:

```powershell
# Run once as Administrator — downloads ys.exe and adds to PATH automatically
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

After that: open any terminal and type `ys`.

### Linux

```sh
curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh
```

Or manually:
```sh
curl -L https://github.com/rahadbhuiya/yolish/releases/latest/download/ys-linux -o ys
chmod +x ys
sudo mv ys /usr/local/bin/
```

### macOS

```sh
curl -fsSL https://raw.githubusercontent.com/rahadbhuiya/yolish/master/install.sh | sh
```

### Build from source (optional)

Only needed if you want to hack on Yolish itself:

```bash
git clone https://github.com/rahadbhuiya/yolish
cd yolish
make        # requires gcc or clang — no other dependencies
```

See [BUILD.md](BUILD.md) for detailed build instructions.

---

## Quick Start

```yolish
-- hello.y
fn main() {
    y.println("Hello from Yolish!")
}
main()
```

```bash
ys hello.y              # interpret
ys -c hello.y           # compile to native binary
./hello                 # run the native binary
```

---

## Usage

```
ys                              Start interactive REPL
ys <file.y>                     Interpret a file
ys -c <file.y>                  Compile for current OS
ys -c <file.y> -o <name>        Compile with custom output name
ys -c <file.y> --target linux   Compile → Linux ELF64
ys -c <file.y> --target windows Compile → Windows PE32+
ys -c <file.y> --target macos   Compile → macOS Mach-O
ys --help                       Show help
```

---

## Language at a Glance

```yolish
-- Variables
let name  = "Yolish"     -- immutable
var count = 0            -- mutable

-- Functions + recursion
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

-- Loops
for i in 0..10 { y.print(i) }

var i = 0
while i < 5 { i = i + 1 }

-- Match expression (returns a value)
let grade = match score {
    90..100      => "A"
    80..90       => "B"
    n if n >= 60 => "C"
    _            => "F"
}

-- Structs + impl methods
struct Circle { radius }
impl Circle {
    fn area(self) {
        return y.math.pi * self.radius * self.radius
    }
}
let c = Circle { radius: 5 }
y.println(c.area())

-- Arrays + functional builtins
let nums   = [1, 2, 3, 4, 5]
let evens  = y.filter(nums, fn(x){ return x % 2 == 0 })
let doubled = y.map(nums, fn(x){ return x * 2 })
let total   = y.sum(nums)

-- String interpolation
let msg = "Hello {name}, score = {score}"

-- Error handling
try {
    throw "oops"
} catch(e) {
    y.println("caught: " + e)
}

-- Closures
let square = fn(x) { return x * x }

-- Capability annotations (Exploidus OS)
@cap(net.read, fs.write)
fn fetch_and_save(url, path) { ... }
```

---

## Feature Table

| Feature | Status |
|---------|--------|
| Variables (`let` / `var`) | [x] |
| Functions + recursion | [x] |
| `if` / `else if` / `else` | [x] |
| `while` loop + `break` / `continue` | [x] |
| `for i in lo..hi` range loop | [x] |
| `for item in array` loop | [x] |
| `for ch in string` character loop | [x] |
| `match` expression + guards + binding | [x] |
| Arrays (dynamic, max 512 elements) | [x] |
| Structs + `impl` methods + `self` | [x] |
| Method chaining | [x] |
| Closures / first-class functions | [x] |
| `try` / `catch` / `throw` | [x] |
| String interpolation `"Hello {name}"` | [x] |
| Multiline strings (backtick) | [x] |
| Raw strings `r"..."` | [x] |
| `y.map` / `y.filter` / `y.reduce` / `y.each` | [x] |
| `y.sort` / `y.zip` / `y.flatten` / `y.sum` / `y.range` | [x] |
| `y.math.*` — sqrt, pow, sin, cos, pi, ... | [x] |
| `y.string.*` — upper, lower, split, join, trim, ... | [x] |
| `y.input` / `y.input_int` / `y.input_float` | [x] |
| Type system — `y.typeof`, `y.is_*`, conversions | [x] |
| Capability system `@cap`, `@intent`, `@audit` | [x] |
| Module / import system | [x] |
| Error objects `y.error(msg, code)` | [x] |
| REPL with colored banner | [x] |
| **Native x86-64 compiler** | [x] **v1.0** |
| Native → Linux ELF64 | [x] |
| Native → Windows PE32+ (with icon) | [x] |
| Native → macOS Mach-O | [x] |
| Native → Exploidus | [ ] v1.1 |
| Float / arrays / structs in native compiler | [ ] v1.1 |
| File I/O in native compiler | [ ] v1.2 |
| Self-hosting (Yolish compiles Yolish) | [ ] v2.0 |

---

## Platforms

| Platform | Interpreter | Native Compiler Output |
|----------|-------------|------------------------|
| Linux       | Yes | ELF64 static binary     |
| macOS       | Yes | Mach-O 64-bit           |
| Windows     | Yes | PE32+ with icon         |
| Exploidus OS| Yes | coming v1.1             |

---

## Standard Library Overview

| Module | Functions |
|--------|-----------|
| **I/O** | `y.print`, `y.println`, `y.input`, `y.input_int`, `y.input_float` |
| **String** | `y.len`, `y.upper`, `y.lower`, `y.trim`, `y.split`, `y.join`, `y.contains`, `y.replace`, `y.substr`, `y.reverse`, `y.repeat`, `y.format`, `y.starts_with`, `y.ends_with`, `y.index_of` |
| **Array** | `y.push`, `y.pop`, `y.slice`, `y.len`, `y.reverse`, `y.sort`, `y.map`, `y.filter`, `y.reduce`, `y.each`, `y.zip`, `y.flatten`, `y.sum`, `y.range` |
| **Math** | `y.math.sqrt`, `y.math.pow`, `y.math.abs`, `y.math.floor`, `y.math.ceil`, `y.math.round`, `y.math.min`, `y.math.max`, `y.math.clamp`, `y.math.sign`, `y.math.pi`, `y.math.log`, `y.math.sin`, `y.math.cos`, `y.math.tan` |
| **Type** | `y.typeof`, `y.is_int`, `y.is_str`, `y.is_float`, `y.is_bool`, `y.is_array`, `y.is_nil`, `y.int`, `y.str`, `y.float`, `y.bool` |
| **Error** | `y.error(msg, code)` |
| **Capability** | `y.capabilities()`, `y.has_cap(caps, name)` |

---

## Roadmap

### Vision

Yolish is the scripting and automation language of Exploidus OS —
lightweight, capability-aware, and focused. Not trying to replace C or Rust.
Just a clean, safe language for OS tools, config scripts, and system utilities.

### Release History

| Version | What shipped |
|---------|-------------|
| v0.1 | Variables, functions, loops |
| v0.2 | Capability system |
| v0.3 | Arrays, structs, match, for-in, builtins, import |
| v0.4 | Annotations (`@intent`, `@audit`) |
| v0.5 | Closures, `try`/`catch`/`throw`, type system, REPL |
| v0.6 | String interpolation, error objects, module system, stdlib |
| v0.7 | `impl` methods, `y.input`, functional builtins, dynamic allocation |
| v0.8 | Match guards and pattern binding |
| **v1.0** | **Native x86-64 compiler — Linux, Windows, macOS** |

### Upcoming

| Version | Plan |
|---------|------|
| v1.1 | Float, arrays, structs in native compiler; Exploidus target |
| v1.2 | File I/O (`y.fs.*`) in native compiler |
| v1.3 | Process spawning, environment variables, system calls |
| v1.5 | Garbage collector; better error messages with file/line/column |
| v1.6 | Improved module system (relative imports, circular detection) |
| v1.7 | Stdlib expansion: `y.json`, `y.time`, `y.env`, `y.path` |
| v2.0 | Self-hosting — Yolish compiles itself; bytecode VM |
| v2.1 | Built-in test runner (`ys test`), formatter (`ys fmt`) |
| v2.2 | Enums |
| v3.0 | Deep Exploidus OS integration; official shell language |

See [ROADMAP.md](ROADMAP.md) for full details and what Yolish will NOT do.

### Development Philosophy

- Simplicity over features
- Security by default — no resource access without a capability
- Focused — does one thing well: scripting for Exploidus OS

## Annotation / Audit Logs

```bash
ys examples/ann_test.y               # both in terminal
ys examples/ann_test.y 2>/dev/null   # program output only
ys examples/ann_test.y 2>audit.log   # save annotation logs separately
```

---

## Contributing

1. Fork the repo
2. Make changes
3. `make` to build, test with `examples/`
4. Open a pull request

---

See [DOCS.md](DOCS.md) for the full language reference.  
See [BUILD.md](BUILD.md) for detailed build and release instructions.  
See [ROADMAP.md](ROADMAP.md) for the full detailed roadmap.  
See [LICENSE](LICENSE) for the full MIT license text.