/*  macho_out.c  —  Write macOS Mach-O 64-bit executable  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Mach-O constants */
#define MH_MAGIC_64     0xfeedfacfU
#define MH_EXECUTE      2
#define MH_NOUNDEFS     1
#define MH_PIE          0x200000
#define CPU_TYPE_X86_64 0x01000007
#define CPU_SUBTYPE_ALL 3

#define LC_SEGMENT_64   0x19
#define LC_UNIXTHREAD   0x5
#define LC_MAIN         0x80000028

#define VM_PROT_NONE    0
#define VM_PROT_READ    1
#define VM_PROT_WRITE   2
#define VM_PROT_EXECUTE 4

/* load address for non-PIE executable */
#define MACHO_BASE      0x100000000ULL
#define PAGE_SIZE       0x1000ULL

typedef struct __attribute__((packed)){
    uint32_t magic,cputype,cpusubtype,filetype,ncmds,sizeofcmds,flags,reserved;
} MachHeader64;

typedef struct __attribute__((packed)){
    uint32_t cmd,cmdsize;
    char segname[16];
    uint64_t vmaddr,vmsize,fileoff,filesize;
    uint32_t maxprot,initprot,nsects,flags;
} SegCommand64;

typedef struct __attribute__((packed)){
    char sectname[16],segname[16];
    uint64_t addr,size;
    uint32_t offset,align,reloff,nreloc,flags,r1,r2;
    uint32_t reserved3;
} Section64;

typedef struct __attribute__((packed)){
    uint32_t cmd,cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
} EntryPointCommand;


