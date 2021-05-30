#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

# define N_CHAR 256
# define N_PID 4194304
# define N_JOBS 1000

char prev_command[N_CHAR] = {'\0'};  

char shell_comment[] = "##";
const char delimiter[] = " ";
int fg_pid = 0;
int prev_exit_status = 0;
int saved_stdout; 

char *trim_ws(char *command){
    char *tmp = (char *) malloc(strlen(command) + 1);
    strcpy(tmp, command);
    char *end;

    while (isspace((unsigned char)*tmp)) 
        tmp++;
    if(*tmp == 0)
        return tmp;
    end = tmp + strlen(tmp) - 1;
    while (end > tmp && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return tmp;
}
void redir_out(char *filename) {
    int fd = open(filename, O_TRUNC | O_CREAT | O_WRONLY, 0666);
    if ((fd <= 0)) {
        fprintf (stderr, "Couldn't open a file\n");
        exit (errno);
    }
    dup2(fd, STDOUT_FILENO); 
    dup2(fd, STDERR_FILENO);
    close(fd);
}

void redir_in(char *filename) {
    int in = open(filename, O_RDONLY);
    if ((in <= 0)) {
        fprintf (stderr, "Couldn't open a file\n");
        exit (errno);
    }
    dup2 (in, STDIN_FILENO);
    close(in);
}

char* check_io_redir(char command[]) {
    if(strchr(command, '>')  != NULL) 
        return ">";
    else if (strchr(command, '<') != NULL)
        return "<";
    return NULL;
}

char* process_redir(char command[]) {
    char* redir_char = check_io_redir(command);
    if(redir_char != NULL) {
        char *token;
        char *tmp = (char *) malloc(strlen(command) + 1);
        strcpy(tmp, command);

        token = strtok(tmp, redir_char);
        char *parsed_command = trim_ws(token);
        token = strtok(NULL, redir_char);
        char *filename = trim_ws(token);

        if(strchr(redir_char, '>')) 
            redir_out(filename);
        else if (strchr(redir_char, '<')) 
            redir_in(filename);
        
        free(tmp);
        return parsed_command;
    }
    free(redir_char);
    return command;
}

int is_bgp(char command[]){
    if(command && *command && command[strlen(command) - 1] == '&') {
        command[strlen(command) - 1] = '\0';
        char *end;
        end = command + strlen(command) - 1;
        while(end > command && isspace((unsigned char)*end))
            end--;
        end[1] = '\0';
        return 1;
    }
    return 0;
}

void process_command(char command[], int script_mode) {
    char *token;
    char *tmp = (char *) malloc(strlen(command) + 1);

    int bgp = is_bgp(command);
    strcpy(tmp, command);
    token = strtok(tmp, delimiter);

    if(token != NULL) {
        if(!strcmp(token, "echo")){
            if(!strcmp(command, "echo $?")) {
                printf("%d", prev_exit_status);
            }
            else {
                token = strtok(NULL, delimiter);
                while(token != NULL) {
                    printf("%s ", token);
                    token = strtok(NULL, delimiter);
                }
            }
            printf("\n");
            prev_exit_status = 1;
        } 
        else if (!strcmp(token, "exit")) {
            token = strtok(NULL, delimiter);
            long code = (int) strtol(token, NULL, 10);
            if(!script_mode) printf("Exiting program with code %ld\n", code);
            exit(code);
        } 
        else if (!strcmp(token, "!!")) { 
            if(prev_command[0] != '\0'){
                if(!script_mode) printf("%s\n", prev_command);
                return process_command(prev_command, script_mode);
            }  
        } 
        else {
            int status;
            int pid;
            int i = 0;
            char * prog_arv[N_CHAR];
            prog_arv[i] = token;
            while(token != NULL) {
                token = strtok(NULL, delimiter);
                prog_arv[++i] = token;
            }
            prog_arv[i+1] = NULL;

            if((pid=fork()) < 0) {
                perror("Fork failed");
            } 
            if(!pid) {
                command = process_redir(command);
                int i = 0;
                char * prog_arv[N_CHAR];
                token = strtok(command, delimiter);
                prog_arv[i] = token;
                while(token != NULL) {
                    token = strtok(NULL, delimiter);
                    prog_arv[++i] = token;
                }
                prog_arv[i+1] = NULL;
                execvp(prog_arv[0], prog_arv);
                if (execvp(prog_arv[0], prog_arv) == -1)
                    printf("bad command \n");
            }
            if (pid) {
                waitpid(pid,NULL,0);
                fg_pid = pid;
                waitpid(fg_pid, &status, 0);
                prev_exit_status = WEXITSTATUS(status);
                fg_pid = 0;
            }
        }
        if(strcmp(prev_command, command)) 
            strcpy(prev_command, command);
    }
    free(tmp);
    dup2(saved_stdout, 1);
}


int run_command(char command[], int script_mode) {
    command[strcspn(command, "\n")] = 0;
    if(command[0] != 0 && strncmp(command, shell_comment, strlen(shell_comment)))
        process_command(command, script_mode);
    return 1;
}

void read_file(char fileName[]) {
    FILE* file = fopen(fileName, "r");
    char line[256];
    while(fgets(line, sizeof(line), file)) {
        run_command(line, 1);
    }
}

void fg_handler() {
    if(fg_pid != 0)
        kill(fg_pid, SIGINT);
    else printf("\n");
}

void stop_handler() {
    if(fg_pid != 0) 
        kill(fg_pid, SIGTSTP);
    else printf("\n");

}

void init_sas() {
    struct sigaction sa, oldsa;
    struct sigaction sh, oldsh;

    sa.sa_handler = fg_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldsa);

    sh.sa_handler = stop_handler;
    sh.sa_flags = 0;
    sigemptyset(&sh.sa_mask);
    sigaction(SIGTSTP, &sh, &oldsh);
}

int main(int argc, char *argv[]) {
    ppid = getpid();
    init_sas();
    saved_stdout = dup(STDOUT_FILENO);

    char command[N_CHAR];
    char args[N_CHAR];
    
    if(argc == 2) {
        read_file(argv[1]);
        return 0;
    }
    while (1) {
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            run_command(command, 0);
        }
    }
    return 0;
}