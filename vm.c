/* 
   vm.c  —  v2.0 (Bytecode VM)
   The actual stack-machine interpreter for Chunks produced by
   bcompiler.c. Reuses Val, the GC, and every existing builtin from
   eval.c (via call_builtin_public) — this file is purely about
   executing opcodes fast, not about re-deriving language semantics.
*/

#include "vm.h"
#include "bcompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define STACK_MAX   4096
#define FRAMES_MAX  256
#define MAX_GLOBALS 1024

typedef struct {
    Chunk *chunk;
    int    ip;
    Val   *slots; /* base of this frame's locals within vm_stack */
} CallFrame;

static Val       vm_stack[STACK_MAX];
static int       vm_sp;
static CallFrame vm_frames[FRAMES_MAX];
static int       vm_frame_count;

#define MAX_TRY_DEPTH 32
typedef struct {
    int    frame_count;    /* vm_frame_count to unwind back to (the frame that owns this try) */
    int    stack_sp;       /* vm_sp to restore to (the try-block's own local-scope baseline) */
    Chunk *catch_chunk;    /* == the owning frame's chunk (try/catch is always within one function) */
    int    catch_ip;       /* absolute offset to jump to */
    int    has_catch_var;
    int    catch_var_slot; /* local slot (relative to the owning frame) to bind the caught value into */
} TryHandler;
static TryHandler try_handlers[MAX_TRY_DEPTH];
static int        try_depth;

static char global_names[MAX_GLOBALS][64];
static Val  global_vals[MAX_GLOBALS];
static int  global_count;

static int  vm_had_runtime_error;

/* v2.6: converts a thrown value into what the catch variable actually
   gets bound to — mirrors eval.c's ND_THROW exactly: a struct passes
   through unchanged, everything else becomes its string representation
   (ints formatted as decimal, strings/errors passed through as-is,
   anything else becomes the literal "err"). */
static Val vm_thrown_to_catch_val(Val thrown){
    if(thrown.type==YS_STRUCT) return thrown;
    char buf[512];
    if(thrown.type==YS_STR||thrown.type==YS_ERR){
        snprintf(buf,sizeof(buf),"%s",thrown.sval?thrown.sval:"");
    } else if(thrown.type==YS_INT){
        snprintf(buf,sizeof(buf),"%lld",(long long)thrown.ival);
    } else {
        snprintf(buf,sizeof(buf),"err");
    }
    Val r=make_nil();
    r.type=YS_STR;
    int len=(int)strlen(buf);
    char *heap=(char*)malloc((size_t)len+1);
    memcpy(heap,buf,(size_t)len+1);
    r.sval=heap; r.slen=len;
    return r;
}

/*  stack primitives  */

static void vm_push(Val v){
    if(vm_sp>=STACK_MAX){
        fprintf(stderr,"[ys vm] stack overflow\n");
        vm_had_runtime_error=1;
        return;
    }
    vm_stack[vm_sp++]=v;
}
static Val vm_pop(void){
    if(vm_sp<=0){ fprintf(stderr,"[ys vm] stack underflow\n"); vm_had_runtime_error=1; return make_nil(); }
    return vm_stack[--vm_sp];
}
static Val vm_peek(int back){ return vm_stack[vm_sp-1-back]; }

/*  globals  */

static int global_find(const char *name){
    for(int i=0;i<global_count;i++)
        if(strcmp(global_names[i],name)==0) return i;
    return -1;
}
static void global_define(const char *name, Val v){
    int idx=global_find(name);
    if(idx>=0){ global_vals[idx]=v; return; }
    if(global_count>=MAX_GLOBALS){ fprintf(stderr,"[ys vm] too many globals\n"); vm_had_runtime_error=1; return; }
    snprintf(global_names[global_count],64,"%s",name);
    global_vals[global_count]=v;
    global_count++;
}

/* v2.6: lets eval.c's env_get() fall back to the VM's own global table
   when a tree-walking closure (see call_closure_public) references a
   name its own Env chain doesn't have — e.g. a top-level `fn`/`let`
   compiled by the VM, which lives here, not in an Env. */
Val *vm_global_lookup_public(const char *name){
    int g=global_find(name);
    return g>=0 ? &global_vals[g] : NULL;
}

