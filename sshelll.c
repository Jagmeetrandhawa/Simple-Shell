#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARGS_MAX 16
#define CMDLINE_MAX 512
#define PIPE_CMDS_MAX 4
#define TOKEN_LEN_MAX 32

int cd(char* arg)
{
        return chdir(arg);
}

char* pwd(char* buf, size_t size)
{
        return getcwd(buf, size);
}

void openFile(int* fd, char* fileName)
{
        /* close the previous opened file
        ex: echo hello world > a.txt > b.txt   --> close a.txt
        fd initially set to 0
        */
        if(*fd > 2)
        {
                close(*fd);
        }

        /* open new file (b.txt) */
        *fd = open(fileName, O_RDWR | O_CREAT | O_TRUNC, 0644);
}

int getPipelineCmds(char* process[], char* args[], int index, bool* redirectErr)
{
        if(args[index] == NULL)
                return index;
        if(strcmp(args[index], "|") == 0 || strcmp(args[index], "|&") == 0)
                index++;
        int processIndex = 0;
        while(args[index] != NULL && (strcmp(args[index], "|") != 0 && strcmp(args[index], "|&") != 0))
        {
                process[processIndex] = args[index];
                processIndex++;
                index++;
        }
        if(args[index] != NULL && strcmp(args[index], "|&") == 0)
                *redirectErr = true;
	process[processIndex] = NULL;
        return index;
}

void pipeline(int numPipes, char* args[], char* cmd, bool redirectOutput, int fileDes, bool redirectError)
{
        /* args = {cmd, arg, arg, |, cmd, arg, arg, |, cmd, arg, arg, |, cmd, arg, arg} */

        int status = 0, status2 = 0, status3 = 0, status4 = 0;
        bool redirectErr1 = false, redirectErr2 = false, redirectErr3 = false, redirectErr4 = false;

        int index = 0;
        char* process1[ARGS_MAX+1];
        index = getPipelineCmds(process1, args, index, &redirectErr1);
        char* process2[ARGS_MAX+1];
        index = getPipelineCmds(process2, args, index, &redirectErr2);
        char* process3[ARGS_MAX+1];
        index = getPipelineCmds(process3, args, index, &redirectErr3);
        char* process4[ARGS_MAX+1];
        index = getPipelineCmds(process4, args, index, &redirectErr4);

        int fd[2];
        pipe(fd);
        int fd2[2];
        pipe(fd2);
        int fd3[2];
        pipe(fd3);

	pid_t pid = fork();
        if(pid == 0)
        {
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                if(redirectErr1)
                        dup2(fd[1], STDERR_FILENO);
                close(fd[1]);

                close(fd2[0]);
                close(fd2[1]);
                close(fd3[0]);
                close(fd3[1]);

                execvp(process1[0], process1);
        }else if(pid == -1)
        {
                exit(1);
        }
 
	pid_t pid2 = fork();
        if(pid2 == 0)
        {
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
                close(fd[0]);

                if(numPipes > 1) //2 or 3 pipes
                {
                        close(fd2[0]);
                        dup2(fd2[1], STDOUT_FILENO);
                        if(redirectErr2)
                                dup2(fd2[1], STDERR_FILENO);
                        close(fd2[1]);
                }else if(redirectOutput) //if first if failed, this is the last command
                {
                        dup2(fileDes, STDOUT_FILENO);
                        if(redirectError)
                                dup2(fileDes, STDERR_FILENO);

                        close(fd2[0]);
                        close(fd2[1]);
                }else
                {
                        close(fd2[0]);
                        close(fd2[1]);
                }

                close(fd3[0]);
                close(fd3[1]);

                execvp(process2[0], process2);
        }else if(pid2 == -1)
        {
                exit(1);
        }

        /* 2 or 3 pipes */
        pid_t pid3;
        if(numPipes > 1)
        {
                pid3 = fork();
                if(pid3 == 0)
                {
                        close(fd2[1]);
                        dup2(fd2[0], STDIN_FILENO);
                        close(fd2[0]);

                        if(numPipes > 2) //3 pipes
                        {
                                close(fd3[0]);
                                dup2(fd3[1], STDOUT_FILENO);
                                if(redirectErr3)
                                        dup2(fd3[1], STDERR_FILENO);
                                close(fd3[1]);
                        }else if(redirectOutput) //if first if failed, this is the last command
                        {
                                dup2(fileDes, STDOUT_FILENO);
                                if(redirectError)
                                        dup2(fileDes, STDERR_FILENO);

                                close(fd3[0]);
                                close(fd3[1]);
                        }else
                        {
                                close(fd3[0]);
                                close(fd3[1]);
                        }

                        close(fd[0]);
                        close(fd[1]);

                        execvp(process3[0], process3);
                }else if(pid3 == -1)
                {
                        exit(1);
                }
        }
        
        /* 3 pipes */
        pid_t pid4;
        if(numPipes > 2)
        {
                pid4 = fork();
                if(pid4 == 0)
                {
                        close(fd3[1]);
                        dup2(fd3[0], STDIN_FILENO);
                        close(fd3[0]);

                        if(redirectOutput)
                        {
                                dup2(fileDes, STDOUT_FILENO);
                        }

                        if(redirectError)
                        {
                                dup2(fileDes, STDERR_FILENO);
                        }

                        close(fd[0]);
                        close(fd[1]);
                        close(fd2[0]);
                        close(fd2[1]);

                        execvp(process4[0], process4);
                }else if(pid4 == -1)
                {
                        exit(1);
                }
        }

	close(fd[0]);
	close(fd[1]);
        close(fd2[0]);
	close(fd2[1]);
        close(fd3[0]);
        close(fd3[1]);
	
        waitpid(pid, &status, 0);
        WIFEXITED(status);

        waitpid(pid2, &status2, 0);
        WIFEXITED(status2);

        /* 2 or 3 pipes */
        if(numPipes > 1)
        {
                waitpid(pid3, &status3, 0);
                WIFEXITED(status3);
        }

        /* 3 pipes */
        if(numPipes > 2)
        {
                waitpid(pid4, &status4, 0);
                WIFEXITED(status4);
        }

        fprintf(stderr, "+ completed '%s' ", cmd);
        int exitStatuses[] = {status, status2, status3, status4};
        for(int i = 0; i <= numPipes; i++)
        {
                fprintf(stderr, "[%d]", exitStatuses[i]);
        }
        fprintf(stderr, "\n");
}

