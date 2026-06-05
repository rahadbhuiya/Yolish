#include "yolish.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif
#include <string.h>

static char src[1<<20];
char g_src_dir[512] = {0};

typedef enum { TARGET_LINUX, TARGET_WINDOWS, TARGET_MACOS } Target;
void ys_compile(Node *prog, Target target, const char *outfile);

static void run_repl(void){
    char line[4096];
    Env *env=env_new(NULL);

#ifdef _WIN32
    SetConsoleOutputCP(65001);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    int ansi_ok = 0;
    if(GetConsoleMode(hOut, &dwMode)){
        if(SetConsoleMode(hOut, dwMode | 0x0004))
            ansi_ok = 1;
    }
#else
    int ansi_ok = 1;
#endif

    if(ansi_ok){
        fprintf(stdout, "\033[33mYolish v1.0\033[0m (Exploidus Runtime)\n");
        fprintf(stdout, "Type \033[36m\"help\"\033[0m or \033[36m\"exit\"\033[0m to quit.\n\n");
    } else {
        fprintf(stdout, "Yolish v1.0 (Exploidus Runtime)\n");
        fprintf(stdout, "Type \"help\" or \"exit\" to quit.\n\n");
    }

    fflush(stdout);
    while(1){
        if(ansi_ok)
            fprintf(stdout,"\033[32mys>\033[0m ");
        else
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

static void usage(void){
    fprintf(stderr,
        "Usage:\n"
        "  ys                         start REPL\n"
        "  ys <file.y>                interpret file\n"
        "  ys -c <file.y>             compile for current OS\n"
        "  ys -c <file.y> -o <out>    compile with output path\n"
        "  ys -c <file.y> --target linux|windows|macos\n"
    );
}

int main(int argc, char **argv){
    if(argc<2){ run_repl(); return 0; }

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
            if(strcmp(argv[i],"linux")==0)   target=TARGET_LINUX;
            else if(strcmp(argv[i],"windows")==0) target=TARGET_WINDOWS;
            else if(strcmp(argv[i],"macos")==0||strcmp(argv[i],"darwin")==0) target=TARGET_MACOS;
            else { fprintf(stderr,"ys: unknown target '%s'\n",argv[i]); return 1; }
        } else if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){
            usage(); return 0;
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

    {
        int di=0,last_sep=-1;
        while(argv[argc-1][di]){
            if(infile[di]=='/'||infile[di]=='\\') last_sep=di;
            di++;
        }
        if(last_sep>=0){
            for(int i=0;i<=last_sep&&i<510;i++) g_src_dir[i]=infile[i];
            g_src_dir[last_sep+1]=0;
            if(chdir(g_src_dir)){}
        }
    }

    Lexer l; lex_init(&l,src,n);
    Node *prog=parse_program(&l);

    if(do_compile){
        char out_buf[512];
        if(!outfile){
            strncpy(out_buf,infile,sizeof(out_buf)-10);
            int ol=strlen(out_buf);
            if(ol>2&&out_buf[ol-2]=='.'&&out_buf[ol-1]=='y') out_buf[ol-2]=0;
            if(target==TARGET_WINDOWS) strcat(out_buf,".exe");
            outfile=out_buf;
        }
        ys_compile(prog,target,outfile);
    } else {
        Env *env=env_new(NULL);
        eval_program(prog,env);
    }
    return 0;
}