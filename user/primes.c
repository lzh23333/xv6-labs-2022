#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int parent_pipe[2]; // parent -> child
int child_pipe[2];  // child -> parent
const int max_val = 35;
const int end_val = -1;

void prime_init(int write_fd) {
    for (int i = 2; i <= max_val; i++) {
        write(write_fd, &i, sizeof(int));
    }
    write(write_fd, &end_val, sizeof(int));
}


int prime_iteration(int write_fd, int read_fd) {
    int buf = 0;
    int n = end_val;
    int size_read = read(read_fd, &n, sizeof(int));
    if (size_read < 0) {
        fprintf(2, "read error\n");
        exit(1);
    }
    if (size_read < sizeof(int) || n == end_val) {
        // no more data, exit
        return 0;
    }
    
    while ((size_read = read(read_fd, &buf, sizeof(int))) > 0) {
        if (buf == end_val) break;
        if (buf % n != 0) {
            write(write_fd, &buf, sizeof(int));
        }
    }
    printf("prime %d\n", n);
    write(write_fd, &end_val, sizeof(int));
    return 1;
}


int main() {
    int pid;
    pipe(parent_pipe);
    pipe(child_pipe);

    pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
    }
    
    else if (pid == 0) {
        // child
        close(child_pipe[0]);
        close(parent_pipe[1]);
        while (prime_iteration(child_pipe[1], parent_pipe[0]));
        close(child_pipe[1]);
        close(parent_pipe[0]);
        exit(1);
    }
    else {
        // parent
        close(parent_pipe[0]);
        close(child_pipe[1]);
        prime_init(parent_pipe[1]);
        while (prime_iteration(parent_pipe[1], child_pipe[0]));
        close(child_pipe[0]);
        close(parent_pipe[1]);
        wait(0);
        exit(1);
    }
}