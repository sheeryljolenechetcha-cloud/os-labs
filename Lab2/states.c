#include <stdio.h>
#include <unistd.h>
#include <time.h>

static void burn(int seconds) {              /* stay on the CPU */
    time_t end = time(NULL) + seconds;
    volatile unsigned long x = 0;
    while (time(NULL) < end) x++;
}

int main(void) {
    printf("PID %d. Watch me with: ./watch.sh %d\n", getpid(), getpid());
    fflush(stdout);
    sleep(5);                                /* phase 1 */
    printf("phase 2: computing\n");  fflush(stdout);
    burn(8);                                 /* phase 2 */
    printf("phase 3: sleeping\n");   fflush(stdout);
    sleep(8);                                /* phase 3 */
    printf("phase 4: type a line and press Enter\n"); fflush(stdout);
    char buf[64];
    fgets(buf, sizeof buf, stdin);           /* phase 4 */
    printf("phase 5: computing again, then exit\n"); fflush(stdout);
    burn(5);
    return 0;
}
