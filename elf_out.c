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

/* ---- dynamically-linked ELF (PT_INTERP/PT_DYNAMIC) ----
   For native compilation that needs to call into a real shared
   library (starting with a couple of libc.so.6 functions as a proof
   of concept; the eventual target is a real TLS library rather than
   hand-rolling cryptography in assembly). Everything else this
   compiler produces is fully static/freestanding by design — this is
   a deliberate, narrow exception, not a general change to how the
   native backend works.

   Layout mirrors elf_write's two-PT_LOAD structure with three
   additions: PT_PHDR, PT_INTERP (pointing at a hardcoded ld.so path),
   and PT_DYNAMIC. The dynamic-linking metadata itself (.dynsym,
   .dynstr, a minimal SysV .hash, .rela.dyn, and the .dynamic array)
   is built here and appended right after the caller's `data` bytes,
   still within the same RW LOAD segment — the caller only needs to
   have already reserved an 8-byte zeroed GOT slot per import inside
   `data` (see compiler.c's dynlink_import) and recorded its offset;
   everything ELF-specific about turning that into a working import
   lives here.

   Import resolution is eager, not lazy: each import gets a plain
   R_X86_64_GLOB_DAT relocation (not a PLT/JUMP_SLOT one), which ld.so
   always resolves as part of ordinary load-time relocation
   processing — so callers just do `mov reg,[got_slot]; call reg`,
   no PLT trampoline stub needed at all.

   This exact structure (down to the PT_PHDR offset/vaddr, which
   matters more than it looks — see the comment on PT_PHDR below) was
   validated against a real dynamically-linked run before being
   ported here. */

#define ET_DYN_PT_INTERP  3
#define ET_DYN_PT_DYNAMIC 2
#define ET_DYN_PT_PHDR    6

#define DT_NULL    0
#define DT_NEEDED  1
#define DT_HASH    4
#define DT_STRTAB  5
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_STRSZ   10
#define DT_SYMENT  11

#define R_X86_64_GLOB_DAT 6

