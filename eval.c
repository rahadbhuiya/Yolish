#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "yolish.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifndef _WIN32
#  include <sys/stat.h>
#  include <dirent.h>
#  include <unistd.h>
#else
#  include <direct.h>
#  include <windows.h>
#  include <io.h>
#endif

/*  Memory pools  */
static Env  envpool[512]; static int envidx=0; /* reduced from 4096 to keep BSS<2GB */
/* No static arrpool — fully dynamic */
static char nmpool[512][32]; static int nmidx=0;


/* forward declaration — defined later in this file */
static Val g_return_val_fwd;

/* 
   v1.5  —  Mark-and-Sweep Garbage Collector
   ================================================================
   All arr_data and field_vals are allocated through gc_alloc().
   A GCNode header sits immediately before each allocation.
   gc_collect() marks from all envpool roots then sweeps unreachable
   nodes. gc_maybe() triggers collection at statement boundaries.
*/

#define GC_MAGIC  0xBC1D5A4Eu  /* sentinel to detect managed pointers  */

typedef struct GCNode {
    unsigned int  magic;       /* always GC_MAGIC                       */
    int           marked;      /* mark bit — set during mark phase      */
    int           birth;       /* gc_cycle when allocated               */
    int           size;        /* number of Val elements                */
    struct GCNode *next;       /* intrusive linked list                 */
    /* Val data[size] follows immediately                               */
} GCNode;

static GCNode *gc_head        = NULL;  /* head of all managed allocations */
static int    gc_alloc_count  = 0;     /* allocations since last collect  */
static int    gc_current_cycle= 0;     /* incremented each collection     */
static int    gc_threshold    = 128;   /* collect after N new allocs      */
static long   gc_total_freed  = 0;     /* lifetime freed count (stats)    */
static long   gc_total_alloc  = 0;     /* lifetime alloc count (stats)    */

/* Allocate a GC-tracked block of n Val elements */
static Val *gc_alloc(int n){
    if(n<=0) n=1;
    GCNode *node=(GCNode*)calloc(1, sizeof(GCNode) + (size_t)n*sizeof(Val));
    if(!node){
        /* fallback: untracked — won't be GC'd but won't crash */
        return (Val*)calloc(n, sizeof(Val));
    }
    node->magic  = GC_MAGIC;
    node->marked = 0;
    node->birth  = gc_current_cycle;
    node->size   = n;
    node->next   = gc_head;
    gc_head      = node;
    gc_alloc_count++;
    gc_total_alloc++;
    return (Val*)(node + 1);   /* data starts right after the header */
}

/*  Mark phase  */

static void gc_mark_val(Val *v);

/* Mark a GC-managed Val* block (arr_data or field_vals) */
static void gc_mark_ptr(Val *data){
    if(!data) return;
    GCNode *node = ((GCNode*)data) - 1;
    if(node->magic != GC_MAGIC) return;  /* not GC-managed — skip safely */
    if(node->marked) return;             /* already visited this cycle */
    node->marked = 1;
    /* recursively mark all Vals inside this block */
    for(int i=0; i<node->size; i++) gc_mark_val(&data[i]);
}

/* Mark a single Val and everything it references */
static void gc_mark_val(Val *v){
    if(!v) return;
    if(v->arr_data)   gc_mark_ptr(v->arr_data);
    if(v->field_vals) gc_mark_ptr(v->field_vals);
    /* closure environment — scan its local bindings */
    if(v->type == YS_FN && v->fn_env){
        Env *fe = (Env*)v->fn_env;
        for(int i=0; i<fe->count; i++) gc_mark_val(&fe->vals[i]);
    }
}

/* Mark from all known roots (envpool + g_return_val) */
static void gc_mark_roots(void){
    /* scan every Env slot that has been used */
    for(int i=0; i<envidx; i++){
        for(int j=0; j<envpool[i].count; j++){
            gc_mark_val(&envpool[i].vals[j]);
        }
    }
    /* mark the pending return value */
    { extern Val g_return_val_fwd; (void)g_return_val_fwd; } /* suppress unused */
    /* g_return_val is a static local declared later — skip here; envpool covers it */
}

/*  Sweep phase  */

static void gc_sweep(void){
    GCNode **node = &gc_head;
    while(*node){
        GCNode *cur = *node;
        if(!cur->marked && cur->birth < gc_current_cycle){
            /* unreachable AND not freshly allocated — free it */
            *node = cur->next;
            free(cur);
            gc_total_freed++;
        } else {
            cur->marked = 0;       /* reset mark for next cycle */
            node = &cur->next;
        }
    }
    gc_current_cycle++;            /* advance cycle counter */
    gc_alloc_count = 0;
    /* Adaptive threshold: grow if we freed a lot */
    if(gc_total_freed > gc_threshold * 4) gc_threshold = gc_threshold * 2;
    if(gc_threshold > 4096) gc_threshold = 4096;
}

/*  Public API  */

/* Full collection: mark from roots + sweep */
static void gc_collect(void){
    gc_mark_roots();
    gc_sweep();
}

/* Call at statement boundaries — only collects when threshold is hit */
static void gc_maybe(void){
    if(gc_alloc_count >= gc_threshold) gc_collect();
}

/* v1.5: alloc_arr and alloc_fld now route through GC allocator */
static Val *alloc_arr(int n){ return gc_alloc(n); }
static Val *alloc_fld(int n){ return gc_alloc(n); }
static char (*alloc_nm(int n))[32]{
    if(nmidx+n>=512) return nmpool;
    char (*p)[32]=&nmpool[nmidx]; nmidx+=n; return p;
}

/*  Environment  */
Env *env_new(Env *parent){
    if(envidx>=4096){ fprintf(stderr,"[YS] env stack overflow\n"); envidx=8; }
    Env *e=&envpool[envidx++];
    e->count=0; e->parent=parent; return e;
}
void env_free(Env *e){ (void)e; if(envidx>0) envidx--; }
Val *env_get(Env *e,const char *name){
    for(;e;e=e->parent)
        for(int i=0;i<e->count;i++)
            if(strcmp_u(e->names[i],name)==0) return &e->vals[i];
    return 0;
}
void env_set(Env *e,const char *name,Val v){
    for(Env *s=e;s;s=s->parent)
        for(int i=0;i<s->count;i++)
            if(strcmp_u(s->names[i],name)==0){s->vals[i]=v;return;}
    env_def(e,name,v);
}
void env_def(Env *e,const char *name,Val v){
    if(e->count>=ENV_MAX) return;
    int n=0; while(name[n]&&n<63){e->names[e->count][n]=name[n];n++;}
    e->names[e->count][n]=0;
    e->vals[e->count++]=v;
}

/*  Value constructors  */
static Val make_nil(void){
    Val r; r.type=YS_NIL; r.ival=0; r.fval=0; r.bval=0;
    r.sval[0]=0; r.fn_node=0; r.cap_fd=-1; r.cap_perm=0;
    r.cap_path[0]=0; r.arr_data=0; r.arr_len=0;
    r.struct_name[0]=0; r.field_vals=0; r.field_names=0; r.field_count=0;
    return r;
}
static Val make_int(int64_t v){Val r=make_nil();r.type=YS_INT;r.ival=v;return r;}
static Val make_float(double v){Val r=make_nil();r.type=YS_FLOAT;r.fval=v;return r;}
static Val make_bool(int v){Val r=make_nil();r.type=YS_BOOL;r.bval=v;r.ival=v;return r;}
static Val make_err(const char *msg){
    Val r=make_nil(); r.type=YS_ERR;
    int i=0; while(msg[i]&&i<254){r.sval[i]=msg[i];i++;} r.sval[i]=0;
    return r;
}
static Val make_str(const char *s){
    Val r=make_nil(); r.type=YS_STR;
    int i=0; while(s[i]&&i<8191){r.sval[i]=s[i];i++;} r.sval[i]=0;
    return r;
}
static Val make_cap(const char *path,int perm,int64_t fd){
    Val r=make_nil(); r.type=YS_CAP;
    r.cap_fd=fd; r.cap_perm=perm;
    int i=0; while(path[i]&&i<127){r.cap_path[i]=path[i];i++;} r.cap_path[i]=0;
    return r;
}

/*  Value helpers  */
static int64_t val_int(Val v){
    if(v.type==YS_INT)   return v.ival;
    if(v.type==YS_FLOAT) return (int64_t)v.fval;
    if(v.type==YS_BOOL)  return v.bval;
    if(v.type==YS_CAP)   return v.cap_fd;
    if(v.type==YS_ARR)   return v.arr_len;
    return 0;
}
static double val_float(Val v){
    if(v.type==YS_FLOAT) return v.fval;
    return (double)val_int(v);
}
static int val_bool(Val v){
    if(v.type==YS_BOOL) return v.bval;
    if(v.type==YS_INT)  return v.ival!=0;
    if(v.type==YS_STR)  return v.sval[0]!=0;
    if(v.type==YS_ARR)  return v.arr_len>0;
    if(v.type==YS_CAP)  return v.cap_fd>=0;
    return 0;
}

static void int_to_str(int64_t n,char *buf){
    if(n==0){buf[0]='0';buf[1]=0;return;}
    int neg=0; if(n<0){neg=1;n=-n;}
    char tmp[24]; int i=0;
    while(n>0){tmp[i++]=(char)('0'+n%10);n/=10;}
    int j=0; if(neg)buf[j++]='-';
    while(i>0) { buf[j++]=tmp[--i]; } buf[j]=0;
}

/*  Print value  */
void ys_print_val(Val v){
    if(v.type==YS_INT){
        /* v2.2: enum value has display name in sval */
        if(v.sval[0] && strchr(v.sval,'.')){ ys_print(v.sval); return; }
        char b[24]; int_to_str(v.ival,b); puts(b);
    } else if(v.type==YS_FLOAT){
        char b[64];
        /* print with up to 6 significant decimal places, strip trailing zeros */
        snprintf(b,sizeof(b),"%.6f",v.fval);
        int dot=-1,last=0;
        for(int i=0;b[i];i++){ if(b[i]=='.') dot=i; last=i; }
        if(dot>=0){
            while(last>dot+1 && b[last]=='0') last--;
        }
        b[last+1]=0;
        puts(b);
    } else if(v.type==YS_BOOL){
        puts(v.bval?"true":"false");
    } else if(v.type==YS_STR){
        puts(v.sval);
    } else if(v.type==YS_ARR){
        puts("[");
        for(int i=0;i<v.arr_len;i++){
            if(i>0) puts(", ");
            ys_print_val(v.arr_data[i]);
        }
        puts("]");
    } else if(v.type==YS_STRUCT){
        puts(v.struct_name); puts("{");
        for(int i=0;i<v.field_count;i++){
            if(i>0) puts(", ");
            puts(v.field_names[i]); puts(": ");
            ys_print_val(v.field_vals[i]);
        }
        puts("}");
    } else if(v.type==YS_CAP){
        puts("<cap:"); puts(v.cap_path); puts(">");
    } else {
        puts("nil");
    }
}

/*  Forward  */
Val eval_block(Node *b,Env *parent);
static Val call_builtin(const char *name,Node **args,int argc,Env *env);

/*  Safe eval: always reads g_return_val after eval_node  */
#define EVAL_SAFE(n, env, dest) do {     eval_node((n),(env));     memcpy(&(dest), &g_return_val, sizeof(Val)); } while(0)

/*  Return signal  */
int g_returning=0;
static Val g_return_val;
static int g_cur_line=0;  /* last known source line */
static int g_ann_depth=0; /* annotation fire depth — suppress nested calls */
int g_throwing=0;     /* throw signal */
char g_throw_msg[512]; /* thrown message */
static Val g_throw_val;       /* thrown value — use g_throw_msg for str content */
static int g_breaking=0;   /* break signal — exit innermost loop */
static int g_continuing=0;
int g_assert_count=0; /* v2.1: assertion counter for test runner */

/* v1.4: Levenshtein distance for typo suggestions in error messages */
static int lev_dist(const char *a, const char *b){
    int la=0,lb=0;
    while(a[la])la++;
    while(b[lb])lb++;
    if(la>16||lb>16) return 99;
    static int dp[17][17];
    for(int i=0;i<=la;i++) dp[i][0]=i;
    for(int j=0;j<=lb;j++) dp[0][j]=j;
    for(int i=1;i<=la;i++)
        for(int j=1;j<=lb;j++){
            int cost=(a[i-1]==b[j-1])?0:1;
            int del=dp[i-1][j]+1,ins=dp[i][j-1]+1,sub=dp[i-1][j-1]+cost;
            dp[i][j]=del<ins?(del<sub?del:sub):(ins<sub?ins:sub);
        }
    return dp[la][lb];
}

/* v1.6 import cache */
char g_imported_modules[64][512];
int  g_nimported=0;
static int g_idepth=0;
static char g_istack[16][512];
static int icached(const char *p){int i;for(i=0;i<g_nimported;i++)if(!strcmp(g_imported_modules[i],p))return 1;return 0;}
static int icirc(const char *p){int i;for(i=0;i<g_idepth;i++)if(!strcmp(g_istack[i],p))return 1;return 0;}
static void ipush(const char *p){if(g_idepth<16){snprintf(g_istack[g_idepth],512,"%s",p);g_idepth++;}}
static void ipop(void){if(g_idepth>0)g_idepth--;}
static void iadd(const char *p){if(g_nimported<64){snprintf(g_imported_modules[g_nimported],512,"%s",p);g_nimported++;}}
static void iresolve(const char *raw,char *out,int sz){
    extern char g_src_dir[512];
    const char *base=(g_src_dir[0])?g_src_dir:".";
    char tmp[1024];
    /* cap input lengths to avoid truncation */
    int blen=(int)strlen(base); if(blen>400)blen=400;
    int rlen=(int)strlen(raw);  if(rlen>500)rlen=500;
    int rel=(raw[0]=='.'&&(raw[1]=='/'||raw[1]=='.'));
    if(rel){
        snprintf(tmp,sizeof(tmp),"%.*s/%.*s",blen,base,rlen,raw);
    } else {
        snprintf(tmp,sizeof(tmp),"%.*s/%.*s",blen,base,rlen,raw);
        FILE*f2=fopen(tmp,"r");
        if(f2){fclose(f2);}
        else{snprintf(tmp,sizeof(tmp),"%.*s",rlen,raw);}
    }
    /* safe copy to out */
    int tl=(int)strlen(tmp);
    if(tl>=sz) tl=sz-1;
    memcpy(out,tmp,tl); out[tl]=0;
    /* auto-append .y */
    int l=(int)strlen(out);
    if(l>1&&!(out[l-2]=='.'&&out[l-1]=='y')&&l+2<sz){out[l]='.';out[l+1]='y';out[l+2]=0;}
}


