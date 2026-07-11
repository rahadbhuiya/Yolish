#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform */
static inline void ys_puts(const char *s){ fputs(s,stdout); fflush(stdout); }
#define puts(s) ys_puts(s)
static inline void ys_print(const char *s){ fputs(s,stdout); fflush(stdout); }
/* v2.4: NULL-safe — dynamic strings may be NULL before first assignment */
static inline int strcmp_u(const char *a,const char *b){
    if(!a) a="";
    if(!b) b="";
    return strcmp(a,b);
}
static inline int str_len_u(const char *s){ if(!s) return 0; int n=0; while(s[n])n++; return n; }

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
    TK_AMP, TK_PIPE, TK_CARET, TK_BANG, TK_SHL, TK_SHR, TK_TILDE,
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
    ND_VM_VALUE, /* v2.0: wraps an already-evaluated Val* (from the VM's
                    stack) so call_builtin() can be reused as-is without
                    every builtin needing a Val-based variant. The Val*
                    lives in fn_node, cast back on use — see eval_node's
                    ND_VM_VALUE case and call_builtin_public() in eval.c. */
} NodeKind;

typedef struct Node Node;
struct Node {
    NodeKind  kind;
    int64_t   ival;
    double    fval;
    char     *sval;    /* v2.4: dynamic — was char[8192], parse-time string storage */
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
    char    *sval;     /* v2.4: dynamic heap string — never embed array */
    int      slen;     /* cached strlen(sval), avoids repeated scans */
    Node    *fn_node;
    void    *fn_env;
    int64_t  cap_fd;
    int      cap_perm;
    char     cap_path[128];
    Val     *arr_data;
    int      arr_len;
    int      arr_cap;  /* v1.9: allocated capacity (0 = same as arr_len) */
    char     struct_name[32];
    Val     *field_vals;
    char   (*field_names)[32];
    int      field_count;
    /* Hashmap storage (YS_MAP). Open-addressing hash table: map_keys[i]/
       map_vals[i] are parallel slots, map_cap is always a power of two.
       An empty slot has map_keys[i].type==YS_NIL && ival==0 (the zeroed
       default); a deleted slot (tombstone, so probing keeps working)
       has map_keys[i].type==YS_NIL && ival==1. map_len counts live
       (non-empty, non-tombstone) entries. */
    Val     *map_keys;
    Val     *map_vals;
    int      map_len;
    int      map_cap;
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
#define YS_MAP    10

/* Environment */
/* v2.5: names/vals are now dynamically grown (start small, double on demand)
   instead of a fixed ENV_MAX-sized array embedded in every scope. This
   removes the per-scope variable-count limit entirely AND keeps small
   scopes (the overwhelming majority — most functions bind a handful of
   variables) cheap, since the old fixed-size design meant every single
   scope paid for the worst case up front. Combined with the v2.4 fix
   that lets scopes accumulate for the process lifetime, this matters:
   a large fixed ENV_MAX made every accumulated scope expensive instead
   of just the ones that actually needed many variables. */
typedef struct Env Env;
struct Env {
    char (*names)[64]; /* heap array, grows via env_grow() */
    Val   *vals;        /* heap array, grows in lockstep with names */
    int    count;
    int    cap;         /* allocated capacity of names/vals */
    Env   *parent;
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
int ys_compile(Node *prog, Target target, const char *outfile);
/* v2.1: exposed for main.c test runner */
extern int  g_throwing;
extern int  g_returning;
extern char g_throw_msg[512];
Val eval_block(Node *b, Env *parent);

/* v2.0: bytecode VM bridge into the existing builtin table (see eval.c) */
Val call_builtin_public(const char *name, Val *argv, int argc);
/* v2.6: closure bridge (see bcompiler.c's ND_FN_LIT and vm.c's OP_CALL/
   OP_MAKE_CLOSURE). A VM-compiled closure carries a real AST Node* +
   Env* (built at capture time), and is invoked via the *same*
   tree-walking call path the AST interpreter itself uses — this is
   what lets a VM closure be passed to y.map/filter/reduce/sort/each
   (which only know how to call AST-style function values) and behave
   identically either way. vm_global_lookup_public lets a closure body,
   evaluated by the tree-walking interpreter, still see globals defined
   by the *VM's own* separate global table (OP_DEFINE_GLOBAL) when its
   own Env chain doesn't have the name. */
Val call_closure_public(Node *fd, Env *ce, Val *argv, int argc);
Val *vm_global_lookup_public(const char *name);
/* v2.6: impl-method bridge (see bcompiler.c's ND_IMPL and vm.c's
   OP_CALL_METHOD). register_impl_methods_public runs at VM-compile
   time (impl blocks are always static top-level declarations), adding
   to eval.c's own method registry so both interpreters share one
   source of truth. call_method_public dispatches a method call
   (p.method(...)) the same tree-walking way eval.c's own ND_CALL
   method dispatch does. */
void register_impl_methods_public(Node *impl_node);
Val call_method_public(Val obj, const char *method_name, Val *argv, int argc);
/* v2.6: `import "file.y" as name` — runs the file in an isolated Env
   and packages every top-level definition into a namespace struct
   value, exactly mirroring eval.c's own ND_MODULE. Exposed so vm.c's
   OP_LOAD_MODULE can run it at the *correct point in execution order*
   (unlike the bare `import` form, which is spliced at compile time —
   this one has real side effects, e.g. running the module file's own
   top-level statements, so it must happen when the bytecode actually
   reaches this point, not eagerly during compilation). */
Val eval_module_public(const char *raw_path, const char *ns_name);

/* v2.0: Val constructors and helpers, exported so vm.c can build/inspect
   values without duplicating eval.c's logic */
Val     make_nil(void);
Val     make_int(int64_t v);
Val     make_float(double v);
Val     make_bool(int v);
Val     make_str(const char *s);
int64_t val_int(Val v);
double  val_float(Val v);
int     val_bool(Val v);
Val    *alloc_arr(int n);
void    ys_print_val(Val v);
extern int  g_assert_count;  /* v2.1: assertions in current test */
extern char g_src_file[512]; /* v1.4: current source filename */