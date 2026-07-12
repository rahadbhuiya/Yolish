/*  elf_out.c  —  Write Linux ELF64 executable  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ELF64 constants */
#define ET_EXEC   2
#define EM_X86_64 62
#define PT_LOAD   1
#define PF_X      1
#define PF_W      2
#define PF_R      4

/* Load address */
#define LOAD_ADDR  0x400000ULL
#define PAGE_SIZE  0x1000ULL

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr;
    uint64_t p_filesz, p_memsz, p_align;
} Elf64_Phdr;

int elf_write(const char *path,
              uint8_t *code, int code_len,
              uint8_t *data, int data_len,
              /* relocs: each entry is {code_off, data_off} relative */
              int *reloc_code_offs, int *reloc_data_offs, int nrelocs,
              int entry_off)
{
    /* Layout:
     *   0x000  ELF header     (64 bytes)
     *   0x040  2 × Phdr       (2 × 56 = 112 bytes)
     *   0x0B0  padding to 0x1000
     *   0x1000 code segment
     *   0x1000+code_len  (align to page)
     *   next page  data segment (rodata)
     */
    uint64_t eh_size    = sizeof(Elf64_Ehdr);
    uint64_t phdr_size  = sizeof(Elf64_Phdr);
    uint64_t code_off_f = PAGE_SIZE;               /* file offset of code */
    uint64_t code_vaddr = LOAD_ADDR + code_off_f;
    uint64_t code_filesz= (uint64_t)code_len;
    uint64_t data_off_f = code_off_f + ((code_filesz + PAGE_SIZE-1) & ~(PAGE_SIZE-1));
    uint64_t data_vaddr = LOAD_ADDR + data_off_f;

    /* apply relocations: each is a RIP-relative 32-bit reference from code
       to the data section. The instruction is: [code_off-4 .. code_off-1] = rel32
       where rel32 = target_vaddr - (code_vaddr + code_off + 0)
       Actually: the 4-byte slot at reloc_code_offs[i] should be:
         data_vaddr + reloc_data_offs[i] - (code_vaddr + reloc_code_offs[i] + 4)
    */
    for(int i=0;i<nrelocs;i++){
        int coff=reloc_code_offs[i];
        int doff=reloc_data_offs[i];
        int64_t target = (int64_t)(data_vaddr + doff);
        int64_t rip    = (int64_t)(code_vaddr + coff + 4);
        int32_t rel32  = (int32_t)(target - rip);
        code[coff  ]=(uint8_t)(rel32    );
        code[coff+1]=(uint8_t)(rel32>> 8);
        code[coff+2]=(uint8_t)(rel32>>16);
        code[coff+3]=(uint8_t)(rel32>>24);
    }

    FILE *f=fopen(path,"wb");
    if(!f){ perror(path); return 1; }

    /* ELF header */
    Elf64_Ehdr eh; memset(&eh,0,sizeof(eh));
    eh.e_ident[0]=0x7f; eh.e_ident[1]='E'; eh.e_ident[2]='L'; eh.e_ident[3]='F';
    eh.e_ident[4]=2;  /* 64-bit */
    eh.e_ident[5]=1;  /* little-endian */
    eh.e_ident[6]=1;  /* ELF version */
    eh.e_ident[7]=0;  /* OS/ABI: SysV */
    eh.e_type      =ET_EXEC;
    eh.e_machine   =EM_X86_64;
    eh.e_version   =1;
    eh.e_entry     =code_vaddr + entry_off;
    eh.e_phoff     =eh_size;
    eh.e_shoff     =0;
    eh.e_ehsize    =(uint16_t)eh_size;
    eh.e_phentsize =(uint16_t)phdr_size;
    eh.e_phnum     =data_len>0?2:1;
    fwrite(&eh,1,sizeof(eh),f);

    /* code segment phdr */
    Elf64_Phdr ph; memset(&ph,0,sizeof(ph));
    ph.p_type  =PT_LOAD;
    ph.p_flags =PF_R|PF_X;
    ph.p_offset=code_off_f;
    ph.p_vaddr =code_vaddr;
    ph.p_paddr =code_vaddr;
    ph.p_filesz=code_filesz;
    ph.p_memsz =code_filesz;
    ph.p_align =PAGE_SIZE;
    fwrite(&ph,1,sizeof(ph),f);

    /* data segment phdr */
    if(data_len>0){
        Elf64_Phdr ph2; memset(&ph2,0,sizeof(ph2));
        ph2.p_type  =PT_LOAD;
        ph2.p_flags =PF_R|PF_W;
        ph2.p_offset=data_off_f;
        ph2.p_vaddr =data_vaddr;
        ph2.p_paddr =data_vaddr;
        ph2.p_filesz=(uint64_t)data_len;
        ph2.p_memsz =(uint64_t)data_len;
        ph2.p_align =PAGE_SIZE;
        fwrite(&ph2,1,sizeof(ph2),f);
    }

    /* padding to code_off_f */
    {
        long cur=ftell(f);
        long pad=(long)code_off_f - cur;
        for(long i=0;i<pad;i++) fputc(0,f);
    }

    /* code */
    fwrite(code,1,code_len,f);

    if(data_len>0){
        /* padding between code and data */
        long cur=ftell(f);
        long target2=(long)data_off_f;
        long pad=target2-cur;
        for(long i=0;i<pad;i++) fputc(0,f);
        /* data */
        fwrite(data,1,data_len,f);
    }

    fclose(f);

    /* make executable (POSIX only) */
#ifndef _WIN32
    {
        char cmd[512];
        snprintf(cmd,sizeof(cmd),"chmod +x \"%s\"",path);
        if(system(cmd)){}
    }
#endif
    return 0;
}