#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#define SIZE 1000

int get_percent_used ();
int runpro();
int main(){
    int res;
    res = runpro();
    printf("%d\n", res);
}

int runpro()
{
    int res;
    pid_t pid;
    FILE* pipe = 0;
    int s;

    //char *argv[] = {"./bug", ">", "log.txt", "2>&1", 0};
    //char *argv[] = {"./bug", 0};
    char command[] = "./bug > log.txt 2>&1";
    res = get_percent_used();
    if(res!=0){
        kill(res, SIGKILL);
    }

    if((pid = fork()) == 0){
        pipe = popen(command, "r");
        pclose(pipe);
        exit(0);
    }
    else if(pid < 0){
        printf("[!] generator_radamsa fork() failed:");
        return 0;
    }
    else{
        res = get_percent_used();
        return res;
        //waitpid(pid, &s, 0x00);
    }
    return 0;

}

int get_percent_used ()
{
    char buffer[SIZE];
    char command[SIZE];
    FILE* pipe = 0;
    int pid = 0;

    sprintf (command, "./getpid.sh");

    if (!(pipe = popen(command, "r"))) {
        perror("open failed");
        return 1;
    }

    while (fgets(buffer, SIZE, pipe) != NULL) {
        if(buffer){
            pid = (int) strtol(buffer, NULL, 10);
        }
    }
    pclose(pipe);

    return pid;
}
