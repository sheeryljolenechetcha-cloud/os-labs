#include <unistd.h>

int main(void) {
    pid_t pid = vfork();
    if (pid == 0) {
        sleep(20);      /* for this demo only: a vfork child should call exec or _exit at once */
        _exit(0);
    }
    return 0;
}
