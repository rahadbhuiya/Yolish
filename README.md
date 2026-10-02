<p align="center">
  <img src="icons/logo.svg" width="120" height="120" alt="Yolish Logo"/>
</p>

<h1 align="center">Yolish</h1>

<p align="center">
  <strong>The official programming language of Exploidus OS.</strong><br/>
  Fast, expressive, capability-aware, with a native x86-64 compiler.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-v2.46-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-7b2fff?style=flat-square"/>
  <img src="https://img.shields.io/badge/compiler-x86--64%20native-00e5ff?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-MIT-gray?style=flat-square"/>
</p>

---

| | |
|--|--|
| **Author** | .Bhuiya |
| **Version** | v2.46 |
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
| **v2.37** | **Second database engine: `y.db.pg_connect(host, port, user, password, dbname)` / `pg_exec(handle, sql)` / `pg_query(handle, sql)` / `pg_close(handle)` — a PostgreSQL client. Unlike SQLite (an embedded library, `libsqlite3`) or TLS (`libssl`), this needed no external library at all: the wire protocol (v3, the one every PostgreSQL server since 7.4 speaks) is implemented directly in `net_runtime.c` over the same raw socket `y.net.connect` already uses, including a self-contained MD5 implementation (RFC 1321, written from scratch — there's no guarantee any crypto library is linked into a given build, since OpenSSL is only pulled in under `YS_WITH_TLS`, and pulling it in just for one hash felt like the wrong trade). Handles the StartupMessage/auth/`ReadyForQuery` handshake, the Simple Query protocol (`Query` → `RowDescription`/`DataRow`*/`CommandComplete`/`ReadyForQuery`), and clean connection teardown (`Terminate`). Auth: trust and MD5 only — deliberately not SCRAM-SHA-256, the default on any PostgreSQL 14+ install that hasn't been reconfigured (`password_encryption` back to `md5`, and the user's password re-set *after* that change — an existing SCRAM-stored credential doesn't retroactively become MD5-compatible just because `pg_hba.conf` says `md5`, learned by hitting exactly that during testing). SCRAM is a materially bigger protocol — a real SASL exchange, HMAC, PBKDF2, optional channel binding — and building it just to get this working felt like the wrong scope for a first pass; a real, documented limitation, not an oversight. `pg_query`'s results reuse the *exact* row-callback the SQLite work built in v2.36 (`ys_db_row_cb`, promoted out of the SQLite-specific gate it was declared under into a shared, engine-agnostic typedef) — same array-of-maps shape, same all-values-as-strings-by-design limitation (Postgres's simple query protocol returns text-format results; binary format isn't requested here), same `y.map.get(row, "col")` access pattern. No native version, same reasoning as v2.36's `sqlite_query`. Verified against a real, separately-installed local PostgreSQL 16 server (not mocked, not assumed): table creation, insert, and an `ORDER BY` read-back matching three real rows in the right order; an empty result set; an explicit `NULL` column coming back as Yolish `nil`; and a wrong-password connection attempt failing cleanly with `-1` rather than hanging or crashing. Full example suite swept clean on both interpreter and VM afterward** |
| **v2.38** | **Closes the native-side gap both v2.36 and v2.37 left open: `y.db.sqlite_query_print(handle, sql)` runs a `SELECT` and prints every row (`col=value`, one column per line, `NULL` for a null column) from a `--target linux` compiled binary — no interpreter, no VM, no libsqlite3-linked `ys` process at runtime, just the compiled binary and `libsqlite3.so.0`. Getting there needed genuinely new compiler infrastructure, not just more of what v2.35's native `sqlite_open`/`sqlite_exec`/`sqlite_close` already had: `sqlite3_exec` calls back into caller-supplied code once per row, and no native builtin before this one had ever needed to be a *callee* of external library code — every native call site so far had only ever been a *caller*. Two new pieces make that possible: a new relocation kind, `RELOC_CODEADDR` (`compiler.c`, threaded through both `elf_write` and `elf_write_dynamic` in `elf_out.c`), which lets native code take the address of a label inside its *own* `.text` section and hand it to external code as real data — a genuinely different reference than `RELOC_DATA` (points into the data section) or an ordinary `call` (jumps to a symbol, never needs its address as a value); and the callback function itself, which — despite being hand-assembled — turns out to need none of `sqlite_open`/`exec`/`close`'s self-aligning-stack workaround from v2.35, because `sqlite3_exec` reaches it through a completely ordinary `call` instruction, giving it correctly 16-byte-aligned `rsp` on entry like any C function would; that workaround was specifically about *this program's own entry point* being misaligned, not about being called into from outside, and this callback is the first piece of native code in the whole project to be called from outside rather than starting from that entry point. A small local `strlen`-and-print helper (there's no NUL-terminated-C-string print helper anywhere in the runtime — `__ys_print_str` takes an explicit length, so this walks each `char*` byte-by-byte itself) is emitted once per `sqlite_query_print` call site and reached via a direct, immediately-resolved backward `call` rather than the existing named-symbol `call_patches` machinery, keeping the whole feature self-contained inside `compiler_net.c`'s dispatch block rather than touching `emit_net_helpers`. Caught and fixed one real transcription bug before it shipped: an early version of that backward call was patched 4 bytes into the *previous* instruction instead of the placeholder itself, a wrong-target relocation that would have silently corrupted the emitted machine code rather than crashing cleanly — caught by working through `x_call_unresolved`'s actual return-value convention by hand rather than assuming it, before ever running the binary. Verified for real, repeatedly: three real rows read back correctly ordered on the very first run (no iteration needed, unlike v2.35's multi-round SIGKILL saga), an explicit `NULL` column printing as literal `NULL`, an empty result set producing no output at all rather than crashing, three independent `sqlite_query_print` call sites in a single compiled program (including one computed `COUNT(*)` column) all working without colliding, and five repeated runs with no crashes; full example suite plus a native-compile spot-check of unrelated existing examples swept clean, confirming the new relocation plumbing didn't disturb anything already working. `y.db.pg_query`'s native equivalent remains unbuilt — Postgres doesn't need this callback-callee trick at all (the wire-protocol parsing loop is entirely caller-side), but it does need the *entire* wire protocol v2.37 built in C reimplemented in hand-assembled native code from scratch, a separate and larger undertaking than extending what already existed here** |
| **v2.39** | **`y.db.pg_connect` now handles SCRAM-SHA-256 (RFC 5802 + RFC 7677) — PostgreSQL's actual default authentication method since version 14, and until now the one real gap in v2.37's client: any stock, unmodified PostgreSQL 14+ server needed its auth method manually downgraded to `md5` before this project could talk to it at all. Fully self-contained, no external crypto library, same reasoning as v2.37's from-scratch MD5: SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104), PBKDF2-HMAC-SHA256 specialized to SCRAM's fixed 32-byte output (RFC 2898), and Base64 encode/decode (RFC 4648), all new. Only plain SCRAM-SHA-256 is requested, not the -PLUS channel-binding variant — every real PostgreSQL server that advertises the -PLUS variant advertises plain SCRAM-SHA-256 right alongside it, so there was no need to actually parse the server's mechanism list rather than just proceeding with the one mechanism this implements. Runs the complete exchange: `SASLInitialResponse` → parse the server-first-message's nonce/salt/iteration-count → `PBKDF2` → derive `ClientKey`/`StoredKey`/`ClientProof` → `SASLResponse` → and — unlike a minimal implementation that would stop at "the server accepted it" — independently recomputes and verifies the server's own final signature (`ServerKey`/`AuthMessage`/expected signature, compared byte-for-byte against what the server actually sent) before considering the connection authenticated, which is what actually proves the server knows the real salted password rather than merely that it liked the proof it was handed. Every cryptographic primitive was verified against Python's own `hashlib`/`hmac` standard library on known values before being wired into the SCRAM flow at all, not just exercised end-to-end and assumed correct from there: SHA-256 and Base64 matched immediately, but the first HMAC-SHA256 check disagreed with the reference value, and the discrepancy turned out to be in the *test harness* — a hardcoded message length one byte too long (an off-by-one counting a string's length by hand) — rather than the implementation, caught by cross-checking against `len()` in Python instead of trusting a hand-count. Verified for real against the local server actually running its unmodified default (reset from the `md5` override v2.37's testing had needed, back to genuine `scram-sha-256`): full connect/insert/ordered-`SELECT`/empty-result/`NULL`-column/wrong-password flow all passing on the very first real attempt against SCRAM, MD5 re-confirmed still working afterward (the shared auth-loop dispatch was restructured to add the SCRAM branch, so this wasn't just "did the new path work" but "did doing that break the old one"), and the full example suite swept clean on both interpreter and VM. Native (`--target linux`) `pg_connect` still doesn't exist at all yet, SCRAM included — that's the much larger from-scratch wire-protocol-in-hand-assembled-machine-code undertaking noted as future, separate work back in v2.37/v2.38, deliberately not started here** |
| **v2.40** | **The native PostgreSQL client v2.38/v2.39 both deferred: `y.db.pg_connect`/`pg_exec` now exist for `--target linux`, reimplementing the wire protocol's framing and parsing directly in hand-assembled machine code rather than calling back into `net_runtime.c`'s C implementation, which native codegen has no way to reach. Deliberately narrower than the C version: `host` must be an IPv4 literal (no hostname/DNS, no IPv6 — matching the "literal-only" constraint every native builtin's string arguments already have), and only trust and MD5 auth are supported (SCRAM-SHA-256 would mean SHA-256/HMAC/PBKDF2 all reimplemented in assembly too, on top of everything here — out of scope for this pass, same reasoning v2.39 gave for why it took a separate version to add SCRAM to the C client at all). No row-reading either — this is connect+exec only, a bounded, deliverable slice rather than the complete client. Two design choices did most of the work in keeping this tractable: precomputing every message whose content is fully knowable at Yolish-compile time (the entire `StartupMessage`, the cleartext `PasswordMessage`, and — since MD5 auth needs `md5(md5(password+username)+salt)` but password and username are already required to be literals — even the *inner* `md5(password+username)` hash, leaving only the outer hash's mix-in of the server's genuinely dynamic salt to compute at the target program's own runtime) rather than building any of that at runtime piece by piece; and using a reserved, writable data-section slot as a plain "global variable" for values that need to survive across many intermediate `call`s (the socket fd, a message's declared length), rather than threading a dedicated stack frame through a long multi-branch sequence — safe here since none of this native runtime is thread-aware or reentrant to begin with, and confirmed safe to rely on since the data segment is mapped read+write (`elf_out.c`'s `PF_R|PF_W` on the second `PT_LOAD`), not read-only. Needed several genuinely new low-level building blocks no earlier native builtin had required: a `read_exact`-style helper looping on the raw `read` syscall (sockets don't guarantee one `recv` returns everything requested), a big-endian 32-bit field extractor (message lengths and PostgreSQL's own auth-type codes are wire-format big-endian; x86 is little-endian), and a runtime hex-encoding loop for the MD5 auth path's outer digest (the salt is the one piece of the whole exchange that can't be known until the server sends it). MD5 itself reuses v2.37's existing ELF dynamic-linking machinery to call `libcrypto.so.3`'s `MD5()` directly, the same approach SQLite/TLS/native-PostgreSQL's-own-inner-hash all already lean on, rather than reimplementing the hash a second time in assembly having already done it once in C. Verified against the real local server, and not merely "connect succeeded": MD5 auth actually writing a row that a separate real `psql` query then read back correctly; five repeated runs, including one where a duplicate-key `INSERT` correctly surfaced as `-1` through the exact same `ErrorResponse`-handling path a first-time success runs through, rather than crashing or hanging — meaningful because it's the first real exercise of this new code's failure path, not just its happy path; trust auth (no password) also verified with its own written-and-read-back row; a wrong password rejected cleanly; and a SCRAM-only server (the unsupported case) failing with a clean `-1` rather than hanging on a message type the auth loop doesn't recognize. Full example suite plus a native-compile spot-check of unrelated existing examples, plus a re-check of native SQLite's `sqlite_query_print`, all swept clean — confirming the new relocation/helper machinery didn't disturb anything already working. `y.db.pg_query_print` (reading rows back natively) remains the one piece of the native PostgreSQL client still unbuilt** |
| **v2.41** | **`y.db.pg_query_print` completes the native PostgreSQL client v2.40 left unfinished: `SELECT` results printed as `col=value` per line, entirely in hand-assembled machine code. Structurally simpler than native SQLite's `sqlite_query_print` (v2.38) in one respect — PostgreSQL hands rows back to whoever's already reading the socket, so there's no callback-callee trick needed, no `RELOC_CODEADDR` involved, just an extension of v2.40's own read loop to actually parse `RowDescription`/`DataRow` instead of only draining past them — but introduced a structural wrinkle SQLite's version never had: column *names* (from `RowDescription`) have to stay valid while *later* `DataRow` messages keep arriving, or a single shared scratch buffer would overwrite them mid-row. Solved with two separate persistent buffers (one only ever holds the latest `RowDescription`, one only ever holds the current `DataRow`) and two cursors walked in lockstep across them per row, re-reading `RowDescription` from its start for every row rather than parsing it once into a name/pointer table — more repeated work per row, meaningfully less code and state to get wrong. Three real, distinct bugs surfaced during testing, none of them found by inspection — every one only showed up by actually running the compiled binary against the real server and watching it fail in a specific, traceable way: (1) a value silently read back as the wrong thing after a register holding a freshly-computed string length got clobbered by an intervening call to the print helper, fixed by saving it to a stack slot across the call instead of trusting the register to survive — the same root cause `strace` had already surfaced once in v2.35's `sqlite_open`, now recurring somewhere the earlier fix's pattern hadn't been carried over; (2) the identical clobber shape a second time in the value-printing path, caught by deliberately re-auditing every other register-across-a-call site in the new code once the first instance turned up rather than assuming it was an isolated mistake; (3) the most interesting one — `ReadyForQuery`'s own 1-byte trailing payload (the transaction-status byte) was never actually read off the socket before the dispatch declared itself done, which didn't matter for a single, one-shot query, but left that byte sitting unread and silently corrupted the *next* message boundary on the same connection, surfacing as a hang (`read()` blocking on a socket that still had data, just not at the offset being asked for) the moment a second query ran over the same handle — root-caused by comparing this dispatch's control flow (branch on message type, *then* decide how much and where to read) against `pg_exec`'s from v2.40, which reads a message's full declared payload unconditionally *before* ever inspecting its type and had therefore never been exposed to the same mistake. Verified for real, deliberately including the failure-inducing case that had been the actual bug, not just the happy path: two rows with two columns each printed correctly and in order on the very first clean run after all three fixes; an empty result set producing no row output without hanging; a genuine SQL `NULL` value (added specifically to re-exercise the bug this version's own testing had just found) printing as literal `NULL`; five repeated runs stable; three independent `pg_query_print` call sites in one compiled program — including a `COUNT(*)` computed column — all correct without colliding; and the full existing example suite, a native-compile spot-check, and both of v2.40's own `pg_connect`/`pg_exec` re-confirmed working, swept clean afterward. The native PostgreSQL client (`pg_connect`/`pg_exec`/`pg_query_print`) is now feature-complete for the scope it was given: IPv4-literal hosts, trust/MD5 auth, `--target linux` only** |
| **v2.42** | **First native struct support in the compiler (`ys -c`), and — real headline of this version — two previously-unknown, pre-existing bugs found and fixed along the way, neither one actually about structs. Struct declarations, struct literals, and field read/write (`p.x`, `p.x = v`) now work for struct-typed **local variables** in native-compiled binaries: a struct-typed local is a stack-allocated block of one 8-byte slot per field (fixed offsets computed entirely at compile time, since a native binary has no runtime type info to fall back on), with field access resolved against a small compile-time struct-layout table built from a dedicated pass over top-level `struct` declarations that runs before any function body compiles. This closes the specific gap `sqlite_query_print` (v2.38), `pg_query_print` (v2.41), and `udp_recv_print` (v2.25) had all separately worked around by degrading to "print instead of return" — there was nowhere native code could hand back a real value — though none of those are switched over yet; that's future work, not part of this version. Struct function parameters and return values are **not implemented** — still the harder problem of matching the real System V struct-passing ABI (small structs in registers, larger ones by hidden pointer) rather than this version's simpler all-stack local model — and neither is `arr[i].field`, which turned out to need native array indexing to exist first, and it doesn't: `compiler.c` has no `ND_INDEX` case at all, a separate, bigger, pre-existing gap this version didn't attempt to close. The two real bugs, both found only because testing struct locals happened to be the first thing in this codebase's history to exercise these particular paths: **(1)** any native-compiled function with one or more parameters segfaulted the *compiler itself*, unconditionally — `compiler.c`'s parameter-binding code read each name from `n->args[pi]->name`, but parser.c's own comment on `fn` parsing says plainly parameters are parsed into `field_names[]`, the exact field `eval.c`'s own call-binding already reads them from; `n->args` is never populated for a function *declaration* at all, only for call sites, so this was a NULL-pointer dereference waiting for literally any native `fn` with a parameter, confirmed to predate this version entirely by reproducing it against the unmodified v2.41 compiler. **(2)** a second `struct` declaration in the same file silently corrupted the first one's field list — parser.c's struct-parsing code used a function-local `static Node *fields[16]` array, shared by *every* struct declaration in the file rather than fresh per struct; since the whole file parses before anything evaluates or compiles it, every earlier struct's `n->stmts` pointer had long since been overwritten to point at whichever struct was parsed *last* by the time either backend read it — dormant everywhere else in the codebase until now because `eval.c`'s struct-literal read path resolves field names from the *literal's own* per-node array, never from the declaration, so nothing had ever actually needed the corrupted pointer before native codegen needed the declaration's field order directly. Fixed with `alloc_stmts()`, the same heap-pool allocator the file already uses a few lines above for enum variants. Verified for real, not just compiled: multiple struct-typed locals in one function, loop-scoped struct locals re-entered across iterations, float-typed fields, whole-struct reassignment to a fresh literal, and a struct used inside a function that also takes a parameter — all cross-checked field-for-field against the interpreter's output on the same source; and, for the parameter-binding fix specifically, `examples/functions.y`'s recursion and multi-argument calls (`factorial(10)` = 3628800) compiled and run natively with output matching the interpreter exactly, aside from one separate, unrelated, pre-existing cosmetic gap this version left alone: native `y.println` has no bool-aware formatter, so `true`/`false` print as `1`/`0` in native binaries. Full existing example suite (55 of 56 passing — the one failure needs interactive stdin and is unrelated) and a native-compile spot-check of unrelated networking examples swept clean afterward, confirming neither fix disturbed anything already working** |
| **v2.43** | **Struct function *return values* now work natively, closing the other half of what v2.42 left open (struct locals only). `fn make_point(a, b) { let p = Point{x: a, y: b}; return p }` then `let q = make_point(3, 4)` now compiles and runs correctly, field-for-field matching the interpreter — verified with a 2-field struct, a 3-field struct, and repeated reassignment of an existing struct local to a fresh call result. Uses a Yolish-only "hidden pointer" calling convention rather than real System V struct-return register classification (RAX:RDX for small structs, hidden pointer for large ones): the caller passes the destination's stack address in `rdi`, the callee writes every field directly into it and shifts its own real parameters to start at `rsi` instead. A deliberate simplification, not a shortcut — nothing outside a native Yolish binary ever needs to interoperate with one of these values directly (struct-crossing-the-FFI-boundary remains separately out of scope, same as v2.42 left it), so there's no correctness reason to reproduce the real ABI's small/large-struct split, only extra complexity for no benefit here. Costs a struct-returning function one parameter slot versus normal — capped at 3 real parameters (rsi/rdx/rcx) rather than 4, since `rdi` is spent on the hidden pointer — documented rather than left as a silent surprise. One real bug, caught by testing rather than inspection: the first version of the field-copy loop wrote field `i` at `[hidden_ptr + i*8]`, but fields actually lay out *downward* from field 0 (per v2.42's own storage model, field `i` sits at `base - i*8`), so field `i`'s true address relative to the hidden pointer is `hidden_ptr - i*8` — the `+` version produced exactly the failure you'd expect from that sign error: field 0 came back correct every time, every other field came back as 0, silently overwritten by the next field's write landing one slot short and the last field's write landing one slot past the struct's actual end. Fixed by flipping the sign; the 2-field and 3-field return tests both matched the interpreter on the very next run. Struct function *parameters* — passing a whole struct value *into* a function, the mirror image of this version's return-value work — were not attempted; neither was native array indexing (`arr[i]`), still needed before `arr[i].field` becomes possible. Full existing example suite (55/56, the one expected failure unrelated) and both v2.42 struct-locals test files re-verified clean afterward** |
| **v2.44** | **Struct function *parameters* now work natively — the other direction from v2.43's return values, and (native array indexing aside) the last piece of the native struct feature set. `fn print_point(p) { y.println(p.x) }` and `fn move_point(p) { p.x = p.x + 100 }` both work, the second with the write visible back in the caller — passed by pointer under the hood (the caller passes its struct local's address, the callee reads/writes through it) rather than by copying words into the callee's frame, the same "Yolish-internal convention, not real System V classification" choice v2.43 made for returns, just applied to the other direction. Discovering which parameters are struct-typed is a genuinely two-part problem, since Yolish has no parameter type annotations at all: a parameter's struct-ness can only be learned from some *call site* passing one, so this version scans every function's body for calls rather than just each function's own signature — and that alone still isn't enough, because a *forwarding* function (`fn forward(p) { print_forwarded(p) }`) has no `let`-tracked local of its own to reveal that `p` is a struct; that fact only exists once some caller of `forward` has already been scanned. The first version of this (a single, unseeded pass) failed exactly that two-hop case — `print_forwarded`'s fields came back `0`/`0` instead of the real values, caught by testing the forwarding case specifically rather than assuming single-hop coverage would generalize. Fixed by seeding each function's own local map, at the start of its scan, with whichever of its parameters `fn_param_struct_type` already has an answer for, and running the whole scan to a fixpoint (bounded at 8 rounds) instead of once, so a forwarding chain of any depth resolves regardless of file order. Verified: plain field read through a parameter, field write-through with the mutation visible to the caller, a function taking a struct parameter *and* returning a different struct built from it, and the two-hop forwarding chain — all matching the interpreter. Known narrow limits, not attempted: a struct literal passed directly as a call argument (only forwarding an *existing* struct local is recognized), and float-ness isn't tracked through a parameter's fields (a struct parameter's `field_is_float` is never populated, since that only ever happens via a literal write, which a pointed-to caller's struct never goes through from inside the callee). Native array indexing (`arr[i]`) remains the one piece of the original v2.42 struct-support scope still untouched — `compiler.c` still has no `ND_INDEX` case, so `arr[i].field` stays impossible. Full existing example suite (55/56, one pre-existing unrelated failure) and every prior v2.42/v2.43 struct test file re-verified clean alongside this version's own new tests** |
| **v2.45** | **Native array indexing (`arr[i]`, both read and write) now works — the last piece of the struct/array feature line that started with v2.42. `let a = [10, 20, 30, 40]`, reading `a[i]` with `i` an arbitrary runtime expression, writing `a[1] = 999`, and looping over an array all compile and run correctly natively, matching the interpreter. Fixed-size and stack-allocated only, decided at compile time from a literal's own element count — no `y.push`/growth, which would need real heap allocation this backend doesn't have (a separate undertaking, left open on purpose, same category as the struct-FFI-boundary and real-ABI-matching gaps v2.42–v2.44 already left open). Element layout deliberately reuses the exact downward-from-element-0 direction (element `i` = element 0 − `i`*8) v2.42 already established for struct fields, rather than reinventing it — this backend had already paid for the opposite sign-error lesson twice on the struct side (v2.43's return-value copy loop, then v2.44's parameter-pointer offset), so this one shipped with the direction settled from the start instead of risking a third repeat. A real bug this version's testing caught, in code v2.42 shipped rather than anything new here: reassigning an array-typed local to a literal with a *different* element count (`var a = [1, 2, 3]` then `a = [100, 200]`) silently left `a[0]`/`a[1]` still reading the original 3-element block's values — `local_alloc_array` (and `local_alloc_struct`, which turned out to have the identical shape of bug for a struct local reassigned to a *different* struct type) only reused the existing same-named `Local*` when sizes/types matched, and allocated a brand-new, permanently-unreachable entry on a mismatch instead of reusing the slot, since `local_find`'s linear scan always resolves a name to its *first* match. Fixed in both functions by reusing the existing `Local*` on a mismatch; caught via array reassignment specifically (where size mismatches happen far more naturally than a struct changing type) but confirmed identical on the struct side and fixed there in the same pass rather than left for later. Known narrow limits, not attempted: `arr[i][j]` and mixing array/struct access (`obj.field[i]`, `arr[i].field`) aren't recognized, and there's no bounds checking — an out-of-range index reads or writes whatever stack memory sits at the computed offset rather than erroring, the same "does the arithmetic, doesn't validate it" posture this file already takes elsewhere. Full existing example suite (55/56, one pre-existing unrelated failure) and every prior v2.42–v2.44 struct test file re-verified clean alongside this version's new array tests** |

| **v2.46** | **`arr[i].field` now works — closing the first of v2.45's two known limitations. `let arr = [p, q]`, where `p`/`q` are existing struct locals, makes `arr` an array of pointers to those structs (reusing v2.44's struct-parameter pointer-passing convention rather than inventing a new one), so `arr[0].x`, `arr[1].y`, and `arr[i].x` with a runtime index all read correctly, and `arr[0].x = 999` writes through — verified to be visible on `p` itself afterward too, confirming `arr[0]` and `p` share the same struct rather than a copy, matching the interpreter's own shared-reference semantics exactly rather than just assuming it. Which struct type an array's elements point to is decided from the first element only — a mixed-type array, or an inline struct literal as an element (`[Point{x:1,y:2}]`, as opposed to an existing struct local), aren't recognized, the same "forward an existing local" restriction this feature line has used since v2.44. `arr[i][j]` (nested indexing), v2.45's other known limitation, remains open. Full existing example suite (55/56, one pre-existing unrelated failure) and every prior v2.42–v2.45 struct/array test file re-verified clean alongside this version's new tests** |

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