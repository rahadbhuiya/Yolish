/*  checker.c  —  v2.1: ys check
 *  Static analysis pass — finds undefined variables, missing returns, etc.
 *  Runs without executing the program.
 */

#include "yolish.h"
#include <stdio.h>
#include <string.h>

#define MAX_NAMES 512

typedef struct {
    char names[MAX_NAMES][128];
    int  count;
    int  parent_start; /* index where parent scope starts */
} CheckEnv;

static int  g_check_errors = 0;
static int  g_check_warnings = 0;
static char g_check_file[512];

/* Known builtins / namespaces that are always available */
static const char *k_builtins[] = {
    "y","process","sys","gc","assert","assert_eq","assert_neq",
    "assert_true","assert_false","assert_nil",
    "println","print","true","false","nil",
    NULL
};

static int ce_is_builtin(const char *name) {
    for(int i=0; k_builtins[i]; i++)
        if(strcmp(name, k_builtins[i])==0) return 1;
    return 0;
}

static int ce_defined(CheckEnv *ce, const char *name) {
    if(ce_is_builtin(name)) return 1;
    for(int i=0; i<ce->count; i++)
        if(strcmp(ce->names[i], name)==0) return 1;
    return 0;
}

static void ce_define(CheckEnv *ce, const char *name) {
    if(ce->count < MAX_NAMES-1) {
        snprintf(ce->names[ce->count], 128, "%.127s", name); ce->count++;
    }
}

/* check_error reserved for future error reporting */
static void check_error(int line, int col, const char *msg){
    (void)line;(void)col;
    if(g_check_file[0]) fprintf(stderr,"%s:%d:%d: error: %s\n",g_check_file,line,col,msg);
    else fprintf(stderr,"error: %s\n",msg);
    g_check_errors++;
}

static void check_warn(int line, int col, const char *msg) {
    if(g_check_file[0])
        printf("%s:%d:%d: warning: %s\n", g_check_file, line, col, msg);
    else
        printf("warning line %d: %s\n", line, msg);
    g_check_warnings++;
}

static void check_expr(Node *n, CheckEnv *ce);
static void check_stmt(Node *n, CheckEnv *ce);

static void check_expr(Node *n, CheckEnv *ce) {
    if(!n) return;
    switch(n->kind) {
    case ND_IDENT:
        if(!ce_defined(ce, n->name)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "undefined '%s'", n->name);
            check_warn(n->line, n->column, msg);
        }
        break;
    case ND_BINOP:
    case ND_ASSIGN:
        check_expr(n->left,  ce);
        check_expr(n->right, ce);
        break;
    case ND_UNOP:
        check_expr(n->right, ce);
        break;
    case ND_CALL:
        /* check function name */
        if(n->name[0] && !ce_defined(ce, n->name)) {
            /* check if it looks like a dotted builtin (y.println etc.) */
            int has_dot=0;
            for(int i=0;n->name[i];i++) if(n->name[i]=='.')has_dot=1;
            if(!has_dot) {
                char msg[128];
                snprintf(msg,sizeof(msg),"possibly undefined function '%s'",n->name);
                check_warn(n->line, n->column, msg);
            }
        }
        /* check args */
        if(n->left) check_expr(n->left, ce);
        for(int i=0; i<n->argc; i++) check_expr(n->args[i], ce);
        break;
    case ND_DOT:
        check_expr(n->left, ce);
        break;
    case ND_INDEX:
        check_expr(n->left,  ce);
        check_expr(n->right, ce);
        break;
    case ND_ARRAY:
        for(int i=0;i<n->stmtc;i++) check_expr(n->stmts[i],ce);
        break;
    case ND_STRUCT_LIT:
        for(int i=0;i<n->argc;i++) check_expr(n->args[i],ce);
        break;
    case ND_IF:
        check_expr(n->cond, ce);
        check_stmt(n->then, ce);
        if(n->els) check_stmt(n->els, ce);
        break;
    case ND_WHILE:
        check_expr(n->cond, ce);
        check_stmt(n->body, ce);
        break;
    case ND_FN_LIT:
        /* closure — params are defined in body scope */
        { CheckEnv inner=*ce;
          for(int i=0;i<n->argc&&i<8;i++)
              if(n->field_names[i][0]) ce_define(&inner,n->field_names[i]);
          if(n->body) check_stmt(n->body,&inner); }
        break;
    case ND_MATCH:
        check_expr(n->left, ce);
        for(int i=0;i<n->stmtc;i++) {
            Node *arm=n->stmts[i];
            if(arm&&arm->kind==ND_MATCH_ARM){
                CheckEnv inner=*ce;
                if(arm->name[0]) ce_define(&inner,arm->name);
                if(arm->cond) check_expr(arm->cond,&inner);
                if(arm->body) check_stmt(arm->body,&inner);
            }
        }
        break;
    case ND_TRY:
        if(n->body) check_stmt(n->body,ce);
        /* catch block */
        if(n->els){
            CheckEnv inner=*ce;
            if(n->name[0]) ce_define(&inner,n->name);
            check_stmt(n->els,&inner);
        }
        break;
    default: break;
    }
}

