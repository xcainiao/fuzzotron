/*
 * File:   monitor.c
 * Author: DoI
 *
 * Monitors a log file and looks for a specific string
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <pcre.h>
#include <string.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include "monitor.h"
#include "util.h"
#include "fuzzotron.h"

#define SIZE 1000
// Currently does not implement file re-open when the file being monitored is overwritten...

int monitor(char * file, char * regex){
    char * buff;
    pcre *re;
    size_t len = 0;
    int check;

    re = compile_regex(regex);

    FILE *fh = fopen(file, "r");
    if(fh == NULL){
        printf("[!] Error opening file %s: %s!\n", file, strerror(errno));
        printf("[!] Falling back to crash-detection only\n");
        return -1;
    }
    fseek(fh, 0, SEEK_END);

    for(;;){
        while(getline(&buff, &len, fh) > 0){
            check = parse_line(buff, re);
            if(check == 0){
                printf("[!] REGEX matched! Exiting.\n");
                regx = 1;
                goto end;
            }
           // printf("%s", buff);
            fflush(stdout);
        }
    }

end:
	fclose(fh);
    return 0;
}

int parse_line(char* line, pcre *regex){
    int ovector[30];
    int match = pcre_exec(
        regex,
        NULL,
        line,
        (int)strlen(line),
        0,
        0,
        ovector,
        30
    );

    if(match < 0){
        return -1;
    }

    return 0;
}

struct real_pcre * compile_regex(char* regex){
    pcre *re;
    int erroroffset;
    const char *error;

    re = pcre_compile(
        regex,
        0,
        &error,
        &erroroffset,
        NULL
    );

    if(re == NULL){
        // Compilation failed!
        fatal("[!] PCRE compile failed at offset %d error: %s\n", erroroffset, error);
    }
    return re;
}


int runpro()
{
    int res;
    pid_t pid;
    FILE* pipe = 0;
    int s;

    //char *argv[] = {"./bug", ">", "log.txt", "2>&1", 0};
    //char *argv[] = {"./bug", 0};
    char command[] = "./testcase/bug > log.txt 2>&1";
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