/*  helpers shared with the AST interpreter's semantics  */

static int is_truthy(Val v){ return val_bool(v); }

static int vals_equal(Val a, Val b){
    if(a.type!=b.type){
        /* allow int/float cross-comparison, matching eval.c's ND_BINOP */
        if((a.type==YS_INT&&b.type==YS_FLOAT)||(a.type==YS_FLOAT&&b.type==YS_INT))
            return val_float(a)==val_float(b);
        return 0;
    }
    switch(a.type){
        case YS_INT:   return a.ival==b.ival;
        case YS_FLOAT: return a.fval==b.fval;
        case YS_BOOL:  return a.bval==b.bval;
        case YS_STR:   return strcmp(a.sval?a.sval:"", b.sval?b.sval:"")==0;
        case YS_NIL:   return 1;
        default:       return 0; /* arrays/structs: reference semantics, not deep-eq here */
    }
}

/*  binary arithmetic (mirrors eval.c's TK_PLUS/MINUS/... handling,
   including string concatenation on '+')  */

static void vm_binop(OpCode op){
    Val b=vm_pop(); Val a=vm_pop();

    if(op==OP_ADD && a.type==YS_STR){
        /* string concat — exact-size allocation, same approach as eval.c
           (no in-place growth into a fixed buffer) */
        int alen=a.slen, blen=b.slen;
        char *buf=(char*)malloc((size_t)alen+(size_t)blen+1);
        memcpy(buf,a.sval,(size_t)alen);
        memcpy(buf+alen,b.sval?b.sval:"",(size_t)blen);
        buf[alen+blen]=0;
        Val r=make_str(buf);
        free(buf); /* make_str copies into its own GC-tracked buffer */
        vm_push(r);
        return;
    }

    int use_float = (a.type==YS_FLOAT||b.type==YS_FLOAT);

    switch(op){
        case OP_ADD: vm_push(use_float?make_float(val_float(a)+val_float(b)):make_int(val_int(a)+val_int(b))); return;
        case OP_SUB: vm_push(use_float?make_float(val_float(a)-val_float(b)):make_int(val_int(a)-val_int(b))); return;
        case OP_MUL: vm_push(use_float?make_float(val_float(a)*val_float(b)):make_int(val_int(a)*val_int(b))); return;
        case OP_DIV:
            if(use_float) vm_push(make_float(val_float(b)!=0.0?val_float(a)/val_float(b):0.0));
            else          vm_push(make_int(val_int(b)!=0?val_int(a)/val_int(b):0));
            return;
        case OP_MOD:
            vm_push(make_int(val_int(b)!=0?val_int(a)%val_int(b):0));
            return;
        case OP_EQ:  vm_push(make_bool(vals_equal(a,b))); return;
        case OP_NEQ: vm_push(make_bool(!vals_equal(a,b))); return;
        case OP_LT:
            if(a.type==YS_STR&&b.type==YS_STR) vm_push(make_bool(strcmp_u(a.sval,b.sval)<0));
            else vm_push(make_bool(use_float?val_float(a)<val_float(b):val_int(a)<val_int(b)));
            return;
        case OP_GT:
            if(a.type==YS_STR&&b.type==YS_STR) vm_push(make_bool(strcmp_u(a.sval,b.sval)>0));
            else vm_push(make_bool(use_float?val_float(a)>val_float(b):val_int(a)>val_int(b)));
            return;
        case OP_LE:
            if(a.type==YS_STR&&b.type==YS_STR) vm_push(make_bool(strcmp_u(a.sval,b.sval)<=0));
            else vm_push(make_bool(use_float?val_float(a)<=val_float(b):val_int(a)<=val_int(b)));
            return;
        case OP_GE:
            if(a.type==YS_STR&&b.type==YS_STR) vm_push(make_bool(strcmp_u(a.sval,b.sval)>=0));
            else vm_push(make_bool(use_float?val_float(a)>=val_float(b):val_int(a)>=val_int(b)));
            return;
        case OP_AND: vm_push(make_bool(is_truthy(a)&&is_truthy(b))); return;
        case OP_OR:  vm_push(make_bool(is_truthy(a)||is_truthy(b))); return;
        case OP_BAND: vm_push(make_int(val_int(a) &  val_int(b))); return;
        case OP_BOR:  vm_push(make_int(val_int(a) |  val_int(b))); return;
        case OP_BXOR: vm_push(make_int(val_int(a) ^  val_int(b))); return;
        case OP_SHL:  vm_push(make_int(val_int(a) << (val_int(b)&63))); return;
        case OP_SHR:  vm_push(make_int(val_int(a) >> (val_int(b)&63))); return;
        default: fprintf(stderr,"[ys vm] bad binop opcode %d\n",(int)op); vm_had_runtime_error=1;
    }
}

