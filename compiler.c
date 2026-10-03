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
#include "net_runtime.h"
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
static void emit5(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e){ emit1(a);emit1(b);emit1(c);emit1(d);emit1(e); }

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
typedef enum { RELOC_DATA, RELOC_CODE, RELOC_CODEADDR } RelocKind;
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

/* symbol table */
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

/*  v2.42: native struct layouts (compile-time only — no runtime type
    info exists in this backend, so field offsets are resolved entirely
    while compiling, the same way struct literals resolve field *names*
    to positions in the interpreter's Val.field_names at eval time, just
    one stage earlier here). Registered from top-level `struct Name {..}`
    declarations before any function body is compiled (see the new scan
    added in ys_compile below) so a struct can be used by any function
    in the file regardless of declaration order relative to that use. */
#define NSTRUCT_MAX 32
typedef struct { char name[32]; char fields[8][32]; int nfields; } NStructDef;
static NStructDef nstruct_defs[NSTRUCT_MAX];
static int        n_nstruct_defs=0;

static NStructDef *nstruct_find(const char *name){
    for(int i=0;i<n_nstruct_defs;i++)
        if(strcmp(nstruct_defs[i].name,name)==0) return &nstruct_defs[i];
    return NULL;
}

static void nstruct_register(Node *n){
    if(n_nstruct_defs>=NSTRUCT_MAX) return;
    if(nstruct_find(n->name)) return; /* already registered */
    NStructDef *d=&nstruct_defs[n_nstruct_defs++];
    /* manual clamp-and-copy, same idiom already used elsewhere in this
       file (see the len>63 clamps above) — strncpy triggered
       -Wstringop-truncation and snprintf triggered -Wformat-truncation
       in its place; both warnings are gcc reasoning about the general
       case (it can't see the source is always null-terminated shorter
       than 64 by construction), not a real bug either way, but a
       hand-rolled bounded copy sidesteps both cleanly. */
    int nlen=(int)strlen(n->name); if(nlen>31) nlen=31;
    memcpy(d->name,n->name,nlen); d->name[nlen]=0;
    d->nfields=n->stmtc<8?n->stmtc:8;
    for(int i=0;i<d->nfields;i++){
        int flen=(int)strlen(n->stmts[i]->name); if(flen>31) flen=31;
        memcpy(d->fields[i],n->stmts[i]->name,flen); d->fields[i][flen]=0;
    }
}

/*  local variable table  */
#define LOCAL_MAX 64
typedef struct {
    char name[64];
    int  rbp_off;
    int  is_float;
    /* v2.42: struct-typed locals. is_struct set means rbp_off is the
       offset of *field 0*; field i lives at rbp_off - i*8 (see
       local_alloc_struct below for why fields land in that order).
       field_is_float mirrors is_float but per field, since a struct's
       fields aren't all necessarily the same runtime representation. */
    int  is_struct;
    char struct_type[32];
    int  field_is_float[8];
    /* v2.44: is_ref means this is a struct-typed *parameter* — the
       local holds one 8-byte pointer (into the *caller's* stack frame)
       rather than the struct's fields themselves, so field access has
       to go through that pointer at runtime instead of reading
       rbp_off-relative slots directly the way a plain struct local
       (v2.42) or a struct return's destination (v2.43) does. rbp_off
       here is where the incoming pointer itself is spilled, not a
       field offset. */
    int  is_ref;
    /* v2.45: fixed-size native arrays. is_array set means rbp_off is
       element 0's offset and element i lives at rbp_off - i*8 — same
       downward-from-element-0 layout v2.42/v2.43/v2.44 already settled
       on for struct fields, reused here rather than reinvented, and
       for the same reason: it's just how local_alloc-style stack
       growth naturally falls out (see local_alloc_array below). Fixed
       size only, decided at compile time from the literal's own
       element count — no y.push/growth support, since that would need
       real heap allocation this backend doesn't have yet (a bigger,
       separate undertaking, not attempted here). */
    int  is_array;
    int  arr_nelems;
    /* v2.46: an array whose elements were each an *existing struct
       local* at the literal site (`let arr = [p, q]` where p, q are
       already Point{...} locals) stores each element as a pointer to
       that struct, same as a struct-typed parameter (Local.is_ref)
       does — see compile_array_lit_into for how this gets set. Lets
       `arr[i].field` resolve field offsets the same way a struct
       parameter's fields already do. An array mixing struct and
       non-struct elements, or whose elements are inline struct
       literals rather than existing locals, isn't supported — see
       compile_array_lit_into's own comment for the exact narrow rule. */
    int  arr_is_struct_ptr;
    char arr_struct_type[32];
    /* v2.47: same idea as arr_is_struct_ptr, for `let arr = [a, b]`
       where a, b are themselves *array* locals — each element stores
       a pointer to the inner array instead of a scalar, enabling
       exactly two levels of arr[i][j] (the inner array's own elements
       are read/written as plain scalars; a third level, arr[i][j][k],
       isn't attempted — see the ND_INDEX case's comment for why two
       levels was the chosen scope). */
    int  arr_is_array_ptr;
} Local;
static Local locals[LOCAL_MAX];
static int   nlocals=0;
static int   stack_size=0;  /* current frame size in bytes */

static void locals_clear(){ nlocals=0; stack_size=0; }

static int local_get(const char *name){
    for(int i=0;i<nlocals;i++) if(strcmp(locals[i].name,name)==0) return locals[i].rbp_off;
    return 0; /* 0 = not found */
}

static Local *local_find(const char *name){
    for(int i=0;i<nlocals;i++) if(strcmp(locals[i].name,name)==0) return &locals[i];
    return NULL;
}

static int local_alloc(const char *name){
    int existing=local_get(name);
    if(existing) return existing;
    stack_size+=8;
    locals[nlocals].rbp_off=-stack_size;
    locals[nlocals].is_struct=0;
    locals[nlocals].is_ref=0; /* v2.44: locals[] is one fixed-size
        global array reused across every function compiled in the
        file (cleared by resetting nlocals, not by zeroing the array),
        so every field a struct-typed local relies on has to be set
        explicitly on allocation rather than assumed zero from a
        previous function's leftover entry — is_ref is new this
        version, so setting it here from the start, rather than
        adding it in a later fix, avoids exactly the kind of stale-
        flag bug local_alloc_struct below already had to guard
        against for its own fields (is_struct, is_float, etc.). */
    locals[nlocals].is_array=0; /* v2.45: same stale-flag reasoning */
    strncpy(locals[nlocals].name,name,63);
    nlocals++;
    return -stack_size;
}

/* v2.42: reserve nfields contiguous 8-byte slots for a struct-typed
   local. Slots are reserved in field order (field 0 first), so field 0
   ends up at the *least* negative rbp offset of the block and each
   later field is 8 bytes deeper (more negative) than the one before
   it — i.e. field i's offset is always (field 0's offset) - i*8,
   regardless of how many other locals come before or after this one.
   Returns the existing Local* unchanged if this name was already
   allocated as this same struct type (so re-entering the same `let`
   in a loop body, or a plain re-read, doesn't reserve stack twice). */
static Local *local_alloc_struct(const char *name, const char *struct_type){
    Local *existing=local_find(name);
    if(existing && existing->is_struct && !existing->is_ref && strcmp(existing->struct_type,struct_type)==0)
        return existing;
    NStructDef *def=nstruct_find(struct_type);
    int nfields=def?def->nfields:0;
    if(nfields<1) nfields=1; /* degrade to a single word rather than 0 bytes */
    int field0_off=0;
    for(int i=0;i<nfields;i++){ stack_size+=8; if(i==0) field0_off=-stack_size; }
    /* v2.45 fix: a same-named local that exists but didn't match above
       (reassigning `p` from one struct type to a *different* one) must
       reuse the SAME Local* slot, not append a new entry under
       nlocals++ — local_find/local_get return the first name match by
       linear scan, so a second same-named entry would just become
       permanently unreachable dead weight while every later lookup of
       `p` kept resolving to the stale first entry. Caught by testing
       array reassignment to a different element count (the same
       append-on-mismatch bug, just easier to trigger there since
       local_alloc_array's reuse condition checks element count, which
       changes far more naturally during normal use than a struct's
       type does) — a[0]/a[1] kept reading the pre-reassignment values
       after `a = [100, 200]`. Fixed here too since the bug was
       structurally identical, not specific to arrays. Never shrinks
       stack_size on a mismatch (always reserves a fresh block above,
       even when reusing the Local*) — slightly wasteful on repeated
       reassignment to a growing/shrinking type/size, but simple and
       correct, consistent with this backend's existing "frames only
       grow, never reused mid-function" model everywhere else. */
    Local *L = existing ? existing : &locals[nlocals++];
    strncpy(L->name,name,63); L->name[63]=0;
    L->rbp_off=field0_off;
    L->is_float=0;
    L->is_struct=1;
    L->is_ref=0;
    L->is_array=0;
    strncpy(L->struct_type,struct_type,31); L->struct_type[31]=0;
    for(int i=0;i<8;i++) L->field_is_float[i]=0;
    return L;
}

