#include "yolish.h"

/*  Memory pools  */
static Env  envpool[4096]; static int envidx=0;
static Val  arrpool[2048]; static int arridx=0;
static Val  fldpool[512];  static int fldidx=0;
static char nmpool[512][32]; static int nmidx=0;

static Val *alloc_arr(int n){
    if(arridx+n>=2048) return arrpool;
    Val *p=&arrpool[arridx]; arridx+=n; return p;
}
static Val *alloc_fld(int n){
    if(fldidx+n>=512) return fldpool;
    Val *p=&fldpool[fldidx]; fldidx+=n; return p;
}
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
    Val r; r.type=VT_NIL; r.ival=0; r.fval=0; r.bval=0;
    r.sval[0]=0; r.fn_node=0; r.cap_fd=-1; r.cap_perm=0;
    r.cap_path[0]=0; r.arr_data=0; r.arr_len=0;
    r.struct_name[0]=0; r.field_vals=0; r.field_names=0; r.field_count=0;
    return r;
}
static Val make_int(int64_t v){Val r=make_nil();r.type=VT_INT;r.ival=v;return r;}
static Val make_float(int64_t v){Val r=make_nil();r.type=VT_FLOAT;r.fval=v;return r;}
static Val make_bool(int v){Val r=make_nil();r.type=VT_BOOL;r.bval=v;r.ival=v;return r;}
static Val make_err(const char *msg){
    Val r=make_nil(); r.type=VT_ERR;
    int i=0; while(msg[i]&&i<254){r.sval[i]=msg[i];i++;} r.sval[i]=0;
    return r;
}
static Val make_str(const char *s){
    Val r=make_nil(); r.type=VT_STR;
    int i=0; while(s[i]&&i<255){r.sval[i]=s[i];i++;} r.sval[i]=0;
    return r;
}
static Val make_cap(const char *path,int perm,int64_t fd){
    Val r=make_nil(); r.type=VT_CAP;
    r.cap_fd=fd; r.cap_perm=perm;
    int i=0; while(path[i]&&i<127){r.cap_path[i]=path[i];i++;} r.cap_path[i]=0;
    return r;
}

/*  Value helpers  */
static int64_t val_int(Val v){
    if(v.type==VT_INT)   return v.ival;
    if(v.type==VT_FLOAT) return v.fval/1000;
    if(v.type==VT_BOOL)  return v.bval;
    if(v.type==VT_CAP)   return v.cap_fd;
    if(v.type==VT_ARR)   return v.arr_len;
    return 0;
}
static int64_t val_float(Val v){
    if(v.type==VT_FLOAT) return v.fval;
    return val_int(v)*1000;
}
static int val_bool(Val v){
    if(v.type==VT_BOOL) return v.bval;
    if(v.type==VT_INT)  return v.ival!=0;
    if(v.type==VT_STR)  return v.sval[0]!=0;
    if(v.type==VT_ARR)  return v.arr_len>0;
    if(v.type==VT_CAP)  return v.cap_fd>=0;
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
    if(v.type==VT_INT){
        char b[24]; int_to_str(v.ival,b); puts(b);
    } else if(v.type==VT_FLOAT){
        char b[32];
        int64_t ip=v.fval/1000,fp=v.fval%1000;
        if(fp<0)fp=-fp;
        int_to_str(ip,b);
        int i=0; while(b[i])i++;
        b[i++]='.';
        b[i++]=(char)('0'+(fp/100)%10);
        b[i++]=(char)('0'+(fp/10)%10);
        b[i++]=(char)('0'+fp%10);
        b[i]=0; puts(b);
    } else if(v.type==VT_BOOL){
        puts(v.bval?"true":"false");
    } else if(v.type==VT_STR){
        puts(v.sval);
    } else if(v.type==VT_ARR){
        puts("[");
        for(int i=0;i<v.arr_len;i++){
            if(i>0) puts(", ");
            ys_print_val(v.arr_data[i]);
        }
        puts("]");
    } else if(v.type==VT_STRUCT){
        puts(v.struct_name); puts("{");
        for(int i=0;i<v.field_count;i++){
            if(i>0) puts(", ");
            puts(v.field_names[i]); puts(": ");
            ys_print_val(v.field_vals[i]);
        }
        puts("}");
    } else if(v.type==VT_CAP){
        puts("<cap:"); puts(v.cap_path); puts(">");
    } else {
        puts("nil");
    }
}