void storePipe(int i, char* tok, char* args[], int* index)
{
    if((unsigned)i < strlen(tok)-1 && tok[i] == '|' && tok[i+1] == '&')
    {
        strcpy(args[*index], "|&");
        (*index)++;
    }else if(tok[i] == '|')
    {
        strcpy(args[*index], "|");
        (*index)++;
    }
}

void storeRedirect(int i, char* tok, char* args[], int* index)
{
    if((unsigned)i < strlen(tok)-1 && tok[i] == '>' && tok[i+1] == '&')
    {
        strcpy(args[*index], ">&");
        (*index)++;
    }else if(tok[i] == '>')
    {
        strcpy(args[*index], ">");
        (*index)++;
    }
}

void parseCmdLine(char* tok, char* args[], int* index)
{
    char argument[TOKEN_LEN_MAX];
    memset(argument, '\0', sizeof(argument));
    int start = 0;

    if(strlen(tok) > 1 && tok[0] == '|' && tok[1] == '&')
    {
        strcpy(args[*index], "|&");
        (*index)++;
        start = 2;
    }else if(strlen(tok) > 0 && tok[0] == '|')
    {
        strcpy(args[*index], "|");
        (*index)++;
        start = 1;
    }

    for(unsigned int i = start; i < strlen(tok); i++)
    {
        if(*index <= ARGS_MAX && strlen(argument) == 0)
        {
                storeRedirect(i, tok, args, index);
                storePipe(i, tok, args, index);
        }
        
        if(*index <= ARGS_MAX && strlen(argument) != 0 && (tok[i] == '>' || tok[i] == '|' || tok[i] == ' '))
        {
            strcpy(args[*index], argument);
            (*index)++;
            memset(argument, '\0', sizeof(argument));

            storeRedirect(i, tok, args, index);
            storePipe(i, tok, args, index);
            
        }else if(tok[i] != '|' && tok[i] != '&' && tok[i] != '>' && tok[i] != ' ')
        {
            char character = tok[i];
            strncat(argument, &character, 1);
        }
    }
    
        if(*index <= ARGS_MAX && strlen(tok) > 0 && argument[0] != '\0')
        {
            strcpy(args[*index], argument);
            (*index)++;
        }

        if(*index <= ARGS_MAX)
        {
                free(args[*index]);
                args[*index] = NULL;
                (*index)++;
                for(int i = *index; i < ARGS_MAX+1; i++)
                {
                        free(args[i]);
                        args[i] = NULL;
                }
        }
}

