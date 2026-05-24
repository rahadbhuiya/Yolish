#include "yolish.h"

static char src[65536];

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,"Yolish is the official programming language of **Exploidus OS**\nAuthor: .Bhuiya\n");
        fprintf(stderr, "Version: Yolish v0.1\nUsage: ys <file.y>\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "ys: cannot open '%s'\n", argv[1]); return 1; }
    int n = (int)fread(src, 1, sizeof(src)-1, f);
    fclose(f); src[n] = 0;

    Lexer l; lex_init(&l, src, n);
    Node *prog = parse_program(&l);
    Env  *env  = env_new(NULL);
    eval_program(prog, env);
    return 0;
}
