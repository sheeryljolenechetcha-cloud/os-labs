#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    char line[256];
    for (;;) {
        printf("minish> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;     /* Ctrl-D */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;
        if (strcmp(line, "exit") == 0) break;

        char *argv[16];
        int argc = 0;
        for (char *tok = strtok(line, " "); tok && argc < 15; tok = strtok(NULL, " "))
            argv[argc++] = tok;
        argv[argc] = NULL;
        if (argc == 0) continue;

        /* built-in: must run in the parent, or the change is lost */
        if (strcmp(argv[0], "cd") == 0) {
            const char *dir = argv[1] ? argv[1] : getenv("HOME");
            if (chdir(dir) != 0) perror("cd");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[0], argv);
            perror(argv[0]);
            exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        printf("[exit %d]\n", WEXITSTATUS(status));
    }
    return 0;
}
