/* 
   bcompiler.c  —  v2.0 (Bytecode VM)
   Walks the existing AST (Node*, produced by parser.c — completely
   unchanged) and emits bytecode into a Chunk. This is a *subset*
   compiler for v2.0: it covers the core language (literals, operators,
   variables, if/while/for-in, break/continue, match/match-guard,
   functions (with implicit last-expression return), arrays, structs,
   builtins) so the VM can be benchmarked and validated end-to-end.
   Closures-with-capture, try/catch, enums, and modules still fall
   back to the AST interpreter (eval.c) — `ys vm` reports which
   construct it hit and exits cleanly rather than miscompiling.
*/

#include "bcompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*  compiler state  */

#define MAX_LOCALS 256
#define MAX_LOOP_DEPTH 32
#define MAX_BREAKS_PER_LOOP 64

typedef struct {
    char name[64];
} LocalSlot;

/* v2.6: tracks a single enclosing loop so break/continue can be
   compiled to real jumps instead of falling back to the AST
   interpreter. Both break and continue are deferred forward jumps:
   for while-loops the continue target (the condition re-check) is
   technically known before the body compiles, but for for-loops the
   continue target is the increment step, which is emitted *after*
   the body — so continue is compiled as a placeholder jump in both
   cases and patched by the enclosing loop once the real target
   address is known, exactly like break already is. */
typedef struct {
    int break_jumps[MAX_BREAKS_PER_LOOP];
    int break_count;
    int continue_jumps[MAX_BREAKS_PER_LOOP];
    int continue_count;
    int body_base_locals; /* CUR->local_count right before the loop body
                              compiles — locals the body declares (directly,
                              or via any nested loop/if) must be popped back
                              down to this depth before each new iteration
                              and at every break/continue, since nothing
                              else in this compiler frees locals when a
                              runtime scope (loop iteration) ends. Without
                              this, a second+ iteration's freshly pushed
                              local lands on top of the stale one instead of
                              overwriting it, corrupting every fixed slot
                              index baked into the body's bytecode. */
} LoopCtx;

typedef struct BCompiler {
    Chunk            *chunk;
    LocalSlot         locals[MAX_LOCALS];
    int               local_count;
    int               in_function;   /* 0 = top-level globals, 1 = function body locals */
    struct BCompiler *enclosing;
    int               had_error;
    LoopCtx           loop_stack[MAX_LOOP_DEPTH];
    int               loop_depth;
} BCompiler;

static BCompiler *CUR = NULL; /* current compiler — simplifies the many
                                   small emit helpers below; restored via
                                   save/restore around nested fn compiles */

/* v2.6: unique suffix generator for hidden compiler-internal variables
   (for-loop bounds/index/iterable). Prefixed with '$', a character the
   lexer never produces inside an identifier, so these can never collide
   with a real user-declared name in either local or global scope. */
static int g_temp_counter = 0;

/*  low-level emitters  */

static void emit_byte(unsigned char b, int line){ chunk_write(CUR->chunk, b, line); }

static void emit_u16(int v, int line){
    emit_byte((unsigned char)((v>>8)&0xFF), line);
    emit_byte((unsigned char)(v&0xFF), line);
}

static int emit_const(Val v, int line){
    int idx = chunk_add_const(CUR->chunk, v);
    emit_byte(OP_CONST, line);
    emit_u16(idx, line);
    return idx;
}

static int emit_jump(unsigned char op, int line){
    emit_byte(op, line);
    int at = CUR->chunk->count;
    emit_u16(0, line); /* placeholder, patched later */
    return at;
}

static void patch_jump_here(int at){
    chunk_patch_jump(CUR->chunk, at, CUR->chunk->count);
}

/* forward decls — full definitions live in the "local variable table"
   section further down; needed here because the generic var helpers
   below are used by both that section's callers and the for-loop
   compiler, and emit_declare/emit_load/emit_store_and_pop are simplest
   to keep next to the other emit_* helpers at the top of the file. */
static int resolve_local(const char *name);
static int add_local(const char *name);
static Val bc_str(const char *s,int len);

/* Generic variable helpers (local-or-global aware), used by the for-loop
   compiler below to declare/read/write both the user's loop variable and
   the compiler's own hidden bookkeeping variables (bounds/index/iterable)
   with identical scoping rules to ND_LET/ND_IDENT/ND_ASSIGN. */

static void emit_declare(const char *name, int line){
    /* Value already sits on top of the stack. For a local, this just
       claims that stack position as the named slot (same trick as
       ND_LET — see the comment there). For a global, it pops the value
       into the named global slot. */
    if(CUR->in_function){
        (void)add_local(name);
    } else {
        int idx=chunk_add_const(CUR->chunk, bc_str(name,(int)strlen(name)));
        emit_byte(OP_DEFINE_GLOBAL,line); emit_u16(idx,line);
    }
}
static void emit_load(const char *name, int line){
    int slot=CUR->in_function?resolve_local(name):-1;
    if(slot>=0){ emit_byte(OP_GET_LOCAL,line); emit_u16(slot,line); }
    else {
        int idx=chunk_add_const(CUR->chunk, bc_str(name,(int)strlen(name)));
        emit_byte(OP_GET_GLOBAL,line); emit_u16(idx,line);
    }
}
static void emit_store_and_pop(const char *name, int line){
    /* Value on top of stack -> write into the existing slot, then
       discard the peeked-back copy (SET_LOCAL/SET_GLOBAL don't pop). */
    int slot=CUR->in_function?resolve_local(name):-1;
    if(slot>=0){ emit_byte(OP_SET_LOCAL,line); emit_u16(slot,line); }
    else {
        int idx=chunk_add_const(CUR->chunk, bc_str(name,(int)strlen(name)));
        emit_byte(OP_SET_GLOBAL,line); emit_u16(idx,line);
    }
    emit_byte(OP_POP,line);
}

