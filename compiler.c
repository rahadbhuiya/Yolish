/*  compiler.c  —  Yolish → x86-64 native code
 *
 *  Supports: int/bool/string variables, functions, if/else,
 *  while, for-range, return, y.println, y.print, y.exit,
 *  arithmetic (+,-,*,/,%), comparisons, logical &&/||/!
 *
 *  Calling convention: System V AMD64 (Linux/macOS) or
 *  Microsoft x64 (Windows) selected at emit time.
 *
 *  All values are 64-bit integers on the stack.
 *  Strings are stored as read-only data pointers.
 */

#include "yolish.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*  output buffer  */
#define CODE_MAX  (1 << 20)   /* 1 MB code */
#define DATA_MAX  (1 << 20)   /* 1 MB rodata */
#define RELOC_MAX 4096

static uint8_t  code_buf[CODE_MAX];
static int      code_len = 0;

static uint8_t  data_buf[DATA_MAX];
static int      data_len = 0;

/* emit helpers */
static void emit1(uint8_t b){ code_buf[code_len++]=b; }
static void emit2(uint8_t a,uint8_t b){ emit1(a);emit1(b); }
static void emit3(uint8_t a,uint8_t b,uint8_t c){ emit1(a);emit1(b);emit1(c); }
static void emit4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){ emit1(a);emit1(b);emit1(c);emit1(d); }

static void emit_i32(int32_t v){
    emit1((uint8_t)(v    ));
    emit1((uint8_t)(v>> 8));
    emit1((uint8_t)(v>>16));
    emit1((uint8_t)(v>>24));
}
static void emit_i64(int64_t v){
    emit_i32((int32_t)(v));
    emit_i32((int32_t)(v>>32));
}

/* patch a 32-bit value at offset */
static void patch_i32(int offset, int32_t v){
    code_buf[offset  ]=(uint8_t)(v    );
    code_buf[offset+1]=(uint8_t)(v>> 8);
    code_buf[offset+2]=(uint8_t)(v>>16);
    code_buf[offset+3]=(uint8_t)(v>>24);
}

/* add string to rodata, return offset */
static int data_add_str(const char *s){
    int off=data_len;
    while(*s) data_buf[data_len++]=(uint8_t)*s++;
    data_buf[data_len++]=0;
    return off;
}

/* add raw bytes (not NUL-terminated, may contain embedded zeros) to
   rodata, return offset. Used for the DNS query packet, which has
   zero bytes as structural content (QNAME label terminator, QDCOUNT
   high byte, etc.) so data_add_str's NUL-scan would truncate it. */
static int data_add_bytes(const uint8_t *b, int len){
    int off=data_len;
    for(int i=0;i<len;i++) data_buf[data_len++]=b[i];
    return off;
}

/* true if s is a plain dotted-decimal IPv4 literal (digits and dots
   only, e.g. "93.184.216.34") — the fast path that skips DNS
   entirely and reuses the original octet-parsing __ys_net_connect.
   Anything containing a letter (or anything else) is treated as a
   hostname needing resolution. Doesn't validate octet ranges/count;
   __ys_net_connect's own parser is just as forgiving of malformed
   input, so this only needs to distinguish the two *shapes*. */
static int is_ipv4_literal(const char *s){
    if(!*s) return 0;
    for(const char *p=s; *p; p++){
        if(!((*p>='0'&&*p<='9') || *p=='.')) return 0;
    }
    return 1;
}

/* true if s looks like an IPv6 literal ("::1", "2001:db8::1", etc.) --
   the one character that never appears in a hostname, IPv4 literal, or
   port number but always appears in an IPv6 address (at minimum "::")
   is ':', so that alone is a safe, sufficient shape test here. Actual
   validation happens in parse_ipv6_literal. */
static int is_ipv6_literal(const char *s){
    return strchr(s,':') != NULL;
}

/* Parse a single run of ':'-separated IPv6 groups containing no "::"
   (parse_ipv6_literal splits on "::" itself and calls this once per
   side). Up to 8 16-bit groups go into out, returned as the group
   count, or -1 on anything malformed. An empty run (s=="", the case
   where "::" sits at the very start or end of the whole address)
   yields 0 groups. If the last field contains a '.', it's treated as
   a trailing embedded IPv4 address (the "192.168.1.1" in
   "::ffff:192.168.1.1") and expands to exactly 2 groups instead of 1. */
static int parse_ipv6_group_run(const char *s, uint16_t *out){
    if(*s=='\0') return 0;
    int n=0;
    const char *p=s;
    while(*p){
        const char *colon=strchr(p,':');
        int is_last_field = (colon==NULL);
        if(is_last_field && strchr(p,'.')){
            unsigned a,b,c,d; char extra;
            if(sscanf(p,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4) return -1;
            if(a>255||b>255||c>255||d>255) return -1;
            if(n+2>8) return -1;
            out[n++]=(uint16_t)((a<<8)|b);
            out[n++]=(uint16_t)((c<<8)|d);
            return n;
        }
        const char *field_end = colon ? colon : p+strlen(p);
        int flen = (int)(field_end-p);
        if(flen<1||flen>4) return -1;
        unsigned val=0;
        for(int i=0;i<flen;i++){
            char c=p[i]; int digit;
            if(c>='0'&&c<='9') digit=c-'0';
            else if(c>='a'&&c<='f') digit=c-'a'+10;
            else if(c>='A'&&c<='F') digit=c-'A'+10;
            else return -1;
            val = val*16 + (unsigned)digit;
        }
        if(n+1>8) return -1;
        out[n++]=(uint16_t)val;
        if(!colon) break;
        p = colon+1;
        if(*p=='\0') return -1; /* trailing ':' not part of "::" */
    }
    return n;
}

/* Parse an IPv6 literal into 16 raw bytes, entirely portable (no
   platform networking headers) since this file is cross-compiled for
   Windows/mingw and macOS as well as Linux, and inet_pton lives in a
   different, not-always-present header on each (this broke the
   Windows build the first time around: arpa/inet.h doesn't exist
   under mingw). Handles "::" zero-compression (at most one, per RFC)
   and a trailing embedded IPv4 tail. Returns 1 on success, 0 if s
   isn't a valid IPv6 address (caller falls back to the
   unresolved-symbol safety net, same as any other unsupported
   y.net.connect argument shape). */
static int parse_ipv6_literal(const char *s, uint8_t out[16]){
    const char *dc = strstr(s,"::");
    uint16_t groups[8];
    int ngroups;
    if(dc){
        if(strstr(dc+2,"::")) return 0; /* at most one "::" is legal */
        char left[64], right[64];
        int llen=(int)(dc-s);
        if(llen<0||llen>=(int)sizeof(left)) return 0;
        memcpy(left,s,(size_t)llen); left[llen]=0;
        const char *rstart=dc+2;
        size_t rlen=strlen(rstart);
        if(rlen>=sizeof(right)) return 0;
        memcpy(right,rstart,rlen); right[rlen]=0;

        uint16_t lg[8], rg[8];
        int lc = parse_ipv6_group_run(left, lg);
        int rc = parse_ipv6_group_run(right, rg);
        if(lc<0||rc<0) return 0;
        int missing = 8-lc-rc;
        if(missing<0) return 0;
        ngroups=0;
        for(int i=0;i<lc;i++) groups[ngroups++]=lg[i];
        for(int i=0;i<missing;i++) groups[ngroups++]=0;
        for(int i=0;i<rc;i++) groups[ngroups++]=rg[i];
    } else {
        ngroups = parse_ipv6_group_run(s, groups);
    }
    if(ngroups!=8) return 0;
    for(int i=0;i<8;i++){
        out[i*2]   = (uint8_t)(groups[i]>>8);
        out[i*2+1] = (uint8_t)(groups[i]&0xFF);
    }
    return 1;
}

/* Build a DNS query packet (header + QNAME + QTYPE=AAAA + QCLASS=IN)
   for a hostname literal, entirely at compile time. Identical to
   build_dns_query except QTYPE=28 (AAAA) instead of 1 (A) -- kept as a
   separate function rather than a shared helper with a type parameter
   since the two query buffers need to coexist at runtime (A tried
   first, AAAA as fallback) and duplicating twelve lines is clearer
   here than threading a type flag through call sites that only ever
   pass a compile-time constant anyway. */
static int build_dns_query_aaaa(const char *host, uint8_t *out){
    int p=0;
    out[p++]=0x12; out[p++]=0x34;             /* transaction ID */
    out[p++]=0x01; out[p++]=0x00;             /* flags: RD=1 */
    out[p++]=0x00; out[p++]=0x01;             /* QDCOUNT=1 */
    out[p++]=0x00; out[p++]=0x00;             /* ANCOUNT=0 */
    out[p++]=0x00; out[p++]=0x00;             /* NSCOUNT=0 */
    out[p++]=0x00; out[p++]=0x00;             /* ARCOUNT=0 */
    const char *s=host;
    while(*s){
        const char *dot=strchr(s,'.');
        int len = dot ? (int)(dot-s) : (int)strlen(s);
        if(len<1||len>63) len = len<1?0:63;
        out[p++]=(uint8_t)len;
        memcpy(out+p,s,len); p+=len;
        s += len; if(*s=='.') s++;
    }
    out[p++]=0x00;             /* end of QNAME */
    out[p++]=0x00; out[p++]=0x1c; /* QTYPE=AAAA (28) */
    out[p++]=0x00; out[p++]=0x01; /* QCLASS=IN */
    return p;
}

/* ---- ELF dynamic linking (PT_INTERP/PT_DYNAMIC) ----
   The native backend is fully static/freestanding by design (no
   libc, no dynamic linking at all) everywhere else in this file. This
   is a deliberate, narrow exception for importing a handful of real
   functions from an actual shared library — the motivating goal is a
   real TLS library eventually, rather than hand-rolling cryptography
   in raw machine code, which would be a serious security risk with
   no review process behind it. TARGET_LINUX/x86-64 only for now.

   This mechanism (and elf_write_dynamic in elf_out.c, which does the
   actual ELF-structure work) was validated against a standalone
   hand-built prototype before being ported here — see
   elf_write_dynamic's comments for the two real bugs that caught. */
static int g_dyn_enabled = 0;
#define MAX_DYN_IMPORTS 32
typedef struct { char name[64]; int got_off; } DynImport;
static DynImport g_dyn_imports[MAX_DYN_IMPORTS];
static int g_dyn_nimports = 0;
#define MAX_DYN_NEEDED 8
static char g_dyn_needed[MAX_DYN_NEEDED][64];
static int g_dyn_nneeded = 0;

/* Registers name ("libssl.so.3" etc.) as a DT_NEEDED library for this
   binary, if not already registered. libc.so.6 is always included
   (see ys_compile's dynlink finalization) since every import so far
   has come from it or, transitively, from something libc itself
   depends on; other libraries (a TLS library, eventually) call this
   explicitly before importing any of their symbols. Multiple needed
   libraries don't need any change to how imports themselves resolve
   — see elf_write_dynamic's comment on why. */
static void dynlink_need_library(const char *libname){
    g_dyn_enabled = 1;
    for(int i=0;i<g_dyn_nneeded;i++) if(strcmp(g_dyn_needed[i],libname)==0) return;
    if(g_dyn_nneeded<MAX_DYN_NEEDED) snprintf(g_dyn_needed[g_dyn_nneeded++],64,"%s",libname);
}

/* Reserves (or reuses, if already imported) an 8-byte zeroed GOT slot
   in data_buf for symname, and returns its offset. Once ld.so
   processes this import's R_X86_64_GLOB_DAT relocation at load time
   (see elf_write_dynamic), that slot holds symname's resolved
   address — codegen calls it with `lea r11,[rip+got_off]` (the usual
   RELOC_DATA pattern) `; mov reg,[r11] ; call reg`. No PLT trampoline
   needed since this is eager (load-time), not lazy, resolution. */
static int dynlink_import(const char *symname){
    g_dyn_enabled = 1;
    for(int i=0;i<g_dyn_nimports;i++)
        if(strcmp(g_dyn_imports[i].name,symname)==0) return g_dyn_imports[i].got_off;
    static const uint8_t zero8[8] = {0,0,0,0,0,0,0,0};
    int off = data_add_bytes(zero8, 8);
    if(g_dyn_nimports<MAX_DYN_IMPORTS){
        snprintf(g_dyn_imports[g_dyn_nimports].name,64,"%s",symname);
        g_dyn_imports[g_dyn_nimports].got_off = off;
        g_dyn_nimports++;
    }
    return off;
}

/* Build a DNS query packet (header + QNAME + QTYPE=A + QCLASS=IN) for
   a hostname literal, entirely at compile time — safe because the
   hostname is already required to be a compile-time string literal,
   same as the IPv4 case. Writes into out (caller-sized buffer, host
   length + 16 bytes is always enough) and returns the packet length.
   Mirrors the verified prototype in proto/dnstest.c build_query(). */
static int build_dns_query(const char *host, uint8_t *out){
    int p=0;
    out[p++]=0x12; out[p++]=0x34;             /* transaction ID */
    out[p++]=0x01; out[p++]=0x00;             /* flags: RD=1 */
    out[p++]=0x00; out[p++]=0x01;             /* QDCOUNT=1 */
    out[p++]=0x00; out[p++]=0x00;             /* ANCOUNT=0 */
    out[p++]=0x00; out[p++]=0x00;             /* NSCOUNT=0 */
    out[p++]=0x00; out[p++]=0x00;             /* ARCOUNT=0 */
    const char *s=host;
    while(*s){
        const char *dot=strchr(s,'.');
        int len = dot ? (int)(dot-s) : (int)strlen(s);
        if(len<1||len>63) len = len<1?0:63; /* defensive clamp, not a validator */
        out[p++]=(uint8_t)len;
        memcpy(out+p,s,len); p+=len;
        s += len; if(*s=='.') s++;
    }
    out[p++]=0x00;             /* end of QNAME */
    out[p++]=0x00; out[p++]=0x01; /* QTYPE=A */
    out[p++]=0x00; out[p++]=0x01; /* QCLASS=IN */
    return p;
}

/*  relocations  */
typedef enum { RELOC_DATA, RELOC_CODE } RelocKind;
typedef struct { RelocKind kind; int code_off; int target_off; } Reloc;
static Reloc relocs[RELOC_MAX];
static int   nrelocs=0;

static void add_reloc(RelocKind k, int code_off, int target_off){
    relocs[nrelocs++]=(Reloc){k,code_off,target_off};
}

/* Windows: indirect call through an IAT slot, e.g. `call [rip+disp32]`.
   Import index must match win_imports[] order in pe_out.c:
   0=GetStdHandle, 1=WriteFile, 2=ExitProcess.
   Emits the correct 6-byte FF 15 <disp32> encoding (a prior version
   emitted an extra stray byte here, misaligning the instruction stream)
   and records the disp32 offset via RELOC_CODE so pe_write can patch it
   to point at the real IAT slot once section layout is known. */
/* forward decl: g_target is defined further below, but the ABI helpers
   right after add_import_call() need to branch on it */
static Target g_target;

static void add_import_call(int import_idx){
    emit2(0xff,0x15);
    add_reloc(RELOC_CODE, code_len, import_idx);
    emit_i32(0);
}

/* Move rax into the 1st/2nd integer-argument register per target ABI.
   SysV (Linux/macOS): arg1=rdi, arg2=rsi.  Microsoft x64 (Windows): arg1=rcx, arg2=rdx.
   Every call site that hands scalar args to a runtime helper (__ys_print_str,
   __ys_print_int, __ys_exit, ...) MUST go through these, or the callee reads
   garbage registers on Windows even though the exact same code "works" on Linux. */
static void x_arg1_from_rax(void){
    if(g_target==TARGET_WINDOWS) emit3(0x48,0x89,0xc1); /* mov rcx,rax */
    else                         emit3(0x48,0x89,0xc7); /* mov rdi,rax */
}
static void x_arg2_from_rax(void){
    if(g_target==TARGET_WINDOWS) emit3(0x48,0x89,0xc2); /* mov rdx,rax */
    else                         emit3(0x48,0x89,0xc6); /* mov rsi,rax */
}
/* lea <arg1-reg>, [rip+data_off]  (records the RELOC_DATA fixup) */
static void x_lea_arg1_data(int data_off){
    if(g_target==TARGET_WINDOWS) emit3(0x48,0x8d,0x0d); /* lea rcx,[rip+..] */
    else                         emit3(0x48,0x8d,0x3d); /* lea rdi,[rip+..] */
    add_reloc(RELOC_DATA,code_len,data_off);
    emit_i32(0);
}

/* ---- TLS public API state (y.net.tls_connect/tls_send/tls_recv_print/
   tls_close) ----
   Generalizes the tls_test/tls_handshake_test/tls_get_test proof-of-concepts
   (further below, in compile_node's ND_CALL handling) into a real API with
   host/port arguments and a small fixed-size connection-handle table — this
   backend has no struct/map type to return a {fd,ctx,ssl} bundle directly,
   so a global array indexed by handle stands in for one.

   Three real bugs surfaced building this the first time (an earlier, lost
   pass) and are designed out here rather than patched after the fact —
   see the longer comment at the tls_connect call site below for what each
   one was and how the design here avoids it structurally. */
#define YS_TLS_MAX_CONN 4
#define YS_TLS_SLOT_SIZE 24   /* per-slot layout: [fd:8][ctx:8][ssl:8] */
#define YS_TLS_RBUF_CAP 4096

static int g_tls_table_off = -1; /* YS_TLS_MAX_CONN * YS_TLS_SLOT_SIZE bytes, zeroed */
static int g_tls_next_off  = -1; /* 8-byte round-robin handle counter, starts at 0 */
static int g_tls_rbuf_off  = -1; /* YS_TLS_RBUF_CAP-byte scratch buffer for tls_recv_print */
static int g_tls_argbuf_off = -1; /* 4*8-byte scratch used only to ferry argument
                                      values across the tls_*'s own nested-frame
                                      switch below -- see x_store_rip_slot's comment */
#define YS_TLSSRV_MAX 2
#define YS_TLSSRV_SLOT_SIZE 16 /* per-slot layout: [listen_fd:8][ctx:8] */
static int g_tlssrv_table_off = -1; /* YS_TLSSRV_MAX * YS_TLSSRV_SLOT_SIZE bytes, zeroed */
static int g_tlssrv_next_off  = -1; /* 8-byte round-robin server-handle counter */

/* Lazily reserves the rodata backing the table/counter/buffer above, the
   first time any tls_connect/tls_send/tls_recv_print/tls_close call is
   compiled. Idempotent — later calls are no-ops once the offsets are set. */
static void tls_state_ensure(void){
    if(g_tls_table_off>=0) return;
    static const uint8_t zero_table[YS_TLS_MAX_CONN*YS_TLS_SLOT_SIZE] = {0};
    g_tls_table_off = data_add_bytes(zero_table, sizeof(zero_table));
    static const uint8_t zero8[8] = {0};
    g_tls_next_off = data_add_bytes(zero8, 8);
    static const uint8_t zero_rbuf[YS_TLS_RBUF_CAP] = {0};
    g_tls_rbuf_off = data_add_bytes(zero_rbuf, YS_TLS_RBUF_CAP);
    static const uint8_t zero32[32] = {0};
    g_tls_argbuf_off = data_add_bytes(zero32, 32);
    static const uint8_t zero_srv[YS_TLSSRV_MAX*YS_TLSSRV_SLOT_SIZE] = {0};
    g_tlssrv_table_off = data_add_bytes(zero_srv, sizeof(zero_srv));
    g_tlssrv_next_off = data_add_bytes(zero8, 8);
}

/* mov [rip+off],rax / mov rax,[rip+off] — a plain rip-relative scratch
   slot, used ONLY to carry a compile_expr() result across these
   functions' own "push rbp; mov rbp,rsp; sub rsp,N" nested-frame switch.

   This exists to fix a real bug this rebuild's first pass had: each
   tls_* function opens its own frame so it can use fixed rbp-relative
   slots (see the alignment-bug comment at tls_connect below for why).
   But compile_expr() on an argument like a handle or port variable
   resolves that variable to an offset from *whichever* rbp is live at
   the moment it's called — and if it's called after this function has
   already done its own `push rbp; mov rbp,rsp`, that's *this* function's
   fresh, not-yet-written frame, not the caller's, so the variable read
   comes back as garbage from an uninitialized slot instead of the
   caller's actual value. The fix is ordering: compile_expr() every
   argument first, while the caller's rbp is still the live one, stash
   each result here (rip-relative, so it doesn't care which rbp is
   active), then open this function's own frame and load back out of
   here into its rbp-relative slots. */
static void x_store_rip_slot(int off){
    emit3(0x48,0x89,0x05); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* mov [rip+off],rax */
}
static void x_load_rip_slot(int off){
    emit3(0x48,0x8b,0x05); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* mov rax,[rip+off] */
}

/* lea r11,[rip+got_off] ; mov rax,[r11] ; call rax — the exact same
   12-byte GOT-indirect-call sequence tls_handshake_test/tls_get_test above
   hand-transcribe at every single call site. Written here exactly once so
   a transcription slip (a stray extra byte, say — the same class of bug
   that once broke the Windows PE import-call sequence, see
   add_import_call's comment) can only happen in one place instead of
   a dozen. */
static void x_call_got(int got_off){
    emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,got_off); emit_i32(0); /* lea r11,[rip+got_off] */
    emit3(0x49,0x8b,0x03); /* mov rax,[r11] */
    emit2(0xff,0xd0);      /* call rax */
}

/* mov [rax+disp8],reg / mov reg,[rax+disp8] — reads/writes one field of a
   tls-table slot once its address has been computed into rax (see
   x_tls_slot_addr below). Same plain 0..7 register encoding (rax=0 rcx=1
   rdx=2 rbx=3 rsp=4 rbp=5 rsi=6 rdi=7) as the existing rbp-relative
   helpers, just with rax as the base register and mod=01 (disp8) instead
   of rbp's fixed 0x45 encoding. */
static void x_mov_rax8_r64(int8_t disp, int reg){
    emit2(0x48,0x89); emit1((uint8_t)(0x40|(reg<<3))); emit1((uint8_t)disp);
}
static void x_mov_r64_rax8(int reg, int8_t disp){
    emit2(0x48,0x8b); emit1((uint8_t)(0x40|(reg<<3))); emit1((uint8_t)disp);
}

/* in: rax = handle (0..YS_TLS_MAX_CONN-1). out: rax = &table[handle].
   slot address = table_base + handle*24 — computed at runtime since the
   handle is a runtime value, not a compile-time constant. */
static void x_tls_slot_addr(void){
    emit3(0x48,0x69,0xc0); emit_i32(YS_TLS_SLOT_SIZE); /* imul rax,rax,24 */
    emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tls_table_off); emit_i32(0); /* lea r11,[rip+table] */
    emit3(0x4c,0x01,0xd8); /* add rax,r11 */
}

/*  symbol table  */
#define SYM_MAX 256
typedef struct { char name[64]; int code_off; int is_extern; } Symbol;
static Symbol syms[SYM_MAX];
static int    nsyms=0;

static void sym_define(const char *name, int off){
    for(int i=0;i<nsyms;i++) if(strcmp(syms[i].name,name)==0){ syms[i].code_off=off; return; }
    strncpy(syms[nsyms].name,name,63);
    syms[nsyms].code_off=off;
    syms[nsyms].is_extern=0;
    nsyms++;
}
static int sym_find(const char *name){
    for(int i=0;i<nsyms;i++) if(strcmp(syms[i].name,name)==0) return syms[i].code_off;
    return -1;
}

/*  local variable table  */
#define LOCAL_MAX 64
typedef struct { char name[64]; int rbp_off; int is_float; } Local;
static Local locals[LOCAL_MAX];
static int   nlocals=0;
static int   stack_size=0;  /* current frame size in bytes */

static void locals_clear(){ nlocals=0; stack_size=0; }

static int local_get(const char *name){
    for(int i=0;i<nlocals;i++) if(strcmp(locals[i].name,name)==0) return locals[i].rbp_off;
    return 0; /* 0 = not found */
}

static int local_alloc(const char *name){
    int existing=local_get(name);
    if(existing) return existing;
    stack_size+=8;
    locals[nlocals].rbp_off=-stack_size;
    strncpy(locals[nlocals].name,name,63);
    nlocals++;
    return -stack_size;
}

/*  call patches  */
#define CALL_PATCH_MAX 512
typedef struct { int code_off; char target[72]; } CallPatch;
static CallPatch call_patches[CALL_PATCH_MAX];
static int       ncall_patches=0;

static void add_call_patch(int off, const char *target){
    call_patches[ncall_patches].code_off=off;
    strncpy(call_patches[ncall_patches].target,target,71);
    ncall_patches++;
}

/*  target  */
/* Target typedef and ys_compile declaration moved to yolish.h */
static Target g_target=TARGET_LINUX;

/*  forward declarations  */
static void compile_node(Node *n);
static void compile_block(Node *b);

/*  x86-64 instruction helpers  */

/* push rax */
static void x_push_rax(){ emit1(0x50); }
/* pop rax */
static void x_pop_rax(){ emit1(0x58); }
/* pop rcx */
/* pop rdi */
/* pop rsi */
/* pop rdx */
/* push rbp */
static void x_push_rbp(){ emit1(0x55); }
/* pop rbp  */
static void x_pop_rbp(){ emit1(0x5d); }
/* mov rbp, rsp */
static void x_mov_rbp_rsp(){ emit3(0x48,0x89,0xe5); }
/* mov rsp, rbp */
static void x_mov_rsp_rbp(){ emit3(0x48,0x89,0xec); }
/* ret */
static void x_ret(){ emit1(0xc3); }
/* nop */

/* sub rsp, imm8 */
static void x_sub_rsp_i8(int8_t n){ emit4(0x48,0x83,0xec,(uint8_t)n); }
/* sub rsp, imm32 */

/* mov rax, imm64 */
static void x_mov_rax_imm64(int64_t v){ emit2(0x48,0xb8); emit_i64(v); }
/* mov rax, imm32 (sign-extended) */
static void x_mov_rax_imm32(int32_t v){ emit2(0x48,0xc7); emit1(0xc0); emit_i32(v); }

/* mov [rbp+off], rax */
static void x_mov_mem_rax(int off){
    if(off>=-128&&off<=127){ emit3(0x48,0x89,0x45); emit1((uint8_t)(int8_t)off); }
    else { emit3(0x48,0x89,0x85); emit_i32(off); }
}
/* mov rax, [rbp+off] */
static void x_mov_rax_mem(int off){
    if(off>=-128&&off<=127){ emit3(0x48,0x8b,0x45); emit1((uint8_t)(int8_t)off); }
    else { emit3(0x48,0x8b,0x85); emit_i32(off); }
}

/* add rax, rcx */
static void x_add_rax_rcx(){ emit3(0x48,0x01,0xc8); }
/* sub rax, rcx  (rax = rax - rcx) */
static void x_sub_rax_rcx(){ emit3(0x48,0x29,0xc8); }
/* imul rax, rcx */
static void x_imul_rax_rcx(){ emit4(0x48,0x0f,0xaf,0xc1); }
/* idiv rcx — rax = rax/rcx, rdx = rax%rcx */
static void x_idiv_setup(){
    /* cqo (sign-extend rax into rdx) */
    emit2(0x48,0x99);
    /* idiv rcx */
    emit3(0x48,0xf7,0xf9);
}

/* cmp rax, rcx */
static void x_cmp_rax_rcx(){ emit3(0x48,0x39,0xc8); }
/* test rax, rax */
static void x_test_rax_rax(){ emit3(0x48,0x85,0xc0); }

/* setCC al + movzx rax,al */
static void x_set_bool(uint8_t cc){
    emit3(0x0f,cc,0xc0);           /* setCC al */
    emit4(0x48,0x0f,0xb6,0xc0);   /* movzx rax, al */
}

/* jmp rel32 — returns patch offset */
static int x_jmp_rel32(){ emit1(0xe9); int p=code_len; emit_i32(0); return p; }
/* jz rel32 */
static int x_jz_rel32(){  emit2(0x0f,0x84); int p=code_len; emit_i32(0); return p; }
/* jnz rel32 */
static int x_jnz_rel32(){ emit2(0x0f,0x85); int p=code_len; emit_i32(0); return p; }
/* jg rel32 (signed greater-than) */
static int x_jg_rel32(){  emit2(0x0f,0x8f); int p=code_len; emit_i32(0); return p; }
/* jl rel32 (signed less-than) */
static int x_jl_rel32(){  emit2(0x0f,0x8c); int p=code_len; emit_i32(0); return p; }
/* jge rel32 (signed greater-or-equal) */
static int x_jge_rel32(){ emit2(0x0f,0x8d); int p=code_len; emit_i32(0); return p; }
/* jle rel32 (signed less-or-equal) */
static int x_jle_rel32(){ emit2(0x0f,0x8e); int p=code_len; emit_i32(0); return p; }

/* Generic helpers for "reg64 <-> [rbp+disp8]" and "mov reg64,imm32" —
   reg is the 3-bit x86 register code with NO REX extension needed:
   rax=0 rcx=1 rdx=2 rbx=3 rsp=4 rbp=5 rsi=6 rdi=7. Restricting to these
   eight keeps every encoding a plain 2-byte REX+opcode with no
   REX.R/X/B extension bits to track, which is much less error-prone
   for hand-written machine code than allowing r8-r15 here too. */
static void x_mov_r64_rbpN(int reg, int8_t disp){
    emit2(0x48,0x8b); emit1((uint8_t)(0x45|(reg<<3))); emit1((uint8_t)disp);
}
static void x_mov_rbpN_r64(int8_t disp, int reg){
    emit2(0x48,0x89); emit1((uint8_t)(0x45|(reg<<3))); emit1((uint8_t)disp);
}
static void x_lea_r64_rbpN(int reg, int8_t disp){
    emit2(0x48,0x8d); emit1((uint8_t)(0x45|(reg<<3))); emit1((uint8_t)disp);
}
static void x_mov_r64_imm32(int reg, int32_t imm){
    emit2(0x48,0xc7); emit1((uint8_t)(0xc0|reg)); emit_i32(imm);
}
static void x_mov_qword_rbpN_imm32(int8_t disp, int32_t imm){
    emit2(0x48,0xc7); emit1((uint8_t)(0x45|0)); emit1((uint8_t)disp); emit_i32(imm);
}

/* r10/r8-specific helpers — used for exactly one thing so far:
   setsockopt's raw syscall ABI, where argument 4 goes in r10 (not rcx
   — the syscall instruction clobbers rcx, so the kernel ABI uses r10
   in its place) and argument 5 goes in r8. Not folded into the
   generic reg-parameter helpers above since r8-r15 need a REX.R/B
   extension bit the 0x45|(reg<<3) trick above doesn't account for;
   easier to hand-write the two specific instructions actually needed
   than to generalize the whole helper set for registers nothing else
   here uses yet. */
