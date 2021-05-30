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

struct job {
    char command[N_CHAR];
    int pid;
    int job_id;
    int status; //0=fg,1=bg,2=stopped
};

char prev_command[N_CHAR] = {'\0'};  

char shell_comment[] = "##";
const char delimiter[] = " ";
int fg_pid = 0;
int prev_exit_status = 0;
int saved_stdout;
int current_nb = 0;

pid_t ppid;
pid_t jobs_to_pid[N_JOBS];
pid_t jobs_order[2];
struct job pids_command[N_PID];

char *trimmer(char *command){
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
        char *parsed_command = trimmer(token);
        token = strtok(NULL, redir_char);
        char *filename = trimmer(token);

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

void swap_jobs_order(int nb) {
    jobs_order[1] = jobs_order[0];
    jobs_order[0] = nb;
}

char *get_job_status(int status) {
    switch (status){
        case 1:
            return "Running";
        case 2:
            return "Stopped";
        default:
            return "";
    }
}

int parse_amp_job(char* token) {
    if(token == NULL || token[0] != '%') {
        printf("Invalid arguments: %s\n", token);
        return 0;
    }
    const char t[2] = "%";
    char* tmp;
    tmp = strtok(token, t);
    return atoi(tmp);
}

char get_jobs_sign(pid_t job_id) {
    if(jobs_order[0] == job_id) 
        return '+';
    else if(jobs_order[1] == job_id) 
        return '-';
    else
        return ' ';
}

void process_fg(char* token) {
    int job_id = parse_amp_job(token);
    pid_t pid = jobs_to_pid[job_id];
    if(pid) {
        struct job* job_ = &pids_command[pid];
        job_->status = 0;
        printf("%s\n", job_->command);
        swap_jobs_order(job_id);
        kill(pid, SIGCONT);
        int status;
        setpgid(pid, pid);
        tcsetpgrp (0, pid);
        waitpid(pid, &status, 0);
        waitpid(pid, &status, 0);
        tcsetpgrp (0, ppid);
        prev_exit_status = WEXITSTATUS(status);
    } 
    else if (job_id)
        printf("fg: %%%d: no such job\n", job_id);
}

void process_bg(char *token) {
    int job_id = parse_amp_job(token);
    pid_t pid = jobs_to_pid[job_id];
    if(pid) {
        struct job* job_ = &pids_command[pid];
        char sign = get_jobs_sign(job_->job_id);
        printf("[%d]%c %s &\n",  job_->job_id, sign, job_->command);
        job_->status = 1;
        kill(pid, SIGCONT);
    }
    else if (job_id > 0){
        printf("bg: %%%d: no such job\n", job_id);
    }
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

void get_jobs() {
    for(int i = 1; i <= current_nb; i++) {
        pid_t pid = jobs_to_pid[i];
        if(pid != 0) {
            struct job* job_ = &pids_command[pid];
            if(job_ != NULL && job_->status > 0) {
                char sign = get_jobs_sign(i);
                printf("[%d]%c  %s      %s &\n", 
                i, sign, get_job_status(job_->status), job_->command);
            }
        }
    }
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
        else if(!strcmp(command, "jobs")) {
            get_jobs();
        }
        else if(!strcmp(token, "fg")) {
            token = strtok(NULL, delimiter);
            process_fg(token);
        } 
        else if(!strcmp(token, "bg")) {
            token = strtok(NULL, delimiter);
            process_bg(token);
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
            else if(!pid) {
                setpgid(0, 0);
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
                //if (execvp(prog_arv[0], prog_arv) == -1)
                //    printf("bad command \n");
                exit(errno);
            }
            else if (pid) {
                struct job curr_job;
                strcpy(curr_job.command , command);
                curr_job.pid = pid;
                curr_job.status = bgp;
                curr_job.job_id = bgp ? ++current_nb : 0;
                pids_command[pid] = curr_job;
                setpgid(pid, pid);

                if(!bgp) {
                    tcsetpgrp (0, pid);
                    waitpid(pid, &status, 0);
                    tcsetpgrp (0, ppid);
                    prev_exit_status = WEXITSTATUS(status);
                } 
                else {
                    jobs_to_pid[current_nb] = pid;
                    swap_jobs_order(current_nb);
                    printf("[%d] %d\n", current_nb, pid);
                }
            }
        }
        if(strcmp(prev_command, command)) 
            strcpy(prev_command, command);
    }
    free(tmp);
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

}

void stop_handler() {

}

void child_handler (int sig, siginfo_t *sip, void *notused){
    int status = 0;
    if (sip->si_pid == waitpid (sip->si_pid, &status, WNOHANG | WUNTRACED)){
        if (WIFEXITED(status)|| WTERMSIG(status)) {
            struct job* job_ = &pids_command[sip->si_pid];
            if(WIFEXITED(status) && job_ != NULL && job_->status == 1 && job_->job_id) {
                char sign = get_jobs_sign(job_->job_id);
                printf("\n[%d]%c  Done                    %s\n",  job_->job_id, sign, job_->command);
                fflush (stdout); 
                jobs_to_pid[job_->job_id] = 0;
                struct job null_job;
                null_job.job_id = 0;
                null_job.pid = 0;
                pids_command[sip->si_pid] = null_job;
            }
            else if (WIFSTOPPED(status) && job_ != NULL) {
                if(!(job_->job_id)) {
                    job_->job_id = ++current_nb;
                    jobs_to_pid[job_->job_id] = sip->si_pid;
                    swap_jobs_order(job_->job_id);
                } 
                job_->status = 2;
                char sign = get_jobs_sign(job_->job_id);
                printf("\n[%d]%c  Stopped                    %s\n",  job_->job_id, sign, job_->command);
                fflush (stdout); 
            }
        }
    } 
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

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    struct sigaction action;
    action.sa_sigaction = child_handler;

    sigfillset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction (SIGCHLD, &action, NULL);
}

int main(int argc, char *argv[]) {
    ppid = getpid();
    init_sas();
    saved_stdout = dup(STDOUT_FILENO);
    char command[N_CHAR];
    
    if(argc == 2) {
        read_file(argv[1]);
        return 0;
    }
    while (1) {
        memset (command, 0, N_CHAR);
        printf("icsh $ ");
        if(fgets(command, N_CHAR, stdin) != NULL){
            run_command(command, 0);
        }
    }
    return 0;
}