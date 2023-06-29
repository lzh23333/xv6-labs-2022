#include "kernel/types.h"
#include "user/user.h"

int isdigit(char c) {
    if(c <= '9' && c >= '0') return 1;
    return 0;
}

int is_valid_digit(const char* s) {
    // check if s is valid non-negtive number
    int len = strlen(s);
    int idx = 0;
    for (; idx < len && isdigit(s[idx]); idx++);
    if (idx == len) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    int seconds;
    if (argc <= 1) {
        // no argments, print error message
        fprintf(2, "Usage: sleep seconds\n");
        exit(1);
    }

    if (argc > 2) {
        fprintf(2, "sleep takes only one argment\n");
        exit(1);
    }

    if (is_valid_digit(argv[1]) == 0) {
        fprintf(2, "sleep argment should be non-negtive number\n");
        exit(1);
    }
    seconds = atoi(argv[1]);
    sleep(seconds);
    exit(0);
}