/* v2.44: reserve one 8-byte slot for a struct-typed *parameter* — just
   a pointer, not the struct's fields (see Local.is_ref's comment).
   Always a fresh local_alloc() call, never reused across re-entry the
   way local_alloc_struct's block is, because a parameter is bound
   exactly once per call, at function entry — there's no loop-body
   re-declaration case to guard against here. */
static Local *local_alloc_struct_ref(const char *name, const char *struct_type){
    int off=local_alloc(name);
    Local *L=local_find(name);
    L->is_struct=1;
    L->is_ref=1;
    L->is_array=0;
    strncpy(L->struct_type,struct_type,31); L->struct_type[31]=0;
    for(int i=0;i<8;i++) L->field_is_float[i]=0;
    (void)off;
    return L;
}

/* v2.45: reserve nelems contiguous 8-byte slots for a fixed-size
   native array local — same reservation shape as local_alloc_struct
   just above (element 0 first/least-negative, each later element 8
   bytes deeper), minus the NStructDef lookup, since an array has no
   named fields, just a count. Re-entering the same `let` (e.g. inside
   a loop body) with the same element count reuses the existing block,
   same as local_alloc_struct; a different element count reserves a
   fresh block rather than trying to resize in place, since this
   backend has no realloc-style move-and-copy machinery — matches how
   this whole feature is scoped to fixed-size arrays only (no
   y.push/growth) in the first place. */
static Local *local_alloc_array(const char *name, int nelems){
    Local *existing=local_find(name);
    if(existing && existing->is_array && existing->arr_nelems==nelems)
        return existing;
    if(nelems<1) nelems=1;
    int elem0_off=0;
    for(int i=0;i<nelems;i++){ stack_size+=8; if(i==0) elem0_off=-stack_size; }
    /* v2.45 fix: see local_alloc_struct's matching comment just above
       — reuse the existing same-named Local* on a size mismatch
       instead of appending a new, permanently-unreachable entry. This
       was the actual bug this comment's sibling describes catching:
       `a = [100, 200]` after `var a = [1, 2, 3]` left a[0]/a[1] still
       reading the old 3-element block's values, because local_find
       kept resolving `a` to the original entry. */
    Local *L = existing ? existing : &locals[nlocals++];
    strncpy(L->name,name,63); L->name[63]=0;
    L->rbp_off=elem0_off;
    L->is_float=0;
    L->is_struct=0;
    L->is_ref=0;
    L->is_array=1;
    L->arr_nelems=nelems;
    L->arr_is_struct_ptr=0; /* set by compile_array_lit_into if elements turn out to be struct locals */
    L->arr_struct_type[0]=0;
    L->arr_is_array_ptr=0; /* set by compile_array_lit_into if elements turn out to be array locals */
    return L;
}

/* v2.42: resolve field_name to its byte offset within a struct-typed
   Local. Returns 1 and sets out_off/out_is_float on success, 0 if
   the field name doesn't exist on this struct (caller decides how to
   fail — see ND_DOT/ND_ASSIGN below, both treat this as "compiles to
   0" the same way an unresolved plain identifier already does
   elsewhere in this file, rather than aborting the whole compile). */
static int nstruct_field_offset(Local *L, const char *field_name, int *out_off, int *out_is_float){
    if(!L->is_struct) return 0;
    NStructDef *def=nstruct_find(L->struct_type);
    if(!def) return 0;
    for(int i=0;i<def->nfields;i++){
        if(strcmp(def->fields[i],field_name)==0){
            *out_off=L->rbp_off-i*8;
            *out_is_float=L->field_is_float[i];
            return 1;
        }
    }
    return 0;
}

/* v2.44: same field lookup, but for a struct-typed *parameter*
   (Local.is_ref) — there's no rbp-relative slot per field to compute
   (the local only holds one pointer, spilled at L->rbp_off), so this
   returns the field's offset *relative to that pointer's target*
   instead. The pointer holds the *caller's* field-0 address (from
   local_alloc_struct: field 0 is the least-negative/highest-address
   slot of the block, and field i sits at field0_off - i*8, i.e.
   *lower* addresses as i increases — see that function's own comment).
   So field i's address relative to the pointer is also -i*8, the same
   downward direction, not +i*8 — this matches the sign-error lesson
   from v2.43's return-value copy loop exactly, caught here by
   reasoning it through against local_alloc_struct's actual layout
   before running anything, rather than by a second field-1-comes-
   back-wrong test failure. Struct-ref locals track no per-field
   float-ness (field_is_float is only ever populated by a literal
   write, which never happens to a pointed-to caller's struct from
   inside the callee), so float fields read through a parameter don't
   get float formatting right yet — a known, narrow limitation of this
   pass, not attempted. */
static int nstruct_field_ptr_offset(Local *L, const char *field_name, int *out_off){
    if(!L->is_struct || !L->is_ref) return 0;
    NStructDef *def=nstruct_find(L->struct_type);
    if(!def) return 0;
    for(int i=0;i<def->nfields;i++){
        if(strcmp(def->fields[i],field_name)==0){ *out_off=-i*8; return 1; }
    }
    return 0;
}

/* v2.46: same field-offset lookup as nstruct_field_ptr_offset above,
   but keyed by a struct type *name* rather than a Local — used for
   arr[i].field, where the pointer being dereferenced comes from an
   array element (Local.arr_struct_type), not from a struct-typed
   local itself. */
static int nstruct_field_offset_by_type(const char *struct_type, const char *field_name, int *out_off){
    NStructDef *def=nstruct_find(struct_type);
    if(!def) return 0;
    for(int i=0;i<def->nfields;i++){
        if(strcmp(def->fields[i],field_name)==0){ *out_off=-i*8; return 1; }
    }
    return 0;
}

/* v2.43: which native-compiled functions return a struct, and which
   struct type. This is a Yolish-internal calling-convention choice,
   not an attempt to match real System V struct-return register
   classification (RAX:RDX for small structs, hidden pointer for large
   ones) — nothing outside a native Yolish binary ever needs to
   interoperate with one of these values directly (unlike the
   dynlink-FFI case, which is why struct-crossing-the-FFI-boundary is
   still explicitly out of scope), so there's no correctness reason to
   match the real ABI and a real reason not to: it would mean splitting
   codegen into a small-struct/large-struct case for no benefit here.
   Every struct-returning function uses a hidden pointer (passed in
   rdi, real params shifted to start at rsi) regardless of field count. */
#define FN_STRUCT_MAX 32
typedef struct { char fn_name[64]; char struct_type[32]; } FnStructReturn;
static FnStructReturn fn_struct_returns[FN_STRUCT_MAX];
static int             n_fn_struct_returns=0;

static const char *fn_returns_struct(const char *fn_name){
    for(int i=0;i<n_fn_struct_returns;i++)
        if(strcmp(fn_struct_returns[i].fn_name,fn_name)==0) return fn_struct_returns[i].struct_type;
    return NULL;
}
static void fn_struct_return_register(const char *fn_name, const char *struct_type){
    if(n_fn_struct_returns>=FN_STRUCT_MAX) return;
    if(fn_returns_struct(fn_name)) return;
    FnStructReturn *r=&fn_struct_returns[n_fn_struct_returns++];
    int l1=(int)strlen(fn_name); if(l1>63) l1=63;
    memcpy(r->fn_name,fn_name,l1); r->fn_name[l1]=0;
    int l2=(int)strlen(struct_type); if(l2>31) l2=31;
    memcpy(r->struct_type,struct_type,l2); r->struct_type[l2]=0;
}

