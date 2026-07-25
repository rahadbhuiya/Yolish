#include "yolish.h"
#include "vm.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>

static char src[1<<20];
char g_src_dir[512]={0};
extern char g_src_file[512];
extern char g_imported_modules[64][512];
extern int  g_nimported;
extern int  g_assert_count;

/* v2.1 tooling */
void ys_format(const char *src, int len);
int  ys_check(Node *prog, const char *filename);


/*  REPL */

static void run_repl(void){
    fprintf(stdout,"  Yolish v2.25 (Exploidus Runtime)\n");
    fprintf(stdout,"  Type \"help\" or \"exit\" to quit.\n\n");

    Env *env=env_new(NULL);
    static char line[8192];

    while(1){
        fprintf(stdout,"ys> ");
        fflush(stdout);

        if(!fgets(line,sizeof(line),stdin)) break;
        int ln=0; while(line[ln]&&line[ln]!='\n')ln++;
        line[ln]=0;
        if(ln==0) continue;
        if(strcmp(line,"exit")==0) break;
        if(strcmp(line,"help")==0){
            fprintf(stdout,"Commands: exit\n");
            fprintf(stdout,"Docs   : https://github.com/rahadbhuiya/Yolish\n\n");
            continue;
        }
        Lexer l; lex_init(&l,line,ln);
        Node *prog=parse_program(&l);
        Val result=eval_program(prog,env);
        if(result.type!=0){ ys_print_val(result); fprintf(stdout,"\n"); }
        fflush(stdout);
    }
    fprintf(stdout,"\nBye!\n");
}


/*  ys test <file.y> */

static int run_tests(const char *tfile) {
    FILE *f=fopen(tfile,"r");
    if(!f){ fprintf(stderr,"ys test: cannot open '%s'\n",tfile); return 1; }
    static char tsrc[1<<20];
    int tlen=(int)fread(tsrc,1,sizeof(tsrc)-1,f);
    fclose(f); tsrc[tlen]=0;

    strncpy(g_src_file,tfile,511);
    {   int last=-1,fl=(int)strlen(tfile);
        for(int i=0;i<fl;i++) if(tfile[i]=='/'||tfile[i]=='\\') last=i;
        if(last>=0){ strncpy(g_src_dir,tfile,last); g_src_dir[last]=0; }
        if(chdir(g_src_dir[0]?g_src_dir:".")){ }
        g_src_dir[0]=0;
    }

    Lexer l; lex_init(&l,tsrc,tlen);
    Node *prog=parse_program(&l);
    Env *env=env_new(NULL);

    /* pass 1: run non-test setup code */
    for(int i=0;i<prog->stmtc;i++){
        if(prog->stmts[i]->kind!=ND_TEST) eval_node(prog->stmts[i],env);
    }

    int passed=0, failed=0;
    printf("\nRunning tests in %s\n\n",tfile);

    /* pass 2: run test blocks */
    for(int i=0;i<prog->stmtc;i++){
        Node *stmt=prog->stmts[i];
        if(stmt->kind!=ND_TEST) continue;
        g_assert_count=0; g_throwing=0;
        eval_block(stmt->body,env);
        if(g_throwing){
            printf("  FAIL  %s\n    %s\n",stmt->sval,g_throw_msg);
            g_throwing=0; failed++;
        } else {
            printf("  PASS  %s (%d assertion%s)\n",
                   stmt->sval, g_assert_count, g_assert_count==1?"":"s");
            passed++;
        }
    }
    printf("\n%d passed, %d failed\n\n",passed,failed);
    return (failed>0)?1:0;
}


/*  ys fmt <file.y> */

static int run_fmt(const char *ffile) {
    FILE *f=fopen(ffile,"r");
    if(!f){ fprintf(stderr,"ys fmt: cannot open '%s'\n",ffile); return 1; }
    static char fsrc[1<<20];
    int flen=(int)fread(fsrc,1,sizeof(fsrc)-1,f);
    fclose(f); fsrc[flen]=0;
    ys_format(fsrc,flen);
    return 0;
}


/*  ys check <file.y> */

static int run_check(const char *cfile) {
    FILE *f=fopen(cfile,"r");
    if(!f){ fprintf(stderr,"ys check: cannot open '%s'\n",cfile); return 1; }
    static char csrc[1<<20];
    int clen=(int)fread(csrc,1,sizeof(csrc)-1,f);
    fclose(f); csrc[clen]=0;
    Lexer l; lex_init(&l,csrc,clen);
    Node *prog=parse_program(&l);
    int errs=ys_check(prog,cfile);
    return (errs>0)?1:0;
}


/*  ys vm <file.y>  —  v2.0 bytecode VM (experimental) */

