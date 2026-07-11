#include "yolish.h"


/* v2.4: dynamic growable buffer shared by all string-literal lexing paths
   (regular "...", raw r"...", and multiline `...`). Replaces three fixed
   8192-byte static buffers, removing the source-literal length cap.
   Safe to share a single buffer because the lexer only ever has one
   string token "in flight" at a time — the parser copies Token.start
   into the AST node (node_set_sval) before the next lex_next() call. */
static char *s_growbuf = NULL;
static int   s_growcap = 0;

static void s_grow_ensure(int need){
    if(s_growcap >= need) return;
    int newcap = s_growcap ? s_growcap*2 : 4096;
    while(newcap < need) newcap *= 2;
    char *nb = (char*)realloc(s_growbuf, (size_t)newcap);
    if(nb){ s_growbuf = nb; s_growcap = newcap; }
}

static int is_alpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
static int is_digit(char c){return c>='0'&&c<='9';}
static int is_alnum(char c){return is_alpha(c)||is_digit(c);}

static int streq(const char *a, int n, const char *b){
    int i=0; while(i<n&&b[i]&&a[i]==b[i])i++;
    return i==n&&!b[i];
}

static Token make_kw_or_ident(const char *s, int n){
    Token t; t.start=s; t.len=n; t.ival=0; t.fval=0; t.column=0;
    if(streq(s,n,"fn"))      {t.kind=TK_FN;       return t;}
    if(streq(s,n,"let"))     {t.kind=TK_LET;      return t;}
    if(streq(s,n,"var"))     {t.kind=TK_VAR;      return t;}
    if(streq(s,n,"if"))      {t.kind=TK_IF;       return t;}
    if(streq(s,n,"else"))    {t.kind=TK_ELSE;     return t;}
    if(streq(s,n,"while"))   {t.kind=TK_WHILE;    return t;}
    if(streq(s,n,"for"))     {t.kind=TK_FOR;      return t;}
    if(streq(s,n,"in"))      {t.kind=TK_IN;       return t;}
    if(streq(s,n,"return"))  {t.kind=TK_RETURN;   return t;}
    if(streq(s,n,"struct"))  {t.kind=TK_STRUCT;   return t;}
    if(streq(s,n,"impl"))    {t.kind=TK_IMPL;     return t;}
    if(streq(s,n,"match"))   {t.kind=TK_MATCH;    return t;}
    if(streq(s,n,"import"))  {t.kind=TK_IMPORT;   return t;}
    if(streq(s,n,"try"))     {t.kind=TK_TRY;      return t;}
    if(streq(s,n,"catch"))   {t.kind=TK_CATCH;    return t;}
    if(streq(s,n,"throw"))   {t.kind=TK_THROW;    return t;}
    if(streq(s,n,"as"))      {t.kind=TK_AS;       return t;}
    if(streq(s,n,"break"))   {t.kind=TK_BREAK;    return t;}
    if(streq(s,n,"continue")){t.kind=TK_CONTINUE; return t;}
    if(streq(s,n,"enum"))    {t.kind=TK_ENUM;     return t;}
    if(streq(s,n,"test"))    {t.kind=TK_TEST;     return t;}
    if(streq(s,n,"true"))    {t.kind=TK_TRUE;  t.ival=1; return t;}
    if(streq(s,n,"false"))   {t.kind=TK_FALSE; t.ival=0; return t;}
    t.kind=TK_IDENT; return t;
}