/* Pops locals (both the runtime stack slots via emitted OP_POP, and the
   compiler's own bookkeeping) back down to `target`. No-op outside a
   function (globals don't grow the stack, so they never need this). */
static void emit_pop_locals_to(int target, int line){
    if(!CUR->in_function) return;
    while(CUR->local_count>target){
        emit_byte(OP_POP,line);
        CUR->local_count--;
    }
}

/* Collapses every local declared since `base` into a single anonymous
   value: whatever's on top of the stack (the scope's "result") gets
   written into the scope's first slot (slot number `base`), then every
   slot above it — including the now-duplicate top — is popped, and the
   compiler stops treating any of them as named locals. Needed wherever
   an expression construct's own internals (e.g. match's hidden subject
   variable, a bound pattern name, or a `let` inside a match arm's body)
   must go out of scope at exactly the point the expression produces its
   value — plain emit_pop_locals_to can't be used there since it always
   pops from the top, which would discard the result, not the locals
   underneath it. No-op outside a function (globals never grow the
   stack) or if nothing was actually declared since `base`. */
static void emit_collapse_scope(int base, int line){
    if(!CUR->in_function) return;
    if(CUR->local_count<=base) return;
    emit_byte(OP_SET_LOCAL,line); emit_u16(base,line);
    int npops=CUR->local_count-base;
    for(int i=0;i<npops;i++) emit_byte(OP_POP,line);
    CUR->local_count=base;
}

/*  value constructors (mirroring eval.c's make_*; duplicated here
   on purpose — bcompiler.c must not depend on eval.c internals, only
   on the public Val layout in yolish.h, to keep the two subsystems
   decoupled)  */

static Val bc_nil(void){
    Val r; r.type=YS_NIL; r.ival=0; r.fval=0; r.bval=0;
    r.sval=""; r.slen=0; r.fn_node=0; r.fn_env=0; r.cap_fd=-1; r.cap_perm=0;
    r.cap_path[0]=0; r.arr_data=0; r.arr_len=0; r.arr_cap=0;
    r.struct_name[0]=0; r.field_vals=0; r.field_names=0; r.field_count=0;
    return r;
}
static Val bc_int(int64_t v){ Val r=bc_nil(); r.type=YS_INT; r.ival=v; return r; }
static Val bc_float(double v){ Val r=bc_nil(); r.type=YS_FLOAT; r.fval=v; return r; }
__attribute__((unused))
static Val bc_bool(int v){ Val r=bc_nil(); r.type=YS_BOOL; r.bval=v; r.ival=v; return r; }
static Val bc_str(const char *s,int len){
    Val r=bc_nil(); r.type=YS_STR;
    char *buf=(char*)malloc((size_t)len+1);
    memcpy(buf,s,(size_t)len); buf[len]=0;
    r.sval=buf; r.slen=len;
    return r;
}

/*  local variable table  */

static int resolve_local(const char *name){
    for(int i=CUR->local_count-1;i>=0;i--)
        if(strcmp(CUR->locals[i].name,name)==0) return i;
    return -1;
}
static int add_local(const char *name){
    if(CUR->local_count>=MAX_LOCALS){
        fprintf(stderr,"[ys vm] too many locals in one function (max %d)\n",MAX_LOCALS);
        CUR->had_error=1;
        return 0;
    }
    int slot=CUR->local_count++;
    snprintf(CUR->locals[slot].name,64,"%s",name);
    return slot;
}

/*  forward decls  */

static void compile_node(Node *n);
static void compile_stmt_list(Node **stmts, int count);
static void compile_tail(Node *n);

/*  builtin name table for OP_BUILTIN  */
/* The VM doesn't reimplement every y.* function — it calls back into
   the *same* call_builtin() used by the AST interpreter (declared in
   eval.c, exposed below) by reconstructing a tiny synthetic arg array.
   This keeps every existing builtin (string/array/fs/json/gc/...)
   working identically under the VM with zero duplicated logic. */
/* call_builtin_public() is declared in yolish.h */

/*  expression / statement compilation  */

static void compile_binop(Node *n){
    compile_node(n->left);
    compile_node(n->right);
    int line=n->line;
    switch(n->op){
        case TK_PLUS:  emit_byte(OP_ADD,line); break;
        case TK_MINUS: emit_byte(OP_SUB,line); break;
        case TK_STAR:  emit_byte(OP_MUL,line); break;
        case TK_SLASH: emit_byte(OP_DIV,line); break;
        case TK_PERCENT: emit_byte(OP_MOD,line); break;
        case TK_EQEQ:  emit_byte(OP_EQ,line); break;
        case TK_NEQ:   emit_byte(OP_NEQ,line); break;
        case TK_LT:    emit_byte(OP_LT,line); break;
        case TK_GT:    emit_byte(OP_GT,line); break;
        case TK_LTE:   emit_byte(OP_LE,line); break;
        case TK_GTE:   emit_byte(OP_GE,line); break;
        case TK_AND:   emit_byte(OP_AND,line); break;
        case TK_OR:    emit_byte(OP_OR,line); break;
        default:
            fprintf(stderr,"[ys vm] unsupported binary operator at line %d\n",line);
            CUR->had_error=1;
    }
}

static void compile_unop(Node *n){
    compile_node(n->left ? n->left : n->right);
    if(n->op==TK_MINUS) emit_byte(OP_NEG,n->line);
    else if(n->op==TK_BANG||n->op==TK_NOT) emit_byte(OP_NOT,n->line);
    else { fprintf(stderr,"[ys vm] unsupported unary operator at line %d\n",n->line); CUR->had_error=1; }
}

