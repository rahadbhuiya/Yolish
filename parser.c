#include "yolish.h"

/* Forward declarations */
static Node *parse_expr(Lexer *l);
static Node *parse_stmt(Lexer *l);
static Node *parse_block(Lexer *l);
static Node *parse_binop(Lexer *l, int min_prec);

/* simple allocator using stack-like bump */
/* v1.9: chunk-based node allocator — pointers never invalidated */
#define NODE_CHUNK_SIZE 2048

typedef struct NodeChunk {
    Node             nodes[NODE_CHUNK_SIZE];
    struct NodeChunk *next;
    int               used;
} NodeChunk;

static NodeChunk *chunk_head = NULL;
static int        pool_idx   = 0; /* total allocated (for save/restore) */

static void pool_ensure(void){
    if(chunk_head && chunk_head->used < NODE_CHUNK_SIZE) return;
    NodeChunk *nc = (NodeChunk*)calloc(1, sizeof(NodeChunk));
    if(!nc){ fprintf(stderr,"ys: out of memory (node pool)\n"); return; }
    nc->next = chunk_head;
    nc->used = 0;
    chunk_head = nc;
}

/* statement pointer pool — avoids static array re-entry bug */
static Node *stmt_pool[8192];
static int   stmt_pool_idx=0;
/* v1.9: persistent array element pool — never reset by save/restore */
#define ARR_ELEM_POOL 65536
static Node *arr_elem_pool[ARR_ELEM_POOL];
static int   arr_elem_idx = 0;


static Node **alloc_stmts(int n){
    if(stmt_pool_idx+n>=8192) stmt_pool_idx=0; /* wrap-around safety */
    Node **p=&stmt_pool[stmt_pool_idx];
    stmt_pool_idx+=n;
    return p;
}
static Node *alloc_node(NodeKind k){
    pool_ensure();
    if(!chunk_head) return NULL;
    Node *n = &chunk_head->nodes[chunk_head->used++];
    pool_idx++;
    memset(n, 0, sizeof(Node));
    n->kind=k;
    return n;
}

/* v2.4: heap-allocate a Node's sval from a (possibly non-null-terminated)
   source span. Parse-time strings live for the lifetime of the process
   (the AST is never freed mid-run), so a plain malloc is sufficient —
   no GC tracking needed here, unlike runtime Val strings. */
static void node_set_sval(Node *n, const char *src, int len){
    if(len<0) len=0;
    n->sval=(char*)malloc((size_t)len+1);
    if(!n->sval) return;
    for(int i=0;i<len;i++) n->sval[i]=src[i];
    n->sval[len]=0;
}

static Token cur(Lexer *l){ return l->cur; }
static Token eat(Lexer *l){
    Token t=l->cur;
    l->cur=lex_next(l);
    while(l->cur.kind==TK_NL) l->cur=lex_next(l);
    return t;
}
static int check(Lexer *l, TokenKind k){ return l->cur.kind==k; }
/* v1.4: human-readable token name */
static const char *tok_name(TokenKind k){
    switch(k){
    case TK_EOF:      return "end of file";
    case TK_IDENT:    return "identifier";
    case TK_INT:      return "integer";
    case TK_FLOAT:    return "float";
    case TK_STR:      return "string";
    case TK_LPAREN:   return "'('";
    case TK_RPAREN:   return "')'";
    case TK_LBRACE:   return "'{'";
    case TK_RBRACE:   return "'}'";
    case TK_LBRACKET: return "'['";
    case TK_RBRACKET: return "']'";
    case TK_COMMA:    return "','";
    case TK_COLON:    return "':'";
    case TK_EQ:       return "'='";
    case TK_ARROW:    return "'->'";
    case TK_FAT_ARROW:return "'=>'";
    case TK_SEMICOLON:return "';'";
    case TK_FN:       return "'fn'";
    case TK_LET:      return "'let'";
    case TK_VAR:      return "'var'";
    case TK_IF:       return "'if'";
    case TK_ELSE:     return "'else'";
    case TK_WHILE:    return "'while'";
    case TK_FOR:      return "'for'";
    case TK_RETURN:   return "'return'";
    case TK_STRUCT:   return "'struct'";
    case TK_ENUM:     return "'enum'";
    case TK_TEST:     return "'test'";
    case TK_MATCH:    return "'match'";
    case TK_IMPORT:   return "'import'";
    case TK_IN:       return "'in'";
    default:          return "token";
    }
}