static void x_lea_r10_rbpN(int8_t disp){
    emit2(0x4c,0x8d); emit1(0x55); emit1((uint8_t)disp); /* lea r10,[rbp+disp8] */
}
static void x_mov_r8d_imm32(int32_t imm){
    emit2(0x41,0xb8); emit_i32(imm); /* mov r8d,imm32 (zero-extends to r8) */
}
static void x_mov_r9d_imm32(int32_t imm){
    emit2(0x41,0xb9); emit_i32(imm); /* mov r9d,imm32 */
}
static void x_mov_r10d_imm32(int32_t imm){
    emit2(0x41,0xba); emit_i32(imm); /* mov r10d,imm32 */
}
static void x_lea_r8_rbpN32(int32_t disp){
    emit3(0x4c,0x8d,0x85); emit_i32(disp); /* lea r8,[rbp+disp32] */
}
static void x_lea_r9_rbpN32(int32_t disp){
    emit3(0x4c,0x8d,0x8d); emit_i32(disp); /* lea r9,[rbp+disp32] */
}
static void x_lea_r10_rbpN32(int32_t disp){
    emit3(0x4c,0x8d,0x95); emit_i32(disp); /* lea r10,[rbp+disp32] */
}

/* 32-bit-displacement versions of the reg<->[rbp+disp] helpers above.
   The int8_t-disp originals only reach +/-128 bytes of frame, which
   the DNS resolver's buffers (resolv.conf read buffer + UDP response
   buffer) blow through easily — these use disp32 (mod=10) unconditionally
   so any offset in a large stack frame is addressable. reg is the same
   0..7 plain-register encoding (no REX.R/X/B) as the disp8 versions. */
static void x_mov_r64_rbpN32(int reg, int32_t disp){
    emit2(0x48,0x8b); emit1((uint8_t)(0x85|(reg<<3))); emit_i32(disp);
}
static void x_mov_rbpN32_r64(int32_t disp, int reg){
    emit2(0x48,0x89); emit1((uint8_t)(0x85|(reg<<3))); emit_i32(disp);
}
static void x_lea_r64_rbpN32(int reg, int32_t disp){
    emit2(0x48,0x8d); emit1((uint8_t)(0x85|(reg<<3))); emit_i32(disp);
}
static void x_mov_qword_rbpN32_imm32(int32_t disp, int32_t imm){
    emit2(0x48,0xc7); emit1(0x85); emit_i32(disp); emit_i32(imm);
}
static void x_mov_byte_rbpN32_al(int32_t disp){
    emit1(0x88); emit1(0x85); emit_i32(disp); /* mov [rbp+disp32], al */
}

/* [rbx+idxreg] byte load/store helpers, idxreg in {0=rax,1=rcx,2=rdx}.
   Used by the DNS resolver's "nameserver " string search and IPv4
   octet-parsing loops, which repeatedly need mov al,[rbx+idx] /
   mov [rbx+idx],dl with idx varying between rax/rcx/rdx across call
   sites — hand-encoding the same SIB byte pattern each time invites
   transcription mistakes, so it's centralized here instead. */
static void x_mov_al_rbx_idx(int idxreg){
    emit1(0x8a); emit1(0x04); emit1((uint8_t)(0x03|(idxreg<<3)));
}
static void x_mov_rbx_idx_dl(int idxreg){
    emit1(0x88); emit1(0x14); emit1((uint8_t)(0x03|(idxreg<<3)));
}

/* patch jump at patch_off to jump to here */
static void x_patch_here(int patch_off){
    patch_i32(patch_off, (int32_t)(code_len - (patch_off+4)));
}

/* call rel32 */
/* call rel32 — unresolved, returns offset to patch */
static int x_call_unresolved(){
    emit1(0xe8);
    int p=code_len;
    emit_i32(0);
    return p;
}

/* lea rax, [rip + off] — for data pointer */

/*  runtime helper stubs  */
/* We need print_str and print_int as runtime helpers.
   They're emitted once at the start of the code section. */

/* Helper offsets */
static int helper_print_str_off  = -1;
static int helper_print_int_off  = -1;
static int helper_print_nl_off   = -1;
static int helper_exit_off       = -1;
static int helper_print_float_off = -1;

/* v1.1: per-local float tracking */
static int g_last_float = 0;

/* SSE2 helpers */
static void x_movq_xmm0_rax(){ emit4(0x66,0x48,0x0f,0x6e); emit1(0xc0); }
static void x_movq_xmm1_rcx(){ emit4(0x66,0x48,0x0f,0x6e); emit1(0xc9); }
static void x_movq_rax_xmm0(){ emit4(0x66,0x48,0x0f,0x7e); emit1(0xc0); }
static void x_addsd(){ emit4(0xf2,0x0f,0x58,0xc1); }
static void x_subsd(){ emit4(0xf2,0x0f,0x5c,0xc1); }
static void x_mulsd(){ emit4(0xf2,0x0f,0x59,0xc1); }
static void x_divsd(){ emit4(0xf2,0x0f,0x5e,0xc1); }
static void x_ucomisd(){ emit4(0x66,0x0f,0x2e,0xc1); }
static void x_cvtsi2sd_xmm0_rax(){ emit4(0xf2,0x48,0x0f,0x2a); emit1(0xc0); }
static void x_cvtsi2sd_xmm1_rcx(){ emit4(0xf2,0x48,0x0f,0x2a); emit1(0xc9); }
static void x_cvtsi2sd_xmm1_rbx(){ emit4(0xf2,0x48,0x0f,0x2a); emit1(0xcb); }
static void x_cvttsd2si_rax_xmm0(){ emit4(0xf2,0x48,0x0f,0x2c); emit1(0xc0); }
static void x_cvttsd2si_rbx_xmm0(){ emit4(0xf2,0x48,0x0f,0x2c); emit1(0xd8); }


/* SYS_write on Linux = 1, macOS = 0x2000004 */
/* SYS_exit  on Linux = 60, macOS = 0x2000001 */

/*  helper emitters  */
/* Reset and use a clean approach */

static void emit_float_helper(void){
    int sn=(g_target==TARGET_LINUX)?1:0x2000004;
    helper_print_float_off=code_len;
    sym_define("__ys_print_float",code_len);
    x_push_rbp(); x_mov_rbp_rsp();
    emit1(0x53); emit4(0x48,0x83,0xec,0x28);
    /* movq xmm0,rdi (load double bits from rdi) */
    emit4(0x66,0x48,0x0f,0x6e); emit1(0xc7);
    /* save xmm0 → [rbp-16] */
    emit4(0xf2,0x0f,0x11,0x45); emit1(0xf0);
    /* check sign */
    x_movq_rax_xmm0();
    emit3(0x48,0xc1,0xe8); emit1(0x3f);
    x_test_rax_rax();
    int jns=code_len; emit2(0x74,0x00);
    /* print '-' */
    emit4(0x48,0x83,0xec,0x08); emit4(0xc6,0x04,0x24,0x2d);
    emit3(0x48,0x89,0xe6); x_mov_rax_imm32(1); emit3(0x48,0x89,0xc2);
    x_mov_rax_imm32(1); emit3(0x48,0x89,0xc7);
    x_mov_rax_imm32(sn); emit2(0x0f,0x05);
    emit4(0x48,0x83,0xc4,0x08);
    /* flip sign bit */
    emit4(0xf2,0x0f,0x10,0x45); emit1(0xf0);
    x_movq_rax_xmm0();
    emit2(0x48,0xb9); emit_i64((int64_t)((uint64_t)1<<63));
    emit3(0x48,0x31,0xc8); x_movq_xmm0_rax();
    emit4(0xf2,0x0f,0x11,0x45); emit1(0xf0);
    code_buf[jns+1]=(uint8_t)(code_len-(jns+2));
    /* integer part */
    emit4(0xf2,0x0f,0x10,0x45); emit1(0xf0);
    x_cvttsd2si_rbx_xmm0();
    emit3(0x48,0x89,0xdf); int pi=x_call_unresolved(); add_call_patch(pi,"__ys_print_int");
    /* print '.' */
    emit4(0x48,0x83,0xec,0x08); emit4(0xc6,0x04,0x24,0x2e);
    emit3(0x48,0x89,0xe6); x_mov_rax_imm32(1); emit3(0x48,0x89,0xc2);
    x_mov_rax_imm32(1); emit3(0x48,0x89,0xc7);
    x_mov_rax_imm32(sn); emit2(0x0f,0x05);
    emit4(0x48,0x83,0xc4,0x08);
    /* frac = xmm0 - float(rbx) */
    emit4(0xf2,0x0f,0x10,0x45); emit1(0xf0);
    x_cvtsi2sd_xmm1_rbx(); x_subsd();
    /* *1e6 */
    int c1e6=data_len;
    { double v=1000000.0; int64_t b; memcpy(&b,&v,8);
      for(int i=0;i<8;i++) data_buf[data_len++]=(uint8_t)(b>>(i*8)); }
    emit4(0xf2,0x0f,0x10,0x0d);
    add_reloc(RELOC_DATA,code_len,c1e6); emit_i32(0);
    x_mulsd(); x_cvttsd2si_rax_xmm0();
    emit3(0x48,0x89,0x45); emit1(0xf8);
    x_mov_rax_imm32(10); emit3(0x48,0x89,0xc3);
    emit3(0x48,0x8b,0x45); emit1(0xf8);
    { int8_t doff[6]={-6,-5,-4,-3,-2,-1};
      for(int di=0;di<6;di++){
        emit3(0x48,0x31,0xd2); emit3(0x48,0xf7,0xf3);
        emit3(0x80,0xc2,0x30);
        emit3(0x88,0x55,(uint8_t)(int8_t)doff[5-di]);
      }
    }
    emit3(0x48,0x8d,0x75); emit1(0xfa);
    x_mov_rax_imm32(6); emit3(0x48,0x89,0xc2);
    x_mov_rax_imm32(1); emit3(0x48,0x89,0xc7);
    x_mov_rax_imm32(sn); emit2(0x0f,0x05);
    emit4(0x48,0x83,0xc4,0x28); emit1(0x5b);
    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
}


