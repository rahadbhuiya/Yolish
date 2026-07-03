/* 
   bcompiler.c  —  v2.0 (Bytecode VM)
   Walks the existing AST (Node*, produced by parser.c — completely
   unchanged) and emits bytecode into a Chunk. This is a *subset*
   compiler for v2.0: it covers the core language (literals, operators,
   variables, if/while, functions, arrays, builtins) so the VM can be
   benchmarked and validated end-to-end. Structs, closures-with-capture,
   match, try/catch, enums, and modules still fall back to the AST
   interpreter (eval.c) — `ys vm` reports which construct it hit and
   exits cleanly rather than miscompiling.
*/

#include "bcompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*  compiler state  */

#define MAX_LOCALS 256

typedef struct {
    char name[64];
} LocalSlot;

typedef struct BCompiler {
    Chunk            *chunk;
    LocalSlot         locals[MAX_LOCALS];
    int               local_count;
    int               in_function;   /* 0 = top-level globals, 1 = function body locals */
    struct BCompiler *enclosing;
    int               had_error;
} BCompiler;

static BCompiler *CUR = NULL; /* current compiler — simplifies the many
                                   small emit helpers below; restored via
                                   save/restore around nested fn compiles */

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
           sentinel, same convention as eval.c's ND_STR case. No
           interpolation support yet in the v2.0 VM subset. */
        const char *s=n->sval?n->sval:"";
        if(s[0]=='\x01') s++;
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
        int loop_start=CUR->chunk->count;
        compile_node(n->cond);
        int exit_jump=emit_jump(OP_JUMP_IF_FALSE,line);
        emit_byte(OP_POP,line);
        compile_node(n->body);
        emit_byte(OP_LOOP,line);
        int back_at=CUR->chunk->count;
        emit_u16(0,line);
        chunk_patch_jump(CUR->chunk, back_at, loop_start);
        patch_jump_here(exit_jump);
        emit_byte(OP_POP,line);
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
        compile_node(n->body);
        emit_byte(OP_NIL,line); emit_byte(OP_RETURN,line); /* implicit return nil if body falls through */
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
            case ND_LET: case ND_VAR: case ND_IF: case ND_WHILE:
            case ND_RETURN: case ND_FN: case ND_BLOCK:
            case ND_STRUCT: case ND_IMPL: /* void-shaped — no value left on stack */
                break;
            default:
                emit_byte(OP_POP, stmts[i]->line);
        }
    }
}

int bcompile_program(Node *prog, Chunk *out_chunk){
    chunk_init(out_chunk);
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