/* lev_dist defined in eval.c */
extern int lev_dist(const char *a, const char *b);

static Token expect(Lexer *l, TokenKind k){
    if(!check(l,k)){
        extern char g_src_file[512];
        char buf[256]; int n=0;
        /* file:line:col format */
        if(g_src_file[0]){
            for(int i=0;g_src_file[i]&&n<100;i++) buf[n++]=g_src_file[i];
        } else { buf[n++]='[';buf[n++]='Y';buf[n++]='S';buf[n++]=']'; }
        buf[n++]=':';
        /* line */
        int ln=l->cur.line>0?l->cur.line:l->line;
        char tmp[16]; int ti=0;
        do{tmp[ti++]=(char)('0'+(ln%10));ln/=10;}while(ln>0);
        while(ti>0){buf[n++]=tmp[--ti];} buf[n++]=':';
        /* column */
        int col=l->cur.column>0?l->cur.column:1;
        ti=0; do{tmp[ti++]=(char)('0'+(col%10));col/=10;}while(col>0);
        while(ti>0){buf[n++]=tmp[--ti];} buf[n++]=':'; buf[n++]=' ';
        /* message */
        const char *exp=tok_name(k);
        const char *got=tok_name(l->cur.kind);
        const char *msg1="expected "; for(int i=0;msg1[i];i++) buf[n++]=msg1[i];
        for(int i=0;exp[i]&&n<220;i++) buf[n++]=exp[i];
        const char *msg2=", got "; for(int i=0;msg2[i];i++) buf[n++]=msg2[i];
        for(int i=0;got[i]&&n<240;i++) buf[n++]=got[i];
        /* show the actual token value if ident */
        if(l->cur.kind==TK_IDENT&&l->cur.len>0&&n<248){
            buf[n++]=' '; buf[n++]='(';
            int tl=l->cur.len<16?l->cur.len:16;
            for(int i=0;i<tl;i++) buf[n++]=l->cur.start[i];
            buf[n++]=')';
        }
        buf[n++]='\n'; buf[n]=0;
        ys_print(buf);
    }
    return eat(l);
}
static int match_tk(Lexer *l, TokenKind k){
    if(check(l,k)){eat(l);return 1;} return 0;
}