static void emit_helpers(void){
    /*  print_str(rdi=buf, rsi=len)  */
    /* SysV ABI: rdi=buf ptr, rsi=len */
    /* Linux/macOS syscall write(fd=1, buf, len): rax=nr, rdi=fd, rsi=buf, rdx=len */
    helper_print_str_off=code_len;
    sym_define("__ys_print_str",code_len);
    if(g_target==TARGET_LINUX||g_target==TARGET_MACOS){
        int sn=(g_target==TARGET_LINUX)?1:0x2000004;
        /* on entry: rdi=buf, rsi=len */
        emit3(0x48,0x89,0xf2); /* mov rdx, rsi  (len → rdx) */
        emit3(0x48,0x89,0xfe); /* mov rsi, rdi  (buf → rsi) */
        x_mov_rax_imm32(1);
        emit3(0x48,0x89,0xc7); /* mov rdi, rax  (1 → rdi = stdout fd) */
        x_mov_rax_imm32(sn);   /* rax = SYS_write */
        emit2(0x0f,0x05);      /* syscall */
        x_ret();
    } else {
        x_ret();
    }

    /*  print_int(rdi=val)  */
    /* Converts int64 to decimal string and writes to stdout */
    helper_print_int_off=code_len;
    sym_define("__ys_print_int",code_len);
    {
        /* push rbx (callee-saved), allocate 24-byte scratch on stack */
        emit1(0x53);             /* push rbx */
        x_sub_rsp_i8(24);        /* sub rsp, 24  (scratch buffer) */
        emit3(0x48,0x89,0xf8);   /* mov rax, rdi  (value) */

        /* handle sign: if rax < 0, write '-' and negate */
        x_test_rax_rax();
        int jns_off=code_len; emit2(0x79,0x00); /* jns +?? */
        /* negative: emit '-' to buf[23] conceptually; instead write sign separately */
        emit3(0x48,0xf7,0xd8);   /* neg rax */
        /* write '-' via syscall inline (1 byte) */
        emit4(0x48,0x83,0xec,0x08); /* sub rsp,8 (align + scratch for '-') */
        /* actually simpler: store '-' on stack */
        emit4(0xc6,0x04,0x24,0x2d); /* mov byte[rsp],'-' */
        /* write(1, rsp, 1) */
        int sn=(g_target==TARGET_LINUX)?1:0x2000004;
        emit3(0x48,0x89,0xe6);   /* mov rsi,rsp */
        emit3(0x48,0xc7,0xc2); emit_i32(1); /* mov rdx,1 */
        x_mov_rax_imm32(1);
        emit3(0x48,0x89,0xc7);   /* mov rdi,1 */
        x_mov_rax_imm32(sn); emit2(0x0f,0x05);
        emit4(0x48,0x83,0xc4,0x08); /* add rsp,8 */
        /* patch jns */
        code_buf[jns_off+1]=(uint8_t)(code_len-(jns_off+2));

        /* digit extraction: rax=value, rbx=digit_count */
        emit3(0x48,0x31,0xdb);   /* xor rbx,rbx */
        int loop_start=code_len;
        emit3(0x48,0x31,0xd2);   /* xor rdx,rdx */
        emit2(0x48,0xb9); emit_i64(10);  /* mov rcx,10 */
        emit3(0x48,0xf7,0xf9);   /* div rcx  → rax=quot, rdx=rem */
        emit3(0x80,0xc2,0x30);   /* add dl,'0' */
        emit3(0x88,0x14,0x1c);   /* mov [rsp+rbx], dl */
        emit3(0x48,0xff,0xc3);   /* inc rbx */
        x_test_rax_rax();
        int jnz_off=code_len; emit2(0x75,0x00); /* jnz loop */
        code_buf[jnz_off+1]=(uint8_t)(loop_start-(jnz_off+2));

        /* reverse digits in [rsp..rsp+rbx-1] */
        emit3(0x48,0x31,0xf6);   /* xor rsi,rsi  (left=0) */
        emit4(0x48,0x8d,0x4b,0xff); /* lea rcx,[rbx-1]  (right) — 4 bytes */
        int rev_start=code_len;
        emit3(0x48,0x39,0xce);   /* cmp rsi,rcx */
        int rev_done=code_len; emit2(0x7d,0x00); /* jge done */
        emit3(0x8a,0x04,0x34);   /* mov al,[rsp+rsi] */
        emit3(0x8a,0x14,0x0c);   /* mov dl,[rsp+rcx] */
        emit3(0x88,0x14,0x34);   /* mov [rsp+rsi],dl */
        emit3(0x88,0x04,0x0c);   /* mov [rsp+rcx],al */
        emit3(0x48,0xff,0xc6);   /* inc rsi */
        emit3(0x48,0xff,0xc9);   /* dec rcx */
        emit2(0xeb,0x00);        /* jmp rev_start */
        code_buf[code_len-1]=(uint8_t)(rev_start-(code_len));
        code_buf[rev_done+1]=(uint8_t)(code_len-(rev_done+2));

        /* write(1, rsp, rbx) */
        int sn2=(g_target==TARGET_LINUX)?1:0x2000004;
        emit3(0x48,0x89,0xe6);   /* mov rsi,rsp */
        emit3(0x48,0x89,0xda);   /* mov rdx,rbx */
        x_mov_rax_imm32(1); emit3(0x48,0x89,0xc7);
        x_mov_rax_imm32(sn2); emit2(0x0f,0x05);

        emit4(0x48,0x83,0xc4,0x18); /* add rsp,24 */
        emit1(0x5b);             /* pop rbx */
        x_ret();
    }

        /*  print_nl()  */
    helper_print_nl_off=code_len;
    sym_define("__ys_print_nl",code_len);
    {
        int nl_data=data_len; data_buf[data_len++]='\n';
        x_push_rbp(); x_mov_rbp_rsp();
        int sn=(g_target==TARGET_LINUX)?1:0x2000004;
        emit3(0x48,0x8d,0x35); /* lea rsi,[rip+rel] */
        add_reloc(RELOC_DATA,code_len,nl_data); emit_i32(0);
        x_mov_rax_imm32(1); emit3(0x48,0x89,0xc7);
        x_mov_rax_imm32(1); emit3(0x48,0x89,0xc2);
        x_mov_rax_imm32(sn); emit2(0x0f,0x05);
        x_pop_rbp(); x_ret();
    }

    /*  exit(rdi=code)  */
    helper_exit_off=code_len;
    sym_define("__ys_exit",code_len);
    {
        int sn=(g_target==TARGET_LINUX)?60:0x2000001;
        x_mov_rax_imm32(sn);
        emit2(0x0f,0x05);
        x_ret();
    }

    /* ---- native TCP networking (Linux only — raw syscalls, no libc) ----
       Both dotted-decimal IPv4 literals ("93.184.216.34") and hostname
       literals ("example.com") are supported: __ys_net_connect handles
       the former (parses the octets directly, no network round trip),
       __ys_net_connect_host below handles the latter via a hand-written
       UDP DNS client (v2.22 — no libc, no getaddrinfo, this backend
       links nothing). The call site (search for "y.net.connect(ip_or_host"
       further down) decides which one to emit based on the literal's
       shape at compile time. See ROADMAP.md's v2.22 entry for how DNS
       resolution works and what it was verified against.
       macOS uses entirely different syscall numbers/ABI and isn't
       covered here either; calling y.net.* when compiling for macOS
       hits the "unresolved symbol" safety net, same as before. */
    if(g_target==TARGET_LINUX){
        /* __ys_net_connect(rdi=ip_str, rsi=ip_str_len, rdx=port) -> rax=fd or -1
           Stack layout (all offsets from rbp):
             -8  current char pointer (starts at ip_str, incremented per byte)
             -16 end pointer (ip_str + ip_str_len)
             -24 port
             -32 octet accumulator
             -40 octet_idx (0..3)
             -48..-45 parsed IPv4 bytes
             -56 fd (once socket() succeeds)
             -96..-81 struct sockaddr_in (16 bytes) */
        sym_define("__ys_net_connect",code_len);
        x_push_rbp(); x_mov_rbp_rsp();
        emit3(0x48,0x81,0xec); emit_i32(112); /* sub rsp,112 */

        x_mov_rbpN_r64(-8,7);  /* [rbp-8]=rdi (str ptr) */
        x_mov_r64_rbpN(0,-8);  /* rax = str ptr */
        emit3(0x48,0x01,0xf0); /* add rax,rsi -> end ptr */
        x_mov_rbpN_r64(-16,0); /* [rbp-16] = end ptr */
        x_mov_rbpN_r64(-24,2); /* [rbp-24] = rdx (port) */
        x_mov_qword_rbpN_imm32(-32,0); /* octet=0 */
        x_mov_qword_rbpN_imm32(-40,0); /* octet_idx=0 */

        int loop_start=code_len;
        x_mov_r64_rbpN(0,-8);  /* rax = cur ptr */
        x_mov_r64_rbpN(1,-16); /* rcx = end ptr */
        emit3(0x48,0x39,0xc8); /* cmp rax,rcx */
        int j_loop_end=x_jge_rel32();

        x_mov_r64_rbpN(2,-8);         /* rdx = cur ptr */
        emit3(0x0f,0xb6,0x02);        /* movzx eax, byte [rdx] */
        emit2(0x3c,0x2e);             /* cmp al, '.' */
        int j_digit=x_jnz_rel32();

        /* dot case */
        x_mov_r64_rbpN(1,-40);        /* rcx = octet_idx */
        x_mov_r64_rbpN(2,-32);        /* rdx = octet */
        x_lea_r64_rbpN(3,-48);        /* rbx = &ipbuf[0] */
        emit3(0x88,0x14,0x0b);        /* mov [rbx+rcx], dl */
        emit3(0x48,0xff,0x45); emit1((uint8_t)-40); /* inc qword [rbp-40] */
        x_mov_qword_rbpN_imm32(-32,0);/* octet=0 */
        int j_next1=x_jmp_rel32();

        x_patch_here(j_digit);
        emit2(0x2c,0x30);             /* sub al,'0' */
        emit3(0x0f,0xb6,0xc0);        /* movzx eax,al */
        x_mov_r64_rbpN(2,-32);        /* rdx = octet */
        emit4(0x48,0x6b,0xd2,0x0a);   /* imul rdx,rdx,10 */
        emit3(0x48,0x01,0xc2);        /* add rdx,rax */
        x_mov_rbpN_r64(-32,2);        /* octet = rdx */

        x_patch_here(j_next1);
        emit3(0x48,0xff,0x45); emit1((uint8_t)-8); /* inc qword [rbp-8] (cur ptr) */
        int j_back=x_jmp_rel32();
        patch_i32(j_back,(int32_t)(loop_start-(j_back+4)));

        x_patch_here(j_loop_end);
        /* final octet */
        x_mov_r64_rbpN(1,-40);
        x_mov_r64_rbpN(2,-32);
        x_lea_r64_rbpN(3,-48);
        emit3(0x88,0x14,0x0b);

        /* build sockaddr_in at [rbp-96] */
        x_lea_r64_rbpN(3,-96);              /* rbx = &sockaddr */
        emit4(0x66,0xc7,0x03,0x02); emit1(0x00); /* mov word [rbx],2 (AF_INET) */
        x_mov_r64_rbpN(0,-24);               /* rax = port */
        emit2(0x86,0xc4);                    /* xchg al,ah (htons) */
        emit4(0x66,0x89,0x43,0x02);          /* mov word [rbx+2],ax */
        x_lea_r64_rbpN(1,-48);                /* rcx = &ipbuf */
        emit2(0x8b,0x01);                     /* mov eax,[rcx] */
        emit3(0x89,0x43,0x04);                /* mov [rbx+4],eax */
        emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0); /* mov qword [rbx+8],0 */

        /* socket(AF_INET=2, SOCK_STREAM=1, 0) */
        x_mov_r64_imm32(7,2);  /* rdi=2 */
        x_mov_r64_imm32(6,1);  /* rsi=1 */
        x_mov_r64_imm32(2,0);  /* rdx=0 */
        x_mov_r64_imm32(0,41); /* rax=SYS_socket */
        emit2(0x0f,0x05);
        emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
        int j_fail1=x_jl_rel32();
        x_mov_rbpN_r64(-56,0); /* [rbp-56]=fd */

        /* connect(fd,&sockaddr,16) */
        x_mov_r64_rbpN(7,-56);      /* rdi=fd */
        x_lea_r64_rbpN(6,-96);      /* rsi=&sockaddr */
        x_mov_r64_imm32(2,16);      /* rdx=16 */
        x_mov_r64_imm32(0,42);      /* rax=SYS_connect */
        emit2(0x0f,0x05);
        emit4(0x48,0x83,0xf8,0x00);
        int j_fail2=x_jl_rel32();

        x_mov_r64_rbpN(0,-56); /* rax=fd (return value) */
        int j_done1=x_jmp_rel32();

        x_patch_here(j_fail2);
        x_mov_r64_rbpN(7,-56);
        x_mov_r64_imm32(0,3); /* SYS_close */
        emit2(0x0f,0x05);
        x_mov_r64_imm32(0,-1);
        int j_done2=x_jmp_rel32();

        x_patch_here(j_fail1);
        x_mov_r64_imm32(0,-1);

        x_patch_here(j_done1);
        x_patch_here(j_done2);
        x_mov_rsp_rbp(); x_pop_rbp(); x_ret();

        /* __ys_net_connect6(rdi=ipv6_16bytes_ptr, rsi=port) -> rax=fd or -1
           Direct IPv6-literal counterpart to __ys_net_connect above.
           No DNS involved at all here -- the 16 address bytes are
           already sitting in rodata, parsed at compile time by this
           compiler's own portable parser (see parse_ipv6_literal),
           since an IPv6 literal in y.net.connect's argument is just as
           much a compile-time-only string as the IPv4 case always was.
           This function only has to build a 28-byte sockaddr_in6
           (family, port, 4 bytes of flowinfo, the 16 address bytes, 4
           bytes of scope_id) and connect() with AF_INET6/SOCK_STREAM
           instead of AF_INET.

           Stack layout (all offsets from rbp):
             -8  addr_ptr (16 raw IPv6 bytes, in rodata)
             -16 port
             -24 fd (once socket() succeeds)
             -56..-29 struct sockaddr_in6 (28 bytes) */
        sym_define("__ys_net_connect6",code_len);
        x_push_rbp(); x_mov_rbp_rsp();
        emit3(0x48,0x81,0xec); emit_i32(64); /* sub rsp,64 */

        x_mov_rbpN_r64(-8,7);   /* addr_ptr = rdi */
        x_mov_rbpN_r64(-16,6);  /* port = rsi */

        x_lea_r64_rbpN(3,-56);              /* rbx = &sockaddr_in6 */
        emit4(0x66,0xc7,0x03,0x0a); emit1(0x00); /* mov word[rbx],10 (AF_INET6) */
        x_mov_r64_rbpN(0,-16);               /* rax = port */
        emit2(0x86,0xc4);                    /* xchg al,ah (htons) */
        emit4(0x66,0x89,0x43,0x02);          /* mov word[rbx+2],ax */
        emit1(0xc7); emit1(0x43); emit1(0x04); emit_i32(0); /* mov dword[rbx+4],0 (flowinfo) */

        x_mov_r64_rbpN(1,-8);                /* rcx = addr_ptr */
        emit3(0x48,0x8b,0x01);               /* mov rax,[rcx] (address bytes 0-7) */
        emit3(0x48,0x89,0x43); emit1(0x08);  /* mov [rbx+8],rax */
        emit4(0x48,0x8b,0x41,0x08);          /* mov rax,[rcx+8] (address bytes 8-15) */
        emit3(0x48,0x89,0x43); emit1(0x10);  /* mov [rbx+16],rax */
        emit1(0xc7); emit1(0x43); emit1(0x18); emit_i32(0); /* mov dword[rbx+24],0 (scope_id) */

        /* socket(AF_INET6=10, SOCK_STREAM=1, 0) */
        x_mov_r64_imm32(7,10);
        x_mov_r64_imm32(6,1);
        x_mov_r64_imm32(2,0);
        x_mov_r64_imm32(0,41); /* SYS_socket */
        emit2(0x0f,0x05);
        emit4(0x48,0x83,0xf8,0x00);
        int j6_fail1=x_jl_rel32();
        x_mov_rbpN_r64(-24,0); /* fd */

        /* connect(fd, &sockaddr_in6, 28) */
        x_mov_r64_rbpN(7,-24);
        x_lea_r64_rbpN(6,-56);
        x_mov_r64_imm32(2,28);
        x_mov_r64_imm32(0,42); /* SYS_connect */
        emit2(0x0f,0x05);
        emit4(0x48,0x83,0xf8,0x00);
        int j6_fail2=x_jl_rel32();

        x_mov_r64_rbpN(0,-24); /* rax=fd (return value) */
        int j6_done1=x_jmp_rel32();

        x_patch_here(j6_fail2);
        x_mov_r64_rbpN(7,-24);
        x_mov_r64_imm32(0,3); /* SYS_close */
        emit2(0x0f,0x05);
        x_mov_r64_imm32(0,-1);
        int j6_done2=x_jmp_rel32();

        x_patch_here(j6_fail1);
        x_mov_r64_imm32(0,-1);

        x_patch_here(j6_done1);
        x_patch_here(j6_done2);
        x_mov_rsp_rbp(); x_pop_rbp(); x_ret();

        /* __ys_net_connect_host(rdi=dns_query_ptr, rsi=dns_query_len, rdx=port)
           -> rax=fd or -1
           Companion to __ys_net_connect above, for when y.net.connect's
           address argument is a hostname literal rather than a dotted-
           decimal IP. The DNS query packet itself is built at *compile
           time* (see build_dns_query() — safe because the hostname, like
           the IP case, must already be a compile-time string literal) and
           handed in here as ready-to-send bytes; this function only does
           the runtime work: find a resolver, send the query over UDP,
           parse the A record out of the response, then connect() exactly
           like __ys_net_connect does once it has 4 IP bytes.

           Stack layout (all offsets from rbp, frame = 960 bytes):
             -8    dns_query_ptr (arg)
             -16   dns_query_len (arg)
             -24   port (arg)
             -32   resolver_ip[4]      (defaults to 8.8.8.8, overwritten
                                         if /etc/resolv.conf yields one)
             -40   udp_fd
             -48   resolv.conf fd
             -56   resolv.conf bytes_read
             -64   DNS response recv_len
             -72   pos (parse cursor into resp_buf; reused earlier as an
                        IPv4-octet accumulator while parsing resolv.conf,
                        since that happens before pos's real use begins)
             -80   ancount (reused earlier as octet_idx, same reasoning)
             -88   loop_i (search index / answer-record index, reused)
             -96   unused (previously found_flag; connect attempts now
                          happen inline per answer record — see the
                          answer-record loop below — so no separate
                          found/not-found flag is needed any more)
             -104  found_ip[4] (current answer record's IP, reused per attempt)
             -112  tcp_fd
             -128  timeval.tv_sec (SO_RCVTIMEO, 3s)
             -120  timeval.tv_usec
             -144  sockaddr_in for the resolver (16 bytes)
             -160  sockaddr_in for the TCP target (16 bytes)
             -168  rr_type   (scratch, current answer record's TYPE)
             -176  rr_rdlen  (scratch, current answer record's RDLENGTH)
             -432  resolv_buf[256]     (base; buf[0] at rbp-432)
             -944  resp_buf[512]       (base; buf[0] at rbp-944)

           Every rbp-relative access here uses the 32-bit-displacement
           helpers (x_*_rbpN32) rather than the int8-disp ones used
           elsewhere in this file, since this frame is far larger than
           the +/-128 byte range those support. */
        sym_define("__ys_net_connect_host",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(960); /* sub rsp,960 */

            x_mov_rbpN32_r64(-8,7);   /* [rbp-8]  = rdi (query ptr) */
            x_mov_rbpN32_r64(-16,6);  /* [rbp-16] = rsi (query len) */
            x_mov_rbpN32_r64(-24,2);  /* [rbp-24] = rdx (port) */

            /* resolver_ip defaults to 8.8.8.8 */
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-32);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-31);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-30);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-29);

            /* ---- try /etc/resolv.conf for a better resolver ---- */
            int resolv_path_off = data_add_str("/etc/resolv.conf");
            x_lea_arg1_data(resolv_path_off); /* rdi = &path (Linux-only fn, arg1=rdi) */
            x_mov_r64_imm32(6,0);             /* rsi = O_RDONLY */
            x_mov_r64_imm32(2,0);             /* rdx = mode */
            x_mov_r64_imm32(0,2);             /* rax = SYS_open */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);       /* cmp rax,0 */
            int j_conf_open_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-48,0);          /* conf_fd = rax */

            x_mov_r64_rbpN32(7,-48);          /* rdi = conf_fd */
            x_lea_r64_rbpN32(6,-432);         /* rsi = &resolv_buf */
            x_mov_r64_imm32(2,255);           /* rdx = 255 */
            x_mov_r64_imm32(0,0);             /* rax = SYS_read */
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-56,0);          /* bytes_read = rax */

            x_mov_r64_rbpN32(7,-48);          /* rdi = conf_fd */
            x_mov_r64_imm32(0,3);             /* rax = SYS_close */
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-56);
            emit4(0x48,0x83,0xf8,0x00);       /* cmp rax,0 */
            int j_bytesread_le0 = x_jle_rel32();

            /* search resolv_buf for "nameserver " (11 bytes) */
            x_mov_qword_rbpN32_imm32(-88,0);  /* loop_i = 0 */
            int search_loop = code_len;
            x_mov_r64_rbpN32(0,-88);          /* rax = i */
            x_mov_r64_rbpN32(1,-56);          /* rcx = bytes_read */
            emit4(0x48,0x83,0xe9,0x0b);       /* sub rcx,11 */
            emit3(0x48,0x39,0xc8);            /* cmp rax,rcx */
            int j_search_end = x_jge_rel32(); /* i >= bytes_read-11 -> not found */

            x_lea_r64_rbpN32(3,-432);         /* rbx = &resolv_buf */
            x_mov_r64_rbpN32(0,-88);          /* rax = i */
            emit3(0x48,0x01,0xc3);            /* add rbx,rax -> &resolv_buf[i] */

            int needle_off = data_add_str("nameserver ");
            emit3(0x48,0x8d,0x35);            /* lea rsi,[rip+needle] */
            add_reloc(RELOC_DATA,code_len,needle_off); emit_i32(0);

            x_mov_r64_imm32(1,0);             /* rcx = j = 0 */
            int cmp_loop = code_len;
            emit4(0x48,0x83,0xf9,0x0b);       /* cmp rcx,11 */
            int j_cmp_done = x_jge_rel32();   /* j>=11 -> full match */
            x_mov_al_rbx_idx(1);              /* al = [rbx+rcx] */
            emit3(0x8a,0x14,0x0e);            /* dl = [rsi+rcx] */
            emit2(0x38,0xd0);                 /* cmp al,dl */
            int j_byte_ne = x_jnz_rel32();
            emit3(0x48,0xff,0xc1);            /* inc rcx */
            int j_cmp_back = x_jmp_rel32();
            patch_i32(j_cmp_back,(int32_t)(cmp_loop-(j_cmp_back+4)));

            x_patch_here(j_byte_ne);          /* no_match: */
            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);            /* inc rax */
            x_mov_rbpN32_r64(-88,0);
            int j_search_back = x_jmp_rel32();
            patch_i32(j_search_back,(int32_t)(search_loop-(j_search_back+4)));

            x_patch_here(j_cmp_done);         /* match_found: rbx = &resolv_buf[i] */
            emit3(0x48,0x8d,0x4b); emit1(0x0b); /* rcx = rbx+11 (first char after "nameserver ") */

            x_mov_qword_rbpN32_imm32(-72,0);  /* octet accumulator (reusing pos slot) */
            x_mov_qword_rbpN32_imm32(-80,0);  /* octet_idx (reusing ancount slot) */

            int parse_ip_loop = code_len;
            /* end pointer is recomputed fresh every iteration rather than
               held in rdx across the whole loop -- both branches below
               reuse rdx as scratch for the accumulator, so a value that
               had to survive the loop body in rdx would get clobbered
               after the first character (this was a real bug: the octet
               parser silently never stored anything as a result, since
               the loop's bounds check would compare rcx against whatever
               accumulator value rdx last held instead of the real end
               pointer, and exit after one character every time). */
            x_lea_r64_rbpN32(2,-432);         /* rdx = &resolv_buf */
            x_mov_r64_rbpN32(0,-56);          /* rax = bytes_read */
            emit3(0x48,0x01,0xc2);            /* rdx += rax -> end pointer */
            emit3(0x48,0x39,0xd1);            /* cmp rcx,rdx */
            int j_parse_ip_ge = x_jge_rel32();
            emit3(0x0f,0xb6,0x01);            /* movzx eax,byte[rcx] */
            emit2(0x3c,0x2e);                 /* cmp al,'.' */
            int j_not_dot = x_jnz_rel32();

            x_mov_r64_rbpN32(0,-80);          /* rax = octet_idx */
            x_lea_r64_rbpN32(3,-32);          /* rbx = &resolver_ip */
            x_mov_r64_rbpN32(2,-72);          /* rdx = accumulator */
            x_mov_rbx_idx_dl(0);              /* [rbx+rax] = dl */
            x_mov_r64_rbpN32(0,-80);
            emit3(0x48,0xff,0xc0);            /* inc rax */
            x_mov_rbpN32_r64(-80,0);          /* octet_idx++ */
            x_mov_qword_rbpN32_imm32(-72,0);  /* accumulator = 0 */
            int j_to_next1 = x_jmp_rel32();

            x_patch_here(j_not_dot);
            emit2(0x3c,0x30);                 /* cmp al,'0' */
            int j_lt0 = x_jl_rel32();
            emit2(0x3c,0x39);                 /* cmp al,'9' */
            int j_gt9 = x_jg_rel32();
            emit2(0x2c,0x30);                 /* sub al,'0' */
            emit3(0x0f,0xb6,0xc0);            /* movzx eax,al */
            x_mov_r64_rbpN32(2,-72);          /* rdx = accumulator */
            emit4(0x48,0x6b,0xd2,0x0a);       /* imul rdx,rdx,10 */
            emit3(0x48,0x01,0xc2);            /* add rdx,rax */
            x_mov_rbpN32_r64(-72,2);          /* accumulator = rdx */

            x_patch_here(j_to_next1);
            emit3(0x48,0xff,0xc1);            /* inc rcx */
            int j_parse_back = x_jmp_rel32();
            patch_i32(j_parse_back,(int32_t)(parse_ip_loop-(j_parse_back+4)));

            x_patch_here(j_parse_ip_ge);
            x_patch_here(j_lt0);
            x_patch_here(j_gt9);

            x_mov_r64_rbpN32(0,-80);          /* rax = octet_idx */
            emit4(0x48,0x83,0xf8,0x03);       /* cmp rax,3 */
            int j_bad_octetcount = x_jnz_rel32(); /* not exactly 3 dots -> malformed, keep 8.8.8.8 */
            x_lea_r64_rbpN32(3,-32);          /* rbx = &resolver_ip */
            x_mov_r64_rbpN32(2,-72);          /* rdx = accumulator (4th octet) */
            x_mov_rbx_idx_dl(0);              /* [rbx+rax] = dl  (rax still = 3) */
            x_patch_here(j_bad_octetcount);

            /* skip_resolv_conf: resolver_ip is now set (parsed, or 8.8.8.8) */
            int skip_resolv_conf = code_len;
            x_patch_here(j_conf_open_fail);
            x_patch_here(j_bytesread_le0);
            x_patch_here(j_search_end);
            (void)skip_resolv_conf;

            /* ---- UDP socket ---- */
            x_mov_r64_imm32(7,2);  /* AF_INET */
            x_mov_r64_imm32(6,2);  /* SOCK_DGRAM */
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_udp_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-40,0); /* udp_fd = rax */

            /* SO_RCVTIMEO = 3s, so a dropped/unanswered query can't hang forever */
            x_mov_qword_rbpN32_imm32(-128,3); /* tv_sec */
            x_mov_qword_rbpN32_imm32(-120,0); /* tv_usec */
            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(6,1);   /* SOL_SOCKET */
            x_mov_r64_imm32(2,20);  /* SO_RCVTIMEO */
            x_lea_r10_rbpN32(-128);
            x_mov_r8d_imm32(16);
            x_mov_r64_imm32(0,54);  /* SYS_setsockopt */
            emit2(0x0f,0x05);       /* ignore result — best-effort */

            /* sockaddr_in for resolver, port 53 */
            x_lea_r64_rbpN32(3,-144);
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00); /* AF_INET */
            x_mov_r64_imm32(0,53);
            emit2(0x86,0xc4);                        /* xchg al,ah (htons) */
            emit4(0x66,0x89,0x43,0x02);
            x_mov_r64_rbpN32(0,-32);                  /* eax = resolver_ip (low 4 bytes) */
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            /* sendto(udp_fd, query_ptr, query_len, 0, &sockaddr_resolver, 16) */
            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_rbpN32(6,-8);
            x_mov_r64_rbpN32(2,-16);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-144);
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44); /* SYS_sendto */
            emit2(0x0f,0x05);

            /* recvfrom(udp_fd, resp_buf, 512, 0, NULL, NULL) */
            x_mov_r64_rbpN32(7,-40);
            x_lea_r64_rbpN32(6,-944);
            x_mov_r64_imm32(2,512);
            x_mov_r10d_imm32(0);
            x_mov_r8d_imm32(0);
            x_mov_r9d_imm32(0);
            x_mov_r64_imm32(0,45); /* SYS_recvfrom */
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-64,0); /* recv_len = rax */

            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(0,3);  /* SYS_close (udp) */
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-64);
            emit4(0x48,0x83,0xf8,0x00);
            int j_recv_fail = x_jle_rel32();

            /* ---- parse DNS response: skip header(12) + QNAME + QTYPE/QCLASS ---- */
            x_mov_qword_rbpN32_imm32(-72,12); /* pos = 12 */
            int qname_loop = code_len;
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);            /* cmp pos,recv_len */
            int j_qname_oob = x_jge_rel32();
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(0,-72);
            x_mov_al_rbx_idx(0);              /* al = resp_buf[pos] */
            emit2(0x84,0xc0);                 /* test al,al */
            int j_qname_end = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);            /* movzx eax,al */
            emit3(0x48,0xff,0xc0);            /* inc rax (label_len+1) */
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);            /* rax += pos */
            x_mov_rbpN32_r64(-72,0);          /* pos = rax */
            int j_qname_back = x_jmp_rel32();
            patch_i32(j_qname_back,(int32_t)(qname_loop-(j_qname_back+4)));

            x_patch_here(j_qname_oob);
            x_patch_here(j_qname_end);
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x05);       /* pos += 1(terminator)+4(qtype+qclass) */
            x_mov_rbpN32_r64(-72,0);

            /* ancount = (resp[6]<<8)|resp[7] */
            x_lea_r64_rbpN32(3,-944);
            emit3(0x0f,0xb6,0x43); emit1(0x06);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-80,0);
            emit3(0x0f,0xb6,0x43); emit1(0x07);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-80,0);          /* ancount = full */

            x_mov_qword_rbpN32_imm32(-88,0);  /* loop_i = 0 */

            int rr_loop = code_len;
            x_mov_r64_rbpN32(0,-88);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x39,0xc8);            /* cmp loop_i,ancount */
            int j_rr_done1 = x_jge_rel32();
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);            /* cmp pos,recv_len (safety) */
            int j_rr_done2 = x_jge_rel32();

            /* NAME: compression pointer (0xC0 top bits) -> pos+=2; else walk labels */
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);              /* al = resp_buf[pos] */
            emit2(0x24,0xc0);                 /* and al,0xC0 */
            emit2(0x3c,0xc0);                 /* cmp al,0xC0 */
            int j_not_ptr = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x02);       /* pos += 2 */
            x_mov_rbpN32_r64(-72,0);
            int j_name_done = x_jmp_rel32();

            x_patch_here(j_not_ptr);
            int name_walk_loop = code_len;
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);
            emit2(0x84,0xc0);
            int j_name_walk_zero = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xff,0xc0);
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);
            int j_name_walk_back = x_jmp_rel32();
            patch_i32(j_name_walk_back,(int32_t)(name_walk_loop-(j_name_walk_back+4)));
            x_patch_here(j_name_walk_zero);
            x_mov_r64_rbpN32(0,-72);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-72,0);
            x_patch_here(j_name_done);

            /* pointer walk: rdx = &resp_buf[pos], read TYPE(2 BE), skip CLASS+TTL(6), read RDLENGTH(2 BE) */
            x_mov_r64_rbpN32(0,-72);
            x_lea_r64_rbpN32(3,-944);
            emit3(0x48,0x89,0xda);            /* mov rdx,rbx */
            emit3(0x48,0x01,0xc2);            /* add rdx,rax -> rdx = &resp_buf[pos] */

            emit2(0x8a,0x02);                 /* al = [rdx] */
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-168,0);
            emit3(0x48,0xff,0xc2);            /* inc rdx */
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-168);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-168,0);         /* rr_type = full */
            emit3(0x48,0xff,0xc2);            /* inc rdx -> at CLASS */

            emit4(0x48,0x83,0xc2,0x06);       /* rdx += 6 (skip CLASS+TTL) */

            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-176,0);
            emit3(0x48,0xff,0xc2);
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-176);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-176,0);         /* rr_rdlen = full */
            emit3(0x48,0xff,0xc2);            /* inc rdx -> rdx now points at RDATA */

            /* pos = (rdx - rbx) so it now marks the RDATA start, consistent for the advance-by-rdlength path below */
            emit3(0x48,0x89,0xd0);            /* mov rax,rdx */
            emit3(0x48,0x29,0xd8);            /* sub rax,rbx */
            x_mov_rbpN32_r64(-72,0);          /* pos = rax */

            x_mov_r64_rbpN32(0,-168);
            emit4(0x48,0x83,0xf8,0x01);       /* cmp rr_type,1 (A) */
            int j_type_no = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-176);
            emit4(0x48,0x83,0xf8,0x04);       /* cmp rr_rdlen,4 */
            int j_rdlen_no = x_jnz_rel32();

            /* MATCH: an A record. Try connecting to it right away rather
               than stopping at the first one found — multiple A records
               (or a CNAME chain ending in several) are common (a plain
               `dig`/live test against www.reddit.com turned up 4), and
               the first one isn't guaranteed reachable. On success,
               return this fd immediately; on failure, fall through to
               the same "advance past this record" code the non-A-type
               path below uses, so the loop moves on to the next answer
               record and tries again. CNAME records (TYPE=5) need no
               special handling at all here — they just take the j_type_no
               branch and get skipped via rdlength like any other
               non-A type, which already surfaces the eventual A record
               later in the same answer section for every resolver tested
               (verified live against www.github.com and
               www.microsoft.com, both CNAME-chained). */
            x_lea_r64_rbpN32(1,-104);         /* rcx = &found_ip */
            emit2(0x8b,0x02);                 /* eax = [rdx] (RDATA) */
            emit2(0x89,0x01);                 /* found_ip = eax */

            x_lea_r64_rbpN32(3,-160);         /* sockaddr_tcp */
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_rbpN32(0,-24);          /* port */
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            x_mov_r64_rbpN32(0,-104);         /* found_ip */
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            /* socket() as non-blocking (SOCK_STREAM|SOCK_NONBLOCK) rather
               than a plain blocking socket -- connect() on a blocking
               socket to an address that's routed but never answers (a
               blackholed/firewalled IP, as opposed to one that responds
               with an immediate RST) can hang for the OS's default TCP
               connect timeout, which is tens of seconds to minutes. That
               would make trying multiple A records nearly pointless in
               practice: exactly the "verify this actually retries, don't
               just assume the refactor works" case a live test against a
               deliberately-blackholed first record caught. Non-blocking
               connect + poll() bounds every attempt to 3s. */
            x_mov_r64_imm32(7,2);
            x_mov_r64_imm32(6,1|0x800); /* SOCK_STREAM|SOCK_NONBLOCK */
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_this_socket_fail = x_jl_rel32(); /* -> advance (no fd to close) */
            x_mov_rbpN32_r64(-112,0);         /* tcp_fd */

            x_mov_r64_rbpN32(7,-112);
            x_lea_r64_rbpN32(6,-160);
            x_mov_r64_imm32(2,16);
            x_mov_r64_imm32(0,42); /* SYS_connect (non-blocking: returns
                                       immediately, almost always with
                                       -EINPROGRESS) */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_connect_immediate_ok = x_jge_rel32(); /* rare, but possible */

            /* in progress (or some other immediate error, which poll +
               SO_ERROR below will surface too) -- wait up to 3s for the
               socket to become writable */
            x_mov_r64_rbpN32(0,-112);         /* rax = tcp_fd (reload -- eax currently
                                                   holds connect()'s return value, e.g.
                                                   -EINPROGRESS, not the fd) */
            emit1(0x89); emit1(0x85); emit_i32(-168); /* pollfd.fd (32-bit) = tcp_fd */
            emit1(0x66); emit1(0xc7); emit1(0x85); emit_i32(-164); emit1(0x04); emit1(0x00); /* pollfd.events = POLLOUT */
            emit1(0x66); emit1(0xc7); emit1(0x85); emit_i32(-162); emit1(0x00); emit1(0x00); /* pollfd.revents = 0 */
            x_lea_r64_rbpN32(7,-168);         /* rdi = &pollfd */
            x_mov_r64_imm32(6,1);             /* rsi = nfds = 1 */
            x_mov_r64_imm32(2,3000);          /* rdx = timeout_ms = 3000 */
            x_mov_r64_imm32(0,7);             /* rax = SYS_poll */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x01);       /* cmp rax,1 (exactly one fd became ready) */
            int j_poll_fail = x_jnz_rel32();  /* 0=timeout, <0=error -> close, advance */

            emit1(0x0f); emit1(0xb7); emit1(0x85); emit_i32(-162); /* movzx eax, word[pollfd.revents] */
            emit1(0xa9); emit_i32(4);         /* test eax, POLLOUT */
            int j_no_pollout = x_jz_rel32();  /* POLLERR/POLLHUP only -> close, advance */

            /* writable doesn't necessarily mean connected -- a failed
               non-blocking connect also wakes poll() up. SO_ERROR is the
               real answer. */
            x_mov_r64_rbpN32(7,-112);         /* rdi = tcp_fd */
            x_mov_r64_imm32(6,1);             /* rsi = SOL_SOCKET */
            x_mov_r64_imm32(2,4);             /* rdx = SO_ERROR */
            x_lea_r10_rbpN32(-176);           /* r10 = &so_error */
            emit1(0xc7); emit1(0x85); emit_i32(-172); emit_i32(4); /* so_error_len = 4 */
            x_lea_r8_rbpN32(-172);            /* r8 = &so_error_len */
            x_mov_r64_imm32(0,55);            /* rax = SYS_getsockopt */
            emit2(0x0f,0x05);
            emit1(0x8b); emit1(0x85); emit_i32(-176); /* eax = so_error */
            emit2(0x85,0xc0);                 /* test eax,eax */
            int j_so_error = x_jnz_rel32();   /* nonzero -> connect actually failed */

            x_patch_here(j_connect_immediate_ok);
            /* connected (either immediately, or confirmed via poll+SO_ERROR) --
               clear O_NONBLOCK so the blocking send/recv_print/close paths
               behave the way callers of a connected socket expect */
            x_mov_r64_rbpN32(7,-112);
            x_mov_r64_imm32(6,4);   /* F_SETFL */
            x_mov_r64_imm32(2,0);   /* flags = 0 (blocking) */
            x_mov_r64_imm32(0,72);  /* SYS_fcntl */
            emit2(0x0f,0x05);       /* best-effort, ignore result */

            x_mov_r64_rbpN32(0,-112);         /* rax = tcp_fd (success return value) */
            int j_final_ok = x_jmp_rel32();

            x_patch_here(j_poll_fail);
            x_patch_here(j_no_pollout);
            x_patch_here(j_so_error);
            x_mov_r64_rbpN32(7,-112);
            x_mov_r64_imm32(0,3);  /* SYS_close the failed attempt */
            emit2(0x0f,0x05);
            int j_after_close = x_jmp_rel32();

            x_patch_here(j_type_no);
            x_patch_here(j_rdlen_no);
            x_patch_here(j_this_socket_fail);
            x_patch_here(j_after_close);
            /* advance past this record (whatever it was — a skipped
               non-A type, or an A record whose connect just failed)
               and loop for the next answer record */
            x_mov_r64_rbpN32(0,-72);          /* rax = pos (RDATA start) */
            x_mov_r64_rbpN32(1,-176);         /* rcx = rdlength */
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);          /* pos += rdlength */

            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-88,0);          /* loop_i++ */
            int j_rr_back = x_jmp_rel32();
            patch_i32(j_rr_back,(int32_t)(rr_loop-(j_rr_back+4)));

            /* every answer record scanned (or the safety bounds hit) with
               no A record we could actually connect to -> -1 */
            x_patch_here(j_udp_fail);
            x_patch_here(j_recv_fail);
            x_patch_here(j_rr_done1);
            x_patch_here(j_rr_done2);
            x_mov_r64_imm32(0,-1);

            x_patch_here(j_final_ok);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_connect_host6(rdi=aaaa_query_ptr, rsi=aaaa_query_len, rdx=port)
           -> rax=fd or -1
           AAAA/IPv6 sibling of __ys_net_connect_host above. Structurally
           identical (same resolv.conf lookup, same UDP query/response,
           same answer-record walk with CNAME-skipping and multi-record
           retry-with-timeout) -- the only real differences are matching
           TYPE=28/RDLENGTH=16 instead of TYPE=1/RDLENGTH=4, copying 16
           RDATA bytes instead of 4, and connecting via AF_INET6 with a
           28-byte sockaddr_in6 instead of AF_INET/sockaddr_in (see
           __ys_net_connect6 above for that same shape used in the
           IPv6-literal case). Kept as a separate function rather than
           threading a record-type/address-family flag through the A
           version: that would touch nearly every offset and struct size
           in the function anyway, so the duplication is more honest
           about what's actually shared (the resolver-lookup and
           packet-framing logic) versus what isn't (the RR match and
           connect target shape).

           NOTE: socket(AF_INET6,...) itself cannot be exercised in an
           environment with IPv6 disabled at the kernel level (confirmed
           via strace elsewhere in this codebase's history returning
           EAFNOSUPPORT) -- the DNS query/response side of this function
           (building the AAAA query, sending/receiving over UDP, walking
           the answer section, extracting 16-byte RDATA) needs no IPv6
           kernel support at all and was verified against real AAAA
           responses; only the final connect() needs a real IPv6-capable
           host to exercise end-to-end.

           Stack layout differs from __ys_net_connect_host only in the
           last two entries:
             -8 through -176: identical in meaning to __ys_net_connect_host
             -1024..-997  struct sockaddr_in6 for the TCP target (28 bytes)
             -976..-961   found_ip6[16] (current answer record's address)
             -944..-433, -432..-177: resp_buf[512] / resolv_buf[256], same
                                       as __ys_net_connect_host (well below
                                       the two new slots above, no overlap)
           Frame is 1040 bytes (vs 960) to fit the two bigger structures. */
        sym_define("__ys_net_connect_host6",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(1040); /* sub rsp,1040 */

            x_mov_rbpN32_r64(-8,7);
            x_mov_rbpN32_r64(-16,6);
            x_mov_rbpN32_r64(-24,2);

            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-32);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-31);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-30);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-29);

            int resolv_path_off6 = data_add_str("/etc/resolv.conf");
            x_lea_arg1_data(resolv_path_off6);
            x_mov_r64_imm32(6,0);
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,2);
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_conf_open_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-48,0);

            x_mov_r64_rbpN32(7,-48);
            x_lea_r64_rbpN32(6,-432);
            x_mov_r64_imm32(2,255);
            x_mov_r64_imm32(0,0);
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-56,0);

            x_mov_r64_rbpN32(7,-48);
            x_mov_r64_imm32(0,3);
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-56);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_bytesread_le0 = x_jle_rel32();

            x_mov_qword_rbpN32_imm32(-88,0);
            int search_loop6 = code_len;
            x_mov_r64_rbpN32(0,-88);
            x_mov_r64_rbpN32(1,-56);
            emit4(0x48,0x83,0xe9,0x0b);
            emit3(0x48,0x39,0xc8);
            int j6_search_end = x_jge_rel32();

            x_lea_r64_rbpN32(3,-432);
            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0x01,0xc3);

            int needle_off6 = data_add_str("nameserver ");
            emit3(0x48,0x8d,0x35);
            add_reloc(RELOC_DATA,code_len,needle_off6); emit_i32(0);

            x_mov_r64_imm32(1,0);
            int cmp_loop6 = code_len;
            emit4(0x48,0x83,0xf9,0x0b);
            int j6_cmp_done = x_jge_rel32();
            x_mov_al_rbx_idx(1);
            emit3(0x8a,0x14,0x0e);
            emit2(0x38,0xd0);
            int j6_byte_ne = x_jnz_rel32();
            emit3(0x48,0xff,0xc1);
            int j6_cmp_back = x_jmp_rel32();
            patch_i32(j6_cmp_back,(int32_t)(cmp_loop6-(j6_cmp_back+4)));

            x_patch_here(j6_byte_ne);
            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-88,0);
            int j6_search_back = x_jmp_rel32();
            patch_i32(j6_search_back,(int32_t)(search_loop6-(j6_search_back+4)));

            x_patch_here(j6_cmp_done);
            emit3(0x48,0x8d,0x4b); emit1(0x0b);

            x_mov_qword_rbpN32_imm32(-72,0);
            x_mov_qword_rbpN32_imm32(-80,0);

            int parse_ip_loop6 = code_len;
            x_lea_r64_rbpN32(2,-432);
            x_mov_r64_rbpN32(0,-56);
            emit3(0x48,0x01,0xc2);
            emit3(0x48,0x39,0xd1);
            int j6_parse_ip_ge = x_jge_rel32();
            emit3(0x0f,0xb6,0x01);
            emit2(0x3c,0x2e);
            int j6_not_dot = x_jnz_rel32();

            x_mov_r64_rbpN32(0,-80);
            x_lea_r64_rbpN32(3,-32);
            x_mov_r64_rbpN32(2,-72);
            x_mov_rbx_idx_dl(0);
            x_mov_r64_rbpN32(0,-80);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-80,0);
            x_mov_qword_rbpN32_imm32(-72,0);
            int j6_to_next1 = x_jmp_rel32();

            x_patch_here(j6_not_dot);
            emit2(0x3c,0x30);
            int j6_lt0 = x_jl_rel32();
            emit2(0x3c,0x39);
            int j6_gt9 = x_jg_rel32();
            emit2(0x2c,0x30);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(2,-72);
            emit4(0x48,0x6b,0xd2,0x0a);
            emit3(0x48,0x01,0xc2);
            x_mov_rbpN32_r64(-72,2);

            x_patch_here(j6_to_next1);
            emit3(0x48,0xff,0xc1);
            int j6_parse_back = x_jmp_rel32();
            patch_i32(j6_parse_back,(int32_t)(parse_ip_loop6-(j6_parse_back+4)));

            x_patch_here(j6_parse_ip_ge);
            x_patch_here(j6_lt0);
            x_patch_here(j6_gt9);

            x_mov_r64_rbpN32(0,-80);
            emit4(0x48,0x83,0xf8,0x03);
            int j6_bad_octetcount = x_jnz_rel32();
            x_lea_r64_rbpN32(3,-32);
            x_mov_r64_rbpN32(2,-72);
            x_mov_rbx_idx_dl(0);
            x_patch_here(j6_bad_octetcount);

            x_patch_here(j6_conf_open_fail);
            x_patch_here(j6_bytesread_le0);
            x_patch_here(j6_search_end);

            /* ---- UDP socket (still plain IPv4 to the resolver -- DNS
               transport is unrelated to which record TYPE is being asked
               for, and resolv.conf entries here are always IPv4) ---- */
            x_mov_r64_imm32(7,2);
            x_mov_r64_imm32(6,2);
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41);
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_udp_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-40,0);

            x_mov_qword_rbpN32_imm32(-128,3);
            x_mov_qword_rbpN32_imm32(-120,0);
            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(6,1);
            x_mov_r64_imm32(2,20);
            x_lea_r10_rbpN32(-128);
            x_mov_r8d_imm32(16);
            x_mov_r64_imm32(0,54);
            emit2(0x0f,0x05);

            x_lea_r64_rbpN32(3,-144);
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_imm32(0,53);
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            x_mov_r64_rbpN32(0,-32);
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_rbpN32(6,-8);
            x_mov_r64_rbpN32(2,-16);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-144);
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44);
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(7,-40);
            x_lea_r64_rbpN32(6,-944);
            x_mov_r64_imm32(2,512);
            x_mov_r10d_imm32(0);
            x_mov_r8d_imm32(0);
            x_mov_r9d_imm32(0);
            x_mov_r64_imm32(0,45);
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-64,0);

            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(0,3);
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-64);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_recv_fail = x_jle_rel32();

            x_mov_qword_rbpN32_imm32(-72,12);
            int qname_loop6 = code_len;
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);
            int j6_qname_oob = x_jge_rel32();
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(0,-72);
            x_mov_al_rbx_idx(0);
            emit2(0x84,0xc0);
            int j6_qname_end = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xff,0xc0);
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);
            int j6_qname_back = x_jmp_rel32();
            patch_i32(j6_qname_back,(int32_t)(qname_loop6-(j6_qname_back+4)));

            x_patch_here(j6_qname_oob);
            x_patch_here(j6_qname_end);
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x05);
            x_mov_rbpN32_r64(-72,0);

            x_lea_r64_rbpN32(3,-944);
            emit3(0x0f,0xb6,0x43); emit1(0x06);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-80,0);
            emit3(0x0f,0xb6,0x43); emit1(0x07);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-80,0);

            x_mov_qword_rbpN32_imm32(-88,0);

            int rr_loop6 = code_len;
            x_mov_r64_rbpN32(0,-88);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x39,0xc8);
            int j6_rr_done1 = x_jge_rel32();
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);
            int j6_rr_done2 = x_jge_rel32();

            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);
            emit2(0x24,0xc0);
            emit2(0x3c,0xc0);
            int j6_not_ptr = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x02);
            x_mov_rbpN32_r64(-72,0);
            int j6_name_done = x_jmp_rel32();

            x_patch_here(j6_not_ptr);
            int name_walk_loop6 = code_len;
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);
            emit2(0x84,0xc0);
            int j6_name_walk_zero = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xff,0xc0);
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);
            int j6_name_walk_back = x_jmp_rel32();
            patch_i32(j6_name_walk_back,(int32_t)(name_walk_loop6-(j6_name_walk_back+4)));
            x_patch_here(j6_name_walk_zero);
            x_mov_r64_rbpN32(0,-72);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-72,0);
            x_patch_here(j6_name_done);

            x_mov_r64_rbpN32(0,-72);
            x_lea_r64_rbpN32(3,-944);
            emit3(0x48,0x89,0xda);
            emit3(0x48,0x01,0xc2);

            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-168,0);
            emit3(0x48,0xff,0xc2);
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-168);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-168,0);
            emit3(0x48,0xff,0xc2);

            emit4(0x48,0x83,0xc2,0x06);

            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-176,0);
            emit3(0x48,0xff,0xc2);
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-176);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-176,0);
            emit3(0x48,0xff,0xc2);

            emit3(0x48,0x89,0xd0);
            emit3(0x48,0x29,0xd8);
            x_mov_rbpN32_r64(-72,0);

            x_mov_r64_rbpN32(0,-168);
            emit4(0x48,0x83,0xf8,0x1c);       /* cmp rr_type,28 (AAAA) */
            int j6_type_no = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-176);
            emit4(0x48,0x83,0xf8,0x10);       /* cmp rr_rdlen,16 */
            int j6_rdlen_no = x_jnz_rel32();

            /* MATCH: an AAAA record -- copy all 16 RDATA bytes (two 8-byte
               chunks) into found_ip6, build a 28-byte sockaddr_in6, and
               try connecting immediately, same retry-with-timeout shape
               as the A-record version. rdx still points at RDATA here,
               same as in __ys_net_connect_host at the equivalent point. */
            x_lea_r64_rbpN32(1,-976);         /* rcx = &found_ip6 */
            emit3(0x48,0x8b,0x02);            /* rax = [rdx] (bytes 0-7) */
            emit3(0x48,0x89,0x01);            /* [rcx] = rax */
            emit4(0x48,0x8b,0x42,0x08);       /* rax = [rdx+8] (bytes 8-15) */
            emit3(0x48,0x89,0x41); emit1(0x08); /* [rcx+8] = rax */

            x_lea_r64_rbpN32(3,-1024);        /* sockaddr_in6 */
            emit4(0x66,0xc7,0x03,0x0a); emit1(0x00); /* family = AF_INET6 */
            x_mov_r64_rbpN32(0,-24);          /* port */
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            emit1(0xc7); emit1(0x43); emit1(0x04); emit_i32(0); /* flowinfo = 0 */
            x_lea_r64_rbpN32(1,-976);         /* rcx = &found_ip6 (again, rbx got overwritten above) */
            emit3(0x48,0x8b,0x01);            /* rax = found_ip6[0..7] */
            emit3(0x48,0x89,0x43); emit1(0x08); /* sockaddr+8 = rax */
            emit4(0x48,0x8b,0x41,0x08);       /* rax = found_ip6[8..15] */
            emit3(0x48,0x89,0x43); emit1(0x10); /* sockaddr+16 = rax */
            emit1(0xc7); emit1(0x43); emit1(0x18); emit_i32(0); /* scope_id = 0 */

            /* non-blocking connect + poll(3s) + SO_ERROR, identical
               shape to the A-record version, just AF_INET6/28 bytes */
            x_mov_r64_imm32(7,10);            /* AF_INET6 */
            x_mov_r64_imm32(6,1|0x800);       /* SOCK_STREAM|SOCK_NONBLOCK */
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41);
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_this_socket_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-112,0);

            x_mov_r64_rbpN32(7,-112);
            x_lea_r64_rbpN32(6,-1024);
            x_mov_r64_imm32(2,28);            /* sizeof(sockaddr_in6) */
            x_mov_r64_imm32(0,42);
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j6_connect_immediate_ok = x_jge_rel32();

            x_mov_r64_rbpN32(0,-112);
            emit1(0x89); emit1(0x85); emit_i32(-168);
            emit1(0x66); emit1(0xc7); emit1(0x85); emit_i32(-164); emit1(0x04); emit1(0x00);
            emit1(0x66); emit1(0xc7); emit1(0x85); emit_i32(-162); emit1(0x00); emit1(0x00);
            x_lea_r64_rbpN32(7,-168);
            x_mov_r64_imm32(6,1);
            x_mov_r64_imm32(2,3000);
            x_mov_r64_imm32(0,7);
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x01);
            int j6_poll_fail = x_jnz_rel32();

            emit1(0x0f); emit1(0xb7); emit1(0x85); emit_i32(-162);
            emit1(0xa9); emit_i32(4);
            int j6_no_pollout = x_jz_rel32();

            x_mov_r64_rbpN32(7,-112);
            x_mov_r64_imm32(6,1);
            x_mov_r64_imm32(2,4);
            x_lea_r10_rbpN32(-176);
            emit1(0xc7); emit1(0x85); emit_i32(-172); emit_i32(4);
            x_lea_r8_rbpN32(-172);
            x_mov_r64_imm32(0,55);
            emit2(0x0f,0x05);
            emit1(0x8b); emit1(0x85); emit_i32(-176);
            emit2(0x85,0xc0);
            int j6_so_error = x_jnz_rel32();

            x_patch_here(j6_connect_immediate_ok);
            x_mov_r64_rbpN32(7,-112);
            x_mov_r64_imm32(6,4);
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,72);
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-112);
            int j6_final_ok = x_jmp_rel32();

            x_patch_here(j6_poll_fail);
            x_patch_here(j6_no_pollout);
            x_patch_here(j6_so_error);
            x_mov_r64_rbpN32(7,-112);
            x_mov_r64_imm32(0,3);
            emit2(0x0f,0x05);
            int j6_after_close = x_jmp_rel32();

            x_patch_here(j6_type_no);
            x_patch_here(j6_rdlen_no);
            x_patch_here(j6_this_socket_fail);
            x_patch_here(j6_after_close);
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-176);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);

            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-88,0);
            int j6_rr_back = x_jmp_rel32();
            patch_i32(j6_rr_back,(int32_t)(rr_loop6-(j6_rr_back+4)));

            x_patch_here(j6_udp_fail);
            x_patch_here(j6_recv_fail);
            x_patch_here(j6_rr_done1);
            x_patch_here(j6_rr_done2);
            x_mov_r64_imm32(0,-1);

            x_patch_here(j6_final_ok);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_send(rdi=buf, rsi=len, rdx=fd) -> rax=bytes written or -1
           Argument order is (buf,len,fd) rather than the more natural
           (fd,buf,len) specifically so the call site can stage the
           literal buf/len first (cheap, no register risk) and the
           fd expression last, without needing to re-shuffle anything
           already in place. Just SYS_write on a connected TCP socket,
           internally reordered to the real write(fd,buf,len) ABI. */
        sym_define("__ys_net_send",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x89,0xf8); /* mov rax,rdi (buf) */
            emit3(0x48,0x89,0xd7); /* mov rdi,rdx (fd) */
            emit3(0x48,0x89,0xf2); /* mov rdx,rsi (len) */
            emit3(0x48,0x89,0xc6); /* mov rsi,rax (buf) */
            x_mov_r64_imm32(0,1); /* SYS_write */
            emit2(0x0f,0x05);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_recv_print(rdi=fd, rsi=maxlen) -> rax=bytes read or -1
           Reads into an internal static buffer and writes it straight
           to stdout. Stands in for a value-returning recv() — see the
           comment at the y.net.recv_print call site in compile_node for
           why. Caps at a fixed internal buffer size regardless of the
           requested maxlen. */
        sym_define("__ys_net_recv_print",code_len);
        {
            int recvbuf_off=data_len;
            static const int RECVBUF_CAP=4096;
            for(int i=0;i<RECVBUF_CAP;i++) data_buf[data_len++]=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x83,0xec); emit1(0x10); /* sub rsp,16 */
            x_mov_rbpN_r64(-8,7);  /* [rbp-8]=fd */
            /* clamp maxlen to RECVBUF_CAP */
            emit3(0x48,0x81,0xfe); emit_i32(RECVBUF_CAP); /* cmp rsi, RECVBUF_CAP */
            int j_ok=x_jl_rel32();
            x_mov_r64_imm32(6,RECVBUF_CAP); /* rsi = RECVBUF_CAP */
            x_patch_here(j_ok);
            x_mov_rbpN_r64(-16,6); /* [rbp-16]=maxlen (clamped) */

            /* read(fd, recvbuf, maxlen) */
            x_mov_r64_rbpN(7,-8);  /* rdi=fd */
            emit3(0x48,0x8d,0x35); /* lea rsi,[rip+recvbuf] */
            add_reloc(RELOC_DATA,code_len,recvbuf_off); emit_i32(0);
            x_mov_r64_rbpN(2,-16); /* rdx=maxlen */
            x_mov_r64_imm32(0,0);  /* SYS_read */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_nowrite=x_jle_rel32(); /* n<=0: nothing to print */
            x_mov_rbpN_r64(-16,0); /* [rbp-16] = n (bytes actually read) */

            /* write(1, recvbuf, n) */
            x_mov_r64_imm32(7,1); /* fd=1 */
            emit3(0x48,0x8d,0x35); /* lea rsi,[rip+recvbuf] */
            add_reloc(RELOC_DATA,code_len,recvbuf_off); emit_i32(0);
            x_mov_r64_rbpN(2,-16); /* rdx=n */
            x_mov_r64_imm32(0,1);  /* SYS_write */
            emit2(0x0f,0x05);
            x_mov_r64_rbpN(0,-16); /* rax = n (return the byte count, not write()'s retval) */

            x_patch_here(j_nowrite);
            emit3(0x48,0x83,0xc4); emit1(0x10); /* add rsp,16 */
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_close(rdi=fd) */
        sym_define("__ys_net_close",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            x_mov_r64_imm32(0,3); /* SYS_close */
            emit2(0x0f,0x05);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_listen(rdi=port) -> rax=fd or -1
           socket + setsockopt(SO_REUSEADDR) + bind(INADDR_ANY:port) +
           listen(backlog=128). */
        sym_define("__ys_net_listen",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(64); /* sub rsp,64 */
            x_mov_rbpN_r64(-8,7); /* [rbp-8]=port */

            x_mov_r64_imm32(7,2); x_mov_r64_imm32(6,1); x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_fail1=x_jl_rel32();
            x_mov_rbpN_r64(-16,0); /* [rbp-16]=fd */

            /* setsockopt(fd, SOL_SOCKET=1, SO_REUSEADDR=2, &optval, 4)
               — lets a restarted server rebind the same port
               immediately instead of failing with "address already in
               use" while the old socket sits in TIME_WAIT. Return
               value ignored: a failed setsockopt here isn't fatal,
               bind() below just behaves as it did before this call
               existed. */
            x_mov_qword_rbpN_imm32(-24,1); /* [rbp-24] = optval = 1 */
            x_mov_r64_rbpN(7,-16);         /* rdi=fd */
            x_mov_r64_imm32(6,1);          /* rsi=SOL_SOCKET */
            x_mov_r64_imm32(2,2);          /* rdx=SO_REUSEADDR */
            x_lea_r10_rbpN(-24);           /* r10=&optval */
            x_mov_r8d_imm32(4);            /* r8=optlen */
            x_mov_r64_imm32(0,54);         /* rax=SYS_setsockopt */
            emit2(0x0f,0x05);

            /* sockaddr_in at [rbp-48]: AF_INET, htons(port), INADDR_ANY, zero */
            x_lea_r64_rbpN(3,-48);
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00); /* mov word [rbx],2 */
            x_mov_r64_rbpN(0,-8);
            emit2(0x86,0xc4); /* xchg al,ah (htons) */
            emit4(0x66,0x89,0x43,0x02); /* mov word [rbx+2],ax */
            emit3(0x48,0xc7,0x43); emit1(0x04); emit_i32(0); /* mov qword [rbx+4],0 — zeros sin_addr (4B) + overlaps into sin_zero's first 4B, harmless since the next instruction zeros that whole range again */
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0); /* mov qword [rbx+8],0 (sin_zero) */

            x_mov_r64_rbpN(7,-16); /* rdi=fd */
            x_lea_r64_rbpN(6,-48); /* rsi=&addr */
            x_mov_r64_imm32(2,16); /* rdx=16 */
            x_mov_r64_imm32(0,49); /* SYS_bind */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_fail2=x_jl_rel32();

            x_mov_r64_rbpN(7,-16); /* rdi=fd */
            x_mov_r64_imm32(6,128); /* rsi=backlog */
            x_mov_r64_imm32(0,50); /* SYS_listen */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_fail3=x_jl_rel32();

            x_mov_r64_rbpN(0,-16); /* return fd */
            int j_done1=x_jmp_rel32();

            x_patch_here(j_fail2);
            x_patch_here(j_fail3);
            x_mov_r64_rbpN(7,-16);
            x_mov_r64_imm32(0,3); /* SYS_close */
            emit2(0x0f,0x05);
            x_mov_r64_imm32(0,-1);
            int j_done2=x_jmp_rel32();

            x_patch_here(j_fail1);
            x_mov_r64_imm32(0,-1);

            x_patch_here(j_done1);
            x_patch_here(j_done2);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_accept(rdi=server_fd) -> rax=client fd or -1 */
        sym_define("__ys_net_accept",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            x_mov_r64_imm32(6,0); /* rsi=NULL */
            x_mov_r64_imm32(2,0); /* rdx=NULL */
            x_mov_r64_imm32(0,43); /* SYS_accept */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_ok=x_jge_rel32();
            x_mov_r64_imm32(0,-1);
            x_patch_here(j_ok);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_socket() -> rax=fd or -1
           An unbound UDP socket (OS assigns a local port on first
           send), for a "client" that only sends and receives replies.
           Mirrors ys_udp_socket in net_runtime.c (the interpreter/VM
           version) — see udp_bind below for the "server" counterpart
           that needs a known port. */
        sym_define("__ys_net_udp_socket",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            x_mov_r64_imm32(7,2); x_mov_r64_imm32(6,2); x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket(AF_INET,SOCK_DGRAM,0) */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_ok=x_jge_rel32();
            x_mov_r64_imm32(0,-1);
            x_patch_here(j_ok);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_bind(rdi=port) -> rax=fd or -1
           socket + SO_REUSEADDR + bind(INADDR_ANY:port). No listen() —
           UDP has no connection to listen for; whatever arrives at this
           port is available via recvfrom immediately. Identical
           setsockopt/bind sequence to __ys_net_listen above, just
           SOCK_DGRAM instead of SOCK_STREAM and no listen() call. */
        sym_define("__ys_net_udp_bind",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(64); /* sub rsp,64 */
            x_mov_rbpN_r64(-8,7); /* [rbp-8]=port */

            x_mov_r64_imm32(7,2); x_mov_r64_imm32(6,2); x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket(AF_INET,SOCK_DGRAM,0) */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_fail1=x_jl_rel32();
            x_mov_rbpN_r64(-16,0); /* [rbp-16]=fd */

            x_mov_qword_rbpN_imm32(-24,1); /* optval=1 */
            x_mov_r64_rbpN(7,-16);
            x_mov_r64_imm32(6,1);  /* SOL_SOCKET */
            x_mov_r64_imm32(2,2);  /* SO_REUSEADDR */
            x_lea_r10_rbpN(-24);
            x_mov_r8d_imm32(4);
            x_mov_r64_imm32(0,54); /* SYS_setsockopt */
            emit2(0x0f,0x05);

            x_lea_r64_rbpN(3,-48); /* sockaddr_in: AF_INET, htons(port), INADDR_ANY, zero */
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_rbpN(0,-8);
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            emit3(0x48,0xc7,0x43); emit1(0x04); emit_i32(0);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            x_mov_r64_rbpN(7,-16);
            x_lea_r64_rbpN(6,-48);
            x_mov_r64_imm32(2,16);
            x_mov_r64_imm32(0,49); /* SYS_bind */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_fail2=x_jl_rel32();

            x_mov_r64_rbpN(0,-16);
            int j_done1=x_jmp_rel32();

            x_patch_here(j_fail2);
            x_mov_r64_rbpN(7,-16);
            x_mov_r64_imm32(0,3);
            emit2(0x0f,0x05);
            x_mov_r64_imm32(0,-1);
            int j_done2=x_jmp_rel32();

            x_patch_here(j_fail1);
            x_mov_r64_imm32(0,-1);

            x_patch_here(j_done1);
            x_patch_here(j_done2);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_send(rdi=sock, rsi=host_ptr, rdx=host_len,
                              rcx=port, r8=data_ptr, r9=data_len)
           -> rax=bytes sent or -1
           host is IPv4-dotted-decimal ONLY for this first native UDP
           batch, not a hostname — the octet-parsing loop here is a
           straight copy of __ys_net_connect's (parses ptr/len args at
           runtime rather than a compile-time literal, since host_ptr
           here is a real argument register, not something baked into
           rodata the way the TCP path's DNS query bytes are). Teaching
           udp_send to also accept hostnames would mean either resolving
           at runtime here too, or restricting host to a compile-time
           literal and building the resolved address at compile time
           like TCP now does — either is a reasonable follow-up, just
           kept out of this batch to bound its size. UDP sends are one
           full datagram in one syscall, so unlike __ys_net_send there's
           no short-write retry loop needed. */
        sym_define("__ys_net_udp_send",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(96); /* sub rsp,96 */

            x_mov_rbpN_r64(-8,7);   /* sock */
            x_mov_rbpN_r64(-16,6);  /* host cur-ptr (mutated during parse) */
            x_mov_r64_rbpN(0,-16);
            emit3(0x48,0x01,0xd0);  /* rax += rdx (host_len) */
            x_mov_rbpN_r64(-24,0);  /* host end ptr */
            x_mov_rbpN_r64(-32,1);  /* port */
            emit3(0x4c,0x89,0xc0);  /* mov rax,r8 */
            x_mov_rbpN_r64(-40,0);  /* data_ptr */
            emit3(0x4c,0x89,0xc8);  /* mov rax,r9 */
            x_mov_rbpN_r64(-48,0);  /* data_len */

            x_mov_qword_rbpN_imm32(-56,0); /* octet accumulator */
            x_mov_qword_rbpN_imm32(-64,0); /* octet_idx */

            int uloop_start=code_len;
            x_mov_r64_rbpN(0,-16);
            x_mov_r64_rbpN(1,-24);
            emit3(0x48,0x39,0xc8);
            int ju_loop_end=x_jge_rel32();

            x_mov_r64_rbpN(2,-16);
            emit3(0x0f,0xb6,0x02);  /* movzx eax,byte[rdx] */
            emit2(0x3c,0x2e);
            int ju_digit=x_jnz_rel32();

            x_mov_r64_rbpN(1,-64);
            x_mov_r64_rbpN(2,-56);
            x_lea_r64_rbpN(3,-72);  /* &ipbuf */
            emit3(0x88,0x14,0x0b);
            emit3(0x48,0xff,0x45); emit1((uint8_t)-64);
            x_mov_qword_rbpN_imm32(-56,0);
            int ju_next1=x_jmp_rel32();

            x_patch_here(ju_digit);
            emit2(0x2c,0x30);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN(2,-56);
            emit4(0x48,0x6b,0xd2,0x0a);
            emit3(0x48,0x01,0xc2);
            x_mov_rbpN_r64(-56,2);

            x_patch_here(ju_next1);
            emit3(0x48,0xff,0x45); emit1((uint8_t)-16);
            int ju_back=x_jmp_rel32();
            patch_i32(ju_back,(int32_t)(uloop_start-(ju_back+4)));

            x_patch_here(ju_loop_end);
            x_mov_r64_rbpN(1,-64);
            x_mov_r64_rbpN(2,-56);
            x_lea_r64_rbpN(3,-72);
            emit3(0x88,0x14,0x0b);

            x_lea_r64_rbpN(3,-88); /* sockaddr_in */
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_rbpN(0,-32); /* port */
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            x_lea_r64_rbpN(1,-72);
            emit2(0x8b,0x01);
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            /* sendto(sock, data_ptr, data_len, 0, &sockaddr, 16) */
            x_mov_r64_rbpN(7,-8);
            x_mov_r64_rbpN(6,-40);
            x_mov_r64_rbpN(2,-48);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-88);
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44); /* SYS_sendto */
            emit2(0x0f,0x05);
            /* rax already holds bytes sent (or -1) — correct return value */
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_send_host(rdi=dns_query_ptr, rsi=dns_query_len,
             rdx=port, rcx=sock, r8=data_ptr, r9=data_len) -> rax=bytes
             sent or -1

           Companion to __ys_net_udp_send above, for when y.net.udp_send's
           host argument is a hostname literal rather than a dotted-
           decimal IP — closing the gap that function's own doc comment
           called out as a reasonable follow-up.

           The resolver-discovery, DNS-query send/receive, and QNAME-skip
           blocks below (down to the "ancount" comment) are a deliberate
           byte-for-byte reuse of __ys_net_connect_host's own logic above
           — not a re-transcription. __ys_net_connect_host itself is left
           completely untouched: it's proven, real-world-tested code (see
           its own comments — verified live against several CNAME-chained
           hosts), and refactoring it to share this logic risked
           introducing a regression into that already-working TCP path
           for the sake of this narrower UDP one. Duplication was the
           lower-risk choice here.

           The answer-record loop past that point is genuinely simpler
           than __ys_net_connect_host's, not just copied: TCP's version
           tries actually connecting to each A record in turn (since some
           may be unreachable) and only gives up after all of them fail.
           UDP has no such notion — sendto() on a resolved IP either
           succeeds or it doesn't, there's nothing to "try connecting" to
           first — so this loop simply takes the *first* A record it
           finds and stops, with none of the non-blocking-connect/poll/
           SO_ERROR machinery that exists solely to bound each TCP
           connection attempt.

           Stack layout (frame = 976 bytes; offsets -8 through -176 and
           the resolv_buf/resp_buf regions below match
           __ys_net_connect_host's exactly, since that portion of the
           code is a direct copy and reusing the same offsets keeps it
           that way byte-for-byte):
             -8    dns_query_ptr (arg)
             -16   dns_query_len (arg)
             -24   port (arg)
             -32   resolver_ip[4]
             -40   udp_fd (the resolver-query socket, not the caller's)
             -48   resolv.conf fd
             -56   resolv.conf bytes_read
             -64   DNS response recv_len
             -72   pos / octet accumulator (reused, same as __ys_net_connect_host)
             -80   ancount / octet_idx (reused, same as __ys_net_connect_host)
             -88   loop_i
             -104  found_ip[4]
             -168  rr_type (scratch)
             -176  rr_rdlen (scratch)
             -432  resolv_buf[256]
             -944  resp_buf[512]
             -952  sock (arg — the caller's UDP socket, unrelated to udp_fd above)
             -960  data_ptr (arg)
             -968  data_len (arg) */
        sym_define("__ys_net_udp_send_host",code_len);
        {
            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(976); /* sub rsp,976 */

            x_mov_rbpN32_r64(-8,7);   /* query ptr */
            x_mov_rbpN32_r64(-16,6);  /* query len */
            x_mov_rbpN32_r64(-24,2);  /* port */
            x_mov_rbpN32_r64(-952,1); /* sock */
            /* r8/r9 aren't in the plain 0..7 register-encoding set
               x_mov_rbpN32_r64 uses (it's rax..rdi only), so move them
               through rax first rather than trying to pass 8/9 as a
               "reg" argument. */
            emit3(0x4c,0x89,0xc0);   /* mov rax,r8 */
            x_mov_rbpN32_r64(-960,0); /* data_ptr */
            emit3(0x4c,0x89,0xc8);   /* mov rax,r9 */
            x_mov_rbpN32_r64(-968,0); /* data_len */

            /* resolver_ip defaults to 8.8.8.8 */
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-32);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-31);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-30);
            x_mov_r64_imm32(0,8); x_mov_byte_rbpN32_al(-29);

            /* ---- try /etc/resolv.conf for a better resolver ---- */
            int resolv_path_off = data_add_str("/etc/resolv.conf");
            x_lea_arg1_data(resolv_path_off);
            x_mov_r64_imm32(6,0);
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,2); /* SYS_open */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_conf_open_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-48,0);

            x_mov_r64_rbpN32(7,-48);
            x_lea_r64_rbpN32(6,-432);
            x_mov_r64_imm32(2,255);
            x_mov_r64_imm32(0,0); /* SYS_read */
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-56,0);

            x_mov_r64_rbpN32(7,-48);
            x_mov_r64_imm32(0,3); /* SYS_close */
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-56);
            emit4(0x48,0x83,0xf8,0x00);
            int j_bytesread_le0 = x_jle_rel32();

            x_mov_qword_rbpN32_imm32(-88,0);
            int search_loop = code_len;
            x_mov_r64_rbpN32(0,-88);
            x_mov_r64_rbpN32(1,-56);
            emit4(0x48,0x83,0xe9,0x0b);
            emit3(0x48,0x39,0xc8);
            int j_search_end = x_jge_rel32();

            x_lea_r64_rbpN32(3,-432);
            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0x01,0xc3);

            int needle_off = data_add_str("nameserver ");
            emit3(0x48,0x8d,0x35);
            add_reloc(RELOC_DATA,code_len,needle_off); emit_i32(0);

            x_mov_r64_imm32(1,0);
            int cmp_loop = code_len;
            emit4(0x48,0x83,0xf9,0x0b);
            int j_cmp_done = x_jge_rel32();
            x_mov_al_rbx_idx(1);
            emit3(0x8a,0x14,0x0e);
            emit2(0x38,0xd0);
            int j_byte_ne = x_jnz_rel32();
            emit3(0x48,0xff,0xc1);
            int j_cmp_back = x_jmp_rel32();
            patch_i32(j_cmp_back,(int32_t)(cmp_loop-(j_cmp_back+4)));

            x_patch_here(j_byte_ne);
            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-88,0);
            int j_search_back = x_jmp_rel32();
            patch_i32(j_search_back,(int32_t)(search_loop-(j_search_back+4)));

            x_patch_here(j_cmp_done);
            emit3(0x48,0x8d,0x4b); emit1(0x0b);

            x_mov_qword_rbpN32_imm32(-72,0);
            x_mov_qword_rbpN32_imm32(-80,0);

            int parse_ip_loop = code_len;
            x_lea_r64_rbpN32(2,-432);
            x_mov_r64_rbpN32(0,-56);
            emit3(0x48,0x01,0xc2);
            emit3(0x48,0x39,0xd1);
            int j_parse_ip_ge = x_jge_rel32();
            emit3(0x0f,0xb6,0x01);
            emit2(0x3c,0x2e);
            int j_not_dot = x_jnz_rel32();

            x_mov_r64_rbpN32(0,-80);
            x_lea_r64_rbpN32(3,-32);
            x_mov_r64_rbpN32(2,-72);
            x_mov_rbx_idx_dl(0);
            x_mov_r64_rbpN32(0,-80);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-80,0);
            x_mov_qword_rbpN32_imm32(-72,0);
            int j_to_next1 = x_jmp_rel32();

            x_patch_here(j_not_dot);
            emit2(0x3c,0x30);
            int j_lt0 = x_jl_rel32();
            emit2(0x3c,0x39);
            int j_gt9 = x_jg_rel32();
            emit2(0x2c,0x30);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(2,-72);
            emit4(0x48,0x6b,0xd2,0x0a);
            emit3(0x48,0x01,0xc2);
            x_mov_rbpN32_r64(-72,2);

            x_patch_here(j_to_next1);
            emit3(0x48,0xff,0xc1);
            int j_parse_back = x_jmp_rel32();
            patch_i32(j_parse_back,(int32_t)(parse_ip_loop-(j_parse_back+4)));

            x_patch_here(j_parse_ip_ge);
            x_patch_here(j_lt0);
            x_patch_here(j_gt9);

            x_mov_r64_rbpN32(0,-80);
            emit4(0x48,0x83,0xf8,0x03);
            int j_bad_octetcount = x_jnz_rel32();
            x_lea_r64_rbpN32(3,-32);
            x_mov_r64_rbpN32(2,-72);
            x_mov_rbx_idx_dl(0);
            x_patch_here(j_bad_octetcount);

            x_patch_here(j_conf_open_fail);
            x_patch_here(j_bytesread_le0);
            x_patch_here(j_search_end);

            /* ---- UDP socket (to the resolver, not the caller's) ---- */
            x_mov_r64_imm32(7,2);
            x_mov_r64_imm32(6,2);
            x_mov_r64_imm32(2,0);
            x_mov_r64_imm32(0,41); /* SYS_socket */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int j_udp_fail = x_jl_rel32();
            x_mov_rbpN32_r64(-40,0);

            x_mov_qword_rbpN32_imm32(-128,3);
            x_mov_qword_rbpN32_imm32(-120,0);
            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(6,1);
            x_mov_r64_imm32(2,20);
            x_lea_r10_rbpN32(-128);
            x_mov_r8d_imm32(16);
            x_mov_r64_imm32(0,54); /* SYS_setsockopt */
            emit2(0x0f,0x05);

            x_lea_r64_rbpN32(3,-144);
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_imm32(0,53);
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            x_mov_r64_rbpN32(0,-32);
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_rbpN32(6,-8);
            x_mov_r64_rbpN32(2,-16);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-144);
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44); /* SYS_sendto (query, to resolver) */
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(7,-40);
            x_lea_r64_rbpN32(6,-944);
            x_mov_r64_imm32(2,512);
            x_mov_r10d_imm32(0);
            x_mov_r8d_imm32(0);
            x_mov_r9d_imm32(0);
            x_mov_r64_imm32(0,45); /* SYS_recvfrom (response, from resolver) */
            emit2(0x0f,0x05);
            x_mov_rbpN32_r64(-64,0);

            x_mov_r64_rbpN32(7,-40);
            x_mov_r64_imm32(0,3);
            emit2(0x0f,0x05);

            x_mov_r64_rbpN32(0,-64);
            emit4(0x48,0x83,0xf8,0x00);
            int j_recv_fail = x_jle_rel32();

            /* ---- parse DNS response: skip header(12) + QNAME + QTYPE/QCLASS ---- */
            x_mov_qword_rbpN32_imm32(-72,12);
            int qname_loop = code_len;
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);
            int j_qname_oob = x_jge_rel32();
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(0,-72);
            x_mov_al_rbx_idx(0);
            emit2(0x84,0xc0);
            int j_qname_end = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xff,0xc0);
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);
            int j_qname_back = x_jmp_rel32();
            patch_i32(j_qname_back,(int32_t)(qname_loop-(j_qname_back+4)));

            x_patch_here(j_qname_oob);
            x_patch_here(j_qname_end);
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x05);
            x_mov_rbpN32_r64(-72,0);

            x_lea_r64_rbpN32(3,-944);
            emit3(0x0f,0xb6,0x43); emit1(0x06);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-80,0);
            emit3(0x0f,0xb6,0x43); emit1(0x07);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-80,0); /* ancount */

            x_mov_qword_rbpN32_imm32(-88,0); /* loop_i = 0 */

            /* ---- answer-record loop: simplified for UDP -- take the
               first A record found and stop; no connect/poll needed. ---- */
            int rr_loop = code_len;
            x_mov_r64_rbpN32(0,-88);
            x_mov_r64_rbpN32(1,-80);
            emit3(0x48,0x39,0xc8);
            int j_rr_done1 = x_jge_rel32();
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-64);
            emit3(0x48,0x39,0xc8);
            int j_rr_done2 = x_jge_rel32();

            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);
            emit2(0x24,0xc0);
            emit2(0x3c,0xc0);
            int j_not_ptr = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-72);
            emit4(0x48,0x83,0xc0,0x02);
            x_mov_rbpN32_r64(-72,0);
            int j_name_done = x_jmp_rel32();

            x_patch_here(j_not_ptr);
            int name_walk_loop = code_len;
            x_lea_r64_rbpN32(3,-944);
            x_mov_r64_rbpN32(2,-72);
            x_mov_al_rbx_idx(2);
            emit2(0x84,0xc0);
            int j_name_walk_zero = x_jz_rel32();
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xff,0xc0);
            x_mov_r64_rbpN32(1,-72);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);
            int j_name_walk_back = x_jmp_rel32();
            patch_i32(j_name_walk_back,(int32_t)(name_walk_loop-(j_name_walk_back+4)));
            x_patch_here(j_name_walk_zero);
            x_mov_r64_rbpN32(0,-72);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-72,0);
            x_patch_here(j_name_done);

            x_mov_r64_rbpN32(0,-72);
            x_lea_r64_rbpN32(3,-944);
            emit3(0x48,0x89,0xda);
            emit3(0x48,0x01,0xc2);

            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-168,0);
            emit3(0x48,0xff,0xc2);
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-168);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-168,0); /* rr_type */
            emit3(0x48,0xff,0xc2);

            emit4(0x48,0x83,0xc2,0x06);

            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            emit3(0x48,0xc1,0xe0); emit1(0x08);
            x_mov_rbpN32_r64(-176,0);
            emit3(0x48,0xff,0xc2);
            emit2(0x8a,0x02);
            emit3(0x0f,0xb6,0xc0);
            x_mov_r64_rbpN32(1,-176);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-176,0); /* rr_rdlen */
            emit3(0x48,0xff,0xc2);

            emit3(0x48,0x89,0xd0);
            emit3(0x48,0x29,0xd8);
            x_mov_rbpN32_r64(-72,0); /* pos = RDATA start */

            x_mov_r64_rbpN32(0,-168);
            emit4(0x48,0x83,0xf8,0x01); /* rr_type==1 (A)? */
            int j_type_no = x_jnz_rel32();
            x_mov_r64_rbpN32(0,-176);
            emit4(0x48,0x83,0xf8,0x04); /* rr_rdlen==4? */
            int j_rdlen_no = x_jnz_rel32();

            /* found an A record: grab its 4 IP bytes and stop looking */
            x_lea_r64_rbpN32(1,-104);
            emit2(0x8b,0x02); /* eax = [rdx] (RDATA) */
            emit2(0x89,0x01); /* found_ip = eax */
            int j_found = x_jmp_rel32();

            x_patch_here(j_type_no);
            x_patch_here(j_rdlen_no);
            /* not an A record (or wrong length) -- advance past it and
               try the next answer record */
            x_mov_r64_rbpN32(0,-72);
            x_mov_r64_rbpN32(1,-176);
            emit3(0x48,0x01,0xc8);
            x_mov_rbpN32_r64(-72,0);

            x_mov_r64_rbpN32(0,-88);
            emit3(0x48,0xff,0xc0);
            x_mov_rbpN32_r64(-88,0);
            int j_rr_back = x_jmp_rel32();
            patch_i32(j_rr_back,(int32_t)(rr_loop-(j_rr_back+4)));

            /* exhausted every answer record, no A found -> -1 */
            x_patch_here(j_udp_fail);
            x_patch_here(j_recv_fail);
            x_patch_here(j_rr_done1);
            x_patch_here(j_rr_done2);
            x_mov_r64_imm32(0,-1);
            int j_no_answer = x_jmp_rel32();

            /* found_ip is set — sendto(sock, data_ptr, data_len, 0,
               &sockaddr{found_ip,port}, 16), same tail __ys_net_udp_send
               above uses. */
            x_patch_here(j_found);
            x_lea_r64_rbpN32(3,-160);
            emit4(0x66,0xc7,0x03,0x02); emit1(0x00);
            x_mov_r64_rbpN32(0,-24); /* port */
            emit2(0x86,0xc4);
            emit4(0x66,0x89,0x43,0x02);
            x_mov_r64_rbpN32(0,-104); /* found_ip */
            emit3(0x89,0x43,0x04);
            emit3(0x48,0xc7,0x43); emit1(0x08); emit_i32(0);

            x_mov_r64_rbpN32(7,-952); /* sock */
            x_mov_r64_rbpN32(6,-960); /* data_ptr */
            x_mov_r64_rbpN32(2,-968); /* data_len */
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-160);
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44); /* SYS_sendto */
            emit2(0x0f,0x05);

            x_patch_here(j_no_answer);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_recv_print(rdi=sock, rsi=maxlen) -> rax=bytes
           received or -1. Same "no runtime string type, so print
           instead of returning a value" reasoning as __ys_net_recv_print
           — the difference from that one is recvfrom() instead of
           read(), since UDP has no fixed peer the way a connected TCP
           socket does; the sender's address recvfrom fills in here is
           simply discarded (see __ys_net_udp_recv_reply_print below for
           the variant that keeps and uses it). */
        sym_define("__ys_net_udp_recv_print",code_len);
        {
            int urecvbuf_off=data_len;
            static const int URECVBUF_CAP=4096;
            for(int i=0;i<URECVBUF_CAP;i++) data_buf[data_len++]=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(64); /* sub rsp,64 */
            x_mov_rbpN_r64(-8,7);  /* sock */
            emit3(0x48,0x81,0xfe); emit_i32(URECVBUF_CAP);
            int ju_ok=x_jl_rel32();
            x_mov_r64_imm32(6,URECVBUF_CAP);
            x_patch_here(ju_ok);
            x_mov_rbpN_r64(-16,6); /* maxlen (clamped) */

            x_mov_qword_rbpN_imm32(-24,16); /* fromlen = sizeof(sockaddr_in) */

            x_mov_r64_rbpN(7,-8);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,urecvbuf_off); emit_i32(0); /* rsi=&buf */
            x_mov_r64_rbpN(2,-16);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-48);  /* r8=&fromaddr (throwaway, 16 bytes at -48..-33) */
            x_lea_r9_rbpN32(-24);  /* r9=&fromlen */
            x_mov_r64_imm32(0,45); /* SYS_recvfrom */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int ju_norecv=x_jle_rel32();
            x_mov_rbpN_r64(-16,0); /* n */

            x_mov_r64_imm32(7,1);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,urecvbuf_off); emit_i32(0);
            x_mov_r64_rbpN(2,-16);
            x_mov_r64_imm32(0,1); /* SYS_write */
            emit2(0x0f,0x05);
            x_mov_r64_rbpN(0,-16); /* return n, not write()'s retval */
            int ju_done=x_jmp_rel32();

            x_patch_here(ju_norecv);
            x_mov_r64_imm32(0,-1);

            x_patch_here(ju_done);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }

        /* __ys_net_udp_recv_reply_print(rdi=sock, rsi=maxlen,
                                          rdx=reply_ptr, rcx=reply_len)
           -> rax=bytes received or -1
           Reads one datagram (printing its payload, same as
           udp_recv_print above), then sends reply_ptr/reply_len back to
           whichever address it just arrived from — captured internally
           in this function's own stack memory and never exposed to the
           Yolish program as a value. This is the native workaround for
           the fact that udp_recv normally returns {data, host, port} as
           a y.map (see ys_udp_recv in net_runtime.c) so a program can
           reply to a specific sender: the native backend has no map or
           runtime-string type to hand that address back through, but
           "receive, then reply to that same sender" is by far the most
           common reason a program needs the sender's address in the
           first place (an echo/reply server), so it's worth a dedicated
           primitive rather than not supporting that pattern at all. If
           the reply send fails, the received byte count is still
           returned — the recv already happened and its payload was
           already printed, so that half of the call did succeed. */
        sym_define("__ys_net_udp_recv_reply_print",code_len);
        {
            int urrecvbuf_off=data_len;
            static const int URRECVBUF_CAP=4096;
            for(int i=0;i<URRECVBUF_CAP;i++) data_buf[data_len++]=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(96); /* sub rsp,96 */
            x_mov_rbpN_r64(-8,7);  /* sock */
            emit3(0x48,0x81,0xfe); emit_i32(URRECVBUF_CAP);
            int jur_ok=x_jl_rel32();
            x_mov_r64_imm32(6,URRECVBUF_CAP);
            x_patch_here(jur_ok);
            x_mov_rbpN_r64(-16,6); /* maxlen (clamped) */
            x_mov_rbpN_r64(-24,2); /* reply_ptr */
            x_mov_rbpN_r64(-32,1); /* reply_len */

            x_mov_qword_rbpN_imm32(-40,16); /* fromlen = sizeof(sockaddr_in) */

            /* recvfrom(sock, buf, maxlen, 0, &fromaddr[-96..-81], &fromlen) —
               fromaddr is KEPT this time (not throwaway), reused directly
               as the destination for the reply's sendto below */
            x_mov_r64_rbpN(7,-8);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,urrecvbuf_off); emit_i32(0);
            x_mov_r64_rbpN(2,-16);
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-96);  /* r8=&fromaddr (16 bytes at -96..-81) */
            x_lea_r9_rbpN32(-40);  /* r9=&fromlen */
            x_mov_r64_imm32(0,45); /* SYS_recvfrom */
            emit2(0x0f,0x05);
            emit4(0x48,0x83,0xf8,0x00);
            int jur_norecv=x_jle_rel32();
            x_mov_rbpN_r64(-16,0); /* n */

            x_mov_r64_imm32(7,1);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,urrecvbuf_off); emit_i32(0);
            x_mov_r64_rbpN(2,-16);
            x_mov_r64_imm32(0,1); /* SYS_write */
            emit2(0x0f,0x05);

            /* sendto(sock, reply_ptr, reply_len, 0, &fromaddr, 16) —
               replying to the address recvfrom just captured */
            x_mov_r64_rbpN(7,-8);
            x_mov_r64_rbpN(6,-24); /* reply_ptr */
            x_mov_r64_rbpN(2,-32); /* reply_len */
            x_mov_r10d_imm32(0);
            x_lea_r8_rbpN32(-96);  /* same fromaddr recvfrom just filled in */
            x_mov_r9d_imm32(16);
            x_mov_r64_imm32(0,44); /* SYS_sendto */
            emit2(0x0f,0x05);
            /* ignore sendto's own result — n (the recv byte count) is
               the return value regardless, per the doc comment above */

            x_mov_r64_rbpN(0,-16); /* return n */
            int jur_done=x_jmp_rel32();

            x_patch_here(jur_norecv);
            x_mov_r64_imm32(0,-1);

            x_patch_here(jur_done);
            x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        }
    }
    emit_float_helper();
}