/* Builds the qualified dotted name for a builtin call. There are two
   distinct shapes the parser produces — both must be handled:
   1) Simple two-segment calls like y.println(...), gc.collect(...) —
      the parser already combines these into n->name="y.println"
      directly, with n->left==NULL. This is the COMMON case.
   2) Deeper chains like y.math.sqrt(...) — the parser leaves n->name
      as just the last segment ("sqrt") and builds a left-chain of
      ND_DOT nodes for the rest, mirroring exactly what eval.c's
      ND_CALL dot-call branch walks. */
static int try_build_qname(Node *n, char *out, int outsz){
    /* Shape 1: already-combined name from the parser. */
    if(strncmp(n->name,"y.",2)==0 || strncmp(n->name,"process.",8)==0
       || strncmp(n->name,"sys.",4)==0 || strncmp(n->name,"gc.",3)==0
       || strncmp(n->name,"cap.",4)==0
       || n->name[0]=='@'){
        snprintf(out,outsz,"%s",n->name);
        return 1;
    }
    /* Shape 2: a left-chain to walk (e.g. y.math.sqrt, or p.method()
       where p is a struct — the latter isn't a builtin and we return 0
       so the caller falls back / reports unsupported). */
    if(n->left){
        char parts[8][64]; int pc=0;
        Node *chain[8]; int depth=0;
        Node *cur2=n->left;
        while(cur2 && depth<8){
            chain[depth++]=cur2;
            if(cur2->kind==ND_DOT) cur2=cur2->left;
            else break;
        }
        for(int ci=depth-1;ci>=0;ci--){ snprintf(parts[pc],64,"%s",chain[ci]->name); pc++; if(pc>=8) break; }
        if(pc<8) { snprintf(parts[pc],64,"%s",n->name); pc++; }
        out[0]=0; int len=0;
        for(int i=0;i<pc;i++){
            int l=(int)strlen(parts[i]);
            if(len+l+1>=outsz) return 0;
            if(len>0) out[len++]='.';
            memcpy(out+len,parts[i],(size_t)l); len+=l;
        }
        out[len]=0;
        if(strncmp(out,"y.",2)==0 || strncmp(out,"process.",8)==0
           || strncmp(out,"sys.",4)==0 || strncmp(out,"gc.",3)==0
           || strncmp(out,"cap.",4)==0)
            return 1;
        return 0; /* struct method call etc — not a builtin, unsupported in this VM subset */
    }
    /* Shape 3: bare builtin names with no namespace prefix at all —
       mirrors the exact bare-name alternatives eval.c's whitelist
       accepts (e.g. "println" as well as "y.println"). */
    static const char *bare_builtins[]={
        "print","println","input","input_int","input_float","len",
        "push","pop","sort","range","filter","map","reduce","each",
        "exit","assert","assert_eq","assert_neq","assert_true",
        "assert_false","assert_nil",NULL
    };
    for(int i=0;bare_builtins[i];i++)
        if(strcmp(n->name,bare_builtins[i])==0){ snprintf(out,outsz,"%s",n->name); return 1; }
    return 0;
}

static void compile_call(Node *n){
    char qname[256];
    /* v2.0 KEY INSIGHT: when the parser builds a dot-call (n->left is
       non-NULL, e.g. "y.println(...)"), n->args[0] is a DUPLICATE of
       the receiver expression (n->left) — the real, explicit arguments
       start at n->args[1]. This mirrors every builtin in eval.c, which
       does `int s=(argc>1)?1:0;` for exactly this reason. Plain calls
       with no receiver (n->left==NULL, e.g. bare "println(...)" or a
       user function "add(2,3)") have no such offset — args[0] is the
       first real argument. */
    int s = n->left ? 1 : 0;
    int real_argc = n->argc - s;
    if(real_argc<0) real_argc=0;
    if(try_build_qname(n,qname,sizeof(qname))){
        /* builtin / namespaced call: y.println(...), y.string.replace(...), gc.collect(), ... */
        if(strcmp(qname,"y.print")==0 || strcmp(qname,"print")==0){
            for(int i=0;i<real_argc;i++){ compile_node(n->args[s+i]); emit_byte(OP_PRINT,n->line); }
            emit_byte(OP_NIL,n->line); /* print returns nil in the AST interpreter too */
            return;
        }
        if(strcmp(qname,"y.println")==0 || strcmp(qname,"println")==0){
            for(int i=0;i<real_argc;i++){ compile_node(n->args[s+i]); emit_byte(OP_PRINTLN,n->line); }
            if(real_argc==0) emit_byte(OP_PRINTLN,n->line); /* bare println() → blank line */
            emit_byte(OP_NIL,n->line);
            return;
        }
        for(int i=0;i<real_argc;i++) compile_node(n->args[s+i]);
        int nameidx = chunk_add_const(CUR->chunk, bc_str(qname,(int)strlen(qname)));
        emit_byte(OP_BUILTIN,n->line);
        emit_u16(nameidx,n->line);
        emit_byte((unsigned char)real_argc,n->line);
        return;
    }
    /* plain call: a user-defined function value, e.g. add(2,3) or a
       local/global holding a closure. Push the callee, then the args,
       then OP_CALL. No receiver offset here — plain calls (n->left==NULL)
       never have the duplicate-arg0 pattern. */
    Node fake_ident; memset(&fake_ident,0,sizeof(Node));
    fake_ident.kind=ND_IDENT;
    snprintf(fake_ident.name,64,"%s",n->name);
    compile_node(&fake_ident);
    for(int i=0;i<n->argc;i++) compile_node(n->args[i]);
    emit_byte(OP_CALL,n->line);
    emit_byte((unsigned char)n->argc,n->line);
}