/* v2.44: which native-compiled functions take a struct-typed
   *parameter*, at which position, and which struct type. Unlike return
   types (inferred purely from the function's own body), a parameter
   has no type annotation anywhere in Yolish source at all — the only
   signal that a given parameter is meant to be a struct is that some
   call site, somewhere in the file, actually passes one. So this table
   is filled by scanning every *call site* in the program (see
   scan_call_args_for_struct_params below), not by scanning each
   function once in isolation. Passed by pointer at runtime either way
   (see Local.is_ref) — "value" here just means the source-level
   parameter name refers to the caller's fields, indirected through one
   pointer register, not that this pass copies bytes on every call. */
#define FN_PARAM_MAX 64
#define SCAN_LOCAL_MAX 16
typedef struct { char fn_name[64]; int param_idx; char struct_type[32]; } FnParamStruct;
static FnParamStruct fn_param_structs[FN_PARAM_MAX];
static int            n_fn_param_structs=0;

static const char *fn_param_struct_type(const char *fn_name, int idx){
    for(int i=0;i<n_fn_param_structs;i++)
        if(fn_param_structs[i].param_idx==idx && strcmp(fn_param_structs[i].fn_name,fn_name)==0)
            return fn_param_structs[i].struct_type;
    return NULL;
}
static void fn_param_struct_register(const char *fn_name, int idx, const char *struct_type){
    if(fn_param_struct_type(fn_name,idx)) return; /* first sighting wins */
    if(n_fn_param_structs>=FN_PARAM_MAX) return;
    FnParamStruct *r=&fn_param_structs[n_fn_param_structs++];
    int l1=(int)strlen(fn_name); if(l1>63) l1=63;
    memcpy(r->fn_name,fn_name,l1); r->fn_name[l1]=0;
    r->param_idx=idx;
    int l2=(int)strlen(struct_type); if(l2>31) l2=31;
    memcpy(r->struct_type,struct_type,l2); r->struct_type[l2]=0;
}

/* Given a call node and the *caller's own* current name→struct-type
   map (built the same way scan_infer_return_struct tracks one, just
   inline here instead of shared, to keep each scan self-contained),
   register any argument that's a plain identifier referring to a
   currently-known struct local as that parameter position's type on
   the callee. Deliberately narrow, matching every prior pass in this
   feature: a struct literal passed directly as an argument (`f(Point{
   x:1,y:2})`) isn't recognized — only passing an existing struct
   local through is. */
static void scan_call_args_for_struct_params(Node *call, char names[][64], char types[][32], int nlocal){
    if(!call || !call->name[0]) return;
    for(int ai=0; ai<call->argc && ai<4; ai++){
        Node *arg=call->args[ai];
        if(!arg || arg->kind!=ND_IDENT) continue;
        for(int j=0;j<nlocal;j++){
            if(strcmp(names[j],arg->name)==0){
                fn_param_struct_register(call->name, ai, types[j]);
                break;
            }
        }
    }
}

/* v2.44: walk one function's top-level statements (same narrow, non-
   recursive scope as scan_infer_return_struct — no descending into
   if/while bodies) tracking its own local struct-type map, and feed
   every call site found in a `let`/`var`/plain-assignment right-hand
   side, a bare call statement, or a `return` expression to
   scan_call_args_for_struct_params above. This has to see every
   function's calls, not just one function in isolation — a struct
   parameter is discovered from the *caller's* side, so the scan is
   driven by walking callers, then recording what it learns against
   the callee's name in the shared fn_param_structs table.
   `fn` (the function being scanned) seeds the local map with its own
   parameters wherever fn_param_struct_type already has an answer for
   them — otherwise a *forwarding* function (`fn forward(p){
   other(p) }`, called as `forward(some_struct)`) would never be
   recognized as passing a struct to `other`, since `p` is a parameter,
   not a `let`-tracked local, and nothing about forward's own body says
   it's a struct. Combined with running this whole scan to a fixpoint
   in ys_compile (see there) rather than a single pass, this lets
   struct-ness propagate through an arbitrarily long forwarding chain
   regardless of which order the functions happen to appear in the
   file — caught by testing a two-hop forward(p){ print_forwarded(p) }
   case specifically, which came back wrong (0/0 instead of the real
   values) under a single non-seeded pass. */
static void scan_fn_body_for_param_structs(Node *fn){
    Node *body=fn?fn->body:NULL;
    if(!body) return;
    char names[SCAN_LOCAL_MAX][64]; char types[SCAN_LOCAL_MAX][32]; int n=0;
    for(int pi=0; pi<fn->argc && pi<4 && n<SCAN_LOCAL_MAX; pi++){
        const char *pst=fn_param_struct_type(fn->name,pi);
        if(pst){
            int nl=(int)strlen(fn->field_names[pi]); if(nl>63) nl=63;
            memcpy(names[n],fn->field_names[pi],nl); names[n][nl]=0;
            int tl=(int)strlen(pst); if(tl>31) tl=31;
            memcpy(types[n],pst,tl); types[n][tl]=0;
            n++;
        }
    }
    for(int i=0;i<body->stmtc;i++){
        Node *s=body->stmts[i];
        if(!s) continue;
        if((s->kind==ND_LET||s->kind==ND_VAR) && s->right){
            if(s->right->kind==ND_STRUCT_LIT && n<SCAN_LOCAL_MAX){
                int nl=(int)strlen(s->name); if(nl>63) nl=63;
                memcpy(names[n],s->name,nl); names[n][nl]=0;
                int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                memcpy(types[n],s->right->name,tl); types[n][tl]=0;
                n++;
            } else if(s->right->kind==ND_CALL){
                scan_call_args_for_struct_params(s->right, names, types, n);
            }
        } else if(s->kind==ND_ASSIGN && s->right){
            const char *aname=(s->name[0])?s->name:(s->left&&s->left->name[0]?s->left->name:"");
            if(s->right->kind==ND_STRUCT_LIT){
                int found=0;
                for(int j=0;j<n;j++) if(strcmp(names[j],aname)==0){
                    int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                    memcpy(types[j],s->right->name,tl); types[j][tl]=0;
                    found=1; break;
                }
                if(!found && n<SCAN_LOCAL_MAX){
                    int nl=(int)strlen(aname); if(nl>63) nl=63;
                    memcpy(names[n],aname,nl); names[n][nl]=0;
                    int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                    memcpy(types[n],s->right->name,tl); types[n][tl]=0;
                    n++;
                }
            } else if(s->right->kind==ND_CALL){
                scan_call_args_for_struct_params(s->right, names, types, n);
            }
        } else if(s->kind==ND_CALL){
            scan_call_args_for_struct_params(s, names, types, n);
        } else if(s->kind==ND_RETURN && s->right && s->right->kind==ND_CALL){
            scan_call_args_for_struct_params(s->right, names, types, n);
        }
    }
}

/* v2.43: does this function's body return a struct, and which one?
   A lightweight dry-run scan (no codegen) over the function's *top-
   level* statements only — deliberately not descending into if/while
   bodies, matching this pass's existing "start narrow" scoping — that
   tracks a small local name→struct-type map as it walks `let`/`var`/
   plain assignment statements, then checks whether any top-level
   `return <ident>` refers to a name the map currently has as a struct.
   This has to run as its own pass, before compiling any function body
   for real, because a function can call a struct-returning function
   that's declared later in the same file — the caller's codegen
   (whether it treats the callee as struct-returning or not) needs the
   answer before it compiles the call, not after. */