/*  string length  */
static int ystrlen(const char *s){ int n=0; while(s[n])n++; return n; }

/* Parses a compile-time URL literal into scheme/host/port/path. Returns
   1 on success (a recognized http:// or https:// scheme with a
   non-empty host), 0 otherwise. Used by y.http.get_print/post_print
   below — url is required to be a string literal (same constraint as
   every other host/data argument in this file), so this all happens
   in C, at compile time, never emitted as runtime code. */
static int parse_http_url(const char *url, int *use_tls, char *host, int hostcap, int *port, char *path, int pathcap){
    const char *p;
    if(strncmp(url,"https://",8)==0){ *use_tls=1; *port=443; p=url+8; }
    else if(strncmp(url,"http://",7)==0){ *use_tls=0; *port=80; p=url+7; }
    else return 0;
    const char *slash=strchr(p,'/');
    const char *hostend = slash ? slash : p+ystrlen(p);
    const char *colon=NULL;
    for(const char *q=p; q<hostend; q++) if(*q==':'){ colon=q; break; }
    int hlen = (int)((colon?colon:hostend)-p);
    if(hlen<=0 || hlen>=hostcap) return 0;
    memcpy(host,p,hlen); host[hlen]=0;
    if(colon){ int pv=0; for(const char *q=colon+1;q<hostend && *q>='0'&&*q<='9';q++) pv=pv*10+(*q-'0'); if(pv>0) *port=pv; }
    if(slash){
        int plen=ystrlen(slash);
        if(plen>=pathcap) return 0;
        memcpy(path,slash,plen+1);
    } else {
        path[0]='/'; path[1]=0;
    }
    return 1;
}