static Node *parse_primary(Lexer *l){
    Token t=cur(l);
    if(t.kind==TK_INT){   eat(l); Node*n=alloc_node(ND_INT);n->ival=t.ival;return n;}
    if(t.kind==TK_FLOAT){ eat(l); Node*n=alloc_node(ND_FLOAT);n->fval=t.fval;return n;}
    if(t.kind==TK_TRUE||t.kind==TK_FALSE){
        eat(l); Node*n=alloc_node(ND_BOOL);n->ival=t.ival;return n;}
    if(t.kind==TK_STR){
        Node*n=alloc_node(ND_STR);
        /* v2.4: no length cap — string literals can be any size */
        node_set_sval(n, t.start, t.len);
        eat(l);
        return n;
    }
    if(t.kind==TK_IDENT){
        eat(l); Node*n=alloc_node(ND_IDENT);
        n->line=t.line; n->column=t.column;
        int len=t.len<63?t.len:63;
        for(int i=0;i<len;i++) n->name[i]=t.start[i];
        n->name[len]=0;
        /* function call */
        if(check(l,TK_LPAREN)){
            eat(l);
            Node *call=alloc_node(ND_CALL);
            for(int i=0;i<63;i++) call->name[i]=n->name[i];
            int argc=0;
            while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&argc<8){
                call->arg_data[argc++]=parse_expr(l);
                if(!match_tk(l,TK_COMMA)) break;
            }
            if(check(l,TK_RPAREN)) eat(l);
            call->args=call->arg_data; call->argc=argc;
            return call;
        }
        /* struct literal: Point { x: 10, y: 20 } — only uppercase names */
        if(check(l,TK_LBRACE) && n->name[0]>='A' && n->name[0]<='Z'){
            /* uppercase ident followed by { = struct literal */
            eat(l);
            Node *sl=alloc_node(ND_STRUCT_LIT);
            for(int i=0;i<63;i++) { sl->name[i]=n->name[i]; } sl->name[63]=0;
            int fc=0;
            while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)&&fc<8){
                while(check(l,TK_NL)) eat(l);
                if(check(l,TK_RBRACE)) break;
                Token fn=expect(l,TK_IDENT);
                int fl=fn.len<31?fn.len:31;
                for(int i=0;i<fl;i++) sl->field_names[fc][i]=fn.start[i];
                sl->field_names[fc][fl]=0;
                if(check(l,TK_COLON)) eat(l);
                sl->arg_data[fc]=parse_expr(l);
                if(check(l,TK_COMMA)) eat(l);
                fc++;
            }
            if(check(l,TK_RBRACE)) eat(l);
            sl->args=sl->arg_data; sl->argc=fc;
            return sl;
        }
        /* index access: arr[i] */
        if(check(l,TK_LBRACKET)){
            eat(l);
            Node *idx=alloc_node(ND_INDEX);
            idx->left=n;
            idx->right=parse_expr(l);
            if(check(l,TK_RBRACKET)) eat(l);
            if(check(l,TK_EQ)){
                eat(l);
                Node *asgn=alloc_node(ND_INDEX_SET);
                asgn->left=idx; asgn->right=parse_expr(l);
                return asgn;
            }
            return idx;
        }
        /* dot access — handles chained dots and method calls: a.b().c().d */
        if(check(l,TK_DOT)){
            Node *cur2=n;
            while(check(l,TK_DOT)){
                eat(l);
                Token m=expect(l,TK_IDENT);
                Node *dot=alloc_node(ND_DOT);
                dot->left=cur2;
                int ml=m.len<63?m.len:63;
                for(int i=0;i<ml;i++) { dot->name[i]=m.start[i]; }
                dot->name[ml]=0;
                /* method call */
                if(check(l,TK_LPAREN)){
                    eat(l); dot->kind=ND_CALL;
                    /* arg_data[0] reserved for obj in method calls */
                    dot->arg_data[0]=cur2; int argc2=1;
                    while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&argc2<8){
                        dot->arg_data[argc2++]=parse_expr(l);
                        if(!match_tk(l,TK_COMMA)) break;
                    }
                    if(check(l,TK_RPAREN)) eat(l);
                    dot->args=dot->arg_data; dot->argc=argc2;
                    /* keep looping — next dot starts a new chained call */
                    cur2=dot;
                    continue;
                }
                cur2=dot;
            }
            return cur2;
        }
        return n;
    }
    if(t.kind==TK_LPAREN){
        eat(l); Node *e=parse_expr(l); expect(l,TK_RPAREN); return e;
    }
    if(t.kind==TK_MINUS){
        eat(l); Node *n=alloc_node(ND_UNOP);
        n->op=TK_MINUS; n->left=parse_primary(l); return n;
    }
    if(t.kind==TK_BANG){
        eat(l); Node *n=alloc_node(ND_UNOP);
        n->op=TK_BANG; n->left=parse_primary(l); return n;
    }
    if(t.kind==TK_TILDE){
        eat(l); Node *n=alloc_node(ND_UNOP);
        n->op=TK_TILDE; n->left=parse_primary(l); return n;
    }
    /* Array literal: [1, 2, 3] */
    if(t.kind==TK_LBRACKET){
        eat(l);
        Node *arr=alloc_node(ND_ARRAY);
        /* v1.9: arr_elem_pool — persistent, never reset by string interp save/restore */
        int _ae_base = arr_elem_idx;
        Node **elems = &arr_elem_pool[_ae_base]; int ec=0;
        if(_ae_base + 1024 < ARR_ELEM_POOL) arr_elem_idx += 1024; /* reserve space */
        while(!check(l,TK_RBRACKET)&&!check(l,TK_EOF)&&ec<1024){
            while(check(l,TK_NL)) eat(l);
            if(check(l,TK_RBRACKET)) break;
            elems[ec++]=parse_expr(l);
            while(check(l,TK_NL)) eat(l);
            if(!match_tk(l,TK_COMMA)) break;
        }
        if(check(l,TK_RBRACKET)) eat(l);
        arr_elem_idx = _ae_base + ec; /* release unused reservation */
        arr->stmts=elems; arr->stmtc=ec;
        arr->args=NULL; arr->argc=0;
        return arr;
    }
    /* anonymous fn literal: fn(params) { body } */
    if(t.kind==TK_FN){
        eat(l);
        Node *n=alloc_node(ND_FN_LIT);
        n->name[0]=0; /* anonymous */
        expect(l,TK_LPAREN);
        int nparams=0;
        while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&nparams<8){
            while(check(l,TK_NL)) eat(l);
            if(check(l,TK_RPAREN)) break;
            if(check(l,TK_IDENT)){
                Token p=eat(l);
                int pl=p.len<31?p.len:31;
                for(int i=0;i<pl;i++) { n->field_names[nparams][i]=p.start[i]; }
                n->field_names[nparams][pl]=0;
                nparams++;
                if(check(l,TK_COLON)){eat(l);eat(l);}
            } else { eat(l); }
            if(!match_tk(l,TK_COMMA)) break;
        }
        if(check(l,TK_RPAREN)) eat(l);
        if(check(l,TK_ARROW)){eat(l);eat(l);}
        n->argc=nparams;
        n->body=parse_block(l);
        return n;
    }

    /* @builtin call */
    if(t.kind==TK_AT){
        eat(l); Token nm=expect(l,TK_IDENT);
        Node *n=alloc_node(ND_CALL);
        n->name[0]='@';
        int nl=nm.len<62?nm.len:62;
        for(int i=0;i<nl;i++) n->name[i+1]=nm.start[i];
        n->name[nl+1]=0;
        expect(l,TK_LPAREN);
        static Node *args[16]; int argc=0;
        while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)){
            args[argc++]=parse_expr(l);
            if(!match_tk(l,TK_COMMA)) break;
        }
        expect(l,TK_RPAREN);
        n->args=(Node**)args; n->argc=argc; return n;
    }
    /* match as expression — allows: let x = match val { ... } */
    if(t.kind==TK_MATCH){
        eat(l);
        Node *n=alloc_node(ND_MATCH);
        n->cond=parse_expr(l);
        expect(l,TK_LBRACE);
        int arms=0;
        while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)&&arms<16){
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
            if(check(l,TK_RBRACE)) break;
            Node *pat=NULL;
            if(check(l,TK_IDENT)&&l->cur.len==1&&l->cur.start[0]=='_'){
                eat(l); pat=alloc_node(ND_IDENT);
                pat->name[0]='_'; pat->name[1]=0;
            } else {
                pat=parse_binop(l,0);
            }
            /* optional guard: if <expr> */
            Node *guard=NULL;
            if(check(l,TK_IF)){ eat(l); guard=parse_expr(l); }
            if(check(l,TK_FAT_ARROW)) eat(l);
            Node *body=NULL;
            if(check(l,TK_LBRACE)) body=parse_block(l);
            else body=parse_expr(l);
            /* wrap in ND_MATCH_ARM: left=pat, cond=guard, right=body */
            Node *arm=alloc_node(ND_MATCH_ARM);
            arm->left=pat; arm->cond=guard; arm->right=body;
            n->arg_data[arms]=arm;
            arms++;
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)||check(l,TK_COMMA)) eat(l);
        }
        expect(l,TK_RBRACE);
        n->args=n->arg_data;
        n->argc=arms;
        return n;
    }

    eat(l);
    return alloc_node(ND_INT); /* fallback */
}

