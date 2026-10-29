#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 4; i++) {
            printf("child %d: my parent is %d\n", getpid(), getppid());
            sleep(2);
        }
        return 0;
    }
    sleep(1);
    printf("parent %d: leaving without my child\n", getpid());
    return 0;                             /* parent ends first */
}