int macho_write(const char *path,
                uint8_t *code, int code_len,
                uint8_t *data, int data_len,
                int *reloc_code, int *reloc_data, int nrelocs,
                int entry_off)
{
    /* Layout:
     * 0:        Mach-O header
     * +sz_hdr:  load commands
     * page-align: __TEXT,__text
     * next page: __DATA,__cstring (rodata)
     */

    /* Compute header size */
    uint32_t hdr_sz = sizeof(MachHeader64);
    /* Load commands:
       1. LC_SEGMENT_64 "__TEXT"  with 1 section "__text"
       2. LC_SEGMENT_64 "__DATA"  with 1 section "__cstring"
       3. LC_SEGMENT_64 "__LINKEDIT" (required, empty)
       4. LC_MAIN
    */
    uint32_t seg64_sz = sizeof(SegCommand64)+sizeof(Section64);
    uint32_t seg64_nosect_sz = sizeof(SegCommand64);
    uint32_t main_sz  = sizeof(EntryPointCommand);
    uint32_t lc_size  = seg64_sz        /* __TEXT */
                      + seg64_sz        /* __DATA */
                      + seg64_nosect_sz /* __LINKEDIT */
                      + main_sz;        /* LC_MAIN */
    uint32_t headers_sz = hdr_sz + lc_size;
    uint32_t headers_pages = (uint32_t)((headers_sz + PAGE_SIZE-1) / PAGE_SIZE);

    uint64_t text_off  = headers_pages * PAGE_SIZE;
    uint64_t text_vsz  = (uint64_t)code_len;
    uint64_t text_vaddr= MACHO_BASE + text_off;

    uint64_t data_off  = text_off + ((text_vsz + PAGE_SIZE-1) & ~(PAGE_SIZE-1));
    uint64_t data_vsz  = (uint64_t)(data_len ? data_len : 1);
    uint64_t data_vaddr= MACHO_BASE + data_off;

    uint64_t linkedit_off = data_off + ((data_vsz+PAGE_SIZE-1)&~(PAGE_SIZE-1));

    /* Apply relocations */
    for(int i=0;i<nrelocs;i++){
        int coff=reloc_code[i], doff=reloc_data[i];
        int64_t target=(int64_t)(data_vaddr+(uint64_t)doff);
        int64_t rip   =(int64_t)(text_vaddr+(uint64_t)coff+4);
        int32_t rel32 =(int32_t)(target-rip);
        code[coff  ]=(uint8_t)(rel32    );
        code[coff+1]=(uint8_t)(rel32>> 8);
        code[coff+2]=(uint8_t)(rel32>>16);
        code[coff+3]=(uint8_t)(rel32>>24);
    }

    FILE *f=fopen(path,"wb");
    if(!f){ perror(path); return 1; }

    /* Mach-O header */
    MachHeader64 hdr;
    memset(&hdr,0,sizeof(hdr));
    hdr.magic      =MH_MAGIC_64;
    hdr.cputype    =CPU_TYPE_X86_64;
    hdr.cpusubtype =CPU_SUBTYPE_ALL;
    hdr.filetype   =MH_EXECUTE;
    hdr.ncmds      =4;
    hdr.sizeofcmds =lc_size;
    hdr.flags      =MH_NOUNDEFS|MH_PIE;
    fwrite(&hdr,1,sizeof(hdr),f);

    /* __TEXT segment */
    {
        SegCommand64 seg; memset(&seg,0,sizeof(seg));
        seg.cmd     =LC_SEGMENT_64;
        seg.cmdsize =seg64_sz;
        strncpy(seg.segname,"__TEXT",16);
        seg.vmaddr  =MACHO_BASE;
        seg.vmsize  =data_off; /* covers headers+text */
        seg.fileoff =0;
        seg.filesize=data_off;
        seg.maxprot =VM_PROT_READ|VM_PROT_EXECUTE;
        seg.initprot=VM_PROT_READ|VM_PROT_EXECUTE;
        seg.nsects  =1;
        fwrite(&seg,1,sizeof(seg),f);
        Section64 sec; memset(&sec,0,sizeof(sec));
        strncpy(sec.sectname,"__text",16);
        strncpy(sec.segname, "__TEXT",16);
        sec.addr  =text_vaddr;
        sec.size  =text_vsz;
        sec.offset=(uint32_t)text_off;
        sec.align =4;
        fwrite(&sec,1,sizeof(sec),f);
    }

    /* __DATA segment */
    {
        SegCommand64 seg; memset(&seg,0,sizeof(seg));
        seg.cmd     =LC_SEGMENT_64;
        seg.cmdsize =seg64_sz;
        strncpy(seg.segname,"__DATA",16);
        seg.vmaddr  =data_vaddr;
        seg.vmsize  =(data_vsz+PAGE_SIZE-1)&~(PAGE_SIZE-1);
        seg.fileoff =data_off;
        seg.filesize=data_vsz;
        seg.maxprot =VM_PROT_READ;
        seg.initprot=VM_PROT_READ;
        seg.nsects  =1;
        fwrite(&seg,1,sizeof(seg),f);
        Section64 sec; memset(&sec,0,sizeof(sec));
        strncpy(sec.sectname,"__cstring",16);
        strncpy(sec.segname, "__DATA",16);
        sec.addr  =data_vaddr;
        sec.size  =data_vsz;
        sec.offset=(uint32_t)data_off;
        sec.align =0;
        sec.flags =0x2; /* S_CSTRING_LITERALS */
        fwrite(&sec,1,sizeof(sec),f);
    }

    /* __LINKEDIT (required empty segment) */
    {
        SegCommand64 seg; memset(&seg,0,sizeof(seg));
        seg.cmd     =LC_SEGMENT_64;
        seg.cmdsize =seg64_nosect_sz;
        strncpy(seg.segname,"__LINKEDIT",16);
        seg.vmaddr  =MACHO_BASE+linkedit_off;
        seg.vmsize  =PAGE_SIZE;
        seg.fileoff =linkedit_off;
        seg.filesize=0;
        seg.maxprot =VM_PROT_READ;
        seg.initprot=VM_PROT_READ;
        seg.nsects  =0;
        fwrite(&seg,1,sizeof(seg),f);
    }

    /* LC_MAIN */
    {
        EntryPointCommand ep; memset(&ep,0,sizeof(ep));
        ep.cmd      =LC_MAIN;
        ep.cmdsize  =main_sz;
        ep.entryoff =(uint64_t)(text_off+(uint64_t)entry_off);
        ep.stacksize=0;
        fwrite(&ep,1,sizeof(ep),f);
    }

    /* Pad to text section */
    {
        long cur=ftell(f);
        for(long i=cur;i<(long)text_off;i++) fputc(0,f);
    }

    /* Code */
    fwrite(code,1,code_len,f);

    /* Pad to data section */
    {
        long cur=ftell(f);
        for(long i=cur;i<(long)data_off;i++) fputc(0,f);
    }

    /* Data (strings) */
    if(data_len>0) fwrite(data,1,data_len,f);

    fclose(f);

    /* chmod +x (POSIX only) */
#ifndef _WIN32
    {
        char cmd[512];
        snprintf(cmd,sizeof(cmd),"chmod +x \"%s\"",path);
        if(system(cmd)){}
    }
#endif

    /* macOS: an unsigned Mach-O executable is killed by the kernel's AMFI
     * / code-signing enforcement before it ever executes a single
     * instruction — this happens on both Apple Silicon *and* Intel Macs
     * on modern macOS, not just arm64. The process shows up as SIGKILL
     * (exit code 137) with zero output, because it never gets past
     * execve(). This is true even for a completely trivial, no-syscall
     * binary, which is why the earlier network-focused debugging never
     * turned anything up: the bug is here, not in y.net.*.
     *
     * We don't hand-roll an LC_CODE_SIGNATURE + CodeDirectory blob here;
     * instead we shell out to the system `codesign` tool to apply an
     * ad-hoc signature ("-s -") after the file is written. This only
     * runs (and only needs to run) when ys itself is executing on macOS
     * and cross-compiling isn't in play; codesign isn't available on
     * Linux/Windows build hosts, so failures there are silently ignored.
     */
#ifdef __APPLE__
    {
        char cmd[600];
        snprintf(cmd,sizeof(cmd),
            "codesign --force -s - \"%s\" >/dev/null 2>&1",path);
        if(system(cmd)){
            fprintf(stderr,
                "warning: codesign failed for \"%s\" — the binary will be "
                "killed by macOS at launch (SIGKILL/exit 137) until it is "
                "signed, e.g. run: codesign -s - \"%s\"\n", path, path);
        }
    }
#endif

    return 0;
}