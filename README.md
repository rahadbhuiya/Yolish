<p align="center">
  <img src="icons/logo.svg" width="120" height="120" alt="Yolish Logo"/>
</p>

<h1 align="center">Yolish</h1>

<p align="center">
  <strong>The official programming language of Exploidus OS.</strong><br/>
  Fast, expressive, capability-aware, with a native x86-64 compiler.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-v2.36-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-7b2fff?style=flat-square"/>
  <img src="https://img.shields.io/badge/compiler-x86--64%20native-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-MIT-gray?style=flat-square"/>
</p>

---

| | |
|--|--|
| **Author** | .Bhuiya |
| **Version** | v2.36 |
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
-- main() is called automatically if defined — no need to call it explicitly
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
| **v2.18** | **`y.net.set_timeout(sock, ms)` — `accept()`/`recv()` can time out instead of blocking forever, verified against both an idle listener and a silent connected peer** |
| **v2.19** | **Refactor: split networking/TLS/HTTP/hashmap engine out of eval.c into net_runtime.c (no behavior change, re-tested everything). Native `y.net.listen` now sets `SO_REUSEADDR` too** |
| **v2.20** | **UDP sockets (`y.net.udp_*`) — `udp_recv` returns the sender's address alongside the data, tested with a real client/server exchange and a socket timeout** |
| **v2.21** | **Build fix: CI and the release workflow use hardcoded source lists that never got updated when v2.19 split out net_runtime.c — every CI run and release build has been failing since. Fixed in ci.yml, release.yml, and four stale BUILD.md snippets** |
| **v2.22** | **Native DNS hostname resolution — `y.net.connect("example.com", port)` now works in native-compiled (`ys -c --target linux`) binaries, not just dotted-decimal IPs. Hand-written UDP DNS client (no libc): reads `/etc/resolv.conf`, falls back to 8.8.8.8, resolves the A record, connects. Verified against real public DNS and a deliberately unresolvable hostname** |
| **v2.23** | **DNS resolver follow-up: CNAME chains (github.com/microsoft.com-style, no special handling needed — non-A records are just skipped) and real multi-A-record fallback (reddit.com's 4 records; every one tried in turn, not just the first), each bounded to a 3s non-blocking connect+poll timeout instead of a plain blocking connect. Found and fixed two real bugs via gdb/strace along the way: resolv.conf was silently never actually used due to a register clobber in the octet parser, and the retry loop was polling on the wrong value entirely (connect()'s return code instead of the fd)** |
| **v2.24** | **IPv6 support for `y.net.connect`: IPv6 literals ("::1", "2001:db8::1", parsed by this compiler's own portable parser at compile time) connect directly through a new `__ys_net_connect6`/`sockaddr_in6` path, and hostname resolution now falls back to an AAAA lookup (`__ys_net_connect_host6`) when no A record is found or connectable, reusing the same CNAME-skip and multi-record-with-timeout logic as the A path. IPv4 stays preferred by default (an A success skips AAAA entirely). The AAAA query/response side (building the query, sending/receiving over UDP, walking the answer section, extracting the 16-byte address) is fully verified against a local fake DNS server; the final `connect()` itself could only be confirmed via `strace` showing the correctly-parsed address and the right `socket(AF_INET6, ...)` call, since this development environment has IPv6 disabled at the kernel level — real end-to-end IPv6 connectivity needs verification on a host that actually has it** |
| **v2.25** | **Native UDP sockets — `y.net.udp_socket/udp_bind/udp_send/udp_recv_print/udp_recv_reply_print/udp_close` now compile under `ys -c --target linux` (previously interpreter/VM only). `udp_recv` normally returns `{data, host, port}` as a `y.map`, which the native backend has no type for, so it splits into two primitives instead: `udp_recv_print` (reads and prints the payload, sender discarded, same reasoning as TCP's `recv_print`) and `udp_recv_reply_print` (reads, prints, and sends a reply back to the captured sender — entirely inside the runtime function's own stack memory, never exposed to the Yolish program as a value), covering the most common reason a program needs the sender's address at all: replying to it. `udp_send`'s host argument is IPv4-literal only for this batch, not a hostname. Verified with a real two-process client/server exchange (client sends, server receives + replies to the captured sender, client receives the reply) and a refused-port send correctly succeeding at the UDP layer (fire-and-forget semantics — the datagram sends fine even with nobody listening)** |
| **v2.26** | **Build fix: v2.24's IPv6-literal parser used the host libc's `inet_pton` (`<arpa/inet.h>`), which doesn't exist under the mingw cross-compiler — broke `make windows` (and the Windows release build) immediately, since compiler.c is the ys compiler's own source and gets built for every target platform regardless of which platform the emitted program targets. Replaced with a small fully portable IPv6 literal parser (`::` zero-compression, embedded trailing IPv4, no platform headers at all) so the same code builds identically on Linux, mingw, and macOS. Re-verified against the same literal formats as v2.24 (compressed, full, IPv4-mapped, invalid-rejected) plus a clean `make windows`** |
| **v2.27** | **ELF dynamic-linking milestone: native-compiled Linux binaries can now import and call real functions from a shared library (starting with libc.so.6's `puts`/`exit`, proven via a new `y.net.dynlink_test()` builtin), a first deliberate exception to this backend's fully-static/no-libc design — aimed at eventually linking against a real TLS library rather than hand-rolling cryptography in raw machine code, which would be a serious, unreviewable security risk. New `elf_write_dynamic` in elf_out.c emits `PT_INTERP`/`PT_DYNAMIC` plus a minimal `.dynsym`/`.dynstr`/SysV `.hash`/`.rela.dyn`, resolving each import eagerly via a plain `R_X86_64_GLOB_DAT` relocation into an 8-byte GOT slot (`dynlink_import` in compiler.c) rather than a lazy PLT trampoline — so calling an imported function is just `mov reg,[got_slot]; call reg`, no PLT stub needed. This whole mechanism was validated against a hand-built standalone prototype before being ported into the real compiler, which caught three real bugs along the way: `PT_PHDR`'s `p_vaddr`/`p_offset` must describe where the phdr table itself lives (not the ELF header start) or ld.so silently miscomputes the load bias and corrupts every address it derives from `.dynamic`; the phdr table itself has to be covered by a `PT_LOAD` segment (not just described by `PT_INTERP`) or the kernel can't hand ld.so a valid `AT_PHDR`; and calling `exit()` via a raw syscall instead of importing libc's own `exit()` skips glibc's stdio-flush machinery, so `puts()`'s buffered output silently never appears even though nothing crashes. Regular (non-dynlink) native compilation is completely unaffected — confirmed both by the existing regression suite (all `examples/dns_examples/*.y`, the UDP client/server exchange) passing unchanged, and by checking that a program with no `dynlink_test()` call still produces a fully static binary, not a dynamic one. What this is *not*: a general FFI. Marshaling arbitrary argument/return types for arbitrary imported functions is a separate, much bigger design problem than what this establishes, which is that the underlying ELF machinery works** |
| **v2.28** | **Seven bugs from an external review, all fixed and verified against both the interpreter and the VM where each applies: (1) chained postfix access (`arr[i].field`, `obj.field[i]`, `arr[i][j]`, any mix) silently parsed wrong instead of erroring — `arr[0].x` printed the whole struct at `arr[0]` instead of its `x` field — because index and dot access were two separate parser blocks that each returned immediately instead of a shared loop; fixed in parser.c, with matching eval.c/vm.c support for assigning through the result (`arr[i].field = v`, `obj.field[i] = v`, etc., previously silent no-ops for anything but a bare identifier target). (2) `y.is_err()` always returned false because `y.error()` builds a struct named "Error", never the separate internal `YS_ERR` representation `y.is_err()` was checking for — now recognizes both. (3) String `<`/`>`/`<=`/`>=` silently fell through to integer/float conversion (so any string comparison was comparing near-zero numbers) in both the interpreter and the VM — fixed in both with a dedicated string branch. (4) `y.slice(...)` doesn't exist — DOCS.md incorrectly showed it in two places instead of the real `y.array.slice(...)`. (5) `y.math.pi`/`sin`/`cos`/`tan`/`log` were documented but never implemented — added, `pi` as a namespaced constant read (no call involved, a separate code path from `y.math.sqrt(x)`-style calls) and the rest via the standard library. (6) `@cap(...)` — the security-critical capability-annotation system — didn't actually do anything: the parser choked on its dotted-identifier arguments (leaving tokens behind to be mis-parsed as later garbage statements), `y.capabilities()`/`y.has_cap()` didn't exist, and nothing ever checked a declared capability against anything before running the function regardless. Now genuinely enforced (deny by default) with a new `y.grant(name)` for granting capabilities in the first place, since the documented model — the kernel grants them on Exploidus OS — has no equivalent yet on an ordinary system. (7) `main()` ran twice: it's auto-called if defined, and README's own Quick Start example also showed calling it explicitly — removed the redundant explicit call from the example rather than the auto-call feature itself** |
| **v2.29** | **TLS milestone: a native-compiled Linux binary completed a real TLS 1.3 handshake and an encrypted HTTPS GET/response round trip against a real server, entirely through hand-assembled machine code calling into the system's actual OpenSSL (`libssl.so.3`), via the ELF dynamic-linking machinery from v2.27. Two things had to work first: `elf_write_dynamic` gained support for multiple `DT_NEEDED` libraries (needed no change to the actual per-import relocation mechanism — ld.so's symbol search already spans every loaded library regardless of which one's `DT_NEEDED` entry brought it in, so this was purely a `.dynamic`/`.dynstr` change), and a check that this backend's simple *unversioned* `R_X86_64_GLOB_DAT` symbol resolution still works against OpenSSL 3.0's *versioned* exports (`OPENSSL_init_ssl@@OPENSSL_3.0.0` and friends) — confirmed via a narrow `y.net.tls_test()` proof of concept before building further. `y.net.tls_handshake_test()` then chains `TLS_client_method` → `SSL_CTX_new` → `SSL_new` → `SSL_set_fd` (reusing the exact same DNS-query-building and `__ys_net_connect_host` call the real `y.net.connect()` uses for the underlying TCP connection) → `SSL_connect`, and `y.net.tls_get_test()` goes one step further, sending a real HTTPS GET via `SSL_write` and printing whatever `SSL_read` decrypts back — verified via `strace` showing a complete, correctly-sequenced TLS 1.3 record flow (ClientHello, then the server's ServerHello/Certificate/Finished flight, then the client's own encrypted Finished) and a real, well-formed decrypted HTTP response coming back. Same scope caveats as the v2.27 milestone: hardcoded test host, no cleanup, and not yet a general public API — `y.net.tls_connect/tls_send/tls_recv_print/tls_close` mirroring the plain-TCP shape is the natural next step, not built in this pass** |
| **v2.30** | **The real TLS public API: `y.net.tls_connect(host, port)`/`tls_send(handle, data)`/`tls_recv_print(handle, maxlen)`/`tls_close(handle)`, backed by a small fixed-size round-robin connection-handle table (4 slots, `{fd, ctx, ssl}` each — still no struct/map type to hand back a bundle directly). Building it surfaced five real bugs, fixed structurally: (1) stack-alignment corruption from pushing a runtime argument onto the stack before a call into libssl's SIMD-using internals — fixed by never push/pop-ing at all in these four functions, using one fixed-size (`sub rsp,N`, N always a multiple of 16) rbp-relative frame instead; (2) a hand-transcribed `lea r11,[rip+got]; mov rax,[r11]; call rax` sequence risking a stray-byte slip at every call site — centralized into one `x_call_got()` helper; (3) `SSL_free()` clobbering RCX (caller-saved) mid-use in `tls_close` — fixed by reloading the slot address from a saved rbp-relative slot after every single call rather than trusting a register to survive one; (4) a frame-switch bug only caught by actually running it — opening each function's own nested frame *before* compiling its arguments made a variable argument (e.g. a handle) resolve against the new, not-yet-written frame instead of the caller's, silently reading uninitialized memory; fixed by evaluating arguments first and staging them through a small rip-relative scratch buffer before switching frames; (5) `tls_recv_print` printing via libc's buffered `puts()` (`tls_get_test`'s style, which only works there because it always calls libc `exit()` right after, flushing stdio) instead of the raw `SYS_write` syscall the plain `recv_print` already uses — invisible in practice, since this program's normal end-of-program path is a raw `exit` syscall that never flushes anything. Also added SNI (`SSL_set_tlsext_host_name` via `SSL_ctrl`) for the hostname branch, after a real Cloudflare-fronted host failed its handshake without it while another happened to succeed regardless — a genuine correctness gap, not a fluke. Verified against two real hosts at once with distinct, non-cross-wired responses, invalid handles failing cleanly (not crashing) on all three operations, and a clean `make windows` (all four builtins guard `g_target==TARGET_LINUX`, since the dynlink machinery underneath is ELF-only)** |
| **v2.31** | **Closes out the rest of native networking: TLS server (`y.net.tls_listen`/`tls_accept`) and a native HTTP client (`y.http.get_print`/`post_print`). `tls_listen(port, certfile, keyfile)` loads a real cert/key off disk and reuses `__ys_net_listen` underneath; `tls_accept` reuses `__ys_net_accept` plus the server-side OpenSSL handshake (`SSL_accept`), storing the result into the *same* client-connection table `tls_connect` uses — with `ctx` deliberately stored as 0 for an accepted connection, so `tls_close`'s unconditional `SSL_CTX_free` is a no-op instead of freeing the listening socket's shared context out from under future accepts. `y.http.get_print`/`post_print` parse the URL entirely at compile time (it's required to be a literal) into scheme/host/port/path, then reuse the exact same connect/TLS-handshake code paths as `y.net.connect`/`y.net.tls_connect` — no separate networking implementation. Two real bugs surfaced building this, both caught only by actually running it against real servers, not by reasoning about the code: (1) `emit_http_request`'s shared cleanup path (SSL_free/SSL_CTX_free/close, all of which clobber rax) read its return value from a stack slot that was only ever written on two of the six paths reaching it — every early-failure path landed there with a stale, uninitialized value instead of the `-1` it had just set, the same *class* of mistake as v2.30's frame-switch bug (a register/value assumed to survive something that clobbers it), just a different concrete cause; fixed by having every path stash its own correct value immediately before converging, not relying on a shared reload afterward. (2) A single `read()`/`SSL_read()` call isn't a full HTTP response: a real local test server sent headers and body as two separate writes, arriving as two separate TCP segments, and the first version of this (matching `tls_recv_print`'s existing one-shot-by-design behavior) silently printed only the headers — no error, no crash, just an incomplete response. Fixed with a proper read loop (continuing until the read returns ≤0) local to the HTTP client only; `y.net.recv_print`/`y.net.tls_recv_print` themselves are untouched, keeping their existing one-shot contract. Verified: a full self-signed-cert TLS server ↔ `tls_connect` client exchange in the same process pair; `get_print`/`post_print` against both a real Cloudflare-fronted host and local plain-HTTP/HTTPS test servers (POST body and Content-Type arriving correctly, multi-segment response now fully captured); invalid inputs (bad cert path, bad server handle, unsupported URL scheme) failing cleanly; full existing regression suite and `make windows` unaffected** |
| **v2.32** | **`y.net.udp_send`'s `host` argument now accepts a hostname literal, not just an IPv4 dotted-decimal one — the one remaining gap the v2.20 changelog entry called out when native UDP first shipped. Resolution goes through a new `__ys_net_udp_send_host` runtime routine, not a change to `y.net.connect`'s own hostname resolver (`__ys_net_connect_host`): that function is proven, real-world-tested code, and refactoring it to share logic with this narrower UDP path risked a regression in the higher-value TCP one for no real benefit — duplicating the resolver-discovery/DNS-query/response-parsing logic was the deliberate, lower-risk choice, with `__ys_net_connect_host` left completely untouched. The one genuine simplification versus that duplicated logic: TCP's resolver tries actually connecting to *each* A record in turn since some may be unreachable, but UDP's `sendto` either succeeds against a resolved IP or it doesn't — there's nothing to "try connecting" to first — so this version takes the first A record found and stops, with none of the non-blocking-connect/poll/SO_ERROR machinery that exists solely to bound a TCP connection attempt. Verified against a local stub DNS resolver (confirmed via the resolved query actually arriving and a correctly-addressed reply socket receiving the exact bytes sent) and a genuine resolution-failure case (an unreachable resolver correctly time out and return `-1` at the same ~3s `SO_RCVTIMEO` bound `y.net.connect`'s hostname path already uses, rather than hanging); the existing IPv4-literal `udp_send` path re-verified unaffected** |
| **v2.33** | **Two real gaps closed in the native TLS/HTTP stack: certificate verification (`y.net.tls_connect` and `y.http.get_print`/`post_print`'s HTTPS path both trusted any certificate a server presented until now) and HTTP chunked-transfer-encoding decoding (a chunked response's hex chunk-size lines and boundary CRLFs printed as literal text before this). Verification adds `SSL_CTX_set_verify(SSL_VERIFY_PEER)`, a TLS 1.2 floor, the system's default CA trust store, and `SSL_set1_host` for hostname matching — the same combination the interpreter/VM `make tls` build already used, tested, and shipped; a self-signed or otherwise-untrusted cert now makes both native paths return `-1` instead of silently completing the handshake anyway. Chunked decoding required restructuring how `y.http.*` reads a response: instead of printing each read as it streams in, the whole response is now accumulated into the shared 4095-byte buffer first (chunk boundaries can split across reads the same way headers/body already could), then decoded and printed in one pass — real trade-off, not free: a chunked response over 4095 bytes now truncates, where a non-chunked one of any size used to stream through in full (chunked ones were already garbled past the first read before this, so nothing regresses for them specifically). Caught two more of the same *class* of mistake this whole native-networking stack keeps surfacing only at build time, never at review time: a symbol-name assumption (`SSL_CTX_set_min_proto_version` isn't actually exported by this OpenSSL build, only its `SSL_CTX_ctrl`-based implementation is — the same situation `SSL_set_tlsext_host_name`/`SSL_ctrl` already had, missed on a second, independent function this time) caught immediately by a runtime `symbol lookup error`; and, twice in a row this time, a str_replace insertion landing mid-function and deleting the tail end of the function it was supposed to follow, both caught immediately by a full-file brace-balance check (skipping comments/strings, since naive counting isn't reliable enough) rather than by testing — a cheap, mechanical check worth running before ever reaching for a test binary on an edit like this. Verified: cert verification against a real self-signed pair (rejected) and a real trusted host (accepted) on both `tls_connect` and `get_print`; chunked decoding against a local server sending "Hello"/" World" as two chunks split across two separate TCP sends (correctly reassembled, markers stripped) and a real non-chunked response (unaffected); full regression suite and `make windows` clean. Native macOS networking, raised as a possible next step, was deliberately not attempted — see the ROADMAP/DOCS entry on why shipping something nobody can run isn't the same as finishing it** |
| **v2.34** | **Two unrelated pieces of housekeeping and one real feature. First, `compiler.c` had grown to ~5650 lines, roughly half of it native networking — split into `compiler.c` + `compiler_net.c`, the latter `#include`-d directly into the former (not a separate translation unit, not in the Makefile's source list) rather than made independently compiled, since making the dozens of shared static helpers/globals it depends on non-static for no behavioral reason wasn't worth the risk. Second — the actual feature, and the one explicitly asked for despite its own caveat — a macOS/Darwin port of plain TCP/UDP native networking (`connect`/`send`/`recv_print`/`close`/`listen`/`accept`/the UDP family), guarded by `--target macos` alongside the existing Linux path. Explicitly, deliberately **unverified**: there is no macOS environment anywhere in this project's toolchain to run a single line of it against, a real departure from how every other piece of this native networking stack got built and tested. What verification *was* possible: confirming a valid Mach-O binary comes out, and checking — instruction by instruction, with a disassembler — that every Darwin-specific constant this port depends on (syscall numbers via the `0x2000000 | n` convention, BSD `sockaddr_in`/`sockaddr_in6`'s length-then-family byte layout instead of Linux's plain 2-byte family field, `AF_INET6`=30 rather than Linux's 10, `SOL_SOCKET`/`SO_REUSEADDR` as 0xffff/4 rather than 1/2) appears exactly as designed in the actual emitted bytes. That confirms the compiler emits what was intended, not that the design itself is correct against real macOS — those are genuinely different claims, and only the first one could be checked here. Hostname/DNS resolution and TLS/HTTP remain out of scope for macOS specifically (documented safety stubs return `-1` rather than silently emitting the wrong platform's syscalls if a hostname literal is used) — see the DOCS.md entry for the full reasoning on both** |
| **v2.35** | **First database support: `y.db.sqlite_open(path)` / `sqlite_exec(handle, sql)` / `sqlite_close(handle)`, backed by real `libsqlite3`, not a bundled/reimplemented engine. `sqlite_exec` is fire-and-forget only for now (SQLite's own `NULL` callback) — `CREATE`/`INSERT`/`UPDATE`/`DELETE` work, but there's no way yet to read query results back into the language; that needs a callback trampoline or `prepare`/`step`/`column`, deliberately left for later rather than half-built now. Ships on all three execution paths: the interpreter and the bytecode VM both go through a new `net_runtime.c` `ys_db_sqlite_*` layer gated by `-DYS_WITH_SQLITE` (same opt-in-library pattern `y.net.tls_*` already established — omit the flag and it degrades to a clean "not compiled in" `-1`, it doesn't fail to build), and native `--target linux` reuses the exact ELF dynamic-linking machinery TLS uses (`dynlink_need_library`/`dynlink_import`/`x_call_got`) to import `sqlite3_open`/`sqlite3_exec`/`sqlite3_close` directly from `libsqlite3.so.0`. Getting the native path right surfaced a real bug, and a real *class* of bug worth flagging on its own: `sqlite3_exec` segfaulted deep inside `libsqlite3`'s own internals (`SIGSEGV`, faulting address `NULL`) despite the call site looking correct by every check that had worked for every native builtin before it. Root cause turned out to be one level up — this whole program's ELF *entry point* leaves `rsp` already 8-mod-16 at process start, one push short of the alignment a function actually reached via a normal `call` would have. That's a pre-existing, program-wide issue, not anything introduced here; every earlier native library call (`dynlink_test`'s `puts()`, all of `y.net.tls_*`'s OpenSSL calls) happened to never trip over it only because none of those particular callees hit an alignment-sensitive SSE store (`movaps`/`movdqa`) on their own stack locals — `sqlite3_exec`'s much heavier internals do. Chose not to fix the entry point itself here: doing that blind, without re-running every existing native call site against it, risked trading one hard-to-see bug for several. Instead made these three new call sites unconditionally self-aligning — each saves the true `rsp`, forces it down to 16-byte alignment immediately before its external call, and restores the saved value afterward — so they're correct regardless of which way the underlying entry-point issue eventually gets fixed. **The entry-point alignment bug itself is still open** and could resurface for any future native library integration that happens to hit an alignment-sensitive callee; flagged for its own dedicated session rather than patched in passing. Verified for real: built `ys` locally, ran actual `CREATE TABLE`/`INSERT` through all three paths (interpreter, VM, native), cross-checked the resulting `.db` file's contents independently with Python's `sqlite3` module, reran the native binary five times back to back with no crashes, and swept the full existing example suite through both the interpreter and the VM plus a native-compile spot-check with no regressions. PostgreSQL/MySQL wire-protocol support, and reading query results back at all, remain explicitly out of scope for this version** |
| **v2.36** | **Closes the gap v2.35 explicitly left open: `y.db.sqlite_query(handle, sql)` reads `SELECT` results back as a real array of maps, one map per row (`{"column_name": value}`), instead of only being able to run fire-and-forget statements. Interpreter and VM only for now — no native version, since a native call site needs a real function-pointer callback for `sqlite3_exec` to call back *into*, and the native backend has no mechanism yet for handing a hand-assembled function a valid callable address of its own (every native builtin so far has only ever been a *caller*, never a *callee* of library code); that's new, separate machinery, not an extension of what v2.35's native path already has. Split cleanly across the same two files SQLite already lives in: `net_runtime.c` stays ignorant of `Val`/maps entirely — it just runs `sqlite3_exec` with an internal trampoline that forwards each row's raw `char**` column values/names to a caller-supplied C callback, capped at a row count the caller passes in (hitting the cap isn't treated as an error, there just aren't more slots left) — while `eval.c` supplies that callback and does the actual `Val` map construction, since the map-building helpers (`ys_map_init`/`ys_map_set`) already live there. Every value comes back as a string regardless of its real SQLite column type — `sqlite3_exec`'s legacy callback API doesn't expose real types, only `prepare`/`step`/`column` would, and pulling that in over one `sqlite3_exec` call is a real trade-off made deliberately to keep this at one function call rather than a four-function bind/step sequence; worth revisiting if callers need real `INTEGER`/`REAL` typing back rather than parsing strings themselves. Reads are accessed via `y.map.get(row, "col")` — this language's maps were never dot-accessible (`row.col` silently returns `nil`, the same "dot on anything that isn't a struct or enum" fallthrough every map access in this language already has), a mismatch caught immediately by testing rather than assumed to work from the array-of-structs shape being superficially similar. Verified for real: three real rows inserted then read back in the correct `ORDER BY`, an empty result set (`WHERE age > 1000`) correctly returning a zero-length array rather than erroring, and an explicit `NULL` column value coming back as Yolish's own `nil` rather than the string `"NULL"` or crashing; full existing example suite swept through both interpreter and VM with no regressions** |

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