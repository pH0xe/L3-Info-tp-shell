/*
 * Copyright (C) 2002, Simon Nieuviarts
 */

#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

#define PIPE_IN 1
#define PIPE_OUT 0
#define MAX_CMD 100

typedef struct t_process{
	int pid;
	char* cmdName;
} process;


int foregroundGroup;
process* bgProcess;

void setPrompt() {
	printf("\r%s> ", getcwd(NULL, 0));
}

void handlerSigChild(int sig){
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG|WUNTRACED)) > 0) {
		int i = 0;
		while (pid != bgProcess[i].pid){
			i++;
		}
		
		printf("\n[%d] done\t%s\n", pid, bgProcess[i].cmdName);
		bgProcess[i].pid = 0;
		free(bgProcess[i].cmdName);
		bgProcess[i].cmdName = NULL;	
	}
	if (foregroundGroup == 0) {
		setPrompt();
		fflush(stdout);
	}
	
    return;
}

void handlerSigInt(int sig) {
	if (foregroundGroup != 0) {
		Kill(-foregroundGroup, SIGINT);
		foregroundGroup = 0;
	} else {
		printf("\n");
	}
	setPrompt();
	fflush(stdout);
    
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
			foregroundGroup = 0;
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

void attentePremierPlan() {
	if (foregroundGroup != 0){
		while (waitpid(-foregroundGroup, NULL, 0) > 0) {}
		foregroundGroup = 0;
	}
}

void ajoutBg(int pid, char* cmdName) {
	int i = 0;
	
	while (i < MAX_CMD && bgProcess[i].pid != 0) {
		i++;
	}

	if (i < MAX_CMD) {
		bgProcess[i].pid = pid;
		bgProcess[i].cmdName = Malloc(strlen(cmdName) +1);
		strcpy(bgProcess[i].cmdName, cmdName);
	} else {
		app_error("Background error : nombre de commande en arrière plan trop important");
	}	
}

void initBgProcess() {
	bgProcess = Malloc(MAX_CMD * sizeof(process));	

	for (size_t i = 0; i < MAX_CMD; i++) {
		bgProcess[i].pid = 0;
	}
}

int** initFd(int cmdCount) {
	int** fd = Malloc(sizeof(int *) * cmdCount);

	int i;
	for(i = 0; i < cmdCount; i++) {
		fd[i] = Malloc(sizeof(int) *2);
		if (pipe(fd[i]) == -1){
			unix_error("Pipe error : ");
		}
	}
	return fd;
}


int main() {
	Signal(SIGCHLD, handlerSigChild);
	Signal(SIGINT, handlerSigInt);

	initBgProcess();

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


		fd = initFd(getCommandCount(l->seq));

		foregroundGroup = 0;
		int group = 0;
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			
			if(strcmp(cmd[0], "quit") == 0) {
				printf("\n");
				exit(0);
			}
			
			
			pid_t pid = execCmd(cmd, l->out, l->in, i, isLast(l->seq, i), fd);

			Setpgid(pid, group);
			group = getpgid(pid);

			if (!l->bg) {
				foregroundGroup = group;
			} else {
				ajoutBg(pid, l->seq[i][0]);	
			}
			// printf("pid : %d, pgid : %d, shell pgid : %d, shell pid : %d\n", pid, getpgid(pid), getpgrp(), getpid());
				
		}
		attentePremierPlan();
	}
}