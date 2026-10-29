#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        printf("child %d: done, exiting now\n", getpid());
        return 0;                         /* child ends at once */
    }
    printf("parent %d: not calling wait, sleeping 30 s\n", getpid());
    sleep(30);                            /* child is a zombie meanwhile */
    return 0;
}
