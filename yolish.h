#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform */
static inline void ys_puts(const char *s){ fputs(s,stdout); fflush(stdout); }
#define puts(s) ys_puts(s)
static inline void ys_print(const char *s){ fputs(s,stdout); fflush(stdout); }
static inline int strcmp_u(const char *a,const char *b){ return strcmp(a,b); }
static inline int str_len_u(const char *s){ int n=0; while(s[n])n++; return n; }

/* Capability permissions */
#define CAP_READ  1
#define CAP_WRITE 2
#define CAP_EXEC  4

/* Token types */
typedef enum {
    TK_EOF, TK_NL,
    TK_INT, TK_FLOAT, TK_STR, TK_IDENT, TK_BOOL,
    TK_FN, TK_LET, TK_VAR, TK_IF, TK_ELSE, TK_WHILE,
    TK_FOR, TK_IN, TK_RETURN, TK_STRUCT, TK_IMPL,
    TK_MATCH, TK_UNSAFE, TK_TRUE, TK_FALSE, TK_IMPORT,
    TK_TRY, TK_CATCH, TK_THROW, TK_AS,
    TK_BREAK, TK_CONTINUE, TK_ENUM, TK_TEST,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_EQ, TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LTE, TK_GTE,
    TK_AND, TK_OR, TK_NOT, TK_ARROW, TK_FAT_ARROW, TK_DOTDOT, TK_DOT,
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
    double      fval;
    int         line;
    int         column; /* v1.4: column number (1-based) */
} Token;

/* AST node types */
typedef enum {
    ND_INT, ND_FLOAT, ND_STR, ND_BOOL, ND_IDENT,
    ND_BINOP, ND_UNOP, ND_ASSIGN, ND_LET, ND_VAR,
    ND_CALL, ND_DOT, ND_INDEX, ND_INDEX_SET,
    ND_IF, ND_WHILE, ND_FOR, ND_RETURN, ND_BLOCK,
    ND_FN, ND_FN_LIT, ND_STRUCT, ND_STRUCT_LIT, ND_MATCH, ND_IMPORT,
    ND_TRY, ND_THROW, ND_MODULE,
    ND_ARRAY,
    ND_BREAK, ND_CONTINUE,
    ND_IMPL,
    ND_MATCH_ARM,
    ND_ENUM,  /* v2.2: enum declaration */
    ND_TEST,  /* v2.1: test block */
} NodeKind;

typedef struct Node Node;
struct Node {
    NodeKind  kind;
    int64_t   ival;
    double    fval;
    char      sval[8192];
    int       op;
    Node     *left;
    Node     *right;
    Node     *cond;
    Node     *then;
    Node     *els;
    Node     *body;
    Node     *arg_data[16];
    Node    **args;
    int       argc;
    Node    **stmts;
    int       stmtc;
    char      name[64];
    char      type[32];
    int       is_mut;
    char      field_names[8][32];
    int       line;   /* v1.4: source line for error messages */
    int       column; /* v1.4: source column for error messages */
};

/* Value forward declaration */
typedef struct Val Val;

/* Array storage */
#define MAX_ARR 512
struct Val {
    int      type;
    int64_t  ival;
    double   fval;
    int      bval;
    char     sval[8192];
    Node    *fn_node;
    void    *fn_env;
    int64_t  cap_fd;
    int      cap_perm;
    char     cap_path[128];
    Val     *arr_data;
    int      arr_len;
    char     struct_name[32];
    Val     *field_vals;
    char   (*field_names)[32];
    int      field_count;
};

/* Value type constants */
#define YS_NIL    0
#define YS_INT    1
#define YS_FLOAT  2
#define YS_BOOL   3
#define YS_STR    4
#define YS_FN     5
#define YS_CAP    6
#define YS_ARR    7
#define YS_STRUCT 8
#define YS_ERR    9

/* Environment */
#define ENV_MAX 48
typedef struct Env Env;
struct Env {
    char  names[ENV_MAX][64];
    Val   vals [ENV_MAX];
    int   count;
    Env  *parent;
};

/* Lexer */
typedef struct {
    const char *src;
    int         pos;
    int         len;
    Token       cur;
    int         line;
    int         column;      /* v1.4: current column (1-based) */
    int         line_start;  /* v1.4: byte offset of current line start */
} Lexer;

/* Function prototypes */
void  lex_init    (Lexer *l, const char *src, int len);
Token lex_next    (Lexer *l);
Node *parse_program(Lexer *l);
Val   eval_program(Node *prog, Env *env);
Val   eval_node   (Node *n,    Env *env);
Env  *env_new(Env *parent);
Val  *env_get(Env *e, const char *name);
void  env_set(Env *e, const char *name, Val v);
void  env_def(Env *e, const char *name, Val v);
void  env_free(Env *e);
void  ys_print_val(Val v);
void  ys_error(int line, int column, const char *msg);
void  parser_pool_save(void);
void  parser_pool_restore(void);
extern char g_src_dir[512];
/* compiler target — defined in compiler.c */
typedef enum { TARGET_LINUX, TARGET_WINDOWS, TARGET_MACOS } Target;
void ys_compile(Node *prog, Target target, const char *outfile);
/* v2.1: exposed for main.c test runner */
extern int  g_throwing;
extern int  g_returning;
extern char g_throw_msg[512];
Val eval_block(Node *b, Env *parent);
extern int  g_assert_count;  /* v2.1: assertions in current test */
extern char g_src_file[512]; /* v1.4: current source filename */