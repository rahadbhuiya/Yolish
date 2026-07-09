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

---

## Upcoming

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
- Pending: String interpolation and annotation support in the VM
- Pending: Yolish compiles itself
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