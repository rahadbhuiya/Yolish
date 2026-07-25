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
| **v2.13** | **`process.fork()`/`process.wait()` — real concurrent servers via fork-per-connection, verified with two simultaneous clients and no cross-talk. Fixed `y.net.send` silently truncating large payloads on a short write (now loops until everything is sent); verified with a 5MB send delivered whole in one call** |
| **v2.14** | **Real TLS/HTTPS (`y.net.tls_connect/send/recv/close`) via OpenSSL — opt-in build (`make tls`), interpreter + VM, Linux/macOS. Certificate verification tested both ways: a self-signed cert is correctly rejected, a valid public HTTPS endpoint succeeds with real data received over the encrypted channel** |
| **v2.15** | **HTTP client (`y.http.get/post`) built on `y.net.*`/`y.net.tls_*` — status/body/headers parsing, chunked Transfer-Encoding decoding (tested against a deliberately 3-chunk response), tested against real HTTPS endpoints and a local server confirming full POST bodies arrive intact** |
| **v2.16** | **`y.http.*` now follows 3xx redirects automatically (up to 10 hops), with correct per-status method/body handling (303 and POST-via-301/302 downgrade to GET; 307/308 preserve method+body) — verified against local test servers for the multi-hop chain, both downgrade cases, and the loop-detection limit** |
| **v2.17** | **Build fix: the Windows target never actually linked against `ws2_32`, so any Windows build broke as soon as v2.9's networking code landed — introduced then, only caught now via a real CI failure. Fixed in the Makefile (both the MinGW cross-compile target and native-Windows `LIBS`); also fixed a `winsock2.h`/`windows.h` include-order warning in the same area. Verified with an actual MinGW cross-compile run through Wine: builds clean, runs, and its networking works** |
| **v2.18** | **`y.net.set_timeout(sock, ms)` — `accept()`/`recv()` can now time out instead of blocking forever (verified: an idle listener times out at the requested duration, a connected-but-silent peer's `recv()` does too, both reporting a distinguishable "timed out" error)** |
| **v2.19** | **Refactor: networking/TLS/HTTP/hashmap engine moved out of eval.c into net_runtime.c (eval.c: 4538 → 3705 lines), no behavior change. Native `y.net.listen` now sets `SO_REUSEADDR` (verified via strace and a real rapid-rebind test), matching the interpreter/VM version — needed extending the native compiler to use r10/r8 for the first time** |
| **v2.20** | **UDP sockets (`y.net.udp_socket/udp_bind/udp_send/udp_recv/udp_close`), interpreter + VM — `udp_recv` returns the sender's address alongside the data, verified with a real two-process client/server exchange and with `y.net.set_timeout` on a UDP socket** |
| **v2.21** | **Build fix: CI and the release workflow's Linux/macOS build steps use hardcoded source-file lists (not the Makefile), and never got updated when v2.19 split net_runtime.c out of eval.c — every CI run and release build has been failing to link since. Fixed in ci.yml, release.yml (both jobs), and four stale manual-build snippets in BUILD.md (which were also still missing several older files like formatter.c/checker.c, and missing -lws2_32 on the Windows ones) — verified by running each fixed command exactly as it now appears in its file** |
| **v2.22** | **Native DNS hostname resolution for `ys -c file.y --target linux`: `y.net.connect("example.com", port)` now works in native-compiled binaries, not just dotted-decimal IPs — a hand-written UDP DNS client (raw `socket`/`sendto`/`recvfrom`, no libc, no `getaddrinfo`) reads the first nameserver from `/etc/resolv.conf`, falling back to 8.8.8.8 if that's absent or unparsable, builds the query packet at compile time (the hostname, like the IP case, is already required to be a literal), and parses the A record out of the response. `SO_RCVTIMEO` (3s) keeps a dropped/unanswered query from hanging the program. Dotted-decimal literals are unaffected — they still take the original fast path straight to the octet parser with no DNS round trip. Linux only — macOS/Windows still have no native `y.net.*` support at all, for either address shape (see the v2.10 native-TCP entry)** |
| **v2.23** | **DNS resolver follow-up: CNAME chains and real multi-A-record fallback for `y.net.connect`. CNAME chains need no special handling — non-A records are just skipped by rdlength, which already surfaces the eventual A record in the same answer section (verified live against www.github.com and www.microsoft.com). Every A record in the response is now tried in turn, not just the first (live-verified against www.reddit.com's 4 records and, more importantly, a local fake-DNS test with a deliberately blackholed first record), each bounded to a 3s connect timeout via non-blocking connect + poll + SO_ERROR rather than a plain blocking connect — which a live test showed would otherwise hang for the OS's default TCP timeout on an unreachable record before ever trying the next one. Building this surfaced two real bugs caught via gdb/strace: the octet parser was clobbering its own end-pointer register, so resolv.conf was silently never actually used no matter its content (always fell back to 8.8.8.8 regardless of what was configured); and the retry loop was seeding poll()'s pollfd with the wrong value (connect()'s return code instead of the actual fd). Both fixed and re-verified end-to-end** |
| **v2.24** | **IPv6 support for `y.net.connect`. Two additions: (1) IPv6 literals ("::1", "2001:db8::1") are parsed at compile time via the host ys compiler's own `inet_pton` — no runtime IPv6 text parsing needed, same reasoning as why the DNS query bytes for a hostname are built at compile time — and connect directly through a new `__ys_net_connect6` using `AF_INET6`/`sockaddr_in6` (28 bytes) instead of `AF_INET`/`sockaddr_in`. (2) Hostname resolution gains an AAAA fallback: `__ys_net_connect_host` (A, IPv4) is tried first as before, and only if it returns -1 does a new `__ys_net_connect_host6` get tried with a separately-built AAAA query, reusing the exact same resolv.conf-lookup, CNAME-skip, and multi-record-with-timeout structure as the A path — the two functions are near-identical by design, since duplicating the shared DNS-transport logic was more honest here than threading a record-type/address-family flag through offsets and struct sizes that differ in size (16 vs 4 byte addresses, 28 vs 16 byte sockaddr) throughout the function. IPv4 stays preferred: an A success skips AAAA entirely. Verified as thoroughly as this development environment allows: `inet_pton` parsing checked directly against several literal formats (compressed, full, IPv4-mapped, invalid-rejected); the AAAA query-build → send → response-parse → 16-byte-extract pipeline verified end-to-end against a local fake DNS server (including the A-returns-nothing → AAAA-fallback path); and `strace` confirming both `__ys_net_connect6` and the AAAA path attempt `socket(AF_INET6, ...)` with the correctly-parsed address. The actual `connect()` call could not be exercised end-to-end here since this environment has IPv6 disabled at the kernel level (`socket(AF_INET6,...)` returns `EAFNOSUPPORT` regardless of code correctness) — needs confirming on a host with real IPv6 connectivity** |
| **v2.25** | **Native UDP sockets: `y.net.udp_socket/udp_bind/udp_send/udp_recv_print/udp_recv_reply_print/udp_close` now compile under `ys -c --target linux`, closing the gap noted in the v2.20 entry above. `udp_recv`'s interpreter/VM signature returns `{data, host, port}` as a `y.map` — no equivalent exists natively (no map type, no runtime string type), so it splits into two primitives instead of one: `udp_recv_print(sock, maxlen)` reads and prints the payload with the sender discarded (same "print instead of return" reasoning as `recv_print`), and `udp_recv_reply_print(sock, maxlen, reply_data)` reads, prints, and sends `reply_data` back to whichever address the datagram arrived from — captured in the runtime function's own stack memory and never surfaced to the Yolish program as a value, covering the single most common reason a program needs the sender's address at all (an echo/reply server) without needing a map or string return type to do it. `udp_send`'s `host` argument is an IPv4 literal only for this batch, not a hostname (parallel to how `y.net.connect` itself started IPv4-literal-only before DNS support was added later) — its octet-parsing loop is adapted from `__ys_net_connect`'s runtime version rather than the newer compile-time literal path, since `host` here arrives as a real ptr/len argument pair, not something already sitting in rodata. Verified with a real two-process client/server exchange (bind, send, receive-and-reply-to-captured-sender, receive the reply) and a send to a refused port correctly succeeding at the UDP layer (fire-and-forget — the datagram sends fine even with nobody listening, which is correct UDP semantics, not a bug)** |

---

## Upcoming

### v2.20: UDP sockets
- Done: `y.net.udp_socket()`/`udp_bind(port)`/`udp_send(sock, host,
  port, data)`/`udp_recv(sock, maxlen)`/`udp_close(sock)`, interpreter
  + VM. `udp_recv` returns the sender's address alongside the data
  (essential for UDP, unlike TCP where the peer is already known) —
  `{data, host, port}` as a `y.map`.
- Verified with a real two-process client/server exchange (client
  sends, server receives and replies to the captured sender address,
  client receives the reply) on both the interpreter and the VM, and
  confirmed `y.net.set_timeout` works identically on a UDP socket as
  it does on TCP (reuses the same `SO_RCVTIMEO` mechanism).
- Native compilation followed in v2.25 (see that entry) — narrower
  than this interpreter/VM API in the same way the native TCP path is
  narrower than its interpreter/VM counterpart: `udp_recv`'s `y.map`
  return splits into `udp_recv_print`/`udp_recv_reply_print` since
  there's no map or runtime string type natively, and `udp_send`'s
  `host` is an IPv4 literal only, not yet a hostname.

### v2.19: net_runtime.c split + native SO_REUSEADDR
- Refactor: the networking/TLS/HTTP/hashmap engine (previously ~840
  lines inline in eval.c) moved to its own file, net_runtime.c, with
  a small net_runtime.h declaring what eval.c's call_builtin()
  dispatch needs. eval.c dropped from 4538 to 3705 lines. No behavior
  change — every feature re-tested after the move (bitwise, hashmap,
  plain networking, TLS, HTTP client + redirects, process.fork
  concurrency, native compilation) to confirm the split didn't break
  anything.
- Done: native `y.net.listen` now sets `SO_REUSEADDR`, matching the
  interpreter/VM version. This needed extending the native compiler's
  hand-written machine code to use r10/r8 (setsockopt's raw-syscall
  ABI for arguments 4/5) — the first native-compiler code to use
  registers outside the base 8 the rest of the file sticks to.
  Verified via strace (exact setsockopt call with correct arguments)
  and in practice (bind, use, close, and immediately rebind the same
  port in one run, no failure).

### v2.18: accept()/recv() timeout
- Done: `y.net.set_timeout(sock, ms)` — sets `SO_RCVTIMEO`, the
  standard portable way to time out either `accept()` (set on a
  listening socket) or `recv()` (set on a connected socket) instead of
  blocking forever. `ms<=0` clears it. Verified both cases directly:
  an idle listener with a 2000ms timeout returns after ~2s reporting
  "accept timed out"; a connected-but-silent peer's `recv()` with a
  1500ms timeout returns after ~1.5s reporting "recv timed out" — both
  distinguishable from other failures via `y.net.last_error()`.
  `connect()`'s own existing 10s timeout is unaffected (separate
  mechanism, unrelated to this).