/*  Runtime error  */
char g_src_file[512]={0};
void ys_error(int line, int col, const char *msg){
    char buf[512]; int n=0;
    if(g_src_file[0]){for(int i=0;g_src_file[i]&&n<200;i++)buf[n++]=g_src_file[i];buf[n++]=':';}else{buf[n++]='[';buf[n++]='Y';buf[n++]='S';buf[n++]=']';buf[n++]=':';}
    if(line>0){
        char tmp[16];int ti=0,ln=line;
        do{tmp[ti++]=(char)('0'+(ln%10));ln/=10;}while(ln>0);
        while(ti>0){buf[n++]=tmp[--ti];}
        buf[n++]=':';
        if(col>0){ti=0;int cn=col;do{tmp[ti++]=(char)('0'+(cn%10));cn/=10;}while(cn>0);while(ti>0)buf[n++]=tmp[--ti];buf[n++]=':';}
    }
    buf[n++]=' ';
    for(int i=0;msg[i]&&n<500;i++) buf[n++]=msg[i];
    buf[n++]='\n'; buf[n]=0;
    ys_print(buf);
}

/*  struct registry  */
#define MAX_STRUCTS 16
static struct {
    char name[32];
    char fields[8][32];
    int  nfields;
} structs[MAX_STRUCTS];
static int nstructs=0;

/*  impl method registry  */
#define MAX_METHODS 64
static struct {
    char struct_name[32];
    char method_name[32];
    Node *fn_node;
} methods[MAX_METHODS];
static int nmethods=0;

/*  eval_node  */

/*  String interpolation: "Hello {name}!"  */
static Val eval_interp_str(const char *s, Env *env){
    char out[8192]; int oi=0;
    int i=0;
    while(s[i]&&oi<8190){
        if(s[i]=='{'){
            i++;
            /* collect expression source until matching } */
            char expr[128]; int ei=0;
            int depth=1;
            while(s[i]&&ei<126){
                if(s[i]=='{') depth++;
                if(s[i]=='}'){depth--;if(depth==0){i++;break;}}
                expr[ei++]=s[i++];
            }
            expr[ei]=0;
            if(ei==0) continue;
            /* {0}, {1}, {2}... are positional placeholders for y.format — pass through literally.
               Yolish identifiers cannot start with a digit, so all-digit content is never a
               variable name; emitting it as-is lets y.format do the substitution later. */
            {
                int all_digits=1;
                for(int _di=0;_di<ei;_di++) if(expr[_di]<'0'||expr[_di]>'9'){all_digits=0;break;}
                if(all_digits){
                    if(oi<8188){ out[oi++]='{';
                        for(int _di=0;_di<ei&&oi<8188;_di++) out[oi++]=expr[_di];
                        out[oi++]='}';
                    }
                    continue;
                }
            }
            /* parse and eval the expression */
            Lexer el; lex_init(&el,expr,ei);
            parser_pool_save();
            Node *en=parse_program(&el);
            /* eval: just eval first statement */
            Val rv=make_nil();
            if(en&&en->stmtc>0) rv=eval_node(en->stmts[0],env);
            parser_pool_restore();
            /* convert to string */
            char tmp[128]; int ti=0;
            if(rv.type==YS_INT){
                int64_t v=rv.ival; int neg=v<0; if(neg)v=-v;
                char tb[32]; int tbi=0;
                do{tb[tbi++]=(char)('0'+(v%10));v/=10;}while(v>0);
                if(neg&&oi<8188) out[oi++]='-';
                while(tbi>0&&oi<8188) out[oi++]=tb[--tbi];
            } else if(rv.type==YS_FLOAT){
                char tb[64];
                snprintf(tb,sizeof(tb),"%.6f",rv.fval);
                int dot=-1,last2=0;
                for(int _i=0;tb[_i];_i++){if(tb[_i]=='.')dot=_i;last2=_i;}
                if(dot>=0){while(last2>dot+1&&tb[last2]=='0')last2--;}
                tb[last2+1]=0;
                for(int _i=0;tb[_i]&&oi<8188;_i++) out[oi++]=tb[_i];
            } else if(rv.type==YS_STR){
                int si2=0; while(rv.sval[si2]&&oi<8188)out[oi++]=rv.sval[si2++];
            } else if(rv.type==YS_ARR){
                out[oi++]='[';
                for(int _ai=0;_ai<rv.arr_len&&oi<8188;_ai++){
                    Val _el=rv.arr_data[_ai];
                    if(_el.type==YS_INT){
                        int64_t v=_el.ival; int neg=v<0; if(neg)v=-v;
                        char tb[24]; int ti=0;
                        do{tb[ti++]=(char)('0'+(v%10));v/=10;}while(v>0);
                        if(neg&&oi<8188)out[oi++]='-';
                        while(ti>0&&oi<8188)out[oi++]=tb[--ti];
                    } else if(_el.type==YS_STR){
                        int _si=0; while(_el.sval[_si]&&oi<8188)out[oi++]=_el.sval[_si++];
                    } else {
                        out[oi++]='?';
                    }
                    if(_ai<rv.arr_len-1&&oi<8188){out[oi++]=','; out[oi++]=' ';}
                }
                out[oi++]=']';
            } else if(rv.type==YS_BOOL){
                const char *bs2=rv.bval?"true":"false"; while(*bs2&&oi<8188)out[oi++]=*bs2++;
            } else if(rv.type==YS_NIL){
                const char *ns="nil"; while(*ns&&oi<8188)out[oi++]=*ns++;
            }
            (void)tmp; (void)ti;
        } else if(s[i]=='\\' && s[i+1]=='{'){
            /* escaped brace \{ → literal { */
            out[oi++]='{'; i+=2;
        } else {
            out[oi++]=s[i++];
        }
    }
    out[oi]=0;
    return make_str(out);
}

