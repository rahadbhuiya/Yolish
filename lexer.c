#include "yolish.h"

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
        static char strbuf[8192]; int slen=0;
        while(*p<end&&s[*p]!='"'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            if(s[*p]=='\\'&&*p+1<end){
                (*p)++;
                if(slen<8189){
                    if(s[*p]=='n')      strbuf[slen++]='\n';
                    else if(s[*p]=='t') strbuf[slen++]='\t';
                    else if(s[*p]=='r') strbuf[slen++]='\r';
                    else                strbuf[slen++]=s[*p];
                }
            } else { if(slen<8190) strbuf[slen++]=s[*p]; }
            (*p)++;
        }
        strbuf[slen]=0;
        t.kind=TK_STR; t.start=strbuf; t.len=slen;
        if(*p<end)(*p)++;
        return t;
    }

    /* raw string r"..." */
    if(s[*p]=='r' && *p+1<end && s[*p+1]=='"'){
        (*p)+=2;
        static char rawbuf[8193]; int rlen=0;
        rawbuf[rlen++]='\x01';
        while(*p<end&&s[*p]!='"'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            if(rlen<8191) rawbuf[rlen++]=s[*p];
            (*p)++;
        }
        rawbuf[rlen]=0;
        t.kind=TK_STR; t.start=rawbuf; t.len=rlen;
        if(*p<end)(*p)++;
        return t;
    }

    /* multiline string `...` — raw, no interpolation */
    if(s[*p]=='`'){
        (*p)++;
        static char mlbuf[8192]; int mlen=0;
        mlbuf[mlen++]='\x01'; /* raw string sentinel: skip interpolation */
        while(*p<end&&s[*p]!='`'){
            if(s[*p]=='\n'){ l->line++; l->line_start=*p+1; }
            if(mlen<8190) mlbuf[mlen++]=s[*p];
            (*p)++;
        }
        mlbuf[mlen]=0;
        t.kind=TK_STR; t.start=mlbuf; t.len=mlen;
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