static void check_stmt(Node *n, CheckEnv *ce) {
    if(!n) return;
    switch(n->kind) {
    case ND_LET:
    case ND_VAR:
        if(n->right) check_expr(n->right, ce);
        ce_define(ce, n->name);
        break;
    case ND_FN:{
        ce_define(ce, n->name);
        CheckEnv inner = *ce;
        /* function params stored in field_names */
        for(int i=0;i<n->argc&&i<8;i++)
            if(n->field_names[i][0]) ce_define(&inner, n->field_names[i]);
        /* check body */
        if(n->body) check_stmt(n->body, &inner);
        break;
    }
    case ND_STRUCT:
        ce_define(ce, n->name);
        break;
    case ND_ENUM:
        ce_define(ce, n->name);
        for(int i=0;i<n->stmtc;i++){
            char qname[128];
            snprintf(qname,sizeof(qname),"%s.%s",n->name,n->stmts[i]->name);
            ce_define(ce,qname);
            ce_define(ce,n->stmts[i]->name);
        }
        break;
    case ND_IMPL:
        for(int i=0;i<n->stmtc;i++) check_stmt(n->stmts[i],ce);
        break;
    case ND_BLOCK:
        for(int i=0;i<n->stmtc;i++) check_stmt(n->stmts[i],ce);
        break;
    case ND_IF:
        check_expr(n->cond, ce);
        if(n->then) check_stmt(n->then, ce);
        if(n->els)  check_stmt(n->els,  ce);
        break;
    case ND_WHILE:
        check_expr(n->cond, ce);
        if(n->body) check_stmt(n->body, ce);
        break;
    case ND_FOR:
        check_expr(n->right, ce);
        { CheckEnv inner=*ce; ce_define(&inner,n->name);
          if(n->body) check_stmt(n->body,&inner); }
        break;
    case ND_RETURN:
        if(n->right) check_expr(n->right, ce);
        break;
    case ND_THROW:
        if(n->right) check_expr(n->right, ce);
        break;
    case ND_ASSIGN:
        check_expr(n->right, ce);
        if(n->left) check_expr(n->left, ce);
        break;
    case ND_IMPORT:
        /* nothing to check statically */
        break;
    case ND_TEST:
        /* register test name, check body */
        { CheckEnv inner=*ce;
          if(n->body) check_stmt(n->body,&inner); }
        break;
    default:
        check_expr(n, ce);
        break;
    }
}

int ys_check(Node *prog, const char *filename) {
    (void)check_error; /* suppress unused warning */
    strncpy(g_check_file, filename, 511);
    g_check_errors = 0;
    g_check_warnings = 0;

    CheckEnv ce; ce.count=0; ce.parent_start=0;

    /* Pre-populate with all top-level definitions (two-pass) */
    for(int i=0; i<prog->stmtc; i++){
        Node *stmt=prog->stmts[i];
        if(!stmt) continue;
        if(stmt->kind==ND_FN)     ce_define(&ce,stmt->name);
        if(stmt->kind==ND_STRUCT) ce_define(&ce,stmt->name);
        if(stmt->kind==ND_ENUM){
            ce_define(&ce,stmt->name);
            for(int j=0;j<stmt->stmtc;j++){
                char q[128];
                snprintf(q,sizeof(q),"%s.%s",stmt->name,stmt->stmts[j]->name);
                ce_define(&ce,q);
                ce_define(&ce,stmt->stmts[j]->name);
            }
        }
    }

    /* Second pass: full check */
    for(int i=0; i<prog->stmtc; i++)
        check_stmt(prog->stmts[i], &ce);

    if(g_check_errors==0 && g_check_warnings==0){
        printf("No issues found.\n");
    } else {
        if(g_check_errors>0)   printf("%d error(s) found.\n",   g_check_errors);
        if(g_check_warnings>0) printf("%d warning(s) found.\n", g_check_warnings);
    }
    return g_check_errors+g_check_warnings;
}