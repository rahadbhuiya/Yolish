
#include "yolish.h"
#include <unistd.h>

static Env envpool[16]; static int envidx=0;
Env *env_new(Env *parent){
    Env *e=&envpool[envidx++%128];
    e->count=0; e->parent=parent; return e;
}
Val *env_get(Env *e, const char *name){
    for(;e;e=e->parent)
        for(int i=0;i<e->count;i++)
            if(strcmp_u(e->names[i],name)==0) return &e->vals[i];
    return 0;
}
void env_set(Env *e, const char *name, Val v){
    for(Env *s=e;s;s=s->parent)
        for(int i=0;i<s->count;i++)
            if(strcmp_u(s->names[i],name)==0){s->vals[i]=v;return;}
    env_def(e,name,v);
}
void env_def(Env *e, const char *name, Val v){
    if(e->count>=ENV_MAX) return;
    int n=0; while(name[n]&&n<63){e->names[e->count][n]=name[n];n++;}
    e->names[e->count][n]=0;
    e->vals[e->count++]=v;
}

static Val make_int(int64_t v){Val r;r.type=VT_INT;r.ival=v;r.fval=0;r.bval=0;r.sval[0]=0;r.fn_node=0;return r;}
static Val make_float(int64_t v){Val r=make_int(0);r.type=VT_FLOAT;r.fval=v;return r;}
static Val make_bool(int v){Val r=make_int(v);r.type=VT_BOOL;r.bval=v;return r;}
static Val make_str(const char *s){Val r=make_int(0);r.type=VT_STR;int i=0;while(s[i]&&i<511){r.sval[i]=s[i];i++;}r.sval[i]=0;return r;}
static Val make_nil(){Val r=make_int(0);r.type=VT_NIL;return r;}

static int64_t val_int(Val v){
    if(v.type==VT_INT) return v.ival;
    if(v.type==VT_FLOAT) return (int64_t)v.fval;
    if(v.type==VT_BOOL) return v.bval;
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
    return 0;
}

/* number → string helper */
static void int_to_str(int64_t n, char *buf){
    if(n<0){*buf++='-';n=-n;}
    if(n==0){*buf++='0';*buf=0;return;}
    char tmp[24];int i=0;
    while(n>0){tmp[i++]=(char)('0'+n%10);n/=10;}
    for(int j=i-1;j>=0;j--)*buf++=tmp[j];
    *buf=0;
}

void ys_print_val(Val v){
    if(v.type==VT_INT){char b[24];int_to_str(v.ival,b);fprintf(stdout,"%s",b);fflush(stdout);}
    else if(v.type==VT_FLOAT){
        char b[32];
        int64_t ip = v.fval / 1000;
        int64_t fp = v.fval % 1000;
        if(fp < 0) fp = -fp;
        int_to_str(ip, b);
        int i = 0; while(b[i]) i++;
        b[i++] = '.';
        b[i++] = (char)('0' + (fp/100)%10);
        b[i++] = (char)('0' + (fp/10)%10);
        b[i++] = (char)('0' + fp%10);
        b[i] = 0;
        puts(b);
    }
    else if(v.type==VT_BOOL) puts(v.bval?"true":"false");
    else if(v.type==VT_STR){ fprintf(stdout,"%s",v.sval); fflush(stdout); }
    else puts("nil");
}

/* builtin functions */
static Val call_builtin(const char *name, Node **args, int argc, Env *env);
Val eval_node(Node *n, Env *env);

static Val eval_block(Node *b, Env *parent){
    Env *e=env_new(parent);
    Val last=make_nil();
    for(int i=0;i<b->stmtc;i++){
            last=eval_node(b->stmts[i],e);
    }
    return last;
}

