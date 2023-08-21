#include <linux/limits.h>
#include "LineParser.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>  // for file related system calls (open)
#include <sys/types.h>  // for system calls related to types
#include <sys/stat.h>  // for file permission bits
#include <sys/wait.h>  // for waitpid system call
#include <errno.h>

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

typedef struct process{
    cmdLine* cmd;                         /* the parsed command line*/
    pid_t pid; 		                  /* the process id that is running the command*/
    int status;                           /* status of the process: RUNNING/SUSPENDED/TERMINATED */
    struct process *next;	                  /* next process in chain */
} process;


process* process_list = NULL;
int debug = 0;

char* history[HISTLEN] = {NULL};
int newest = 0;
int oldest = 0;
int histCount = 0;

void addHistory(char* cmdline) {
    if (history[newest] != NULL) {
        free(history[newest]);
    }
    history[newest] = strdup(cmdline);
    newest = (newest + 1) % HISTLEN;
    if (histCount < HISTLEN) {
        histCount++;
    }
    if (newest == oldest) {
        oldest = (oldest + 1) % HISTLEN;
    }
}

void printHistory() {
    for (int i = 0; i < histCount; i++) {
        int index = (oldest + i) % HISTLEN;
        printf("%d: %s\n", i + 1, history[index]);
    }
}



void addProcess(process** process_list, cmdLine* cmd, pid_t pid){
    // Create a new process node
    process* newProcess = malloc(sizeof(process));
    newProcess->cmd = cmd;
    newProcess->pid = pid;
    newProcess->status = RUNNING;
    newProcess->next = NULL;

    // Check if the list is empty. If so, set the new node as the head
    if (*process_list == NULL){
        *process_list = newProcess;
    }
    else{
        // If the list is not empty, add the new process to the end of the list
        process* current = *process_list;
        while (current->next){
            current = current->next;
        }
        current->next = newProcess;
    }
}
void deleteProcess(process** process_list, pid_t pid){
    process* current = *process_list;
    process* prev = NULL;

    while (current){
        if (current->pid == pid){
            // Found the process to delete
            if (prev == NULL){
                // Process to delete is the first process
                *process_list = current->next;
            }
            else {
                // Process to delete is not the first process
                prev->next = current->next;
            }

            // Free memory allocated to the process's cmdLine
            freeCmdLines(current->cmd);
            free(current);
            return;
        }

        prev = current;
        current = current->next;
    }
}

void updateProcessStatus(process* process_list, int pid, int status) {
    process* curr = process_list;
    while (curr != NULL) {
        if (curr->pid == pid) {
            curr->status = status;
            break;
        }
        curr = curr->next;
    }
}





void freeProcessList(process* process_list) {
    while(process_list) {
        process* next = process_list->next;
        freeCmdLines(process_list->cmd);
        free(process_list);
        process_list = next;
    }
}


void updateProcessList(process **process_list) {
    process* curr = *process_list;
    while (curr != NULL) {
        int status;
        pid_t returnPid = waitpid(curr->pid, &status, WNOHANG);
        if (returnPid == -1) {
            // Process has finished or an error occurred
            if (errno == ECHILD) {
                // The process has already exited
                updateProcessStatus(*process_list, curr->pid, TERMINATED);
            } else {
                perror("waitpid failed");
            }
        } else if (returnPid == 0) {
            // Process has not changed
        } else {
            if (WIFSTOPPED(status)) {
                updateProcessStatus(*process_list, curr->pid, SUSPENDED);
            } else if (WIFCONTINUED(status)) {
                updateProcessStatus(*process_list, curr->pid, RUNNING);
            } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                // The child has exited
                updateProcessStatus(*process_list, curr->pid, TERMINATED);
            }
        }
        curr = curr->next;
    }
}



void printProcessList(process** process_list){
    updateProcessList(process_list);
    
    printf("Index\tPID\tStatus\t\tCommand\n");

    process* current = *process_list;
    int index = 0;
    while (current){
        char* status;
        switch(current->status){
            case RUNNING:
                status = "RUNNING  ";
                break;
            case SUSPENDED:
                status = "SUSPENDED";
                break;
            case TERMINATED:
                status = "TERMINATED";
                break;
        }

        printf("%-6d\t%-6d\t%-6s\t%s", index, current->pid, status, current->cmd->arguments[0]);

        for (int i = 1; i < current->cmd->argCount; i++)
        {
            printf("%s ", current->cmd->arguments[i]);
        }
        printf("\n");

        process* next = current->next;

        // Delete the process if it was terminated
        if (current->status == TERMINATED){
            deleteProcess(process_list, current->pid);
        }

        current = next;
        index++;
    }
}