static void compile_node(Node *n){
    if(!n) return;
    int line=n->line;
    switch(n->kind){
    case ND_INT:   emit_const(bc_int(n->ival),line); break;
    case ND_FLOAT: emit_const(bc_float(n->fval),line); break;
    case ND_BOOL:  emit_byte(n->ival?OP_TRUE:OP_FALSE,line); break;
    case ND_STR: {
        /* sval[0]=='\x01' marks a raw/multiline string — strip the
           sentinel, same convention as eval.c's ND_STR case. Raw
           strings skip interpolation entirely, matching eval.c. */
        const char *s=n->sval?n->sval:"";
        int is_raw=(s[0]=='\x01');
        if(is_raw) s++;
        if(!is_raw){
            for(int i=0;s[i];i++){
                if(s[i]!='{') continue;
                /* Mirror eval_interp_str's own carve-out: an all-digit
                   {0}, {1}, ... is a y.format positional placeholder,
                   deliberately left untouched by the AST interpreter
                   too (Yolish identifiers can't start with a digit, so
                   it's never a variable reference) — safe to pass
                   through as a plain literal, no fallback needed.
                   Anything else inside {...} (an identifier, an
                   expression, or empty {}) is real interpolation,
                   which needs an Env the VM doesn't have. */
                int j=i+1, all_digits=1, len=0;
                while(s[j] && s[j]!='}'){
                    if(s[j]<'0'||s[j]>'9') all_digits=0;
                    j++; len++;
                }
                if(len==0 || !all_digits){
                    fprintf(stderr,"[ys vm] string interpolation not yet supported at line %d — falling back\n",line);
                    CUR->had_error=1;
                    break;
                }
                i=j; /* skip past this placeholder, keep scanning */
            }
        }
        emit_const(bc_str(s,(int)strlen(s)),line);
        break;
    }
    case ND_IDENT: {
        int slot=CUR->in_function?resolve_local(n->name):-1;
        if(slot>=0){ emit_byte(OP_GET_LOCAL,line); emit_u16(slot,line); }
        else {
            int idx=chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
            emit_byte(OP_GET_GLOBAL,line); emit_u16(idx,line);
        }
        break;
    }
    case ND_BINOP: compile_binop(n); break;
    case ND_UNOP:  compile_unop(n); break;
    case ND_ASSIGN: {
        if(n->left && n->left->kind==ND_IDENT){
            compile_node(n->right);
            int slot=CUR->in_function?resolve_local(n->left->name):-1;
            if(slot>=0){ emit_byte(OP_SET_LOCAL,line); emit_u16(slot,line); }
            else {
                int idx=chunk_add_const(CUR->chunk, bc_str(n->left->name,(int)strlen(n->left->name)));
                emit_byte(OP_SET_GLOBAL,line); emit_u16(idx,line);
            }
        } else if(n->left && n->left->kind==ND_DOT){
            /* p.x = value  (struct field write)
               Stack order for OP_SET_FIELD: push struct, then push value.
               OP_SET_FIELD writes through the shared field_vals GC pointer
               so the change is visible through ALL Val copies of this struct.
               Pushes the value back (as the expression result). */
            compile_node(n->left->left); /* push the struct (gets field_vals ptr) */
            compile_node(n->right);       /* push new value */
            int fidx = chunk_add_const(CUR->chunk,
                           bc_str(n->left->name,(int)strlen(n->left->name)));
            emit_byte(OP_SET_FIELD,line); emit_u16(fidx,line);
        } else if(n->left && n->left->kind==ND_INDEX){
            /* arr[i] = value — OP_INDEX_SET expects array, index, value
               on the stack (bottom to top), so push in that order. */
            compile_node(n->left->left);  /* array */
            compile_node(n->left->right); /* index */
            compile_node(n->right);       /* value */
            emit_byte(OP_INDEX_SET,line);
        } else {
            fprintf(stderr,"[ys vm] unsupported assignment target at line %d\n",line);
            CUR->had_error=1;
        }
        break;
    }
    case ND_LET: case ND_VAR: {
        if(n->right) compile_node(n->right);
        else emit_byte(OP_NIL,line);
        if(CUR->in_function){
            /* v2.0 FIX: the initializer's value is already sitting on
               the stack at exactly the position this local should live
               at — add_local() just names that position. Emitting
               SET_LOCAL+POP here was a bug: OP_SET_LOCAL only *peeks*
               (it never pops), so the explicit POP afterward discarded
               the local's own value, leaving its stack slot to be
               silently overwritten by the very next push (e.g. the next
               let's initializer, or any nested call's arguments). That
               manifested as "the first local works, every local after
               it reads back nil/garbage". Do nothing further here —
               the value stays exactly where it landed. */
            (void)add_local(n->name);
        } else {
            int idx=chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
            emit_byte(OP_DEFINE_GLOBAL,line); emit_u16(idx,line);
        }
        break;
    }
    case ND_BLOCK:
        compile_stmt_list(n->stmts, n->stmtc);
        break;
    case ND_IF: {
        compile_node(n->cond);
        int else_jump=emit_jump(OP_JUMP_IF_FALSE,line);
        emit_byte(OP_POP,line); /* discard the (true) condition */
        compile_node(n->then);
        int end_jump=emit_jump(OP_JUMP,line);
        patch_jump_here(else_jump);
        emit_byte(OP_POP,line); /* discard the (false) condition */
        if(n->els) compile_node(n->els);
        patch_jump_here(end_jump);
        break;
    }
    case ND_WHILE: {
        if(CUR->loop_depth>=MAX_LOOP_DEPTH){
            fprintf(stderr,"[ys vm] loops nested too deeply (max %d) at line %d\n",MAX_LOOP_DEPTH,line);
            CUR->had_error=1;
            break;
        }
        LoopCtx *lc=&CUR->loop_stack[CUR->loop_depth++];
        lc->break_count=0;
        lc->continue_count=0;
        int loop_start=CUR->chunk->count;
        compile_node(n->cond);
        int exit_jump=emit_jump(OP_JUMP_IF_FALSE,line);
        emit_byte(OP_POP,line);
        lc->body_base_locals=CUR->in_function?CUR->local_count:0;
        compile_node(n->body);
        for(int i=0;i<lc->continue_count;i++)
            chunk_patch_jump(CUR->chunk, lc->continue_jumps[i], loop_start);
        /* Release whatever locals this iteration's body declared before
           looping back — otherwise iteration 2's redeclare lands above
           the stale slot instead of overwriting it. */
        emit_pop_locals_to(lc->body_base_locals,line);
        emit_byte(OP_LOOP,line);
        int back_at=CUR->chunk->count;
        emit_u16(0,line);
        chunk_patch_jump(CUR->chunk, back_at, loop_start);
        patch_jump_here(exit_jump);
        emit_byte(OP_POP,line);
        /* break targets land here — past the condition's cleanup POP,
           so the stack depth matches the loop's own natural exit. */
        int break_target=CUR->chunk->count;
        for(int i=0;i<lc->break_count;i++)
            chunk_patch_jump(CUR->chunk, lc->break_jumps[i], break_target);
        CUR->loop_depth--;
        break;
    }
    case ND_BREAK: {
        if(CUR->loop_depth<=0){
            fprintf(stderr,"[ys vm] 'break' used outside of a loop at line %d\n",line);
            CUR->had_error=1;
            break;
        }
        LoopCtx *lc=&CUR->loop_stack[CUR->loop_depth-1];
        if(lc->break_count>=MAX_BREAKS_PER_LOOP){
            fprintf(stderr,"[ys vm] too many break statements in one loop (max %d) at line %d\n",MAX_BREAKS_PER_LOOP,line);
            CUR->had_error=1;
            break;
        }
        /* Pop whatever this iteration has declared so far — break_target
           expects the stack at exactly body_base_locals, same as the
           loop's own natural (condition-false) exit path. */
        emit_pop_locals_to(lc->body_base_locals,line);
        lc->break_jumps[lc->break_count++]=emit_jump(OP_JUMP,line);
        break;
    }
    case ND_CONTINUE: {
        if(CUR->loop_depth<=0){
            fprintf(stderr,"[ys vm] 'continue' used outside of a loop at line %d\n",line);
            CUR->had_error=1;
            break;
        }
        LoopCtx *lc=&CUR->loop_stack[CUR->loop_depth-1];
        if(lc->continue_count>=MAX_BREAKS_PER_LOOP){
            fprintf(stderr,"[ys vm] too many continue statements in one loop (max %d) at line %d\n",MAX_BREAKS_PER_LOOP,line);
            CUR->had_error=1;
            break;
        }
        /* Same cleanup as break — the continue target (condition
           re-check or the for-loop's increment step) expects the stack
           back at body_base_locals, ready for the next iteration. */
        emit_pop_locals_to(lc->body_base_locals,line);
        lc->continue_jumps[lc->continue_count++]=emit_jump(OP_JUMP,line);
        break;
    }
    case ND_FOR: {
        /* for name in <range a..b> { body }   OR   for name in <arr/str expr> { body }
           Desugars to a counted while-loop. The loop variable's stack
           slot/global is reserved exactly once, before the loop starts,
           and updated in place each iteration via emit_store_and_pop —
           mirroring exactly how ND_WHILE-style loops already behave, so
           break/continue (which target this loop's LoopCtx) work
           identically to the while case above. */
        if(CUR->loop_depth>=MAX_LOOP_DEPTH){
            fprintf(stderr,"[ys vm] loops nested too deeply (max %d) at line %d\n",MAX_LOOP_DEPTH,line);
            CUR->had_error=1;
            break;
        }
        int tid=g_temp_counter++;
        char hivar[80], idxvar[80], arrvar[80];
        LoopCtx *lc=&CUR->loop_stack[CUR->loop_depth++];
        lc->break_count=0;
        lc->continue_count=0;
        int loop_base_locals=CUR->in_function?CUR->local_count:0;

        if(n->cond && n->cond->kind==ND_BINOP && n->cond->op==TK_DOTDOT){
            /* range form: for x in lo..hi */
            snprintf(hivar,sizeof(hivar),"$for_hi%d",tid);
            compile_node(n->cond->left);          /* lo */
            emit_declare(n->name,line);           /* loop var := lo, slot reserved */
            compile_node(n->cond->right);         /* hi */
            emit_declare(hivar,line);             /* hidden upper bound */

            int cond_check=CUR->chunk->count;
            emit_load(n->name,line);
            emit_load(hivar,line);
            emit_byte(OP_LT,line);
            int exit_jump=emit_jump(OP_JUMP_IF_FALSE,line);
            emit_byte(OP_POP,line);
            lc->body_base_locals=CUR->in_function?CUR->local_count:0;
            compile_node(n->body);
            /* Release whatever this iteration's body declared before
               falling into the increment step (continue does the same
               cleanup at its own jump site — see ND_CONTINUE). */
            emit_pop_locals_to(lc->body_base_locals,line);

            /* increment step starts here — this is continue's target */
            int increment=CUR->chunk->count;
            for(int i=0;i<lc->continue_count;i++)
                chunk_patch_jump(CUR->chunk, lc->continue_jumps[i], increment);
            emit_load(n->name,line);
            emit_const(bc_int(1),line);
            emit_byte(OP_ADD,line);
            emit_store_and_pop(n->name,line);
            emit_byte(OP_LOOP,line);
            int back_at=CUR->chunk->count; emit_u16(0,line);
            chunk_patch_jump(CUR->chunk, back_at, cond_check);

            patch_jump_here(exit_jump);
            emit_byte(OP_POP,line);
        } else {
            /* array/string form: for x in expr — expr evaluated once,
               matching the AST interpreter, then indexed each iteration.
               "len" is the same shared builtin y.len uses, so length
               semantics (including the "0 for anything non-array/
               non-string" fallback) match the AST interpreter exactly. */
            snprintf(arrvar,sizeof(arrvar),"$for_arr%d",tid);
            snprintf(idxvar,sizeof(idxvar),"$for_idx%d",tid);
            snprintf(hivar,sizeof(hivar),"$for_len%d",tid);

            compile_node(n->cond);
            emit_declare(arrvar,line);

            emit_const(bc_int(0),line);
            emit_declare(idxvar,line);

            emit_load(arrvar,line);
            { int nameidx=chunk_add_const(CUR->chunk, bc_str("len",3));
              emit_byte(OP_BUILTIN,line); emit_u16(nameidx,line); emit_byte(1,line); }
            emit_declare(hivar,line);

            emit_byte(OP_NIL,line);
            emit_declare(n->name,line); /* loop var slot reserved once, filled below */

            int cond_check=CUR->chunk->count;
            emit_load(idxvar,line);
            emit_load(hivar,line);
            emit_byte(OP_LT,line);
            int exit_jump=emit_jump(OP_JUMP_IF_FALSE,line);
            emit_byte(OP_POP,line);

            emit_load(arrvar,line);
            emit_load(idxvar,line);
            emit_byte(OP_INDEX_GET,line);
            emit_store_and_pop(n->name,line);

            lc->body_base_locals=CUR->in_function?CUR->local_count:0;
            compile_node(n->body);
            emit_pop_locals_to(lc->body_base_locals,line);

            /* increment step starts here — this is continue's target */
            int increment=CUR->chunk->count;
            for(int i=0;i<lc->continue_count;i++)
                chunk_patch_jump(CUR->chunk, lc->continue_jumps[i], increment);
            emit_load(idxvar,line);
            emit_const(bc_int(1),line);
            emit_byte(OP_ADD,line);
            emit_store_and_pop(idxvar,line);
            emit_byte(OP_LOOP,line);
            int back_at=CUR->chunk->count; emit_u16(0,line);
            chunk_patch_jump(CUR->chunk, back_at, cond_check);

            patch_jump_here(exit_jump);
            emit_byte(OP_POP,line);
        }

        int break_target=CUR->chunk->count;
        for(int i=0;i<lc->break_count;i++)
            chunk_patch_jump(CUR->chunk, lc->break_jumps[i], break_target);
        /* Release this for-loop's own hidden bookkeeping vars (and loop
           var) now that the whole statement is done — needed so an
           enclosing loop re-executing this statement next iteration
           doesn't stack a second copy on top of the first. */
        emit_pop_locals_to(loop_base_locals,line);
        CUR->loop_depth--;
        break;
    }
    case ND_MATCH: {
        /* match <subject> { pat [if guard] => body, ... }
           Compiles to a chain of pattern tests. The subject is
           evaluated once into a hidden variable so every arm's test
           (and a bound arm's guard/body) can read it as many times as
           needed. A bare-identifier pattern (not "_") always matches
           and binds the subject to that name — for local scope this
           is done by temporarily renaming the hidden variable's own
           slot (zero stack cost, no cleanup needed); for global scope
           it's just another global define. Whichever arm's body runs
           produces the match's value; if no arm matches, the result
           is nil — both mirroring eval.c's ND_MATCH exactly. */
        int tid=g_temp_counter++;
        char subjvar[64]; snprintf(subjvar,sizeof(subjvar),"$match_subj%d",tid);
        int base_locals=CUR->in_function?CUR->local_count:0;

        compile_node(n->cond);
        emit_declare(subjvar,line);
        int subj_slot=CUR->in_function?resolve_local(subjvar):-1;

        int end_jumps[40]; int end_count=0;

        for(int i=0;i<n->argc;i++){
            Node *arm_node=n->arg_data[i];
            if(!arm_node || arm_node->kind!=ND_MATCH_ARM) continue;
            Node *pat=arm_node->left, *guard=arm_node->cond, *body=arm_node->right;
            if(!pat||!body) continue;

            int is_wildcard=(pat->kind==ND_IDENT && pat->name[0]=='_' && pat->name[1]==0);
            int is_binding =(pat->kind==ND_IDENT && !is_wildcard);

            /*  pattern test: leaves matched(bool) on top  */
            if(is_wildcard || is_binding){
                emit_byte(OP_TRUE,line);
            } else if(pat->kind==ND_BINOP && pat->op==TK_DOTDOT){
                emit_load(subjvar,line); compile_node(pat->left);  emit_byte(OP_GE,line);
                emit_load(subjvar,line); compile_node(pat->right); emit_byte(OP_LT,line);
                emit_byte(OP_AND,line);
            } else {
                emit_load(subjvar,line);
                compile_node(pat);
                emit_byte(OP_EQ,line);
            }

            int fail_jump=emit_jump(OP_JUMP_IF_FALSE,line);
            emit_byte(OP_POP,line); /* discard true matched-bool */

            /*  bind (if a named pattern)  */
            if(is_binding){
                if(CUR->in_function && subj_slot>=0)
                    snprintf(CUR->locals[subj_slot].name,64,"%s",pat->name);
                else if(!CUR->in_function){
                    emit_load(subjvar,line);
                    emit_declare(pat->name,line);
                }
            }

            /* guard  */
            int guard_fail_jump=-1;
            if(guard){
                compile_node(guard);
                guard_fail_jump=emit_jump(OP_JUMP_IF_FALSE,line);
                emit_byte(OP_POP,line); /* discard true guard-bool */
            }

            /*  body: produces this arm's result  */
            compile_tail(body);
            emit_collapse_scope(base_locals,line); /* drop subjvar (+ any arm-local lets), keep the result */
            if(is_binding && CUR->in_function && subj_slot>=0)
                snprintf(CUR->locals[subj_slot].name,64,"%s",subjvar); /* restore — later arms still need to resolve subjvar by its real name */
            /* emit_collapse_scope reset CUR->local_count to base_locals —
               correct bytecode for *this* arm's own runtime success path
               (which jumps straight to match_end and never touches later
               arms' code), but it's wrong as *compile-time* bookkeeping
               for the arms still to be compiled below: subjvar itself
               must still count as "in scope" for their pattern tests to
               resolve it as a local. Put it back. */
            if(CUR->in_function) CUR->local_count=base_locals+1;
            if(end_count<40) end_jumps[end_count++]=emit_jump(OP_JUMP,line);

            /*  failure landing zone: guard-fail and pattern-fail both
               need to reach "test next arm", but they must NOT share the
               same POP — each discards a *different* leftover boolean
               (the guard's false result vs. the pattern test's false
               result). Guard-fail does its own POP, then jumps past
               pattern-fail's POP entirely. */
            int guard_fail_skip=-1;
            if(guard_fail_jump>=0){
                patch_jump_here(guard_fail_jump);
                emit_byte(OP_POP,line); /* discard false guard-bool */
                if(is_binding && CUR->in_function && subj_slot>=0)
                    snprintf(CUR->locals[subj_slot].name,64,"%s",subjvar);
                guard_fail_skip=emit_jump(OP_JUMP,line);
            }
            patch_jump_here(fail_jump);
            emit_byte(OP_POP,line); /* discard false matched-bool */
            if(guard_fail_skip>=0) patch_jump_here(guard_fail_skip);
        }

        /* no arm matched */
        emit_byte(OP_NIL,line);
        emit_collapse_scope(base_locals,line);

        int match_end=CUR->chunk->count;
        for(int i=0;i<end_count;i++) chunk_patch_jump(CUR->chunk, end_jumps[i], match_end);
        break;
    }
    case ND_RETURN: {
        if(n->right) compile_node(n->right);
        else emit_byte(OP_NIL,line);
        emit_byte(OP_RETURN,line);
        break;
    }
    case ND_CALL: compile_call(n); break;
    case ND_ARRAY: {
        int cnt = n->stmtc>0 ? n->stmtc : n->argc;
        for(int i=0;i<cnt;i++) compile_node(n->stmtc>0?n->stmts[i]:n->args[i]);
        emit_byte(OP_ARRAY,line); emit_u16(cnt,line);
        break;
    }
    case ND_INDEX:
        compile_node(n->left);
        compile_node(n->right);
        emit_byte(OP_INDEX_GET,line);
        break;
    case ND_FN: {
        /* v2.0: @intent/@audit annotations trigger scheduler/audit log
           lines in the AST interpreter (see eval.c's g_ann_depth logic)
           that this VM subset doesn't replicate yet. Rather than silently
           produce correct *results* with missing log output, fall back
           — the AST interpreter handles annotated functions completely. */
        if(n->type[0]){
            fprintf(stderr,"[ys vm] annotated function '%s' (@%s) not yet supported at line %d\n",
                    n->name, n->type, line);
            CUR->had_error=1;
            break;
        }
        /* Compile the function body into its own Chunk, wrap it as a
           FnProto, store it as a constant, and bind it as a global
           (top-level fn) — matching the AST interpreter's behavior
           where top-level `fn` declarations are visible everywhere. */
        FnProto *proto=(FnProto*)calloc(1,sizeof(FnProto));
        chunk_init(&proto->chunk);
        snprintf(proto->name,64,"%s",n->name);
        proto->arity=n->argc;
        for(int i=0;i<n->argc && i<8;i++) snprintf(proto->param_names[i],64,"%s",n->field_names[i]);

        BCompiler fcomp; memset(&fcomp,0,sizeof(fcomp));
        fcomp.chunk=&proto->chunk; fcomp.in_function=1; fcomp.enclosing=CUR;
        BCompiler *saved=CUR; CUR=&fcomp;
        for(int i=0;i<n->argc && i<8;i++) (void)add_local(proto->param_names[i]);
        /* Implicit return: like eval.c's eval_block-based function call,
           a function with no explicit `return` returns the value of its
           last statement (nil if that statement is void-shaped, or if an
           explicit `return` already fired first — see compile_tail). */
        compile_tail(n->body);
        emit_byte(OP_RETURN,line);
        int err=fcomp.had_error;
        CUR=saved;
        if(err) CUR->had_error=1;

        Val fnval=bc_nil(); fnval.type=YS_FN; fnval.fn_node=(Node*)proto;
        int kidx=chunk_add_const(CUR->chunk, fnval);
        emit_byte(OP_CLOSURE,line); emit_u16(kidx,line);
        int nameidx=chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
        emit_byte(OP_DEFINE_GLOBAL,line); emit_u16(nameidx,line);
        break;
    }


    /*  Structs (v2.0 Phase 2) */

    case ND_STRUCT: {
        /* struct Point { x y } — register the struct name as a global
           string, same as the AST interpreter: env_def(env, name, make_str(name))
           This lets code like  Point { x: 10 }  find the type name in scope. */
        Val namestr = bc_str(n->name,(int)strlen(n->name));
        int stridx = chunk_add_const(CUR->chunk, namestr);
        emit_byte(OP_CONST, line); emit_u16(stridx, line);
        int globidx = chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
        emit_byte(OP_DEFINE_GLOBAL, line); emit_u16(globidx, line);
        break;
    }

    case ND_STRUCT_LIT: {
        /* Point { x: 10  y: 20 }
           Push each field value in declaration order, then OP_STRUCT_NEW.
           Field values are in n->args[0..argc-1], names in n->field_names[]. */
        int fcount = n->argc;
        if(fcount > 8) fcount = 8; /* language limit: 8 fields per struct */
        for(int i = 0; i < fcount; i++) compile_node(n->args[i]);
        /* Emit the struct name + field count */
        int nameidx = chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
        emit_byte(OP_STRUCT_NEW, line);
        emit_u16(nameidx, line);
        emit_byte((unsigned char)fcount, line);
        /* Embed field name constant indices inline in the bytecode */
        for(int i = 0; i < fcount; i++){
            int fidx = chunk_add_const(CUR->chunk,
                           bc_str(n->field_names[i],(int)strlen(n->field_names[i])));
            emit_u16(fidx, line);
        }
        break;
    }

    case ND_DOT: {
        /* p.x — field read. Also handles enum dot access via OP_GET_FIELD
           (the VM's OP_GET_FIELD falls back to nil for non-struct Vals,
           matching the AST's "dot on non-struct: nil" behaviour).
           Method calls p.method() go through compile_call() when the parent
           node is ND_CALL, not here — here n->kind==ND_DOT means it is
           used as a pure r-value (reading a field). */
        compile_node(n->left); /* push the struct */
        int fidx = chunk_add_const(CUR->chunk, bc_str(n->name,(int)strlen(n->name)));
        emit_byte(OP_GET_FIELD, line);
        emit_u16(fidx, line);
        break;
    }

    case ND_IMPL: {
        /* impl Point { fn dist(self) { ... } }
           Phase 2 deferred: fall back to AST interpreter for any program
           that contains impl blocks.  Struct definition + literal + field
           read/write are fully supported above. */
        fprintf(stderr,"[ys vm] impl blocks not yet supported at line %d — falling back\n", line);
        CUR->had_error=1;
        break;
    }

    default:
        fprintf(stderr,"[ys vm] unsupported construct (node kind %d) at line %d — falling back to AST interpreter for this run\n", (int)n->kind, line);
        CUR->had_error=1;
    }
}