- Interpreter + VM only, matching the tier the rest of `y.net.*` not
  already covering native compilation is at.

### v2.16: HTTP redirect following
- Done: `y.http.get`/`post` now follow 3xx redirects automatically (up
  to 10 hops, "too many redirects" on a loop rather than hanging).
  Correct method/body handling per redirect status: 303 and
  POST-via-301/302 downgrade to GET with no body; 307/308 preserve the
  original method and body. Handles both absolute and relative
  `Location` headers. All of this — the multi-hop chain, both
  downgrade cases, the loop-detection limit — was verified against
  purpose-built local test servers, not just read from the HTTP spec
  and assumed correct.
- Not implemented for native compilation (same tier as the rest of
  `y.http.*`/`y.net.tls_*`).

### v2.15: HTTP client — y.http.*
- Done: `y.http.get(url)`/`y.http.post(url, body, content_type)`,
  interpreter + VM, built on `y.net.*`/`y.net.tls_*`. Returns a
  `y.map` with status/body/headers, or nil on failure.
- Handles both `http://` and `https://` (the latter needs `make tls`).
  Decodes chunked `Transfer-Encoding` — tested directly against a
  server that deliberately sent a 3-chunk response, reassembled
  correctly. Tested against real endpoints (pypi.org, api.github.com)
  and a local server for the POST body (confirmed the full body
  arrives, not just headers).
