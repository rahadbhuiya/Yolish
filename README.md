<p align="center">
  <img src="icons/logo.svg" width="120" height="120" alt="Yolish Logo"/>
</p>

<h1 align="center">Yolish</h1>

<p align="center">
  <strong>The official programming language of Exploidus OS.</strong><br/>
  Fast, expressive, capability-aware, with a native x86-64 compiler.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-v2.17-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-7b2fff?style=flat-square"/>
  <img src="https://img.shields.io/badge/compiler-x86--64%20native-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-MIT-gray?style=flat-square"/>
</p>

---

| | |
|--|--|
| **Author** | .Bhuiya |
| **Version** | v2.17 |
| **Extension** | `.y` |
| **Compiler/Interpreter** | `ys` / `ys.exe` |
| **Targets** | Linux ELF64 · Windows PE32+ · macOS Mach-O |

---

## Install

**No dependencies required.** Download the binary and run.

### Windows

**Option 1: GUI installer (recommended)**

1. Download [`yolish-setup.exe`](../../releases/latest/download/yolish-setup.exe)
2. Double-click → Next → Next → Finish
3. Open any new terminal and type `ys`

The installer automatically adds Yolish to your PATH and creates a
Start Menu shortcut that opens the Yolish REPL in a terminal window.
An entry in Add/Remove Programs is also created for clean uninstallation.

**Option 2: manual (no installer)**

1. Download [`ys.exe`](../../releases/latest/download/ys.exe)
2. Put it anywhere (e.g. `C:\Tools\ys.exe`)
3. Add that folder to your PATH, or run the PowerShell auto-installer:

```powershell
# Run once as Administrator
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
make        # requires gcc or clang, no other dependencies
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
ys test <file.y>                Run test blocks
ys fmt  <file.y>                Format source (prints to stdout)
ys check <file.y>               Static check without running
ys vm <file.y>                   Run via the bytecode VM (faster, full language coverage)
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

-- Match expression with guards
let grade = match score {
    90..100      => "A"
    80..90       => "B"
    n if n >= 60 => "C"
    _            => "F"
}

-- Enums (v2.2)
enum Direction { North  South  East  West }
let dir = Direction.North
match dir {
    Direction.North => y.println("going north")
    _               => y.println("other direction")
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
let nums    = [1, 2, 3, 4, 5]
let evens   = y.filter(nums, fn(x){ return x % 2 == 0 })
let doubled = y.map(nums, fn(x){ return x * 2 })
let total   = y.sum(nums)

-- String interpolation
let msg = "Hello {name}, score = {score}"

-- Backtick strings (raw, no interpolation, perfect for JSON/templates)
let json = `{"name": "Yolish", "version": 1}`

-- File I/O (v1.2)
y.fs.write("log.txt", "started\n")
let content = y.fs.read("log.txt")
y.println(y.fs.exists("log.txt"))

-- JSON (v1.7)
let obj = y.json.parse(`{"lang": "yolish", "stable": true}`)
y.println(obj.lang)
y.println(y.json.stringify(obj))

-- Process & System (v1.3)
let out = process.spawn("uname -s")
y.println(sys.platform())

-- Time (v1.7)
let now = y.time.now()
y.println(y.time.format(now, "%Y-%m-%d %H:%M:%S"))

-- Module import (v1.6: relative path, cached)
import "./utils.y"

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
| Variables (`let` / `var`) | Done |
| Functions + recursion | Done |
| `if` / `else if` / `else` | Done |
| `while` loop + `break` / `continue` | Done |
| `for i in lo..hi` range loop | Done |
| `for item in array` loop | Done |
| `for ch in string` character loop | Done |
| `match` expression + guards + binding | Done |
| **Enums** (`enum Direction { N S E W }`) | Done **v2.2** |
| Arrays (dynamic, max 1024 elements, O(1) amortized push) | Done |
| Strings (dynamic heap-allocated, unlimited size) | Done **v2.4** |
| Structs + `impl` methods + `self` | Done |
| Method chaining | Done |
| Closures / first-class functions | Done |
| `try` / `catch` / `throw` | Done |
| String interpolation `"Hello {name}"` | Done |
| Backtick strings (raw, multiline) | Done |
| Raw strings `r"..."` | Done |
| `y.map` / `y.filter` / `y.reduce` / `y.each` | Done |
| `y.sort` / `y.zip` / `y.flatten` / `y.sum` / `y.range` | Done |
| `y.math.*`: sqrt, pow, sin, cos, pi, ... | Done |
| `y.string.*`: upper, lower, split, join, trim, ... | Done |
| `y.input` / `y.input_int` / `y.input_float` | Done |
| Type system: `y.typeof`, `y.is_*`, conversions | Done |
| Capability system `@cap`, `@intent`, `@audit` | Done |
| Module / import system + relative paths + caching | Done **v1.6** |
| Error objects `y.error(msg, code)` | Done |
| Better errors: `file:line:col` + typo suggestion | Done **v1.4** |
| REPL with colored banner | Done |
| **Float arithmetic** (SSE2 native) | Done **v1.1** |
| **Arrays in native compiler** | Done **v1.1** |
| **File I/O** (`y.fs.*`, 10 functions) | Done **v1.2** |
| **Process & System** (`process.*`, `sys.*`) | Done **v1.3** |
| **JSON** (`y.json.parse`, `y.json.stringify`) | Done **v1.7** |
| **Time** (`y.time.now`, `sleep`, `format`) | Done **v1.7** |
| **Path** (`y.path.join`, `basename`, `ext`, ...) | Done **v1.7** |
| **Env** (`y.env.get`, `y.env.set`) | Done **v1.7** |
| **Native x86-64 compiler** | Done **v1.0** |
| Native → Linux ELF64 | Done |
| Native → Windows PE32+ (with icon) | Done |
| Native → macOS Mach-O | Done |
| Native → Exploidus | Pending v1.8 |
| **Garbage Collector** (mark-and-sweep, `gc.collect`, `gc.stats`) | Done **v1.5** |
| **Built-in test runner** (`ys test`, `test` blocks, `assert*`) | Done **v2.1** |
| **Static checker** (`ys check`, undefined vars, type hints) | Done **v2.1** |
| **Code formatter** (`ys fmt`, prints formatted source) | Done **v2.1** |
| **Bytecode VM** (`ys vm`, full language coverage) | Done **v2.6** |
| Self-hosting (Yolish compiles Yolish) | Pending |

---

## Platforms

| Platform | Interpreter | Native Compiler Output |
|----------|-------------|------------------------|
| Linux       | Done | ELF64 static binary     |
| macOS       | Done | Mach-O 64-bit           |
| Windows     | Done | PE32+ with icon         |
| Exploidus OS| Done | coming v1.8             |

---

## Standard Library Overview

| Module | Functions |
|--------|-----------|
| **I/O** | `y.print`, `y.println`, `y.input`, `y.input_int`, `y.input_float` |
| **String** | `y.len`, `y.upper`, `y.lower`, `y.trim`, `y.split`, `y.join`, `y.contains`, `y.replace`, `y.substr`, `y.reverse`, `y.repeat`, `y.starts_with`, `y.ends_with`, `y.index_of` |
| **Array** | `y.push`, `y.pop`, `y.slice`, `y.len`, `y.reverse`, `y.sort`, `y.map`, `y.filter`, `y.reduce`, `y.each`, `y.zip`, `y.flatten`, `y.sum`, `y.range` |
| **Math** | `y.math.sqrt`, `y.math.pow`, `y.math.abs`, `y.math.floor`, `y.math.ceil`, `y.math.round`, `y.math.min`, `y.math.max`, `y.math.pi`, `y.math.log`, `y.math.sin`, `y.math.cos`, `y.math.tan` |
| **File I/O** | `y.fs.read`, `y.fs.write`, `y.fs.append`, `y.fs.exists`, `y.fs.list`, `y.fs.mkdir`, `y.fs.delete`, `y.fs.rename`, `y.fs.size`, `y.fs.is_dir` |
| **JSON** | `y.json.parse(str)`, `y.json.stringify(val)` |
| **Time** | `y.time.now()`, `y.time.unix()`, `y.time.sleep(ms)`, `y.time.format(ms, fmt)` |
| **Path** | `y.path.join(...)`, `y.path.basename(p)`, `y.path.dirname(p)`, `y.path.ext(p)`, `y.path.stem(p)`, `y.path.abs(p)` |
| **Env** | `y.env.get(key)`, `y.env.set(key, val)`, `y.env.unset(key)` |
| **Process** | `process.spawn(cmd)`, `process.spawn_code(cmd)`, `process.env(key)`, `process.pid()` |
| **System** | `sys.exit(code)`, `sys.platform()` |
| **Type** | `y.typeof`, `y.is_int`, `y.is_str`, `y.is_float`, `y.is_bool`, `y.is_array`, `y.is_nil`, `y.int`, `y.str`, `y.float`, `y.bool` |
| **Error** | `y.error(msg, code)` |
| **Capability** | `y.capabilities()`, `y.has_cap(caps, name)` |
| **GC** | `gc.collect()`, `gc.stats()` |
| **Test** | `assert(expr)`, `assert_eq(a,b)`, `assert_neq(a,b)`, `assert_true(v)`, `assert_false(v)`, `assert_nil(v)` |

---

## Roadmap

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
| **v1.0** | **Native x86-64 compiler, Linux, Windows, macOS** |
| **v1.1** | **Float (SSE2) + arrays in native compiler** |
| **v1.2** | **File I/O, `y.fs.*` (10 functions)** |
| **v1.3** | **Process and system, `process.*`, `sys.*`** |
| **v1.4** | **Error messages, `file:line:col` + typo suggestions** |
| **v1.6** | **Module system, relative imports, circular detection, caching** |
| **v1.7** | **Stdlib expansion, `y.json`, `y.time`, `y.env`, `y.path`** |
| **v2.2** | **Enums, `enum Direction { N S E W }` + match integration** |
| **v2.0-v2.6** | **Bytecode VM (`ys vm`) introduced in v2.0, reached full language coverage in v2.6: closures, try/catch/throw, enums, both forms of import, impl blocks, array index assignment** |
| **v2.9** | **TCP networking (`y.net.*`, interpreter + VM), bitwise operators (`& \| ^ << >> ~`), hashmap (`y.map.*`), binary-safe `y.fs.*`, native-compile safety net (refuses to write a broken executable on unresolved symbols), Windows double-click console pause, two stack-overflow fixes in the string library** |
| **v2.10** | **Native TCP networking on Linux — `ys -c file.y --target linux` can now compile `y.net.connect/send/recv_print/close` down to raw syscalls, no libc. Narrower API than the interpreter/VM version (IPv4 literals only, no hostnames — see DOCS.md). Also fixed the ELF writer marking its whole data segment read-only, which broke any runtime write into that memory** |
| **v2.11** | **Connect timeout for `y.net.connect` (10s default — previously a bare blocking connect() could hang indefinitely against an unreachable address). Server-side sockets (`y.net.listen/accept`), interpreter + VM, tested with a real two-process client/server exchange** |
| **v2.12** | **Native listen/accept on Linux — `y.net.listen/accept` now compiles to raw syscalls too, tested with real native-compiled client/server pairs (and cross-compatibility with the interpreter's sockets)** |
| **v2.13** | **`process.fork()`/`process.wait()` for real concurrent servers (fork-per-connection, tested with two simultaneous clients). Fixed `y.net.send` silently truncating large payloads on a short write — verified with a 5MB send** |
| **v2.14** | **Real TLS/HTTPS via OpenSSL (`y.net.tls_*`) — opt-in build (`make tls`), interpreter + VM. Certificate verification actually tested: self-signed certs rejected, valid certs succeed with real HTTPS data** |
| **v2.15** | **HTTP client (`y.http.get/post`) — status/body/headers as a `y.map`, chunked Transfer-Encoding decoding, works over both plain HTTP and HTTPS** |
| **v2.16** | **`y.http.*` follows redirects automatically (up to 10 hops) with correct 301/302/303/307/308 method-downgrade semantics — verified against local test servers, not just assumed from the spec** |
| **v2.17** | **Build fix: Windows target was never actually linking `ws2_32`, breaking Windows builds since v2.9's networking landed. Fixed in the Makefile, verified with a real MinGW cross-compile run through Wine (builds, runs, networking works)** |

### Upcoming

| Version | Plan |
|---------|------|
| v1.8 | Native to Exploidus OS target |
| v3.0 | Deep Exploidus OS integration; official shell language |

See [ROADMAP.md](ROADMAP.md) for full details.

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