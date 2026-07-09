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
    emit_float_helper();
}

/*  string length  */
static int ystrlen(const char *s){ int n=0; while(s[n])n++; return n; }

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
        /* user function call — pass args in registers (SysV) */
        /* translate "main" to "__ys_main" for call */
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

/* resolve all call patches */
static void resolve_calls(void){
    for(int i=0;i<ncall_patches;i++){
        int target=sym_find(call_patches[i].target);
        if(target<0){
            fprintf(stderr,"ys: unresolved symbol: %s\n",call_patches[i].target);
            continue;
        }
        int off=call_patches[i].code_off;
        patch_i32(off,(int32_t)(target-(off+4)));
    }
}

/*  public compile function  */

/*  output format declarations  */
extern int elf_write(const char *path,
    uint8_t *code, int code_len,
    uint8_t *data, int data_len,
    int *reloc_code, int *reloc_data, int nrelocs,
    int entry_off);

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
}

/*  compile_program  */
void ys_compile(Node *prog, Target target, const char *outfile){
    if(!prog){ fprintf(stderr,"ys: no AST to compile\n"); return; }
    g_target=target;

    code_len=0; data_len=0; nrelocs=0;
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
    resolve_calls();

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
        ret=elf_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off);
        break;
    case TARGET_MACOS:
        ret=macho_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off);
        break;
    case TARGET_WINDOWS:
        ret=pe_write(outfile,code_buf,code_len,data_buf,data_len,rc,rd,nr,entry_off,ic_off,ic_idx,n_ic);
        break;
    }
    if(ret==0) fprintf(stdout,"ys: compiled → %s\n",outfile);
}