static int prec(TokenKind k){
    switch(k){
    case TK_OR:  return 1;
    case TK_AND: return 2;
    case TK_PIPE:  return 3;  /* bitwise | */
    case TK_CARET: return 4;  /* bitwise ^ */
    case TK_AMP:   return 5;  /* bitwise & */
    case TK_EQEQ: case TK_NEQ: return 6;
    case TK_LT: case TK_GT: case TK_LTE: case TK_GTE: return 7;
    case TK_DOTDOT: return 7; /* range operator, same level as comparison */
    case TK_SHL: case TK_SHR: return 8; /* bitwise shifts */
    case TK_PLUS: case TK_MINUS: return 9;
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 10;
    default: return 0;
    }
}

static Node *parse_binop(Lexer *l, int min_prec){
    Node *left=parse_primary(l);
    while(1){
        int p=prec(cur(l).kind);
        if(p<=min_prec) break;
        Token op=eat(l);
        Node *right=parse_binop(l,p);
        Node *n=alloc_node(ND_BINOP);
        n->op=op.kind; n->left=left; n->right=right;
        left=n;
    }
    return left;
}

static Node *parse_expr(Lexer *l){
    Node *left=parse_binop(l,0);
    /* assignment */
    if(check(l,TK_EQ)){
        eat(l);
        Node *n=alloc_node(ND_ASSIGN);
        n->left=left; n->right=parse_expr(l);
        return n;
    }
    return left;
}


