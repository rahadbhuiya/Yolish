/*  pe_out.c  —  Write Windows PE32+ executable (.exe)
 *
 *  Minimal PE32+ with:
 *  - MZ stub
 *  - PE header + optional header
 *  - .text section (code)
 *  - .rdata section (read-only data / strings)
 *  - Import table: kernel32.dll (WriteFile, GetStdHandle, ExitProcess)
 *
 *  Windows x64 calling convention:
 *    args: rcx, rdx, r8, r9  (shadow space 32 bytes required)
 *    syscalls not used — Win32 API calls instead
 *
 *  The runtime helpers are re-emitted for Windows ABI inside this file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define IMAGE_BASE       0x140000000ULL
#define SECTION_ALIGN    0x1000
#define FILE_ALIGN       0x200


/* helpers for pe code */


/* import directory: kernel32 imports */
typedef struct { uint32_t rva; char name[64]; } WinImport;
static WinImport win_imports[]={
    {0,"GetStdHandle"},
    {0,"WriteFile"},
    {0,"ExitProcess"},
};
#define N_IMPORTS (sizeof(win_imports)/sizeof(win_imports[0]))
static uint64_t import_thunks[N_IMPORTS];  /* filled in: VA of IAT entries */

static uint32_t align_up(uint32_t v, uint32_t a){ return (v+a-1)&~(a-1); }

/* RVA helpers */
static uint32_t text_rva   = SECTION_ALIGN;
static uint32_t rdata_rva  = 0; /* set after text */
static uint32_t idata_rva  = 0; /* import data */

/* write 2-byte little-endian */
static void w2(FILE*f,uint16_t v){ fputc(v&0xff,f); fputc((v>>8)&0xff,f); }
/* write 4-byte little-endian */
static void w4(FILE*f,uint32_t v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }
/* write 8-byte little-endian */
static void w8(FILE*f,uint64_t v){ w4(f,(uint32_t)v); w4(f,(uint32_t)(v>>32)); }

