/* 
   bytecode.h  —  v2.0 (Bytecode VM)
   ================================================================
   Opcode set and the Chunk container that holds compiled bytecode
   for a single function (or the top-level script). Each Chunk owns:
     - code[]      : flat byte array of opcodes + operands
     - constants[] : pool of Val literals referenced by OP_CONST
     - lines[]     : source line per byte, for runtime error messages

   Design notes:
   - Stack-based VM (not register-based) — simplest correct design,
     matches the reference (Crafting Interpreters' clox) closely.
   - Reuses the existing Val struct and GC from eval.c/yolish.h so
     strings, arrays, structs, and closures behave identically to the
     AST interpreter — only *how* we get to those Val operations
     changes, not their semantics.
   - This is an additive subsystem: the AST interpreter (eval.c) is
     completely untouched. `ys file.y` still uses it. `ys vm file.y`
     (new) uses this compiler+VM instead.
*/

#ifndef YOLISH_BYTECODE_H
#define YOLISH_BYTECODE_H

#include "yolish.h"

typedef enum {
    /*  stack literals  */
    OP_CONST,         /* operand: u16 constant pool index → push constants[idx] */
    OP_NIL,           /* push nil */
    OP_TRUE,          /* push true */
    OP_FALSE,         /* push false */

    /*  stack manipulation  */
    OP_POP,           /* discard top of stack */
    OP_DUP,           /* duplicate top of stack */

    /* arithmetic / comparison / logic (binary: pop b,a push a OP b) */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,

    /*  unary (pop a, push OP a)  */
    OP_NEG,           /* numeric negate */
    OP_NOT,           /* boolean not */

    /*  variables  */
    OP_GET_LOCAL,     /* operand: u16 slot → push frame->slots[slot] */
    OP_SET_LOCAL,     /* operand: u16 slot → frame->slots[slot] = pop() ; push it back */
    OP_GET_GLOBAL,    /* operand: u16 const idx (name) → push globals[name] */
    OP_SET_GLOBAL,    /* operand: u16 const idx (name) → globals[name] = pop() ; push it back */
    OP_DEFINE_GLOBAL, /* operand: u16 const idx (name) → globals[name] = pop() (no push back) */

    /*  control flow (operand: i16 relative jump in bytes from
       the position *after* the operand)  */
    OP_JUMP,
    OP_JUMP_IF_FALSE, /* peeks top of stack, does NOT pop (caller pops if needed) */
    OP_LOOP,          /* same as OP_JUMP but conventionally backward */

    /*  functions  */
    OP_CALL,          /* operand: u8 argc → calls the callee found argc+1 below top */
    OP_RETURN,        /* pop return value, pop frame, push value to caller */
    OP_CLOSURE,       /* operand: u16 const idx (FnProto) → build a closure Val */

    /*  arrays  */
    OP_ARRAY,         /* operand: u16 count → pop count values, push array */
    OP_INDEX_GET,     /* pop index, pop array/string → push element */
    OP_INDEX_SET,     /* pop value, pop index, pop array → set element, push value */

    /*  structs (v2.0 Phase 2)  */
    OP_STRUCT_NEW,    /* u16 name_idx, u8 fcount, then u16 field_name_idx*fcount.
                          Pops fcount values (field[0] at bottom, field[N-1] on top).
                          Creates YS_STRUCT Val with GC-tracked field_vals,
                          malloc field_names; pushes struct. */
    OP_GET_FIELD,     /* u16 name_idx -> pop struct, push field value (nil if not found). */
    OP_SET_FIELD,     /* u16 name_idx -> pop value then pop struct. Writes to
                          struct.field_vals[i] via shared GC pointer so all copies
                          of this struct Val see the update. Pushes value. */

    /*  builtins (escape hatch into the existing builtin table)  */
    OP_BUILTIN,       /* operand: u16 name-const idx, u8 argc
                          → pop argc values, call the named builtin, push result.
                          This is how y.println, y.len, y.push, gc.collect, etc.
                          all work without reimplementing every builtin for the VM. */

    /*  I/O shortcuts (hot path, avoid a builtin-name lookup)  */
    OP_PRINT,
    OP_PRINTLN,

    OP_HALT,

    /* v2.0: mirrors eval_program()'s "auto-call main() if it exists"
       behavior — looks up the global "main", and if it's a function,
       calls it with zero args. No-op if "main" isn't defined or isn't
       a function. Emitted once, right before OP_HALT, at the end of
       every top-level program. */
    OP_CALL_MAIN_IF_EXISTS
} OpCode;

typedef struct {
    unsigned char *code;      /* flat instruction stream */
    int            count;
    int            cap;
    int           *lines;     /* lines[i] = source line of code[i] */
    Val           *constants; /* constant pool */
    int            kcount;
    int            kcap;
} Chunk;

void  chunk_init(Chunk *c);
int   chunk_write(Chunk *c, unsigned char byte, int line);
int   chunk_add_const(Chunk *c, Val v);
void  chunk_patch_jump(Chunk *c, int offset, int target);

/* A compiled function: its own Chunk plus metadata needed at call time. */
typedef struct FnProto {
    Chunk   chunk;
    int     arity;
    char    param_names[8][64]; /* mirrors Node.field_names limit (8 params) */
    char    name[64];
    int     upvalue_count;
} FnProto;

#endif