/* Compiles `n` as the tail (final, value-producing) position of a
   block — mirroring eval_block()'s semantics, where a block's value is
   whatever its last statement evaluated to. Used for match-arm bodies,
   which are expressions (possibly wrapped in a { block }).
   - A block recurses: every statement but the last compiles normally
     (as compile_stmt_list already does — non-void ones get popped),
     then the last one is itself compiled as a tail.
   - if/else recurses into both branches so `match`-like exhaustive
     if-chains still produce a value; a missing else yields nil.
   - Anything already expression-shaped (calls, binops, literals,
     idents, match, struct/array literals, ...) already leaves a value
     — compile it as-is.
   - The remaining "void-shaped" statement kinds (let/var/while/for/fn/
     struct/impl/break/continue) have side effects but no meaningful
     value in eval.c either; compile the side effect, then push nil. */
static void compile_tail(Node *n){
    if(!n) return;
    int line=n->line;
    if(n->kind==ND_BLOCK){
        int c=n->stmtc;
        if(c==0){ emit_byte(OP_NIL,line); return; }
        if(c>1) compile_stmt_list(n->stmts, c-1);
        compile_tail(n->stmts[c-1]);
        return;
    }
    if(n->kind==ND_IF){
        compile_node(n->cond);
        int else_jump=emit_jump(OP_JUMP_IF_FALSE,line);
        emit_byte(OP_POP,line);
        compile_tail(n->then);
        int end_jump=emit_jump(OP_JUMP,line);
        patch_jump_here(else_jump);
        emit_byte(OP_POP,line);
        if(n->els) compile_tail(n->els);
        else emit_byte(OP_NIL,line);
        patch_jump_here(end_jump);
        return;
    }
    switch(n->kind){
        case ND_LET: case ND_VAR: case ND_WHILE: case ND_FOR:
        case ND_FN: case ND_STRUCT: case ND_IMPL:
        case ND_BREAK: case ND_CONTINUE:
            compile_node(n);
            emit_byte(OP_NIL,line);
            return;
        default:
            compile_node(n); /* already leaves a value */
            return;
    }
}