static int run_vm(const char *vfile) {
    FILE *f=fopen(vfile,"r");
    if(!f){ fprintf(stderr,"ys vm: cannot open '%s'\n",vfile); return 1; }
    static char vsrc[1<<20];
    int vlen=(int)fread(vsrc,1,sizeof(vsrc)-1,f);
    fclose(f); vsrc[vlen]=0;

    strncpy(g_src_file,vfile,511);
    {   int last=-1,fl=(int)strlen(vfile);
        for(int i=0;i<fl;i++) if(vfile[i]=='/'||vfile[i]=='\\') last=i;
        if(last>=0){ strncpy(g_src_dir,vfile,last); g_src_dir[last]=0; }
        if(chdir(g_src_dir[0]?g_src_dir:".")){ }
        g_src_dir[0]=0;
    }

    Lexer l; lex_init(&l,vsrc,vlen);
    Node *prog=parse_program(&l);

    VMResult r=vm_interpret(prog);
    if(r==VM_COMPILE_ERROR){
        fprintf(stderr,"ys vm: this program uses a construct the v2.0 bytecode\n"
                        "       compiler doesn't support yet — falling back to\n"
                        "       the standard interpreter.\n\n");
        Env *env=env_new(NULL);
        eval_program(prog,env);
        return 0;
    }
    return (r==VM_RUNTIME_ERROR)?1:0;
}

/*  Usage */

static void usage(void){
    fprintf(stderr,
        "Usage:\n"
        "  ys                         start REPL\n"
        "  ys <file.y>                interpret file\n"
        "  ys -c <file.y>             compile to native binary\n"
        "  ys -c <file.y> -o <out>    compile with custom output name\n"
        "  ys -c <file.y> --target <t> compile for linux|windows|macos\n"
        "  ys test <file.y>           run test blocks\n"
        "  ys fmt  <file.y>           format source (prints to stdout)\n"
        "  ys check <file.y>          static check without running\n"
        "  ys vm <file.y>             run via the bytecode VM (experimental, v2.0)\n"
        "  ys --help                  show this help\n");
}


/*  main */

int main(int argc,char **argv){

    /* no args → REPL */
    if(argc==1){ run_repl(); return 0; }

    /* subcommands: test / fmt / check */
    if(argc>=2){
        if(strcmp(argv[1],"test")==0){
            if(argc<3){ fprintf(stderr,"Usage: ys test <file.y>\n"); return 1; }
            return run_tests(argv[2]);
        }
        if(strcmp(argv[1],"fmt")==0){
            if(argc<3){ fprintf(stderr,"Usage: ys fmt <file.y>\n"); return 1; }
            return run_fmt(argv[2]);
        }
        if(strcmp(argv[1],"check")==0){
            if(argc<3){ fprintf(stderr,"Usage: ys check <file.y>\n"); return 1; }
            return run_check(argv[2]);
        }
        if(strcmp(argv[1],"vm")==0){
            if(argc<3){ fprintf(stderr,"Usage: ys vm <file.y>\n"); return 1; }
            return run_vm(argv[2]);
        }
        if(strcmp(argv[1],"--help")==0||strcmp(argv[1],"-h")==0){
            usage(); return 0;
        }
    }

    /* normal interpret / compile */
    int   do_compile  = 0;
    const char *infile = NULL;
    const char *outfile= NULL;
    Target target = TARGET_LINUX;
#if defined(_WIN32)||defined(_WIN64)
    target=TARGET_WINDOWS;
#elif defined(__APPLE__)
    target=TARGET_MACOS;
#else
    target=TARGET_LINUX;
#endif

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-c")==0||strcmp(argv[i],"--compile")==0){
            do_compile=1;
        } else if((strcmp(argv[i],"-o")==0||strcmp(argv[i],"--output")==0)&&i+1<argc){
            outfile=argv[++i];
        } else if(strcmp(argv[i],"--target")==0&&i+1<argc){
            i++;
            if(strcmp(argv[i],"linux")==0)        target=TARGET_LINUX;
            else if(strcmp(argv[i],"windows")==0)  target=TARGET_WINDOWS;
            else if(strcmp(argv[i],"macos")==0||strcmp(argv[i],"darwin")==0) target=TARGET_MACOS;
            else { fprintf(stderr,"ys: unknown target '%s'\n",argv[i]); return 1; }
        } else if(argv[i][0]!='-'){
            infile=argv[i];
        } else {
            fprintf(stderr,"ys: unknown flag '%s'\n",argv[i]); return 1;
        }
    }

    if(!infile){ usage(); return 1; }

    FILE *f=fopen(infile,"r");
    if(!f){ fprintf(stderr,"ys: cannot open '%s'\n",infile); return 1; }
    int n=(int)fread(src,1,sizeof(src)-1,f);
    fclose(f); src[n]=0;

    {   int last_sep=-1;
        for(int di=0;infile[di];di++) if(infile[di]=='/'||infile[di]=='\\') last_sep=di;
        if(last_sep>=0){
            for(int i=0;i<last_sep&&i<510;i++) g_src_dir[i]=infile[i];
            g_src_dir[last_sep]=0;
            if(chdir(g_src_dir)){}
            g_src_dir[0]=0;
            strncpy(g_src_file,infile,511);
        }
    }

    Lexer l; lex_init(&l,src,n);
    Node *prog=parse_program(&l);

    if(do_compile){
        char out_buf[512];
        if(!outfile){
            strncpy(out_buf,infile,sizeof(out_buf)-10);
            int ol=(int)strlen(out_buf);
            if(ol>2&&out_buf[ol-2]=='.'&&out_buf[ol-1]=='y') out_buf[ol-2]=0;
            if(target==TARGET_WINDOWS) strcat(out_buf,".exe");
            outfile=out_buf;
        }
        int rc=ys_compile(prog,target,outfile);
        if(rc!=0) return 1;
    } else {
        Env *env=env_new(NULL);
        eval_program(prog,env);
    }
    return 0;
}