/*  Module function call — in separate fn to reduce eval_node stack frame  */
__attribute__((noinline))
static Val call_module_fn(Val *fv2, Node *n, Env *env){
    Node *fd=fv2->fn_node;
    Env *ce=(fv2->fn_env)?((Env*)fv2->fn_env):env;
    Env *fe=env_new(ce);
    int arg_start=(n->argc>fd->argc)?1:0;
    for(int i=0;i<fd->argc&&(i+arg_start)<n->argc;i++){
        Val arg=eval_node(n->args[i+arg_start],env);
        env_def(fe,fd->field_names[i],arg);
    }
    g_returning=0;
    int saved=envidx;
    static Val _mod_result;
    { Val _tmp=eval_block(fd->body,fe);
      memcpy(&_mod_result,&_tmp,sizeof(Val)); }
    envidx=saved;
    if(g_returning){ memcpy(&_mod_result,&g_return_val,sizeof(Val)); }
    g_returning=0;
    return _mod_result;
}
__attribute__((noinline)) Val eval_node(Node *n,Env *env){
    if(!n) return make_nil();
    switch(n->kind){

    case ND_INT:   return make_int(n->ival);
    case ND_FLOAT: return make_float(n->fval);
    case ND_BOOL:  return make_bool((int)n->ival);
    case ND_STR:{
        /* raw string: sentinel \x01 prefix — skip interpolation, return content after sentinel */
        if(n->sval[0]=='\x01') return make_str(n->sval+1);
        /* check for {expr} interpolation */
        int _has_interp=0;
        for(int _i=0;n->sval[_i];_i++) if(n->sval[_i]=='{'){_has_interp=1;break;}
        if(_has_interp) return eval_interp_str(n->sval,env);
        return make_str(n->sval);
    }

    case ND_IDENT:{
        Val *v=env_get(env,n->name);
        if(v) return *v;
        /* builtin namespace prefixes are not variables */
        { static const char *ns[]={"y","process","sys","cap","gc",NULL};
          for(int _k=0;ns[_k];_k++) if(strcmp(n->name,ns[_k])==0) return make_nil(); }
        /* v1.4: undefined variable with typo suggestion */
        char suggest[64]=""; int best=99;
        Env *se=env;
        while(se){
            for(int _i=0;_i<se->count;_i++){
                int d=lev_dist(n->name,se->names[_i]);
                if(d>0 && d<best && d<=3){
                    best=d;
                    snprintf(suggest,64,"%s",se->names[_i]);
                }
            }
            se=se->parent;
        }
        char errmsg[256];
        if(suggest[0])
            snprintf(errmsg,sizeof(errmsg),"undefined '%s' — did you mean '%s'?",n->name,suggest);
        else
            snprintf(errmsg,sizeof(errmsg),"undefined '%s'",n->name);
        ys_error(g_cur_line,0,errmsg);
        return make_nil();
    }

    case ND_LET: case ND_VAR:{
        Val v=make_nil();
        if(n->right){
            /* Write result to g_return_val first, then read from it */
            Val _tmp=eval_node(n->right,env);
            if(g_returning==2){
                /* match result */
                memcpy(&v,&g_return_val,sizeof(Val));
                g_returning=0;
            } else if(g_returning==1){
                /* function return propagates up */
                memcpy(&v,&g_return_val,sizeof(Val));
            } else {
                memcpy(&v,&_tmp,sizeof(Val));
                if(v.type==YS_NIL && g_return_val.type!=YS_NIL)
                    memcpy(&v,&g_return_val,sizeof(Val));
            }
        }
        env_def(env,n->name,v); return v;
    }

    case ND_ASSIGN:{
        Val v=eval_node(n->right,env);
        if(n->left&&n->left->kind==ND_IDENT)
            env_set(env,n->left->name,v);
        return v;
    }

    /*  Array literal  */
    case ND_ARRAY:{
        Val v=make_nil(); v.type=YS_ARR;
        v.arr_len=n->argc;
        v.arr_data=alloc_arr(n->argc);
        for(int i=0;i<n->argc;i++)
            v.arr_data[i]=eval_node(n->args[i],env);
        return v;
    }

    /*  Array index read  */
    /* struct field access: point.x */
    case ND_DOT:{
        Val obj=eval_node(n->left,env);
        if(obj.type==YS_STRUCT){
            for(int i=0;i<obj.field_count;i++){
                if(strcmp_u(obj.field_names[i],n->name)==0)
                    return obj.field_vals[i];
            }
            ys_error(g_cur_line,0,"unknown struct field");
            return make_nil();
        }
        /* v2.2: enum access — sentinel has sval="enum:EnumName" */
        if(obj.type==YS_INT && obj.sval[0]=='e' && obj.sval[1]=='n'
           && obj.sval[2]=='u' && obj.sval[3]=='m' && obj.sval[4]==':'){
            /* look up "EnumName.Variant" in env */
            char qname[128];
            snprintf(qname,sizeof(qname),"%.60s.%.60s",&obj.sval[5],n->name);
            Val *ev=env_get(env,qname);
            if(ev) return *ev;
            return make_nil();
        }
        /* dot on non-struct/non-enum: treat as builtin namespace */
        return make_nil();
    }

    case ND_INDEX:{
        Val arr=eval_node(n->left,env);
        Val idx=eval_node(n->right,env);
        int i=(int)val_int(idx);
        /* struct field access via index */
        if(arr.type==YS_STRUCT){
            const char *fname=n->right->sval;
            for(int j=0;j<arr.field_count;j++)
                if(strcmp_u(arr.field_names[j],fname)==0)
                    return arr.field_vals[j];
            return make_nil();
        }
        if(arr.type!=YS_ARR||!arr.arr_data) return make_nil();
        if(i<0||i>=arr.arr_len) return make_nil();
        return arr.arr_data[i];
    }

    /*  Array index write  */
    case ND_INDEX_SET:{
        Val arr=eval_node(n->left->left,env);
        Val idx=eval_node(n->left->right,env);
        Val val=eval_node(n->right,env);
        int i=(int)val_int(idx);
        if(arr.type==YS_ARR&&arr.arr_data&&i>=0&&i<arr.arr_len)
            arr.arr_data[i]=val;
        if(n->left->left->kind==ND_IDENT)
            env_set(env,n->left->left->name,arr);
        return val;
    }

    /*  Struct definition  */

    case ND_TEST:
        /* In normal interpreter mode, test blocks are skipped.
           Use  ys test file.y  to run them. */
        return make_nil();

    case ND_ENUM:{
        /* v2.2: register EnumName.Variant (qualified only, saves env slots) */
        for(int i=0;i<n->stmtc;i++){
            Node *v=n->stmts[i];
            char qname[128];
            snprintf(qname,sizeof(qname),"%s.%s",n->name,v->name);
            Val ev; ev.type=YS_INT; ev.ival=(int64_t)i;
            snprintf(ev.sval,sizeof(ev.sval),"%s.%s",n->name,v->name);
            env_def(env,qname,ev);
        }
        /* sentinel: env["Direction"] = int(4) with sval="enum:Direction" */
        Val meta; meta.type=YS_INT; meta.ival=(int64_t)n->stmtc;
        for(int i=0;i<(int)sizeof(meta.sval);i++) meta.sval[i]=0;
        snprintf(meta.sval,sizeof(meta.sval),"enum:%s",n->name);
        env_def(env,n->name,meta);
        return make_nil();
    }
    case ND_STRUCT:{
        if(nstructs<MAX_STRUCTS){
            int si=nstructs++;
            int ni=0;
            while(n->name[ni]&&ni<31){structs[si].name[ni]=n->name[ni];ni++;}
            structs[si].name[ni]=0;
            structs[si].nfields=n->stmtc;
            for(int i=0;i<n->stmtc&&i<8;i++){
                int fi=0;
                const char *fn=n->stmts[i]->name;
                while(fn[fi]&&fi<31){structs[si].fields[i][fi]=fn[fi];fi++;}
                structs[si].fields[i][fi]=0;
            }
        }
        return make_nil();
    }

    /*  Struct literal  */
    case ND_STRUCT_LIT:{
        Val v=make_nil(); v.type=YS_STRUCT;
        int ni=0;
        while(n->name[ni]&&ni<31){v.struct_name[ni]=n->name[ni];ni++;}
        v.struct_name[ni]=0;
        v.field_count=n->argc;
        v.field_vals=alloc_fld(n->argc);
        v.field_names=alloc_nm(n->argc);
        for(int i=0;i<n->argc;i++){
            int fi=0;
            const char *fn=n->field_names[i];
            while(fn[fi]&&fi<31){v.field_names[i][fi]=fn[fi];fi++;}
            v.field_names[i][fi]=0;
            v.field_vals[i]=eval_node(n->args[i],env);
        }
        return v;
    }

    /*  impl block — register methods for a struct  */
    case ND_IMPL:{
        for(int i=0;i<n->stmtc;i++){
            Node *fn=n->stmts[i];
            if(fn&&fn->kind==ND_FN&&nmethods<MAX_METHODS){
                int si=nmethods++;
                /* struct name */
                int ni=0;
                while(n->name[ni]&&ni<31){methods[si].struct_name[ni]=n->name[ni];ni++;}
                methods[si].struct_name[ni]=0;
                /* method name */
                int mi=0;
                while(fn->name[mi]&&mi<31){methods[si].method_name[mi]=fn->name[mi];mi++;}
                methods[si].method_name[mi]=0;
                methods[si].fn_node=fn;
            }
        }
        return make_nil();
    }

    case ND_BINOP:{
        Val L=eval_node(n->left,env);
        Val R=eval_node(n->right,env);
        int use_f=(L.type==YS_FLOAT||R.type==YS_FLOAT);
        switch(n->op){
        case TK_PLUS:
            if(L.type==YS_STR){
                Val r=make_str(L.sval);
                int i=0; while(r.sval[i])i++;
                int j=0; while(R.sval[j]&&i<8190)r.sval[i++]=R.sval[j++];
                r.sval[i]=0; return r;
            }
            return use_f?make_float(val_float(L)+val_float(R)):make_int(val_int(L)+val_int(R));
        case TK_MINUS:
            return use_f?make_float(val_float(L)-val_float(R))
                        :make_int(val_int(L)-val_int(R));
        case TK_STAR:
            return use_f?make_float(val_float(L)*val_float(R))
                        :make_int(val_int(L)*val_int(R));
        case TK_SLASH:{int64_t d=val_int(R);
            return use_f?make_float(val_float(R)!=0.0?val_float(L)/val_float(R):0.0)
                        :make_int(d?val_int(L)/d:0);}
        case TK_PERCENT:{int64_t d=val_int(R);return make_int(d?val_int(L)%d:0);}
        case TK_EQEQ:
            if(L.type==YS_STR&&R.type==YS_STR)
                return make_bool(strcmp_u(L.sval,R.sval)==0);
            return make_bool(val_int(L)==val_int(R));
        case TK_NEQ:
            if(L.type==YS_STR&&R.type==YS_STR)
                return make_bool(strcmp_u(L.sval,R.sval)!=0);
            return make_bool(val_int(L)!=val_int(R));
        case TK_LT:   return make_bool(use_f?val_float(L)<val_float(R):val_int(L)<val_int(R));
        case TK_GT:   return make_bool(use_f?val_float(L)>val_float(R):val_int(L)>val_int(R));
        case TK_LTE:  return make_bool(use_f?val_float(L)<=val_float(R):val_int(L)<=val_int(R));
        case TK_GTE:  return make_bool(use_f?val_float(L)>=val_float(R):val_int(L)>=val_int(R));
        case TK_AND:  return make_bool(val_bool(L)&&val_bool(R));
        case TK_OR:   return make_bool(val_bool(L)||val_bool(R));
        default: return make_nil();
        }
    }

    case ND_UNOP:{
        Val v=eval_node(n->left,env);
        if(n->op==TK_MINUS)
            return v.type==YS_FLOAT?make_float(-v.fval):make_int(-val_int(v));
        if(n->op==TK_BANG) return make_bool(!val_bool(v));
        return v;
    }

    case ND_IF:{
        Val c=eval_node(n->cond,env);
        if(val_bool(c)) return eval_block(n->then,env);
        if(n->els){
            /* else if produces a nested ND_IF, not a block */
            if(n->els->kind==ND_IF) return eval_node(n->els,env);
            return eval_block(n->els,env);
        }
        return make_nil();
    }

    case ND_WHILE:{
        Val last=make_nil();
        while(val_bool(eval_node(n->cond,env))){
            last=eval_block(n->body,env);
            if(g_continuing){ g_continuing=0; continue; }
            if(g_breaking)  { g_breaking=0;   break; }
            if((g_returning&&g_returning!=2)||g_throwing) break;
        }
        return last;
    }

    /* for item in arr   /   for i in start..end */
    case ND_FOR:{
        Val last=make_nil();
        /* range: check AST shape BEFORE evaluating */
        if(n->cond && n->cond->kind==ND_BINOP && n->cond->op==TK_DOTDOT){
            int64_t lo=val_int(eval_node(n->cond->left,env));
            int64_t hi=val_int(eval_node(n->cond->right,env));
            for(int64_t idx=lo; idx<hi; idx++){
                Env *fe=env_new(env);
                env_def(fe,n->name,make_int(idx));
                last=eval_block(n->body,fe);
                if(g_continuing){ g_continuing=0; continue; }
                if(g_breaking)  { g_breaking=0;   break; }
                if(g_returning||g_throwing) break;
            }
        } else {
            Val iter=eval_node(n->cond,env);
            if(iter.type==YS_ARR){
                for(int idx=0;idx<iter.arr_len;idx++){
                    Env *fe=env_new(env);
                    env_def(fe,n->name,iter.arr_data[idx]);
                    last=eval_block(n->body,fe);
                    if(g_continuing){ g_continuing=0; continue; }
                    if(g_breaking)  { g_breaking=0;   break; }
                    if(g_returning||g_throwing) break;
                }
            } else if(iter.type==YS_STR){
                int slen=str_len_u(iter.sval);
                for(int idx=0;idx<slen;idx++){
                    char ch[2]; ch[0]=iter.sval[idx]; ch[1]=0;
                    Env *fe=env_new(env);
                    env_def(fe,n->name,make_str(ch));
                    last=eval_block(n->body,fe);
                    if(g_continuing){ g_continuing=0; continue; }
                    if(g_breaking)  { g_breaking=0;   break; }
                    if(g_returning||g_throwing) break;
                }
            }
        }
        return last;
    }

    case ND_MATCH:{
        Val subject=eval_node(n->cond,env);
        for(int i=0;i<n->argc;i++){
            Node *arm_node=n->arg_data[i];
            if(!arm_node) continue;
            /* support both old-style (pat/body in i*2 slots) and new ND_MATCH_ARM */
            Node *pat, *body, *guard;
            if(arm_node->kind==ND_MATCH_ARM){
                pat=arm_node->left; guard=arm_node->cond; body=arm_node->right;
            } else {
                /* fallback: shouldn't happen but be safe */
                pat=arm_node; guard=NULL; body=NULL;
            }
            if(!pat||!body) continue;
            int matched=0;

            /* wildcard _ */
            if(pat->kind==ND_IDENT && pat->name[0]=='_' && pat->name[1]==0){
                matched=1;
            }
            /* named binding: bare ident (not a known variable) — binds subject */
            else if(pat->kind==ND_IDENT){
                /* always matches and binds subject to this name in a child env */
                matched=1;
            }
            /* range pattern: a..b */
            else if(pat->kind==ND_BINOP && pat->op==TK_DOTDOT){
                int64_t lo=val_int(eval_node(pat->left, env));
                int64_t hi=val_int(eval_node(pat->right,env));
                int64_t sv=val_int(subject);
                matched=(sv>=lo && sv<hi);
            }
            /* literal patterns */
            else {
                Val pv=eval_node(pat,env);
                if(subject.type==YS_INT   && pv.type==YS_INT)  matched=(subject.ival==pv.ival);
                else if(subject.type==YS_FLOAT && pv.type==YS_FLOAT) matched=(subject.fval==pv.fval);
                else if(subject.type==YS_BOOL  && pv.type==YS_BOOL)  matched=(subject.bval==pv.bval);
                else if(subject.type==YS_STR   && pv.type==YS_STR)   matched=(strcmp_u(subject.sval,pv.sval)==0);
                else if(subject.type==YS_INT   && pv.type==YS_FLOAT) matched=((double)subject.ival==pv.fval);
                else matched=0;
            }

            if(matched){
                /* create arm env and bind name if pattern is a named ident */
                Env *arm_env=env;
                int is_binding=(pat->kind==ND_IDENT && !(pat->name[0]=='_' && pat->name[1]==0));
                if(is_binding || guard){
                    arm_env=env_new(env);
                    if(is_binding) env_def(arm_env,pat->name,subject);
                }
                /* guard: evaluate in arm_env so binding is visible */
                if(guard && !val_bool(eval_node(guard,arm_env))){
                    /* guard failed — keep looking */
                    matched=0;
                    continue;
                }
                Val _mv;
                if(body->kind==ND_BLOCK) _mv=eval_block(body,arm_env);
                else _mv=eval_node(body,arm_env);
                if(g_returning==1) return g_return_val;
                if(g_returning==2){ memcpy(&_mv,&g_return_val,sizeof(Val)); g_returning=0; }
                return _mv;
            }
        }
        return make_nil(); /* no arm matched */
    }

    case ND_THROW:{
        Val thrown = n->right ? eval_node(n->right,env) : make_nil();
        /* store message in stable global string */
        if(thrown.type==YS_STRUCT){
            /* Error struct — extract .message field */
            for(int _fi=0;_fi<thrown.field_count;_fi++){
                if(thrown.field_names[_fi][0]=='m'&&thrown.field_names[_fi][1]=='e'){
                    Val _mv=thrown.field_vals[_fi];
                    int ci=0; while(_mv.sval[ci]&&ci<510){g_throw_msg[ci]=_mv.sval[ci];ci++;}
                    g_throw_msg[ci]=0; break;
                }
            }
            g_throw_val=thrown; /* keep struct for catch */
            g_throwing=1; return g_throw_val;
        } else if(thrown.type==YS_STR||thrown.type==YS_ERR){
            int ci=0; while(thrown.sval[ci]&&ci<510){g_throw_msg[ci]=thrown.sval[ci];ci++;}
            g_throw_msg[ci]=0;
        } else if(thrown.type==YS_INT){
            /* convert int to string */
            int64_t v=thrown.ival; int neg=v<0; if(neg)v=-v;
            char tb[32]; int ti=0;
            do{tb[ti++]=(char)('0'+(v%10));v/=10;}while(v>0);
            int ci=0; if(neg)g_throw_msg[ci++]='-';
            while(ti>0&&ci<510) g_throw_msg[ci++]=tb[--ti];
            g_throw_msg[ci]=0;
        } else {
            g_throw_msg[0]='e';g_throw_msg[1]='r';g_throw_msg[2]='r';g_throw_msg[3]=0;
        }
        g_throw_val=make_err(g_throw_msg);
        g_throwing=1;
        return g_throw_val;
    }

    case ND_TRY:{
        /* run try body; catch any throw */
        g_throwing=0;
        Val result=eval_block(n->then,env);
        if(g_throwing){
            g_throwing=0;
            if(n->els){
                Env *ce=env_new(env);
                if(n->name[0]){
                    /* pass struct error directly, else wrap as string */
                    if(g_throw_val.type==YS_STRUCT)
                        env_def(ce,n->name,g_throw_val);
                    else
                        env_def(ce,n->name,make_str(g_throw_msg));
                }
                int saved=envidx;
                result=eval_block(n->els,ce);
                envidx=saved;
            }
        }
        return result;
    }

    case ND_BLOCK: return eval_block(n,env);

    case ND_FN_LIT:{
        Val v=make_nil(); v.type=YS_FN; v.fn_node=n;
        v.fn_env=(void*)env; /* capture current environment */
        return v;
    }

    case ND_FN:{
        Val v=make_nil(); v.type=YS_FN; v.fn_node=n;
        /* annotation info lives in fn_node->type and fn_node->sval */
        env_def(env,n->name,v); return v;
    }

    case ND_CALL:{
        if(n->name[0]=='@'
            ||strcmp_u(n->name,"y.print")==0 ||strcmp_u(n->name,"print")==0
            ||strcmp_u(n->name,"y.println")==0||strcmp_u(n->name,"println")==0
            ||strcmp_u(n->name,"y.input")==0  ||strcmp_u(n->name,"input")==0
            ||strcmp_u(n->name,"y.input_int")==0
            ||strcmp_u(n->name,"y.input_float")==0
            ||strcmp_u(n->name,"y.len")==0    ||strcmp_u(n->name,"len")==0
            ||strcmp_u(n->name,"y.abs")==0    ||strcmp_u(n->name,"abs")==0
            ||strcmp_u(n->name,"y.str")==0    ||strcmp_u(n->name,"str")==0
            ||strcmp_u(n->name,"y.int")==0    ||strcmp_u(n->name,"int")==0
            ||strcmp_u(n->name,"y.float")==0  ||strcmp_u(n->name,"float")==0
            ||strcmp_u(n->name,"y.bool")==0   ||strcmp_u(n->name,"bool")==0
            ||strcmp_u(n->name,"y.push")==0   ||strcmp_u(n->name,"push")==0
            ||strcmp_u(n->name,"y.pop")==0    ||strcmp_u(n->name,"pop")==0
            ||strcmp_u(n->name,"y.sort")==0   ||strcmp_u(n->name,"sort")==0
            ||strcmp_u(n->name,"y.range")==0  ||strcmp_u(n->name,"range")==0
            ||strcmp_u(n->name,"y.zip")==0    ||strcmp_u(n->name,"zip")==0
            ||strcmp_u(n->name,"y.flatten")==0||strcmp_u(n->name,"flatten")==0
            ||strcmp_u(n->name,"y.sum")==0    ||strcmp_u(n->name,"sum")==0
            ||strcmp_u(n->name,"y.map")==0    ||strcmp_u(n->name,"map")==0
            ||strcmp_u(n->name,"y.filter")==0 ||strcmp_u(n->name,"filter")==0
            ||strcmp_u(n->name,"y.reduce")==0 ||strcmp_u(n->name,"reduce")==0
            ||strcmp_u(n->name,"y.each")==0   ||strcmp_u(n->name,"each")==0
            ||strcmp_u(n->name,"y.min_arr")==0||strcmp_u(n->name,"y.max_arr")==0
            ||strcmp_u(n->name,"y.exit")==0   ||strcmp_u(n->name,"exit")==0
            /* v2.1 test assertions */
            ||strcmp_u(n->name,"assert")==0
            ||strcmp_u(n->name,"assert_eq")==0
            ||strcmp_u(n->name,"assert_neq")==0
            ||strcmp_u(n->name,"assert_true")==0
            ||strcmp_u(n->name,"assert_false")==0
            ||strcmp_u(n->name,"assert_nil")==0)
            return call_builtin(n->name,n->args,n->argc,env);

        /* dot calls — build fully qualified name (handles y.math.sqrt etc) */
        if(n->left){
            static char qname[128];
            /* walk left chain to build prefix */
            Node *chain[8]; int depth=0;
            Node *cur2=n->left;
            while(cur2&&depth<8){
                chain[depth++]=cur2;
                if(cur2->kind==ND_DOT) cur2=cur2->left;
                else break;
            }
            int qi=0;
            /* build from outermost to innermost */
            for(int ci=depth-1;ci>=0;ci--){
                const char *seg=chain[ci]->name;
                int si=0; while(seg[si]&&qi<120){qname[qi++]=seg[si++];}
                qname[qi++]='.';
            }
            const char *mth=n->name;
            int mi=0; while(mth[mi]&&qi<126){qname[qi++]=mth[mi++];}
            qname[qi]=0;
            /* first try as builtin */
            if(strncmp(qname,"y.",2)==0||qname[0]=='@')
                return call_builtin(qname,n->args,n->argc,env);
            /* eval the object — works for identifiers AND chained calls */
            static Val _obj_val_static;
            { Val _obj_tmp=eval_node(n->left,env); memcpy(&_obj_val_static,&_obj_tmp,sizeof(Val)); }
            Val obj_val=_obj_val_static;
            /* impl method call: look up method in registry */
            if(obj_val.type==YS_STRUCT){
                for(int mi2=0;mi2<nmethods;mi2++){
                    if(strcmp_u(methods[mi2].struct_name,obj_val.struct_name)==0
                    && strcmp_u(methods[mi2].method_name,n->name)==0){
                        Node *fd=methods[mi2].fn_node;
                        Env *fe=env_new(env);
                        /* in a dot call: args[0]=obj, args[1..]=explicit args
                           fn params: field_names[0]="self", field_names[1..]=rest */
                        if(fd->argc>0 && strcmp_u(fd->field_names[0],"self")==0){
                            env_def(fe,"self",obj_val);
                            /* explicit args start at n->args[1] (args[0] is obj) */
                            for(int pi=1;pi<fd->argc&&pi<n->argc;pi++){
                                Val arg=eval_node(n->args[pi],env);
                                env_def(fe,fd->field_names[pi],arg);
                            }
                        } else {
                            /* no self param — bind explicit args starting at args[1] */
                            for(int pi=0;pi<fd->argc&&(pi+1)<n->argc;pi++){
                                Val arg=eval_node(n->args[pi+1],env);
                                env_def(fe,fd->field_names[pi],arg);
                            }
                        }
                        g_returning=0; g_breaking=0; g_continuing=0;
                        int saved=envidx;
                        Val result=eval_block(fd->body,fe);
                        envidx=saved;
                        if(g_returning){ memcpy(&result,&g_return_val,sizeof(Val)); }
                        g_returning=0;
                        return result;
                    }
                }
                /* field access or field fn call */
                for(int fi=0;fi<obj_val.field_count;fi++){
                    if(strcmp_u(obj_val.field_names[fi],n->name)==0){
                        Val fv2=obj_val.field_vals[fi];
                        if(fv2.type==YS_FN&&fv2.fn_node){
                            return call_module_fn(&fv2,n,env);
                        }
                        return obj_val.field_vals[fi];
                    }
                }
            }
            return call_builtin(qname,n->args,n->argc,env);
        }

        /* user function */
        Val *fv=env_get(env,n->name);
        if(fv&&fv->type==YS_FN&&fv->fn_node){
            Node *fn_def=fv->fn_node;
            /* use captured env for closures, call-site env for named fns */
            Env *closure_env=(fv->fn_env)?((Env*)fv->fn_env):env;

            /* reset return signal before any annotation side effects */
            g_returning=0;

            /* fire annotation only on outermost call, not recursive re-entry */
            {
                const char *ann_t = fn_def->type; /* annotation type from AST */
                const char *ann_a = fn_def->sval; /* annotation arg from AST */
                if(g_ann_depth==0 && ann_t[0]){
                    /*  @intent → stderr  */
                    if(ann_t[0]=='i'&&ann_t[1]=='n'){
                        fputs("[scheduler] intent=",stderr);
                        fputs(ann_a[0]?ann_a:"unspecified",stderr);
                        fputs(" fn=",stderr); fputs(n->name,stderr); fputs("\n",stderr);
                        fflush(stderr);
                    }
                    /*  @audit → stderr  */
                    else if(ann_t[0]=='a'&&ann_t[1]=='u'){
                        fputs("[audit] tag=",stderr);
                        fputs(ann_a[0]?ann_a:"untagged",stderr);
                        fputs(" fn=",stderr); fputs(n->name,stderr);
                        fputs(" args=",stderr);
                        char ac[2]; ac[0]=(char)('0'+(n->argc<9?n->argc:9)); ac[1]=0;
                        fputs(ac,stderr); fputs("\n",stderr);
                        fflush(stderr);
                    }
                }
            }

            Env *fe=env_new(closure_env);
            for(int i=0;i<fn_def->argc&&i<n->argc;i++){
                Val arg=eval_node(n->args[i],env);
                env_def(fe,fn_def->field_names[i],arg);
            }
            g_returning=0;
            g_breaking=0;   /* break/continue must not leak out of a function call */
            g_continuing=0;
            /* don't reset g_throwing — let it propagate to try/catch */
            if(fn_def->type[0]) g_ann_depth++;
            Val result=eval_block(fn_def->body,fe);
            if(fn_def->type[0]) g_ann_depth--;
            if(!g_throwing) g_returning=0; /* preserve throw signal */
            return result;
        }
        return make_nil();
    }

    case ND_MODULE:{
        /* import "file.y" as name — run file, collect fns into namespace struct */
        static char mod_src[65536];
        FILE *mf=fopen(n->sval,"r");
        if(!mf){
            /* try relative to source dir */
            char rel[640]; int di=0;
            while(g_src_dir[di]&&di<510){rel[di]=g_src_dir[di];di++;}
            int si=0; while(n->sval[si]&&di<638){rel[di++]=n->sval[si++];} rel[di]=0;
            mf=fopen(rel,"r");
        }
        if(!mf){ ys_error(g_cur_line,0,"cannot open module file"); return make_nil(); }
        int msz=(int)fread(mod_src,1,sizeof(mod_src)-1,mf);
        fclose(mf); mod_src[msz]=0;
        /* parse and eval in isolated env */
        Lexer ml; lex_init(&ml,mod_src,msz);
        Node *mprog=parse_program(&ml);
        Env *menv=env_new(NULL); /* isolated namespace */
        for(int i=0;i<mprog->stmtc;i++) eval_node(mprog->stmts[i],menv);
        /* collect all definitions into a module Val (YS_STRUCT) */
        Val mod=make_nil(); mod.type=YS_STRUCT;
        int nl2=str_len_u(n->name)<31?str_len_u(n->name):31;
        for(int i=0;i<nl2;i++) { mod.struct_name[i]=n->name[i]; } mod.struct_name[nl2]=0;
        mod.field_count=menv->count;
        mod.field_vals=alloc_fld(menv->count+1);
        mod.field_names=alloc_nm(menv->count+1);
        for(int i=0;i<menv->count;i++){
            for(int j=0;j<64;j++) mod.field_names[i][j]=menv->names[i][j];
            mod.field_vals[i]=menv->vals[i];
        }
        env_def(env,n->name,mod);
        return mod;
    }

    case ND_IMPORT:{
        char resolved[512]; iresolve(n->sval,resolved,sizeof(resolved));
        if(icached(resolved)) return make_nil();
        if(icirc(resolved)){ys_error(g_cur_line,0,"circular import");return make_nil();}
        ipush(resolved);
        static char import_src[65536];
        FILE *impf=fopen(resolved,"r");
        if(!impf){char em[300];snprintf(em,sizeof(em),"cannot import: %s",resolved);
                  ys_error(g_cur_line,0,em);ipop();return make_nil();}
        int sz=(int)fread(import_src,1,sizeof(import_src)-1,impf);
        fclose(impf); import_src[sz]=0;
        extern char g_src_dir[512];
        char odir[512],ofile[512];
        snprintf(odir,512,"%s",g_src_dir);snprintf(ofile,512,"%s",g_src_file);
        {int last=-1,rl=(int)strlen(resolved);
         for(int i=0;i<rl;i++){char ch=resolved[i];if(ch==47||ch==92)last=i;}
         if(last>=0){strncpy(g_src_dir,resolved,last);g_src_dir[last]=0;}
         strncpy(g_src_file,resolved,511);}
        Lexer il; lex_init(&il,import_src,sz);
        Node *iprog=parse_program(&il);
        eval_program(iprog,env);
        snprintf(g_src_dir,512,"%s",odir);snprintf(g_src_file,512,"%s",ofile);
        iadd(resolved);ipop();
        return make_nil();
    }

    case ND_RETURN:{
        if(n->right){
            Val _rv=eval_node(n->right,env);
            memcpy(&g_return_val,&_rv,sizeof(Val));
        } else {
            memset(&g_return_val,0,sizeof(Val));
        }
        g_returning=1;
        return g_return_val;
    }

    case ND_BREAK:
        g_breaking=1;
        return make_nil();

    case ND_CONTINUE:
        g_continuing=1;
        return make_nil();

    default: return make_nil();
    }
}