void execute(cmdLine *pCmdLine, process** process_list) {
    int pid;
    int pipefd[2];  // pipe file descriptors
    int pipeCreated = 0;  // flag to check if pipe was created

    // If there is a next command, we need to create a pipe
    if (pCmdLine->next) {
        if (pipe(pipefd) == -1) {
            perror("pipe");
            _exit(1);
        }
        pipeCreated = 1;
    }

    pid = fork();
    if(pid == -1){
        printf("fork failed\n");
        exit(1);
    }
    else if(pid == 0){
        // If this command should output to the next command, redirect output to the pipe
        if (pCmdLine->next) {
            close(pipefd[0]);  // Close the unused read end
            if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                perror("dup2 pipe");
                _exit(1);
            }
            close(pipefd[1]);  // Close the write end which is now duplicated
        }

        // Redirect input if necessary
        else if (pCmdLine->inputRedirect != NULL) {
            int input_fd = open(pCmdLine->inputRedirect, O_RDONLY);
            if (input_fd == -1) {
                perror("open input file failed\n");
                _exit(1);
            }
            if (dup2(input_fd, 0) == -1) {
                perror("dup2 input failed\n");
                _exit(1);
            }
            close(input_fd);
        }

        // Redirect output if necessary
        else if (pCmdLine->outputRedirect != NULL) {
            int output_fd = open(pCmdLine->outputRedirect, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
            if (output_fd == -1) {
                perror("open output file failed\n");
                _exit(1);
            }
            if (dup2(output_fd, 1) == -1) {
                perror("dup2 output failed\n");
                _exit(1);
            }
            close(output_fd);
        }

        if(execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1){
            perror("execvp failed\n");
            exit(1);
        }
    }

    else {
        // Parent process
        addProcess(process_list, pCmdLine, pid);
        if (pipeCreated) {
            // Fork another child to execute the next command
            pid_t secondChild = fork();
            if (secondChild == -1) {
                perror("fork failed\n");
                exit(1);
            }
            else if (secondChild == 0) {
                // Inside second child
                close(pipefd[1]);  // Close the unused write end
                if (dup2(pipefd[0], STDIN_FILENO) == -1) {
                    perror("dup2 pipe");
                                        _exit(1);
                }
                close(pipefd[0]);  // Close the read end which is now duplicated

                if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
                    perror("execvp failed\n");
                    _exit(1);
                }
            }
            else {
                addProcess(process_list, pCmdLine->next, secondChild);
                // In parent process, close both ends of the pipe
                close(pipefd[0]);
                close(pipefd[1]);

                // Wait for the second child to terminate
                if (pCmdLine->next->blocking) {
                    waitpid(pid, NULL, 0);  // Waiting for the first child
                    waitpid(secondChild, NULL, 0);
                }
            }
        }
        // If no pipe was created, wait for the first child if necessary
        else if (pCmdLine->blocking) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void executeHistoryCommand(int n, process** process_list) {
    if (n < 1 || n > histCount) {
        printf("Command not in history.\n");
    } else {
        int index = (oldest + n - 1) % HISTLEN;
        cmdLine* line = parseCmdLines(history[index]);
        addHistory(history[index]);
        execute(line, process_list);
    }
}


int main(int argc, char **argv)
{
    int pid;
    if (argc>1) {
        for(int i = 1 ; i<argc ; i++){
            if(strcmp(argv[i] , "-d") == 0)
                debug = 1;
        }
    }
    while (1) {
        char pathName[PATH_MAX];
        getcwd(pathName, sizeof(pathName));
        printf("current working directory: %s\n", pathName);
        char input[2048];
        printf("enter command lines: ");
        if(fgets(input,sizeof(input),stdin)<=0)
            printf("couln't get command lines\n");

        input[strcspn(input,"\n")]='\0';

        if(strncmp(input,"quit" , 4)==0) 
            exit(0);

        else if(strncmp(input, "cd",2) == 0) {   // 1c
            if(chdir(input+3) == -1)
                fprintf(stderr, "cd failed\n");
        }

        else if(strncmp(input, "suspend",7) == 0) {
            pid=atoi(input+8);
            if (kill(pid, SIGTSTP) == -1) {
                perror("kill(SIGTSTP) failed\n");
                return 1;
            }
            printf("Process with PID %d suspended\n", pid);
            updateProcessStatus(process_list, pid, SUSPENDED);
        }

        else if (strncmp(input, "wake",4) == 0) {
            pid=atoi(input+5);
            if (kill(pid, SIGCONT) == -1) {
                perror("kill(SIGCONT) failed\n");
                return 1;
            }
            printf("Process with PID %d woke up\n", pid);
            updateProcessStatus(process_list, pid, RUNNING);
        }

        else if (strncmp(input, "kill",4) == 0) {
            pid=atoi(input+5);
            if (kill(pid, SIGKILL) == -1) {
                perror("kill(SIGKILL) failed\n");
                return 1;
            }
            printf("Process with PID %d killed\n", pid);
            updateProcessStatus(process_list, pid, TERMINATED);
        }
        else if(strncmp(input, "procs",5) == 0) {
            printProcessList(&process_list);
        }
        else if(strncmp(input, "history",7) == 0) {
            printHistory();
        }
        else if (strncmp(input, "!!", 2) == 0) {
            if (histCount == 0) {
                printf("No commands in history.\n");
            } else {
                executeHistoryCommand(histCount, &process_list);
            }
        }
        else if (strncmp(input, "!", 1) == 0) {
            int n = atoi(input + 1);
            executeHistoryCommand(n, &process_list);
        }
        else {
        cmdLine * line = parseCmdLines(input);
        addHistory(input);
        execute(line, &process_list);
        }
    }
    return 0;
}