/*  Forward  */
static Val eval_block(Node *b,Env *parent);
static Val call_builtin(const char *name,Node **args,int argc,Env *env);

/*  Return signal  */
static int g_returning=0;
static Val g_return_val;
static int g_cur_line=0;  /* last known source line */
static int g_ann_depth=0; /* annotation fire depth — suppress nested calls */
static int g_throwing=0;     /* throw signal */
static char g_throw_msg[512]; /* thrown message */
static Val g_throw_val;       /* thrown value — use g_throw_msg for str content */

/*  Runtime error  */
void ys_error(int line, const char *msg){
    char buf[256]; int n=0;
    const char *pre="[YS] error";
    for(int i=0;pre[i];i++) buf[n++]=pre[i];
    if(line>0){
        const char *lp=" (line ";
        for(int i=0;lp[i];i++) buf[n++]=lp[i];
        char tmp[16]; int ti=0; int ln=line;
        do { tmp[ti++]=(char)('0'+(ln%10)); ln/=10; } while(ln>0);
        while(ti>0) buf[n++]=tmp[--ti];
        buf[n++]=')';
    }
    buf[n++]=':'; buf[n++]=' ';
    for(int i=0;msg[i]&&n<250;i++) buf[n++]=msg[i];
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

/*  eval_node  */
Val eval_node(Node *n,Env *env){
    if(!n) return make_nil();
    switch(n->kind){

    case ND_INT:   return make_int(n->ival);
    case ND_FLOAT: return make_float(n->fval);
    case ND_BOOL:  return make_bool((int)n->ival);
    case ND_STR:   return make_str(n->sval);

    case ND_IDENT:{
        Val *v=env_get(env,n->name);
        return v?*v:make_nil();
    }

    case ND_LET: case ND_VAR:{
        Val v=n->right?eval_node(n->right,env):make_nil();
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
        Val v=make_nil(); v.type=VT_ARR;
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
        if(obj.type==VT_STRUCT){
            for(int i=0;i<obj.field_count;i++){
                if(strcmp_u(obj.field_names[i],n->name)==0)
                    return obj.field_vals[i];
            }
            ys_error(g_cur_line,"unknown struct field");
            return make_nil();
        }
        /* dot on non-struct: treat as builtin namespace (y.print etc handled via ND_CALL) */
        return make_nil();
    }

    case ND_INDEX:{
        Val arr=eval_node(n->left,env);
        Val idx=eval_node(n->right,env);
        int i=(int)val_int(idx);
        /* struct field access via index */
        if(arr.type==VT_STRUCT){
            const char *fname=n->right->sval;
            for(int j=0;j<arr.field_count;j++)
                if(strcmp_u(arr.field_names[j],fname)==0)
                    return arr.field_vals[j];
            return make_nil();
        }
        if(arr.type!=VT_ARR||!arr.arr_data) return make_nil();
        if(i<0||i>=arr.arr_len) return make_nil();
        return arr.arr_data[i];
    }

    /*  Array index write  */
    case ND_INDEX_SET:{
        Val arr=eval_node(n->left->left,env);
        Val idx=eval_node(n->left->right,env);
        Val val=eval_node(n->right,env);
        int i=(int)val_int(idx);
        if(arr.type==VT_ARR&&arr.arr_data&&i>=0&&i<arr.arr_len)
            arr.arr_data[i]=val;
        if(n->left->left->kind==ND_IDENT)
            env_set(env,n->left->left->name,arr);
        return val;
    }

    /*  Struct definition  */
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
        Val v=make_nil(); v.type=VT_STRUCT;
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

    case ND_BINOP:{
        Val L=eval_node(n->left,env);
        Val R=eval_node(n->right,env);
        int use_f=(L.type==VT_FLOAT||R.type==VT_FLOAT);
        switch(n->op){
        case TK_PLUS:
            if(L.type==VT_STR){
                Val r=make_str(L.sval);
                int i=0; while(r.sval[i])i++;
                int j=0; while(R.sval[j]&&i<254)r.sval[i++]=R.sval[j++];
                r.sval[i]=0; return r;
            }
            return use_f?make_float((val_float(L)+val_float(R))/1000):make_int(val_int(L)+val_int(R));
        case TK_MINUS:
            return use_f?make_float((val_float(L)-val_float(R))/1000)
                        :make_int(val_int(L)-val_int(R));
        case TK_STAR:
            return use_f?make_float(val_float(L)*val_float(R)/1000000)
                        :make_int(val_int(L)*val_int(R));
        case TK_SLASH:{int64_t d=val_int(R);
            return use_f?make_float(val_float(L)*1000/val_float(R))
                        :make_int(d?val_int(L)/d:0);}
        case TK_PERCENT:{int64_t d=val_int(R);return make_int(d?val_int(L)%d:0);}
        case TK_EQEQ:
            if(L.type==VT_STR&&R.type==VT_STR)
                return make_bool(strcmp_u(L.sval,R.sval)==0);
            return make_bool(val_int(L)==val_int(R));
        case TK_NEQ:
            if(L.type==VT_STR&&R.type==VT_STR)
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
            return v.type==VT_FLOAT?make_float(-v.fval):make_int(-val_int(v));
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
        while(val_bool(eval_node(n->cond,env)))
            last=eval_block(n->body,env);
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
                if(g_returning) break;
            }
        } else {
            Val iter=eval_node(n->cond,env);
            if(iter.type==VT_ARR){
                for(int idx=0;idx<iter.arr_len;idx++){
                    Env *fe=env_new(env);
                    env_def(fe,n->name,iter.arr_data[idx]);
                    last=eval_block(n->body,fe);
                    if(g_returning) break;
                }
            } else if(iter.type==VT_STR){
                int slen=str_len_u(iter.sval);
                for(int idx=0;idx<slen;idx++){
                    char ch[2]; ch[0]=iter.sval[idx]; ch[1]=0;
                    Env *fe=env_new(env);
                    env_def(fe,n->name,make_str(ch));
                    last=eval_block(n->body,fe);
                    if(g_returning) break;
                }
            }
        }
        return last;
    }

    case ND_MATCH:{
        Val subject=eval_node(n->cond,env);
        for(int i=0;i<n->argc;i++){
            Node *pat =n->arg_data[i*2  ];
            Node *body=n->arg_data[i*2+1];
            if(!pat||!body) continue;
            int matched=0;

            /* wildcard _ */
            if(pat->kind==ND_IDENT && pat->name[0]=='_' && pat->name[1]==0){
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
                if(subject.type==VT_INT   && pv.type==VT_INT)  matched=(subject.ival==pv.ival);
                else if(subject.type==VT_FLOAT && pv.type==VT_FLOAT) matched=(subject.fval==pv.fval);
                else if(subject.type==VT_BOOL  && pv.type==VT_BOOL)  matched=(subject.bval==pv.bval);
                else if(subject.type==VT_STR   && pv.type==VT_STR)   matched=(strcmp_u(subject.sval,pv.sval)==0);
                else if(subject.type==VT_INT   && pv.type==VT_FLOAT) matched=(subject.ival*1000==pv.fval);
                else matched=0;
            }

            if(matched){
                /* body is block or expression */
                if(body->kind==ND_BLOCK) return eval_block(body,env);
                return eval_node(body,env);
            }
        }
        return make_nil(); /* no arm matched */
    }

    case ND_THROW:{
        Val thrown = n->right ? eval_node(n->right,env) : make_nil();
        /* store message in stable global string */
        if(thrown.type==VT_STR||thrown.type==VT_ERR){
            int ci=0; while(thrown.sval[ci]&&ci<510){g_throw_msg[ci]=thrown.sval[ci];ci++;}
            g_throw_msg[ci]=0;
        } else if(thrown.type==VT_INT){
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
                if(n->name[0]) env_def(ce,n->name,make_str(g_throw_msg));
                int saved=envidx;
                result=eval_block(n->els,ce);
                envidx=saved;
            }
        }
        return result;
    }

    case ND_BLOCK: return eval_block(n,env);

    case ND_FN_LIT:{
        Val v=make_nil(); v.type=VT_FN; v.fn_node=n;
        v.fn_env=(void*)env; /* capture current environment */
        return v;
    }

    case ND_FN:{
        Val v=make_nil(); v.type=VT_FN; v.fn_node=n;
        /* annotation info lives in fn_node->type and fn_node->sval */
        env_def(env,n->name,v); return v;
    }

    case ND_CALL:{
        if(n->name[0]=='@'
            ||strcmp_u(n->name,"y.print")==0 ||strcmp_u(n->name,"print")==0
            ||strcmp_u(n->name,"y.println")==0||strcmp_u(n->name,"println")==0
            ||strcmp_u(n->name,"y.input")==0  ||strcmp_u(n->name,"input")==0
            ||strcmp_u(n->name,"y.len")==0    ||strcmp_u(n->name,"len")==0
            ||strcmp_u(n->name,"y.abs")==0    ||strcmp_u(n->name,"abs")==0
            ||strcmp_u(n->name,"y.str")==0    ||strcmp_u(n->name,"str")==0
            ||strcmp_u(n->name,"y.int")==0    ||strcmp_u(n->name,"int")==0
            ||strcmp_u(n->name,"y.push")==0   ||strcmp_u(n->name,"push")==0
            ||strcmp_u(n->name,"y.pop")==0    ||strcmp_u(n->name,"pop")==0
            ||strcmp_u(n->name,"y.exit")==0   ||strcmp_u(n->name,"exit")==0)
            return call_builtin(n->name,n->args,n->argc,env);

        /* dot calls — build qualified name */
        if(n->left){
            static char qname[128];
            int qi=0;
            const char *obj=n->left->name;
            while(obj[qi]&&qi<60){qname[qi]=obj[qi];qi++;}
            qname[qi++]='.';
            const char *mth=n->name;
            int mi=0;
            while(mth[mi]&&qi<126){qname[qi++]=mth[mi++];}
            qname[qi]=0;
            return call_builtin(qname,n->args,n->argc,env);
        }

        /* user function */
        Val *fv=env_get(env,n->name);
        if(fv&&fv->type==VT_FN&&fv->fn_node){
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
                    /* ── @intent → stderr ── */
                    if(ann_t[0]=='i'&&ann_t[1]=='n'){
                        fputs("[scheduler] intent=",stderr);
                        fputs(ann_a[0]?ann_a:"unspecified",stderr);
                        fputs(" fn=",stderr); fputs(n->name,stderr); fputs("\n",stderr);
                        fflush(stderr);
                    }
                    /* ── @audit → stderr ── */
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
            /* don't reset g_throwing — let it propagate to try/catch */
            if(fn_def->type[0]) g_ann_depth++;
            Val result=eval_block(fn_def->body,fe);
            if(fn_def->type[0]) g_ann_depth--;
            if(!g_throwing) g_returning=0; /* preserve throw signal */
            return result;
        }
        return make_nil();
    }

    case ND_IMPORT:{
        /* load and eval another .y file */
        /* path is relative to source dir — chdir in main.c handles resolution */
        static char import_src[65536];
        FILE *f=fopen(n->sval,"r");
        if(!f){
            ys_error(g_cur_line,"cannot open import file");
            return make_nil();
        }
        int sz=(int)fread(import_src,1,sizeof(import_src)-1,f);
        fclose(f); import_src[sz]=0;
        Lexer il; lex_init(&il,import_src,sz);
        Node *iprog=parse_program(&il);
        eval_program(iprog,env); /* share same env = exported symbols */
        return make_nil();
    }

    case ND_RETURN:
        g_return_val=n->right?eval_node(n->right,env):make_nil();
        g_returning=1;
        return g_return_val;

    default: return make_nil();
    }
}

