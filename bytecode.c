/* 
   bytecode.c  —  v2.0 (Bytecode VM)
   Chunk: a dynamically-growing instruction stream + constant pool.
*/

#include "bytecode.h"
#include <stdlib.h>
#include <string.h>

void chunk_init(Chunk *c){
    c->code=NULL; c->count=0; c->cap=0;
    c->lines=NULL;
    c->constants=NULL; c->kcount=0; c->kcap=0;
}

/* Returns the index the byte was written to. */
int chunk_write(Chunk *c, unsigned char byte, int line){
    if(c->count>=c->cap){
        int newcap = c->cap ? c->cap*2 : 64;
        c->code  = (unsigned char*)realloc(c->code,  (size_t)newcap);
        c->lines = (int*)          realloc(c->lines, (size_t)newcap*sizeof(int));
        c->cap = newcap;
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    return c->count++;
}

/* Adds a Val to the constant pool, returns its index (u16 range assumed —
   64K constants per function/chunk is far beyond any real program). */
int chunk_add_const(Chunk *c, Val v){
    if(c->kcount>=c->kcap){
        int newcap = c->kcap ? c->kcap*2 : 16;
        c->constants = (Val*)realloc(c->constants, (size_t)newcap*sizeof(Val));
        c->kcap = newcap;
    }
    c->constants[c->kcount]=v;
    return c->kcount++;
}

/* Patches a previously-emitted u16 jump operand at `offset` so that it
   jumps to `target` (both are byte offsets into c->code). The operand
   is a signed 16-bit relative offset, computed from the position right
   after the 2-byte operand itself. */
void chunk_patch_jump(Chunk *c, int offset, int target){
    int rel = target - (offset + 2);
    c->code[offset]   = (unsigned char)((rel >> 8) & 0xFF);
    c->code[offset+1] = (unsigned char)(rel & 0xFF);
}