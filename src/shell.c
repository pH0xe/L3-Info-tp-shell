/*
 * Copyright (C) 2002, Simon Nieuviarts
 */

#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

#define PIPE_IN 1
#define PIPE_OUT 0

// Variable vixant le nombre de commandes pouvant etre lancé en meme temps en background.
#define MAX_CMD 100

typedef struct t_process{
	int pid;
	char* cmdName;
} process;

// variable indiquant le pgid de la commande en premier plan.
int foregroundGroup;
process* bgProcess;

/**
 * fonction permettant l'affichage du repertoire courant en prompt du shell 
 */
void setPrompt() {
	printf("\r%s> ", getcwd(NULL, 0));
}

/**
 * Handler permettant d'attraper les fils une fois terminer pour eviter les zombies.
 * Si ce sont des fils en arriere plan affiche le pid et le nom de la commande qui c'est terminer puis affiche le prompt si aucun processus est en premier plan.
 */
void handlerSigChild(int sig){
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG|WUNTRACED)) > 0) {
		int i = 0;

		// on cherche le processus dans la structure pour avoir son nom.
		while (pid != bgProcess[i].pid){
			i++;
		}
		
		printf("\n[%d] done\t%s\n", pid, bgProcess[i].cmdName);

		// on libere l'espace occuper pour le nom et on remet pid a 0
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

/**
 * Handler du SIGINT.
 * tue tous les processus en premier plan a l'aide de leurs pgid contenue dans foregroundGroup puis affiche le prompt.
 * Si aucun processus n'est en premier plan, reviens a la ligne et affiche le prompt.
 */
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

/**
 * test si il sagit d'une commande interne au shell
 * Ici seul la commande cd (change directory) est prise en compte comme commande interne.
 * 
 * char* cmd : la commande a tester
 */
int isCmdInterne(char** cmd) {
	return strcmp(cmd[0], "cd") == 0;
}

/**
 * Execute la commande interne, ici cd 
 * 
 * char* cmd : la commande interne a executer
 */
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

/**
 * redireige l'entrée standard dans le fichier passée en parametre si il est different de NULL
 * 
 * char* in : le nom du fichier de redirection 
 */
void redirectInput(char * in) {
	if (in != NULL) {
		int fIn = Open(in, O_RDONLY, 0);
		Dup2(fIn, STDIN_FILENO);
	}
}

/**
 * redireige la sortie standard dans le fichier passée en parametre si il est different de NULL
 * 
 * char* out : le nom du fichier de redirection
 */
void redirectOutput(char * out) {
	if (out != NULL) {
		int fOut = Open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IWUSR | S_IRUSR);
		Dup2(fOut, STDOUT_FILENO);
	}
}
/**
 * Execution de la commande passé en parametre.
 * execution dans un execvp si ce n'est pas une commande interne sinon execute l'instruction interne.
 * Création d'un fils a l'aide de fork, puis ouverture et fermeture des pipes 
 * 
 * char ** cmd : la commande a lancer
 * char* out : le nom du fichier de redirection de sortie
 * char* in : le nom du fichier de redirection d'entrée
 * int cmdNumber : l'indice de la commande a lancer dans la séquence
 * int last : 1 ou 0, indique si il sagit de la derniere commande de la sequence.
 * int** fd : les pipes 
 * 
 * retourne le pid du fils.
 */
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

/**
 * Compte le nombre de commande séparé par des pipe
 * 
 * char *** seq : la sequence contenant les commandes a executer.
 */
int getCommandCount(char*** seq) {
	int i;
	for (i=0; seq[i]!=0; i++) {}
	return i;
}

/**
 * Renvoie vrai si il s'agit de la derniere commande de la sequence
 * 
 * seq char *** : la sequence contenant tout les commandes
 * int i : l'indice de la commande a verifier.
 */
int isLast(char*** seq, int i) {
	return (getCommandCount(seq) == i+1);
}

/**
 * Boucle permettant d'attendre les processus en premier plan.
 */
void attentePremierPlan() {
	if (foregroundGroup != 0){
		while (waitpid(-foregroundGroup, NULL, 0) > 0) {}
		foregroundGroup = 0;
	}
}

/**
 * ajoute dans l'array bgProcess le pid et le nom d'un processus s'executant en arriere plan.
 * 
 * int pid : le pid du processus en arriere plan
 * char* cmdName : le nom du processus en arriere plan
 */ 
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

/**
 * Alloue l'espace nécessaire pour stocké les nom et pid des processus en arriere plan.
 * Initialise tout les structure de bgProcess a pid = 0 et cmdName a NULL;
 */
void initBgProcess() {
	bgProcess = Malloc(MAX_CMD * sizeof(process));	

	for (size_t i = 0; i < MAX_CMD; i++) {
		bgProcess[i].pid = 0;
	}
}

/**
 * initialise tous les pipe nécessaire en fonction du nombre de commande.
 * 
 * int cmdCount : le nombre de commande dans la sequence
 * 
 * retourne l'adresse de la structure alloué
 */
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
	// Definition des handler pour attraper les signaux.
	Signal(SIGCHLD, handlerSigChild);
	Signal(SIGINT, handlerSigInt);

	// initialisation du tableau des processus en arrière plan
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

		// pour chaque sequence de commande on créé le nombre de pipe nécessaire.
		fd = initFd(getCommandCount(l->seq));

		foregroundGroup = 0;
		int group = 0;
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			
			// si la commande et "quit" on arrete le programme
			if(strcmp(cmd[0], "quit") == 0) {
				printf("\n");
				exit(0);
			}
			
			// on execute la commande et on recupere le pid de la commande lancé.
			pid_t pid = execCmd(cmd, l->out, l->in, i, isLast(l->seq, i), fd);

			// tout les processus d'une sequence on un pgid different du shell.
			// chaque commande de la séquence on le pgid de la premiere commande de la séquence.
			// le pgid de la premiere commande est mise a sont propre pid.
			Setpgid(pid, group);
			group = getpgid(pid);

			// si en premier plan on stock sa valeur dans foregroundGroup sinon on le stock dans bgProcess
			if (!l->bg) {
				foregroundGroup = group;
			} else {
				ajoutBg(pid, l->seq[i][0]);	
			}			
		}
		// on attend tout les processus en premier plan.
		attentePremierPlan();
	}
}