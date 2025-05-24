#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>

#define MAX_INPUT_LENGTH 1024
#define MAX_ARGS 64
#define MAX_COMMANDS 10
#define MAX_HISTORY 100

char history[MAX_HISTORY][MAX_INPUT_LENGTH];
int history_count = 0;

void add_to_history(const char *cmd) {
    if (cmd[0] == '\0') return;
    if (history_count < MAX_HISTORY) {
        strncpy(history[history_count], cmd, MAX_INPUT_LENGTH - 1);
        history[history_count][MAX_INPUT_LENGTH - 1] = '\0';
        history_count++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strncpy(history[MAX_HISTORY - 1], cmd, MAX_INPUT_LENGTH - 1);
        history[MAX_HISTORY - 1][MAX_INPUT_LENGTH - 1] = '\0';
    }
}

void print_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s\n", i + 1, history[i]);
    }
}

int parse_command(char *cmd, char **args, char **input_file, char **output_file, int *append) {
    *input_file = NULL;
    *output_file = NULL;
    *append = 0;

    char *token = strtok(cmd, " \t");
    int i = 0;
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t");
            if (!token) {
                fprintf(stderr, "Syntax error: expected input file after <\n");
                return -1;
            }
            *input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t");
            if (!token) {
                fprintf(stderr, "Syntax error: expected output file after >\n");
                return -1;
            }
            *output_file = token;
            *append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t");
            if (!token) {
                fprintf(stderr, "Syntax error: expected output file after >>\n");
                return -1;
            }
            *output_file = token;
            *append = 1;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;
    return 0;
}

int execute_single_command(char **args, char *input_file, char *output_file, int append) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    } else if (pid == 0) {
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);

        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) {
                perror("open input file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (output_file) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(output_file, flags, 0644);
            if (fd < 0) {
                perror("open output file");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(args[0], args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
}

int execute_pipeline(char **commands[], int num_commands) {
    int prev_pipe = -1;
    int status = 0;
    pid_t pids[MAX_COMMANDS];

    for (int i = 0; i < num_commands; i++) {
        int pipefd[2];
        if (i < num_commands - 1 && pipe(pipefd) == -1) {
            perror("pipe");
            return -1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        } else if (pid == 0) {
            struct sigaction sa;
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGINT, &sa, NULL);

            if (i > 0) {
                dup2(prev_pipe, STDIN_FILENO);
                close(prev_pipe);
            }

            if (i < num_commands - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }

            char *args[MAX_ARGS];
            char *input_file = NULL, *output_file = NULL;
            int append = 0;
            char cmd_copy[MAX_INPUT_LENGTH];
            strncpy(cmd_copy, *commands[i], MAX_INPUT_LENGTH);
            cmd_copy[MAX_INPUT_LENGTH - 1] = '\0';

            if (parse_command(cmd_copy, args, &input_file, &output_file, &append) < 0)
                exit(EXIT_FAILURE);

            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd < 0) {
                    perror("open input file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (output_file) {
                int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                int fd = open(output_file, flags, 0644);
                if (fd < 0) {
                    perror("open output file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            pids[i] = pid;
            if (i > 0) close(prev_pipe);
            if (i < num_commands - 1) {
                prev_pipe = pipefd[0];
                close(pipefd[1]);
            }
        }
    }

    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], &status, 0);
    }
    return WEXITSTATUS(status);
}

void trim_whitespace(char **str) {
    while (**str == ' ' || **str == '\t') (*str)++;
    char *end = *str + strlen(*str) - 1;
    while (end > *str && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    *(end + 1) = '\0';
}

int split_commands(char *input, char *delim, char **commands, int max_cmds) {
    int count = 0;
    char *token = strtok(input, delim);
    while (token && count < max_cmds) {
        trim_whitespace(&token);
        if (*token != '\0') commands[count++] = token;
        token = strtok(NULL, delim);
    }
    return count;
}

int main() {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    char input[MAX_INPUT_LENGTH];

    while (1) {
        printf("sh> ");
        fflush(stdout);

        if (!fgets(input, MAX_INPUT_LENGTH, stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        add_to_history(input);

        char *groups[MAX_COMMANDS];
        int num_groups = split_commands(input, ";", groups, MAX_COMMANDS);

        for (int i = 0; i < num_groups; i++) {
            char *group = groups[i];
            if (strlen(group) == 0) continue;

            if (strcmp(group, "exit") == 0) exit(0);
            else if (strcmp(group, "history") == 0) {
                print_history();
                continue;
            } else if (strncmp(group, "cd ", 3) == 0) {
                char *dir = group + 3;
                trim_whitespace(&dir);
                if (chdir(dir) == -1){
                    perror("cd");
                }
                continue;
            }

            char *sub_commands[MAX_COMMANDS];
            int num_sub = split_commands(group, "&&", sub_commands, MAX_COMMANDS);
            int last_status = 0;

            for (int j = 0; j < num_sub; j++) {
                if (last_status != 0) break;

                char *pipeline[MAX_COMMANDS];
                int num_pipes = split_commands(sub_commands[j], "|", pipeline, MAX_COMMANDS);
                if (num_pipes == 0) continue;

                char *commands[MAX_COMMANDS][MAX_ARGS];
                for (int k = 0; k < num_pipes; k++) {
                    char *args[MAX_ARGS];
                    char *input_file = NULL, *output_file = NULL;
                    int append = 0;
                    char cmd_copy[MAX_INPUT_LENGTH];
                    strncpy(cmd_copy, pipeline[k], MAX_INPUT_LENGTH);
                    cmd_copy[MAX_INPUT_LENGTH - 1] = '\0';

                    if (parse_command(cmd_copy, args, &input_file, &output_file, &append) < 0) {
                        last_status = 1;
                        break;
                    }

                    commands[k][0] = cmd_copy;
                }

                if (num_pipes == 1) {
                    char *args[MAX_ARGS];
                    char *input_file = NULL, *output_file = NULL;
                    int append = 0;
                    char cmd_copy[MAX_INPUT_LENGTH];
                    strncpy(cmd_copy, pipeline[0], MAX_INPUT_LENGTH);
                    parse_command(cmd_copy, args, &input_file, &output_file, &append);
                    last_status = execute_single_command(args, input_file, output_file, append);
                } else {
                    last_status = execute_pipeline((char ***)pipeline, num_pipes);
                }
            }
        }
    }
    return 0;
}