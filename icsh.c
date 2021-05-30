#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

# define N_CHAR 256

char prev_command[N_CHAR] = {'\0'};  

char echo_trigger[] = "echo";
char exit_trigger[] = "exit";
char prev_trigger[] = "!!";
const char delimiter[] = " ";

int process_command(char command[]) {
    int run = 1;
    char *token;
    char *tmp = (char *) malloc(strlen(command) + 1);

    strcpy(tmp, command);
    token = strtok(tmp, delimiter);

    if(token != NULL) {
        if(!strcmp(token, echo_trigger)){
            token = strtok(NULL, delimiter);
            while(token != NULL) {
                printf("%s ", token);
                token = strtok(NULL, delimiter);
            }
            printf("\n");
        } 
        else if (!strcmp(token, exit_trigger)) {
            token = strtok(NULL, delimiter);
            long code = (int) strtol(token, NULL, 10);
            printf("Exiting program with code %ld\n", code);
            exit(code);
        } 
        else if (!strcmp(token, prev_trigger)) { 
            if(prev_command[0] != '\0'){
                printf("%s\n", prev_command);
                return process_command(prev_command);
            }  
        } 
        else {
            printf("bad command\n");
        }
        if(strcmp(prev_command, command)) strcpy(prev_command, command);
    }

    return run;
}


int run_command(char command[]) {
    command[strcspn(command, "\n")] = 0;
    if(command[0] != 0) {
        return process_command(command);
    }
    return 1;
}

int main(int argc, char *argv[]) {
    char command[N_CHAR];

    while (1) {
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            run_command(command);
        }
    }
    return 0;
}