static int scan_infer_return_struct(Node *body, char *out_type /* size 32 */){
    if(!body) return 0;
    char names[SCAN_LOCAL_MAX][64]; char types[SCAN_LOCAL_MAX][32]; int n=0;
    for(int i=0;i<body->stmtc;i++){
        Node *s=body->stmts[i];
        if(!s) continue;
        if((s->kind==ND_LET||s->kind==ND_VAR) && s->right && s->right->kind==ND_STRUCT_LIT){
            if(n<SCAN_LOCAL_MAX){
                int nl=(int)strlen(s->name); if(nl>63) nl=63;
                memcpy(names[n],s->name,nl); names[n][nl]=0;
                int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                memcpy(types[n],s->right->name,tl); types[n][tl]=0;
                n++;
            }
        } else if(s->kind==ND_ASSIGN && s->right && s->right->kind==ND_STRUCT_LIT){
            const char *aname=(s->name[0])?s->name:(s->left&&s->left->name[0]?s->left->name:"");
            int found=0;
            for(int j=0;j<n;j++) if(strcmp(names[j],aname)==0){
                int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                memcpy(types[j],s->right->name,tl); types[j][tl]=0;
                found=1; break;
            }
            if(!found && n<SCAN_LOCAL_MAX){
                int nl=(int)strlen(aname); if(nl>63) nl=63;
                memcpy(names[n],aname,nl); names[n][nl]=0;
                int tl=(int)strlen(s->right->name); if(tl>31) tl=31;
                memcpy(types[n],s->right->name,tl); types[n][tl]=0;
                n++;
            }
        } else if(s->kind==ND_RETURN && s->right && s->right->kind==ND_IDENT){
            for(int j=0;j<n;j++) if(strcmp(names[j],s->right->name)==0){
                memcpy(out_type,types[j],32);
                return 1;
            }
        }
    }
    return 0;
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
/* v2.44: defined after compile_expr (it's declared alongside the
   other struct-call helpers, compile_struct_lit_into/
   compile_struct_returning_call) but used from inside compile_expr's
   own ND_CALL case, hence the forward declaration here. */
static void compile_struct_arg_ptr(Local *AL);
static void compile_block(Node *b);
/* Defined in compiler_net.c (see the #include further down, after all
   of this file's shared helpers it depends on) -- forward-declared
   here because emit_helpers() below calls emit_net_helpers() before
   that #include point is reached. */
static void emit_net_helpers(void);
static int compile_net_call(Node *n, const char *fn, int is_y_net, int is_y_http, int is_y_db);

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
/* v2.43: two helpers for struct-return codegen — r10 is used as a
   scratch pointer register (never allocated to anything else in this
   file, so nothing to save/restore around it) that holds the caller's
   hidden destination address while a callee copies its return struct's
   fields into it field by field. */
/* mov r10, [rbp+off] */
static void x_mov_r10_mem(int off){
    if(off>=-128&&off<=127){ emit4(0x4c,0x8b,0x55,(uint8_t)(int8_t)off); }
    else { emit3(0x4c,0x8b,0x95); emit_i32(off); }
}
/* mov [r10+off], rax */
static void x_mov_r10off_rax(int off){
    if(off>=-128&&off<=127){ emit3(0x49,0x89,0x42); emit1((uint8_t)(int8_t)off); }
    else { emit3(0x49,0x89,0x82); emit_i32(off); }
}
/* v2.44: mov rax, [r10+off] — the read counterpart, needed for struct-
   typed *parameters* (a caller-owned struct, accessed by pointer, as
   opposed to v2.42/v2.43's directly-owned struct locals/return
   destinations, which only ever needed the write direction above). */
static void x_mov_rax_r10off(int off){
    if(off>=-128&&off<=127){ emit4(0x49,0x8b,0x42,(uint8_t)(int8_t)off); }
    else { emit3(0x49,0x8b,0x82); emit_i32(off); }
}
/* v2.45: three more helpers for native array indexing — arr[i] needs
   the element's address computed at *runtime* (i is an arbitrary
   expression, not a compile-time-constant field name the way a
   struct's field is), unlike every struct helper above which only
   ever needed a fixed, compile-time-known offset. */
/* lea r10, [rbp+off] — r10 = address of a local's block (element 0 for
   an array, matching x_mov_r10_mem's disp encoding exactly, just LEA
   instead of MOV since this computes an address, not a load). */
static void x_lea_r10_mem(int off){
    if(off>=-128&&off<=127){ emit4(0x4c,0x8d,0x55,(uint8_t)(int8_t)off); }
    else { emit3(0x4c,0x8d,0x95); emit_i32(off); }
}
/* imul rax, rax, 8 — rax = index * 8 (scale a runtime index to a byte
   offset; every element is one 8-byte word, matching every other
   fixed-size-word assumption v2.42 onward already makes). */
static void x_imul_rax_8(){ emit4(0x48,0x6b,0xc0,0x08); }
/* sub r10, rax — r10 -= index*8, giving r10 = &element[i] given r10
   already held &element[0] and elements lay out *downward* (element i
   = element0 - i*8, the same direction v2.42/v2.43/v2.44 all use for
   struct fields, so subtracting rather than adding here too — this
   backend has already paid for that sign-error lesson twice on the
   struct side; array indexing reuses the same direction on purpose,
   not by accident). */
static void x_sub_r10_rax(){ emit3(0x49,0x29,0xc2); }
/* v2.46: mov r10, [r10+off] — follow a pointer already held in r10
   (used for arr[i].field: r10 first holds &elem[i], which itself
   holds a struct pointer per v2.46's array-of-struct-pointers
   support, so this re-reads through it to get the actual struct
   address before applying a field offset). */
static void x_mov_r10_r10off(int off){
    if(off>=-128&&off<=127){ emit4(0x4d,0x8b,0x52,(uint8_t)(int8_t)off); }
    else { emit3(0x4d,0x8b,0x92); emit_i32(off); }
}
/* v2.47: push/pop r10 — needed because arr[i][j]'s inner index
   expression (j) is compiled *after* r10 already holds the inner
   array's address, and that index expression could itself be
   anything, including another struct/array access that uses r10 as
   scratch internally and would silently clobber it. Saving r10 across
   that compile_expr call and restoring it afterward avoids relying on
   "the index expression probably won't touch r10" — caught by
   reasoning about it while writing the ND_INDEX nested case, not by a
   failing test (a simple arr[i][j] with a plain variable or literal
   index wouldn't have exercised this at all). */
static void x_push_r10(){ emit2(0x41,0x52); }
static void x_pop_r10(){ emit2(0x41,0x5a); }

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

/* mov dstreg, [basereg + idxreg*8] -- 8-byte indexed load (SIB,
   scale=8), for walking pointer arrays like char** at runtime
   (colvals[i]/colnames[i] in the SQLite query_print callback).
   All three register args must be in {0..7} (rax..rdi, no REX.B/X/R
   extension) -- idxreg may not be rsp(4), which x86-64 reserves to
   mean "no index" in SIB encoding and can't actually address with. */
static void x_mov_r64_idx8(int dstreg, int basereg, int idxreg){
    emit1(0x48); emit1(0x8b);
    emit1((uint8_t)(0x04 | (dstreg<<3)));
    emit1((uint8_t)(0xc0 | (idxreg<<3) | basereg));
}

/* cmp byte [basereg+idxreg*1], imm8 -- single-byte indexed compare,
   scale=1 (unlike x_mov_r64_idx8's scale=8, since this walks raw
   bytes for an inline strlen loop, not an array of 8-byte pointers).
   Same register-range restriction as x_mov_r64_idx8 (0..7, idxreg
   can't be rsp). */
static void x_cmp_byte_idx1_imm8(int basereg, int idxreg, uint8_t imm){
    emit1(0x80); emit1(0x3c);
    emit1((uint8_t)((idxreg<<3)|basereg));
    emit1(imm);
}

/* movzx dstreg32, byte [basereg+idxreg*1] -- generic scale-1 indexed
   byte load with zero-extension (unlike x_mov_r64_idx8's scale=8,
   this is for walking a plain byte buffer one byte at a time, e.g.
   the input bytes and hex-table lookups in native pg_connect's
   runtime hex-encoding loop). Same register-range restriction as the
   other SIB helpers (0..7, idxreg can't be rsp). */
static void x_movzx_r32_idx1(int dstreg, int basereg, int idxreg){
    emit1(0x0f); emit1(0xb6);
    emit1((uint8_t)(0x04|(dstreg<<3)));
    emit1((uint8_t)((idxreg<<3)|basereg));
}

/* mov reg64, [rip+data_off]  /  mov [rip+data_off], reg64 -- generic
   RIP-relative load/store against a data-section offset (unlike
   x_lea_r64_codeaddr-style helpers, which only ever compute an
   address as a VALUE, these actually read or write the 8 bytes
   there). Used to treat a reserved data-section slot as a plain
   "global variable" scratch location for values that need to
   survive across many intermediate `call`s within one native
   builtin's inline code -- simpler than threading a dedicated stack
   frame through a long, multi-call sequence like native pg_connect's
   auth loop. Safe because none of this native runtime is
   thread-aware or reentrant to begin with. */
static void x_mov_r64_from_data(int reg, int data_off){
    emit2(0x48,0x8b);
    emit1((uint8_t)((reg<<3)|0x05));
    add_reloc(RELOC_DATA,code_len,data_off);
    emit_i32(0);
}
static void x_mov_data_from_r64(int data_off, int reg){
    emit2(0x48,0x89);
    emit1((uint8_t)((reg<<3)|0x05));
    add_reloc(RELOC_DATA,code_len,data_off);
    emit_i32(0);
}

/* lea rdx, [rip+label_code_off] -- like x_lea_arg1_data's RELOC_DATA
   version, but the target is a CODE offset (a native function's own
   starting address, e.g. a callback about to be handed to an
   external library like sqlite3_exec) rather than a data offset.
   Hardcoded to rdx since that's the only register this is currently
   needed for (sqlite3_exec's 3rd argument); generalize to a reg
   parameter if a second call site ever needs a different register. */
static void x_lea_rdx_codeaddr(int label_code_off){
    emit3(0x48,0x8d,0x15);
    add_reloc(RELOC_CODEADDR,code_len,label_code_off);
    emit_i32(0);
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
/* v2.43: set while compiling the body of a function that
   scan_infer_return_struct found to return a struct — g_cur_fn_ret_ptr_off
   is where that function's hidden return-pointer argument (rdi at
   entry) got spilled to, so ND_RETURN can find it. Empty string /
   0 respectively mean "not currently in a struct-returning function". */
static char g_cur_fn_struct_ret_type[32] = {0};
static int  g_cur_fn_struct_ret_ptr_off = 0;

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
    emit_net_helpers();
    emit_float_helper();
}

/*  string length  */
static int ystrlen(const char *s){ int n=0; while(s[n])n++; return n; }

#include "compiler_net.c"


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
        int is_y_db = n->left && n->left->kind==ND_DOT
            && strcmp(n->left->name,"db")==0
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
        if(compile_net_call(n, fn, is_y_net, is_y_http, is_y_db)) break;
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
            /* v2.44: an argument this callee's signature (per the
               scan_fn_body_for_param_structs pre-pass) expects as a
               struct is passed by address, not by value — see
               compile_struct_arg_ptr's own comment. */
            const char *pst=fn_param_struct_type(n->name,i);
            Local *AL = (pst && n->args[i] && n->args[i]->kind==ND_IDENT) ? local_find(n->args[i]->name) : NULL;
            if(AL && AL->is_struct) compile_struct_arg_ptr(AL);
            else compile_expr(n->args[i]);
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
    case ND_DOT:{
        /* v2.42: struct field read (p.x). Only a plain local identifier
           on the left is supported this pass — arr[i].field would need
           native array indexing to exist first, which it doesn't (see
           ROADMAP.md's v2.42 entry for the scope line on this); a
           left side that isn't a struct-typed local falls through to
           the same "compiles to 0" behavior an unresolved plain
           identifier already gets elsewhere in this file, rather than
           a hard compile error. */
        g_last_float=0;
        if(n->left && n->left->kind==ND_IDENT){
            Local *L=local_find(n->left->name);
            if(L && L->is_struct && L->is_ref){
                /* v2.44: struct parameter — indirect through the
                   pointer spilled at L->rbp_off. */
                int off;
                if(nstruct_field_ptr_offset(L,n->name,&off)){
                    x_mov_r10_mem(L->rbp_off);
                    x_mov_rax_r10off(off);
                    break;
                }
            } else if(L && L->is_struct){
                int off, isf;
                if(nstruct_field_offset(L,n->name,&off,&isf)){
                    x_mov_rax_mem(off);
                    g_last_float=isf;
                    break;
                }
            }
        }
        /* v2.46: arr[i].field — the array-index case falls outside
           the "n->left->kind==ND_IDENT" check above (n->left is itself
           an ND_INDEX node here), so it needs its own branch rather
           than extending that one. Only recognized when the array was
           built as an array-of-struct-pointers (Local.arr_is_struct_ptr,
           set by compile_array_lit_into) — an array of plain scalars
           falls through to the same "compiles to 0" default every
           other unresolved access in this file already gets. */
        if(n->left && n->left->kind==ND_INDEX && n->left->left && n->left->left->kind==ND_IDENT){
            Local *AL=local_find(n->left->left->name);
            if(AL && AL->is_array && AL->arr_is_struct_ptr){
                int foff;
                if(nstruct_field_offset_by_type(AL->arr_struct_type,n->name,&foff)){
                    compile_expr(n->left->right); /* index -> rax */
                    x_imul_rax_8();
                    x_lea_r10_mem(AL->rbp_off);
                    x_sub_r10_rax();        /* r10 = &elem[i] */
                    x_mov_r10_r10off(0);    /* r10 = elem[i] itself (the struct pointer) */
                    x_mov_rax_r10off(foff);
                    break;
                }
            }
        }
        x_mov_rax_imm32(0);
        break;
    }
    case ND_INDEX:{
        /* v2.45: array index read (arr[i]), i an arbitrary runtime
           expression. obj.field[i] still isn't attempted. A left side
           that isn't an array-typed local, or an out-of-range index,
           both fall through to the same "compiles to 0" behavior the
           rest of this file already uses for an unresolved access,
           rather than a hard compile error or a runtime bounds check
           (this backend has neither the infrastructure nor, for a
           first pass, the stated need for bounds-checked array
           access — see ROADMAP.md's v2.45 entry). */
        g_last_float=0;
        if(n->left && n->left->kind==ND_IDENT){
            Local *L=local_find(n->left->name);
            if(L && L->is_array){
                compile_expr(n->right); /* index -> rax */
                x_imul_rax_8();
                x_lea_r10_mem(L->rbp_off);
                x_sub_r10_rax();        /* r10 = &elem[i] */
                x_mov_rax_r10off(0);
                break;
            }
        }
        /* v2.47: arr[i][j] — exactly two levels, array-of-array-
           pointers (Local.arr_is_array_ptr, set by
           compile_array_lit_into) only; arr[i][j][k] isn't attempted.
           n->left here is itself an ND_INDEX (arr[i]), whose own left
           must be a plain array-local identifier — the inner index
           chain isn't walked any deeper than that one extra level, a
           deliberate scope line rather than a recursion depth this
           code happens to fall short of: going further would need
           arrays-of-arrays-of-arrays to exist as a concept at all,
           which nothing in this feature line has built yet. */
        if(n->left && n->left->kind==ND_INDEX && n->left->left && n->left->left->kind==ND_IDENT){
            Local *L=local_find(n->left->left->name);
            if(L && L->is_array && L->arr_is_array_ptr){
                compile_expr(n->left->right); /* outer index i -> rax */
                x_imul_rax_8();
                x_lea_r10_mem(L->rbp_off);
                x_sub_r10_rax();         /* r10 = &outer_elem[i] */
                x_mov_r10_r10off(0);     /* r10 = outer_elem[i] itself (inner array's address) */
                x_push_r10();            /* save — n->right may itself use r10 as scratch */
                compile_expr(n->right);  /* inner index j -> rax */
                x_imul_rax_8();
                x_pop_r10();             /* restore inner array's address */
                x_sub_r10_rax();         /* r10 = &inner[j] */
                x_mov_rax_r10off(0);
                break;
            }
        }
        x_mov_rax_imm32(0);
        break;
    }
    default:
        x_mov_rax_imm32(0);
        break;
    }
}

