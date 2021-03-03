/*
 * Copyright (C) 2002, Simon Nieuviarts
 */

#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

#define PIPE_IN 1
#define PIPE_OUT 0

int groupId;

void setPrompt() {
	printf("\r%s> ", getcwd(NULL, 0));
}

void handlerSigChild(int sig)
{
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG|WUNTRACED)) > 0) {
		printf("\n[%d] done\n", pid);
	}
	if (groupId == 0) {
		setPrompt();
		fflush(stdout);
	}
	
    return;
}

void handlerSigInt(int sig) {
	
    return;
}

int isCmdInterne(char** cmd) {
	return strcmp(cmd[0], "cd") == 0;
}

void execCmdInterne(char** cmd) {
	if (strcmp(cmd[0], "cd") == 0) {
		if (cmd[1] == NULL) {
			if (chdir(getenv("HOME")) < 0) {
				unix_error("cd");
			}
		} else {
			if (chdir(cmd[1]) < 0) {
				unix_error("cd");
			}
		}
	}
}

void redirectInput(char * in) {
	if (in != NULL) {
		int fIn = Open(in, O_RDONLY, 0);
		Dup2(fIn, STDIN_FILENO);
	}
}

void redirectOutput(char * out) {
	if (out != NULL) {
		int fOut = Open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IWUSR | S_IRUSR);
		Dup2(fOut, STDOUT_FILENO);
	}
}

pid_t execCmd(char **cmd, char* out, char* in, int cmdNumber, int last, int** fd) {
	if (isCmdInterne(cmd)) {
		execCmdInterne(cmd);
	} else {
		pid_t pid = Fork();
		if (pid == 0){ // Fils
			if (last && cmdNumber == 0){
				redirectInput(in);
				redirectOutput(out);	

			} else if (cmdNumber == 0) {
				Close(fd[cmdNumber][PIPE_OUT]);
				Close(fd[cmdNumber][PIPE_IN]);
				Dup2(fd[cmdNumber+1][PIPE_IN], STDOUT_FILENO);
				redirectInput(in);
			} else if (last) {
				Dup2(fd[cmdNumber][PIPE_OUT], STDIN_FILENO);
				Close(fd[cmdNumber][PIPE_IN]);
				redirectOutput(out);
			} else {
				Dup2(fd[cmdNumber][PIPE_OUT], STDIN_FILENO);
				Dup2(fd[cmdNumber+1][PIPE_IN], STDOUT_FILENO);
				Close(fd[cmdNumber][PIPE_IN]);
				Close(fd[cmdNumber+1][PIPE_OUT]);
			}

			int returnVal = execvp(cmd[0], cmd);
			if(returnVal < 0)
				unix_error(cmd[0]);
		} else {
			Close(fd[cmdNumber][PIPE_OUT]);
			Close(fd[cmdNumber][PIPE_IN]);
			return pid;
		}
	}
	return 0;
}

int getCommandCount(char*** seq) {
	int i;
	for (i=0; seq[i]!=0; i++) {}
	return i;
}

int isLast(char*** seq, int i) {
	return (getCommandCount(seq) == i+1);
}

int main() {
	Signal(SIGCHLD, handlerSigChild);

	while (1) {
		struct cmdline *l;
		int i;
		int** fd;

		setPrompt();
		l = readcmd();

		/* If input stream closed, normal termination */
		if (!l) {
			printf("exit\n");
			exit(0);
		}

		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		int cmdCount = getCommandCount(l->seq);
		
		fd = Malloc(sizeof(int *) * cmdCount);

		for(i = 0; i < cmdCount; i++) {
			fd[i] = Malloc(sizeof(int) *2);
			if (pipe(fd[i]) == -1){
				unix_error("Pipe error : ");
			}
		}

		groupId = 0;
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			
			if(strcmp(cmd[0], "quit") == 0) {
				printf("\n");
				exit(0);
			}
			
			
			pid_t pid = execCmd(cmd, l->out, l->in, i, isLast(l->seq, i), fd);

			if (!l->bg) {
				Setpgid(pid, groupId);
				groupId = getpgid(pid);
			}
				
		}
		if (groupId != 0){
			while (waitpid(-groupId, NULL, 0) > 0) {}
			groupId = 0;
		}
	}
}