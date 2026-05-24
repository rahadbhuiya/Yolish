#include "yolish.h"

/* Forward declarations */
static Node *parse_expr(Lexer *l);
static Node *parse_stmt(Lexer *l);

/* simple allocator using stack-like bump */
static Node pool[512];
static int pool_idx=0;
static Node *alloc_node(NodeKind k){
    if(pool_idx>=512) return pool; /* out of nodes */
    Node *n=&pool[pool_idx++];
    for(int i=0;i<(int)sizeof(Node);i++) ((char*)n)[i]=0;
    n->kind=k;
    return n;
}

static Token cur(Lexer *l){ return l->cur; }
static Token eat(Lexer *l){
    Token t=l->cur;
    l->cur=lex_next(l);
    while(l->cur.kind==TK_NL) l->cur=lex_next(l);
    return t;
}
static int check(Lexer *l, TokenKind k){ return l->cur.kind==k; }
static Token expect(Lexer *l, TokenKind k){
    if(!check(l,k)){ ys_print("[YS] parse error\n"); }
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
        int len=t.len<255?t.len:255;
        for(int i=0;i<len;i++) n->sval[i]=t.start[i];
        n->sval[len]=0;
        eat(l);
        return n;
    }
    if(t.kind==TK_IDENT){
        eat(l); Node*n=alloc_node(ND_IDENT);
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
            call->args=call->arg_data; call->argc=argc;
            return call;
        }
        /* dot access */
        if(check(l,TK_DOT)){
            eat(l);
            Token m=expect(l,TK_IDENT);
            Node *dot=alloc_node(ND_DOT);
            dot->left=n;
            int ml=m.len<63?m.len:63;
            for(int i=0;i<ml;i++) dot->name[i]=m.start[i];
            dot->name[ml]=0;
            /* method call */
            if(check(l,TK_LPAREN)){
                eat(l); dot->kind=ND_CALL;
                dot->arg_data[0]=n; int argc=1;
                while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)&&argc<8){
                    dot->arg_data[argc++]=parse_expr(l);
                    if(!match_tk(l,TK_COMMA)) break;
                }
                expect(l,TK_RPAREN);
                dot->args=dot->arg_data; dot->argc=argc;
            }
            return dot;
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
    eat(l);
    return alloc_node(ND_INT); /* fallback */
}

static int prec(TokenKind k){
    switch(k){
    case TK_OR:  return 1;
    case TK_AND: return 2;
    case TK_EQEQ: case TK_NEQ: return 3;
    case TK_LT: case TK_GT: case TK_LTE: case TK_GTE: return 4;
    case TK_PLUS: case TK_MINUS: return 5;
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 6;
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
    static Node *stmts[512]; int cnt=0;
    while(!check(l,TK_RBRACE)&&!check(l,TK_EOF)){
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
        if(check(l,TK_RBRACE)) break;
        stmts[cnt++]=parse_stmt(l);
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
    }

    expect(l,TK_RBRACE);
    block->stmts=(Node**)stmts; block->stmtc=cnt;
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

    /* if */
    if(t.kind==TK_IF){
        eat(l); Node *n=alloc_node(ND_IF);
        n->cond=parse_expr(l);
        n->then=parse_block(l);
        if(check(l,TK_ELSE)){eat(l); n->els=parse_block(l);}
        return n;
    }

    /* while */
    if(t.kind==TK_WHILE){
        eat(l); Node *n=alloc_node(ND_WHILE);
        n->cond=parse_expr(l); n->body=parse_block(l); return n;
    }

    /* return */
    if(t.kind==TK_RETURN){
        eat(l); Node *n=alloc_node(ND_RETURN);
        if(!check(l,TK_NL)&&!check(l,TK_SEMICOLON)&&!check(l,TK_RBRACE))
            n->right=parse_expr(l);
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
        /* skip params for now */
        while(!check(l,TK_RPAREN)&&!check(l,TK_EOF)) eat(l);
        expect(l,TK_RPAREN);
        if(check(l,TK_ARROW)){eat(l);eat(l);} /* skip return type */
        n->body=parse_block(l);
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
    static Node *stmts[512]; int cnt=0;
    while(!check(l,TK_EOF)){
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
        if(check(l,TK_EOF)) break;
        stmts[cnt++]=parse_stmt(l);
        while(check(l,TK_NL)||check(l,TK_SEMICOLON)) eat(l);
    }
    prog->stmts=(Node**)stmts; prog->stmtc=cnt;
    return prog;
}
