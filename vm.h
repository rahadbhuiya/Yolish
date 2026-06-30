#ifndef YOLISH_VM_H
#define YOLISH_VM_H

#include "bytecode.h"

typedef enum { VM_OK, VM_COMPILE_ERROR, VM_RUNTIME_ERROR } VMResult;

/* Compiles `prog` and runs it on a fresh VM instance. Returns VM_OK on a
   clean run. On VM_COMPILE_ERROR, bcompile_program() has already printed
   which construct it couldn't handle — the caller (main.c) is expected
   to fall back to the AST interpreter (eval_program) in that case. */
VMResult vm_interpret(Node *prog);

#endif