- No redirect following, no cookies, no compression, no connection
  reuse — a basic client for straightforward request/response use.
- Not implemented for native compilation (same tier as `y.net.tls_*`).

### v2.14: TLS/HTTPS — y.net.tls_*
- Done: real TLS via OpenSSL (`y.net.tls_connect/send/recv/close`),
  interpreter + VM, Linux/macOS. Opt-in build flag (`make tls`,
  `-DYS_WITH_TLS -lssl -lcrypto`) so the existing build pipeline
  (including Windows CI) keeps working unchanged without needing
  OpenSSL set up.
- Certificate verification is enabled and was actually tested both
  ways, not just assumed: a self-signed/untrusted cert (a local test
  server) is correctly rejected; a valid cert (a real public HTTPS
  endpoint) succeeds with real response data received over the
  encrypted channel. SNI and hostname verification both confirmed
  working.
- This closes the single biggest gap flagged in v2.13's roadmap notes
  between "works and is tested" and genuinely usable for real traffic
  — `y.net.tls_*` can now talk to ordinary public HTTPS APIs/websites,
  not just plain-HTTP or internal/trusted-network services.
- Pending: TLS on Windows (interpreter/VM) — OpenSSL isn't wired up
  for MinGW builds yet.
- Pending: TLS for native compilation on any target — would need the
  native backend to support dynamic linking at all first, which it
  currently doesn't on any target (this is a bigger, separate
  foundational change, not a small extension of the existing raw-
  syscall approach).