Val eval_block(Node *b,Env *parent){
    if(!b) return make_nil();
    Env *e=env_new(parent);
    Val last=make_nil();
    for(int i=0;i<b->stmtc;i++){
        gc_maybe();    /* v1.5: GC safe point — env 'e' is root, last is retired */
        last=eval_node(b->stmts[i],e);
        if(g_returning==2){ memcpy(&last,&g_return_val,sizeof(Val)); g_returning=0; }
        if((g_returning&&g_returning!=2)||g_throwing||g_breaking||g_continuing) break;
    }
    env_free(e);   /* v1.5: release this block's env slot so GC can reclaim it */
    return last;
}

/*  Builtins  */
static Val call_builtin(const char *name,Node **args,int argc,Env *env){
    if(g_throwing) return make_nil();

    /* y.print */
    if(strcmp_u(name,"y.print")==0||strcmp_u(name,"print")==0){
        int s=(argc>1)?1:0;
        for(int i=s;i<argc;i++){
            Val _pv=eval_node(args[i],env);
            if(g_throwing) return make_nil();
            ys_print_val(_pv);
        }
        return make_nil();
    }
    /* y.println */
    if(strcmp_u(name,"y.println")==0||strcmp_u(name,"println")==0){
        int s=(argc>1)?1:0;
        for(int i=s;i<argc;i++){
            Val _pv=eval_node(args[i],env);
            if(g_throwing) return make_nil();
            ys_print_val(_pv);
        }
        puts("\n"); return make_nil();
    }
    /* y.input(prompt?) — print optional prompt then read a line from stdin */
    if(strcmp_u(name,"y.input")==0||strcmp_u(name,"input")==0){
        int s=(argc>1)?1:0;
        /* print prompt if given */
        if(argc>s){
            Val prompt=eval_node(args[s],env);
            if(prompt.type==YS_STR&&prompt.sval[0]){
                fputs(prompt.sval,stdout); fflush(stdout);
            }
        }
        static char ibuf[256]; int i=0; char c=0;
        while(i<255){if(fread(&c,1,1,stdin)!=1)break;if(c=='\n'||c=='\r')break;ibuf[i++]=c;}
        ibuf[i]=0; return make_str(ibuf);
    }
    /* y.input_int(prompt?) — read line and parse as int */
    if(strcmp_u(name,"y.input_int")==0){
        int s=(argc>1)?1:0;
        if(argc>s){
            Val prompt=eval_node(args[s],env);
            if(prompt.type==YS_STR&&prompt.sval[0]){fputs(prompt.sval,stdout);fflush(stdout);}
        }
        static char ibuf2[64]; int i=0; char c=0;
        while(i<63){if(fread(&c,1,1,stdin)!=1)break;if(c=='\n'||c=='\r')break;ibuf2[i++]=c;}
        ibuf2[i]=0;
        /* parse integer (handles negative) */
        int64_t v=0; int neg=0; int j=0;
        if(ibuf2[j]=='-'){neg=1;j++;}
        for(;ibuf2[j]>='0'&&ibuf2[j]<='9';j++) v=v*10+(ibuf2[j]-'0');
        return make_int(neg?-v:v);
    }
    /* y.input_float(prompt?) — read line and parse as float */
    if(strcmp_u(name,"y.input_float")==0){
        int s=(argc>1)?1:0;
        if(argc>s){
            Val prompt=eval_node(args[s],env);
            if(prompt.type==YS_STR&&prompt.sval[0]){fputs(prompt.sval,stdout);fflush(stdout);}
        }
        static char ibuf3[64]; int i=0; char c=0;
        while(i<63){if(fread(&c,1,1,stdin)!=1)break;if(c=='\n'||c=='\r')break;ibuf3[i++]=c;}
        ibuf3[i]=0;
        return make_float(strtod(ibuf3,NULL));
    }
    /* y.len */
    if(strcmp_u(name,"y.len")==0||strcmp_u(name,"len")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==YS_ARR) return make_int(v.arr_len);
        return make_int(str_len_u(v.sval));
    }
    /* y.abs */
    if(strcmp_u(name,"y.abs")==0||strcmp_u(name,"abs")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==YS_FLOAT) return make_float(v.fval<0.0?-v.fval:v.fval);
        return make_int(v.ival<0?-v.ival:v.ival);
    }
    /* y.str — convert any value to string */
    if(strcmp_u(name,"y.str")==0||strcmp_u(name,"str")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        static char b[8192];
        if(v.type==YS_STR)  return v;
        if(v.type==YS_FLOAT){
            snprintf(b,sizeof(b),"%.6f",v.fval);
            int dot=-1,last=0;
            for(int i=0;b[i];i++){if(b[i]=='.')dot=i;last=i;}
            if(dot>=0){while(last>dot+1&&b[last]=='0')last--;}
            b[last+1]=0;
            return make_str(b);
        }
        if(v.type==YS_BOOL) return make_str(v.ival?"true":"false");
        if(v.type==YS_NIL)  return make_str("nil");
        if(v.type==YS_ARR){
            int bi=0; b[bi++]='[';
            for(int i=0;i<v.arr_len&&bi<250;i++){
                if(i>0){b[bi++]=',';b[bi++]=' ';}
                Val item=v.arr_data[i];
                char tmp[64];
                if(item.type==YS_STR){ snprintf(tmp,sizeof(tmp),"%.62s",item.sval); }
                else { int_to_str(val_int(item),tmp); }
                int tl=(int)strlen(tmp);
                if(bi+tl<250){memcpy(b+bi,tmp,tl);bi+=tl;}
            }
            if(bi<254) b[bi++]=']';
            b[bi]=0; return make_str(b);
        }
        /* int / anything else */
        int_to_str(val_int(v),b); return make_str(b);
    }
    /* y.int — parse string to int, or truncate float */
    if(strcmp_u(name,"y.int")==0||strcmp_u(name,"int")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==YS_STR){
            int neg=0; int j=0;
            if(v.sval[j]=='-'){neg=1;j++;}
            int64_t r=0;
            for(;v.sval[j]>='0'&&v.sval[j]<='9';j++) r=r*10+(v.sval[j]-'0');
            return make_int(neg?-r:r);
        }
        if(v.type==YS_FLOAT) return make_int((int64_t)v.fval);
        if(v.type==YS_BOOL)  return make_int(v.ival?1:0);
        return make_int(val_int(v));
    }
    /* y.float — parse string to float, or convert int */
    if(strcmp_u(name,"y.float")==0||strcmp_u(name,"float")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==YS_FLOAT) return v;
        if(v.type==YS_STR){
            return make_float(strtod(v.sval,NULL));
        }
        return make_float((double)val_int(v));
    }
    /* y.bool — parse string/int to bool */
    if(strcmp_u(name,"y.bool")==0||strcmp_u(name,"bool")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==YS_BOOL) return v;
        if(v.type==YS_STR){
            int b2=(strcmp_u(v.sval,"true")==0
                  ||strcmp_u(v.sval,"1")==0
                  ||strcmp_u(v.sval,"yes")==0);
            return make_bool(b2);
        }
        return make_bool(val_int(v)!=0);
    }
    /* y.substr(s, start, len) */
    if(strcmp_u(name,"y.substr")==0||strcmp_u(name,"substr")==0){
        int s0=(argc>3)?1:0;
        Val sv=eval_node(args[s0],env);
        int start=(int)val_int(eval_node(args[s0+1],env));
        int slen2=(int)val_int(eval_node(args[s0+2],env));
        int total=str_len_u(sv.sval);
        if(start<0) start=0;
        if(start>total) start=total;
        if(slen2<0) slen2=0;
        if(start+slen2>total) slen2=total-start;
        char buf[256]; int bi=0;
        for(int i=start;i<start+slen2&&bi<254;i++) buf[bi++]=sv.sval[i];
        buf[bi]=0;
        return make_str(buf);
    }
    /* y.contains(s, sub) */
    if(strcmp_u(name,"y.contains")==0||strcmp_u(name,"contains")==0){
        int s0=(argc>2)?1:0;
        Val sv=eval_node(args[s0],env);
        Val sub=eval_node(args[s0+1],env);
        int slen2=str_len_u(sv.sval), sublen=str_len_u(sub.sval);
        if(sublen==0) return make_bool(1);
        for(int i=0;i<=slen2-sublen;i++){
            int match=1;
            for(int j=0;j<sublen;j++) if(sv.sval[i+j]!=sub.sval[j]){match=0;break;}
            if(match) return make_bool(1);
        }
        return make_bool(0);
    }
    /* y.upper(s) */
    if(strcmp_u(name,"y.upper")==0||strcmp_u(name,"upper")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        char buf[256]; int i=0;
        while(sv.sval[i]&&i<254){
            char c=sv.sval[i];
            buf[i++]=(c>='a'&&c<='z')?(c-32):c;
        }
        buf[i]=0; return make_str(buf);
    }
    /* y.lower(s) */
    if(strcmp_u(name,"y.lower")==0||strcmp_u(name,"lower")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        char buf[256]; int i=0;
        while(sv.sval[i]&&i<254){
            char c=sv.sval[i];
            buf[i++]=(c>='A'&&c<='Z')?(c+32):c;
        }
        buf[i]=0; return make_str(buf);
    }
    /* y.trim(s) */
    if(strcmp_u(name,"y.trim")==0||strcmp_u(name,"trim")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        const char *p=sv.sval;
        while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
        int end=str_len_u(p);
        while(end>0&&(p[end-1]==' '||p[end-1]=='\t'||p[end-1]=='\n'||p[end-1]=='\r')) end--;
        char buf[256]; int i=0;
        while(i<end&&i<254){buf[i]=p[i];i++;}
        buf[i]=0; return make_str(buf);
    }
    /* y.split(s, sep) → array of strings */
    if(strcmp_u(name,"y.split")==0||strcmp_u(name,"split")==0){
        int s0=(argc>2)?1:0;
        Val sv=eval_node(args[s0],env);
        Val sep=eval_node(args[s0+1],env);
        int seplen=str_len_u(sep.sval);
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(32); result.arr_len=0;
        const char *cur2=sv.sval;
        if(seplen==0){
            result.arr_data[result.arr_len++]=make_str(sv.sval);
        } else {
            while(1){
                const char *found=0;
                int slen3=str_len_u(cur2);
                for(int i=0;i<=slen3-seplen;i++){
                    int m=1;
                    for(int j=0;j<seplen;j++) if(cur2[i+j]!=sep.sval[j]){m=0;break;}
                    if(m){found=cur2+i;break;}
                }
                if(!found){
                    if(result.arr_len<32) result.arr_data[result.arr_len++]=make_str(cur2);
                    break;
                }
                char chunk[256]; int ci=0;
                while(cur2+ci<found&&ci<254){chunk[ci]=cur2[ci];ci++;}
                chunk[ci]=0;
                if(result.arr_len<32) result.arr_data[result.arr_len++]=make_str(chunk);
                cur2=found+seplen;
            }
        }
        return result;
    }

    /* y.format("Hello {0}, age {1}", val1, val2) */
    if(strcmp_u(name,"y.format")==0||strcmp_u(name,"format")==0){
        int s0=(argc>1)?1:0;
        Val fmt=eval_node(args[s0],env);
        const char *f=fmt.sval;
        char buf[8192]; int bi=0;
        while(*f && bi<8190){
            if(*f=='{'){
                f++;
                /* parse index digit(s) */
                int idx=0;
                while(*f>='0'&&*f<='9'){idx=idx*10+(*f-'0');f++;}
                if(*f=='}') f++;
                int arg_idx=s0+1+idx;
                if(arg_idx<argc){
                    Val av=eval_node(args[arg_idx],env);
                    if(av.type==YS_INT){
                        int64_t v=av.ival; int neg=0;
                        if(v<0){neg=1;v=-v;}
                        char tb[32]; int ti=0;
                        do{tb[ti++]=(char)('0'+(v%10));v/=10;}while(v>0);
                        if(neg&&bi<8188) buf[bi++]='-';
                        while(ti>0&&bi<8188) buf[bi++]=tb[--ti];
                    } else if(av.type==YS_STR){
                        for(int i=0;av.sval[i]&&bi<8188;i++) buf[bi++]=av.sval[i];
                    } else if(av.type==YS_BOOL){
                        const char *bs=av.bval?"true":"false";
                        for(int i=0;bs[i]&&bi<8188;i++) buf[bi++]=bs[i];
                    } else if(av.type==YS_FLOAT){
                        char fb2[64];
                        snprintf(fb2,sizeof(fb2),"%.6f",av.fval);
                        int dot2=-1,last2=0;
                        for(int i=0;fb2[i];i++){if(fb2[i]=='.')dot2=i;last2=i;}
                        if(dot2>=0){while(last2>dot2+1&&fb2[last2]=='0')last2--;}
                        fb2[last2+1]=0;
                        for(int i=0;fb2[i]&&bi<8188;i++) buf[bi++]=fb2[i];
                    }
                }
            } else {
                buf[bi++]=*f++;
            }
        }
        buf[bi]=0;
        return make_str(buf);
    }

    /* y.map(arr, fn) → new array */
    if(strcmp_u(name,"y.map")==0||strcmp_u(name,"map")==0){
        int s0=(argc>2)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(arr.arr_len+1);
        result.arr_len=arr.arr_len;
        if(fn.type==YS_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                if(fd->argc>1) env_def(fe,fd->field_names[1],make_int(i));
                g_returning=0;
                int saved=envidx;
                Val r=eval_block(fd->body,fe);
                envidx=saved;
                if(g_returning){memcpy(&r,&g_return_val,sizeof(Val));g_returning=0;}
                result.arr_data[i]=r;
            }
        }
        return result;
    }
    /* y.filter(arr, fn) → filtered array */
    if(strcmp_u(name,"y.filter")==0||strcmp_u(name,"filter")==0){
        int s0=(argc>2)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(arr.arr_len+1);
        result.arr_len=0;
        if(fn.type==YS_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                if(fd->argc>1) env_def(fe,fd->field_names[1],make_int(i));
                g_returning=0;
                int saved=envidx;
                Val r=eval_block(fd->body,fe);
                envidx=saved;
                if(g_returning){memcpy(&r,&g_return_val,sizeof(Val));g_returning=0;}
                if(val_bool(r)) result.arr_data[result.arr_len++]=arr.arr_data[i];
            }
        }
        return result;
    }
    /* y.reduce(arr, fn, init) → single value */
    if(strcmp_u(name,"y.reduce")==0||strcmp_u(name,"reduce")==0){
        int s0=(argc>3)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        Val acc=eval_node(args[s0+2],env);
        if(fn.type==YS_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],acc);
                if(fd->argc>1) env_def(fe,fd->field_names[1],arr.arr_data[i]);
                if(fd->argc>2) env_def(fe,fd->field_names[2],make_int(i));
                g_returning=0;
                int saved=envidx;
                Val r=eval_block(fd->body,fe);
                envidx=saved;
                if(g_returning){memcpy(&r,&g_return_val,sizeof(Val));g_returning=0;}
                acc=r;
            }
        }
        return acc;
    }
    /* y.each(arr, fn) → run fn for side effects */
    if(strcmp_u(name,"y.each")==0||strcmp_u(name,"each")==0){
        int s0=(argc>2)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        if(fn.type==YS_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                if(fd->argc>1) env_def(fe,fd->field_names[1],make_int(i));
                g_returning=0;
                int saved=envidx;
                eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
            }
        }
        return make_nil();
    }
    /* y.sort(arr) or y.sort(arr, fn_comparator) → sorted copy */
    if(strcmp_u(name,"y.sort")==0||strcmp_u(name,"sort")==0||strcmp_u(name,"y.array.sort")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        if(arr.type!=YS_ARR||arr.arr_len==0) return arr;
        /* copy into static scratch (safe from stack reuse) */
        Val *sort_scratch=alloc_arr(arr.arr_len+1);
        int slen=arr.arr_len;
        for(int i=0;i<slen;i++) sort_scratch[i]=arr.arr_data[i];
        /* optional comparator fn */
        Val cmp_fn=make_nil();
        if(argc>s0+1) cmp_fn=eval_node(args[s0+1],env);
        /* insertion sort */
        for(int i=1;i<slen;i++){
            Val key=sort_scratch[i]; int j=i-1;
            while(j>=0){
                int should_swap=0;
                if(cmp_fn.type==YS_FN&&cmp_fn.fn_node){
                    Node *fd=cmp_fn.fn_node;
                    Env *ce=(cmp_fn.fn_env)?((Env*)cmp_fn.fn_env):env;
                    Env *fe=env_new(ce);
                    if(fd->argc>0) env_def(fe,fd->field_names[0],key);
                    if(fd->argc>1) env_def(fe,fd->field_names[1],sort_scratch[j]);
                    g_returning=0;
                    int saved=envidx;
                    Val r=eval_block(fd->body,fe);
                    envidx=saved;
                    if(g_returning){memcpy(&r,&g_return_val,sizeof(Val));g_returning=0;}
                    should_swap=val_bool(r);
                } else {
                    if(key.type==YS_STR&&sort_scratch[j].type==YS_STR)
                        should_swap=(strcmp(key.sval,sort_scratch[j].sval)<0);
                    else
                        should_swap=(val_int(key)<val_int(sort_scratch[j]));
                }
                if(!should_swap) break;
                sort_scratch[j+1]=sort_scratch[j]; j--;
            }
            sort_scratch[j+1]=key;
        }
        /* copy result via static out to avoid stack Val corruption */
        static Val sort_out;
        sort_out=make_nil(); sort_out.type=YS_ARR;
        sort_out.arr_data=alloc_arr(slen+1);
        sort_out.arr_len=slen;
        for(int i=0;i<slen;i++) sort_out.arr_data[i]=sort_scratch[i];
        return sort_out;
    }
    /* y.range(n) or y.range(start, end) or y.range(start, end, step) → array */
    if(strcmp_u(name,"y.range")==0||strcmp_u(name,"range")==0){
        int s0=(argc>1)?1:0;
        int64_t start=0,end2=0,step=1;
        if(argc-s0==1){
            end2=val_int(eval_node(args[s0],env));
        } else if(argc-s0==2){
            start=val_int(eval_node(args[s0],env));
            end2 =val_int(eval_node(args[s0+1],env));
        } else if(argc-s0>=3){
            start=val_int(eval_node(args[s0],env));
            end2 =val_int(eval_node(args[s0+1],env));
            step =val_int(eval_node(args[s0+2],env));
        }
        if(step==0) step=1;
        /* count elements */
        int64_t cnt2=0;
        if(step>0) cnt2=(end2>start)?(end2-start+step-1)/step:0;
        else       cnt2=(start>end2)?(start-end2-step-1)/(-step):0;
        /* no cap on range size */
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr((int)cnt2+1);
        result.arr_len=(int)cnt2;
        int64_t v2=start;
        for(int i=0;i<(int)cnt2;i++,v2+=step) result.arr_data[i]=make_int(v2);
        return result;
    }
    /* y.zip(arr1, arr2) → array of Pair{first, second} structs */
    if(strcmp_u(name,"y.zip")==0||strcmp_u(name,"zip")==0){
        int s0=(argc>2)?1:0;
        Val a1=eval_node(args[s0],env);
        Val a2=eval_node(args[s0+1],env);
        int len=(a1.arr_len<a2.arr_len)?a1.arr_len:a2.arr_len;
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(len+1);
        result.arr_len=len;
        static char pair_names[2][32]={"first","second"};
        for(int i=0;i<len;i++){
            Val pair; memset(&pair,0,sizeof(Val));
            pair.type=YS_STRUCT;
            strcpy(pair.struct_name,"Pair");
            pair.field_count=2;
            pair.field_names=pair_names;
            pair.field_vals=alloc_arr(2);
            memcpy(&pair.field_vals[0],&a1.arr_data[i],sizeof(Val));
            memcpy(&pair.field_vals[1],&a2.arr_data[i],sizeof(Val));
            result.arr_data[i]=pair;
        }
        return result;
    }
    /* y.flatten(arr_of_arrs) → flat array */
    if(strcmp_u(name,"y.flatten")==0||strcmp_u(name,"flatten")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(arr.arr_len*4+1);
        result.arr_len=0;
        for(int i=0;i<arr.arr_len&&result.arr_len<64;i++){
            Val inner=arr.arr_data[i];
            if(inner.type==YS_ARR){
                for(int j=0;j<inner.arr_len&&result.arr_len<64;j++)
                    result.arr_data[result.arr_len++]=inner.arr_data[j];
            } else {
                result.arr_data[result.arr_len++]=inner;
            }
        }
        return result;
    }
    /* y.sum(arr) → sum of elements */
    if(strcmp_u(name,"y.sum")==0||strcmp_u(name,"sum")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        int use_f=0;
        for(int i=0;i<arr.arr_len;i++) if(arr.arr_data[i].type==YS_FLOAT) use_f=1;
        if(use_f){
            double s=0.0; for(int i=0;i<arr.arr_len;i++) s+=val_float(arr.arr_data[i]);
            return make_float(s);
        }
        int64_t s=0; for(int i=0;i<arr.arr_len;i++) s+=val_int(arr.arr_data[i]);
        return make_int(s);
    }
    /* y.min(arr) / y.max(arr) → min or max element */
    if(strcmp_u(name,"y.min_arr")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        if(arr.arr_len==0) return make_nil();
        Val m=arr.arr_data[0];
        for(int i=1;i<arr.arr_len;i++) if(val_int(arr.arr_data[i])<val_int(m)) m=arr.arr_data[i];
        return m;
    }
    if(strcmp_u(name,"y.max_arr")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        if(arr.arr_len==0) return make_nil();
        Val m=arr.arr_data[0];
        for(int i=1;i<arr.arr_len;i++) if(val_int(arr.arr_data[i])>val_int(m)) m=arr.arr_data[i];
        return m;
    }

    /* y.typeof(val) → "int","float","str","bool","array","struct","fn","err","nil" */
    if(strcmp_u(name,"y.typeof")==0||strcmp_u(name,"typeof")==0){
        int s0=(argc>1)?1:0;
        Val v=eval_node(args[s0],env);
        const char *t="nil";
        if(v.type==YS_INT)    t="int";
        else if(v.type==YS_FLOAT)  t="float";
        else if(v.type==YS_STR)    t="str";
        else if(v.type==YS_BOOL)   t="bool";
        else if(v.type==YS_ARR)    t="array";
        else if(v.type==YS_STRUCT) t="struct";
        else if(v.type==YS_FN)     t="fn";
        else if(v.type==YS_CAP)    t="cap";
        else if(v.type==YS_ERR)    t="err";
        return make_str(t);
    }
    /* y.is_int / y.is_str / y.is_float / y.is_bool / y.is_array / y.is_fn / y.is_nil */
    if(strcmp_u(name,"y.is_int")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_INT); }
    if(strcmp_u(name,"y.is_float")==0){ int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_FLOAT); }
    if(strcmp_u(name,"y.is_str")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_STR); }
    if(strcmp_u(name,"y.is_bool")==0) { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_BOOL); }
    if(strcmp_u(name,"y.is_array")==0){ int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_ARR); }
    if(strcmp_u(name,"y.is_fn")==0)   { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_FN); }
    if(strcmp_u(name,"y.is_nil")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_NIL); }
    if(strcmp_u(name,"y.is_err")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==YS_ERR); }

    /* y.error(message, code) → Error struct */
    if(strcmp_u(name,"y.error")==0||strcmp_u(name,"error")==0){
        int s0=(argc>1)?1:0;
        Val msg=eval_node(args[s0],env);
        Val code=make_int(0);
        if(argc>s0+1) code=eval_node(args[s0+1],env);
        /* build Error struct */
        Val v=make_nil(); v.type=YS_STRUCT;
        v.struct_name[0]='E';v.struct_name[1]='r';v.struct_name[2]='r';
        v.struct_name[3]='o';v.struct_name[4]='r';v.struct_name[5]=0;
        v.field_vals=alloc_fld(2); v.field_names=alloc_nm(2); v.field_count=2;
        /* field 0: message */
        const char *f0="message";
        for(int i=0;f0[i];i++) { v.field_names[0][i]=f0[i]; } v.field_names[0][7]=0;
        v.field_vals[0]=msg;
        /* field 1: code */
        const char *f1="code";
        for(int i=0;f1[i];i++) { v.field_names[1][i]=f1[i]; } v.field_names[1][4]=0;
        v.field_vals[1]=code;
        return v;
    }

    /*  y.math  */
    if(strcmp_u(name,"y.math.sqrt")==0){
        int s0=(argc>1)?1:0; Val v=eval_node(args[s0],env);
        double d=val_float(v);
        if(d<0.0){ys_error(0,0,"sqrt of negative");return make_nil();}
        if(d==0.0) return v.type==YS_FLOAT?make_float(0.0):make_int(0);
        double x=d,y2=1.0;
        for(int _i=0;_i<60&&(x-y2)>0.000001;_i++){x=(x+y2)/2.0;y2=d/x;}
        if(v.type!=YS_FLOAT){int64_t ir=(int64_t)(x+0.5);return make_int(ir);}
        return make_float(x);
    }
    if(strcmp_u(name,"y.math.pow")==0){
        int s0=(argc>1)?1:0;
        Val bv=eval_node(args[s0],env);
        Val ev=eval_node(args[s0+1],env);
        if(bv.type==YS_FLOAT||ev.type==YS_FLOAT){
            double res=1.0,base2=val_float(bv); int64_t e2=val_int(ev);
            for(int64_t i=0;i<(e2<0?-e2:e2);i++) res*=base2;
            if(e2<0) res=1.0/res;
            return make_float(res);
        }
        int64_t base=val_int(bv),exp2=val_int(ev),result=1;
        for(int64_t i=0;i<exp2;i++) result*=base;
        return make_int(result);
    }
    if(strcmp_u(name,"y.math.abs")==0){
        int s0=(argc>1)?1:0; Val v=eval_node(args[s0],env);
        if(v.type==YS_FLOAT) return make_float(v.fval<0.0?-v.fval:v.fval);
        int64_t iv=val_int(v); return make_int(iv<0?-iv:iv);
    }
    if(strcmp_u(name,"y.math.min")==0){
        int s0=(argc>1)?1:0;
        Val av=eval_node(args[s0],env);
        Val bv=eval_node(args[s0+1],env);
        if(av.type==YS_FLOAT||bv.type==YS_FLOAT)
            return val_float(av)<val_float(bv)?av:bv;
        return make_int(val_int(av)<val_int(bv)?val_int(av):val_int(bv));
    }
    if(strcmp_u(name,"y.math.max")==0){
        int s0=(argc>1)?1:0;
        Val av=eval_node(args[s0],env);
        Val bv=eval_node(args[s0+1],env);
        if(av.type==YS_FLOAT||bv.type==YS_FLOAT)
            return val_float(av)>val_float(bv)?av:bv;
        return make_int(val_int(av)>val_int(bv)?val_int(av):val_int(bv));
    }
    if(strcmp_u(name,"y.math.clamp")==0){
        int s0=(argc>1)?1:0;
        Val v=eval_node(args[s0],env);
        Val lv=eval_node(args[s0+1],env);
        Val hv=eval_node(args[s0+2],env);
        if(v.type==YS_FLOAT||lv.type==YS_FLOAT||hv.type==YS_FLOAT){
            double d=val_float(v),lo=val_float(lv),hi=val_float(hv);
            return make_float(d<lo?lo:d>hi?hi:d);
        }
        int64_t iv=val_int(v),lo=val_int(lv),hi=val_int(hv);
        return make_int(iv<lo?lo:iv>hi?hi:iv);
    }
    if(strcmp_u(name,"y.math.floor")==0){
        int s0=(argc>1)?1:0; Val v=eval_node(args[s0],env);
        if(v.type==YS_FLOAT){
            double d=v.fval; int64_t i=(int64_t)d;
            return make_int(d<0.0&&d!=(double)i?i-1:i);
        }
        return make_int(v.ival);
    }
    if(strcmp_u(name,"y.math.ceil")==0){
        int s0=(argc>1)?1:0; Val v=eval_node(args[s0],env);
        if(v.type==YS_FLOAT){
            double d=v.fval; int64_t i=(int64_t)d;
            return make_int(d>0.0&&d!=(double)i?i+1:i);
        }
        return make_int(v.ival);
    }
    if(strcmp_u(name,"y.math.round")==0){
        int s0=(argc>1)?1:0; Val v=eval_node(args[s0],env);
        if(v.type==YS_FLOAT) return make_int((int64_t)(v.fval>=0.0?v.fval+0.5:v.fval-0.5));
        return make_int(v.ival);
    }
    if(strcmp_u(name,"y.math.sign")==0){
        int s0=(argc>1)?1:0; int64_t v=val_int(eval_node(args[s0],env));
        return make_int(v>0?1:v<0?-1:0);
    }

    /*  y.string  */
    if(strcmp_u(name,"y.string.repeat")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        int n2=(int)val_int(eval_node(args[s0+1],env));
        char buf[512]; int bi=0, slen=str_len_u(sv.sval);
        for(int i=0;i<n2&&bi<8188;i++)
            for(int j=0;j<slen&&bi<8188;j++) buf[bi++]=sv.sval[j];
        buf[bi]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.string.starts_with")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        Val pv=eval_node(args[s0+1],env);
        int pl=str_len_u(pv.sval);
        int match=1; for(int i=0;i<pl;i++) if(sv.sval[i]!=pv.sval[i]){match=0;break;}
        return make_bool(match);
    }
    if(strcmp_u(name,"y.string.ends_with")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        Val pv=eval_node(args[s0+1],env);
        int sl=str_len_u(sv.sval), pl=str_len_u(pv.sval);
        if(pl>sl) return make_bool(0);
        int match=1;
        for(int i=0;i<pl;i++) if(sv.sval[sl-pl+i]!=pv.sval[i]){match=0;break;}
        return make_bool(match);
    }
    if(strcmp_u(name,"y.string.replace")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        Val from=eval_node(args[s0+1],env);
        Val to=eval_node(args[s0+2],env);
        int fl=str_len_u(from.sval), tl=str_len_u(to.sval);
        char buf[512]; int bi=0, si=0, slen=str_len_u(sv.sval);
        while(si<slen&&bi<8188){
            int match=(fl>0);
            for(int i=0;i<fl&&match;i++) if(sv.sval[si+i]!=from.sval[i]) match=0;
            if(match&&fl>0){
                for(int i=0;i<tl&&bi<8188;i++) buf[bi++]=to.sval[i];
                si+=fl;
            } else { buf[bi++]=sv.sval[si++]; }
        }
        buf[bi]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.string.pad_left")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        int width=(int)val_int(eval_node(args[s0+1],env));
        int sl=str_len_u(sv.sval);
        char buf[256]; int bi=0;
        for(int i=sl;i<width&&bi<254;i++) buf[bi++]=' ';
        for(int i=0;sv.sval[i]&&bi<254;i++) buf[bi++]=sv.sval[i];
        buf[bi]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.string.pad_right")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        int width=(int)val_int(eval_node(args[s0+1],env));
        int sl=str_len_u(sv.sval);
        char buf[256]; int bi=0;
        for(int i=0;sv.sval[i]&&bi<254;i++) buf[bi++]=sv.sval[i];
        for(int i=sl;i<width&&bi<254;i++) buf[bi++]=' ';
        buf[bi]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.string.reverse")==0){
        int s0=(argc>1)?1:0;
        Val sv=eval_node(args[s0],env);
        int sl=str_len_u(sv.sval);
        char buf[256]; 
        for(int i=0;i<sl&&i<254;i++) buf[i]=sv.sval[sl-1-i];
        buf[sl<254?sl:254]=0; return make_str(buf);
    }

    /*  y.array  */
    if(strcmp_u(name,"y.array.reverse")==0){
        int s0=(argc>1)?1:0; Val arr=eval_node(args[s0],env);
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(arr.arr_len+1); result.arr_len=arr.arr_len;
        for(int i=0;i<arr.arr_len;i++) result.arr_data[i]=arr.arr_data[arr.arr_len-1-i];
        return result;
    }
    if(strcmp_u(name,"y.array.join")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        Val sep=make_str(""); if(argc>s0+1) sep=eval_node(args[s0+1],env);
        char buf[8192]; int bi=0;
        for(int i=0;i<arr.arr_len;i++){
            /* print element */
            Val el=arr.arr_data[i];
            if(el.type==YS_STR){ int j=0; while(el.sval[j]&&bi<8188) buf[bi++]=el.sval[j++]; }
            else if(el.type==YS_INT){
                int64_t v=el.ival; int neg=v<0; if(neg)v=-v;
                char tb[24]; int ti=0;
                do{tb[ti++]=(char)('0'+(v%10));v/=10;}while(v>0);
                if(neg&&bi<8188) buf[bi++]='-';
                while(ti>0&&bi<8188) buf[bi++]=tb[--ti];
            }
            if(i<arr.arr_len-1){ int j=0; while(sep.sval[j]&&bi<8188) buf[bi++]=sep.sval[j++]; }
        }
        buf[bi]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.array.slice")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        int start=(int)val_int(eval_node(args[s0+1],env));
        int end2=(argc>s0+2)?(int)val_int(eval_node(args[s0+2],env)):arr.arr_len;
        if(start<0){start=0;} if(end2>arr.arr_len){end2=arr.arr_len;}
        Val result=make_nil(); result.type=YS_ARR;
        int len=end2-start; if(len<0)len=0;
        result.arr_data=alloc_arr(len+1); result.arr_len=len;
        for(int i=0;i<len;i++) result.arr_data[i]=arr.arr_data[start+i];
        return result;
    }
    if(strcmp_u(name,"y.array.find")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn=eval_node(args[s0+1],env);
        if(fn.type==YS_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                g_returning=0; int saved=envidx;
                Val r=eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
                if(val_bool(r)) return arr.arr_data[i];
            }
        }
        return make_nil();
    }
    if(strcmp_u(name,"y.array.index_of")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        Val target=eval_node(args[s0+1],env);
        for(int i=0;i<arr.arr_len;i++){
            Val el=arr.arr_data[i];
            if(el.type==target.type){
                if(el.type==YS_INT&&el.ival==target.ival) return make_int(i);
                if(el.type==YS_STR&&strcmp_u(el.sval,target.sval)==0) return make_int(i);
                if(el.type==YS_BOOL&&el.bval==target.bval) return make_int(i);
            }
        }
        return make_int(-1);
    }
    if(strcmp_u(name,"y.array.contains")==0){
        int s0=(argc>1)?1:0;
        Val arr=eval_node(args[s0],env);
        Val target=eval_node(args[s0+1],env);
        for(int i=0;i<arr.arr_len;i++){
            Val el=arr.arr_data[i];
            if(el.type==target.type){
                if(el.type==YS_INT&&el.ival==target.ival) return make_bool(1);
                if(el.type==YS_STR&&strcmp_u(el.sval,target.sval)==0) return make_bool(1);
            }
        }
        return make_bool(0);
    }
    /* y.array.sort → redirect to y.sort (handled above) */

    /* y.push(arr, val) */
    if(strcmp_u(name,"y.push")==0||strcmp_u(name,"push")==0){
        int s=(argc>2)?1:0;
        Val arr=eval_node(args[s],env);
        Val val=eval_node(args[s+1],env);
        if(arr.type!=YS_ARR) return make_nil();
        Val *newdata=alloc_arr(arr.arr_len+1);
        for(int i=0;i<arr.arr_len;i++) newdata[i]=arr.arr_data[i];
        newdata[arr.arr_len]=val;
        arr.arr_data=newdata; arr.arr_len++;
        if(args[s]->kind==ND_IDENT) env_set(env,args[s]->name,arr);
        return arr;
    }
    /* y.pop(arr) */
    if(strcmp_u(name,"y.pop")==0||strcmp_u(name,"pop")==0){
        int s=(argc>1)?1:0;
        Val arr=eval_node(args[s],env);
        if(arr.type!=YS_ARR||arr.arr_len==0) return make_nil();
        Val last=arr.arr_data[arr.arr_len-1];
        arr.arr_len--;
        if(args[s]->kind==ND_IDENT) env_set(env,args[s]->name,arr);
        return last;
    }
    /* y.exit */
    if(strcmp_u(name,"y.exit")==0||strcmp_u(name,"exit")==0){
        int s=(argc>1)?1:0;
        int code=argc>s?(int)val_int(eval_node(args[s],env)):0;
        exit(code);
    }

    /*  Capability  */
    if(strcmp_u(name,"cap.open")==0||strcmp_u(name,"open")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        int perm=CAP_READ;
        if(argc>s+1) perm=(int)val_int(eval_node(args[s+1],env));
        const char *mode=(perm&CAP_WRITE)?"w":"r";
        FILE *fp=fopen(path_v.sval,mode);
        if(!fp){puts("[cap] open failed: ");puts(path_v.sval);puts("\n");return make_int(-1);}
        return make_cap(path_v.sval,perm,(int64_t)(uintptr_t)fp);
    }
    if(strcmp_u(name,"cap.read")==0||strcmp_u(name,"read")==0){
        int s=(argc>1)?1:0;
        Val cap=eval_node(args[s],env);
        if(cap.type!=YS_CAP) return make_str("");
        FILE *fp=(FILE*)(uintptr_t)cap.cap_fd;
        static char rbuf[4096]; int n=(int)fread(rbuf,1,4095,fp);
        if(n<0) { n=0; } rbuf[n]=0; return make_str(rbuf);
    }
    if(strcmp_u(name,"cap.write")==0||strcmp_u(name,"write")==0){
        int s=(argc>2)?1:0;
        Val cap=eval_node(args[s],env);
        Val dat=eval_node(args[s+1],env);
        if(cap.type!=YS_CAP) return make_int(-1);
        FILE *fp=(FILE*)(uintptr_t)cap.cap_fd;
        int n=(int)fwrite(dat.sval,1,str_len_u(dat.sval),fp);
        fflush(fp); return make_int(n);
    }
    if(strcmp_u(name,"cap.close")==0||strcmp_u(name,"close")==0){
        int s=(argc>1)?1:0;
        Val cap=eval_node(args[s],env);
        if(cap.type==YS_CAP&&cap.cap_fd) fclose((FILE*)(uintptr_t)cap.cap_fd);
        return make_nil();
    }
    if(strcmp_u(name,"cap.perm")==0||strcmp_u(name,"perm")==0){
        int s=(argc>1)?1:0;
        Val cap=eval_node(args[s],env);
        return make_int(cap.type==YS_CAP?cap.cap_perm:0);
    }


    /* y.fs.read(path) → string */
    if(strcmp_u(name,"y.fs.read")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        FILE *fp=fopen(path_v.sval,"rb");
        if(!fp){ g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"y.fs.read: cannot open '%.100s'",path_v.sval); return make_nil(); }
        static char fbuf[8192];
        int n=(int)fread(fbuf,1,8191,fp);
        if(n<0) n=0;
        fbuf[n]=0; fclose(fp);
        return make_str(fbuf);
    }

    /* y.fs.write(path, data) → int (bytes written) */
    if(strcmp_u(name,"y.fs.write")==0){
        int s=(argc>2)?1:0;
        Val path_v=eval_node(args[s],env);
        Val data_v=eval_node(args[s+1],env);
        if(g_throwing) return make_nil();
        FILE *fp=fopen(path_v.sval,"wb");
        if(!fp){ g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"y.fs.write: cannot open '%.100s'",path_v.sval); return make_nil(); }
        int n=(int)fwrite(data_v.sval,1,str_len_u(data_v.sval),fp);
        fclose(fp); return make_int(n);
    }

    /* y.fs.append(path, data) → int */
    if(strcmp_u(name,"y.fs.append")==0){
        int s=(argc>2)?1:0;
        Val path_v=eval_node(args[s],env);
        Val data_v=eval_node(args[s+1],env);
        if(g_throwing) return make_nil();
        FILE *fp=fopen(path_v.sval,"ab");
        if(!fp) return make_int(-1);
        int n=(int)fwrite(data_v.sval,1,str_len_u(data_v.sval),fp);
        fclose(fp); return make_int(n);
    }

    /* y.fs.exists(path) → bool */
    if(strcmp_u(name,"y.fs.exists")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        struct stat st; return make_bool(stat(path_v.sval,&st)==0);
#else
        return make_bool(_access(path_v.sval,0)==0);
#endif
    }

    /* y.fs.list(dir) → array of strings */
    if(strcmp_u(name,"y.fs.list")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(256); result.arr_len=0;
#ifndef _WIN32
        DIR *d=opendir(path_v.sval);
        if(!d) return result;
        struct dirent *entry;
        while((entry=readdir(d))!=NULL && result.arr_len<255){
            if(strcmp(entry->d_name,".")==0||strcmp(entry->d_name,"..")==0) continue;
            result.arr_data[result.arr_len++]=make_str(entry->d_name);
        }
        closedir(d);
#else
        WIN32_FIND_DATA fd; char pattern[512];
        snprintf(pattern,sizeof(pattern),"%s\\*",path_v.sval);
        HANDLE h=FindFirstFile(pattern,&fd);
        if(h==INVALID_HANDLE_VALUE) return result;
        do {
            if(strcmp(fd.cFileName,".")==0||strcmp(fd.cFileName,"..")==0) continue;
            if(result.arr_len<255) result.arr_data[result.arr_len++]=make_str(fd.cFileName);
        } while(FindNextFile(h,&fd));
        FindClose(h);
#endif
        return result;
    }

    /* y.fs.mkdir(path) → bool */
    if(strcmp_u(name,"y.fs.mkdir")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        return make_bool(mkdir(path_v.sval,0755)==0);
#else
        return make_bool(_mkdir(path_v.sval)==0);
#endif
    }

    /* y.fs.delete(path) → bool */
    if(strcmp_u(name,"y.fs.delete")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        return make_bool(remove(path_v.sval)==0);
    }

    /* y.fs.rename(old, new) → bool */
    if(strcmp_u(name,"y.fs.rename")==0){
        int s=(argc>2)?1:0;
        Val old_v=eval_node(args[s],env);
        Val new_v=eval_node(args[s+1],env);
        if(g_throwing) return make_nil();
        return make_bool(rename(old_v.sval,new_v.sval)==0);
    }

    /* y.fs.size(path) → int (bytes, -1 on error) */
    if(strcmp_u(name,"y.fs.size")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        struct stat st;
        if(stat(path_v.sval,&st)!=0) return make_int(-1);
        return make_int((int64_t)st.st_size);
#else
        HANDLE h=CreateFile(path_v.sval,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if(h==INVALID_HANDLE_VALUE) return make_int(-1);
        LARGE_INTEGER sz; GetFileSizeEx(h,&sz); CloseHandle(h);
        return make_int((int64_t)sz.QuadPart);
#endif
    }

    /* y.fs.is_dir(path) → bool */
    if(strcmp_u(name,"y.fs.is_dir")==0){
        int s=(argc>1)?1:0;
        Val path_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        struct stat st;
        if(stat(path_v.sval,&st)!=0) return make_bool(0);
        return make_bool(S_ISDIR(st.st_mode));
#else
        DWORD attr=GetFileAttributes(path_v.sval);
        return make_bool(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY));
#endif
    }