static Node *parse_block(Lexer *l){
    expect(l,TK_LBRACE);
    Node *block=alloc_node(ND_BLOCK);
    /* parse into local buffer first — nested blocks use stmt_pool during parse */
    Node *tmp[128]; int cnt=0;
    while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)){
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
        if(check(l,TK_RBRACE)) break;
        if(cnt<128) tmp[cnt++]=parse_stmt(l);
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
    }
    expect(l,TK_RBRACE);
    /* now copy to permanent pool — after all nested blocks already claimed their slots */
    Node **stmts=&stmt_pool[stmt_pool_idx];
    for(int i=0;i<cnt&&stmt_pool_idx<4095;i++) stmt_pool[stmt_pool_idx++]=tmp[i];
    block->stmts=stmts; block->stmtc=cnt;
    return block;
}



static Node *parse_stmt(Lexer *l){
    Token t=cur(l);
    /* let / var */
    if(t.kind==TK_LET||t.kind==TK_VAR){
        eat(l);
        Node *n=alloc_node(t.kind==TK_LET?ND_LET:ND_VAR);
        n->is_mut=(t.kind==TK_VAR);
        Token nm=expect(l,TK_IDENT);
        int nl=nm.len<63?nm.len:63;
        for(int i=0;i<nl;i++) n->name[i]=nm.start[i];
        n->name[nl]=0;
        /* optional type */
        if(match_tk(l,TK_COLON)){
            Token ty=eat(l);
            int tl=ty.len<31?ty.len:31;
            for(int i=0;i<tl;i++) n->type[i]=ty.start[i];
            n->type[tl]=0;
        }
        if(match_tk(l,TK_EQ)) n->right=parse_expr(l);
        return n;
    }

    /* if / else if / else */
    if(t.kind==TK_IF){
        eat(l); Node *n=alloc_node(ND_IF);
        n->cond=parse_expr(l);
        n->then=parse_block(l);
        if(check(l,TK_ELSE)){
            eat(l);
            if(check(l,TK_IF)) n->els=parse_stmt(l); /* else if → nested ND_IF */
            else                n->els=parse_block(l);
        }
        return n;
    }

    /* while */
    if(t.kind==TK_WHILE){
        eat(l); Node *n=alloc_node(ND_WHILE);
        n->cond=parse_expr(l); n->body=parse_block(l); return n;
    }

    /* for item in expr { } */
    if(t.kind==TK_FOR){
        eat(l); Node *n=alloc_node(ND_FOR);
        Token var=expect(l,TK_IDENT);
        int vl=var.len<63?var.len:63;
        for(int i=0;i<vl;i++) { n->name[i]=var.start[i]; } n->name[vl]=0;
        expect(l,TK_IN);
        n->cond=parse_expr(l); /* iterable: array or a..b range binop */
        n->body=parse_block(l);
        return n;
    }

    /* return */
    if(t.kind==TK_RETURN){
        eat(l); Node *n=alloc_node(ND_RETURN);
        if(!check(l,TK_NL)&&!check(l,TK_SEMICOLON)&&!check(l,TK_RBRACE))
            n->right=parse_expr(l);
        return n;
    }

    /* struct Point { x, y } */

    /* v2.1: test "description" { ... } */
    if(t.kind==TK_TEST){
        eat(l);
        Node *n=alloc_node(ND_TEST);
        n->line=t.line; n->column=t.column;
        /* description string */
        if(check(l,TK_STR)){
            Token dt=eat(l);
            node_set_sval(n, dt.start, dt.len);
        }
        /* test body block */
        n->body=parse_block(l);
        return n;
    }
    /* v2.2: enum Foo { A  B  C } */
    if(t.kind==TK_ENUM){
        eat(l);
        Node *n=alloc_node(ND_ENUM);
        Token nm=expect(l,TK_IDENT);
        int nl=nm.len<63?nm.len:63;
        for(int i=0;i<nl;i++){n->name[i]=nm.start[i];} n->name[nl]=0;
        expect(l,TK_LBRACE);
        Node **variants=alloc_stmts(32); int vc=0;
        while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)&&vc<32){
            while(check(l,TK_NL)||check(l,TK_COMMA)||check(l,TK_SEMICOLON)) eat(l);
            if(check(l,TK_RBRACE)) break;
            Token vt=expect(l,TK_IDENT);
            Node *v=alloc_node(ND_IDENT);
            int vl=vt.len<63?vt.len:63;
            for(int i=0;i<vl;i++){v->name[i]=vt.start[i];} v->name[vl]=0;
            v->ival=vc;
            variants[vc++]=v;
        }
        expect(l,TK_RBRACE);
        n->stmts=variants; n->stmtc=vc;
        return n;
    }
    if(t.kind==TK_STRUCT){
        eat(l);
        Node *n=alloc_node(ND_STRUCT);
        Token nm=expect(l,TK_IDENT);
        int nl=nm.len<63?nm.len:63;
        for(int i=0;i<nl;i++){n->name[i]=nm.start[i];} n->name[nl]=0;
        expect(l,TK_LBRACE);
        static Node *fields[16]; int fc=0;
        while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)){
            while(check(l,TK_NL)||check(l,TK_COMMA)) eat(l);
            if(check(l,TK_RBRACE)) break;
            Token fn=expect(l,TK_IDENT);
            Node *fld=alloc_node(ND_IDENT);
            int fl=fn.len<63?fn.len:63;
            for(int i=0;i<fl;i++){fld->name[i]=fn.start[i];} fld->name[fl]=0;
            if(check(l,TK_COMMA)) eat(l);
            fields[fc++]=fld;
        }
        if(check(l,TK_RBRACE)) eat(l);
        n->stmts=(Node**)fields; n->stmtc=fc;
        return n;
    }
    /* @intent("arg") / @audit("arg") annotation before fn */
    if(t.kind==TK_AT){
        eat(l);
        Token ann=expect(l,TK_IDENT);
        /* annotation name into a temp buffer */
        char ann_name[32]; int al=ann.len<31?ann.len:31;
        for(int i=0;i<al;i++) { ann_name[i]=ann.start[i]; } ann_name[al]=0;
        /* parse optional ("arg") */
        char ann_arg[64]; ann_arg[0]=0;
        if(check(l,TK_LPAREN)){
            eat(l);
            if(check(l,TK_STR)){
                Token av=eat(l);
                int vl=av.len<63?av.len:63;
                for(int i=0;i<vl;i++) { ann_arg[i]=av.start[i]; } ann_arg[vl]=0;
            }
            if(check(l,TK_RPAREN)) eat(l);
        }
        while(check(l,TK_NL)) eat(l);
        /* next statement must be fn */
        if(!check(l,TK_FN)){
            ys_print("[YS] annotation must precede fn\n");
            return alloc_node(ND_INT);
        }
        eat(l); /* eat TK_FN */
        Node *n=alloc_node(ND_FN);
        /* copy annotation into node */
        for(int i=0;ann_name[i]&&i<31;i++) n->type[i]=ann_name[i];
        n->type[31]=0;
        node_set_sval(n, ann_arg, (int)strlen(ann_arg));
        /* parse rest of fn (name, params, body) */
        Token nm=expect(l,TK_IDENT);
        int nl2=nm.len<63?nm.len:63;
        for(int i=0;i<nl2;i++) { n->name[i]=nm.start[i]; } n->name[nl2]=0;
        expect(l,TK_LPAREN);
        int nparams=0;
        while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&nparams<8){
            while(check(l,TK_NL)) eat(l);
            if(check(l,TK_RPAREN)) break;
            if(check(l,TK_IDENT)){
                Token p=eat(l);
                int pl=p.len<31?p.len:31;
                for(int i=0;i<pl;i++) n->field_names[nparams][i]=p.start[i];
                n->field_names[nparams][pl]=0;
                nparams++;
                if(check(l,TK_COLON)){eat(l);eat(l);}
            } else { eat(l); }
            if(!match_tk(l,TK_COMMA)) break;
        }
        n->argc=nparams;
        expect(l,TK_RPAREN);
        if(check(l,TK_ARROW)){eat(l);eat(l);}
        n->body=parse_block(l);
        return n;
    }

    /* fn */
    if(t.kind==TK_FN){
        eat(l); Node *n=alloc_node(ND_FN);
        Token nm=expect(l,TK_IDENT);
        int nl=nm.len<63?nm.len:63;
        for(int i=0;i<nl;i++) n->name[i]=nm.start[i];
        n->name[nl]=0;
        expect(l,TK_LPAREN);
        /* parse parameter names into field_names[] */
        int nparams=0;
        while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&nparams<8){
            while(check(l,TK_NL)) eat(l);
            if(check(l,TK_RPAREN)) break;
            if(check(l,TK_IDENT)){
                Token p=eat(l);
                int pl=p.len<31?p.len:31;
                for(int i=0;i<pl;i++) n->field_names[nparams][i]=p.start[i];
                n->field_names[nparams][pl]=0;
                nparams++;
                /* skip optional type annotation: a: str */
                if(check(l,TK_COLON)){eat(l);eat(l);}
            } else {
                eat(l);
            }
            if(!match_tk(l,TK_COMMA)) break;
        }
        n->argc=nparams;
        expect(l,TK_RPAREN);
        if(check(l,TK_ARROW)){eat(l);eat(l);} /* skip return type */
        n->body=parse_block(l);
        return n;
    }
    
    /* match x { pat [if guard] => body, ... } */
    if(t.kind==TK_MATCH){
        eat(l);
        Node *n=alloc_node(ND_MATCH);
        n->cond=parse_expr(l);
        expect(l,TK_LBRACE);
        int arms=0;
        while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)&&arms<16){
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
            if(check(l,TK_RBRACE)) break;
            Node *pat=NULL;
            if(check(l,TK_IDENT)&&l->cur.len==1&&l->cur.start[0]=='_'){
                eat(l); pat=alloc_node(ND_IDENT);
                pat->name[0]='_'; pat->name[1]=0;
            } else {
                pat=parse_binop(l,0);
            }
            /* optional guard: if <expr> */
            Node *guard=NULL;
            if(check(l,TK_IF)){ eat(l); guard=parse_expr(l); }
            if(check(l,TK_FAT_ARROW)) eat(l);
            Node *body=NULL;
            if(check(l,TK_LBRACE)) body=parse_block(l);
            else body=parse_expr(l);
            Node *arm=alloc_node(ND_MATCH_ARM);
            arm->left=pat; arm->cond=guard; arm->right=body;
            n->arg_data[arms]=arm;
            arms++;
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)||check(l,TK_COMMA)) eat(l);
        }
        expect(l,TK_RBRACE);
        n->args=n->arg_data;
        n->argc=arms;
        return n;
    }

    /* impl StructName { fn method(self, ...) { } } */
    if(t.kind==TK_IMPL){
        eat(l);
        Node *n=alloc_node(ND_IMPL);
        Token nm=expect(l,TK_IDENT);
        int nl=nm.len<63?nm.len:63;
        for(int i=0;i<nl;i++) n->name[i]=nm.start[i];
        n->name[nl]=0;
        expect(l,TK_LBRACE);
        /* parse methods into stmt_pool */
        Node *tmp[32]; int cnt=0;
        while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)){
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
            if(check(l,TK_RBRACE)) break;
            if(cnt<32) tmp[cnt++]=parse_stmt(l);
            while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
        }
        expect(l,TK_RBRACE);
        Node **stmts=&stmt_pool[stmt_pool_idx];
        for(int i=0;i<cnt&&stmt_pool_idx<4095;i++) stmt_pool[stmt_pool_idx++]=tmp[i];
        n->stmts=stmts; n->stmtc=cnt;
        return n;
    }

    /* throw expr */
    if(t.kind==TK_THROW){
        eat(l);
        Node *n=alloc_node(ND_THROW);
        n->right=parse_expr(l);
        return n;
    }

    /* break */
    if(t.kind==TK_BREAK){
        eat(l);
        return alloc_node(ND_BREAK);
    }

    /* continue */
    if(t.kind==TK_CONTINUE){
        eat(l);
        return alloc_node(ND_CONTINUE);
    }

    /* try { } catch(e) { } */
    if(t.kind==TK_TRY){
        eat(l);
        Node *n=alloc_node(ND_TRY);
        n->then=parse_block(l);   /* try body */
        n->name[0]=0;
        if(check(l,TK_CATCH)){
            eat(l);
            /* optional (varname) */
            if(check(l,TK_LPAREN)){
                eat(l);
                if(check(l,TK_IDENT)){
                    Token vt=eat(l);
                    int vl=vt.len<63?vt.len:63;
                    for(int i=0;i<vl;i++) { n->name[i]=vt.start[i]; }
                    n->name[vl]=0;
                }
                if(check(l,TK_RPAREN)) eat(l);
            }
            n->els=parse_block(l); /* catch body */
        }
        return n;
    }

    /* import "file.y" [as name] */
    if(t.kind==TK_IMPORT){
        eat(l);
        Node *n=alloc_node(ND_IMPORT);
        if(check(l,TK_STR)){
            Token pt=eat(l);
            node_set_sval(n, pt.start, pt.len);
        }
        /* optional: as namespace_name → ND_MODULE */
        if(check(l,TK_AS)){
            eat(l);
            Token nm=expect(l,TK_IDENT);
            n->kind=ND_MODULE;
            int nl=nm.len<63?nm.len:63;
            for(int i=0;i<nl;i++) n->name[i]=nm.start[i];
            n->name[nl]=0;
        }
        return n;
    }

    /* pragma / comment skip */
    if(t.kind==TK_IDENT&&t.start[0]=='#'){
        while(!check(l,TK_NL)&&!check(l,TK_EOF)) eat(l);
        return alloc_node(ND_INT);
    }
    return parse_expr(l);
}

Node *parse_program(Lexer *l){
    Node *prog=alloc_node(ND_BLOCK);
    Node *tmp[256]; int cnt=0;
    while(!check(l,TK_EOF)){
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
        if(check(l,TK_EOF)) break;
        if(cnt<256) tmp[cnt++]=parse_stmt(l);
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
    }
    Node **stmts=&stmt_pool[stmt_pool_idx];
    for(int i=0;i<cnt&&stmt_pool_idx<4095;i++) stmt_pool[stmt_pool_idx++]=tmp[i];
    prog->stmts=stmts; prog->stmtc=cnt;
    return prog;
}
/* Pool checkpoint for sub-parses (e.g. string interpolation) */
/* v1.9: chunk allocator — save/restore just tracks stmt_pool for interp strings */
static int pool_save_idx=0;
static int stmt_pool_save_idx=0;
void parser_pool_save(void){
    pool_save_idx=pool_idx;
    stmt_pool_save_idx=stmt_pool_idx;
}
void parser_pool_restore(void){
    pool_idx=pool_save_idx;
    stmt_pool_idx=stmt_pool_save_idx;
}