/*  the dispatch loop  */

static int read_u16(CallFrame *f){
    int hi=f->chunk->code[f->ip++];
    int lo=f->chunk->code[f->ip++];
    return (hi<<8)|lo;
}
static int read_i16(CallFrame *f){
    int v=read_u16(f);
    if(v & 0x8000) v -= 0x10000; /* sign-extend */
    return v;
}

static VMResult run(void){
    CallFrame *frame=&vm_frames[vm_frame_count-1];
    for(;;){
        if(vm_had_runtime_error) return VM_RUNTIME_ERROR;
        unsigned char op=frame->chunk->code[frame->ip++];
        switch((OpCode)op){
            case OP_CONST: { int idx=read_u16(frame); vm_push(frame->chunk->constants[idx]); break; }
            case OP_NIL:   vm_push(make_nil()); break;
            case OP_TRUE:  vm_push(make_bool(1)); break;
            case OP_FALSE: vm_push(make_bool(0)); break;
            case OP_POP:   vm_pop(); break;
            case OP_DUP:   vm_push(vm_peek(0)); break;

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
            case OP_AND: case OP_OR:
            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
                vm_binop((OpCode)op); break;

            case OP_NEG: {
                Val a=vm_pop();
                vm_push(a.type==YS_FLOAT?make_float(-a.fval):make_int(-a.ival));
                break;
            }
            case OP_NOT: { Val a=vm_pop(); vm_push(make_bool(!is_truthy(a))); break; }
            case OP_BNOT: { Val a=vm_pop(); vm_push(make_int(~val_int(a))); break; }

            case OP_GET_LOCAL: { int slot=read_u16(frame); vm_push(frame->slots[slot]); break; }
            case OP_SET_LOCAL: { int slot=read_u16(frame); frame->slots[slot]=vm_peek(0); break; }
            case OP_GET_GLOBAL: {
                int idx=read_u16(frame);
                const char *name=frame->chunk->constants[idx].sval;
                int g=global_find(name);
                if(g<0){
                    /* v2.0: the AST interpreter prints a file:line:col
                       "undefined 'x' — did you mean 'y'?" diagnostic
                       with typo suggestion (see eval.c's lev_dist). This
                       VM subset doesn't carry source position info into
                       runtime yet, so it prints a plain message instead
                       — still better than silently returning nil with
                       no explanation. */
                    fprintf(stderr,"[ys vm] undefined '%s'\n",name);
                }
                vm_push(g>=0?global_vals[g]:make_nil());
                break;
            }
            case OP_SET_GLOBAL: {
                int idx=read_u16(frame);
                const char *name=frame->chunk->constants[idx].sval;
                global_define(name, vm_peek(0));
                break;
            }
            case OP_DEFINE_GLOBAL: {
                int idx=read_u16(frame);
                const char *name=frame->chunk->constants[idx].sval;
                global_define(name, vm_pop());
                break;
            }
            case OP_JUMP: { int off=read_i16(frame); frame->ip += off; break; }
            case OP_JUMP_IF_FALSE: {
                int off=read_i16(frame);
                if(!is_truthy(vm_peek(0))) frame->ip += off;
                break;
            }
            case OP_LOOP: { int off=read_i16(frame); frame->ip += off; break; }

            case OP_CALL: {
                int argc=frame->chunk->code[frame->ip++];
                Val callee=vm_peek(argc);
                if(callee.type!=YS_FN || !callee.fn_node){
                    fprintf(stderr,"[ys vm] attempt to call a non-function value\n");
                    vm_had_runtime_error=1; break;
                }
                if(callee.fn_env){
                    /* Tree-walking closure (built by OP_MAKE_CLOSURE):
                       fn_node is a real AST Node*, fn_env a real Env*
                       with captures already bound. Dispatch through the
                       same call path the AST interpreter itself uses,
                       so this closure behaves identically whether
                       called directly from VM-compiled code or handed
                       to a builtin like y.map that invokes it itself. */
                    Node *fd=(Node*)callee.fn_node;
                    Env *ce=(Env*)callee.fn_env;
                    Val args[16];
                    int na=argc>16?16:argc;
                    for(int i=0;i<na;i++) args[i]=vm_peek(argc-1-i);
                    Val result=call_closure_public(fd, ce, args, na);
                    vm_sp -= (argc+1); /* drop args + callee, same as the FnProto path's OP_RETURN */
                    vm_push(result);
                    break;
                }
                FnProto *proto=(FnProto*)callee.fn_node;
                if(argc!=proto->arity){
                    fprintf(stderr,"[ys vm] '%s' expects %d arg(s), got %d\n",proto->name,proto->arity,argc);
                    vm_had_runtime_error=1; break;
                }
                if(vm_frame_count>=FRAMES_MAX){
                    fprintf(stderr,"[ys vm] call stack overflow\n");
                    vm_had_runtime_error=1; break;
                }
                CallFrame *nf=&vm_frames[vm_frame_count++];
                nf->chunk=&proto->chunk;
                nf->ip=0;
                nf->slots=&vm_stack[vm_sp-argc]; /* args are already in place as locals 0..argc-1 */
                frame=nf;
                break;
            }
            case OP_RETURN: {
                Val result=vm_pop();
                vm_frame_count--;
                /* drop the callee's frame: locals + the function value
                   itself that OP_CALL peeked under the args */
                vm_sp = (int)(frame->slots - vm_stack) - 1;
                if(vm_frame_count==0) return VM_OK; /* top-level halt via return (shouldn't normally happen) */
                frame=&vm_frames[vm_frame_count-1];
                vm_push(result);
                break;
            }
            case OP_CLOSURE: { int idx=read_u16(frame); vm_push(frame->chunk->constants[idx]); break; }
            case OP_MAKE_CLOSURE: {
                int proto_idx=read_u16(frame);
                int ncap=frame->chunk->code[frame->ip++];
                int name_idx[32];
                for(int i=0;i<ncap && i<32;i++) name_idx[i]=read_u16(frame);
                Env *ce=env_new(NULL);
                for(int i=ncap-1;i>=0;i--){
                    Val v=vm_pop();
                    if(i<32) env_def(ce, frame->chunk->constants[name_idx[i]].sval, v);
                }
                Val closure=frame->chunk->constants[proto_idx]; /* fn_node = literal's AST Node* */
                closure.fn_env=(void*)ce;
                vm_push(closure);
                break;
            }

            case OP_TRY_BEGIN: {
                int has_var=frame->chunk->code[frame->ip++];
                int var_slot=read_u16(frame);
                int off=read_i16(frame);
                if(try_depth>=MAX_TRY_DEPTH){
                    fprintf(stderr,"[ys vm] try/catch nested too deeply (max %d)\n",MAX_TRY_DEPTH);
                    vm_had_runtime_error=1; break;
                }
                TryHandler *th=&try_handlers[try_depth++];
                th->frame_count=vm_frame_count;
                th->stack_sp=vm_sp;
                th->catch_chunk=frame->chunk;
                th->catch_ip=frame->ip+off; /* frame->ip here == right after the offset field, matching chunk_patch_jump's own convention */
                th->has_catch_var=has_var;
                th->catch_var_slot=var_slot;
                break;
            }
            case OP_TRY_END: {
                if(try_depth>0) try_depth--;
                break;
            }
            case OP_THROW: {
                Val thrown=vm_pop();
                if(try_depth<=0){
                    /* Uncaught — matches the AST interpreter's own
                       behavior exactly: g_throwing propagates all the
                       way up with nothing left to catch it, and the
                       program just silently stops right here. */
                    return VM_OK;
                }
                TryHandler th=try_handlers[--try_depth];
                vm_frame_count=th.frame_count;
                frame=&vm_frames[vm_frame_count-1];
                frame->chunk=th.catch_chunk;
                frame->ip=th.catch_ip;
                vm_sp=th.stack_sp;
                if(th.has_catch_var){
                    frame->slots[th.catch_var_slot]=vm_thrown_to_catch_val(thrown);
                    vm_sp=th.stack_sp+1;
                }
                break;
            }

            case OP_CALL_METHOD: {
                int nameidx=read_u16(frame);
                int argc=frame->chunk->code[frame->ip++];
                const char *mname=frame->chunk->constants[nameidx].sval;
                Val args[16];
                int na=argc>16?16:argc;
                for(int i=na-1;i>=0;i--) args[i]=vm_pop();
                Val recv=vm_pop();
                Val result=call_method_public(recv,mname,args,na);
                vm_push(result);
                break;
            }

            case OP_LOAD_MODULE: {
                int pathidx=read_u16(frame);
                int nameidx=read_u16(frame);
                const char *path=frame->chunk->constants[pathidx].sval;
                const char *nsname=frame->chunk->constants[nameidx].sval;
                Val mod=eval_module_public(path,nsname);
                vm_push(mod);
                break;
            }

            case OP_ARRAY: {
                int cnt=read_u16(frame);
                Val arr=make_nil(); arr.type=YS_ARR;
                arr.arr_data=alloc_arr(cnt>0?cnt:1);
                arr.arr_len=cnt; arr.arr_cap=cnt;
                for(int i=cnt-1;i>=0;i--) arr.arr_data[i]=vm_pop();
                vm_push(arr);
                break;
            }
            case OP_INDEX_GET: {
                Val idxv=vm_pop(); Val base=vm_pop();
                int idx=(int)val_int(idxv);
                if(base.type==YS_ARR){
                    if(idx<0||idx>=base.arr_len) vm_push(make_nil());
                    else vm_push(base.arr_data[idx]);
                } else if(base.type==YS_STR){
                    if(idx<0||idx>=base.slen) vm_push(make_nil());
                    else { char buf[2]={base.sval[idx],0}; vm_push(make_str(buf)); }
                } else vm_push(make_nil());
                break;
            }
            case OP_INDEX_SET: {
                Val val=vm_pop(); Val idxv=vm_pop(); Val base=vm_pop();
                int idx=(int)val_int(idxv);
                if(base.type==YS_ARR && idx>=0 && idx<base.arr_len) base.arr_data[idx]=val;
                vm_push(val);
                break;
            }

            /*  struct opcodes (v2.0 Phase 2)  */
            case OP_STRUCT_NEW: {
                int nameidx = read_u16(frame);
                int fcount  = frame->chunk->code[frame->ip++];
                if(fcount > 8) fcount = 8;
                /* Read field name constant indices from the bytecode stream */
                int fidxs[8];
                for(int i=0; i<fcount; i++) fidxs[i] = read_u16(frame);
                /* Build the struct Val */
                Val v = make_nil(); v.type = YS_STRUCT;
                const char *sname = frame->chunk->constants[nameidx].sval;
                strncpy(v.struct_name, sname?sname:"", 31);
                v.field_count = fcount;
                v.field_vals  = alloc_arr(fcount > 0 ? fcount : 1);
                /* Allocate field names with malloc — small, bounded cost.
                   (Not GC-tracked yet; future work to add gc_mark_str support.) */
                char (*fnames)[32] = (char(*)[32])malloc((size_t)(fcount>0?fcount:1) * 32);
                v.field_names = fnames;
                /* Pop field values: they were pushed field[0]..field[N-1] so
                   field[N-1] is on top. Fill in reverse. */
                for(int i = fcount-1; i >= 0; i--){
                    v.field_vals[i] = vm_pop();
                    const char *fn = frame->chunk->constants[fidxs[i]].sval;
                    snprintf(fnames[i], 32, "%s", fn ? fn : "");
                }
                vm_push(v);
                break;
            }

            case OP_GET_FIELD: {
                int nameidx = read_u16(frame);
                const char *fname = frame->chunk->constants[nameidx].sval;
                Val obj = vm_pop();
                Val result = make_nil();
                if(obj.type == YS_STRUCT && obj.field_vals && obj.field_names && fname){
                    for(int i=0; i<obj.field_count; i++){
                        if(strcmp_u(obj.field_names[i], fname)==0){
                            result = obj.field_vals[i];
                            break;
                        }
                    }
                } else if(obj.type==YS_INT && obj.sval && fname
                          && obj.sval[0]=='e' && obj.sval[1]=='n' && obj.sval[2]=='u'
                          && obj.sval[3]=='m' && obj.sval[4]==':'){
                    /* v2.6: EnumName.Variant — obj is the sentinel value
                       registered for "EnumName" (see bcompiler.c's
                       ND_ENUM), sval="enum:EnumName". Reconstruct the
                       qualified name and look it up, exactly like
                       eval.c's own ND_DOT enum handling. Only checks the
                       VM's global table, matching how enums are almost
                       always declared at top level (see the note in
                       ND_ENUM) — same scope limitation as match/try's
                       hidden state for function-local declarations. */
                    char qname[128];
                    snprintf(qname,sizeof(qname),"%.60s.%.60s",&obj.sval[5],fname);
                    Val *found=vm_global_lookup_public(qname);
                    if(found) result=*found;
                }
                vm_push(result);
                break;
            }

            case OP_SET_FIELD: {
                /* Stack on entry: [..., struct, value]  (value on top) */
                int nameidx = read_u16(frame);
                const char *fname = frame->chunk->constants[nameidx].sval;
                Val newval = vm_pop();
                Val obj    = vm_pop();
                if(obj.type == YS_STRUCT && obj.field_vals && obj.field_names && fname){
                    for(int i=0; i<obj.field_count; i++){
                        if(strcmp_u(obj.field_names[i], fname)==0){
                            obj.field_vals[i] = newval; /* mutates through GC ptr */
                            break;
                        }
                    }
                }
                vm_push(newval);
                break;
            }

            case OP_BUILTIN: {
                int nameidx=read_u16(frame);
                int argc=frame->chunk->code[frame->ip++];
                const char *name=frame->chunk->constants[nameidx].sval;
                Val argv[16];
                int n=argc>16?16:argc;
                for(int i=n-1;i>=0;i--) argv[i]=vm_pop();
                Val result=call_builtin_public(name, argv, n);
                vm_push(result);
                break;
            }
            case OP_PRINT:   { Val v=vm_pop(); ys_print_val(v); break; }
            case OP_PRINTLN: { Val v=vm_pop(); ys_print_val(v); printf("\n"); break; }

            case OP_CALL_MAIN_IF_EXISTS: {
                int g=global_find("main");
                if(g>=0 && global_vals[g].type==YS_FN && global_vals[g].fn_node){
                    FnProto *proto=(FnProto*)global_vals[g].fn_node;
                    if(proto->arity==0){
                        if(vm_frame_count>=FRAMES_MAX){
                            fprintf(stderr,"[ys vm] call stack overflow\n");
                            vm_had_runtime_error=1; break;
                        }
                        /* Push the callee itself first, matching OP_CALL's
                           stack shape, so OP_RETURN's "(slots - stack) - 1"
                           rewind math stays correct uniformly. */
                        vm_push(global_vals[g]);
                        CallFrame *nf=&vm_frames[vm_frame_count++];
                        nf->chunk=&proto->chunk;
                        nf->ip=0;
                        nf->slots=&vm_stack[vm_sp]; /* zero args: slots start right after the callee */
                        frame=nf;
                    }
                }
                break;
            }

            case OP_HALT: return vm_had_runtime_error?VM_RUNTIME_ERROR:VM_OK;

            default:
                fprintf(stderr,"[ys vm] unknown opcode %d\n",(int)op);
                return VM_RUNTIME_ERROR;
        }
    }
}

VMResult vm_interpret(Node *prog){
    Chunk top;
    if(!bcompile_program(prog,&top)) return VM_COMPILE_ERROR;

    vm_sp=0; vm_frame_count=0; global_count=0; vm_had_runtime_error=0; try_depth=0;
    vm_frames[vm_frame_count].chunk=&top;
    vm_frames[vm_frame_count].ip=0;
    vm_frames[vm_frame_count].slots=&vm_stack[0];
    vm_frame_count++;

    return run();
}