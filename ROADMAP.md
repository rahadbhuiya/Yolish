# Yolish Roadmap

---

## Vision

Yolish is the scripting and automation language of Exploidus OS.

The goal is simple: a lightweight, capability-aware language that makes
it easy to write OS tools, config scripts, and system utilities, with
security built in from the start.

Yolish is not trying to replace Rust or C. It is the glue language of
Exploidus, readable, safe, and practical.

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
| **v1.0** | **Native x86-64 compiler, Linux, Windows, macOS; logo; colored REPL** |
| **v1.1** | **Float arithmetic (SSE2), array literals in native compiler** |
| **v1.2** | **File I/O, `y.fs.read/write/append/exists/list/mkdir/delete/rename/size/is_dir`** |
| **v1.3** | **Process and system, `process.spawn`, `process.env`, `sys.exit`, `sys.platform`** |
| **v1.4** | **Error messages in `file:line:col` format, typo suggestions (Levenshtein)** |
| **v1.5** | **Garbage collector, mark-and-sweep; `gc.collect()`, `gc.stats()`** |
| **v1.6** | **Module system, relative imports (`./utils.y`), circular detection, import caching** |
| **v1.7** | **Stdlib expansion, `y.json`, `y.time`, `y.env`, `y.path`** |
| **v2.0** | **Bytecode VM, `ys vm file.y`. Stack-based VM compiling the language to bytecode. Benchmarked 35 to 40 times faster than the AST interpreter on `fib(27)` (10.8s to 0.3s). Started as a subset compiler; full coverage landed over v2.0 through v2.6, see below** |
| **v2.1** | **Tooling, `ys test` (test runner), `ys fmt` (formatter), `ys check` (static analysis)** |
| **v2.2** | **Enums, `enum Status { Ok NotFound Error }` with match integration** |
| **v2.3** | **Scalability: dynamic (chunk-based) node pool, unlimited import size, 1024-element arrays, O(1) amortized push/pop, immutable array semantics fix** |
| **v2.4** | **Unlimited strings, `Val.sval`/`Node.sval` moved to the heap and GC-tracked, dynamic lexer string buffers; fixed a critical closure-corruption bug (self-referencing env parent chain) that had existed since v1.5's GC introduction** |
| **v2.5** | **Unlimited variables per scope, `Env.names`/`Env.vals` converted from a fixed 48-slot array to a dynamically-growing heap array; also fixed seven builtin short-name aliases (`y.replace`, `y.join`, `y.repeat`, `y.starts_with`, `y.ends_with`, `y.reverse`, `y.index_of`) that were documented but silently returned `nil` because only their namespaced forms were registered |
| **v2.6** | **Bytecode VM reaches full language coverage. `ys vm` now compiles for-in loops, `break`/`continue`, `match`/match guards, closures (dispatched through the tree-walking interpreter so they work with `y.map`/`y.filter`/`y.reduce`/`y.sort`/`y.each`), `try`/`catch`/`throw` (native VM frame unwinding, so a throw several calls deep is still caught correctly), `enum`, both forms of `import`, `impl` blocks and struct methods, and array index assignment. Only string interpolation and `@intent`/`@audit` annotations still fall back to the AST interpreter. Also fixed three interpreter bugs found while building this out: a loop-scoped local going stale after the first iteration, `g_returning` staying set after a caught throw and silently truncating the rest of the calling scope, and module functions being unable to reference other names from their own module** |
| **v2.9** | **Windows PE backend fixes: correct RVA (not VA) in the import table, IAT call-site patching, Microsoft x64 ABI calling convention for `print`/`exit`, full 8-byte NULL for `WriteFile`'s `lpOverlapped`, and an auto-pause on double-click-launched consoles. Native compiler safety net: `ys -c` now refuses to write an executable if any symbol failed to resolve. TCP client sockets (`y.net.connect/send/recv/close/last_error`, interpreter + VM — native `-c` compilation not yet covered, see Upcoming). Bitwise operators (`& \| ^ << >> ~`, interpreter + VM + lexer/parser). Hashmap (`y.map.new/set/get/has/delete/keys/values/len`, open-addressing with automatic growth). Binary-safe `y.fs.read/write/append` (previously `y.fs.read` silently truncated at 8191 bytes and `y.fs.write`/`append` truncated at the first embedded NUL byte). Two stack buffer overflow fixes in `y.string.repeat`/`y.string.replace` (both wrote up to 8188 bytes into 512-byte stack arrays)** |
| **v2.10** | **Native TCP networking on Linux (`ys -c file.y --target linux`): raw `socket`/`connect`/`read`/`write`/`close` syscalls, no libc — a hand-written IPv4 dotted-decimal parser (there's no DNS-resolution syscall to lean on), `y.net.connect/send/recv_print/close`. Along the way: fixed the ELF writer marking its data segment read-only, which made any runtime write into that segment (e.g. `recv`'s destination buffer) fail with EFAULT** |
| **v2.11** | **Connect timeout (10s default, non-blocking connect + poll) for the interpreter/VM networking path — a bare blocking connect() to an unreachable address could previously hang for the OS's own TCP timeout. Server-side sockets, interpreter + VM (`y.net.listen/accept`), tested with a real two-process client/server exchange over both the tree-walking interpreter and the bytecode VM** |
| **v2.12** | **Native listen/accept on Linux (`y.net.listen/accept` compiled to raw bind/listen/accept syscalls) — tested with real native-compiled client/server pairs and cross-compatibility between native and interpreted endpoints (they're both just standard TCP)** |

---

## Upcoming

### v2.12: Networking (in progress)
- Done: native listen/accept on Linux (`y.net.listen/accept`, raw
  bind/listen/accept syscalls) — tested with real native-compiled
  client/server pairs, and cross-compatibility (native server with an
  interpreter client and vice versa, both just standard TCP underneath)
- Done (v2.11): connect timeout (10s default) for the interpreter/VM
  path; server-side sockets, interpreter + VM (`y.net.listen/accept`)
- Done (v2.10): TCP client sockets, native compilation on Linux (raw
  syscalls, no libc) — narrower API: `y.net.connect/send/recv_print/
  close`, IPv4 literal addresses and literal string data only
- Done (v2.9): TCP client sockets, interpreter + bytecode VM
  (`y.net.connect/send/recv/close/last_error`) — hostnames, arbitrary
  receive length, string return values
- Pending: native compilation on Windows (Winsock2 imports) and macOS
  (BSD sockets via libSystem, needs Mach-O dynamic linking first)
- Pending: hostname resolution for native builds — needs either a
  hand-rolled DNS client (raw UDP) or dynamic linking against libc
- Pending: SO_REUSEADDR for native listen (needs setsockopt's 5-arg
  syscall ABI, which needs r8/r10 — outside the 8-base-register
  encoding this file currently sticks to)
- Pending: concurrency for the server path — right now handling a
  second client means finishing (or at least accepting past) the first
- Pending: recv/accept timeout (only connect() has one so far)
- Pending: UDP sockets
- Pending: HTTP client convenience wrapper built on top of TCP

### v1.8: Native Compiler Expansion
- Exploidus OS native binary target
- Structs in native compiler
- `y.print`/`y.println` for all types natively compiled

### v2.0: Self-Hosting (in progress)
- Done: Bytecode VM for faster interpretation, shipped as `ys vm`
- Done: Full language coverage in the VM (structs, closures with variable
  capture, match expressions, enums, try/catch, both forms of import,
  impl blocks, array index assignment). Only string interpolation and
  `@intent`/`@audit` annotations still fall back to the AST interpreter
- Done: binary-safe `y.fs.read/write/append` — a self-hosted PE/ELF/Mach-O
  writer needs to read arbitrary-size source files and write arbitrary
  binary output without null-byte truncation
- Done: bitwise operators (`& | ^ << >> ~`) — needed for emitting machine
  code bytes, packing header flags, etc.
- Done: hashmap (`y.map.*`) — needed for a real symbol table
- Pending: String interpolation and annotation support in the VM
- Pending: Yolish compiles itself — realistic next step is porting the
  **lexer** first and diffing its token stream against the C lexer's,
  then the parser, then the compiler backends, rather than attempting
  the whole toolchain at once
- Pending: Constant folding and dead code elimination

### v3.0: Exploidus Integration
- Deep Exploidus OS kernel integration
- Capability tokens validated by kernel (not just runtime)
- `@intent` annotations wired to OS scheduler directly
- Sandboxed script execution model
- Yolish as the official shell scripting language of Exploidus

---

## What Yolish will NOT do

These are intentionally out of scope. Yolish stays focused.

- No generics or traits, use structs and closures instead
- No async/await or threading, Exploidus handles concurrency at the OS level
- No macros or metaprogramming, keep the language readable
- No GUI toolkit, that belongs in a separate Exploidus UI framework
- No package registry, modules are plain `.y` files, no dependency hell
- No FFI / C interop, capabilities handle OS access; Yolish is not a systems language

---

## Development Philosophy

- Simplicity over features
- Security by default, no resource access without a capability
- Lightweight, fast startup, low memory, no VM overhead for simple scripts
- Readable, someone unfamiliar with Yolish can still read it
- Focused, does one thing well: scripting for Exploidus OS