/* v2.42: write a struct literal (Point{x: 1, y: 2}) directly into a
   struct-typed local's stack slots, field by field, in the *struct
   declaration's* field order (not necessarily the literal's — a
   literal is allowed to list fields in any order, same as the
   interpreter's ND_STRUCT_LIT/eval.c). A field the literal doesn't
   mention is left at whatever was already in that stack slot (0 on
   first entry into a fresh frame in practice, since this backend
   never reuses a frame's raw memory for anything else beforehand) —
   matching this pass's stack-block-per-local storage model, which has
   no separate "uninitialized" representation to write instead. */
static void compile_struct_lit_into(Node *lit, Local *L){
    NStructDef *def=nstruct_find(L->struct_type);
    if(!def) return;
    for(int fi=0; fi<def->nfields; fi++){
        for(int li=0; li<lit->argc; li++){
            if(strcmp(lit->field_names[li],def->fields[fi])==0){
                compile_expr(lit->args[li]);
                int off=L->rbp_off - fi*8;
                x_mov_mem_rax(off);
                L->field_is_float[fi]=g_last_float;
                break;
            }
        }
    }
}

/* v2.45: write an array literal ([1, 2, 3]) directly into an array-
   typed local's stack slots, element by element. Element count/node
   access mirrors eval.c's own ND_ARRAY handling exactly
   (`nc = stmtc>0 ? stmtc : argc`, element i from stmts[i] or args[i])
   — the parser apparently uses one or the other depending on which
   literal syntax was used, so this has to check the same way eval.c
   already does rather than assuming one is always populated.
   v2.46 addition: if every element is a plain identifier referring to
   an *existing* struct local (`let arr = [p, q]`), store each
   element as a pointer to that struct instead of a scalar word — the
   same compile_struct_arg_ptr helper v2.44 already built for passing
   a struct to a function parameter, reused here rather than
   duplicated — and record the array's element struct type on L so
   `arr[i].field` can resolve it later. Narrow on purpose: this is
   decided from the *first* element only (an array mixing struct and
   non-struct elements, or whose struct elements are different struct
   types, isn't validated or specially handled — later elements are
   still written as struct pointers if the first one was, which is
   wrong for a genuinely mixed array, but mixed-type arrays aren't a
   case this pass claims to support in the first place); an inline
   struct literal as an array element (`[Point{x:1,y:2}]`, as opposed
   to an existing struct local) isn't recognized either, matching
   every other "forward an existing local, not an inline literal"
   restriction already established throughout this feature. */