int analyzeArgs(int* index, char* args[], int* numPipes, bool* redirectOutput, bool* redirectError, int* fd, char fileName[])
{
    for(int i = 0; i < *index; i++)
    {
        if(args[i] == NULL)
            break;
        if(!strcmp(args[i], "|") || !strcmp(args[i], "|&"))
            (*numPipes)++;
        if(!strcmp(args[i], ">") || !strcmp(args[i], ">&") || !strcmp(args[i], "|") || !strcmp(args[i], "|&"))
        {
            if(args[i+1] != NULL && strcspn(args[i+1], ">|") < strlen(args[i+1]))
            {
                //error: >> >&> |> |&> >| >&| || |&|
                return 1;
            }
        }
        
        if(!strcmp(args[i], ">") || !strcmp(args[i], ">&"))
        {
            *redirectOutput = true;
            if(!strcmp(args[i], ">&"))
                *redirectError = true;
            strcpy(fileName, args[i+1]);
            openFile(fd, fileName);
            int copyi = i;
            for(int j = i+2; j < ARGS_MAX+1; j++)
            {
                if(j == (*index) - 1)
                {
                        free(args[copyi]);
                        args[copyi] = NULL;
                        break;
                }else
                {
                        args[copyi] = args[j];
                }
                copyi++;
            }
            i--;
            (*index) = (*index) - 2;
            free(args[*index]);
            args[*index] = NULL;
        }
    }
    return 0;
}

void freeArgs(char* args[])
{
        for(int i = 0; i < ARGS_MAX+1; i++)
        {
                if(args[i] != NULL)
                {
                        free(args[i]);
                        args[i] = NULL;
                } 
        }
}

int sls(char* buf, size_t size)
{
        DIR *dirp;
        struct dirent *dp;
        struct stat sts;

        getcwd(buf, size);

        dirp = opendir(".");
        if(dirp == NULL)
                return errno;
        while((dp = readdir(dirp)) != NULL)
        {
                char firstChar = dp->d_name[0];
                if(firstChar == '.')
                        continue;
                stat(dp->d_name, &sts);
                printf("%s (%lld bytes)\n", dp->d_name, (long long)sts.st_size);
        }
        closedir(dirp);
        return 0;
}