int pe_write(const char *path,
             uint8_t *code, int code_len,
             uint8_t *data, int data_len,
             int *reloc_code, int *reloc_data, int nrelocs,
             int entry_off,
             int *icall_off, int *icall_idx, int n_icalls)
{
    /*  compute layout  */
    uint32_t text_vsize  = (uint32_t)code_len;
    uint32_t text_fsize  = align_up(text_vsize, FILE_ALIGN);
    uint32_t rdata_vsize = (uint32_t)(data_len + 256); /* extra for import strings */
    uint32_t rdata_fsize = align_up(rdata_vsize, FILE_ALIGN);

    text_rva  = SECTION_ALIGN;
    rdata_rva = text_rva  + align_up(text_vsize, SECTION_ALIGN);
    idata_rva = rdata_rva + align_up(rdata_vsize, SECTION_ALIGN);

    uint32_t text_rawoff  = FILE_ALIGN;
    uint32_t rdata_rawoff = text_rawoff  + text_fsize;
    uint32_t idata_rawoff = rdata_rawoff + rdata_fsize;

    /*  apply data relocations  */
    uint64_t code_va = IMAGE_BASE + text_rva;
    uint64_t data_va = IMAGE_BASE + rdata_rva;
    for(int i=0;i<nrelocs;i++){
        int coff=reloc_code[i], doff=reloc_data[i];
        int64_t target=(int64_t)(data_va+doff);
        int64_t rip=(int64_t)(code_va+coff+4);
        int32_t rel32=(int32_t)(target-rip);
        code[coff  ]=(uint8_t)(rel32    );
        code[coff+1]=(uint8_t)(rel32>> 8);
        code[coff+2]=(uint8_t)(rel32>>16);
        code[coff+3]=(uint8_t)(rel32>>24);
    }

    /*  build import table in a scratch buffer  */
    /* We need: IAT, INT, DLL names, function name strings */
    static uint8_t idata_buf[4096]; int idata_len=0;
    #define IDA(b) idata_buf[idata_len++]=(uint8_t)(b)
    #define IDA32(v) {IDA(v);IDA((v)>>8);IDA((v)>>16);IDA((v)>>24);}
    #define IDA64(v) {IDA32(v);IDA32((v)>>32);}
    #define IDA_STR(s) {const char*_s=(s);while(*_s)IDA(*_s++);IDA(0);}
    #define IDA_PAD2 if(idata_len%2)IDA(0);

    /* Offsets within idata section (relative to idata_rva) */
    /* Layout: IDT (import directory table) | IAT | INT | name strings | DLL name */
    /* IDT: 1 entry + null terminator = 2 × 20 bytes */
    int idt_off=0;          /* IDT starts at 0 */
    int iat_off=40;         /* after IDT (2 entries × 20) */
    int int_off=iat_off + (int)(N_IMPORTS+1)*8;
    int names_off=int_off  + (int)(N_IMPORTS+1)*8;

    /* reserve space */
    idata_len = names_off;
    memset(idata_buf,0,sizeof(idata_buf));

    /* write function name strings and fill INT/IAT */
    for(int i=0;i<(int)N_IMPORTS;i++){
        /* name hint entry: 2-byte hint + name string */
        IDA_PAD2;
        int fn_off=idata_len;
        IDA(0); IDA(0); /* hint = 0 */
        IDA_STR(win_imports[i].name);
        IDA_PAD2;
        /* fill INT entry — must be a bare RVA (bit63=0 => import-by-name
           via RVA to IMAGE_IMPORT_BY_NAME), NOT a full virtual address.
           Storing IMAGE_BASE+rva here makes the loader dereference an
           out-of-range RVA while binding imports, which crashes the
           process at startup before any of our code runs. */
        uint64_t hint_rva=(uint64_t)(uint32_t)(idata_rva+fn_off);
        uint8_t *p=idata_buf+int_off+i*8;
        for(int b=0;b<8;b++) p[b]=(uint8_t)(hint_rva>>(b*8));
        /* fill IAT entry (same initially, loader overwrites with real VA) */
        uint8_t *q=idata_buf+iat_off+i*8;
        for(int b=0;b<8;b++) q[b]=(uint8_t)(hint_rva>>(b*8));
        /* record IAT VA for code to call */
        import_thunks[i]=IMAGE_BASE+idata_rva+iat_off+i*8;
        win_imports[i].rva=idata_rva+iat_off+i*8;
    }

    /* --- patch `call [rip+disp32]` sites to point at their IAT slot ---
       emit_win32_helpers() emits `FF 15 <disp32>` placeholders with
       disp32=0 for GetStdHandle/WriteFile/ExitProcess. Those were never
       being patched, so at runtime the CPU dereferenced whatever bytes
       happened to follow the instruction as a function pointer and
       jumped there — an access violation almost immediately after the
       process started. Fix up disp32 = IAT_slot_VA - (address of next
       instruction), i.e. standard RIP-relative addressing. */
    for(int i=0;i<n_icalls;i++){
        int disp_off=icall_off[i];      /* offset of the disp32 field */
        int idx=icall_idx[i];           /* index into win_imports[] */
        uint64_t iat_slot_va=IMAGE_BASE+idata_rva+iat_off+(uint32_t)idx*8;
        uint64_t next_insn_va=code_va+disp_off+4; /* disp32 field is 4 bytes, followed by next insn */
        int32_t disp32=(int32_t)((int64_t)iat_slot_va-(int64_t)next_insn_va);
        code[disp_off  ]=(uint8_t)(disp32    );
        code[disp_off+1]=(uint8_t)(disp32>> 8);
        code[disp_off+2]=(uint8_t)(disp32>>16);
        code[disp_off+3]=(uint8_t)(disp32>>24);
    }

    /* DLL name */
    int dll_name_off=idata_len;
    { const char *d="KERNEL32.DLL"; while(*d)idata_buf[idata_len++]=(uint8_t)*d++; idata_buf[idata_len++]=0; }
    int idata_vsize=idata_len;

    /* fill IDT entry */
    uint8_t *idt=idata_buf+idt_off;
    /* INT RVA */ uint32_t v=idata_rva+int_off;
    idt[0]=v;idt[1]=v>>8;idt[2]=v>>16;idt[3]=v>>24;
    /* timestamp */ idt[4]=idt[5]=idt[6]=idt[7]=0;
    /* forwarder */ idt[8]=idt[9]=idt[10]=idt[11]=0;
    /* DLL name RVA */ v=idata_rva+dll_name_off;
    idt[12]=v;idt[13]=v>>8;idt[14]=v>>16;idt[15]=v>>24;
    /* IAT RVA */ v=idata_rva+iat_off;
    idt[16]=v;idt[17]=v>>8;idt[18]=v>>16;idt[19]=v>>24;
    /* null terminator already zeroed */

    /* --- patch calls to imported functions in code --- */
    /* We need to patch CALL instructions that reference Win32 functions.
       Since we can't do dynamic relocs easily, we use a trampoline:
       For each import, emit a small stub:
         mov rax, [rip+IAT_offset]
         jmp rax
       Then patch call sites to call the stub. */
    /* For now: the code calls __win_GetStdHandle, __win_WriteFile, __win_ExitProcess */
    /* These are resolved as stubs at the end of the code section */

    /*  write file  */
    FILE *fp=fopen(path,"wb");
    if(!fp){ perror(path); return 1; }

    /* MZ stub */
    static const uint8_t mz_stub[64]={
        0x4d,0x5a, /* MZ */
        0x90,0x00, /* bytes on last page */
        0x03,0x00, /* pages in file */
        0x00,0x00, /* relocations */
        0x04,0x00, /* size of header in paragraphs */
        0x00,0x00, /* min extra paragraphs */
        0xff,0xff, /* max extra paragraphs */
        0x00,0x00, /* initial SS */
        0xb8,0x00, /* initial SP */
        0x00,0x00, /* checksum */
        0x00,0x00, /* initial IP */
        0x00,0x00, /* initial CS */
        0x40,0x00, /* file address of reloc table */
        0x00,0x00, /* overlay number */
        0,0,0,0,0,0,0,0, /* reserved */
        0,0,0,0,          /* OEM id */
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* reserved */
        0x40,0x00,0x00,0x00  /* PE offset = 0x40 */
    };
    fwrite(mz_stub,1,64,fp);

    /* PE signature */
    fputc('P',fp);fputc('E',fp);fputc(0,fp);fputc(0,fp);

    /* COFF header */
    w2(fp,0x8664);  /* machine: AMD64 */
    w2(fp,3);       /* number of sections: .text, .rdata, .idata */
    w4(fp,0);       /* timestamp */
    w4(fp,0);       /* symbol table ptr */
    w4(fp,0);       /* number of symbols */
    w2(fp,0xf0);    /* optional header size = 240 bytes */
    w2(fp,0x0023);  /* characteristics: executable, large-address-aware,
                       relocs-stripped (no .reloc section is emitted below) */

    /* Optional header (PE32+) */
    w2(fp,0x020b);  /* magic: PE32+ */
    fputc(14,fp); fputc(0,fp); /* linker version */
    w4(fp,text_fsize);         /* size of code */
    w4(fp,rdata_fsize+align_up(idata_vsize,FILE_ALIGN)); /* size of init data */
    w4(fp,0);                  /* size of uninit data */
    w4(fp,text_rva+entry_off); /* entry point RVA */
    w4(fp,text_rva);           /* base of code */
    w8(fp,IMAGE_BASE);         /* image base */
    w4(fp,SECTION_ALIGN);      /* section alignment */
    w4(fp,FILE_ALIGN);         /* file alignment */
    w2(fp,6); w2(fp,0);        /* OS version */
    w2(fp,0); w2(fp,0);        /* image version */
    w2(fp,6); w2(fp,0);        /* subsystem version (Vista+) */
    w4(fp,0);                  /* win32 version */
    /* size of image: must cover all sections */
    uint32_t img_size=idata_rva+align_up(idata_vsize,SECTION_ALIGN);
    w4(fp,img_size);
    w4(fp,FILE_ALIGN);         /* size of headers (fits in FILE_ALIGN) */
    w4(fp,0);                  /* checksum */
    w2(fp,3);                  /* subsystem: console */
    w2(fp,0x20);                /* DLL characteristics: NX_COMPAT only.
                                    DYNAMIC_BASE (ASLR) is intentionally
                                    NOT set: this writer emits no .reloc
                                    section, so the image must always be
                                    loaded at IMAGE_BASE as-is. */
    w8(fp,0x100000);           /* stack reserve */
    w8(fp,0x1000);             /* stack commit */
    w8(fp,0x100000);           /* heap reserve */
    w8(fp,0x1000);             /* heap commit */
    w4(fp,0);                  /* loader flags */
    w4(fp,16);                 /* number of rva/sizes */
    /* data directories (16 entries × 8 bytes) */
    for(int i=0;i<16;i++){
        if(i==1){ w4(fp,idata_rva); w4(fp,idata_vsize); } /* import table */
        else if(i==12){ w4(fp,idata_rva+iat_off); w4(fp,(uint32_t)(N_IMPORTS*8)); } /* IAT */
        else { w4(fp,0); w4(fp,0); }
    }

    /* Section table */
    /* .text */
    fwrite(".text\0\0\0",1,8,fp);
    w4(fp,text_vsize); w4(fp,text_rva);
    w4(fp,text_fsize); w4(fp,text_rawoff);
    w4(fp,0); w4(fp,0); w2(fp,0); w2(fp,0);
    w4(fp,0x60000020); /* code, executable, readable */
    /* .rdata */
    fwrite(".rdata\0\0",1,8,fp);
    w4(fp,rdata_vsize); w4(fp,rdata_rva);
    w4(fp,rdata_fsize); w4(fp,rdata_rawoff);
    w4(fp,0); w4(fp,0); w2(fp,0); w2(fp,0);
    w4(fp,0x40000040); /* init data, readable */
    /* .idata */
    fwrite(".idata\0\0",1,8,fp);
    w4(fp,(uint32_t)idata_vsize); w4(fp,idata_rva);
    w4(fp,align_up(idata_vsize,FILE_ALIGN)); w4(fp,idata_rawoff);
    w4(fp,0); w4(fp,0); w2(fp,0); w2(fp,0);
    w4(fp,0xc0000040); /* init data, readable, writable (IAT needs write) */

    /* pad to text_rawoff */
    { long cur=ftell(fp); for(long i=cur;i<text_rawoff;i++) fputc(0,fp); }

    /* code */
    fwrite(code,1,code_len,fp);
    { long cur=ftell(fp); long end=text_rawoff+text_fsize; for(long i=cur;i<end;i++) fputc(0,fp); }

    /* .rdata: strings */
    fwrite(data,1,data_len,fp);
    { long cur=ftell(fp); long end=rdata_rawoff+rdata_fsize; for(long i=cur;i<end;i++) fputc(0,fp); }

    /* .idata */
    fwrite(idata_buf,1,idata_vsize,fp);
    { long cur=ftell(fp); long end=idata_rawoff+align_up(idata_vsize,FILE_ALIGN); for(long i=cur;i<end;i++) fputc(0,fp); }

    fclose(fp);
    return 0;
}

/* Return the IAT RVA for a given import name */
uint32_t pe_import_rva(const char *name){
    for(int i=0;i<(int)N_IMPORTS;i++)
        if(strcmp(win_imports[i].name,name)==0)
            return win_imports[i].rva;
    return 0;
}