static void compile_array_lit_into(Node *lit, Local *L){
    int nc = (lit->stmtc > 0) ? lit->stmtc : lit->argc;
    if(nc>L->arr_nelems) nc=L->arr_nelems;
    if(nc>0){
        Node *el0 = (lit->stmtc > 0) ? lit->stmts[0] : lit->args[0];
        if(el0 && el0->kind==ND_IDENT){
            Local *AL0=local_find(el0->name);
            if(AL0 && AL0->is_struct){
                L->arr_is_struct_ptr=1;
                strncpy(L->arr_struct_type,AL0->struct_type,31); L->arr_struct_type[31]=0;
            } else if(AL0 && AL0->is_array){
                /* v2.47: `let arr = [a, b]` where a, b are themselves
                   array locals — same "decide from the first element"
                   rule as the struct-pointer case above. */
                L->arr_is_array_ptr=1;
            }
        }
    }
    for(int i=0;i<nc;i++){
        Node *el = (lit->stmtc > 0) ? lit->stmts[i] : lit->args[i];
        if(L->arr_is_struct_ptr && el && el->kind==ND_IDENT){
            Local *AL=local_find(el->name);
            if(AL && AL->is_struct){ compile_struct_arg_ptr(AL); x_mov_mem_rax(L->rbp_off - i*8); continue; }
        }
        if(L->arr_is_array_ptr && el && el->kind==ND_IDENT){
            Local *AL=local_find(el->name);
            if(AL && AL->is_array){
                /* address of AL's element 0 (lea rax,[rbp+AL->rbp_off]) */
                if(AL->rbp_off>=-128 && AL->rbp_off<=127){ emit3(0x48,0x8d,0x45); emit1((uint8_t)(int8_t)AL->rbp_off); }
                else { emit3(0x48,0x8d,0x85); emit_i32(AL->rbp_off); }
                x_mov_mem_rax(L->rbp_off - i*8);
                continue;
            }
        }
        compile_expr(el);
        x_mov_mem_rax(L->rbp_off - i*8);
    }
}

/* v2.44: put the *address* of a struct-typed argument in rax, for a
   call site passing it to a parameter the fn_param_struct_type scan
   found to be a struct. If AL is a directly-owned struct local
   (v2.42's local_alloc_struct), that's a fresh lea of its block's
   base. If AL is itself a struct *parameter* being forwarded to
   another call (AL->is_ref), the local already holds a pointer —
   passing it on means loading that pointer's value, not lea'ing the
   local's own slot (which would take the address of the pointer
   variable, not the struct it points to). */
static void compile_struct_arg_ptr(Local *AL){
    if(AL->is_ref){
        x_mov_rax_mem(AL->rbp_off);
        return;
    }
    if(AL->rbp_off>=-128 && AL->rbp_off<=127){
        emit3(0x48,0x8d,0x45); emit1((uint8_t)(int8_t)AL->rbp_off);
    } else {
        emit3(0x48,0x8d,0x85); emit_i32(AL->rbp_off);
    }
}

/* v2.43: call a struct-returning function, writing its result directly
   into destL's slots rather than through rax — see fn_struct_returns'
   comment for why this uses a Yolish-only hidden-pointer convention
   for every struct-returning call rather than real SysV classification.
   Args are pushed then popped in reverse, same pattern the plain
   ND_CALL user-function path already uses, so evaluating one arg can't
   clobber a register another arg's value is sitting in. Capped at 3
   real arguments (rsi/rdx/rcx) since rdi is reserved for the hidden
   pointer — one fewer than a normal function gets, documented in
   ROADMAP.md's v2.43 entry, not silently different from plain calls.
   v2.44 addition: an argument the callee expects as a struct (per
   fn_param_struct_type) is passed by address via compile_struct_arg_ptr
   instead of compile_expr, same as the plain-call path below. */
static void compile_struct_returning_call(Node *call, Local *destL){
    static const uint8_t arg_regs[][3]={
        {0x48,0x89,0xc6}, /* mov rsi,rax */
        {0x48,0x89,0xc2}, /* mov rdx,rax */
        {0x48,0x89,0xc1}, /* mov rcx,rax */
    };
    int nargs=call->argc; if(nargs>3) nargs=3;
    for(int i=0;i<nargs;i++){
        const char *pst=fn_param_struct_type(call->name,i);
        Local *AL = (pst && call->args[i] && call->args[i]->kind==ND_IDENT) ? local_find(call->args[i]->name) : NULL;
        if(AL && AL->is_struct) compile_struct_arg_ptr(AL);
        else compile_expr(call->args[i]);
        x_push_rax();
    }
    for(int i=nargs-1;i>=0;i--){ x_pop_rax(); emit3(arg_regs[i][0],arg_regs[i][1],arg_regs[i][2]); }
    /* rdi = &destL (lea rdi,[rbp+destL->rbp_off]) */
    if(destL->rbp_off>=-128 && destL->rbp_off<=127){
        emit3(0x48,0x8d,0x7d); emit1((uint8_t)(int8_t)destL->rbp_off);
    } else {
        emit3(0x48,0x8d,0xbd); emit_i32(destL->rbp_off);
    }
    int p=x_call_unresolved(); add_call_patch(p,call->name);
}