/* Connects to host:port (TLS if use_tls, plain TCP otherwise), sends
   the pre-built request at data offset req_off/req_len, reads and
   prints whatever comes back, then closes -- one shot, no stored
   handle, since y.http.get_print/post_print don't need one (url, and
   therefore host/port, are compile-time constants here, unlike
   tls_connect's runtime host/port, so none of that function's
   argument-staging machinery is needed: nothing here is ever compiled
   from a Node*, so there's no frame-switch-ordering bug to avoid in
   the first place). Leaves rax = bytes received or -1. Shared between
   get_print and post_print so the connect/handshake logic — the part
   that's actually easy to get subtly wrong — exists in exactly one
   place. */
/* in: rax = byte index into the shared g_tls_rbuf_off scratch buffer.
   out: rax = that byte, zero-extended. Used by the chunked-encoding
   decoder below, which needs to read arbitrary bytes out of the
   buffer by a runtime-computed index rather than a compile-time
   disp8, unlike every other rbuf access elsewhere in this file. */
static void x_rbuf_byte_at_rax(void){
    emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0); /* lea r11,[rip+rbuf] */
    emit3(0x4c,0x01,0xd8); /* add rax,r11 */
    emit3(0x0f,0xb6,0x00); /* movzx eax,byte[rax] */
}

/* Decodes and prints whatever's sitting in g_tls_rbuf_off[0..total),
   where total is read from [rbp-40] of the caller's (emit_http_request's)
   own frame -- both its plain-HTTP and HTTPS branches accumulate their
   response into that same slot before calling this, rather than
   printing each read()/SSL_read() chunk immediately as they arrive
   the way tls_recv_print/__ys_net_recv_print do. That's a deliberate,
   local-to-the-HTTP-client change: chunked transfer-encoding can't be
   decoded a few bytes at a time as they stream in, since a chunk's
   hex size line can itself be split across two separate reads --
   decoding needs the *whole* response in hand first. The real cost of
   that: a chunked response larger than the 4095-byte rbuf cap gets
   silently truncated rather than streamed in full the way a
   non-chunked one still can be (see the non-chunked fallback below,
   which still only sees whatever fit in the buffer). A real, documented
   trade-off, not an oversight.

   Detects chunked encoding by scanning the header section for the
   substring "chunked" (case-sensitive) -- not "Transfer-Encoding:
   chunked" specifically, just the word anywhere in the headers, which
   in practice is exactly as reliable and far simpler to implement.
   Headers are always printed verbatim either way. If no header/body
   blank-line separator is found at all (an unusually malformed or
   truncated response), the whole buffer is printed raw and nothing
   past that point runs. */
static void emit_http_decode_and_print(void){
    /* ---- find "\r\n\r\n" (header_end = start of that sequence) ---- */
    x_mov_qword_rbpN32_imm32(-56,-1); /* header_end = -1 (not found yet) */
    x_mov_qword_rbpN32_imm32(-64,0);  /* pos = 0 */
    int find_loop=code_len;
    x_mov_r64_rbpN(0,-64);
    emit4(0x48,0x83,0xc0,0x03);
    x_mov_r64_rbpN(1,-40);
    emit3(0x48,0x39,0xc8);
    int j_find_done=x_jge_rel32();

    x_mov_r64_rbpN(0,-64);
    x_rbuf_byte_at_rax();
    emit4(0x48,0x83,0xf8,0x0d); /* '\r' */
    int j_ne1=x_jnz_rel32();
    x_mov_r64_rbpN(0,-64); emit3(0x48,0xff,0xc0);
    x_rbuf_byte_at_rax();
    emit4(0x48,0x83,0xf8,0x0a); /* '\n' */
    int j_ne2=x_jnz_rel32();
    x_mov_r64_rbpN(0,-64); emit4(0x48,0x83,0xc0,0x02);
    x_rbuf_byte_at_rax();
    emit4(0x48,0x83,0xf8,0x0d);
    int j_ne3=x_jnz_rel32();
    x_mov_r64_rbpN(0,-64); emit4(0x48,0x83,0xc0,0x03);
    x_rbuf_byte_at_rax();
    emit4(0x48,0x83,0xf8,0x0a);
    int j_ne4=x_jnz_rel32();

    x_mov_r64_rbpN(0,-64);
    x_mov_rbpN_r64(-56,0); /* header_end = pos */
    int j_found=x_jmp_rel32();

    x_patch_here(j_ne1); x_patch_here(j_ne2); x_patch_here(j_ne3); x_patch_here(j_ne4);
    x_mov_r64_rbpN(0,-64); emit3(0x48,0xff,0xc0);
    x_mov_rbpN_r64(-64,0);
    int j_find_back=x_jmp_rel32();
    patch_i32(j_find_back,(int32_t)(find_loop-(j_find_back+4)));

    x_patch_here(j_find_done);
    x_patch_here(j_found);

    x_mov_r64_rbpN(0,-56);
    emit4(0x48,0x83,0xf8,0x00);
    int j_have_split=x_jge_rel32();
    /* no split found -- print the whole buffer raw and stop */
    x_mov_r64_imm32(7,1);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0);
    x_mov_r64_rbpN(2,-40);
    x_mov_r64_imm32(0,1); emit2(0x0f,0x05);
    int j_all_done=x_jmp_rel32();
    x_patch_here(j_have_split);

    x_mov_r64_rbpN(0,-56);
    emit4(0x48,0x83,0xc0,0x04);
    x_mov_rbpN_r64(-72,0); /* body_start = header_end + 4 */

    /* ---- scan headers[0..header_end) for "chunked" ---- */
    int needle_off=data_add_str("chunked");
    x_mov_qword_rbpN32_imm32(-80,0); /* is_chunked = 0 */
    x_mov_qword_rbpN32_imm32(-64,0); /* outer pos = 0 */
    int outer_loop=code_len;
    x_mov_r64_rbpN(0,-64);
    emit4(0x48,0x83,0xc0,0x07); /* pos+7 (strlen("chunked")) */
    x_mov_r64_rbpN(1,-56); /* header_end */
    emit3(0x48,0x39,0xc8);
    int j_outer_done=x_jg_rel32();

    x_mov_qword_rbpN32_imm32(-88,0); /* inner i = 0 */
    int inner_loop=code_len;
    x_mov_r64_rbpN(0,-88);
    emit4(0x48,0x83,0xf8,0x07);
    int j_matched=x_jz_rel32(); /* i==7: all 7 bytes matched */

    x_mov_r64_rbpN(0,-64); x_mov_r64_rbpN(1,-88);
    emit3(0x48,0x01,0xc8); /* rax = pos+i */
    x_rbuf_byte_at_rax();
    x_mov_rbpN_r64(-96,0); /* stash buffer byte */

    emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,needle_off); emit_i32(0); /* rax=&needle */
    x_mov_r64_rbpN(1,-88); emit3(0x48,0x01,0xc8); /* rax=&needle[i] */
    emit3(0x0f,0xb6,0x00); /* al=needle[i] */

    x_mov_r64_rbpN(1,-96);
    emit3(0x48,0x39,0xc8); /* cmp rax,rcx  (needle byte vs buffer byte) */
    int j_mismatch=x_jnz_rel32();

    x_mov_r64_rbpN(0,-88); emit3(0x48,0xff,0xc0);
    x_mov_rbpN_r64(-88,0);
    int j_inner_back=x_jmp_rel32();
    patch_i32(j_inner_back,(int32_t)(inner_loop-(j_inner_back+4)));

    x_patch_here(j_matched);
    x_mov_qword_rbpN32_imm32(-80,1); /* is_chunked = 1 */
    int j_scan_done=x_jmp_rel32();

    x_patch_here(j_mismatch);
    x_mov_r64_rbpN(0,-64); emit3(0x48,0xff,0xc0);
    x_mov_rbpN_r64(-64,0);
    int j_outer_back=x_jmp_rel32();
    patch_i32(j_outer_back,(int32_t)(outer_loop-(j_outer_back+4)));

    x_patch_here(j_outer_done);
    x_patch_here(j_scan_done);

    /* headers + blank line, always printed verbatim */
    x_mov_r64_imm32(7,1);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0);
    x_mov_r64_rbpN(2,-72); /* body_start */
    x_mov_r64_imm32(0,1); emit2(0x0f,0x05);

    x_mov_r64_rbpN(0,-80);
    emit4(0x48,0x83,0xf8,0x00);
    int j_is_chunked=x_jnz_rel32();
    /* not chunked -- print body[body_start..total) verbatim, same as
       the old streaming behavior's net effect */
    x_mov_r64_imm32(7,1);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0);
    x_mov_r64_rbpN(0,-72);
    emit3(0x48,0x01,0xc6); /* rsi += body_start (rax) */
    x_mov_r64_rbpN(2,-40); x_mov_r64_rbpN(1,-72);
    emit3(0x48,0x29,0xca); /* rdx -= body_start (total-body_start) */
    x_mov_r64_imm32(0,1); emit2(0x0f,0x05);
    int j_done2=x_jmp_rel32();
    x_patch_here(j_is_chunked);

    /* ---- chunked: pos = body_start; loop: parse hex size, skip to
       \r\n, print that many bytes, skip trailing \r\n, repeat until
       size==0 ---- */
    x_mov_r64_rbpN(0,-72);
    x_mov_rbpN_r64(-64,0); /* pos = body_start */

    int chunk_loop=code_len;
    x_mov_qword_rbpN32_imm32(-56,0); /* reuse header_end slot as chunk_size accumulator */

    int hex_loop=code_len;
    x_mov_r64_rbpN(0,-64);
    x_mov_r64_rbpN(1,-40);
    emit3(0x48,0x39,0xc8);
    int j_hex_oob=x_jge_rel32();
    x_mov_r64_rbpN(0,-64);
    x_rbuf_byte_at_rax(); /* rax = current byte */
    x_mov_rbpN_r64(-96,0);

    emit4(0x48,0x83,0xf8,0x30); /* '0' */
    int j_lt0=x_jl_rel32();
    emit4(0x48,0x83,0xf8,0x39); /* '9' */
    int j_le9=x_jle_rel32();
    x_patch_here(j_lt0);
    emit4(0x48,0x83,0xf8,0x61); /* 'a' */
    int j_lta=x_jl_rel32();
    emit4(0x48,0x83,0xf8,0x66); /* 'f' */
    int j_lef=x_jle_rel32();
    x_patch_here(j_lta);
    /* not a hex digit -- stop accumulating the size */
    int j_hex_end=x_jmp_rel32();

    x_patch_here(j_le9);
    x_mov_r64_rbpN(0,-56); emit4(0x48,0x6b,0xc0,0x10); /* chunk_size *= 16 */
    x_mov_r64_rbpN(1,-96); emit4(0x48,0x83,0xe9,0x30); /* digit - '0' */
    emit3(0x48,0x01,0xc8);
    x_mov_rbpN_r64(-56,0);
    int j_digit_done1=x_jmp_rel32();

    x_patch_here(j_lef);
    x_mov_r64_rbpN(0,-56); emit4(0x48,0x6b,0xc0,0x10);
    x_mov_r64_rbpN(1,-96); emit4(0x48,0x83,0xe9,0x57); /* digit - 'a' + 10 */
    emit3(0x48,0x01,0xc8);
    x_mov_rbpN_r64(-56,0);

    x_patch_here(j_digit_done1);
    x_mov_r64_rbpN(0,-64); emit3(0x48,0xff,0xc0);
    x_mov_rbpN_r64(-64,0);
    int j_hex_back=x_jmp_rel32();
    patch_i32(j_hex_back,(int32_t)(hex_loop-(j_hex_back+4)));

    x_patch_here(j_hex_oob);
    x_patch_here(j_hex_end);

    /* skip forward to the \r\n ending the chunk-size line (tolerates
       chunk extensions like ";foo=bar" before it, same as any
       compliant client must) */
    int skip_loop=code_len;
    x_mov_r64_rbpN(0,-64);
    emit4(0x48,0x83,0xc0,0x01);
    x_mov_r64_rbpN(1,-40);
    emit3(0x48,0x39,0xc8);
    int j_skip_oob=x_jge_rel32();
    x_mov_r64_rbpN(0,-64);
    x_rbuf_byte_at_rax();
    emit4(0x48,0x83,0xf8,0x0d);
    int j_found_cr=x_jz_rel32();
    x_mov_r64_rbpN(0,-64); emit3(0x48,0xff,0xc0);
    x_mov_rbpN_r64(-64,0);
    int j_skip_back=x_jmp_rel32();
    patch_i32(j_skip_back,(int32_t)(skip_loop-(j_skip_back+4)));

    x_patch_here(j_skip_oob);
    int j_done3=x_jmp_rel32(); /* ran off the end mid chunk-size line -- stop */
    x_patch_here(j_found_cr);

    x_mov_r64_rbpN(0,-64); emit4(0x48,0x83,0xc0,0x02); /* pos += 2 (skip \r\n) */
    x_mov_rbpN_r64(-64,0);

    x_mov_r64_rbpN(0,-56); /* chunk_size */
    emit4(0x48,0x83,0xf8,0x00);
    int j_chunk_nonzero=x_jnz_rel32();
    int j_done4=x_jmp_rel32(); /* size 0 -- last chunk, stop */
    x_patch_here(j_chunk_nonzero);

    /* clamp chunk_size to whatever's actually left in the buffer, in
       case the response got truncated mid-chunk against the 4095-byte
       cap */
    x_mov_r64_rbpN(0,-40); x_mov_r64_rbpN(1,-64);
    emit3(0x48,0x29,0xc8); /* rax = total - pos (bytes actually available) */
    x_mov_r64_rbpN(1,-56); /* rcx = chunk_size */
    emit3(0x48,0x39,0xc8); /* cmp rax,rcx */
    int j_have_enough=x_jge_rel32();
    x_mov_rbpN_r64(-56,0); /* chunk_size = total-pos (clamped) */
    x_patch_here(j_have_enough);

    x_mov_r64_imm32(7,1);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0);
    x_mov_r64_rbpN(0,-64);
    emit3(0x48,0x01,0xc6); /* rsi += pos */
    x_mov_r64_rbpN(2,-56); /* rdx = chunk_size */
    x_mov_r64_imm32(0,1); emit2(0x0f,0x05);

    x_mov_r64_rbpN(0,-64); x_mov_r64_rbpN(1,-56);
    emit3(0x48,0x01,0xc8); /* pos += chunk_size */
    emit4(0x48,0x83,0xc0,0x02); /* pos += 2 (trailing \r\n after chunk data) */
    x_mov_rbpN_r64(-64,0);

    int j_chunk_back=x_jmp_rel32();
    patch_i32(j_chunk_back,(int32_t)(chunk_loop-(j_chunk_back+4)));

    x_patch_here(j_done2);
    x_patch_here(j_done3);
    x_patch_here(j_done4);
    x_patch_here(j_all_done);
}

static void emit_http_request(const char *host, int port, int use_tls, int req_off, int req_len){
    if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); return; }
    tls_state_ensure();

    uint8_t ip6buf[16];
    int is_ipv6 = is_ipv6_literal(host) && parse_ipv6_literal(host,ip6buf);
    int is_ipv4 = is_ipv4_literal(host);

    x_push_rbp(); x_mov_rbp_rsp();
    emit3(0x48,0x81,0xec); emit_i32(112); /* -8 fd -16 method -24 ctx -32 ssl -40 total -48 n
                                              -56 header_end -64 pos -72 body_start -80 is_chunked
                                              -88 inner_i -96 cmpbyte (decode-and-print scratch) */

    int fail_patches[8]; int nfail=0;

    if(is_ipv6){
        int off=data_add_bytes(ip6buf,16);
        emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0);
        x_mov_r64_imm32(6,port);
        int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect6");
    } else if(is_ipv4){
        int off=data_add_str(host);
        emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0);
        x_mov_r64_imm32(6,ystrlen(host));
        x_mov_r64_imm32(2,port);
        int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect");
    } else {
        uint8_t qbuf[512]; int qlen=build_dns_query(host,qbuf);
        int qoff=data_add_bytes(qbuf,qlen);
        emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,qoff); emit_i32(0);
        x_mov_r64_imm32(6,qlen);
        x_mov_r64_imm32(2,port);
        int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect_host");
    }
    x_mov_rbpN_r64(-8,0);
    emit4(0x48,0x83,0xf8,0x00);
    int j_conn_ok=x_jge_rel32();
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_conn_ok);

    if(!use_tls){
        /* plain HTTP: __ys_net_send to write the request (reusing the
           exact subroutine y.net.send calls), then a hand-rolled
           read/write loop for the response instead of a single
           __ys_net_recv_print call.

           This loop exists because of a real bug this rebuild's testing
           caught: a single read() isn't guaranteed to capture a whole
           HTTP response. A real local test server sent headers and body
           as two separate writes, which arrived as two separate TCP
           segments -- one read() call only ever saw the headers, and
           the body was silently missing from the output (no crash, no
           error, just an incomplete response — exactly the kind of bug
           that's easy to miss without actually running it against a
           real server). __ys_net_recv_print/y.net.tls_recv_print keep
           their existing, documented single-read behavior unchanged —
           that's an established contract other code may depend on —
           this loop is local to the HTTP client only. */
        emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,req_off); emit_i32(0); /* rdi=&req */
        x_mov_r64_imm32(6,req_len); /* rsi=len */
        x_mov_r64_rbpN(2,-8);       /* rdx=fd */
        { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_send"); }

        x_mov_qword_rbpN_imm32(-40,0); /* total=0 */

        int loop_top1=code_len;
        /* remaining = (CAP-1) - total; stop if <= 0 */
        x_mov_r64_imm32(0,YS_TLS_RBUF_CAP-1);
        x_mov_r64_rbpN(1,-40);
        emit3(0x48,0x29,0xc8); /* rax -= rcx (remaining) */
        emit4(0x48,0x83,0xf8,0x00);
        int j_room_left=x_jg_rel32();
        int j_loop_end1=x_jmp_rel32();
        x_patch_here(j_room_left);
        x_mov_rbpN_r64(-104,0); /* stash remaining capacity separately from "n" */

        x_mov_r64_rbpN(7,-8); /* rdi=fd */
        emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0); /* rsi=&rbuf */
        x_mov_r64_rbpN(0,-40); emit3(0x48,0x01,0xc6); /* rsi += total (append, don't overwrite) */
        x_mov_r64_rbpN(2,-104); /* rdx=remaining capacity */
        x_mov_r64_imm32(0,0); /* SYS_read */
        emit2(0x0f,0x05);
        /* rax = n, straight off the syscall -- save it before anything
           else touches rax */
        x_mov_rbpN_r64(-48,0);
        emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
        int j_have_chunk1=x_jg_rel32();
        int j_loop_end1b=x_jmp_rel32();
        x_patch_here(j_have_chunk1);

        x_mov_r64_rbpN(0,-40); x_mov_r64_rbpN(1,-48);
        emit3(0x48,0x01,0xc8); /* rax = old_total + n */
        x_mov_rbpN_r64(-40,0);

        int jback1=x_jmp_rel32();
        patch_i32(jback1,(int32_t)(loop_top1-(jback1+4)));
        x_patch_here(j_loop_end1);
        x_patch_here(j_loop_end1b);

        x_mov_r64_rbpN(7,-8);
        { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }

        emit_http_decode_and_print();
        x_mov_r64_rbpN(0,-40);

        for(int i=0;i<nfail;i++) x_patch_here(fail_patches[i]);
        x_mov_rsp_rbp(); x_pop_rbp();
        return;
    }

    /* HTTPS: same handshake shape as y.net.tls_connect, minus all the
       handle/table bookkeeping (nothing here is stored past this one
       request) and minus the argument-staging machinery (host/port are
       C constants here, not a Node* — nothing to stage across a frame
       switch that hasn't already happened by the time this runs). */
    dynlink_need_library("libssl.so.3");
    int init_got=dynlink_import("OPENSSL_init_ssl");
    int method_got=dynlink_import("TLS_client_method");
    int ctxnew_got=dynlink_import("SSL_CTX_new");
    int sslnew_got=dynlink_import("SSL_new");
    int setfd_got=dynlink_import("SSL_set_fd");
    int sslconnect_got=dynlink_import("SSL_connect");
    int sslctrl_got=dynlink_import("SSL_ctrl");
    int sslwrite_got=dynlink_import("SSL_write");
    int sslread_got=dynlink_import("SSL_read");
    int ctxfree_got=dynlink_import("SSL_CTX_free");
    int sslfree_got=dynlink_import("SSL_free");
    int setverify_got=dynlink_import("SSL_CTX_set_verify");
    int ctxctrl_got=dynlink_import("SSL_CTX_ctrl");
    int setdefverify_got=dynlink_import("SSL_CTX_set_default_verify_paths");
    int set1host_got=dynlink_import("SSL_set1_host");
    int getverifyresult_got=dynlink_import("SSL_get_verify_result");

    x_mov_r64_imm32(7,0); x_mov_r64_imm32(6,0);
    x_call_got(init_got);

    x_call_got(method_got);
    emit3(0x48,0x89,0xc7);
    x_call_got(ctxnew_got);
    x_mov_rbpN_r64(-24,0);
    x_test_rax_rax();
    int j_ctx_ok=x_jnz_rel32();
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_ctx_ok);

    /* Certificate verification -- same approach as y.net.tls_connect,
       see that function's comment for the full reasoning. Duplicated
       rather than shared for the same reason the rest of this
       function's handshake logic is duplicated: host/port here are C
       constants, not a Node*, so there's no clean way to call into
       tls_connect's Node*-driven codegen from here. */
    x_mov_r64_rbpN(7,-24);
    x_mov_r64_imm32(6,1);               /* SSL_VERIFY_PEER */
    x_mov_r64_imm32(2,0);
    x_call_got(setverify_got);

    x_mov_r64_rbpN(7,-24);
    x_mov_r64_imm32(6,123);             /* SSL_CTRL_SET_MIN_PROTO_VERSION */
    x_mov_r64_imm32(2,0x0303);          /* TLS1_2_VERSION */
    x_mov_r64_imm32(1,0);
    x_call_got(ctxctrl_got);

    x_mov_r64_rbpN(7,-24);
    x_call_got(setdefverify_got);
    emit4(0x48,0x83,0xf8,0x01);
    int j_verifypaths_ok=x_jz_rel32();
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_verifypaths_ok);

    x_mov_r64_rbpN(7,-24);
    x_call_got(sslnew_got);
    x_mov_rbpN_r64(-32,0);
    x_test_rax_rax();
    int j_ssl_ok=x_jnz_rel32();
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_ssl_ok);

    if(!is_ipv6 && !is_ipv4){
        int host_off=data_add_str(host);
        x_mov_r64_rbpN(7,-32);
        x_mov_r64_imm32(6,55);
        x_mov_r64_imm32(2,0);
        emit3(0x48,0x8d,0x0d); add_reloc(RELOC_DATA,code_len,host_off); emit_i32(0);
        x_call_got(sslctrl_got);

        x_mov_r64_rbpN(7,-32);
        emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,host_off); emit_i32(0);
        x_call_got(set1host_got);
    }

    x_mov_r64_rbpN(7,-32);
    x_mov_r64_rbpN(6,-8);
    x_call_got(setfd_got);
    emit4(0x48,0x83,0xf8,0x01);
    int j_setfd_ok=x_jz_rel32();
    x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_setfd_ok);

    x_mov_r64_rbpN(7,-32);
    x_call_got(sslconnect_got);
    emit4(0x48,0x83,0xf8,0x01);
    int j_hs_ok=x_jz_rel32();
    x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_hs_ok);

    x_mov_r64_rbpN(7,-32);
    x_call_got(getverifyresult_got);
    x_test_rax_rax();
    int j_verify_ok=x_jz_rel32();
    x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
    x_mov_rax_imm32(-1); x_mov_rbpN_r64(-40,0); fail_patches[nfail++]=x_jmp_rel32();
    x_patch_here(j_verify_ok);

    x_mov_r64_rbpN(7,-32);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,req_off); emit_i32(0);
    x_mov_r64_imm32(2,req_len);
    x_call_got(sslwrite_got);

    /* read/decode/print loop -- see the plain-HTTP branch's comment
       above for why this accumulates into the buffer instead of
       printing each SSL_read chunk immediately (same reasoning,
       extended further for v2.33: chunked-encoding decoding needs the
       whole response in hand, not a few bytes at a time). */
    x_mov_qword_rbpN_imm32(-40,0); /* total=0 */

    int loop_top2=code_len;
    x_mov_r64_imm32(0,YS_TLS_RBUF_CAP-1);
    x_mov_r64_rbpN(1,-40);
    emit3(0x48,0x29,0xc8);
    emit4(0x48,0x83,0xf8,0x00);
    int j_room_left2=x_jg_rel32();
    int j_loop_end2=x_jmp_rel32();
    x_patch_here(j_room_left2);
    x_mov_rbpN_r64(-104,0); /* remaining capacity */

    x_mov_r64_rbpN(7,-32);
    emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0);
    x_mov_r64_rbpN(0,-40); emit3(0x48,0x01,0xc6); /* rsi += total */
    x_mov_r64_rbpN(2,-104);
    x_call_got(sslread_got);
    emit4(0x48,0x83,0xf8,0x00);
    int j_have_chunk2=x_jg_rel32();
    int j_loop_end2b=x_jmp_rel32();
    x_patch_here(j_have_chunk2);
    x_mov_rbpN_r64(-16,0); /* n */

    x_mov_r64_rbpN(0,-40); x_mov_r64_rbpN(1,-16);
    emit3(0x48,0x01,0xc8); /* total += n */
    x_mov_rbpN_r64(-40,0);

    int jback2=x_jmp_rel32();
    patch_i32(jback2,(int32_t)(loop_top2-(jback2+4)));
    x_patch_here(j_loop_end2);
    x_patch_here(j_loop_end2b);

    emit_http_decode_and_print();

    x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
    x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
    x_mov_r64_rbpN(7,-8); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }

    for(int i=0;i<nfail;i++) x_patch_here(fail_patches[i]);
    x_mov_r64_rbpN(0,-40);
    x_mov_rsp_rbp(); x_pop_rbp();
}

