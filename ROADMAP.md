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
| **v2.24** | **IPv6 support for `y.net.connect`. Two additions: (1) IPv6 literals ("::1", "2001:db8::1") are parsed at compile time by this compiler's own portable parser (no platform networking headers — see the v2.26 entry for why that matters) — no runtime IPv6 text parsing needed, same reasoning as why the DNS query bytes for a hostname are built at compile time — and connect directly through a new `__ys_net_connect6` using `AF_INET6`/`sockaddr_in6` (28 bytes) instead of `AF_INET`/`sockaddr_in`. (2) Hostname resolution gains an AAAA fallback: `__ys_net_connect_host` (A, IPv4) is tried first as before, and only if it returns -1 does a new `__ys_net_connect_host6` get tried with a separately-built AAAA query, reusing the exact same resolv.conf-lookup, CNAME-skip, and multi-record-with-timeout structure as the A path — the two functions are near-identical by design, since duplicating the shared DNS-transport logic was more honest here than threading a record-type/address-family flag through offsets and struct sizes that differ in size (16 vs 4 byte addresses, 28 vs 16 byte sockaddr) throughout the function. IPv4 stays preferred: an A success skips AAAA entirely. Verified as thoroughly as this development environment allows: the IPv6 literal parser checked directly against several formats (compressed, full, IPv4-mapped, invalid-rejected); the AAAA query-build → send → response-parse → 16-byte-extract pipeline verified end-to-end against a local fake DNS server (including the A-returns-nothing → AAAA-fallback path); and `strace` confirming both `__ys_net_connect6` and the AAAA path attempt `socket(AF_INET6, ...)` with the correctly-parsed address. The actual `connect()` call could not be exercised end-to-end here since this environment has IPv6 disabled at the kernel level (`socket(AF_INET6,...)` returns `EAFNOSUPPORT` regardless of code correctness) — needs confirming on a host with real IPv6 connectivity** |
| **v2.25** | **Native UDP sockets: `y.net.udp_socket/udp_bind/udp_send/udp_recv_print/udp_recv_reply_print/udp_close` now compile under `ys -c --target linux`, closing the gap noted in the v2.20 entry above. `udp_recv`'s interpreter/VM signature returns `{data, host, port}` as a `y.map` — no equivalent exists natively (no map type, no runtime string type), so it splits into two primitives instead of one: `udp_recv_print(sock, maxlen)` reads and prints the payload with the sender discarded (same "print instead of return" reasoning as `recv_print`), and `udp_recv_reply_print(sock, maxlen, reply_data)` reads, prints, and sends `reply_data` back to whichever address the datagram arrived from — captured in the runtime function's own stack memory and never surfaced to the Yolish program as a value, covering the single most common reason a program needs the sender's address at all (an echo/reply server) without needing a map or string return type to do it. `udp_send`'s `host` argument is an IPv4 literal only for this batch, not a hostname (parallel to how `y.net.connect` itself started IPv4-literal-only before DNS support was added later) — its octet-parsing loop is adapted from `__ys_net_connect`'s runtime version rather than the newer compile-time literal path, since `host` here arrives as a real ptr/len argument pair, not something already sitting in rodata. Verified with a real two-process client/server exchange (bind, send, receive-and-reply-to-captured-sender, receive the reply) and a send to a refused port correctly succeeding at the UDP layer (fire-and-forget — the datagram sends fine even with nobody listening, which is correct UDP semantics, not a bug)** |
| **v2.26** | **Build fix: v2.24's IPv6-literal parser used the host libc's `inet_pton` (`<arpa/inet.h>`), which doesn't exist under the mingw cross-compiler — broke `make windows` (and the Windows release build) immediately, since compiler.c is the ys compiler's own source and gets built for every target platform regardless of which platform the emitted program targets. Replaced with a small fully portable IPv6 literal parser (`::` zero-compression, embedded trailing IPv4, no platform headers at all) so the same code builds identically on Linux, mingw, and macOS. Re-verified against the same literal formats as v2.24 (compressed, full, IPv4-mapped, invalid-rejected) plus a clean `make windows`** |
| **v2.27** | **ELF dynamic-linking milestone: native-compiled Linux binaries can now import and call real functions from a shared library (starting with libc.so.6's `puts`/`exit`, proven via a new `y.net.dynlink_test()` builtin) — a first, deliberate exception to this backend's fully-static/no-libc design, aimed at eventually linking against a real TLS library rather than hand-rolling cryptography in raw machine code (a serious, unreviewable security risk that was flagged and explicitly ruled out before this work started). New `elf_write_dynamic` in elf_out.c emits `PT_INTERP`/`PT_DYNAMIC` plus a minimal `.dynsym`/`.dynstr`/SysV `.hash`/`.rela.dyn`, resolving each import eagerly via a plain `R_X86_64_GLOB_DAT` relocation into an 8-byte GOT slot (`dynlink_import` in compiler.c) rather than a lazy PLT trampoline, so an imported call is just `mov reg,[got_slot]; call reg` — no PLT stub needed. Validated against a hand-built standalone prototype (raw Python-constructed ELF bytes) before being ported into the real compiler, which is what caught all three of the real bugs along the way:
> 1. `PT_PHDR`'s `p_vaddr`/`p_offset` must describe where the phdr table itself lives (right after the Ehdr), not the ELF header's own start — get this wrong and ld.so silently miscomputes the executable's load bias, which then corrupts every address it derives from `.dynamic`. This doesn't fail loudly: it crashed deep inside ld.so's own audit-tag-processing code, nothing to do with auditing itself, just the first place that happened to dereference the resulting bad pointer.
> 2. The phdr table itself has to be covered by a `PT_LOAD` segment (describing it via `PT_PHDR` alone isn't enough) — otherwise the kernel can't hand ld.so a valid `AT_PHDR` in the auxiliary vector, and `main_map->l_phdr` comes back NULL. This one only showed up once ported into the real compiler: the standalone prototype's tiny hand-written code happened to fit entirely within file offset 0's segment by construction, so it never exposed the bug; the real compiler's much larger `emit_helpers()` output didn't.
> 3. Calling `exit()` via a raw syscall instead of importing libc's own `exit()` skips glibc's stdio-flush machinery, so `puts()`'s internally-buffered output silently never appears even though nothing crashes — caught in the standalone prototype before porting, by noticing `puts()`'s message wasn't showing up despite a clean exit.
>
> Regular (non-`dynlink_test()`) native compilation is completely unaffected: the existing regression suite (all `examples/dns_examples/*.y`, the UDP client/server exchange) passes unchanged, and a program that never calls `dynlink_test()` still produces a fully static binary — confirmed with `file` — not a dynamic one. What this explicitly is *not*: a general FFI. Marshaling arbitrary argument/return types for arbitrary imported functions is a separate, much bigger design problem than what this establishes, which is that the underlying ELF dynamic-linking machinery itself works correctly** |
| **v2.28** | **Seven bugs from an external review, fixed and re-verified against the interpreter and, where applicable, the VM (parser.c and eval.c are shared by both; the VM has its own separate comparison-opcode implementation in vm.c that needed its own matching fix for #3):
> 1. **Chained postfix access silently parsed wrong instead of erroring.** `arr[i].field`, `obj.field[i]`, `arr[i][j]`, or any mix of the two, more than one level deep, produced silently wrong results rather than a parse error — `arr[0].x` printed the *entire struct* at `arr[0]` instead of its `x` field, because indexing (`[...]`) and dot access (`.field`) were two separate parser blocks that each returned immediately on their own kind of postfix instead of composing, so a `.field` following a `[...]` (or vice versa) was silently left unconsumed on the token stream. Fixed in parser.c with one shared postfix loop handling both in any order/repetition. This also meant assignment through a chained target (`arr[i].field = v`, `obj.field[i] = v`) was a silent no-op beyond a bare identifier target — fixed to match, in both eval.c (interpreter) and confirmed already correct in bcompiler.c (VM), which had been designed for this shape of assignment but could never actually receive it from the broken parser.
> 2. **`y.is_err()` always returned false.** It checked for the internal `YS_ERR` value type, but `y.error()` builds a struct named `"Error"` — a different, pre-existing representation used for something else entirely (the value a bare `throw` produces with no explicit value) — so `y.is_err(y.error(...))` could never be true. Now recognizes both representations.
> 3. **String `<`/`>`/`<=`/`>=` silently always compared as numbers.** Both the interpreter's `ND_BINOP` handling and the VM's separate `OP_LT`/`OP_GT`/`OP_LE`/`OP_GE` implementation fell straight through to integer/float conversion for any operand type, so two strings being compared were actually comparing their (near-always-zero) numeric conversions — `"Aisha" < "Rafi"` and `"Rafi" < "Aisha"` were both silently false. Fixed in both places with a dedicated string branch.
> 4. **`y.slice(...)` doesn't exist.** DOCS.md showed it in two places; the real function is `y.array.slice(...)`. Both corrected.
> 5. **`y.math.pi`/`sin`/`cos`/`tan`/`log` were documented (README, DOCS.md) but never implemented** — silently returned `nil`. Added: `pi` as a namespaced-constant read (`y.math.pi`, no call — a separate code path from call-style builtins like `y.math.sqrt(x)`, needing its own qualified-name lookup since the existing one only fires for actual calls), the rest via the standard math library.
> 6. **`@cap(...)` — the security-critical capability-annotation system — enforced nothing at all**, despite "security by default, no resource access without a capability" being a stated design principle of the language elsewhere in this document. The parser choked on `@cap(net.read, fs.write)`'s dotted-identifier arguments (they matched neither `@intent`/`@audit`'s single-quoted-string case nor an immediate close-paren, so parsing broke silently and left tokens on the stream to be mis-parsed as later garbage statements), `y.capabilities()` and `y.has_cap()` didn't exist as builtins, and — the actual severity here — nothing ever checked a function's declared capabilities against anything before running it regardless of whether they'd been granted. All fixed: the annotation parser now handles comma-separated dotted names, `y.capabilities()`/`y.has_cap()` exist, and `@cap`-annotated functions now genuinely refuse to run (verified via `try`/`catch`, which surfaces a clear `capability denied: fn '...' requires '...'` error) unless every declared capability has been granted — deny by default. Since the documented grant mechanism is OS-level ("the kernel validates the capability" on Exploidus OS) and there's no such kernel on an ordinary development machine, a new `y.grant(name)` was added as the minimal thing that lets `@cap`-gated code run at all outside that kernel — not part of the previously-documented API, since none existed for this.
> 7. **`main()` ran twice.** It's auto-called if defined (a real, working convenience feature), and README's own Quick Start example also called it explicitly right after defining it — removed the redundant explicit call from the example instead of the auto-call feature itself, since nothing else in the codebase depended on the double-call and the auto-call is genuinely useful.
>
> Every fix re-verified individually plus a full pass of the existing example suite (`examples/*.y`) and the native-compiler regression set (`examples/dns_examples/*.y`, the UDP client/server exchange, `y.net.dynlink_test()`) to confirm none of the shared-file changes (parser.c, eval.c) affected native compilation, which doesn't support structs/capabilities/etc. at all but does share the same parser** |
| **v2.29** | **TLS milestone, built on v2.27's dynamic-linking machinery: a native-compiled Linux binary completed a real TLS 1.3 handshake and an encrypted HTTPS request/response round trip against a real server, entirely through hand-assembled machine code calling into the system's actual OpenSSL (`libssl.so.3`) — no hand-rolled cryptography, which was explicitly ruled out as a security risk when this direction was first scoped.
> Two open questions had to be resolved first:
> 1. `elf_write_dynamic` only supported a single hardcoded `DT_NEEDED` library (`libc.so.6`). Extended to a list — this needed no change at all to the actual per-import `R_X86_64_GLOB_DAT` relocation mechanism, since ld.so's symbol search already spans every loaded library regardless of which one's `DT_NEEDED` entry brought it in; it was purely a `.dynstr`/`.dynamic`-array change (`dynlink_need_library` in compiler.c registers a library once, `libc.so.6` always included).
> 2. This backend's existing symbol resolution is deliberately unversioned (no `.gnu.version`/`.gnu.version_r`) — fine for libc's `puts`/`exit`, but OpenSSL 3.0 exports everything with an explicit version tag (`OPENSSL_init_ssl@@OPENSSL_3.0.0` and so on), a real question mark this backend hadn't faced yet. Checked in isolation first with a narrow `y.net.tls_test()` (just `OPENSSL_init_ssl` + `TLS_client_method`, no socket involved) before building anything further on top — confirmed working: an unversioned symbol reference resolves to a versioned export's default version with no special handling needed.
>
> With both confirmed, `y.net.tls_handshake_test()` chains `TLS_client_method` → `SSL_CTX_new` → `SSL_new` → `SSL_set_fd` (reusing the exact same DNS-query-building and `__ys_net_connect_host` call the real `y.net.connect()` already uses for the underlying TCP connection — no new networking code needed) → `SSL_connect`, and `y.net.tls_get_test()` goes one step further: a real HTTPS GET via `SSL_write`, printing whatever `SSL_read` decrypts back. Verified with `strace`: a complete, correctly-ordered TLS 1.3 record flow (ClientHello out; the server's ServerHello/EncryptedExtensions/Certificate/CertificateVerify/Finished flight in one shot; the client's own encrypted Finished out) followed by a real, well-formed decrypted HTTP response — proving the encryption/decryption round trip genuinely works, not just the handshake. Re-verified 5/5 clean runs plus the full existing native regression set (`examples/dns_examples/*.y`, the UDP client/server exchange, `y.net.dynlink_test()`) unaffected.
> Same scope caveats as v2.27: hardcoded test host/port, no `SSL_free`/`SSL_CTX_free`/socket cleanup, and not yet a general public API. `y.net.tls_connect(host, port)` / `tls_send` / `tls_recv_print` / `tls_close`, mirroring the plain-TCP shape and probably needing a small fixed-size connection-handle table (this backend has no struct/map type to return a `{ssl, ctx, fd}` bundle with directly), is the natural next step — not attempted in this pass** |
| **v2.30** | **The real TLS public API `y.net.tls_connect(host, port)` / `tls_send(handle, data)` / `tls_recv_print(handle, maxlen)` / `tls_close(handle)`, closing out the "natural next step" the v2.29 entry above ended on. Backed by a small fixed-size round-robin connection-handle table (`YS_TLS_MAX_CONN`=4 slots, `{fd, ctx, ssl}` each) since this backend still has no struct/map type to hand back a bundle directly. Building it surfaced five real bugs, each fixed structurally rather than patched after the fact:
> 1. **Stack-alignment corruption.** Pushing a runtime argument (port) onto the stack to marshal it into a call, the same way `y.net.connect` already does, leaves rsp 8-but-not-16-byte aligned by the time a `call` reaches into libssl's SIMD-using internals if the push count before it is odd. Fixed by never using push/pop inside any of the four new functions at all: each opens one fixed-size frame (`sub rsp,N`, N always a multiple of 16, same shape as `tls_handshake_test`/`tls_get_test`) and stores every intermediate value — fd, ctx, ssl, the handle itself — into an rbp-relative slot instead, so rsp only ever moves twice (entry, exit), both by a multiple of 16.
> 2. **Hand-transcribed GOT-call risk.** The `lea r11,[rip+got]; mov rax,[r11]; call rax` sequence used to be re-typed at every single call site — exactly the kind of place a stray extra byte slips in unnoticed. Centralized into one `x_call_got()` helper.
> 3. **Register clobber across a call.** `SSL_free()`'s call clobbers every caller-saved register, RCX included — reading `&slot` back out of RCX right after that call in `tls_close` reads garbage. Fixed by saving the slot address to `[rbp-16]` once and reloading from there after every single call, never trusting a register to survive one.
> 4. **Frame-switch argument bug — caught only once this was actually run, not from reasoning about it up front.** Each function opens its own nested stack frame (needed for #1 above) with its own `push rbp; mov rbp,rsp`. Compiling an argument expression (e.g. a handle stored in an outer local) *after* that switch resolves the variable against the new, not-yet-written frame instead of the caller's — silently reading uninitialized stack memory instead of the real value, with no crash to signal it (`objdump` on the emitted binary showed `mov rax,[rbp-0x8]` reading a slot before anything had ever written to it). Fixed by evaluating every argument first, while the caller's rbp is still live, staging each result through a small rip-relative scratch buffer, and only then opening the new frame and loading them back out of it.
> 5. **`puts()`'s output silently never appearing — also only caught by running it.** `tls_recv_print` initially printed the same way `tls_get_test` does, via libc's buffered `puts()`. That works for `tls_get_test` only because it always calls libc's real `exit()` immediately afterward, which flushes stdio on the way out; a function meant to return normally, like this one, hits this program's *normal* end-of-program path instead, which is a raw `exit` syscall — bypassing glibc's flush machinery entirely, so the buffered payload just never appears, even though `SSL_read` genuinely received it (confirmed with `strace`: the `read()` succeeds, no matching `write(1,...)` ever follows). Switched to the same raw `SYS_write(1, buf, n)` syscall `__ys_net_recv_print` (the plain, non-TLS version) already uses.
>
> Also added SNI (`SSL_set_tlsext_host_name`, a macro over `SSL_ctrl(ssl, 55, 0, host)` — both constants ABI-stable since OpenSSL 1.0.x) for the hostname branch, after real-host testing turned up a live discrepancy: a Cloudflare-fronted `example.com` failed its handshake without it while Google's frontend happened to succeed regardless — a genuine correctness gap for any name-based-virtual-hosting server, not an isolated fluke. Not sent for the IP-literal branch, since an IP address isn't valid SNI content.
> Verified against two real hosts at once (Cloudflare/`example.com`, Google) with distinct, non-cross-wired responses proving the handle table isolates simultaneous connections correctly; an invalid handle failing cleanly (`-1`, no-op) across all three of `tls_send`/`tls_recv_print`/`tls_close` without crashing; and a clean `make windows` — these four builtins now explicitly check `g_target==TARGET_LINUX` and no-op/return `-1` otherwise, so compiling one of these calls for a non-Linux target can't emit the ELF-only dynlink machinery into a PE/Mach-O binary (the same class of mistake v2.26 had to fix for the IPv6 parser).
> Unrelated fix caught along the way, in `main.c` rather than the TLS code itself: `ys -c some/dir/file.y` with no explicit `-o` computed the default output path from the *full* source path after already `chdir`-ing into its directory, doubling the directory and making `fopen` fail inside `ys_compile` — now built from the basename only, since the working directory is already correct by that point** |
| **v2.31** | **Closes out the rest of native networking: TLS server (`y.net.tls_listen(port, certfile, keyfile)` / `tls_accept(server_handle)`) and a native HTTP client (`y.http.get_print(url)` / `post_print(url, body, content_type)`).
> `tls_listen` loads a real certificate/key pair off disk (`SSL_CTX_use_certificate_file`/`SSL_CTX_use_PrivateKey_file`) and reuses `__ys_net_listen` — the exact same socket/bind/listen subroutine `y.net.listen` calls — rather than re-implementing it. `tls_accept` reuses `__ys_net_accept` the same way, then does the server-side half of the handshake (`SSL_accept` instead of `SSL_connect`), and stores the resulting connection into the *same* client-connection table `tls_connect` already uses, with one deliberate difference: `ctx` is stored as `0`, not the listening socket's real, shared `SSL_CTX` — because `tls_close` unconditionally calls `SSL_CTX_free(ctx)`, and if an accepted connection's slot held the real shared context, closing that one connection would free the context every future `tls_accept` on the same listener still needs. `SSL_CTX_free(NULL)` is a documented no-op (the same property `tls_close` already relied on for `SSL_free`), so storing `0` makes that cleanup call harmless instead of a use-after-free waiting to happen. Backed by its own small round-robin table (`YS_TLSSRV_MAX`=2 slots, `{listen_fd, ctx}` each) — a genuinely separate handle space from the connection table, since a listening socket's context is long-lived and shared, unlike a one-shot client connection's.
>
> `y.http.get_print`/`post_print` parse the URL entirely in C, at compile time (`parse_http_url`) — scheme picks TLS or plain TCP, host/port/path all come out of the literal, none of it ever touches runtime code. The connect-and-request sequence itself (`emit_http_request`, shared between both functions) reuses the exact same underlying pieces as `y.net.connect`/`y.net.tls_connect`: same three-shape host resolution, same handshake steps, same SNI logic — this is a thin composition layer over already-tested machinery, not a separate networking stack.
>
> Two real bugs surfaced building this, both caught only by actually running the result against real servers:
> 1. **A register/value-across-a-call bug of the same *class* as v2.30's frame-switch bug, different concrete cause.** `emit_http_request`'s shared cleanup path (`SSL_free`/`SSL_CTX_free`/`close`, which between them clobber rax three times over) read its final return value from a stack slot that was only ever written on two of the six code paths that could reach it — the "response received" and "response empty" paths stashed their value there correctly, but the five early-failure paths (connect failed, `SSL_CTX_new` failed, `SSL_new` failed, `SSL_set_fd` failed, `SSL_connect` failed) set `rax=-1` and jumped straight to that same convergence point *without* writing to the slot, so the final unconditional reload picked up whatever uninitialized garbage happened to be sitting in that stack slot instead of `-1`. Same underlying lesson as v2.30's discovery — never assume a value survives to a point in the code without explicitly checking every path that reaches that point actually put it there — just a different function, a different slot, and a different way of arriving at the same mistake. Fixed by having every single path (all seven of them: five early failures, empty-response, and successful-response) stash its own correct value into the slot immediately, right before converging, rather than relying on a shared reload afterward to "just work."
> 2. **A single read isn't a full HTTP response.** The first version of the read/print step (matching `tls_recv_print`'s existing one-shot-by-design behavior) called `SSL_read`/raw `read()` exactly once. Against real production servers (Cloudflare, Google) this happened to work fine — small responses, sent as one segment. Against a real local Python test server, headers and body arrived as two separate writes on the server side and landed as two separate TCP segments on the wire; the one-shot read only ever saw the headers, and the body was silently missing from the printed output — no crash, no error, just an incomplete response that would be easy to miss without testing against something less cooperative than a CDN's edge server. Fixed with an actual read loop (a backward jump, `patch_i32(jback, loop_top-(jback+4))` — the same raw technique `ND_WHILE`'s codegen already uses elsewhere in this file) that keeps reading and printing chunks until the read returns `≤0`, accumulating a running total for the final return value. Deliberately scoped to the HTTP client only — `y.net.recv_print`/`y.net.tls_recv_print` keep their existing, documented one-shot contract unchanged, since other code may already depend on that.
>
> Verified: a full self-signed-certificate TLS server ↔ `tls_connect` client exchange, both directions, in the same process pair; `get_print` against a real Cloudflare-fronted host (matching v2.30's testing) and a real local Python HTTP server (confirming the multi-segment fix — response body now arrives intact); `post_print` against a local server that echoes the request body and `Content-Type` back, confirming both arrive correctly; invalid inputs (nonexistent cert/key paths, an out-of-range server handle, an unsupported URL scheme, a malformed URL) all failing cleanly (`-1`) rather than crashing; the full existing interpreter/native regression suite and `make windows` unaffected** |

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