static void compile_stmt_list(Node **stmts, int count){
    for(int i=0;i<count;i++){
        compile_node(stmts[i]);
        /* Most statement forms leave nothing on the stack (ND_LET/VAR,
           ND_IF, ND_WHILE, ND_RETURN, ND_FN). Expression-statements
           (a bare call, e.g. `foo(1,2)` on its own line) DO leave a
           value — pop it so the stack doesn't grow unbounded across a
           block. We detect this generically: anything that isn't one
           of the "void-shaped" statement kinds gets an explicit pop. */
        switch(stmts[i]->kind){
            case ND_LET: case ND_VAR: case ND_IF: case ND_WHILE: case ND_FOR:
            case ND_RETURN: case ND_FN: case ND_BLOCK:
            case ND_STRUCT: case ND_IMPL:
            case ND_BREAK: case ND_CONTINUE: /* void-shaped — no value left on stack */
                break;
            default:
                emit_byte(OP_POP, stmts[i]->line);
        }
    }
}

int bcompile_program(Node *prog, Chunk *out_chunk){
    chunk_init(out_chunk);
    g_temp_counter=0;
    BCompiler top; memset(&top,0,sizeof(top));
    top.chunk=out_chunk; top.in_function=0; top.enclosing=NULL;
    CUR=&top;
    compile_stmt_list(prog->stmts, prog->stmtc);
    emit_byte(OP_CALL_MAIN_IF_EXISTS, 0);
    emit_byte(OP_HALT, 0);
    int ok = !top.had_error;
    CUR=NULL;
    return ok;
}