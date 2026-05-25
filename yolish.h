#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Platform  */
static inline void ys_puts(const char *s){ fputs(s,stdout); fflush(stdout); }
#define puts(s) ys_puts(s)
static inline void ys_print(const char *s){ fputs(s,stdout); fflush(stdout); }
static inline int strcmp_u(const char *a,const char *b){ return strcmp(a,b); }
static inline int str_len_u(const char *s){ int n=0; while(s[n])n++; return n; }

/*  Capability permissions  */
#define CAP_READ  1
#define CAP_WRITE 2
#define CAP_EXEC  4

/*  Token types  */
typedef enum {
    TK_EOF, TK_NL,
    TK_INT, TK_FLOAT, TK_STR, TK_IDENT, TK_BOOL,
    TK_FN, TK_LET, TK_VAR, TK_IF, TK_ELSE, TK_WHILE,
    TK_FOR, TK_IN, TK_RETURN, TK_STRUCT, TK_IMPL,
    TK_MATCH, TK_CAP, TK_UNSAFE, TK_TRUE, TK_FALSE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_EQ, TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LTE, TK_GTE,
    TK_AND, TK_OR, TK_NOT, TK_ARROW, TK_DOTDOT, TK_DOT,
    TK_AMP, TK_PIPE, TK_CARET, TK_BANG,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_COLON,
    TK_SEMICOLON, TK_AT,
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *start;
    int         len;
    int64_t     ival;
    int64_t     fval;
} Token;

/*  AST node types  */
typedef enum {
    ND_INT, ND_FLOAT, ND_STR, ND_BOOL, ND_IDENT,
    ND_BINOP, ND_UNOP, ND_ASSIGN, ND_LET, ND_VAR,
    ND_CALL, ND_DOT, ND_INDEX,
    ND_IF, ND_WHILE, ND_FOR, ND_RETURN, ND_BLOCK,
    ND_FN, ND_STRUCT, ND_MATCH,
} NodeKind;

typedef struct Node Node;
struct Node {
    NodeKind  kind;
    int64_t   ival;
    int64_t   fval;
    char      sval[256];
    int       op;
    Node     *left;
    Node     *right;
    Node     *cond;
    Node     *then;
    Node     *els;
    Node     *body;
    Node     *arg_data[8];
    Node    **args;
    int       argc;
    Node    **stmts;
    int       stmtc;
    char      name[64];
    char      type[32];
    int       is_mut;
};

/*  Value types  */
typedef enum {
    VT_NIL, VT_INT, VT_FLOAT, VT_BOOL, VT_STR, VT_FN, VT_CAP
} ValType;

typedef struct {
    ValType  type;
    int64_t  ival;
    int64_t  fval;
    int      bval;
    char     sval[512];
    Node    *fn_node;
    /* capability fields */
    int64_t  cap_fd;
    int      cap_perm;
    char     cap_path[256];
} Val;

/*  Environment  */
#define ENV_MAX 32
typedef struct Env Env;
struct Env {
    char  names[ENV_MAX][64];
    Val   vals [ENV_MAX];
    int   count;
    Env  *parent;
};

/*  Lexer  */
typedef struct {
    const char *src;
    int         pos;
    int         len;
    Token       cur;
} Lexer;

/*  Function prototypes  */
void  lex_init    (Lexer *l, const char *src, int len);
Token lex_next    (Lexer *l);

Node *parse_program(Lexer *l);

Val   eval_program(Node *prog, Env *env);
Val   eval_node   (Node *n,    Env *env);

Env  *env_new(Env *parent);
Val  *env_get(Env *e, const char *name);
void  env_set(Env *e, const char *name, Val v);
void  env_def(Env *e, const char *name, Val v);
void  ys_print_val(Val v);
