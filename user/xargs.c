#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"


char* args[MAXARG] = {0};
char* command;
const int MAXLEN = 1024;


int readline(int argc) {
    // if readline suceess, ereturn 1
    // fail(no more line), return 0
    char cbuf;
    int read_cnt = 0;
    int idx = argc;
    char strbuf[MAXLEN];
    memset(strbuf, 0, sizeof(strbuf));
    while (read(0, &cbuf, 1) == 1) {
        if (idx >= MAXARG) {
            fprintf(2, "xargs: argments too long\n");
            exit(1);
        }
        if (cbuf == '\n' && idx == argc && read_cnt == 0) return 0;
        else if (cbuf == ' ' && read_cnt == 0) continue;
        else if (cbuf == ' ' || cbuf == '\n') {
            // read a word done.
            args[idx] = (char*)malloc(strlen(strbuf)+1);
            strcpy(args[idx], strbuf);
            // printf("read word: %s ", strbuf);
            read_cnt = 0;
            idx++;
            if (cbuf == '\n') {
                return 1;
            }
        }
        else {
            // read a normal char
            strbuf[read_cnt++] = cbuf;
        }
    }
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs command2 ...\n");
        exit(1);
    }

    int pid;
    // initialize command params
    command = argv[1];
    
    for (int i = 1; i < argc; i++) {
        args[i-1] = argv[i];
    }

    while (readline(argc-1)) {
        pid = fork();
        if (pid < 0) {
            fprintf(2, "fork error\n");
            exit(1);
        }
        else if (pid == 0) {
            // printf("child exec args: \n");
            // for (int i = 0; i < MAXARG && args[i] != 0; i++) {
            //     printf("%s\n", args[i]);
            // }
            exec(command, args);
            fprintf(2, "exec xargs fail\n");
            exit(1);
        }
        else {
            wait(0);
            // for (int i = argc-1; i < MAXARG; i++) {
            //     // free malloc space
            //     free(args[i]);
            //     args[i] = 0;
            // }
        }
    }

    exit(0);
    
}