static Val eval_block(Node *b,Env *parent){
    if(!b) return make_nil();
    Env *e=env_new(parent);
    Val last=make_nil();
    for(int i=0;i<b->stmtc;i++){
        last=eval_node(b->stmts[i],e);
        if(g_returning||g_throwing) break;
    }
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
    /* y.input */
    if(strcmp_u(name,"y.input")==0||strcmp_u(name,"input")==0){
        static char ibuf[256]; int i=0; char c=0;
        while(i<255){if(fread(&c,1,1,stdin)!=1)break;if(c=='\n'||c=='\r')break;ibuf[i++]=c;}
        ibuf[i]=0; return make_str(ibuf);
    }
    /* y.len */
    if(strcmp_u(name,"y.len")==0||strcmp_u(name,"len")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==VT_ARR) return make_int(v.arr_len);
        return make_int(str_len_u(v.sval));
    }
    /* y.abs */
    if(strcmp_u(name,"y.abs")==0||strcmp_u(name,"abs")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        if(v.type==VT_FLOAT) return make_float(v.fval<0?-v.fval:v.fval);
        return make_int(v.ival<0?-v.ival:v.ival);
    }
    /* y.str */
    if(strcmp_u(name,"y.str")==0||strcmp_u(name,"str")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        char b[64]; int_to_str(val_int(v),b);
        return make_str(b);
    }
    /* y.int */
    if(strcmp_u(name,"y.int")==0||strcmp_u(name,"int")==0){
        int s=(argc>1)?1:0;
        Val v=eval_node(args[s],env);
        return make_int(val_int(v));
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
        Val result=make_nil(); result.type=VT_ARR;
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
        char buf[512]; int bi=0;
        while(*f && bi<510){
            if(*f=='{'){
                f++;
                /* parse index digit(s) */
                int idx=0;
                while(*f>='0'&&*f<='9'){idx=idx*10+(*f-'0');f++;}
                if(*f=='}') f++;
                int arg_idx=s0+1+idx;
                if(arg_idx<argc){
                    Val av=eval_node(args[arg_idx],env);
                    if(av.type==VT_INT){
                        int64_t v=av.ival; int neg=0;
                        if(v<0){neg=1;v=-v;}
                        char tb[32]; int ti=0;
                        do{tb[ti++]=(char)('0'+(v%10));v/=10;}while(v>0);
                        if(neg&&bi<509) buf[bi++]='-';
                        while(ti>0&&bi<510) buf[bi++]=tb[--ti];
                    } else if(av.type==VT_STR){
                        for(int i=0;av.sval[i]&&bi<510;i++) buf[bi++]=av.sval[i];
                    } else if(av.type==VT_BOOL){
                        const char *bs=av.bval?"true":"false";
                        for(int i=0;bs[i]&&bi<510;i++) buf[bi++]=bs[i];
                    } else if(av.type==VT_FLOAT){
                        int64_t whole=av.fval/1000, frac=av.fval%1000;
                        if(frac<0)frac=-frac;
                        int64_t w2=whole; int neg=0;
                        if(w2<0){neg=1;w2=-w2;}
                        char tb[32]; int ti=0;
                        do{tb[ti++]=(char)('0'+(w2%10));w2/=10;}while(w2>0);
                        if(neg&&bi<509) buf[bi++]='-';
                        while(ti>0&&bi<510) buf[bi++]=tb[--ti];
                        buf[bi++]='.';
                        char fb[8]; int fi=0;
                        int64_t fr=frac;
                        do{fb[fi++]=(char)('0'+(fr%10));fr/=10;}while(fi<3);
                        while(fi>0&&bi<510) buf[bi++]=fb[--fi];
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
        Val result=make_nil(); result.type=VT_ARR;
        result.arr_data=alloc_arr(arr.arr_len+1);
        result.arr_len=arr.arr_len;
        if(fn.type==VT_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                g_returning=0;
                int saved=envidx;
                result.arr_data[i]=eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
            }
        }
        return result;
    }
    /* y.filter(arr, fn) → filtered array */
    if(strcmp_u(name,"y.filter")==0||strcmp_u(name,"filter")==0){
        int s0=(argc>2)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        Val result=make_nil(); result.type=VT_ARR;
        result.arr_data=alloc_arr(arr.arr_len+1);
        result.arr_len=0;
        if(fn.type==VT_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                g_returning=0;
                int saved=envidx;
                Val r=eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
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
        if(fn.type==VT_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],acc);
                if(fd->argc>1) env_def(fe,fd->field_names[1],arr.arr_data[i]);
                g_returning=0;
                int saved=envidx;
                acc=eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
            }
        }
        return acc;
    }
    /* y.each(arr, fn) → run fn for side effects */
    if(strcmp_u(name,"y.each")==0||strcmp_u(name,"each")==0){
        int s0=(argc>2)?1:0;
        Val arr=eval_node(args[s0],env);
        Val fn =eval_node(args[s0+1],env);
        if(fn.type==VT_FN&&fn.fn_node){
            Node *fd=fn.fn_node;
            Env *ce=(fn.fn_env)?((Env*)fn.fn_env):env;
            for(int i=0;i<arr.arr_len;i++){
                Env *fe=env_new(ce);
                if(fd->argc>0) env_def(fe,fd->field_names[0],arr.arr_data[i]);
                g_returning=0;
                int saved=envidx;
                eval_block(fd->body,fe);
                envidx=saved; g_returning=0;
            }
        }
        return make_nil();
    }

    /* y.typeof(val) → "int","float","str","bool","array","struct","fn","err","nil" */
    if(strcmp_u(name,"y.typeof")==0||strcmp_u(name,"typeof")==0){
        int s0=(argc>1)?1:0;
        Val v=eval_node(args[s0],env);
        const char *t="nil";
        if(v.type==VT_INT)    t="int";
        else if(v.type==VT_FLOAT)  t="float";
        else if(v.type==VT_STR)    t="str";
        else if(v.type==VT_BOOL)   t="bool";
        else if(v.type==VT_ARR)    t="array";
        else if(v.type==VT_STRUCT) t="struct";
        else if(v.type==VT_FN)     t="fn";
        else if(v.type==VT_CAP)    t="cap";
        else if(v.type==VT_ERR)    t="err";
        return make_str(t);
    }
    /* y.is_int / y.is_str / y.is_float / y.is_bool / y.is_array / y.is_fn / y.is_nil */
    if(strcmp_u(name,"y.is_int")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_INT); }
    if(strcmp_u(name,"y.is_float")==0){ int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_FLOAT); }
    if(strcmp_u(name,"y.is_str")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_STR); }
    if(strcmp_u(name,"y.is_bool")==0) { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_BOOL); }
    if(strcmp_u(name,"y.is_array")==0){ int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_ARR); }
    if(strcmp_u(name,"y.is_fn")==0)   { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_FN); }
    if(strcmp_u(name,"y.is_nil")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_NIL); }
    if(strcmp_u(name,"y.is_err")==0)  { int s0=(argc>1)?1:0; return make_bool(eval_node(args[s0],env).type==VT_ERR); }

    /* y.push(arr, val) */
    if(strcmp_u(name,"y.push")==0||strcmp_u(name,"push")==0){
        int s=(argc>2)?1:0;
        Val arr=eval_node(args[s],env);
        Val val=eval_node(args[s+1],env);
        if(arr.type!=VT_ARR) return make_nil();
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
        if(arr.type!=VT_ARR||arr.arr_len==0) return make_nil();
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
        if(cap.type!=VT_CAP) return make_str("");
        FILE *fp=(FILE*)(uintptr_t)cap.cap_fd;
        static char rbuf[4096]; int n=(int)fread(rbuf,1,4095,fp);
        if(n<0) { n=0; } rbuf[n]=0; return make_str(rbuf);
    }
    if(strcmp_u(name,"cap.write")==0||strcmp_u(name,"write")==0){
        int s=(argc>2)?1:0;
        Val cap=eval_node(args[s],env);
        Val dat=eval_node(args[s+1],env);
        if(cap.type!=VT_CAP) return make_int(-1);
        FILE *fp=(FILE*)(uintptr_t)cap.cap_fd;
        int n=(int)fwrite(dat.sval,1,str_len_u(dat.sval),fp);
        fflush(fp); return make_int(n);
    }
    if(strcmp_u(name,"cap.close")==0||strcmp_u(name,"close")==0){
        int s=(argc>1)?1:0;
        Val cap=eval_node(args[s],env);
        if(cap.type==VT_CAP&&cap.cap_fd) fclose((FILE*)(uintptr_t)cap.cap_fd);
        return make_nil();
    }
    if(strcmp_u(name,"cap.perm")==0||strcmp_u(name,"perm")==0){
        int s=(argc>1)?1:0;
        Val cap=eval_node(args[s],env);
        return make_int(cap.type==VT_CAP?cap.cap_perm:0);
    }

    return make_nil();
}

Val eval_program(Node *prog,Env *env){
    Val last=make_nil();
    for(int i=0;i<prog->stmtc;i++) last=eval_node(prog->stmts[i],env);
    Val *mf=env_get(env,"main");
    if(mf&&mf->type==VT_FN&&mf->fn_node){
        Env *fe=env_new(env);
        return eval_block(mf->fn_node->body,fe);
    }
    return last;
}