/*  loop/branch label stack  */
#define LABEL_MAX 64
static int break_stack[LABEL_MAX];   /* patch offsets for break */
static int bstack_top=0;
static int continue_stack[LABEL_MAX];
static int cstack_top=0;

/*  compile expression → result in rax  */

static void compile_expr(Node *n){
    if(!n){ x_mov_rax_imm32(0); return; }
    switch(n->kind){
    case ND_INT:
        if(n->ival>=-2147483648LL && n->ival<=2147483647LL) x_mov_rax_imm32((int32_t)n->ival);
        else x_mov_rax_imm64(n->ival);
        g_last_float=0; break;
    case ND_BOOL:
        x_mov_rax_imm32(n->ival?1:0); g_last_float=0; break;
    case ND_FLOAT:{
        int64_t bits; double v=n->fval; memcpy(&bits,&v,8);
        x_mov_rax_imm64(bits); g_last_float=1; break;
    }
    case ND_STR:{
        int off=data_add_str(n->sval);
        /* lea rax, [rip + data_off] */
        emit3(0x48,0x8d,0x05);
        add_reloc(RELOC_DATA,code_len,off); emit_i32(0);
        break;
    }
    case ND_IDENT:{
        int off=local_get(n->name);
        g_last_float=0;
        if(off){ x_mov_rax_mem(off);
            for(int _i=0;_i<nlocals;_i++) if(strcmp(locals[_i].name,n->name)==0){g_last_float=locals[_i].is_float;break;}
        } else x_mov_rax_imm32(0);
        break;
    }
    case ND_UNOP:
        compile_expr(n->right);
        if(n->op==TK_MINUS){
            if(g_last_float){ emit2(0x48,0xb9); emit_i64((int64_t)((uint64_t)1<<63)); emit3(0x48,0x31,0xc8); }
            else emit3(0x48,0xf7,0xd8);
        } else if(n->op==TK_NOT){ x_test_rax_rax(); x_set_bool(0x94); g_last_float=0; }
        break;
    case ND_BINOP:{
        /* short-circuit && and || */
        if(n->op==TK_AND){
            compile_expr(n->left); x_test_rax_rax();
            int jz=x_jz_rel32();
            compile_expr(n->right); x_test_rax_rax();
            x_set_bool(0x95); /* setne */
            int jend=x_jmp_rel32();
            x_patch_here(jz);
            x_mov_rax_imm32(0);
            x_patch_here(jend);
            break;
        }
        if(n->op==TK_OR){
            compile_expr(n->left); x_test_rax_rax();
            int jnz=x_jnz_rel32();
            compile_expr(n->right); x_test_rax_rax();
            x_set_bool(0x95);
            int jend=x_jmp_rel32();
            x_patch_here(jnz);
            x_mov_rax_imm32(1);
            x_patch_here(jend);
            break;
        }
        compile_expr(n->left); int _lf=g_last_float; x_push_rax();
        compile_expr(n->right); int _rf=g_last_float;
        emit3(0x48,0x89,0xc1); x_pop_rax();
        if(_lf||_rf||n->left->kind==ND_FLOAT||n->right->kind==ND_FLOAT){
            if(_lf) x_movq_xmm0_rax(); else x_cvtsi2sd_xmm0_rax();
            if(_rf) x_movq_xmm1_rcx(); else x_cvtsi2sd_xmm1_rcx();
            switch(n->op){
            case TK_PLUS:  x_addsd(); x_movq_rax_xmm0(); g_last_float=1; break;
            case TK_MINUS: x_subsd(); x_movq_rax_xmm0(); g_last_float=1; break;
            case TK_STAR:  x_mulsd(); x_movq_rax_xmm0(); g_last_float=1; break;
            case TK_SLASH: x_divsd(); x_movq_rax_xmm0(); g_last_float=1; break;
            case TK_EQEQ: x_ucomisd(); x_set_bool(0x94); g_last_float=0; break;
            case TK_NEQ:  x_ucomisd(); x_set_bool(0x95); g_last_float=0; break;
            case TK_LT:   x_ucomisd(); x_set_bool(0x92); g_last_float=0; break;
            case TK_LTE:  x_ucomisd(); x_set_bool(0x96); g_last_float=0; break;
            case TK_GT:   x_ucomisd(); x_set_bool(0x97); g_last_float=0; break;
            case TK_GTE:  x_ucomisd(); x_set_bool(0x93); g_last_float=0; break;
            default: x_mov_rax_imm32(0); g_last_float=0; break;
            }
        } else {
            g_last_float=0;
            switch(n->op){
            case TK_PLUS:    x_add_rax_rcx(); break;
            case TK_MINUS:   x_sub_rax_rcx(); break;
            case TK_STAR:    x_imul_rax_rcx(); break;
            case TK_SLASH:   x_idiv_setup(); break;
            case TK_PERCENT: x_idiv_setup(); emit3(0x48,0x89,0xd0); break;
            case TK_EQEQ:   x_cmp_rax_rcx(); x_set_bool(0x94); break;
            case TK_NEQ:    x_cmp_rax_rcx(); x_set_bool(0x95); break;
            case TK_LT:     x_cmp_rax_rcx(); x_set_bool(0x9c); break;
            case TK_LTE:    x_cmp_rax_rcx(); x_set_bool(0x9e); break;
            case TK_GT:     x_cmp_rax_rcx(); x_set_bool(0x9f); break;
            case TK_GTE:    x_cmp_rax_rcx(); x_set_bool(0x9d); break;
            default: x_mov_rax_imm32(0); break;
            }
        }
        break;
    }
    case ND_CALL:{
        /* handle builtins */
        const char *fn=n->name;
        /* Is this call really y.NAMESPACE.method(...)? n->name is only
           the trailing segment ("connect" for y.net.connect(...)), so
           for method names generic enough to plausibly be a user
           function (connect/send/recv/close, unlike e.g. println),
           verify the receiver chain before treating it as a builtin —
           otherwise a user's own `fn connect(...)` would get silently
           hijacked. */
        int is_y_net = n->left && n->left->kind==ND_DOT
            && strcmp(n->left->name,"net")==0
            && n->left->left && n->left->left->kind==ND_IDENT
            && strcmp(n->left->left->name,"y")==0;
        int is_y_http = n->left && n->left->kind==ND_DOT
            && strcmp(n->left->name,"http")==0
            && n->left->left && n->left->left->kind==ND_IDENT
            && strcmp(n->left->left->name,"y")==0;
        /* y.println(val) */
        if(strcmp(fn,"println")==0||strcmp(fn,"y.println")==0){
            if(n->argc>0){
                Node *arg=(n->left)?n->args[1]:n->args[0];
                if(!arg){ /* no-arg println: just newline */
                    int p=x_call_unresolved(); add_call_patch(p,"__ys_print_nl"); break;
                }
                if(arg->kind==ND_STR){
                    /* string literal: print_str(ptr, len) */
                    int off=data_add_str(arg->sval);
                    int len=ystrlen(arg->sval);
                    x_lea_arg1_data(off);
                    x_mov_rax_imm32(len);
                    x_arg2_from_rax();
                    int p=x_call_unresolved(); add_call_patch(p,"__ys_print_str");
                } else {
                    compile_expr(arg); x_arg1_from_rax();
                    if(g_last_float){ int p=x_call_unresolved(); add_call_patch(p,"__ys_print_float"); }
                    else { int p=x_call_unresolved(); add_call_patch(p,"__ys_print_int"); }
                }
            }
            /* newline */
            int p2=x_call_unresolved(); add_call_patch(p2,"__ys_print_nl");
            x_mov_rax_imm32(0);
            break;
        }
        /* y.print(val) */
        if(strcmp(fn,"print")==0||strcmp(fn,"y.print")==0){
            if(n->argc>0){
                Node *arg=(n->left)?n->args[1]:n->args[0];
                if(arg&&arg->kind==ND_STR){
                    int off=data_add_str(arg->sval);
                    int len=ystrlen(arg->sval);
                    x_lea_arg1_data(off);
                    x_mov_rax_imm32(len); x_arg2_from_rax();
                    int p=x_call_unresolved(); add_call_patch(p,"__ys_print_str");
                } else if(arg){
                    compile_expr(arg); x_arg1_from_rax();
                    if(g_last_float){ int p=x_call_unresolved(); add_call_patch(p,"__ys_print_float"); }
                    else { int p=x_call_unresolved(); add_call_patch(p,"__ys_print_int"); }
                }
            }
            x_mov_rax_imm32(0);
            break;
        }
        /* y.exit(code) */
        if(strcmp(fn,"exit")==0||strcmp(fn,"y.exit")==0){
            Node *arg=(n->argc>0)?((n->left)?n->args[1]:n->args[0]):NULL;
            if(arg){ compile_expr(arg); x_arg1_from_rax(); }
            else { x_mov_rax_imm32(0); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_exit");
            break;
        }
        /* y.net.connect(ip_or_host, port) -> fd or -1
           The address MUST be a string literal — the native backend has
           no general runtime string type yet (only literals, the same
           ceiling println/print already have), so a variable holding
           an address string can't be passed through here. Arguments are
           marshaled via push/pop rather than assuming evaluation order
           leaves earlier registers untouched — robust regardless of
           what compile_expr does internally.

           Three address shapes, checked in this order:
             1. IPv6 literal ("::1", "2001:db8::1") -- parsed by this
                compiler's own portable parser at compile time
                (parse_ipv6_literal) and handed straight to
                __ys_net_connect6 as 16 raw bytes; no DNS involved. If
                it looks IPv6-shaped (has a ':') but doesn't actually
                parse, falls through to case 3 below
                instead of a special error path -- building a query
                containing colons that will just cleanly fail to resolve
                at runtime, same philosophy as the final fallback.
             2. Dotted-decimal IPv4 literal ("93.184.216.34") -- the
                original fast path straight to __ys_net_connect, no DNS.
             3. Anything else with a letter in it is a hostname: the DNS
                query packet for it is built here at compile time
                (build_dns_query — valid since the literal is fixed at
                compile time either way) and handed to
                __ys_net_connect_host, which resolves it over UDP at
                runtime before connecting.
           Linux only, matching every other y.net.* native symbol (see
           the TARGET_LINUX guard these are defined under above) —
           macOS/Windows have no native y.net.* support at all yet for
           any shape of address. */
        if(is_y_net && strcmp(fn,"connect")==0){
            int base=n->left?1:0;
            Node *ip_arg=(n->argc>base)?n->args[base]:NULL;
            Node *port_arg=(n->argc>base+1)?n->args[base+1]:NULL;

            uint8_t ip6buf[16];
            int is_ipv6 = ip_arg && ip_arg->kind==ND_STR &&
                          is_ipv6_literal(ip_arg->sval) &&
                          parse_ipv6_literal(ip_arg->sval, ip6buf);
            if(is_ipv6){
                int off = data_add_bytes(ip6buf, 16);
                if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
                emit1(0x50); /* push rax (port) */
                emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* lea rdi,[rip+off] */
                emit1(0x5e); /* pop rsi (port) -- connect6 is (rdi=addr_ptr, rsi=port), no length arg needed */
                int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect6");
                break;
            }

            int is_hostname = ip_arg && ip_arg->kind==ND_STR && !is_ipv4_literal(ip_arg->sval);
            if(is_hostname){
                uint8_t qbuf[512], qbuf6[512];
                int qlen = build_dns_query(ip_arg->sval, qbuf);
                int qlen6 = build_dns_query_aaaa(ip_arg->sval, qbuf6);
                int off = data_add_bytes(qbuf, qlen);
                int off6 = data_add_bytes(qbuf6, qlen6);

                if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
                emit1(0x50); /* push rax (port) — kept on the stack across
                                 both attempts below rather than consumed
                                 immediately, since it's needed twice if
                                 the A lookup comes back empty */

                /* try A (IPv4) first */
                emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* rdi = &a_query */
                x_mov_rax_imm32(qlen); emit3(0x48,0x89,0xc6); /* rsi = qlen */
                emit4(0x48,0x8b,0x14,0x24); /* mov rdx,[rsp] (peek port, stack untouched) */
                int pA=x_call_unresolved(); add_call_patch(pA,"__ys_net_connect_host");
                emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
                int j_a_ok = x_jge_rel32(); /* rax>=0 -> got a fd, skip AAAA entirely */

                /* no usable A record (-1) -> fall back to AAAA (IPv6) */
                emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off6); emit_i32(0); /* rdi = &aaaa_query */
                x_mov_rax_imm32(qlen6); emit3(0x48,0x89,0xc6); /* rsi = qlen6 */
                emit4(0x48,0x8b,0x14,0x24); /* mov rdx,[rsp] (peek port again) */
                int p6=x_call_unresolved(); add_call_patch(p6,"__ys_net_connect_host6");
                /* falls straight into j_a_ok's target with rax = the
                   AAAA attempt's result; the A-succeeded path jumps to
                   the exact same point with rax = its own fd, so either
                   way rax is correct by the time rsp gets cleaned up */

                x_patch_here(j_a_ok);
                emit4(0x48,0x83,0xc4,0x08); /* add rsp,8 (discard the pushed port) */
                break;
            }
            int off, len;
            if(ip_arg && ip_arg->kind==ND_STR){
                off=data_add_str(ip_arg->sval); len=ystrlen(ip_arg->sval);
            } else { off=data_add_str(""); len=0; } /* unsupported shape: fails to connect cleanly rather than miscompiling */
            if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
            emit1(0x50); /* push rax (port) */
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* lea rdi,[rip+off] */
            x_mov_rax_imm32(len); emit3(0x48,0x89,0xc6); /* mov rsi,rax (len) */
            emit1(0x5a); /* pop rdx (port) */
            int p=x_call_unresolved();
            add_call_patch(p,"__ys_net_connect");
            break;
        }
        /* y.net.send(sock, data) -> bytes written or -1. data MUST be a
           string literal, same reasoning as connect's ip above. */
        if(is_y_net && strcmp(fn,"send")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            Node *data_arg=(n->argc>base+1)?n->args[base+1]:NULL;
            int off, len;
            if(data_arg && data_arg->kind==ND_STR){ off=data_add_str(data_arg->sval); len=ystrlen(data_arg->sval); }
            else { off=data_add_str(""); len=0; }
            if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
            emit1(0x50); /* push rax (fd) */
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* lea rdi,[rip+off] */
            x_mov_rax_imm32(len); emit3(0x48,0x89,0xc6); /* mov rsi,rax (len) */
            emit1(0x5a); /* pop rdx (fd) */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_send");
            break;
        }
        /* y.net.recv_print(sock, maxlen) — reads up to maxlen bytes and
           prints them directly to stdout. This stands in for a
           value-returning recv() in native-compiled code: the native
           backend has no runtime string type to hand a received buffer
           back as a Yolish value (only compile-time string literals
           exist there), so "read and print" is the honest capability
           on offer for this batch, not a real y.net.recv(sock,maxlen)
           returning a string. */
        if(is_y_net && strcmp(fn,"recv_print")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            Node *maxlen_arg=(n->argc>base+1)?n->args[base+1]:NULL;
            if(maxlen_arg) compile_expr(maxlen_arg); else x_mov_rax_imm32(1024);
            emit1(0x50); /* push rax (maxlen) */
            if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
            emit3(0x48,0x89,0xc7); /* mov rdi,rax (fd) */
            emit1(0x5e); /* pop rsi (maxlen) */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_recv_print");
            break;
        }
        /* y.net.close(sock) */
        if(is_y_net && strcmp(fn,"close")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            if(sock_arg){ compile_expr(sock_arg); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close");
            break;
        }
        /* y.net.listen(port) -> fd or -1 */
        if(is_y_net && strcmp(fn,"listen")==0){
            int base=n->left?1:0;
            Node *port_arg=(n->argc>base)?n->args[base]:NULL;
            if(port_arg){ compile_expr(port_arg); x_arg1_from_rax(); } else { x_mov_rax_imm32(0); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_listen");
            break;
        }
        /* y.net.accept(server_sock) -> fd or -1 */
        if(is_y_net && strcmp(fn,"accept")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            if(sock_arg){ compile_expr(sock_arg); x_arg1_from_rax(); } else { x_mov_rax_imm32(-1); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_accept");
            break;
        }
        /* y.net.udp_socket() -> fd or -1 */
        if(is_y_net && strcmp(fn,"udp_socket")==0){
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_socket");
            break;
        }
        /* y.net.udp_bind(port) -> fd or -1 */
        if(is_y_net && strcmp(fn,"udp_bind")==0){
            int base=n->left?1:0;
            Node *port_arg=(n->argc>base)?n->args[base]:NULL;
            if(port_arg){ compile_expr(port_arg); x_arg1_from_rax(); } else { x_mov_rax_imm32(0); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_bind");
            break;
        }
        /* y.net.udp_send(sock, host, port, data) -> bytes sent or -1.
           host and data MUST be string literals (host is IPv4-literal
           only for now — see __ys_net_udp_send's doc comment). All six
           values are staged onto the stack in the reverse of the order
           they're needed, then popped straight into the target ABI
           (rdi=sock, rsi=host_ptr, rdx=host_len, rcx=port, r8=data_ptr,
           r9=data_len) — robust regardless of what compile_expr does
           internally for sock/port, the only two pieces that aren't
           already compile-time constants. */
        /* y.net.udp_send(sock, host, port, data) -> bytes sent or -1.
           host may be an IPv4-dotted-decimal literal (existing path,
           __ys_net_udp_send, unchanged below) or a hostname literal
           (new: resolves via __ys_net_udp_send_host, built alongside
           TLS server support). Both are decided here at compile time —
           host must be a literal either way, so which runtime helper
           to call is already known before any code is emitted. */
        if(is_y_net && strcmp(fn,"udp_send")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            Node *host_arg=(n->argc>base+1)?n->args[base+1]:NULL;
            Node *port_arg=(n->argc>base+2)?n->args[base+2]:NULL;
            Node *data_arg=(n->argc>base+3)?n->args[base+3]:NULL;

            int doff,dlen;
            if(data_arg && data_arg->kind==ND_STR){ doff=data_add_str(data_arg->sval); dlen=ystrlen(data_arg->sval); }
            else { doff=data_add_str(""); dlen=0; }

            int is_hostname = host_arg && host_arg->kind==ND_STR && !is_ipv4_literal(host_arg->sval);
            if(is_hostname){
                uint8_t qbuf[512]; int qlen=build_dns_query(host_arg->sval,qbuf);
                int qoff=data_add_bytes(qbuf,qlen);

                x_mov_rax_imm32(dlen); emit1(0x50);                  /* push data_len */
                emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,doff); emit_i32(0); emit1(0x50); /* push data_ptr */
                if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
                emit1(0x50);                                          /* push sock */
                if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
                emit1(0x50);                                          /* push port */
                x_mov_rax_imm32(qlen); emit1(0x50);                   /* push query_len */
                emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,qoff); emit_i32(0); emit1(0x50); /* push query_ptr */

                emit1(0x5f); /* pop rdi = query_ptr */
                emit1(0x5e); /* pop rsi = query_len */
                emit1(0x5a); /* pop rdx = port */
                emit1(0x59); /* pop rcx = sock */
                emit2(0x41,0x58); /* pop r8 = data_ptr */
                emit2(0x41,0x59); /* pop r9 = data_len */
                int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_send_host");
                break;
            }

            int hoff,hlen;
            if(host_arg && host_arg->kind==ND_STR){ hoff=data_add_str(host_arg->sval); hlen=ystrlen(host_arg->sval); }
            else { hoff=data_add_str(""); hlen=0; }

            x_mov_rax_imm32(dlen); emit1(0x50);                  /* push data_len */
            emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,doff); emit_i32(0); emit1(0x50); /* push data_ptr */
            if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
            emit1(0x50);                                          /* push port */
            x_mov_rax_imm32(hlen); emit1(0x50);                   /* push host_len */
            emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,hoff); emit_i32(0); emit1(0x50); /* push host_ptr */
            if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
            emit1(0x50);                                          /* push sock */

            emit1(0x5f); /* pop rdi = sock */
            emit1(0x5e); /* pop rsi = host_ptr */
            emit1(0x5a); /* pop rdx = host_len */
            emit1(0x59); /* pop rcx = port */
            emit2(0x41,0x58); /* pop r8 = data_ptr */
            emit2(0x41,0x59); /* pop r9 = data_len */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_send");
            break;
        }
        /* y.net.udp_recv_print(sock, maxlen) -> bytes received or -1,
           payload printed to stdout. Sender's address is read by
           recvfrom internally but discarded — see udp_recv_reply_print
           below for the variant that uses it. */
        if(is_y_net && strcmp(fn,"udp_recv_print")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            Node *maxlen_arg=(n->argc>base+1)?n->args[base+1]:NULL;
            if(maxlen_arg) compile_expr(maxlen_arg); else x_mov_rax_imm32(1024);
            emit1(0x50); /* push maxlen */
            if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
            emit3(0x48,0x89,0xc7); /* mov rdi,rax (sock) */
            emit1(0x5e); /* pop rsi (maxlen) */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_recv_print");
            break;
        }
        /* y.net.udp_recv_reply_print(sock, maxlen, reply_data) -> bytes
           received or -1. Reads one datagram (printing its payload,
           same as udp_recv_print), then sends reply_data back to
           whichever address it just arrived from. reply_data MUST be a
           string literal, same reasoning as data in udp_send/send. */
        if(is_y_net && strcmp(fn,"udp_recv_reply_print")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            Node *maxlen_arg=(n->argc>base+1)?n->args[base+1]:NULL;
            Node *reply_arg=(n->argc>base+2)?n->args[base+2]:NULL;

            int roff,rlen;
            if(reply_arg && reply_arg->kind==ND_STR){ roff=data_add_str(reply_arg->sval); rlen=ystrlen(reply_arg->sval); }
            else { roff=data_add_str(""); rlen=0; }

            x_mov_rax_imm32(rlen); emit1(0x50);                  /* push reply_len */
            emit3(0x48,0x8d,0x05); add_reloc(RELOC_DATA,code_len,roff); emit_i32(0); emit1(0x50); /* push reply_ptr */
            if(maxlen_arg) compile_expr(maxlen_arg); else x_mov_rax_imm32(1024);
            emit1(0x50);                                          /* push maxlen */
            if(sock_arg) compile_expr(sock_arg); else x_mov_rax_imm32(-1);
            emit1(0x50);                                          /* push sock */

            emit1(0x5f); /* pop rdi = sock */
            emit1(0x5e); /* pop rsi = maxlen */
            emit1(0x5a); /* pop rdx = reply_ptr */
            emit1(0x59); /* pop rcx = reply_len */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_udp_recv_reply_print");
            break;
        }
        /* y.net.udp_close(sock) — close(fd) is generic regardless of
           socket type, so this reuses __ys_net_close directly rather
           than defining an identical duplicate under a different name. */
        if(is_y_net && strcmp(fn,"udp_close")==0){
            int base=n->left?1:0;
            Node *sock_arg=(n->argc>base)?n->args[base]:NULL;
            if(sock_arg){ compile_expr(sock_arg); x_arg1_from_rax(); }
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close");
            break;
        }
        /* y.net.dynlink_test() — proof-of-concept native call into a
           real shared-library function (libc.so.6's puts, followed by
           its exit), exercising the PT_INTERP/PT_DYNAMIC machinery
           end to end through the real compiler rather than just the
           standalone prototype it was validated against. Narrow and
           specific on purpose: marshaling arbitrary arguments/return
           types for arbitrary imported functions is a separate,
           much bigger design problem (a general FFI) than what this
           establishes, which is that the ELF-writing side works.
           Calls exit() (imported) rather than __ys_exit's raw exit
           syscall deliberately — puts() buffers its output, and a raw
           syscall skips glibc's own stdio-flush machinery, so the
           message would never actually appear even though nothing
           would crash (this exact mistake happened in the standalone
           prototype and is why the message wasn't showing up there
           at first). */
        if(is_y_net && strcmp(fn,"dynlink_test")==0){
            int puts_got = dynlink_import("puts");
            int exit_got = dynlink_import("exit");
            int msg_off = data_add_str("hello from dynamically-linked Yolish!");

            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,msg_off); emit_i32(0); /* rdi=&msg */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0); /* r11=&puts_got */
            emit3(0x49,0x8b,0x03); /* rax=[r11] */
            emit2(0xff,0xd0);      /* call rax (puts) */

            x_mov_rax_imm32(0);
            emit3(0x48,0x89,0xc7); /* rdi=0 */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,exit_got); emit_i32(0); /* r11=&exit_got */
            emit3(0x49,0x8b,0x03);
            emit2(0xff,0xd0);      /* call rax (exit(0) — never returns) */
            break;
        }
        /* y.net.tls_handshake_test() — full handshake proof-of-concept:
           TCP connect (reusing the exact same DNS-query-building and
           __ys_net_connect_host call the real y.net.connect() call site
           uses) to a hardcoded real HTTPS host, then
           TLS_client_method -> SSL_CTX_new -> SSL_new -> SSL_set_fd ->
           SSL_connect. Prints whether the handshake actually completed
           (SSL_connect returns 1 on success). Hardcoded host/port and
           no cleanup/send/recv yet, deliberately — this is validating
           that the sequence of calls with each one's return value
           feeding the next works at all, before building the general
           tls_connect/tls_send/tls_recv_print/tls_close API on top. */
        if(is_y_net && strcmp(fn,"tls_handshake_test")==0){
            dynlink_need_library("libssl.so.3");
            int init_got=dynlink_import("OPENSSL_init_ssl");
            int method_got=dynlink_import("TLS_client_method");
            int ctxnew_got=dynlink_import("SSL_CTX_new");
            int sslnew_got=dynlink_import("SSL_new");
            int setfd_got=dynlink_import("SSL_set_fd");
            int sslconnect_got=dynlink_import("SSL_connect");
            int puts_got=dynlink_import("puts");
            int exit_got=dynlink_import("exit");

            uint8_t qbuf[512];
            int qlen=build_dns_query("example.com", qbuf);
            int qoff=data_add_bytes(qbuf,qlen);
            int ok_off=data_add_str("TLS handshake succeeded!");
            int fail_off=data_add_str("TLS handshake failed");
            int noconn_off=data_add_str("TCP connect failed");

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(32); /* -8 fd, -16 method, -24 ctx, -32 ssl */

            /* OPENSSL_init_ssl(0,0) */
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7);
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc6);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,init_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            /* TCP connect to example.com:443 (A record only, no AAAA
               fallback here — this test is about the TLS chain, not
               re-proving dual-stack DNS) */
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,qoff); emit_i32(0); /* rdi=&query */
            x_mov_rax_imm32(qlen); emit3(0x48,0x89,0xc6); /* rsi=qlen */
            x_mov_rax_imm32(443); emit3(0x48,0x89,0xc2); /* rdx=443 */
            int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect_host");
            x_mov_rbpN_r64(-8,0);
            emit4(0x48,0x83,0xf8,0x00);
            int j_conn_ok=x_jge_rel32();

            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,noconn_off); emit_i32(0);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            int j_to_end=x_jmp_rel32();

            x_patch_here(j_conn_ok);
            /* TLS_client_method() */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,method_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-16,0);

            /* SSL_CTX_new(method) */
            x_mov_r64_rbpN(7,-16);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,ctxnew_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-24,0);

            /* SSL_new(ctx) */
            x_mov_r64_rbpN(7,-24);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslnew_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-32,0);

            /* SSL_set_fd(ssl, fd) */
            x_mov_r64_rbpN(7,-32);
            x_mov_r64_rbpN(0,-8); emit3(0x48,0x89,0xc6);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,setfd_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            /* SSL_connect(ssl) */
            x_mov_r64_rbpN(7,-32);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslconnect_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            emit4(0x48,0x83,0xf8,0x01);
            int j_hs_ok=x_jz_rel32();
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,fail_off); emit_i32(0);
            int j_to_print=x_jmp_rel32();
            x_patch_here(j_hs_ok);
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,ok_off); emit_i32(0);
            x_patch_here(j_to_print);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            x_patch_here(j_to_end);
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,exit_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            break;
        }
        /* y.net.tls_get_test() — one step further than
           tls_handshake_test: after the handshake succeeds, sends a
           real HTTPS GET over SSL_write and prints whatever SSL_read
           decrypts back, proving actual encrypted data transfer works
           end to end, not just the handshake. Same hardcoded-host
           scope as tls_handshake_test, same reasoning for staying
           narrow before generalizing into a public tls_connect/
           tls_send/tls_recv_print API. */
        if(is_y_net && strcmp(fn,"tls_get_test")==0){
            dynlink_need_library("libssl.so.3");
            int init_got=dynlink_import("OPENSSL_init_ssl");
            int method_got=dynlink_import("TLS_client_method");
            int ctxnew_got=dynlink_import("SSL_CTX_new");
            int sslnew_got=dynlink_import("SSL_new");
            int setfd_got=dynlink_import("SSL_set_fd");
            int sslconnect_got=dynlink_import("SSL_connect");
            int sslwrite_got=dynlink_import("SSL_write");
            int sslread_got=dynlink_import("SSL_read");
            int puts_got=dynlink_import("puts");
            int exit_got=dynlink_import("exit");

            uint8_t qbuf[512];
            int qlen=build_dns_query("example.com", qbuf);
            int qoff=data_add_bytes(qbuf,qlen);
            int fail_off=data_add_str("TLS handshake failed");
            int noconn_off=data_add_str("TCP connect failed");
            int req_off=data_add_str("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
            int req_len=ystrlen("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");
            int rb_off=data_len; static const int RB_CAP=4096;
            for(int i=0;i<RB_CAP;i++) data_buf[data_len++]=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(32); /* -8 fd, -16 method, -24 ctx, -32 ssl */

            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7);
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc6);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,init_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,qoff); emit_i32(0);
            x_mov_rax_imm32(qlen); emit3(0x48,0x89,0xc6);
            x_mov_rax_imm32(443); emit3(0x48,0x89,0xc2);
            int p2=x_call_unresolved(); add_call_patch(p2,"__ys_net_connect_host");
            x_mov_rbpN_r64(-8,0);
            emit4(0x48,0x83,0xf8,0x00);
            int j2_conn_ok=x_jge_rel32();
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,noconn_off); emit_i32(0);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            int j2_to_end=x_jmp_rel32();

            x_patch_here(j2_conn_ok);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,method_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-16,0);

            x_mov_r64_rbpN(7,-16);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,ctxnew_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-24,0);

            x_mov_r64_rbpN(7,-24);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslnew_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-32,0);

            x_mov_r64_rbpN(7,-32);
            x_mov_r64_rbpN(0,-8); emit3(0x48,0x89,0xc6);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,setfd_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            x_mov_r64_rbpN(7,-32);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslconnect_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            emit4(0x48,0x83,0xf8,0x01);
            int j2_hs_ok=x_jz_rel32();
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,fail_off); emit_i32(0);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            int j2_to_end2=x_jmp_rel32();

            x_patch_here(j2_hs_ok);
            /* SSL_write(ssl, req_ptr, req_len) */
            x_mov_r64_rbpN(7,-32);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,req_off); emit_i32(0); /* rsi=&req */
            x_mov_rax_imm32(req_len); emit3(0x48,0x89,0xc2); /* rdx=req_len */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslwrite_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            /* SSL_read(ssl, respbuf, RB_CAP-1) */
            x_mov_r64_rbpN(7,-32);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,rb_off); emit_i32(0); /* rsi=&respbuf */
            x_mov_rax_imm32(RB_CAP-1); emit3(0x48,0x89,0xc2);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,sslread_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            /* NUL-terminate at [respbuf+n] so puts() stops at the real
               end of what SSL_read actually decrypted, then print it */
            emit3(0x48,0x8d,0x0d); add_reloc(RELOC_DATA,code_len,rb_off); emit_i32(0); /* rcx=&respbuf */
            emit3(0x48,0x01,0xc8); /* rax += rcx (rax = &respbuf[n]) */
            emit3(0xc6,0x00,0x00); /* mov byte[rax],0 */

            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,rb_off); emit_i32(0); /* rdi=&respbuf */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            x_patch_here(j2_to_end); x_patch_here(j2_to_end2);
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,exit_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            break;
        }
        /* y.net.tls_test() — narrower proof-of-concept than
           tls_handshake_test above: just checks whether this backend's
           simple unversioned R_X86_64_GLOB_DAT symbol resolution works
           at all against libssl.so.3's versioned exports (OpenSSL 3.0
           tags everything like OPENSSL_init_ssl@@OPENSSL_3.0.0), with
           no socket/handshake involved yet — kept as the minimal
           isolated check that caught the versioned-symbol question
           before the full chain above was built on top of it. */
        if(is_y_net && strcmp(fn,"tls_test")==0){
            dynlink_need_library("libssl.so.3");
            int init_got=dynlink_import("OPENSSL_init_ssl");
            int method_got=dynlink_import("TLS_client_method");
            int puts_got=dynlink_import("puts");
            int exit_got=dynlink_import("exit");
            int ok_off=data_add_str("libssl linked and callable");
            int fail_off=data_add_str("libssl call returned null — versioned symbol resolution may have failed");

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(16); /* sub rsp,16 */

            /* OPENSSL_init_ssl(0, NULL) */
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7); /* rdi=0 */
            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc6); /* rsi=0 */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,init_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            /* TLS_client_method() -> rax */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,method_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            x_mov_rbpN_r64(-8,0); /* save method ptr */

            x_mov_r64_rbpN(0,-8);
            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_ok=x_jnz_rel32();

            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,fail_off); emit_i32(0);
            int j_after=x_jmp_rel32();
            x_patch_here(j_ok);
            emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,ok_off); emit_i32(0);
            x_patch_here(j_after);

            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,puts_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);

            x_mov_rax_imm32(0); emit3(0x48,0x89,0xc7);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,exit_got); emit_i32(0);
            emit3(0x49,0x8b,0x03); emit2(0xff,0xd0);
            break;
        }
        /* y.net.tls_connect(host, port) -> handle (0..YS_TLS_MAX_CONN-1) or
           -1. Generalizes tls_handshake_test above into a real, reusable
           connection: resolves host the same three ways y.net.connect does
           (IPv6 literal / IPv4 literal / hostname via DNS), completes the
           TLS handshake, and stores {fd,ctx,ssl} into a round-robin slot in
           the fixed-size table declared near tls_state_ensure, returning
           the slot index as the handle.

           Every intermediate value (fd, method, ctx, ssl, port, the handle
           itself) lives in an rbp-relative slot for the whole function,
           never in a register carried across a `call` — this is what fixes
           the two real bugs an earlier attempt at this exact function hit:
             1. Stack-alignment corruption: that attempt pushed the port
                argument onto the stack to marshal it into a few different
                call sites, the same way y.net.connect above still does.
                One push is 8 bytes — an odd number of them before a `call`
                into libssl leaves rsp 8-byte-but-not-16-byte aligned, and
                OpenSSL's SIMD-using internals can fault on that. This
                function never pushes/pops at all: it opens one fixed-size
                frame (sub rsp,48, a multiple of 16) at entry and only ever
                stores into fixed slots inside it, so rsp only moves twice
                in the whole function (entry and exit), both by multiples
                of 16 — misalignment isn't reachable.
             2. The repeated `lea r11,[rip+got]; mov rax,[r11]; call rax`
                sequence used to be hand-transcribed at every call site;
                x_call_got() above writes it exactly once now. */
        if(is_y_net && strcmp(fn,"tls_connect")==0){
            if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); break; } /* ELF-dynlink-only */
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int init_got=dynlink_import("OPENSSL_init_ssl");
            int method_got=dynlink_import("TLS_client_method");
            int ctxnew_got=dynlink_import("SSL_CTX_new");
            int sslnew_got=dynlink_import("SSL_new");
            int setfd_got=dynlink_import("SSL_set_fd");
            int sslconnect_got=dynlink_import("SSL_connect");
            int ctxfree_got=dynlink_import("SSL_CTX_free");
            int sslfree_got=dynlink_import("SSL_free");
            int sslctrl_got=dynlink_import("SSL_ctrl");
            int setverify_got=dynlink_import("SSL_CTX_set_verify");
            int setdefverify_got=dynlink_import("SSL_CTX_set_default_verify_paths");
            int ctxctrl_got=dynlink_import("SSL_CTX_ctrl");
            int set1host_got=dynlink_import("SSL_set1_host");
            int getverifyresult_got=dynlink_import("SSL_get_verify_result");

            int cbase=n->left?1:0;
            Node *host_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            Node *port_arg=(n->argc>cbase+1)?n->args[cbase+1]:NULL;

            int fail_patches[8]; int nfail=0;

            /* Evaluate port BEFORE opening our own nested frame below —
               compile_expr() resolves a variable argument against
               whichever rbp is live *right now*; do it after "push rbp"
               and it resolves against this function's own fresh frame
               instead of the caller's, reading garbage (see
               x_store_rip_slot's comment for the full story — this is
               the exact bug an earlier pass of this rebuild hit). */
            if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
            x_store_rip_slot(g_tls_argbuf_off+0);

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(48); /* -8 fd -16 method -24 ctx -32 ssl -40 port -48 handle */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-40,0); /* port */

            /* OPENSSL_init_ssl(0,0) */
            x_mov_r64_imm32(7,0); x_mov_r64_imm32(6,0);
            x_call_got(init_got);

            /* TCP connect — same three address shapes as y.net.connect,
               reading port from [rbp-40] via a register move instead of
               push/pop so no call site here can desync rsp's alignment. */
            uint8_t ip6buf[16];
            int is_ipv6 = host_arg && host_arg->kind==ND_STR &&
                          is_ipv6_literal(host_arg->sval) &&
                          parse_ipv6_literal(host_arg->sval, ip6buf);
            int sni_off=-1;
            if(is_ipv6){
                int off = data_add_bytes(ip6buf, 16);
                emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* rdi=&addr */
                x_mov_r64_rbpN(6,-40); /* rsi=port */
                int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect6");
            } else {
                int is_hostname_local = host_arg && host_arg->kind==ND_STR && !is_ipv4_literal(host_arg->sval);
                if(is_hostname_local) sni_off = data_add_str(host_arg->sval);
                if(is_hostname_local){
                    uint8_t qbuf[512]; int qlen=build_dns_query(host_arg->sval, qbuf);
                    int qoff=data_add_bytes(qbuf,qlen);
                    emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,qoff); emit_i32(0); /* rdi=&query */
                    x_mov_r64_imm32(6,qlen);  /* rsi=qlen */
                    x_mov_r64_rbpN(2,-40);    /* rdx=port */
                    int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect_host");
                } else {
                    int off,len2;
                    if(host_arg && host_arg->kind==ND_STR){ off=data_add_str(host_arg->sval); len2=ystrlen(host_arg->sval); }
                    else { off=data_add_str(""); len2=0; }
                    emit3(0x48,0x8d,0x3d); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* rdi=&addr */
                    x_mov_r64_imm32(6,len2);  /* rsi=len */
                    x_mov_r64_rbpN(2,-40);    /* rdx=port */
                    int p=x_call_unresolved(); add_call_patch(p,"__ys_net_connect");
                }
            }
            x_mov_rbpN_r64(-8,0); /* fd */
            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_conn_ok=x_jge_rel32();
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_conn_ok);

            /* TLS_client_method() */
            x_call_got(method_got);
            x_mov_rbpN_r64(-16,0);

            /* SSL_CTX_new(method) */
            x_mov_r64_rbpN(7,-16);
            x_call_got(ctxnew_got);
            x_mov_rbpN_r64(-24,0);
            x_test_rax_rax();
            int j_ctx_ok=x_jnz_rel32();
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_ctx_ok);

            /* Certificate verification, enabled here to match the
               interpreter/VM version's already-tested approach exactly
               (see net_runtime.c's ys_tls_ensure_ctx) -- this native
               path trusted whatever certificate a server presented
               until now, which the docs called out plainly as a real
               gap rather than something to leave silently assumed. */
            x_mov_r64_rbpN(7,-24);              /* rdi=ctx */
            x_mov_r64_imm32(6,1);               /* rsi=SSL_VERIFY_PEER */
            x_mov_r64_imm32(2,0);               /* rdx=NULL (no custom callback) */
            x_call_got(setverify_got);

            x_mov_r64_rbpN(7,-24);
            x_mov_r64_imm32(6,123);             /* rsi=SSL_CTRL_SET_MIN_PROTO_VERSION */
            x_mov_r64_imm32(2,0x0303);          /* rdx=TLS1_2_VERSION */
            x_mov_r64_imm32(1,0);               /* rcx=NULL (unused for this ctrl) */
            x_call_got(ctxctrl_got);

            x_mov_r64_rbpN(7,-24);
            x_call_got(setdefverify_got);
            emit4(0x48,0x83,0xf8,0x01);         /* cmp rax,1 */
            int j_verifypaths_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_verifypaths_ok);

            /* SSL_new(ctx) */
            x_mov_r64_rbpN(7,-24);
            x_call_got(sslnew_got);
            x_mov_rbpN_r64(-32,0);
            x_test_rax_rax();
            int j_ssl_ok=x_jnz_rel32();
            x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_ssl_ok);

            /* SNI: SSL_set_tlsext_host_name(ssl, host) -- a macro over
               SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME=55,
               TLSEXT_NAMETYPE_host_name=0, host); these two constants have
               been ABI-stable across OpenSSL 1.0.x-3.x. Without this, many
               real name-based-virtual-hosting servers either hand back a
               default/unrelated certificate or refuse the handshake
               outright -- a real gap the first version of this function
               had, silently working against some hosts (a CDN happy to
               serve *something* without SNI) and failing against others
               (a host that isn't). Only sent for the hostname branch,
               since sending an IP-literal as SNI isn't meaningful. */
            if(sni_off>=0){
                x_mov_r64_rbpN(7,-32);             /* rdi=ssl */
                x_mov_r64_imm32(6,55);              /* rsi=SSL_CTRL_SET_TLSEXT_HOSTNAME */
                x_mov_r64_imm32(2,0);               /* rdx=TLSEXT_NAMETYPE_host_name */
                emit3(0x48,0x8d,0x0d); add_reloc(RELOC_DATA,code_len,sni_off); emit_i32(0); /* rcx=&host */
                x_call_got(sslctrl_got);

                /* SSL_set1_host(ssl, host) -- checks the presented
                   certificate's subject/SAN actually matches this
                   hostname during verification, not just that it
                   chains to a trusted root. Without this, a valid cert
                   for an entirely unrelated domain would still pass. */
                x_mov_r64_rbpN(7,-32);
                emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,sni_off); emit_i32(0); /* rsi=&host */
                x_call_got(set1host_got);
            }

            /* SSL_set_fd(ssl, fd) */
            x_mov_r64_rbpN(7,-32);
            x_mov_r64_rbpN(6,-8);
            x_call_got(setfd_got);
            emit4(0x48,0x83,0xf8,0x01); /* cmp rax,1 */
            int j_setfd_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
            x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_setfd_ok);

            /* SSL_connect(ssl) */
            x_mov_r64_rbpN(7,-32);
            x_call_got(sslconnect_got);
            emit4(0x48,0x83,0xf8,0x01); /* cmp rax,1 */
            int j_hs_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
            x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_hs_ok);

            /* SSL_connect()==1 only means the handshake completed, not
               that the certificate presented was actually trusted --
               SSL_VERIFY_PEER above makes OpenSSL run the check, but
               checking the *result* is a separate, deliberate step
               (SSL_get_verify_result), same as the interpreter/VM
               version does. Skipping this line would silently accept
               any certificate, self-signed or otherwise, regardless of
               everything set up above. */
            x_mov_r64_rbpN(7,-32);
            x_call_got(getverifyresult_got);
            x_test_rax_rax(); /* X509_V_OK == 0 */
            int j_verify_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-32); x_call_got(sslfree_got);
            x_mov_r64_rbpN(7,-24); x_call_got(ctxfree_got);
            x_mov_r64_rbpN(0,-8); x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_verify_ok);

            /* handshake succeeded — allocate a handle (round-robin counter,
               MAX_TLS_CONN is a power of 2 so "mod" is just an AND) and
               store {fd,ctx,ssl} into that slot. */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tls_next_off); emit_i32(0); /* r11=&next */
            emit3(0x49,0x8b,0x03); /* rax=[r11] (handle to use) */
            x_mov_rbpN_r64(-48,0); /* save handle */
            emit3(0x48,0x89,0xc1); /* mov rcx,rax */
            emit3(0x48,0xff,0xc1); /* inc rcx */
            emit4(0x48,0x83,0xe1,(YS_TLS_MAX_CONN-1)); /* and rcx,3 */
            emit3(0x49,0x89,0x0b); /* mov [r11],rcx */

            x_mov_r64_rbpN(0,-48); /* rax=handle */
            x_tls_slot_addr();     /* rax=&table[handle] */
            x_mov_r64_rbpN(1,-8);  x_mov_rax8_r64(0,1);  /* [rax+0]=fd */
            x_mov_r64_rbpN(1,-24); x_mov_rax8_r64(8,1);  /* [rax+8]=ctx */
            x_mov_r64_rbpN(1,-32); x_mov_rax8_r64(16,1); /* [rax+16]=ssl */

            x_mov_r64_rbpN(0,-48); /* rax=handle (final return value) */

            for(int i=0;i<nfail;i++) x_patch_here(fail_patches[i]);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.net.tls_send(handle, data) -> bytes written or -1. data MUST be
           a string literal, same limitation as y.net.send's data argument.
           handle is bounds-checked before it's ever used to index the
           table, so an out-of-range value fails cleanly instead of reading
           past it. */
        if(is_y_net && strcmp(fn,"tls_send")==0){
            if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); break; }
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int sslwrite_got=dynlink_import("SSL_write");

            int cbase=n->left?1:0;
            Node *handle_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            Node *data_arg=(n->argc>cbase+1)?n->args[cbase+1]:NULL;
            int off,len2;
            if(data_arg && data_arg->kind==ND_STR){ off=data_add_str(data_arg->sval); len2=ystrlen(data_arg->sval); }
            else { off=data_add_str(""); len2=0; }

            if(handle_arg) compile_expr(handle_arg); else x_mov_rax_imm32(-1);
            x_store_rip_slot(g_tls_argbuf_off+0);

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(16); /* -8 handle */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-8,0);

            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_ge0=x_jge_rel32();
            x_mov_rax_imm32(-1); int f1=x_jmp_rel32();
            x_patch_here(j_ge0);
            emit4(0x48,0x83,0xf8,YS_TLS_MAX_CONN); /* cmp rax,4 */
            int j_lt=x_jl_rel32();
            x_mov_rax_imm32(-1); int f2=x_jmp_rel32();
            x_patch_here(j_lt);

            x_mov_r64_rbpN(0,-8); /* rax=handle */
            x_tls_slot_addr();    /* rax=&table[handle] */
            x_mov_r64_rax8(0,16); /* rax=ssl */
            x_test_rax_rax();
            int j_have_ssl=x_jnz_rel32();
            x_mov_rax_imm32(-1); int f3=x_jmp_rel32();
            x_patch_here(j_have_ssl);

            emit3(0x48,0x89,0xc7); /* rdi=ssl (mov rdi,rax) */
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,off); emit_i32(0); /* rsi=&data */
            x_mov_r64_imm32(2,len2); /* rdx=len */
            x_call_got(sslwrite_got);

            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_ok=x_jg_rel32();
            x_mov_rax_imm32(-1); /* normalize SSL_write's <=0 error returns to -1 */
            x_patch_here(j_ok);

            x_patch_here(f1); x_patch_here(f2); x_patch_here(f3);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.net.tls_recv_print(handle, maxlen) -> bytes received or -1,
           payload printed to stdout — mirrors y.net.recv_print's "read and
           print" shape for the same reason (no runtime string type to hand
           a decrypted buffer back as a value). maxlen is clamped to the
           scratch buffer's real capacity before it ever reaches SSL_read,
           so a large maxlen can't overflow it.

           Prints via a raw SYS_write(1,...) syscall, the same way
           __ys_net_recv_print above does — NOT libc's puts(), which is
           what tls_get_test uses. That's deliberate: puts() is buffered,
           and this program's normal (non-tls_get_test) end-of-program
           path is a raw exit syscall rather than a call into libc's own
           exit(), which is what would normally flush stdio on the way
           out. tls_get_test gets away with puts() only because it always
           calls libc exit() explicitly right after; a function meant to
           return normally, like this one, can't rely on that — a real
           bug this rebuild's testing caught (the payload was actually
           being received correctly; it just never made it to the
           terminal). */
        if(is_y_net && strcmp(fn,"tls_recv_print")==0){
            if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); break; }
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int sslread_got=dynlink_import("SSL_read");

            int cbase=n->left?1:0;
            Node *handle_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            Node *maxlen_arg=(n->argc>cbase+1)?n->args[cbase+1]:NULL;

            if(handle_arg) compile_expr(handle_arg); else x_mov_rax_imm32(-1);
            x_store_rip_slot(g_tls_argbuf_off+0);
            if(maxlen_arg) compile_expr(maxlen_arg); else x_mov_rax_imm32(YS_TLS_RBUF_CAP-1);
            x_store_rip_slot(g_tls_argbuf_off+8);

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(32); /* -8 handle -16 maxlen -24 n */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-8,0);

            x_load_rip_slot(g_tls_argbuf_off+8);
            emit2(0x48,0x3d); emit_i32(YS_TLS_RBUF_CAP-1); /* cmp rax,4095 */
            int j_len_ok=x_jle_rel32();
            x_mov_rax_imm32(YS_TLS_RBUF_CAP-1); /* clamp */
            x_patch_here(j_len_ok);
            x_mov_rbpN_r64(-16,0);

            int fail_patches2[4]; int nfail2=0;
            x_mov_r64_rbpN(0,-8); /* rax=handle */
            emit4(0x48,0x83,0xf8,0x00);
            int j_ge0=x_jge_rel32();
            x_mov_rax_imm32(-1); fail_patches2[nfail2++]=x_jmp_rel32();
            x_patch_here(j_ge0);
            emit4(0x48,0x83,0xf8,YS_TLS_MAX_CONN);
            int j_lt=x_jl_rel32();
            x_mov_rax_imm32(-1); fail_patches2[nfail2++]=x_jmp_rel32();
            x_patch_here(j_lt);

            x_mov_r64_rbpN(0,-8);
            x_tls_slot_addr();
            x_mov_r64_rax8(0,16); /* rax=ssl */
            x_test_rax_rax();
            int j_have_ssl=x_jnz_rel32();
            x_mov_rax_imm32(-1); fail_patches2[nfail2++]=x_jmp_rel32();
            x_patch_here(j_have_ssl);

            emit3(0x48,0x89,0xc7); /* rdi=ssl */
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0); /* rsi=&rbuf */
            x_mov_r64_rbpN(2,-16); /* rdx=maxlen (clamped) */
            x_call_got(sslread_got);

            x_mov_rbpN_r64(-24,0); /* save n */
            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_have_data=x_jg_rel32();
            x_mov_rax_imm32(-1); fail_patches2[nfail2++]=x_jmp_rel32();
            x_patch_here(j_have_data);

            /* write(1, &rbuf, n) -- raw syscall, see comment above */
            x_mov_r64_imm32(7,1); /* rdi=1 */
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,g_tls_rbuf_off); emit_i32(0); /* rsi=&rbuf */
            x_mov_r64_rbpN(2,-24); /* rdx=n */
            x_mov_r64_imm32(0,1);  /* SYS_write */
            emit2(0x0f,0x05);
            x_mov_r64_rbpN(0,-24); /* rax=n (final return value, not write()'s retval) */

            for(int i=0;i<nfail2;i++) x_patch_here(fail_patches2[i]);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.net.tls_close(handle) -> 0. Frees the OpenSSL objects and
           closes the socket for handle's slot, then zeroes the slot.

           Every value needed after a call is read back out of the
           [rbp-16] slot-address slot, never out of a register: SSL_free()
           clobbers every caller-saved register (RCX included, and RCX is
           exactly what an earlier attempt at this function was holding
           &slot in), so whatever's needed for the field access right
           after a call has to survive in memory, not in a register. */
        if(is_y_net && strcmp(fn,"tls_close")==0){
            if(g_target!=TARGET_LINUX){ break; }
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int sslfree_got=dynlink_import("SSL_free");
            int ctxfree_got=dynlink_import("SSL_CTX_free");

            int cbase=n->left?1:0;
            Node *handle_arg=(n->argc>cbase)?n->args[cbase]:NULL;

            if(handle_arg) compile_expr(handle_arg); else x_mov_rax_imm32(-1);
            x_store_rip_slot(g_tls_argbuf_off+0);

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(16); /* -8 handle -16 slot_addr */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-8,0);

            emit4(0x48,0x83,0xf8,0x00);
            int j_ge0=x_jge_rel32();
            int fskip1=x_jmp_rel32();
            x_patch_here(j_ge0);
            emit4(0x48,0x83,0xf8,YS_TLS_MAX_CONN);
            int j_lt=x_jl_rel32();
            int fskip2=x_jmp_rel32();
            x_patch_here(j_lt);

            x_mov_r64_rbpN(0,-8);
            x_tls_slot_addr();
            x_mov_rbpN_r64(-16,0); /* save slot address -- never trust a register across the calls below */

            /* SSL_free(ssl) — SSL_free(NULL) is a documented no-op, so no
               branch needed on whether this handle was ever populated. */
            x_mov_r64_rbpN(0,-16); x_mov_r64_rax8(1,16); /* rcx=[slot+16]=ssl */
            emit3(0x48,0x89,0xcf); /* rdi=rcx */
            x_call_got(sslfree_got);

            /* SSL_CTX_free(ctx) — reload the slot address fresh; rax/rcx/
               rdi were all just clobbered by the call above. */
            x_mov_r64_rbpN(0,-16); x_mov_r64_rax8(1,8); /* rcx=[slot+8]=ctx */
            emit3(0x48,0x89,0xcf);
            x_call_got(ctxfree_got);

            /* close(fd) via the existing runtime helper — same reload
               discipline, no exceptions. */
            x_mov_r64_rbpN(0,-16); x_mov_r64_rax8(0,0); /* rax=[slot+0]=fd */
            x_arg1_from_rax();
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }

            /* zero the slot so a later call on this handle (before the
               round-robin counter wraps back to it) fails the ssl==0
               check instead of reusing freed pointers. */
            x_mov_r64_rbpN(0,-16);
            x_mov_r64_imm32(1,0);
            x_mov_rax8_r64(0,1); x_mov_rax8_r64(8,1); x_mov_rax8_r64(16,1);

            x_patch_here(fskip1); x_patch_here(fskip2);
            x_mov_rax_imm32(0);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.net.tls_listen(port, certfile, keyfile) -> server handle
           (0..YS_TLSSRV_MAX-1) or -1. certfile/keyfile MUST be string
           literals (PEM paths, loaded from disk at runtime) — same
           literal-argument constraint as host/data elsewhere in this
           file. Backed by its own small round-robin table
           (g_tlssrv_table_off, {listen_fd, ctx} per slot) — a genuinely
           separate handle space from the client-connection table above,
           since a listening socket + its SSL_CTX are long-lived and
           reused across many accepted connections, unlike a client
           connection's one-shot {fd, ctx, ssl}. */
        if(is_y_net && strcmp(fn,"tls_listen")==0){
            if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); break; }
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int method_got=dynlink_import("TLS_server_method");
            int ctxnew_got=dynlink_import("SSL_CTX_new");
            int usecert_got=dynlink_import("SSL_CTX_use_certificate_file");
            int usekey_got=dynlink_import("SSL_CTX_use_PrivateKey_file");
            int ctxfree_got=dynlink_import("SSL_CTX_free");

            int cbase=n->left?1:0;
            Node *port_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            Node *cert_arg=(n->argc>cbase+1)?n->args[cbase+1]:NULL;
            Node *key_arg=(n->argc>cbase+2)?n->args[cbase+2]:NULL;
            int cert_off = (cert_arg && cert_arg->kind==ND_STR) ? data_add_str(cert_arg->sval) : data_add_str("");
            int key_off  = (key_arg && key_arg->kind==ND_STR)  ? data_add_str(key_arg->sval)  : data_add_str("");

            if(port_arg) compile_expr(port_arg); else x_mov_rax_imm32(0);
            x_store_rip_slot(g_tls_argbuf_off+0);

            int fail_patches[6]; int nfail=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(48); /* -8 port -16 ctx -24 listen_fd -32 handle */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-8,0);

            /* TLS_server_method() + SSL_CTX_new(method) */
            x_call_got(method_got);
            emit3(0x48,0x89,0xc7); /* rdi=method (mov rdi,rax) */
            x_call_got(ctxnew_got);
            x_mov_rbpN_r64(-16,0);
            x_test_rax_rax();
            int j_ctx_ok=x_jnz_rel32();
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_ctx_ok);

            /* SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM=1) */
            x_mov_r64_rbpN(7,-16);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,cert_off); emit_i32(0); /* rsi=&certfile */
            x_mov_r64_imm32(2,1); /* rdx=SSL_FILETYPE_PEM */
            x_call_got(usecert_got);
            emit4(0x48,0x83,0xf8,0x01); /* cmp rax,1 */
            int j_cert_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-16); x_call_got(ctxfree_got);
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_cert_ok);

            /* SSL_CTX_use_PrivateKey_file(ctx, keyfile, SSL_FILETYPE_PEM=1) */
            x_mov_r64_rbpN(7,-16);
            emit3(0x48,0x8d,0x35); add_reloc(RELOC_DATA,code_len,key_off); emit_i32(0); /* rsi=&keyfile */
            x_mov_r64_imm32(2,1);
            x_call_got(usekey_got);
            emit4(0x48,0x83,0xf8,0x01);
            int j_key_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-16); x_call_got(ctxfree_got);
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_key_ok);

            /* __ys_net_listen(port) -- the exact same socket/bind/listen
               subroutine y.net.listen() itself calls; reused rather than
               re-implemented here. */
            x_mov_r64_rbpN(7,-8);
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_listen"); }
            emit4(0x48,0x83,0xf8,0x00);
            int j_listen_ok=x_jge_rel32();
            x_mov_r64_rbpN(7,-16); x_call_got(ctxfree_got);
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_listen_ok);
            x_mov_rbpN_r64(-24,0); /* listen_fd */

            /* allocate a server handle (round-robin, YS_TLSSRV_MAX is a
               power of 2) and store {listen_fd, ctx} into that slot. */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tlssrv_next_off); emit_i32(0); /* r11=&next */
            emit3(0x49,0x8b,0x03); /* rax=[r11] (handle to use) */
            x_mov_rbpN_r64(-32,0);
            emit3(0x48,0x89,0xc1); /* mov rcx,rax */
            emit3(0x48,0xff,0xc1); /* inc rcx */
            emit4(0x48,0x83,0xe1,(YS_TLSSRV_MAX-1)); /* and rcx,1 */
            emit3(0x49,0x89,0x0b); /* mov [r11],rcx */

            x_mov_r64_rbpN(0,-32); /* rax=handle */
            emit3(0x48,0x69,0xc0); emit_i32(YS_TLSSRV_SLOT_SIZE); /* imul rax,rax,16 */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tlssrv_table_off); emit_i32(0); /* lea r11,[rip+srv table] */
            emit3(0x4c,0x01,0xd8); /* add rax,r11 -- rax=&srvtable[handle] */
            x_mov_r64_rbpN(1,-24); x_mov_rax8_r64(0,1); /* [rax+0]=listen_fd */
            x_mov_r64_rbpN(1,-16); x_mov_rax8_r64(8,1); /* [rax+8]=ctx */

            x_mov_r64_rbpN(0,-32); /* rax=handle (final return value) */

            for(int i=0;i<nfail;i++) x_patch_here(fail_patches[i]);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.net.tls_accept(server_handle) -> connection handle
           (0..YS_TLS_MAX_CONN-1) or -1. Blocks for one client, completes
           a server-side handshake, and stores the resulting connection
           into the SAME client-connection table tls_connect uses --
           tls_send/tls_recv_print/tls_close work on an accepted
           connection exactly the way they work on one tls_connect
           opened, with one deliberate difference: the ctx field is
           stored as 0 (not the server's real, shared SSL_CTX), so that
           tls_close's unconditional SSL_CTX_free(ctx) call is a no-op
           for an accepted connection instead of freeing the listening
           socket's shared context out from under future tls_accept
           calls (SSL_CTX_free(NULL) is a documented no-op, the same
           property tls_close already relies on for SSL_free). */
        if(is_y_net && strcmp(fn,"tls_accept")==0){
            if(g_target!=TARGET_LINUX){ x_mov_rax_imm32(-1); break; }
            tls_state_ensure();
            dynlink_need_library("libssl.so.3");
            int sslnew_got=dynlink_import("SSL_new");
            int setfd_got=dynlink_import("SSL_set_fd");
            int sslaccept_got=dynlink_import("SSL_accept");
            int sslfree_got=dynlink_import("SSL_free");

            int cbase=n->left?1:0;
            Node *srv_arg=(n->argc>cbase)?n->args[cbase]:NULL;

            if(srv_arg) compile_expr(srv_arg); else x_mov_rax_imm32(-1);
            x_store_rip_slot(g_tls_argbuf_off+0);

            int fail_patches[6]; int nfail=0;

            x_push_rbp(); x_mov_rbp_rsp();
            emit3(0x48,0x81,0xec); emit_i32(64); /* -8 srv_h -16 srv_slot -24 client_fd -32 ctx -40 ssl -48 conn_h */

            x_load_rip_slot(g_tls_argbuf_off+0);
            x_mov_rbpN_r64(-8,0);

            emit4(0x48,0x83,0xf8,0x00); /* cmp rax,0 */
            int j_ge0=x_jge_rel32();
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_ge0);
            emit4(0x48,0x83,0xf8,YS_TLSSRV_MAX); /* cmp rax,2 */
            int j_lt=x_jl_rel32();
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_lt);

            /* server slot address: table_base + handle*16 */
            x_mov_r64_rbpN(0,-8);
            emit3(0x48,0x69,0xc0); emit_i32(YS_TLSSRV_SLOT_SIZE);
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tlssrv_table_off); emit_i32(0);
            emit3(0x4c,0x01,0xd8);
            x_mov_rbpN_r64(-16,0);

            /* __ys_net_accept(listen_fd) — blocks for one client */
            x_mov_r64_rbpN(0,-16); x_mov_r64_rax8(7,0); /* rdi=[srv_slot+0]=listen_fd */
            { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_accept"); }
            emit4(0x48,0x83,0xf8,0x00);
            int j_acc_ok=x_jge_rel32();
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_acc_ok);
            x_mov_rbpN_r64(-24,0); /* client_fd */

            /* SSL_new(ctx) — ctx belongs to the listening socket, reload
               fresh from the server slot (never trust a register across
               the __ys_net_accept call above). */
            x_mov_r64_rbpN(0,-16); x_mov_r64_rax8(1,8); /* rcx=[srv_slot+8]=ctx */
            x_mov_rbpN_r64(-32,1);
            emit3(0x48,0x89,0xcf); /* rdi=ctx (mov rdi,rcx) */
            x_call_got(sslnew_got);
            x_mov_rbpN_r64(-40,0);
            x_test_rax_rax();
            int j_ssl_ok=x_jnz_rel32();
            x_mov_r64_rbpN(7,-24); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_ssl_ok);

            /* SSL_set_fd(ssl, client_fd) */
            x_mov_r64_rbpN(7,-40);
            x_mov_r64_rbpN(6,-24);
            x_call_got(setfd_got);
            emit4(0x48,0x83,0xf8,0x01);
            int j_setfd_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-40); x_call_got(sslfree_got);
            x_mov_r64_rbpN(7,-24); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_setfd_ok);

            /* SSL_accept(ssl) — server-side handshake */
            x_mov_r64_rbpN(7,-40);
            x_call_got(sslaccept_got);
            emit4(0x48,0x83,0xf8,0x01);
            int j_hs_ok=x_jz_rel32();
            x_mov_r64_rbpN(7,-40); x_call_got(sslfree_got);
            x_mov_r64_rbpN(7,-24); { int p=x_call_unresolved(); add_call_patch(p,"__ys_net_close"); }
            x_mov_rax_imm32(-1); fail_patches[nfail++]=x_jmp_rel32();
            x_patch_here(j_hs_ok);

            /* handshake succeeded — allocate a CLIENT-table connection
               handle (same table, same counter, same slot layout
               tls_connect uses) and store {client_fd, 0, ssl} — ctx=0
               deliberately, see the comment above this whole block. */
            emit3(0x4c,0x8d,0x1d); add_reloc(RELOC_DATA,code_len,g_tls_next_off); emit_i32(0);
            emit3(0x49,0x8b,0x03);
            x_mov_rbpN_r64(-48,0);
            emit3(0x48,0x89,0xc1);
            emit3(0x48,0xff,0xc1);
            emit4(0x48,0x83,0xe1,(YS_TLS_MAX_CONN-1));
            emit3(0x49,0x89,0x0b);

            x_mov_r64_rbpN(0,-48);
            x_tls_slot_addr();
            x_mov_r64_rbpN(1,-24); x_mov_rax8_r64(0,1);  /* [rax+0]=client_fd */
            x_mov_r64_imm32(1,0);  x_mov_rax8_r64(8,1);  /* [rax+8]=0 (no per-connection ctx) */
            x_mov_r64_rbpN(1,-40); x_mov_rax8_r64(16,1); /* [rax+16]=ssl */

            x_mov_r64_rbpN(0,-48); /* rax=connection handle (final return value) */

            for(int i=0;i<nfail;i++) x_patch_here(fail_patches[i]);
            x_mov_rsp_rbp(); x_pop_rbp();
            break;
        }
        /* y.http.get_print(url) -> bytes received or -1, response
           printed to stdout. url MUST be a string literal (scheme,
           host, optional :port, optional path all parsed at compile
           time by parse_http_url — never at runtime, so there's no
           argument-staging/frame-switch concern here at all: nothing
           in emit_http_request is ever compiled from a Node*). Chooses
           TLS or plain TCP automatically from the URL's scheme. */
        if(is_y_http && strcmp(fn,"get_print")==0){
            int cbase=n->left?1:0;
            Node *url_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            char host[256]; char path[256]; int port=0, use_tls=0;
            if(!url_arg || url_arg->kind!=ND_STR ||
               !parse_http_url(url_arg->sval,&use_tls,host,sizeof(host),&port,path,sizeof(path))){
                x_mov_rax_imm32(-1); break;
            }
            char req[560];
            int reqlen=snprintf(req,sizeof(req),
                "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",path,host);
            if(reqlen<0||reqlen>=(int)sizeof(req)){ x_mov_rax_imm32(-1); break; }
            int req_off=data_add_bytes((const uint8_t*)req,reqlen);
            emit_http_request(host,port,use_tls,req_off,reqlen);
            break;
        }
        /* y.http.post_print(url, body, content_type) -> bytes received
           or -1, response printed to stdout. url/body/content_type
           MUST all be string literals, same reasoning as get_print
           above (and the same reasoning tls_send's data argument
           already has one function up). content_type defaults to
           "text/plain" if omitted. */
        if(is_y_http && strcmp(fn,"post_print")==0){
            int cbase=n->left?1:0;
            Node *url_arg=(n->argc>cbase)?n->args[cbase]:NULL;
            Node *body_arg=(n->argc>cbase+1)?n->args[cbase+1]:NULL;
            Node *ctype_arg=(n->argc>cbase+2)?n->args[cbase+2]:NULL;
            char host[256]; char path[256]; int port=0, use_tls=0;
            if(!url_arg || url_arg->kind!=ND_STR ||
               !parse_http_url(url_arg->sval,&use_tls,host,sizeof(host),&port,path,sizeof(path))){
                x_mov_rax_imm32(-1); break;
            }
            const char *body = (body_arg && body_arg->kind==ND_STR) ? body_arg->sval : "";
            const char *ctype = (ctype_arg && ctype_arg->kind==ND_STR) ? ctype_arg->sval : "text/plain";
            int bodylen=ystrlen(body);
            char req[1600];
            int reqlen=snprintf(req,sizeof(req),
                "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                path,host,ctype,bodylen,body);
            if(reqlen<0||reqlen>=(int)sizeof(req)){ x_mov_rax_imm32(-1); break; }
            int req_off=data_add_bytes((const uint8_t*)req,reqlen);
            emit_http_request(host,port,use_tls,req_off,reqlen);
            break;
        }
        char fn_call_name[68];
        if(strcmp(fn,"main")==0){ snprintf(fn_call_name,68,"__ys_main"); fn=fn_call_name; }
        /* save caller-saved registers we care about: none needed now */
        /* args: rdi, rsi, rdx, rcx, r8, r9 */
        static const uint8_t arg_regs[][3]={
            {0x48,0x89,0xc7}, /* mov rdi,rax */
            {0x48,0x89,0xc6}, /* mov rsi,rax */
            {0x48,0x89,0xc2}, /* mov rdx,rax */
            {0x48,0x89,0xc1}, /* mov rcx,rax */
        };
        int first=(n->left)?1:0; /* skip arg[0]=self for dot calls */
        int nargs=n->argc-first;
        if(nargs>4) nargs=4;
        /* push args in reverse then load */
        for(int i=first;i<n->argc&&i-first<4;i++){
            compile_expr(n->args[i]);
            x_push_rax();
        }
        for(int i=nargs-1;i>=0;i--){
            x_pop_rax();
            emit3(arg_regs[i][0],arg_regs[i][1],arg_regs[i][2]);
        }
        /* call */
        int p=x_call_unresolved(); add_call_patch(p,fn);
        break;
    }
    default:
        x_mov_rax_imm32(0);
        break;
    }
}

