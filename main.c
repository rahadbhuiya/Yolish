#include "yolish.h"
#include <unistd.h>

static char src[65536];
char g_src_dir[512] = {0}; /* set from argv[1] */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Yolish — the official programming language of Exploidus OS\n");
        fprintf(stderr, "Author  : .Bhuiya\n");
        fprintf(stderr, "Version : v0.3\n");
        fprintf(stderr, "Usage   : ys <file.y>\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "ys: cannot open '%s'\n", argv[1]); return 1; }
    int n = (int)fread(src, 1, sizeof(src)-1, f);
    fclose(f); src[n] = 0;

    /* extract directory and chdir into it so imports resolve relative to source */
    {
        int di=0, last_sep=-1;
        while(argv[1][di]) { if(argv[1][di]=='/'||argv[1][di]=='\\') last_sep=di; di++; }
        if(last_sep>=0){
            for(int i=0;i<=last_sep&&i<510;i++) g_src_dir[i]=argv[1][i];
            g_src_dir[last_sep+1]=0;
            if(chdir(g_src_dir)!=0) { fprintf(stderr,"ys: warning: cannot chdir to source directory\n"); }
        }
    }

    /* file already read above */
    if(0){ /* dummy block to avoid duplicate fopen below */ 
    } /* end dummy block */

    Lexer l; lex_init(&l, src, n);
    Node *prog = parse_program(&l);
    Env  *env  = env_new(NULL);
    eval_program(prog, env);
    return 0;
}