int elf_write_dynamic(const char *path,
              uint8_t *code, int code_len,
              uint8_t *data, int data_len,
              int *reloc_code_offs, int *reloc_data_offs, int nrelocs,
              int entry_off,
              const char *needed_lib,
              const char **import_names, int *import_got_offs, int nimports)
{
    const char interp[] = "/lib64/ld-linux-x86-64.so.2";
    int interp_len = (int)sizeof(interp); /* includes NUL */

    /* .dynstr: \0 + needed_lib\0 + each import name\0 */
    uint8_t dynstr[2048]; int dynstr_len=0;
    dynstr[dynstr_len++]=0;
    int needed_str_off = dynstr_len;
    { const char *p=needed_lib; while(*p) dynstr[dynstr_len++]=(uint8_t)*p++; dynstr[dynstr_len++]=0; }
    int import_str_off[64];
    for(int i=0;i<nimports;i++){
        import_str_off[i]=dynstr_len;
        const char *p=import_names[i];
        while(*p) dynstr[dynstr_len++]=(uint8_t)*p++;
        dynstr[dynstr_len++]=0;
    }

    /* .dynsym: null entry + one undefined entry per import */
    int nsyms = nimports+1;
    uint8_t dynsym[2048]; int dynsym_len=0;
    memset(dynsym+dynsym_len,0,24); dynsym_len+=24; /* index 0: null entry */
    for(int i=0;i<nimports;i++){
        uint8_t *e=dynsym+dynsym_len;
        e[0]=(uint8_t)(import_str_off[i]    ); e[1]=(uint8_t)(import_str_off[i]>>8);
        e[2]=(uint8_t)(import_str_off[i]>>16); e[3]=(uint8_t)(import_str_off[i]>>24);
        e[4]=(1<<4)|2; /* STB_GLOBAL<<4 | STT_FUNC */
        e[5]=0;        /* st_other */
        e[6]=0; e[7]=0;/* st_shndx = SHN_UNDEF */
        memset(e+8,0,16); /* st_value=0, st_size=0 */
        dynsym_len+=24;
    }

    /* minimal SysV .hash: nbucket=1 so every symbol lands in bucket 0
       regardless of its name's hash, chained in import order:
       bucket[0] -> symtab index 1 -> chain[1]=2 -> ... -> 0 (STN_UNDEF,
       end of chain). chain[0] is the reserved null-symbol slot and is
       never traversed into, so it's just 0. */
    int nbucket=1, nchain=nsyms;
    uint8_t hasht[2048]; int hasht_len=0;
    #define PUT32(buf,len,v) do{ (buf)[(len)]=(uint8_t)(v); (buf)[(len)+1]=(uint8_t)((v)>>8); (buf)[(len)+2]=(uint8_t)((v)>>16); (buf)[(len)+3]=(uint8_t)((v)>>24); (len)+=4; }while(0)
    PUT32(hasht,hasht_len,nbucket);
    PUT32(hasht,hasht_len,nchain);
    PUT32(hasht,hasht_len, nimports>0?1u:0u); /* bucket[0] */
    PUT32(hasht,hasht_len,0u);                /* chain[0] (unused) */
    for(int i=1;i<nsyms;i++)
        PUT32(hasht,hasht_len, (i+1<nsyms)?(uint32_t)(i+1):0u); /* chain[i] */

    /* .rela.dyn: one R_X86_64_GLOB_DAT per import, pointing at its
       already-reserved GOT slot inside `data` */
    uint8_t rela[2048]; int rela_len=0;
    for(int i=0;i<nimports;i++){
        /* r_offset gets filled in below once data_vaddr is known */
        rela_len += 24;
    }

    /* ---- layout ----
       Unlike elf_write's two-segment layout (which starts code at a
       fresh page, leaving the ELF header/phdrs uncovered by any
       PT_LOAD segment — fine there, since nothing needs to read them
       back), here the header+phdrs+interp+code all have to live in
       ONE contiguous PT_LOAD(RX) segment starting at file offset 0.
       The kernel computes AT_PHDR from the mapping that covers file
       offset 0 (roughly: that segment's load address + e_phoff), so
       if the phdr table isn't inside any LOAD segment at all,
       main_map->l_phdr ends up NULL and rtld_setup_main_map's very
       first phdr-table access (computing the load bias) segfaults —
       this was the very next bug after the PT_PHDR offset/vaddr one
       above, caught the same way (against the real compiler's larger
       code_len, where code no longer starts at a page-aligned offset
       the way the standalone prototype's smaller version did). */
    uint64_t eh_size=sizeof(Elf64_Ehdr), phdr_size=sizeof(Elf64_Phdr);
    int nphdr=5; /* PHDR, INTERP, LOAD(code), LOAD(data), DYNAMIC */
    uint64_t hdr_region = eh_size + (uint64_t)nphdr*phdr_size;
    uint64_t interp_off_f = hdr_region;
    uint64_t code_off_f = interp_off_f + (uint64_t)interp_len;
    uint64_t code_vaddr = LOAD_ADDR + code_off_f;
    uint64_t code_filesz = (uint64_t)code_len;
    uint64_t text_seg_filesz = code_off_f + code_filesz; /* whole RX segment, from file offset 0 */
    uint64_t data_off_f = (text_seg_filesz+PAGE_SIZE-1)&~(PAGE_SIZE-1);
    uint64_t data_vaddr = LOAD_ADDR + data_off_f;

    uint64_t hash_off = data_off_f + (uint64_t)data_len;
    uint64_t dynsym_off = hash_off + (uint64_t)hasht_len;
    uint64_t dynstr_off = dynsym_off + (uint64_t)dynsym_len;
    uint64_t rela_off = dynstr_off + (uint64_t)dynstr_len;
    uint64_t dyn_off = rela_off + (uint64_t)rela_len;
    dyn_off = (dyn_off+7)&~(uint64_t)7;

    uint64_t hash_va = LOAD_ADDR+hash_off, dynsym_va=LOAD_ADDR+dynsym_off;
    uint64_t dynstr_va=LOAD_ADDR+dynstr_off, rela_va=LOAD_ADDR+rela_off;
    uint64_t dyn_va=LOAD_ADDR+dyn_off;

    /* now that data_vaddr is known, fill in rela.dyn's r_offset fields */
    rela_len=0;
    for(int i=0;i<nimports;i++){
        uint64_t got_va = data_vaddr + (uint64_t)import_got_offs[i];
        uint64_t r_info = ((uint64_t)(i+1)<<32) | R_X86_64_GLOB_DAT;
        uint8_t *e=rela+rela_len;
        memcpy(e,&got_va,8); memcpy(e+8,&r_info,8);
        uint64_t addend=0; memcpy(e+16,&addend,8);
        rela_len+=24;
    }

    uint64_t dyn_entries[] = {
        DT_NEEDED, (uint64_t)needed_str_off,
        DT_HASH, hash_va,
        DT_STRTAB, dynstr_va,
        DT_SYMTAB, dynsym_va,
        DT_STRSZ, (uint64_t)dynstr_len,
        DT_SYMENT, 24,
        DT_RELA, rela_va,
        DT_RELASZ, (uint64_t)rela_len,
        DT_RELAENT, 24,
        DT_NULL, 0,
    };
    int dyn_len = (int)sizeof(dyn_entries);

    uint64_t rw_filesz = (uint64_t)data_len + (uint64_t)hasht_len + (uint64_t)dynsym_len
                        + (uint64_t)dynstr_len + (uint64_t)rela_len
                        + (dyn_off - (rela_off+(uint64_t)rela_len)) + (uint64_t)dyn_len;

    /* apply the caller's regular rodata relocations exactly as
       elf_write does (this also covers codegen's `lea r11,[rip+got_off]`
       references into the GOT slots, since those are just ordinary
       offsets within `data` from code's point of view) */
    for(int i=0;i<nrelocs;i++){
        int coff=reloc_code_offs[i], doff=reloc_data_offs[i];
        int64_t target=(int64_t)(data_vaddr+(uint64_t)doff);
        int64_t rip=(int64_t)(code_vaddr+(uint64_t)coff+4);
        int32_t rel32=(int32_t)(target-rip);
        code[coff]=(uint8_t)rel32; code[coff+1]=(uint8_t)(rel32>>8);
        code[coff+2]=(uint8_t)(rel32>>16); code[coff+3]=(uint8_t)(rel32>>24);
    }

    FILE *f=fopen(path,"wb");
    if(!f){ perror(path); return 1; }

    Elf64_Ehdr eh; memset(&eh,0,sizeof(eh));
    eh.e_ident[0]=0x7f; eh.e_ident[1]='E'; eh.e_ident[2]='L'; eh.e_ident[3]='F';
    eh.e_ident[4]=2; eh.e_ident[5]=1; eh.e_ident[6]=1; eh.e_ident[7]=0;
    eh.e_type=ET_EXEC; eh.e_machine=EM_X86_64; eh.e_version=1;
    eh.e_entry=code_vaddr+(uint64_t)entry_off;
    eh.e_phoff=eh_size; eh.e_shoff=0;
    eh.e_ehsize=(uint16_t)eh_size; eh.e_phentsize=(uint16_t)phdr_size;
    eh.e_phnum=(uint16_t)nphdr;
    fwrite(&eh,1,sizeof(eh),f);

    Elf64_Phdr ph;
    /* PT_PHDR — p_vaddr/p_offset MUST describe where the phdr table
       itself lives (right after the 64-byte Ehdr), NOT LOAD_ADDR/0.
       Getting this wrong doesn't fail loudly: ld.so uses it to compute
       the executable's load bias (l_addr = AT_PHDR - this p_vaddr),
       and if that comes out non-zero, every address it derives from
       the rest of .dynamic gets silently shifted by the difference —
       this crashed deep inside ld.so's own audit-tag processing
       (nothing to do with audit functionality itself, just the first
       code path that happened to dereference a corrupted pointer)
       when the standalone prototype had this wrong. */
    memset(&ph,0,sizeof(ph));
    ph.p_type=ET_DYN_PT_PHDR; ph.p_flags=PF_R;
    ph.p_offset=eh_size; ph.p_vaddr=ph.p_paddr=LOAD_ADDR+eh_size;
    ph.p_filesz=ph.p_memsz=hdr_region-eh_size; ph.p_align=8;
    fwrite(&ph,1,sizeof(ph),f);

    memset(&ph,0,sizeof(ph));
    ph.p_type=ET_DYN_PT_INTERP; ph.p_flags=PF_R;
    ph.p_offset=interp_off_f; ph.p_vaddr=ph.p_paddr=LOAD_ADDR+interp_off_f;
    ph.p_filesz=ph.p_memsz=(uint64_t)interp_len; ph.p_align=1;
    fwrite(&ph,1,sizeof(ph),f);

    memset(&ph,0,sizeof(ph));
    ph.p_type=PT_LOAD; ph.p_flags=PF_R|PF_X;
    ph.p_offset=0; ph.p_vaddr=ph.p_paddr=LOAD_ADDR;
    ph.p_filesz=ph.p_memsz=text_seg_filesz; ph.p_align=PAGE_SIZE;
    fwrite(&ph,1,sizeof(ph),f);

    memset(&ph,0,sizeof(ph));
    ph.p_type=PT_LOAD; ph.p_flags=PF_R|PF_W;
    ph.p_offset=data_off_f; ph.p_vaddr=ph.p_paddr=data_vaddr;
    ph.p_filesz=ph.p_memsz=rw_filesz; ph.p_align=PAGE_SIZE;
    fwrite(&ph,1,sizeof(ph),f);

    memset(&ph,0,sizeof(ph));
    ph.p_type=ET_DYN_PT_DYNAMIC; ph.p_flags=PF_R|PF_W;
    ph.p_offset=dyn_off; ph.p_vaddr=ph.p_paddr=dyn_va;
    ph.p_filesz=ph.p_memsz=(uint64_t)dyn_len; ph.p_align=8;
    fwrite(&ph,1,sizeof(ph),f);

    { long cur=ftell(f); for(long i=0;i<(long)interp_off_f-cur;i++) fputc(0,f); }
    fwrite(interp,1,(size_t)interp_len,f);
    { long cur=ftell(f); for(long i=0;i<(long)code_off_f-cur;i++) fputc(0,f); }
    fwrite(code,1,(size_t)code_len,f);
    { long cur=ftell(f); for(long i=0;i<(long)data_off_f-cur;i++) fputc(0,f); }
    fwrite(data,1,(size_t)data_len,f);
    fwrite(hasht,1,(size_t)hasht_len,f);
    fwrite(dynsym,1,(size_t)dynsym_len,f);
    fwrite(dynstr,1,(size_t)dynstr_len,f);
    fwrite(rela,1,(size_t)rela_len,f);
    { long cur=ftell(f); for(long i=0;i<(long)dyn_off-cur;i++) fputc(0,f); }
    fwrite(dyn_entries,1,(size_t)dyn_len,f);

    fclose(f);
#ifndef _WIN32
    { char cmd[512]; snprintf(cmd,sizeof(cmd),"chmod +x \"%s\"",path); if(system(cmd)){} }
#endif
    return 0;
}