/* ================================================================
   v1.3  —  Process & System  (process.* / sys.*)
   ================================================================ */

    /* process.spawn(cmd) → string (stdout captured) */
    if(strcmp_u(name,"process.spawn")==0){
        int s=(argc>1)?1:0;
        Val cmd_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        static char pbuf[8192]; int pn=0;
#ifndef _WIN32
        FILE *pp=popen(cmd_v.sval,"r");
#else
        FILE *pp=_popen(cmd_v.sval,"r");
#endif
        if(!pp){ g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"process.spawn: failed to run '%.100s'",cmd_v.sval); return make_nil(); }
        int c;
        while(pn<8190 && (c=fgetc(pp))!=EOF) pbuf[pn++]=(char)c;
        pbuf[pn]=0;
#ifndef _WIN32
        pclose(pp);
#else
        _pclose(pp);
#endif
        return make_str(pbuf);
    }

    /* process.spawn_code(cmd) → int (exit code) */
    if(strcmp_u(name,"process.spawn_code")==0){
        int s=(argc>1)?1:0;
        Val cmd_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        int code=system(cmd_v.sval);
        return make_int(code);
    }

    /* process.env(key) → string or nil */
    if(strcmp_u(name,"process.env")==0){
        int s=(argc>1)?1:0;
        Val key_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        const char *val_ptr=getenv(key_v.sval);
        return val_ptr ? make_str(val_ptr) : make_nil();
    }

    /* process.pid() → int */
    if(strcmp_u(name,"process.pid")==0){
#ifndef _WIN32
        return make_int((int64_t)getpid());
#else
        return make_int((int64_t)GetCurrentProcessId());
#endif
    }

    /* sys.exit(code?) */
    if(strcmp_u(name,"sys.exit")==0){
        int s=(argc>1)?1:0;
        int code=argc>s?(int)val_int(eval_node(args[s],env)):0;
        exit(code);
    }

    /* sys.platform() → "linux" | "windows" | "macos" */
    if(strcmp_u(name,"sys.platform")==0){
#if defined(_WIN32)
        return make_str("windows");
#elif defined(__APPLE__)
        return make_str("macos");
#else
        return make_str("linux");
#endif
    }