/*  compile statement  */
static void compile_node(Node *n){
    if(!n) return;
    switch(n->kind){
    case ND_LET:
    case ND_VAR:{
        compile_expr(n->right);
        int off=local_alloc(n->name);
        for(int _i=0;_i<nlocals;_i++) if(strcmp(locals[_i].name,n->name)==0){locals[_i].is_float=g_last_float;break;}
        x_mov_mem_rax(off); break;
    }
    case ND_ASSIGN:{
        compile_expr(n->right);
        /* target name is in n->left->name (parser stores ident as left child) */
        const char *aname = (n->name[0]) ? n->name
                          : (n->left && n->left->name[0]) ? n->left->name : "";
        int off=local_get(aname); if(off==0) off=local_alloc(aname);
        for(int _i=0;_i<nlocals;_i++) if(strcmp(locals[_i].name,aname)==0){locals[_i].is_float=g_last_float;break;}
        x_mov_mem_rax(off); break;
    }
    case ND_RETURN:{
        if(n->right) compile_expr(n->right);
        else x_mov_rax_imm32(0);
        /* epilogue */
        x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        break;
    }
    case ND_IF:{
        compile_expr(n->cond);
        x_test_rax_rax();
        int jz=x_jz_rel32();
        /* parser stores if-body in n->then, not n->body */
        compile_block(n->then ? n->then : n->body);
        if(n->els){
            int jend=x_jmp_rel32();
            x_patch_here(jz);
            compile_node(n->els);
            x_patch_here(jend);
        } else {
            x_patch_here(jz);
        }
        break;
    }
    case ND_WHILE:{
        int loop_top=code_len;
        compile_expr(n->cond);
        x_test_rax_rax();
        int jz=x_jz_rel32();
        /* push break/continue targets */
        break_stack[bstack_top++]=jz; /* placeholder */
        continue_stack[cstack_top++]=loop_top;
        compile_block(n->body);
        int jback=x_jmp_rel32();
        patch_i32(jback,(int32_t)(loop_top-(jback+4)));
        x_patch_here(jz);
        bstack_top--; cstack_top--;
        break;
    }
    case ND_FOR:{
        /* for i in lo..hi  (exclusive upper bound) */
        if(n->cond && n->cond->kind==ND_BINOP && n->cond->op==TK_DOTDOT){
            /* allocate loop var and hi bound on stack */
            compile_expr(n->cond->left);
            int i_off  = local_alloc(n->name);
            x_mov_mem_rax(i_off);              /* i = lo */
            compile_expr(n->cond->right);
            int hi_off = local_alloc("__for_hi");
            x_mov_mem_rax(hi_off);             /* hi = upper */

            int loop_top = code_len;
            /* cmp i, hi  (both in memory) */
            x_mov_rax_mem(i_off);              /* rax = i */
            emit3(0x48,0x89,0xc1);             /* mov rcx,rax */
            x_mov_rax_mem(hi_off);             /* rax = hi */
            x_cmp_rax_rcx();                   /* cmp hi, i  → flags: hi-i */
            /* jle exit  (i >= hi → hi <= i → hi-i <= 0) */
            /* We want: exit when i >= hi  ←→  hi <= i  ←→  hi - i <= 0
               cmp hi,i sets flags for hi-i:
               hi<=i means hi-i<=0: SF=OF (for <=), so JLE = 0x8e */
            emit2(0x0f,0x8e); int jle=code_len; emit_i32(0); /* jle exit */

            continue_stack[cstack_top++] = loop_top;
            break_stack[bstack_top++]    = jle;
            compile_block(n->body);

            /* i++ */
            x_mov_rax_mem(i_off);
            emit3(0x48,0xff,0xc0);             /* inc rax */
            x_mov_mem_rax(i_off);
            int jback = x_jmp_rel32();
            patch_i32(jback,(int32_t)(loop_top-(jback+4)));
            x_patch_here(jle);
            bstack_top--; cstack_top--;
        }
        break;
    }
    case ND_BREAK:{
        /* jmp to end of loop — add to break patches */
        if(bstack_top>0){
            int p=x_jmp_rel32();
            /* save patch to resolve later */
            break_stack[bstack_top-1]=p; /* overwrite with actual jmp */
        }
        break;
    }
    case ND_CONTINUE:{
        if(cstack_top>0){
            int target=continue_stack[cstack_top-1];
            int p=x_jmp_rel32();
            patch_i32(p,(int32_t)(target-(p+4)));
        }
        break;
    }
    case ND_FN:{
        /* function definition — compile body */
        /* rename "main" to "__ys_main" to avoid clash with ELF entry "main" */
        const char *fn_label=n->name;
        char fn_label_buf[72];
        if(strcmp(n->name,"main")==0){
            snprintf(fn_label_buf,sizeof(fn_label_buf),"__ys_%s",n->name);
            fn_label=fn_label_buf;
        }
        int fn_start=code_len;
        sym_define(fn_label,fn_start);
        Local saved_locals[LOCAL_MAX];
        int saved_nlocals=nlocals, saved_ss=stack_size;
        memcpy(saved_locals,locals,sizeof(Local)*nlocals);
        locals_clear();
        x_push_rbp(); x_mov_rbp_rsp();
        int sub_patch=code_len;
        emit3(0x48,0x81,0xec); emit_i32(0); /* sub rsp, frame — patched later */
        /* allocate parameters as locals */
        /* SysV ABI: args in rdi, rsi, rdx, rcx, r8, r9 */
        /* We store each arg onto the stack: mov [rbp+off], reg */
        /* ModRM bytes for mov [rbp+disp8], rdi/rsi/rdx/rcx */
        {
            static const uint8_t modrm[]={0x7d,0x75,0x55,0x4d};
            for(int pi=0; pi<n->argc && pi<4; pi++){
                int poff=local_alloc(n->args[pi]->name);
                if(poff>=-128&&poff<=127){
                    emit4(0x48,0x89,(uint8_t)(modrm[pi]|0x40),(uint8_t)(int8_t)poff);
                } else {
                    emit3(0x48,0x89,(uint8_t)(modrm[pi]|0x80)); emit_i32(poff);
                }
            }
        }
        /* compile body */
        compile_block(n->body);
        /* default return 0 if no return stmt */
        x_mov_rax_imm32(0);
        x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
        /* patch sub rsp */
        int frame=(stack_size+15)&~15; /* align to 16 */
        patch_i32(sub_patch+3,frame);
        /* restore outer locals */
        memcpy(locals,saved_locals,sizeof(Local)*saved_nlocals);
        nlocals=saved_nlocals; stack_size=saved_ss;
        break;
    }
    case ND_BLOCK:
        compile_block(n);
        break;
    case ND_ARRAY: /* fallthrough */
    default:
        compile_expr(n);
        break;
    }
}