Val eval_node(Node *n, Env *env){
    if(!n) return make_nil();
    switch(n->kind){
    case ND_INT:   return make_int(n->ival);
    case ND_FLOAT: return make_float(n->fval);
    case ND_BOOL:  return make_bool((int)n->ival);
    case ND_STR:   return make_str(n->sval);
    case ND_IDENT: {
        Val *v=env_get(env,n->name);
        return v?*v:make_nil();
    }
    case ND_LET: case ND_VAR: {
        Val v=n->right?eval_node(n->right,env):make_nil();
        env_def(env,n->name,v); return v;
    }
    case ND_ASSIGN: {
        Val v=eval_node(n->right,env);
        if(n->left->kind==ND_IDENT) env_set(env,n->left->name,v);
        return v;
    }
    case ND_BINOP: {
        Val L=eval_node(n->left,env);
        Val R=eval_node(n->right,env);
        int use_float=(L.type==VT_FLOAT||R.type==VT_FLOAT);
        switch(n->op){
        case TK_PLUS:
            if(L.type==VT_STR||R.type==VT_STR){
                Val r=make_str(L.sval);
                int i=0;while(r.sval[i])i++;
                int j=0;while(R.sval[j]&&i<511)r.sval[i++]=R.sval[j++];
                r.sval[i]=0; return r;
            }
            return use_float?make_float((val_float(L)+val_float(R))/1000):make_int(val_int(L)+val_int(R));
        case TK_MINUS:  return use_float?make_float((val_float(L)-val_float(R))/1000):make_int(val_int(L)-val_int(R));
        case TK_STAR:   return use_float?make_float(val_float(L)*val_float(R)/1000000):make_int(val_int(L)*val_int(R));
        case TK_SLASH:  {
            int64_t d=val_int(R);
            return use_float?make_float(val_float(L)*1000/val_float(R)):make_int(d?val_int(L)/d:0);
        }
        case TK_PERCENT:{int64_t d=val_int(R);return make_int(d?val_int(L)%d:0);}
        case TK_EQEQ:  return make_bool(val_int(L)==val_int(R));
        case TK_NEQ:   return make_bool(val_int(L)!=val_int(R));
        case TK_LT:    return make_bool(use_float?(val_float(L)<val_float(R)):val_int(L)<val_int(R));
        case TK_GT:    return make_bool(use_float?(val_float(L)>val_float(R)):val_int(L)>val_int(R));
        case TK_LTE:   return make_bool(use_float?(val_float(L)<=val_float(R)):val_int(L)<=val_int(R));
        case TK_GTE:   return make_bool(use_float?(val_float(L)>=val_float(R)):val_int(L)>=val_int(R));
        case TK_AND:   return make_bool(val_bool(L)&&val_bool(R));
        case TK_OR:    return make_bool(val_bool(L)||val_bool(R));
        default: return make_nil();
        }
    }
    case ND_UNOP: {
        Val v=eval_node(n->left,env);
        if(n->op==TK_MINUS) return v.type==VT_FLOAT?make_float(-v.fval):make_int(-val_int(v));
        if(n->op==TK_BANG)  return make_bool(!val_bool(v));
        return v;
    }
    case ND_IF: {
        Val c=eval_node(n->cond,env);
        if(val_bool(c)) return eval_block(n->then,env);
        if(n->els)      return eval_block(n->els,env);
        return make_nil();
    }
    case ND_WHILE: {
        Val last=make_nil();
        while(val_bool(eval_node(n->cond,env)))
            last=eval_block(n->body,env);
        return last;
    }
    case ND_BLOCK: return eval_block(n,env);
    case ND_FN:    {
        Val v=make_nil(); v.type=VT_FN; v.fn_node=n;
        env_def(env,n->name,v); return v;
    }
    case ND_CALL: {
        /* builtins */
        if(n->name[0]=='@'||strcmp_u(n->name,"y.print")==0||strcmp_u(n->name,"print")==0||
           strcmp_u(n->name,"y.println")==0||strcmp_u(n->name,"y.input")==0||
           strcmp_u(n->name,"y.sqrt")==0||strcmp_u(n->name,"y.abs")==0||
           strcmp_u(n->name,"y.len")==0||strcmp_u(n->name,"y.exit")==0)
            return call_builtin(n->name,n->args,n->argc,env);
        /* user fn */
        Val *fv=env_get(env,n->name);
        if(fv&&fv->type==VT_FN&&fv->fn_node){
            Env *fe=env_new(env);
            return eval_block(fv->fn_node->body,fe);
        }
        /* dot method dispatch */
        if(n->left){
            return call_builtin(n->name,n->args,n->argc,env);
        }
        return make_nil();
    }
    case ND_RETURN: return n->right?eval_node(n->right,env):make_nil();
    default: return make_nil();
    }
}

static Val call_builtin(const char *name, Node **args, int argc, Env *env){
    /* y.print / @print */
    if(strcmp_u(name,"y.print")==0||strcmp_u(name,"print")==0||strcmp_u(name,"@print")==0){
        int start = (argc>1)?1:0; /* skip object arg for dot calls */
        for(int i=start;i<argc;i++){
            Val v=eval_node(args[i],env);
            ys_print_val(v);
        }
        return make_nil();
    }
    if(strcmp_u(name,"y.println")==0||strcmp_u(name,"y.println")==0){
        for(int i=0;i<argc;i++){Val v=eval_node(args[i],env);ys_print_val(v);}
        puts("\n"); return make_nil();
    }
    if(strcmp_u(name,"y.input")==0){
        /* read one line from stdin */
        char buf[256]; int i=0; char c=0;
        while(i<255){
            read(STDIN_FILENO,&c,1);
            if(c=='\n'||c=='\r') break;
            buf[i++]=c;
        }
        buf[i]=0; return make_str(buf);
    }
    if(strcmp_u(name,"y.len")==0&&argc>0){
        Val v=eval_node(args[0],env);
        int l=0; while(v.sval[l])l++;
        return make_int(l);
    }
    if(strcmp_u(name,"y.abs")==0&&argc>0){
        Val v=eval_node(args[0],env);
        if(v.type==VT_FLOAT) return make_float(v.fval<0?-v.fval:v.fval);
        return make_int(v.ival<0?-v.ival:v.ival);
    }
    if(strcmp_u(name,"y.exit")==0){
        int code=argc>0?(int)val_int(eval_node(args[0],env)):0;
        exit(code);
    }
    return make_nil();
}

Val eval_program(Node *prog, Env *env){
    Val last=make_nil();
    for(int i=0;i<prog->stmtc;i++) last=eval_node(prog->stmts[i],env);
    /* auto-call main() */
    Val *main_fn=env_get(env,"main");
    if(main_fn&&main_fn->type==VT_FN&&main_fn->fn_node){
            Env *fe=env_new(env);
        return eval_block(main_fn->fn_node->body,fe);
    }
    return last;
}
