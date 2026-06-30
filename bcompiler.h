#ifndef YOLISH_BCOMPILER_H
#define YOLISH_BCOMPILER_H

#include "bytecode.h"

/* Compiles a top-level program (the result of parse_program) into a
   single top-level Chunk. Nested `fn` definitions are compiled into
   their own FnProto (stored as OP_CLOSURE constants) recursively.
   Returns 1 on success, 0 if compilation hit an unsupported construct
   (message printed to stderr — this is a v2.0 *subset* compiler, not
   yet a full replacement for the AST interpreter). */
int bcompile_program(Node *prog, Chunk *out_chunk);

#endif