/* 
   v1.7  —  y.time.*
*/

    /* y.time.now() → int (milliseconds since epoch) */
    if(strcmp_u(name,"y.time.now")==0){
#ifndef _WIN32
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);
        return make_int((int64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000);
#else
        FILETIME ft; GetSystemTimeAsFileTime(&ft);
        int64_t t=((int64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime;
        /* convert from 100ns intervals since 1601 to ms since 1970 */
        return make_int(t/10000 - 11644473600000LL);
#endif
    }

    /* y.time.sleep(ms) → nil */
    if(strcmp_u(name,"y.time.sleep")==0){
        int s=(argc>1)?1:0;
        int64_t ms=val_int(eval_node(args[s],env));
        if(g_throwing) return make_nil();
#ifndef _WIN32
        { struct timespec _ts; _ts.tv_sec=ms/1000; _ts.tv_nsec=(ms%1000)*1000000L; nanosleep(&_ts,NULL); }
#else
        Sleep((DWORD)ms);
#endif
        return make_nil();
    }

    /* y.time.format(ms, fmt?) → string */
    if(strcmp_u(name,"y.time.format")==0){
        int s=(argc>1)?1:0;
        int64_t ms=val_int(eval_node(args[s],env));
        if(g_throwing) return make_nil();
        const char *fmt="%Y-%m-%d %H:%M:%S";
        if(argc>s+1){ Val fv=eval_node(args[s+1],env); if(fv.type==YS_STR) fmt=fv.sval; }
        time_t t=(time_t)(ms/1000);
        struct tm *tm_info=localtime(&t);
        static char tbuf[128];
        strftime(tbuf,sizeof(tbuf),fmt,tm_info);
        return make_str(tbuf);
    }

    /* y.time.unix() → int (seconds since epoch) */
    if(strcmp_u(name,"y.time.unix")==0){
        return make_int((int64_t)time(NULL));
    }

/* 
   v1.7  —  y.env.*
*/

    /* y.env.get(key) → string or nil */
    if(strcmp_u(name,"y.env.get")==0){
        int s=(argc>1)?1:0;
        Val k=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        const char *v=getenv(k.sval);
        return v ? make_str(v) : make_nil();
    }

    /* y.env.set(key, val) → nil */
    if(strcmp_u(name,"y.env.set")==0){
        int s=(argc>2)?1:0;
        Val k=eval_node(args[s],env);
        Val v=eval_node(args[s+1],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        setenv(k.sval,v.sval,1);
#else
        _putenv_s(k.sval,v.sval);
#endif
        return make_nil();
    }

    /* y.env.unset(key) → nil */
    if(strcmp_u(name,"y.env.unset")==0){
        int s=(argc>1)?1:0;
        Val k=eval_node(args[s],env);
        if(g_throwing) return make_nil();
#ifndef _WIN32
        unsetenv(k.sval);
#else
        _putenv_s(k.sval,"");
#endif
        return make_nil();
    }

/* 
   v1.7  —  y.path.*
*/

    /* y.path.join(a, b, ...) → string */
    if(strcmp_u(name,"y.path.join")==0){
        int s=(argc>1)?1:0;
        static char pb[512]; int pi=0;
        for(int i=s;i<argc&&pi<508;i++){
            Val seg=eval_node(args[i],env);
            if(g_throwing) return make_nil();
            /* add separator if needed */
            if(pi>0 && pb[pi-1]!='/' && pb[pi-1]!='\\') pb[pi++]='/';
            for(int j=0;seg.sval[j]&&pi<508;j++) pb[pi++]=seg.sval[j];
        }
        pb[pi]=0;
        return make_str(pb);
    }

    /* y.path.basename(path) → string  e.g. "/a/b/file.y" → "file.y" */
    if(strcmp_u(name,"y.path.basename")==0){
        int s=(argc>1)?1:0;
        Val p=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        int len=str_len_u(p.sval), last=-1;
        for(int i=0;i<len;i++) if(p.sval[i]=='/'||p.sval[i]=='\\') last=i;
        return make_str(p.sval+last+1);
    }

    /* y.path.dirname(path) → string  e.g. "/a/b/file.y" → "/a/b" */
    if(strcmp_u(name,"y.path.dirname")==0){
        int s=(argc>1)?1:0;
        Val p=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        int len=str_len_u(p.sval), last=-1;
        for(int i=0;i<len;i++) if(p.sval[i]=='/'||p.sval[i]=='\\') last=i;
        if(last<0) return make_str(".");
        static char db[512]; int di=0;
        for(;di<last&&di<510;di++) db[di]=p.sval[di];
        db[di]=0; return make_str(db);
    }

    /* y.path.ext(path) → string  e.g. "file.y" → ".y" */
    if(strcmp_u(name,"y.path.ext")==0){
        int s=(argc>1)?1:0;
        Val p=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        int len=str_len_u(p.sval), last=-1;
        for(int i=0;i<len;i++) if(p.sval[i]=='.') last=i;
        if(last<0) return make_str("");
        return make_str(p.sval+last);
    }

    /* y.path.stem(path) → string  e.g. "file.y" → "file" */
    if(strcmp_u(name,"y.path.stem")==0){
        int s=(argc>1)?1:0;
        Val p=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        int len=str_len_u(p.sval);
        int last_slash=-1, last_dot=-1;
        for(int i=0;i<len;i++){
            if(p.sval[i]=='/'||p.sval[i]=='\\') last_slash=i;
            if(p.sval[i]=='.') last_dot=i;
        }
        int start=last_slash+1;
        int end=(last_dot>start)?last_dot:len;
        static char stb[256]; int si=0;
        for(int i=start;i<end&&si<254;i++) stb[si++]=p.sval[i];
        stb[si]=0; return make_str(stb);
    }

    /* y.path.abs(path) - string */
    if(strcmp_u(name,"y.path.abs")==0){
        int sv=(argc>1)?1:0;
        Val pv=eval_node(args[sv],env);
        if(g_throwing) return make_nil();
        static char absbuf[1024];
#ifndef _WIN32
        /* already absolute */
        if((unsigned char)pv.sval[0]==47u){
            snprintf(absbuf,sizeof(absbuf),"%.1000s",pv.sval);
            return make_str(absbuf);
        }
        /* "." -> cwd */
        { char cwd[512];
          if(getcwd(cwd,sizeof(cwd))){
            if(pv.sval[0]=='.'&&pv.sval[1]==0){ return make_str(cwd); }
            snprintf(absbuf,sizeof(absbuf),"%.400s/%.500s",cwd,pv.sval);
            return make_str(absbuf);
          }
        }
#else
        if(GetFullPathName(pv.sval,1024,absbuf,NULL)) return make_str(absbuf);
#endif
        return pv;
    }

/* 
   v1.7  —  y.json.*   (simple, no nested objects in parse)
*/

    /* y.json.stringify(val) → string */
    if(strcmp_u(name,"y.json.stringify")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        static char jb[8192]; int ji=0;

        /* inline serializer — handles str/int/float/bool/nil/arr/struct */
        #define JS_CHR(c) do{ if(ji<8188) jb[ji++]=(c); }while(0)
        #define JS_STR(p) do{ const char *_p=(p); while(*_p&&ji<8186) jb[ji++]=*_p++; }while(0)

        if(v.type==YS_STR){
            JS_CHR('"');
            for(int i=0;v.sval[i]&&ji<8184;i++){
                char _c=v.sval[i];
                if(_c=='"'){JS_CHR('\\');JS_CHR('"');}
                else if(_c=='\\'){JS_CHR('\\');JS_CHR('\\');}
                else if(_c=='\n'){JS_CHR('\\');JS_CHR('n');}
                else if(_c=='\t'){JS_CHR('\\');JS_CHR('t');}
                else JS_CHR(_c);
            }
            JS_CHR('"');
        } else if(v.type==YS_INT){
            char tmp[24]; int_to_str(v.ival,tmp); JS_STR(tmp);
        } else if(v.type==YS_FLOAT){
            char tmp[32]; snprintf(tmp,sizeof(tmp),"%g",v.fval); JS_STR(tmp);
        } else if(v.type==YS_BOOL){
            JS_STR(v.bval?"true":"false");
        } else if(v.type==YS_NIL){
            JS_STR("null");
        } else if(v.type==YS_ARR){
            JS_CHR('[');
            for(int i=0;i<v.arr_len;i++){
                if(i>0){JS_CHR(',');JS_CHR(' ');}
                Val el=v.arr_data[i];
                if(el.type==YS_STR){ JS_CHR('"'); JS_STR(el.sval); JS_CHR('"'); }
                else if(el.type==YS_INT){ char tmp[24]; int_to_str(el.ival,tmp); JS_STR(tmp); }
                else if(el.type==YS_FLOAT){ char tmp[32]; snprintf(tmp,sizeof(tmp),"%g",el.fval); JS_STR(tmp); }
                else if(el.type==YS_BOOL){ JS_STR(el.bval?"true":"false"); }
                else { JS_STR("null"); }
            }
            JS_CHR(']');
        } else if(v.type==YS_STRUCT){
            JS_CHR('{');
            for(int i=0;i<v.field_count;i++){
                if(i>0){JS_CHR(',');JS_CHR(' ');}
                JS_CHR('"'); JS_STR(v.field_names[i]); JS_CHR('"'); JS_CHR(':'); JS_CHR(' ');
                Val fv=v.field_vals[i];
                if(fv.type==YS_STR){ JS_CHR('"'); JS_STR(fv.sval); JS_CHR('"'); }
                else if(fv.type==YS_INT){ char tmp[24]; int_to_str(fv.ival,tmp); JS_STR(tmp); }
                else if(fv.type==YS_FLOAT){ char tmp[32]; snprintf(tmp,sizeof(tmp),"%g",fv.fval); JS_STR(tmp); }
                else if(fv.type==YS_BOOL){ JS_STR(fv.bval?"true":"false"); }
                else { JS_STR("null"); }
            }
            JS_CHR('}');
        }
        #undef JS_CHR
        #undef JS_STR
        jb[ji]=0; return make_str(jb);
    }

    /* y.json.parse(str) → value */
    if(strcmp_u(name,"y.json.parse")==0){
        int s=(argc>1)?1:0;
        Val src_v=eval_node(args[s],env);
        if(g_throwing) return make_nil();
        const char *js=src_v.sval; int jp=0;

        /* skip whitespace helper */
        #define JP_SKIP while(js[jp]==' '||js[jp]=='\t'||js[jp]=='\n'||js[jp]=='\r')jp++

        JP_SKIP;
        /* string */
        if(js[jp]=='"'){
            jp++;
            static char jsbuf[8192]; int jsi=0;
            while(js[jp]&&js[jp]!='"'){
                if(js[jp]=='\\'&&js[jp+1]){
                    jp++;
                    if(js[jp]=='n'&&jsi<8190) jsbuf[jsi++]='\n';
                    else if(js[jp]=='t'&&jsi<8190) jsbuf[jsi++]='\t';
                    else if(jsi<8190) jsbuf[jsi++]=js[jp];
                } else { if(jsi<8190) jsbuf[jsi++]=js[jp]; }
                jp++;
            }
            jsbuf[jsi]=0;
            return make_str(jsbuf);
        }
        /* boolean/null */
        if(js[jp]=='t'&&js[jp+1]=='r'&&js[jp+2]=='u'&&js[jp+3]=='e') return make_bool(1);
        if(js[jp]=='f'&&js[jp+1]=='a'&&js[jp+2]=='l'&&js[jp+3]=='s'&&js[jp+4]=='e') return make_bool(0);
        if(js[jp]=='n'&&js[jp+1]=='u'&&js[jp+2]=='l'&&js[jp+3]=='l') return make_nil();
        /* number */
        if(js[jp]=='-'||(js[jp]>='0'&&js[jp]<='9')){
            int neg=0; if(js[jp]=='-'){neg=1;jp++;}
            int64_t iv=0;
            while(js[jp]>='0'&&js[jp]<='9'){iv=iv*10+(js[jp]-'0');jp++;}
            if(js[jp]=='.'){
                jp++; double fv=(double)iv, div=1.0;
                while(js[jp]>='0'&&js[jp]<='9'){fv=fv*10+(js[jp]-'0');div*=10;jp++;}
                return make_float(neg?-fv/div:fv/div);
            }
            return make_int(neg?-iv:iv);
        }
        /* array */
        if(js[jp]=='['){
            jp++;
            Val result=make_nil(); result.type=YS_ARR;
            result.arr_data=alloc_arr(128); result.arr_len=0;
            while(js[jp]&&js[jp]!=']'&&result.arr_len<127){
                while(js[jp]==' '||js[jp]=='\t'||js[jp]=='\n'||js[jp]=='\r'){jp++;}
                if(js[jp]==']') break;
                if(js[jp]==','){jp++;continue;}
                Val el=make_nil();
                if(js[jp]=='"'){
                    jp++; static char jab[512]; int jai=0;
                    while(js[jp]&&js[jp]!='"'&&jai<510) jab[jai++]=js[jp++];
                    jab[jai]=0; if(js[jp]=='"') jp++;
                    el=make_str(jab);
                } else if(js[jp]=='-'||(js[jp]>='0'&&js[jp]<='9')){
                    int ng2=0; if(js[jp]=='-'){ng2=1;jp++;}
                    int64_t n2=0;
                    while(js[jp]>='0'&&js[jp]<='9'){n2=n2*10+(js[jp]-'0');jp++;}
                    el=make_int(ng2?-n2:n2);
                } else if(js[jp]=='t'){el=make_bool(1);jp+=4;}
                else if(js[jp]=='f'){el=make_bool(0);jp+=5;}
                else if(js[jp]=='n'){jp+=4;}
                else {jp++; continue;}
                result.arr_data[result.arr_len++]=el;
            }
            if(js[jp]==']') jp++;
            #undef JP_SKIP
            return result;
        }
        /* object → struct */
        if(js[jp]=='{'){
            jp++;
            Val result=make_nil(); result.type=YS_STRUCT;
            result.struct_name[0]=0; int fc=0;
            result.field_vals=alloc_fld(32);
            result.field_names=alloc_nm(32);
            while(js[jp]&&js[jp]!='}'&&fc<31){
                while(js[jp]==' '||js[jp]=='\t'||js[jp]=='\n'||js[jp]=='\r'){jp++;}
                if(js[jp]=='}') break;
                if(js[jp]==','||js[jp]==':'){jp++;continue;}
                if(js[jp]!='"'){jp++;continue;}
                /* key */
                jp++; char key[64]; int ki=0;
                while(js[jp]&&js[jp]!='"'&&ki<62) key[ki++]=js[jp++];
                key[ki]=0; if(js[jp]=='"') jp++;
                while(js[jp]==' '||js[jp]=='\t'||js[jp]=='\n'||js[jp]=='\r'){jp++;}
                if(js[jp]==':') jp++;
                while(js[jp]==' '||js[jp]=='\t'||js[jp]=='\n'||js[jp]=='\r')jp++;
                /* value */
                Val fval_v=make_nil();
                if(js[jp]=='"'){
                    jp++; static char fvb[512]; int fvi=0;
                    while(js[jp]&&js[jp]!='"'&&fvi<510) fvb[fvi++]=js[jp++];
                    fvb[fvi]=0; if(js[jp]=='"') jp++;
                    fval_v=make_str(fvb);
                } else if(js[jp]=='-'||(js[jp]>='0'&&js[jp]<='9')){
                    int fneg=0; if(js[jp]=='-'){fneg=1;jp++;}
                    int64_t fn=0;
                    while(js[jp]>='0'&&js[jp]<='9'){fn=fn*10+(js[jp]-'0');jp++;}
                    if(js[jp]=='.'){
                        jp++; double ff=(double)fn, fd=1.0;
                        while(js[jp]>='0'&&js[jp]<='9'){ff=ff*10+(js[jp]-'0');fd*=10;jp++;}
                        fval_v=make_float(fneg?-ff/fd:ff/fd);
                    } else fval_v=make_int(fneg?-fn:fn);
                } else if(js[jp]=='t'){fval_v=make_bool(1);jp+=4;}
                else if(js[jp]=='f'){fval_v=make_bool(0);jp+=5;}
                else if(js[jp]=='n'){jp+=4;}
                else jp++;
                for(int ki2=0;ki2<31;ki2++) result.field_names[fc][ki2]=key[ki2];
                result.field_names[fc][31]=0;
                result.field_vals[fc]=fval_v; fc++;
            }
            result.field_count=fc;
            #undef JP_SKIP
            return result;
        }
        #undef JP_SKIP
        return make_nil();
    }

/* 
   v1.6  —  Module system helpers
*/

    /* y.module.loaded() → array of imported module names */
    if(strcmp_u(name,"y.module.loaded")==0){
        /* import cache is maintained in eval_node ND_IMPORT;
           this returns a read-only snapshot as an array of strings */
        extern char g_imported_modules[64][512];
        extern int  g_nimported;
        Val result=make_nil(); result.type=YS_ARR;
        result.arr_data=alloc_arr(g_nimported+1); result.arr_len=g_nimported;
        for(int i=0;i<g_nimported;i++) result.arr_data[i]=make_str(g_imported_modules[i]);
        return result;
    }
    /*  v2.1 Test Assertion Builtins  */

    /* assert(cond, msg?) — throws if cond is false */
    if(strcmp_u(name,"assert")==0){
        int sv=0; /* no receiver arg */
        Val cond=eval_node(args[sv],env);
        if(g_throwing) return make_nil();
        if(!val_bool(cond)){
            char em[256]="assertion failed";
            if(argc>sv+1){
                Val mv=eval_node(args[sv+1],env);
                if(!g_throwing&&mv.type==YS_STR)
                    snprintf(em,sizeof(em),"assertion failed: %.200s",mv.sval);
            }
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"%s",em);
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }

    /* assert_eq(a, b, msg?) — throws if a != b */
    if(strcmp_u(name,"assert_eq")==0){
        int sv=0; /* no receiver arg */
        Val a=eval_node(args[sv],env);
        Val b=eval_node(args[sv+1],env);
        if(g_throwing) return make_nil();
        int eq=(a.type==b.type);
        if(eq){
            if(a.type==YS_INT)   eq=(a.ival==b.ival);
            else if(a.type==YS_FLOAT) eq=(a.fval==b.fval);
            else if(a.type==YS_BOOL)  eq=(a.bval==b.bval);
            else if(a.type==YS_STR)   eq=(strcmp_u(a.sval,b.sval)==0);
            else eq=1;
        }
        if(!eq){
            char em[512];
            /* format expected vs got */
            char sa[64]={0}, sb[64]={0};
            if(a.type==YS_INT){char t[32];int_to_str(a.ival,t);for(int i=0;t[i]&&i<62;i++)sa[i]=t[i];}
            else if(a.type==YS_STR){for(int i=0;a.sval[i]&&i<62;i++)sa[i]=a.sval[i];}
            else if(a.type==YS_BOOL){sa[0]=a.bval?'t':'f';}
            if(b.type==YS_INT){char t[32];int_to_str(b.ival,t);for(int i=0;t[i]&&i<62;i++)sb[i]=t[i];}
            else if(b.type==YS_STR){for(int i=0;b.sval[i]&&i<62;i++)sb[i]=b.sval[i];}
            else if(b.type==YS_BOOL){sb[0]=b.bval?'t':'f';}
            snprintf(em,sizeof(em),"assert_eq failed: expected [%s], got [%s]",sb,sa);
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"%s",em);
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }

    /* assert_neq(a, b, msg?) */
    if(strcmp_u(name,"assert_neq")==0){
        int sv=0;
        Val a=eval_node(args[sv],env);
        Val b=eval_node(args[sv+1],env);
        if(g_throwing) return make_nil();
        int eq=(a.type==b.type);
        if(eq){
            if(a.type==YS_INT)  eq=(a.ival==b.ival);
            else if(a.type==YS_STR) eq=(strcmp_u(a.sval,b.sval)==0);
            else if(a.type==YS_BOOL) eq=(a.bval==b.bval);
        }
        if(eq){
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"assert_neq failed: values are equal");
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }

    /* assert_nil(v) */
    if(strcmp_u(name,"assert_nil")==0){
        int sv=0;
        Val v=eval_node(args[sv],env);
        if(g_throwing) return make_nil();
        if(v.type!=YS_NIL){
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"assert_nil failed: value is not nil");
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }

    /* assert_true(v) / assert_false(v) */
    if(strcmp_u(name,"assert_true")==0){
        int sv=0;
        Val v=eval_node(args[sv],env);
        if(g_throwing) return make_nil();
        if(!val_bool(v)){
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"assert_true failed");
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }

    if(strcmp_u(name,"assert_false")==0){
        int sv=0;
        Val v=eval_node(args[sv],env);
        if(g_throwing) return make_nil();
        if(val_bool(v)){
            g_throwing=1; snprintf(g_throw_msg,sizeof(g_throw_msg),"assert_false failed");
            return make_nil();
        }
        g_assert_count++;
        return make_bool(1);
    }



    /*  v1.5 GC builtins  */

    /* gc.collect() → nil  — force a full GC cycle */
    if(strcmp_u(name,"gc.collect")==0){
        gc_collect();
        return make_nil();
    }

    /* gc.stats() → struct { alloc, freed, live, threshold, cycle } */
    if(strcmp_u(name,"gc.stats")==0){
        Val r=make_nil(); r.type=YS_STRUCT;
        snprintf(r.struct_name,sizeof(r.struct_name),"%s","GCStats");
        r.field_count=5;
        r.field_vals  = alloc_fld(5);
        r.field_names = alloc_nm(5);
        /* count live (non-freed) GC nodes */
        int live=0; GCNode *gn=gc_head; while(gn){live++;gn=gn->next;}
        r.field_vals[0]=make_int(gc_total_alloc);
        r.field_vals[1]=make_int(gc_total_freed);
        r.field_vals[2]=make_int((int64_t)live);
        r.field_vals[3]=make_int(gc_threshold);
        r.field_vals[4]=make_int(gc_current_cycle);
        snprintf(r.field_names[0],32,"%s","alloc");
        snprintf(r.field_names[1],32,"%s","freed");
        snprintf(r.field_names[2],32,"%s","live");
        snprintf(r.field_names[3],32,"%s","threshold");
        snprintf(r.field_names[4],32,"%s","cycle");
        return r;
    }



    return make_nil();
}

Val eval_program(Node *prog,Env *env){
    Val last=make_nil();
    for(int i=0;i<prog->stmtc;i++){
        gc_maybe();                          /* v1.5: GC safe point */
        last=eval_node(prog->stmts[i],env);
    }
    Val *mf=env_get(env,"main");
    if(mf&&mf->type==YS_FN&&mf->fn_node){
        Env *fe=env_new(env);
        return eval_block(mf->fn_node->body,fe);
    }
    return last;
}