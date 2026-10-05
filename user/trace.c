#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[]) {
    if (argc <= 2) {
        printf("Error: you must enter a program to be traced\n");
        exit(1);
    }
    trace(atoi(argv[1]));
    // char* new_argv[] = (argv + 2, argv + argc);
    exec(argv[2], argv + 2);
    exit(2);
}