Token lex_next(Lexer *l){
    const char *s=l->src; int *p=&l->pos; int end=l->len;
    Token t; t.ival=0; t.fval=0;

    /* skip whitespace (not newline) and comments */
    while(*p<end && (s[*p]==' '||s[*p]=='\t'||s[*p]=='\r')) (*p)++;
    if(*p<end && s[*p]=='-' && *p+1<end && s[*p+1]=='-'){
        while(*p<end && s[*p]!='\n') (*p)++;
    }

    /* v1.4: record column AFTER skipping whitespace */
    t.line   = l->line;
    t.column = (*p - l->line_start) + 1; /* 1-based */

    if(*p>=end){t.kind=TK_EOF;t.start=s+*p;t.len=0;return t;}

    t.start=s+*p;

    /* newline */
    if(s[*p]=='\n'){
        t.kind=TK_NL; t.len=1; (*p)++;
        l->line++;
        l->line_start=*p;   /* v1.4: update line_start */
        t.line=l->line;
        t.column=1;
        return t;
    }

    /* number */
    if(is_digit(s[*p])){
        int64_t v=0; int start=*p;
        while(*p<end&&is_digit(s[*p])){v=v*10+(s[*p]-'0');(*p)++;}
        if(*p<end&&s[*p]=='.'&&(*p+1>=end||s[*p+1]!='.')&&(*p+1<end&&s[*p+1]>='0'&&s[*p+1]<='9')){
            (*p)++; double frac=0.0, div=1.0;
            while(*p<end&&is_digit(s[*p])){frac=frac*10+(s[*p]-'0');div*=10;(*p)++;}
            t.kind=TK_FLOAT; t.fval=(double)v + frac/div;
        } else { t.kind=TK_INT; t.ival=v; }
        t.len=*p-start; return t;
    }

    /* string: "..." */
    if(s[*p]=='"'){
        (*p)++;
        /* v2.4: dynamic — grow as needed, no length cap */
        int cap_guess = (end - *p) + 16;
        s_grow_ensure(cap_guess);
        int slen=0;
        while(*p<end&&s[*p]!='"'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            s_grow_ensure(slen+4);
            if(s[*p]=='\\'&&*p+1<end){
                (*p)++;
                if(s[*p]=='n')      s_growbuf[slen++]='\n';
                else if(s[*p]=='t') s_growbuf[slen++]='\t';
                else if(s[*p]=='r') s_growbuf[slen++]='\r';
                else                s_growbuf[slen++]=s[*p];
            } else { s_growbuf[slen++]=s[*p]; }
            (*p)++;
        }
        s_grow_ensure(slen+1);
        s_growbuf[slen]=0;
        t.kind=TK_STR; t.start=s_growbuf; t.len=slen;
        if(*p<end)(*p)++;
        return t;
    }

    /* raw string r"..." */
    if(s[*p]=='r' && *p+1<end && s[*p+1]=='"'){
        (*p)+=2;
        int cap_guess = (end - *p) + 16;
        s_grow_ensure(cap_guess);
        int rlen=0;
        s_growbuf[rlen++]='\x01';
        while(*p<end&&s[*p]!='"'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            s_grow_ensure(rlen+4);
            s_growbuf[rlen++]=s[*p];
            (*p)++;
        }
        s_grow_ensure(rlen+1);
        s_growbuf[rlen]=0;
        t.kind=TK_STR; t.start=s_growbuf; t.len=rlen;
        if(*p<end)(*p)++;
        return t;
    }

    /* multiline string `...` — raw, no interpolation */
    if(s[*p]=='`'){
        (*p)++;
        int cap_guess = (end - *p) + 16;
        s_grow_ensure(cap_guess);
        int mlen=0;
        s_growbuf[mlen++]='\x01'; /* raw string sentinel: skip interpolation */
        while(*p<end&&s[*p]!='`'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            s_grow_ensure(mlen+4);
            s_growbuf[mlen++]=s[*p];
            (*p)++;
        }
        s_grow_ensure(mlen+1);
        s_growbuf[mlen]=0;
        t.kind=TK_STR; t.start=s_growbuf; t.len=mlen;
        if(*p<end)(*p)++;
        return t;
    }

    /* identifier / keyword */
    if(is_alpha(s[*p])){
        int start=*p;
        while(*p<end&&is_alnum(s[*p]))(*p)++;
        t=make_kw_or_ident(s+start,*p-start);
        t.start=s+start;
        t.line=l->line;
        t.column=(start - l->line_start) + 1; /* v1.4 */
        return t;
    }

    /* two-char operators */
    char c=s[*p]; (*p)++;
    t.len=1;
    if(c=='='&&*p<end&&s[*p]=='='){t.kind=TK_EQEQ;     t.len=2;(*p)++;return t;}
    if(c=='='&&*p<end&&s[*p]=='>'){t.kind=TK_FAT_ARROW; t.len=2;(*p)++;return t;}
    if(c=='!'&&*p<end&&s[*p]=='='){t.kind=TK_NEQ;       t.len=2;(*p)++;return t;}
    if(c=='<'&&*p<end&&s[*p]=='='){t.kind=TK_LTE;       t.len=2;(*p)++;return t;}
    if(c=='>'&&*p<end&&s[*p]=='='){t.kind=TK_GTE;       t.len=2;(*p)++;return t;}
    if(c=='-'&&*p<end&&s[*p]=='>'){t.kind=TK_ARROW;     t.len=2;(*p)++;return t;}
    if(c=='.'&&*p<end&&s[*p]=='.'){t.kind=TK_DOTDOT;    t.len=2;(*p)++;return t;}
    if(c=='&'&&*p<end&&s[*p]=='&'){t.kind=TK_AND;       t.len=2;(*p)++;return t;}
    if(c=='|'&&*p<end&&s[*p]=='|'){t.kind=TK_OR;        t.len=2;(*p)++;return t;}
    if(c=='<'&&*p<end&&s[*p]=='<'){t.kind=TK_SHL;       t.len=2;(*p)++;return t;}
    if(c=='>'&&*p<end&&s[*p]=='>'){t.kind=TK_SHR;       t.len=2;(*p)++;return t;}

    switch(c){
    case '+': t.kind=TK_PLUS;      break;
    case '-': t.kind=TK_MINUS;     break;
    case '*': t.kind=TK_STAR;      break;
    case '/': t.kind=TK_SLASH;     break;
    case '%': t.kind=TK_PERCENT;   break;
    case '=': t.kind=TK_EQ;        break;
    case '<': t.kind=TK_LT;        break;
    case '>': t.kind=TK_GT;        break;
    case '!': t.kind=TK_BANG;      break;
    case '.': t.kind=TK_DOT;       break;
    case '@': t.kind=TK_AT;        break;
    case '(': t.kind=TK_LPAREN;    break;
    case ')': t.kind=TK_RPAREN;    break;
    case '{': t.kind=TK_LBRACE;    break;
    case '}': t.kind=TK_RBRACE;    break;
    case '[': t.kind=TK_LBRACKET;  break;
    case ']': t.kind=TK_RBRACKET;  break;
    case ',': t.kind=TK_COMMA;     break;
    case ':': t.kind=TK_COLON;     break;
    case ';': t.kind=TK_SEMICOLON; break;
    case '&': t.kind=TK_AMP;       break;
    case '|': t.kind=TK_PIPE;      break;
    case '^': t.kind=TK_CARET;     break;
    case '~': t.kind=TK_TILDE;     break;
    default:  t.kind=TK_EOF;       break;
    }
    return t;
}

void lex_init(Lexer *l, const char *src, int len){
    l->src=src; l->pos=0; l->len=len;
    l->line=1; l->column=1; l->line_start=0; /* v1.4 */
    l->cur=lex_next(l);
}

Token lex_peek(Lexer *l){ return l->cur; }

Token lex_next_tok(Lexer *l){
    Token t=l->cur;
    l->cur=lex_next(l);
    while(l->cur.kind==TK_NL) l->cur=lex_next(l);
    return t;
}