/*  compile statement  */
static void compile_node(Node *n){
    if(!n) return;
    switch(n->kind){
    case ND_LET:
    case ND_VAR:{
        /* v2.42: `let p = Point{x: 1, y: 2}` — the right side is a
           struct literal, so it doesn't go through compile_expr/rax
           at all (a struct doesn't fit in one register); allocate the
           struct-typed local's whole block up front and write every
           field straight into its own slot instead. */
        if(n->right && n->right->kind==ND_STRUCT_LIT){
            Local *L=local_alloc_struct(n->name, n->right->name);
            compile_struct_lit_into(n->right, L);
            break;
        }
        /* v2.45: `let arr = [1, 2, 3]` — same idea as the struct
           literal case just above, fixed-size and stack-allocated
           rather than going through compile_expr/rax. */
        if(n->right && n->right->kind==ND_ARRAY){
            int nc = (n->right->stmtc > 0) ? n->right->stmtc : n->right->argc;
            Local *L=local_alloc_array(n->name, nc);
            compile_array_lit_into(n->right, L);
            break;
        }
        /* v2.43: `let q = make_point(1, 2)` where make_point is a
           known struct-returning function — same idea, but the
           source of the fields is the callee's own hidden-pointer
           write rather than a literal compiled locally. */
        if(n->right && n->right->kind==ND_CALL){
            const char *rt=fn_returns_struct(n->right->name);
            if(rt){
                Local *L=local_alloc_struct(n->name, rt);
                compile_struct_returning_call(n->right, L);
                break;
            }
        }
        compile_expr(n->right);
        int off=local_alloc(n->name);
        for(int _i=0;_i<nlocals;_i++) if(strcmp(locals[_i].name,n->name)==0){locals[_i].is_float=g_last_float;break;}
        x_mov_mem_rax(off); break;
    }
    case ND_ASSIGN:{
        /* target name is in n->left->name (parser stores ident as left child) */
        const char *aname = (n->name[0]) ? n->name
                          : (n->left && n->left->name[0]) ? n->left->name : "";
        /* v2.46: arr[i].field = v — field write through an array-of-
           struct-pointers element. Checked before the plain obj.field
           branch below, since n->left->left is an ND_INDEX node here,
           not an ND_IDENT — this is a genuinely different shape, not
           an extension of that branch's existing ND_IDENT check. */
        if(n->left && n->left->kind==ND_DOT && n->left->left && n->left->left->kind==ND_INDEX
           && n->left->left->left && n->left->left->left->kind==ND_IDENT){
            Local *AL=local_find(n->left->left->left->name);
            if(AL && AL->is_array && AL->arr_is_struct_ptr){
                int foff;
                if(nstruct_field_offset_by_type(AL->arr_struct_type,n->left->name,&foff)){
                    compile_expr(n->right);          /* value -> rax */
                    x_push_rax();
                    compile_expr(n->left->left->right); /* index -> rax */
                    x_imul_rax_8();
                    x_lea_r10_mem(AL->rbp_off);
                    x_sub_r10_rax();                 /* r10 = &elem[i] */
                    x_mov_r10_r10off(0);              /* r10 = elem[i] (struct pointer) */
                    x_pop_rax();                      /* restore value */
                    x_mov_r10off_rax(foff);
                }
                break;
            }
        }
        /* v2.42: obj.field = v — field write on a struct-typed local.
           Only a plain local identifier on the left of the dot is
           supported this pass, same limitation as ND_DOT's read side
           above (no native array indexing yet to support arr[i].field). */
        if(n->left && n->left->kind==ND_DOT && n->left->left && n->left->left->kind==ND_IDENT){
            Local *L=local_find(n->left->left->name);
            if(L && L->is_struct && L->is_ref){
                /* v2.44: field write through a struct parameter. */
                int off;
                if(nstruct_field_ptr_offset(L,n->left->name,&off)){
                    compile_expr(n->right);
                    x_mov_r10_mem(L->rbp_off);
                    x_mov_r10off_rax(off);
                }
                break;
            } else if(L && L->is_struct){
                NStructDef *def=nstruct_find(L->struct_type);
                if(def){
                    for(int fi=0; fi<def->nfields; fi++){
                        if(strcmp(def->fields[fi],n->left->name)==0){
                            compile_expr(n->right);
                            int off=L->rbp_off - fi*8;
                            x_mov_mem_rax(off);
                            L->field_is_float[fi]=g_last_float;
                            break;
                        }
                    }
                }
                break;
            }
        }
        /* v2.47: arr[i][j] = v — array-of-array-pointers write, same
           exactly-two-levels scope as the ND_INDEX read case, and the
           same r10-clobber precaution (save/restore across compiling
           the inner index, since it could itself touch r10). Checked
           before the single-level branch right below, since
           n->left->left is itself an ND_INDEX here, not an ND_IDENT —
           a different shape, not an extension of that branch. */
        if(n->left && n->left->kind==ND_INDEX && n->left->left && n->left->left->kind==ND_INDEX
           && n->left->left->left && n->left->left->left->kind==ND_IDENT){
            Local *L=local_find(n->left->left->left->name);
            if(L && L->is_array && L->arr_is_array_ptr){
                compile_expr(n->right);           /* value -> rax */
                x_push_rax();
                compile_expr(n->left->left->right); /* outer index i -> rax */
                x_imul_rax_8();
                x_lea_r10_mem(L->rbp_off);
                x_sub_r10_rax();                  /* r10 = &outer_elem[i] */
                x_mov_r10_r10off(0);              /* r10 = inner array's address */
                x_push_r10();
                compile_expr(n->left->right);     /* inner index j -> rax */
                x_imul_rax_8();
                x_pop_r10();
                x_sub_r10_rax();                  /* r10 = &inner[j] */
                x_pop_rax();                       /* restore value */
                x_mov_r10off_rax(0);
                break;
            }
        }
        /* v2.45: arr[i] = v — array index write, same plain-local-only
           scope as every struct branch above (n->left->left must be a
           direct ND_IDENT referring to an array local; obj.field[i]
           isn't attempted this pass). */
        if(n->left && n->left->kind==ND_INDEX && n->left->left && n->left->left->kind==ND_IDENT){
            Local *L=local_find(n->left->left->name);
            if(L && L->is_array){
                compile_expr(n->right);   /* value to store -> rax */
                x_push_rax();
                compile_expr(n->left->right); /* index -> rax */
                x_imul_rax_8();
                x_lea_r10_mem(L->rbp_off);
                x_sub_r10_rax();          /* r10 = &elem[i] */
                x_pop_rax();              /* restore value */
                x_mov_r10off_rax(0);
                break;
            }
        }
        /* v2.42: `p = Point{...}` — reassigning a whole struct-typed
           local to a fresh literal. Same direct field-by-field write
           as ND_LET/ND_VAR above, reusing the existing block if `p`
           is already this struct type rather than growing the frame
           again (see local_alloc_struct). */
        if(n->right && n->right->kind==ND_STRUCT_LIT){
            Local *L=local_alloc_struct(aname, n->right->name);
            compile_struct_lit_into(n->right, L);
            break;
        }
        /* v2.45: `arr = [1, 2, 3]` — reassigning a whole array-typed
           local to a fresh literal, same pattern as the struct case
           just above. */
        if(n->right && n->right->kind==ND_ARRAY){
            int nc = (n->right->stmtc > 0) ? n->right->stmtc : n->right->argc;
            Local *L=local_alloc_array(aname, nc);
            compile_array_lit_into(n->right, L);
            break;
        }
        /* v2.43: `p = make_point(1, 2)` — same struct-returning-call
           path ND_LET/ND_VAR use above. */
        if(n->right && n->right->kind==ND_CALL){
            const char *rt=fn_returns_struct(n->right->name);
            if(rt){
                Local *L=local_alloc_struct(aname, rt);
                compile_struct_returning_call(n->right, L);
                break;
            }
        }
        compile_expr(n->right);
        int off=local_get(aname); if(off==0) off=local_alloc(aname);
        for(int _i=0;_i<nlocals;_i++) if(strcmp(locals[_i].name,aname)==0){locals[_i].is_float=g_last_float;break;}
        x_mov_mem_rax(off); break;
    }
    case ND_STRUCT:
        /* v2.42: declarations are registered into nstruct_defs by the
           dedicated scan in ys_compile, before any function body is
           compiled — nothing left to emit here. */
        break;
    case ND_RETURN:{
        /* v2.43: `return p` inside a function scan_infer_return_struct
           already determined returns a struct — copy p's fields into
           the caller's hidden destination (held in r10, loaded from
           where the hidden pointer arg was spilled at function entry)
           instead of trying to fit a whole struct through rax. Falls
           through to the plain scalar path below for anything that
           doesn't match (returning a non-struct expression from a
           struct-returning function isn't meaningful Yolish and isn't
           specially handled — same "compiles to something rather than
           erroring" posture the rest of this pass already takes). */
        if(g_cur_fn_struct_ret_type[0] && n->right && n->right->kind==ND_IDENT){
            Local *L=local_find(n->right->name);
            if(L && L->is_struct && strcmp(L->struct_type,g_cur_fn_struct_ret_type)==0){
                NStructDef *def=nstruct_find(L->struct_type);
                if(def){
                    x_mov_r10_mem(g_cur_fn_struct_ret_ptr_off);
                    for(int fi=0; fi<def->nfields; fi++){
                        /* fields lay out *downward* from field 0 (see
                           local_alloc_struct: field i = base - i*8),
                           and r10 holds field 0's own address — so
                           field i's address relative to r10 is -i*8,
                           not +i*8. First version of this loop used
                           +fi*8 and wrote field 1 back on top of field
                           0's slot (and field 0 one slot past the end)
                           — caught immediately by testing: q.x came
                           back right, q.y came back as 0 every time. */
                        x_mov_rax_mem(L->rbp_off - fi*8);
                        x_mov_r10off_rax(-fi*8);
                    }
                    x_mov_rax_mem(g_cur_fn_struct_ret_ptr_off); /* rax=hidden ptr too, harmless extra courtesy for any caller that happens to check it */
                    x_mov_rsp_rbp(); x_pop_rbp(); x_ret();
                    break;
                }
            }
        }
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
        /* v2.43: is this a function scan_infer_return_struct found to
           return a struct? If so, rdi at entry is the caller's hidden
           destination pointer, not this function's first real
           parameter — spill it immediately to its own local slot (so
           ND_RETURN can find it later; nothing here tries to keep it
           in a register across the whole function body, since this
           compiler doesn't track register liveness across statements)
           and shift real parameters to start at rsi instead of rdi. */
        const char *ret_struct_type=fn_returns_struct(n->name);
        if(ret_struct_type){
            g_cur_fn_struct_ret_ptr_off=local_alloc("__struct_ret_ptr__");
        }
        memcpy(g_cur_fn_struct_ret_type, ret_struct_type?ret_struct_type:"", ret_struct_type?strlen(ret_struct_type)+1:1);
        /* allocate parameters as locals */
        /* SysV ABI: args in rdi, rsi, rdx, rcx, r8, r9 */
        /* We store each arg onto the stack: mov [rbp+off], reg */
        /* ModRM bytes for mov [rbp+disp8], rdi/rsi/rdx/rcx */
        {
            static const uint8_t modrm[]={0x7d,0x75,0x55,0x4d};
            /* v2.42 fix: a real, pre-existing bug, found while testing
               struct support — parser.c's TK_FN case (see its own
               comment: "parse parameter names into field_names[]")
               stores parameter names in n->field_names[], the same
               place eval.c's own function-call binding
               (`env_def(fe,fd->field_names[i],arg)`) reads them from.
               n->args is never populated for ND_FN at all — it's only
               ever set on ND_CALL. Reading n->args[pi]->name here was
               therefore always a NULL-pointer dereference in a
               statically-linked binary the moment ANY native-compiled
               function had one or more parameters — not a struct-
               related bug, just never previously exercised, since
               ROADMAP.md's own native-compiler examples/regression
               notes don't mention compiling a plain user function with
               parameters. Confirmed by reproducing the crash against
               the pre-struct-work compiler too. */
            if(ret_struct_type){
                /* rdi is the hidden destination pointer here, not a
                   real parameter — spill it to its own slot with a
                   direct mov (not through the generic per-slot loop
                   below, since that loop's index-to-register mapping
                   is for real parameters only). Real params start at
                   modrm[1] (rsi), capped at 3 (rsi,rdx,rcx). */
                if(g_cur_fn_struct_ret_ptr_off>=-128 && g_cur_fn_struct_ret_ptr_off<=127){
                    emit4(0x48,0x89,(uint8_t)(modrm[0]|0x40),(uint8_t)(int8_t)g_cur_fn_struct_ret_ptr_off);
                } else {
                    emit3(0x48,0x89,(uint8_t)(modrm[0]|0x80)); emit_i32(g_cur_fn_struct_ret_ptr_off);
                }
                for(int pi=0; pi<n->argc && pi<3; pi++){
                    const char *pst=fn_param_struct_type(n->name,pi);
                    int poff = pst ? local_alloc_struct_ref(n->field_names[pi],pst)->rbp_off
                                   : local_alloc(n->field_names[pi]);
                    int mi=pi+1; /* rsi,rdx,rcx */
                    if(poff>=-128&&poff<=127){
                        emit4(0x48,0x89,(uint8_t)(modrm[mi]|0x40),(uint8_t)(int8_t)poff);
                    } else {
                        emit3(0x48,0x89,(uint8_t)(modrm[mi]|0x80)); emit_i32(poff);
                    }
                }
            } else {
                for(int pi=0; pi<n->argc && pi<4; pi++){
                    const char *pst=fn_param_struct_type(n->name,pi);
                    int poff = pst ? local_alloc_struct_ref(n->field_names[pi],pst)->rbp_off
                                   : local_alloc(n->field_names[pi]);
                    if(poff>=-128&&poff<=127){
                        emit4(0x48,0x89,(uint8_t)(modrm[pi]|0x40),(uint8_t)(int8_t)poff);
                    } else {
                        emit3(0x48,0x89,(uint8_t)(modrm[pi]|0x80)); emit_i32(poff);
                    }
                }
            }
        }
        /* compile body */
        compile_block(n->body);
        g_cur_fn_struct_ret_type[0]=0;
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
    int entry_off,
    int *ca_code, int *ca_target, int n_ca);

extern int elf_write_dynamic(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off,
    const char **needed_libs, int nneeded,
    const char **import_names, int *import_got_offs, int nimports,
    int *ca_code, int *ca_target, int n_ca);

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

    /* v2.42: register struct declarations before anything else — a
       function may use a struct declared later in the same file
       (top-level declaration order shouldn't matter, same as it
       doesn't for ND_FN below), so this has to be its own pass ahead
       of the function-compiling pass, not folded into it. */
    n_nstruct_defs=0;
    for(int i=0;i<prog->stmtc;i++){
        Node *n=prog->stmts[i];
        if(n && n->kind==ND_STRUCT) nstruct_register(n);
    }

    /* v2.43: which functions return a struct, and which one — must run
       after struct registration (needs nstruct_defs to make sense of
       what a literal's type name refers to isn't actually required by
       the scan itself, but keeping struct-related setup together
       avoids ordering surprises) and before any function body compiles
       for real, so a caller's codegen already knows whether a callee
       returns a struct by the time it needs to compile a call to it,
       regardless of which function is declared first in the file. */
    n_fn_struct_returns=0;
    for(int i=0;i<prog->stmtc;i++){
        Node *n=prog->stmts[i];
        if(n && n->kind==ND_FN){
            char rt[32];
            if(scan_infer_return_struct(n->body, rt)) fn_struct_return_register(n->name, rt);
        }
    }

    /* v2.44: struct-typed function *parameters* — discovered from call
       sites (see scan_fn_body_for_param_structs' own comment for why),
       so this has to walk every function's body looking for calls,
       not just each function's own signature. Runs after the return-
       type scan above (no ordering dependency between the two, but
       keeping all of v2.42/v2.43/v2.44's struct-signature setup
       together avoids surprises) and, like it, before any function
       body compiles for real.
       Run to a fixpoint (bounded at 8 rounds — generous headroom over
       any forwarding chain a real program is likely to have, and
       cheap to re-run since these are small top-level-only scans) so
       struct-ness propagates through a forwarding chain — a function
       that only passes its own struct parameter on to another call —
       regardless of which order the functions are declared in the
       file. A single pass isn't enough: scan_fn_body_for_param_structs
       seeds each function's local map from what's *already* known
       about its own parameters, so a chain of N forwarding hops needs
       up to N passes before the last hop's parameter type is known. */
    n_fn_param_structs=0;
    for(int round=0; round<8; round++){
        int before=n_fn_param_structs;
        for(int i=0;i<prog->stmtc;i++){
            Node *n=prog->stmts[i];
            if(n && n->kind==ND_FN) scan_fn_body_for_param_structs(n);
        }
        if(n_fn_param_structs==before) break; /* no new signatures learned this round */
    }

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
    static int ca_off[RELOC_MAX], ca_target[RELOC_MAX]; int n_ca=0;
    for(int i=0;i<nrelocs;i++){
        if(relocs[i].kind==RELOC_DATA){
            rc[nr]=relocs[i].code_off;
            rd[nr]=relocs[i].target_off;
            nr++;
        } else if(relocs[i].kind==RELOC_CODE){
            ic_off[n_ic]=relocs[i].code_off;
            ic_idx[n_ic]=relocs[i].target_off; /* import index */
            n_ic++;
        } else if(relocs[i].kind==RELOC_CODEADDR){
            ca_off[n_ca]=relocs[i].code_off;
            ca_target[n_ca]=relocs[i].target_off; /* code offset of the referenced label */
            n_ca++;
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
                                   lnames,g_dyn_nneeded,inames,igots,g_dyn_nimports,ca_off,ca_target,n_ca);
        } else {
            ret=elf_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off,ca_off,ca_target,n_ca);
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