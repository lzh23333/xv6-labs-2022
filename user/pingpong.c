#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main() {
    int ping_fd[2], pong_fd[2];
    char buf[1];
    int pid;
    if(pipe(ping_fd) == -1 || pipe(pong_fd) == -1) {
        fprintf(2, "pipe error\n");
        exit(1);
    }
    
    pid = fork();
    if (pid == 0) {
        // child
        close(ping_fd[1]);
        pid = getpid();
        read(ping_fd[0], buf, 1);
        printf("%d: received ping\n", pid);
        write(pong_fd[1], buf, 1);
        close(pong_fd[1]);
        close(ping_fd[0]);
        exit(0);
    }
    else if (pid > 0) {
        // parent
        close(pong_fd[1]);
        close(ping_fd[1]);
        pid = getpid();
        write(ping_fd[1], buf, 1);
        close(ping_fd[1]);
        read(pong_fd[0], buf, 1);
        close(pong_fd[1]);
        printf("%d: received pong\n", pid);
        exit(0);
    }
    else {
        fprintf(2, "fork error\n");
        exit(1);
    }
}