- Pending: `y.net.tls_listen`/`tls_accept` (server-side TLS) — this
  batch covers the client side only.

### v2.13: Networking — toward production readiness
- Done: `process.fork()`/`process.wait()`, enabling a real
  fork-per-connection concurrent server (interpreter + VM). Verified
  with two real clients connecting simultaneously — separate forked
  children, no cross-talk between connections
- Done: `y.net.send` now loops until all data is sent instead of
  returning after a single short write, which could previously
  silently truncate large payloads. Verified with a 5MB send in one
  call, fully delivered
- Done (v2.12): native listen/accept on Linux
- Done (v2.11): connect timeout; server-side sockets, interpreter + VM
- Done (v2.10): native TCP client sockets, Linux
- Done (v2.9): TCP client sockets, interpreter + bytecode VM
- **TLS/HTTPS: see v2.14 above** — shipped shortly after this note was
  originally written as "not planned near-term" for the interpreter/VM
  via OpenSSL (opt-in build flag). Still not available for native
  compilation or on Windows — those parts of the original concern
  still stand.
- Pending: native compilation on Windows (Winsock2) and macOS (needs
  Mach-O dynamic linking first)
- Pending: hostname resolution for native builds
- Pending: `process.fork()`-based concurrency for native compilation
  (interpreter/VM only right now)

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