int main(void)
{
        char cmd[CMDLINE_MAX];
        char* args[ARGS_MAX+1];
        char fileName[TOKEN_LEN_MAX];
        
        while (1) {
                char *nl;
                bool redirectOutput = false, redirectError = false;
                int fd = 0;
                int numPipes = 0;
                int index = 0;
                memset(fileName, '\0', TOKEN_LEN_MAX);
                for(int i = 0; i < ARGS_MAX+1; i++)
                {
                        args[i] = malloc(TOKEN_LEN_MAX * sizeof(char*));
                }

                /* Print prompt */
                printf("sshell@ucd$ ");
                fflush(stdout);

                /* Get command line */
                fgets(cmd, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmd);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmd, '\n');
                if (nl)
                        *nl = '\0';

                /* Builtin command */
                if (!strcmp(cmd, "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, 0);
                        break;
                }

                parseCmdLine(cmd, args, &index);

                /* Error check*/
                if(args[index-1] != NULL)
                {
                        fprintf(stderr, "Error: too many process arguments\n");
                        freeArgs(args);
                        continue;
                }

                if( !strcmp(args[0], ">") || !strcmp(args[0], ">&") || !strcmp(args[0], "|") || !strcmp(args[0], "|&")
                        || !strcmp(args[index-2], "|") || !strcmp(args[index-2], "|&") )
                {
                        fprintf(stderr, "Error: missing command\n");
                        freeArgs(args);
                        continue;
                }

                if(!strcmp(args[index-2], ">") || !strcmp(args[index-2], ">&"))
                {
                        fprintf(stderr, "Error: no output file\n");
                        freeArgs(args);
                        continue;
                }

                int count = 0;
                for(int i = 0; i < ARGS_MAX+1; i++)
                {
                        if(args[i] != NULL && (!strcmp(args[i], "|") || !strcmp(args[i], "|&")))
                                count++;
                }
                if(count > 0)
                {
                        /*
                        0 | 1 | 2 | 3
                        # pipes			error if
                        0                       n/a
                        1			i=0
                        2			i=0, i=1
                        3			i=0, i=1, i=2
                        */
                        int argIndex = 0;
                        bool error = false;
                        for(int i = 0; i < count; i++)
                        {
                                char* process[ARGS_MAX+1];
                                bool temp = false;
                                argIndex = getPipelineCmds(process, args, argIndex, &temp);

                                for(int j = 0; process[j] != NULL; j++)
                                {
                                        if(!strcmp(process[j], ">") || !strcmp(process[j], ">&"))
                                        {
                                                error = true;
                                                fprintf(stderr, "Error: mislocated output redirection\n");
                                                break;
                                        }
                                }
                                if(error)
                                        break;
                        }
                        if(error)
                        {
                                freeArgs(args);
                                continue;
                        }
                }

                /* Analyze arguments */
                int problem = analyzeArgs(&index, args, &numPipes, &redirectOutput, &redirectError, &fd, fileName);
                if(problem)
                {
                        fprintf(stderr, "Error: missing command\n");
                        freeArgs(args);
                        continue;
                }

                /* Error check */
                if(fd < 0)
                {
                        fprintf(stderr, "Error: cannot open output file\n");
                        freeArgs(args);
                        continue;
                }

                /* Pipeline */
                if(numPipes > 0)
                {
                        /* do piping */
                        pipeline(numPipes, args, cmd, redirectOutput, fd, redirectError);
                        freeArgs(args);
                        continue;
                }

                /* Regular command */
                pid_t pid;
                int status = 0;
                
                if(strcmp(args[0], "cd") == 0)
                {
                        status = cd(args[1]);
                        status = status * -1;
                        if(status)
                                fprintf(stderr, "Error: cannot cd into directory\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                        freeArgs(args);
                        continue;
                }else if(strcmp(args[0], "pwd") == 0)
                {
                        char currentDir[CMDLINE_MAX];
                        char* retval = pwd(currentDir, sizeof(currentDir));
                        if(retval == NULL)
                                status = errno;
                        printf("%s\n", currentDir);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                        freeArgs(args);
                        continue;
                }else if(strcmp(args[0], "sls") == 0)
                {
                        char currentDir[TOKEN_LEN_MAX];
                        status = sls(currentDir, sizeof(currentDir));
                        if(status)
                                fprintf(stderr, "Error: cannot open directory\n");
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                        freeArgs(args);
                        continue;
                }
                
                pid = fork();
                
                if(pid == 0)
                {
                        /* child */
                        if(redirectOutput)
                        {
                                dup2(fd, STDOUT_FILENO);
                        }
                        if(redirectError)
                        {
                                dup2(fd, STDERR_FILENO);
                        }
                        
                        execvp(args[0], args);
                        
                        fprintf(stderr, "Error: command not found\n");
                        exit(1);
                }else if (pid > 0) 
                {
                        /* Parent */
                        waitpid(pid, &status, 0);
                        WIFEXITED(status);
                }else
                {
                        perror("fork");
                        exit(1);
                }
                
                fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                
                if(fd > 2 && (redirectOutput || redirectError))
                        close(fd);
                freeArgs(args);
        }
        freeArgs(args);
        return EXIT_SUCCESS;
}
