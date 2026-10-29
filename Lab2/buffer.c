#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("parent pid=%d\n", getpid());
    fork();
    return 0;
}
