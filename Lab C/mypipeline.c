#include "linux/limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int main() {
    int status1, status2;
    int fd[2];
    fprintf(stderr,"(parent_process>forking...)\n");
    // Create the pipe
    if (pipe(fd) == -1) {
        perror("pipe");
        _exit(1);
    }

    // Fork the first child
    pid_t child1 = fork();
    
    if (child1 == -1) {
        perror("fork");
        _exit(1);
    }
    fprintf(stderr,"(parent_process>created process with id: %d)\n",child1);

    if (child1 == 0) {
        // Inside child1
        fprintf(stderr,"(child1>redirecting stdout to the write end of the pipe…)\n");

        // Close standard output
        close(STDOUT_FILENO);

        // Duplicate the write end of the pipe
        dup(fd[1]);

        // Close the duplicated file descriptor
        close(fd[1]);

        // Execute "ls -l"
        char *args[] = {"ls", "-l", 0};
        fprintf(stderr,"(child1>going to execute cmd: %s)\n", args[0]);
        execvp(args[0], args);
    } else {
        // Inside parent
        fprintf(stderr,"(parent_process>closing the write end of the pipe…)\n");
        // Close the write end of the pipe
        close(fd[1]);

        // Fork the second child
        pid_t child2 = fork();

        if (child2 == -1) {
            perror("fork");
            exit(1);
        }
        fprintf(stderr,"(parent_process>created process with id: %d)\n",child2);

        if (child2 == 0) {
            // Inside child2
            fprintf(stderr,"(child2>redirecting stdin to the write end of the pipe…)\n");
            // Close standard input
            close(STDIN_FILENO);

            // Duplicate the read end of the pipe
            dup(fd[0]);

            // Close the duplicated file descriptor
            close(fd[0]);

            // Execute "tail -n 2"
            char *args[] = {"tail", "-n", "2", 0};
            fprintf(stderr,"(child2>going to execute cmd: %s)\n", args[0]);
            execvp(args[0], args);
        } else {
            // Inside parent again
            fprintf(stderr,"(parent_process>closing the read end of the pipe…)\n");
            // Close the read end of the pipe
            close(fd[0]);
            fprintf(stderr,"(parent_process>waiting for child processes to terminate…)\n");
            // Wait for the child processes to terminate
            waitpid(child1, &status1, 0);
            waitpid(child2, &status2, 0);
        }
    }
    fprintf(stderr,"(parent_process>)>exiting...\n");
    return 0;
}