static void compile_block(Node *b){
    if(!b) return;
    for(int i=0;i<b->stmtc;i++) compile_node(b->stmts[i]);
}

/*  main entry  */

/* resolve all call patches — returns the number of symbols that
   could not be resolved (e.g. a builtin like y.net.connect that isn't
   implemented for native compilation yet). Callers must NOT write out
   an executable when this is nonzero — the code contains raw call
   rel32 instructions still pointing at placeholder offset 0, which
   would jump to garbage at runtime instead of failing to build. */
static int resolve_calls(void){
    int nfail=0;
    for(int i=0;i<ncall_patches;i++){
        int target=sym_find(call_patches[i].target);
        if(target<0){
            fprintf(stderr,"ys: unresolved symbol: %s\n",call_patches[i].target);
            nfail++;
            continue;
        }
        int off=call_patches[i].code_off;
        patch_i32(off,(int32_t)(target-(off+4)));
    }
    return nfail;
}

/*  public compile function  */

/*  output format declarations  */
extern int elf_write(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off);

extern int elf_write_dynamic(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off,
    const char **needed_libs, int nneeded,
    const char **import_names, int *import_got_offs, int nimports);

extern int macho_write(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off);

extern int pe_write(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off,
    int *icall_off, int *icall_idx, int n_icalls);

/*  win32 helpers (emitted when target=windows)  */
static void emit_win32_helpers(void){
    /* Windows calls: WriteFile, GetStdHandle, ExitProcess
       We emit call-thru stubs using indirect calls through IAT.
       At link time the IAT RVAs are fixed up.
       We use a simple approach: mov rax, [rip+iat_slot]; call rax */

    /* __ys_print_str(rcx=buf, rdx=len) */
    sym_define("__ys_print_str", code_len);
    helper_print_str_off=code_len;
    x_push_rbp(); x_mov_rbp_rsp();
    x_sub_rsp_i8(0x48); /* 72 bytes: 32 shadow + locals */
    /* WriteFile(handle, buf, len, &written, NULL) */
    /* Windows ABI on entry: rcx=buf, rdx=len (this helper's own proto) */
    /* Save buf/len first, before either register gets clobbered */
    emit3(0x48,0x89,0x4d); emit1(0xe0); /* mov [rbp-32], rcx (buf) */
    emit3(0x48,0x89,0x55); emit1(0xe8); /* mov [rbp-24], rdx (len) */
    /* GetStdHandle(-11) → rax = stdout handle */
    emit3(0x48,0xc7,0xc1); emit_i32(-11); /* mov rcx,-11 */
    add_import_call(0); /* call [rip+GetStdHandle_IAT] */
    /* WriteFile(handle, buf, len, &written, NULL) */
    emit3(0x48,0x89,0xc1); /* mov rcx,rax (handle) */
    emit3(0x48,0x8b,0x55); emit1(0xe0); /* mov rdx,[rbp-32] (buf) */
    emit3(0x4c,0x8b,0x45); emit1(0xe8); /* mov r8,[rbp-24] (len) */
    /* r9 = &written = rsp+0x28 */
    emit4(0x4c,0x8d,0x4c,0x24); emit1(0x28);
    /* [rsp+32] = NULL (must zero the FULL 8-byte pointer slot — a 32-bit
       write here only clears the low half, leaving the upper 4 bytes as
       whatever garbage was already on the stack, so lpOverlapped ends up
       being a bogus non-NULL pointer and WriteFile silently fails) */
    emit4(0x48,0xc7,0x44,0x24); emit1(0x20); emit_i32(0);
    add_import_call(1); /* call [rip+WriteFile_IAT] */
    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();

    /* __ys_print_int(rcx=val) */
    sym_define("__ys_print_int", code_len);
    helper_print_int_off=code_len;
    /* Convert int to string (same algorithm as Linux but using rcx ABI) */
    x_push_rbp(); x_mov_rbp_rsp();
    emit1(0x53); /* push rbx */
    x_sub_rsp_i8(48);
    emit3(0x48,0x89,0xc8); /* mov rax,rcx */
    /* rest identical to Linux version */
    int loop_t=code_len;
    emit3(0x48,0x31,0xd2); emit2(0x48,0xb9); emit_i64(10);
    emit3(0x48,0xf7,0xf9); emit3(0x80,0xc2,0x30);
    emit3(0x88,0x14,0x1c); emit3(0x48,0xff,0xc3);
    x_test_rax_rax();
    int jnz3=x_jnz_rel32(); patch_i32(jnz3,(int32_t)(loop_t-(jnz3+4)));
    x_patch_here(jnz3);
    /* reverse */
    emit3(0x48,0x31,0xf6); emit3(0x48,0x89,0xd9); emit3(0x48,0xff,0xc9);
    int rev_t=code_len;
    emit3(0x48,0x39,0xce);
    int rev_d=x_jz_rel32(); code_buf[rev_d-2]=0x8d;
    emit3(0x8a,0x04,0x34); emit3(0x8a,0x14,0x0c);
    emit3(0x88,0x14,0x34); emit3(0x88,0x04,0x0c);
    emit3(0x48,0xff,0xc6); emit3(0x48,0xff,0xc9);
    int jb=x_jmp_rel32(); patch_i32(jb,(int32_t)(rev_t-(jb+4)));
    x_patch_here(rev_d);
    /* call __ys_print_str(rcx=rsp, rdx=rbx) */
    emit3(0x48,0x89,0xe1); /* mov rcx,rsp */
    emit3(0x48,0x89,0xda); /* mov rdx,rbx */
    int p=x_call_unresolved(); add_call_patch(p,"__ys_print_str");
    emit1(0x5b);
    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();

    /* __ys_print_nl() */
    sym_define("__ys_print_nl", code_len);
    helper_print_nl_off=code_len;
    {
        int nl_data=data_len; data_buf[data_len++]='\n';
        x_push_rbp(); x_mov_rbp_rsp();
        emit3(0x48,0x8d,0x0d); /* lea rcx,[rip+nl] */
        add_reloc(RELOC_DATA,code_len,nl_data); emit_i32(0);
        x_mov_rax_imm32(1); emit3(0x48,0x89,0xc2); /* rdx=1 */
        int p2=x_call_unresolved(); add_call_patch(p2,"__ys_print_str");
        x_pop_rbp(); x_ret();
    }

    /* __ys_exit(rcx=code) */
    sym_define("__ys_exit", code_len);
    helper_exit_off=code_len;
    add_import_call(2); /* call [rip+ExitProcess_IAT] */
    x_ret();

    /* __ys_win_maybe_pause(): if this process is the ONLY one attached to
       its console, that console was auto-created by Explorer because the
       .exe was double-clicked (rather than inherited from an existing
       cmd.exe) — in that case Windows destroys the window the instant the
       process exits, so any printed output disappears before it can be
       read. Detect this with GetConsoleProcessList() (returns 1 if we're
       the sole owner) and, only then, prompt + wait for Enter. When
       launched from an existing terminal, GetConsoleProcessList() returns
       >1 and we skip straight through — no extra keypress needed there. */
    sym_define("__ys_win_maybe_pause", code_len);
    x_push_rbp(); x_mov_rbp_rsp();
    x_sub_rsp_i8(0x60); /* 32 shadow + pid buf(8) + read buf/count locals */

    /* GetConsoleProcessList(&pids, 2) -> eax = attached process count */
    emit3(0x48,0x8d,0x4d); emit1(0xf0);   /* lea rcx,[rbp-0x10] (pid buffer) */
    emit1(0xba); emit_i32(2);             /* mov edx,2 */
    add_import_call(3);                   /* call [rip+GetConsoleProcessList_IAT] */
    emit3(0x83,0xf8,0x01);                /* cmp eax,1 */
    int jg_skip=x_jg_rel32();             /* if count>1: skip pause entirely */

    /* print "\nPress Enter to continue . . . " */
    {
        static const char *msg="\nPress Enter to continue . . . ";
        int moff=data_add_str(msg);
        int mlen=ystrlen(msg);
        x_lea_arg1_data(moff);
        x_mov_rax_imm32(mlen); x_arg2_from_rax();
        int pp=x_call_unresolved(); add_call_patch(pp,"__ys_print_str");
    }

    /* GetStdHandle(STD_INPUT_HANDLE = -10) -> rax = console input handle */
    emit3(0x48,0xc7,0xc1); emit_i32(-10); /* mov rcx,-10 */
    add_import_call(0);                   /* call [rip+GetStdHandle_IAT] */

    /* ReadFile(handle, buf=[rbp-0x20], 1, &read=[rbp-0x30], NULL) —
       a console handle in default (line-buffered) mode blocks until the
       user presses Enter, which is exactly the wait we want. */
    emit3(0x48,0x89,0xc1);                /* mov rcx,rax (handle) */
    emit3(0x48,0x8d,0x55); emit1(0xe0);   /* lea rdx,[rbp-0x20] (buf) */
    emit2(0x41,0xb8); emit_i32(1);        /* mov r8d,1 (nNumberOfBytesToRead) */
    emit3(0x4c,0x8d,0x4d); emit1(0xd0);   /* lea r9,[rbp-0x30] (&bytesRead) */
    emit4(0x48,0xc7,0x44,0x24); emit1(0x20); emit_i32(0); /* [rsp+0x20]=NULL (full 8 bytes) */
    add_import_call(4);                   /* call [rip+ReadFile_IAT] */

    x_patch_here(jg_skip); /* skip_pause: */
    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
}

/*  compile_program  */
int ys_compile(Node *prog, Target target, const char *outfile){
    if(!prog){ fprintf(stderr,"ys: no AST to compile\n"); return 1; }
    g_target=target;

    code_len=0; data_len=0; nrelocs=0;
    g_dyn_enabled=0; g_dyn_nimports=0; g_dyn_nneeded=0;
    nlocals=0; stack_size=0;
    nsyms=0; ncall_patches=0;

    /* emit runtime helpers */
    if(target==TARGET_WINDOWS) emit_win32_helpers();
    else emit_helpers();

    /* scan top-level for function definitions first */
    for(int i=0;i<prog->stmtc;i++){
        Node *n=prog->stmts[i];
        if(n && n->kind==ND_FN) compile_node(n);
    }

    /* emit _start / main entry */
    int entry_off=code_len;
    sym_define("_start",entry_off);
    sym_define("main",entry_off);

    x_push_rbp(); x_mov_rbp_rsp();
    int sub_patch=code_len;
    emit3(0x48,0x81,0xec); emit_i32(0); /* sub rsp, frame */

    /* compile top-level statements (non-fn) */
    locals_clear();
    for(int i=0;i<prog->stmtc;i++){
        Node *n=prog->stmts[i];
        if(!n) continue;
        if(n->kind==ND_FN) continue; /* already compiled */
        if(n->kind==ND_CALL&&
           (strcmp(n->name,"main")==0||strcmp(n->name,"fn")==0)) continue;
        /* if there's a main() fn, call it */
        compile_node(n);
    }

    /* call __ys_main() if user defined a main() function */
    if(sym_find("__ys_main")>=0){
        int pm=x_call_unresolved(); add_call_patch(pm,"__ys_main");
    }

    /* On Windows, give double-click-launched consoles a chance to show
       their output before the window disappears (see __ys_win_maybe_pause
       for why this is skipped automatically when run from cmd.exe). */
    if(target==TARGET_WINDOWS){
        int pp=x_call_unresolved(); add_call_patch(pp,"__ys_win_maybe_pause");
    }

    /* exit(0) */
    x_mov_rax_imm32(0);
    x_arg1_from_rax();
    int ep=x_call_unresolved(); add_call_patch(ep,"__ys_exit");

    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();

    /* patch frame size */
    int frame=(stack_size+15)&~15;
    if(frame==0) frame=16;
    patch_i32(sub_patch+3,frame);

    /* resolve all calls */
    int unresolved=resolve_calls();
    if(unresolved>0){
        fprintf(stderr,"ys: compile failed — %d unresolved symbol(s) "
                        "(likely a builtin not yet supported for native "
                        "compilation on this target); no file written.\n",
                        unresolved);
        return 1;
    }

    /* collect reloc arrays */
    static int rc[RELOC_MAX], rd[RELOC_MAX]; int nr=0;
    static int ic_off[RELOC_MAX], ic_idx[RELOC_MAX]; int n_ic=0;
    for(int i=0;i<nrelocs;i++){
        if(relocs[i].kind==RELOC_DATA){
            rc[nr]=relocs[i].code_off;
            rd[nr]=relocs[i].target_off;
            nr++;
        } else if(relocs[i].kind==RELOC_CODE){
            ic_off[n_ic]=relocs[i].code_off;
            ic_idx[n_ic]=relocs[i].target_off; /* import index */
            n_ic++;
        }
    }

    /* write output */
    int ret=0;
    switch(target){
    case TARGET_LINUX:
        if(g_dyn_enabled){
            static const char *inames[MAX_DYN_IMPORTS];
            static int igots[MAX_DYN_IMPORTS];
            for(int i=0;i<g_dyn_nimports;i++){ inames[i]=g_dyn_imports[i].name; igots[i]=g_dyn_imports[i].got_off; }
            dynlink_need_library("libc.so.6"); /* always present: puts/exit for dynlink_test, and a safe default */
            static const char *lnames[MAX_DYN_NEEDED];
            for(int i=0;i<g_dyn_nneeded;i++) lnames[i]=g_dyn_needed[i];
            ret=elf_write_dynamic(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off,
                                   lnames,g_dyn_nneeded,inames,igots,g_dyn_nimports);
        } else {
            ret=elf_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off);
        }
        break;
    case TARGET_MACOS:
        ret=macho_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off);
        break;
    case TARGET_WINDOWS:
        ret=pe_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off,ic_off,ic_idx,n_ic);
        break;
    }
    if(ret==0) fprintf(stdout,"ys: compiled → %s\n",outfile);
    return ret;
}