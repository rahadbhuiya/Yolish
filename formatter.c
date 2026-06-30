/*  formatter.c  —  v2.1: ys fmt
 *  Text-based source formatter for .y files.
 *  Normalizes indentation, removes trailing whitespace,
 *  preserves strings and comments.
 */

#include "yolish.h"
#include <stdio.h>
#include <string.h>

void ys_format(const char *src, int len) {
    int indent   = 0;
    int i        = 0;
    int col      = 0;      /* current column */
    int in_str   = 0;      /* inside "..." */
    int in_raw   = 0;      /* inside r"..." or `...` */
    int in_mstr  = 0;      /* inside `...` */
    int in_cmt   = 0;      /* inside -- comment */
    char line_buf[4096];
    int  llen    = 0;

    while(i <= len) {
        char c = (i < len) ? src[i] : '\n';  /* flush at EOF */

        /*  handle newline: flush current line  */
        if(c == '\n' || i == len) {
            /* trim trailing whitespace */
            while(llen > 0 && (line_buf[llen-1]==' ' || line_buf[llen-1]=='\t')) llen--;
            line_buf[llen] = 0;

            /* blank line: print as-is */
            if(llen == 0) {
                putchar('\n');
            } else {
                /* print with correct indentation if it's not a comment/blank */
                char first = 0;
                int  fi = 0;
                while(fi < llen && (line_buf[fi]==' '||line_buf[fi]=='\t')) fi++;
                if(fi < llen) first = line_buf[fi];

                /* calculate target indent */
                int target_indent = indent;
                if(first == '}') target_indent = (indent > 0) ? indent - 1 : 0;

                for(int j=0; j<target_indent*4; j++) putchar(' ');
                /* print content without leading whitespace */
                fputs(line_buf+fi, stdout);
                putchar('\n');
            }

            llen = 0; col = 0;
            in_cmt = 0;
            if(i == len) break;
            i++; continue;
        }

        /* track comment state  */
        if(!in_str && !in_raw && !in_mstr && !in_cmt) {
            if(c == '-' && i+1<len && src[i+1]=='-') in_cmt = 1;
        }

        /*  track string state  */
        if(!in_cmt) {
            if(!in_str && !in_raw && !in_mstr) {
                if(c == '`')              { in_mstr = 1; }
                else if(c=='"')           { in_str  = 1; }
                else if(c=='r'&&i+1<len&&src[i+1]=='"') { in_raw=1; i++; if(llen<4090)line_buf[llen++]='r'; }
            } else if(in_str  && c=='"') { in_str  = 0; }
            else if(in_raw  && c=='"') { in_raw  = 0; }
            else if(in_mstr && c=='`') { in_mstr = 0; }
        }

        /*  track indent level from { }  */
        if(!in_str && !in_raw && !in_mstr && !in_cmt) {
            if(c == '{') indent++;
            if(c == '}') { /* indent decremented when we flush the line */ }
        }

        if(llen < 4090) line_buf[llen++] = c;
        col++;
        i++;
    }
}