/*
 * File:   fuzzotron.c
 * Author: DoI
 *
 * Fuzzotron is a simple network socket fuzzer. Connect to a tcp or udp port
 * and fire some testcases generated with either blab or radamsa.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <linux/limits.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#include "monitor.h"
#include "fuzzotron.h"
#include "sender.h"
#include "generator.h"
#include "trace.h"
#include "util.h"

// Struct to hold arguments passed to the monitor thread
struct monitor_args mon_args;

int stop = 0;   // the global 'stop fuzzing' variable. When set to 1, all threads will spool
int regx = 0;   // the global 'stop fuzzing' variable. When set to 1, all threads will spool
                // their cases to disk and exit.
pthread_mutex_t runlock;
int check_pid = 0; // server pid to check for crash.
struct fuzzer_args fuzz; // Arguments for the fuzzer threads
char * output_dir = NULL, * server_command = NULL; // directory for potential crashes

static unsigned long cases_sent = 0;
static unsigned long cases_jettisoned = 0;
static unsigned long paths = 0;

int main(int argc, char** argv) {

    memset(&fuzz, 0x00, sizeof(fuzz));
    // parse arguments
    int c, threads = 1;
    static int use_blab = 0, use_radamsa = 0, use_self = 0;;
    char * logfile = NULL, * regex = NULL;
    fuzz.protocol = 0; fuzz.is_tls = 0; fuzz.destroy = 0;

    static struct option arg_options[] = {
        {"alpn", required_argument, 0, 'l'},
        {"blab", no_argument, &use_blab, 1},
        {"radamsa", no_argument, &use_radamsa, 1},
        {"self", no_argument, &use_self, 1},
        {"ssl", no_argument, &fuzz.is_tls, 1},
        {"grammar",  required_argument, 0, 'g'},
        {"output",  required_argument, 0, 'o'},
        {"directory",  required_argument, 0, 'd'},
        {"protocol",  required_argument, 0, 'p'},
        {"destroy", no_argument, &fuzz.destroy, 1},
        {"checkscript", required_argument, 0, 'z'},
        {"trace", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    int arg_index;
    while((c = getopt_long(argc, argv, "d:c:h:p:g:t:m:c:P:r:w:s:z:o:C:", arg_options, &arg_index)) != -1){
        switch(c){
            case 'c':
                // Define PID to check for crash
                check_pid = atoi(optarg);
                printf("[+] Monitoring PID %d\n", check_pid);
                break;

            case 'd':
                // define test case directory for blab
                fuzz.in_dir = optarg;
                if(directory_exists(fuzz.in_dir) < 0){
                    fatal("Could not open %s\n", fuzz.in_dir);
                }
                break;

            case 'g':
                // define grammar
                fuzz.grammar = optarg;
                break;

            case 'h':
                // define host
                fuzz.host = optarg;
                break;

            case 'l':
                // set ALPN string
                fuzz.alpn = optarg;
                break;

            case 'm':
                // Log file to monitor
                logfile = optarg;
                break;

            case 'o':
                // Output dir for crashes
                output_dir = optarg;

                break;

            case 'p':
                // define port
                fuzz.port = atoi(optarg);
                break;

            case 'P':
                // define protocol
                if((strcmp(optarg,"udp") != 0) && (strcmp(optarg,"tcp") != 0) && (strcmp(optarg,"unix") != 0)){
                        fatal("Please specify either 'tcp', 'udp' or 'unix' for -P\n");
                }
                if(strcmp(optarg,"tcp") == 0){
                            fuzz.protocol = 1;
                            fuzz.send = send_tcp;
                }
                else if(strcmp(optarg,"udp") == 0){
                            fuzz.protocol = 2;
                            fuzz.send = send_udp;
                }
                else if(strcmp(optarg,"unix") == 0){
                    fuzz.protocol = 3;
                    fuzz.send = send_unix;
                }

                break;

            case 'r':
                // define regex for monitoring
                regex = optarg;
                break;

            case 't':
                // define threads
                threads = atoi(optarg);
                break;

            case 's':
                fuzz.shm_id = atoi(optarg);
                break;

            case 'z':
                fuzz.check_script = optarg;
                break;

           }
    }

    // check argument sanity
    if((fuzz.host == NULL) || (fuzz.port == 0 && fuzz.protocol != 3) ||
            (use_blab == 1 && use_radamsa == 1 && use_self == 1) ||
            (use_blab == 1 && use_radamsa == 1 && use_self == 0) ||
            (use_blab == 0 && use_radamsa == 1 && use_self == 1) ||
            (use_blab == 1 && use_radamsa == 0 && use_self == 1) ||
            (use_blab == 0 && use_radamsa == 0 && use_self == 0) ||
            (use_blab == 1 && fuzz.in_dir && !fuzz.shm_id) ||
            (use_radamsa == 1 && fuzz.grammar != NULL) ||
            (fuzz.protocol == 0) || (output_dir == NULL) ){
        help();
        return -1;
    }

    // if we're using blab, ensure we have a grammar defined
    if(check_pid == 0){
        fatal("your server is not running\n");
    }

    // if we're using blab, ensure we have a grammar defined
    if(use_blab == 1 && fuzz.grammar == NULL){
        fatal("If using blab, -g or --grammar must be specified\n");
    }

    // if we're using radamsa, ensure the directory with the example cases is defined
    if(use_radamsa == 1 && fuzz.in_dir == NULL){
        fatal("If using radamsa, -d or --directory must be specified\n");
    }

    if(fuzz.shm_id && threads > 1){
        fatal("Tracing only supported single threaded");
    }
    if(fuzz.shm_id && fuzz.gen == BLAB && fuzz.in_dir == NULL){
        fatal("Blab and tracing requires --directory");
    }
    if(fuzz.shm_id && use_blab == 1 && fuzz.in_dir){
        // Note: hmmm, maybe using blab and instrumentation is a good idea... Save testcases that blab generates
        // that hit new paths and use this to seed a mutation-based fuzzer?
        puts(GRN "[+] Experimental discovery mode enabled\n" RESET);
    }

    // Fuzzotron and SSL is not thread safe
    if(threads > 1 && fuzz.is_tls){
        fatal("Sorry, fuzzotron does not support multithreading and SSL yet\n");
    }

    if(logfile != NULL){
        if(regex == NULL){
            printf("[!] No regex specified, falling back to Crash-Detect mode\n");
        }
        else {
            printf("[+] Monitoring logfile %s\n", logfile);
            // Spawn the monitor
            int rc;
            pthread_t monitor;

            mon_args.file = logfile;
            mon_args.regex = regex;

            printf("[+] Spawning monitor\n");
            rc = pthread_create(&monitor, NULL, call_monitor, NULL);
            if(rc){
                fatal("Creating pthread failed: %s\n", strerror(errno));
            }
            printf("[+] Monitor Spawned!\n");
        }
    }

    if(fuzz.check_script){
        if(!file_exists(fuzz.check_script)){
            printf("[!] File %s not found\n", fuzz.check_script);
            help();
            return -1;
        }

        printf("[+] Using check script: %s\n", fuzz.check_script);
    }

    fuzz.tmp_dir = CASE_DIR;
    if(directory_exists(fuzz.tmp_dir) < 0){
        if(mkdir(fuzz.tmp_dir, 0755)<0){
            fatal("[!] Could not mkdir %s: %s\n", fuzz.tmp_dir, strerror(errno));
        }
    }

    if(directory_exists(output_dir) < 0){
        if(mkdir(output_dir, 0755)<0){
            fatal("[!] Could not mkdir %s: %s\n", output_dir, strerror(errno));
        }
    }

    if(use_blab == 1){
        fuzz.gen = BLAB;
    }
    else if(use_radamsa){
        fuzz.gen = RADAMSA;
    }
    else if(use_self){
        fuzz.gen = SELF;
    }

    if (pthread_mutex_init(&runlock, NULL) != 0){
        fatal("[!] pthread_mutex_init failed");
    }

    signal(SIGPIPE, SIG_IGN);
    pthread_t workers[threads];
    int i;
    struct worker_args targs[threads];
    for(i = 1; i <= threads; i++){

        targs[i-1].thread_id = i;
        targs[i-1].threads = threads;

        printf("[+] Spawning worker thread %d\n", i);
        if(pthread_create(&workers[i-1], NULL, worker, &targs[i-1]) > 0)
            fatal("Creating pthread failed: %s\n", strerror(errno));

        usleep(2000);
    }

    char spinner[4] = "|/-\\";
    struct spint { unsigned i:2; } s;
    s.i=0;
    while(1){
        usleep(50000);
        if(stop == 1){
            printf("\n");
            break;
        }

        printf("[%c] Sent cases: %lu", spinner[s.i],  cases_sent);
        if(fuzz.shm_id)
            printf(" Paths:%lu Jettisoned: %lu\r", paths, cases_jettisoned);
        else
            printf("\r");

        fflush(stdout);
        s.i++;
    }

    for(i = 1; i <= threads; i++){
        pthread_join(workers[i-1], NULL);
    }

    pthread_mutex_destroy(&runlock);
    printf("[.] Done. Total testcases issued: %lu\n", cases_sent);
    //if(check_pid)
    //    kill(check_pid, SIGKILL);
	return 1;   
}

void * call_monitor(){
    monitor(mon_args.file, mon_args.regex);
    return NULL;
}

// worker thread, generate cases and sends them
void * worker(void * worker_args){
    struct worker_args *thread_info = (struct worker_args *)worker_args;
    printf("[.] Worker %u alive\n", thread_info->thread_id);

    int deterministic = 1;

    char prefix[25];
    sprintf(prefix,"%d",(int)syscall(SYS_gettid));

    struct testcase * cases = 0x00;
    struct testcase * entry = 0x00;

    uint32_t exec_hash;
    int r;

    if(fuzz.gen == RADAMSA || fuzz.gen == SELF){
        if(deterministic == 1 && thread_info->thread_id == 1){
            
            struct testcase * orig_cases = load_testcases(fuzz.in_dir, "");
            struct testcase * orig_entry = orig_cases;

            if(send_cases(orig_entry) < 0){
                goto cleanup;
            }

            deterministic = 0;
        }
        if(fuzz.gen == SELF)
            cases = generator_other(CASE_COUNT, fuzz.in_dir, fuzz.tmp_dir, prefix);
        if(fuzz.gen == RADAMSA)
            cases = generator_radamsa(CASE_COUNT, fuzz.in_dir, fuzz.tmp_dir, prefix);
        if(send_cases(cases) < 0){
            goto cleanup;
        }
	}

cleanup:
    stop = 1;
    printf("[!] Thread %d exiting\n", thread_info->thread_id);
    return NULL;
}


// Send all cases in a struct. return -1 if any failure, otherwise 0. Frees the supplied cases struct
// and updates global counters.
int send_cases(void * cases){
    int ret = 0, r = 0;
    struct testcase * entry = cases;
    uint32_t exec_hash;
    

    while(entry){
        if(entry->len == 0){
            entry = entry->next;
            continue;
        }
        else {
            ret = fuzz.send(fuzz.host, fuzz.port, entry->data, entry->len);
        }
        if(check_stop(entry, ret)<0){
            free_testcases(cases);
            return -1;
        }

        entry = entry->next;
        cases_sent++;
    }

    free_testcases(cases);
    return 0;
}

// checks the return code from send_cases et-al and sets the global stop variable if
// its time to stop fuzzing and saves the cases.
int check_stop(void * cases, int result){
    int ret = 0;

    usleep(50000);

    printf("now server pid: %d\n", check_pid);
    if(regx == 1){
        // We have experienced a crash. set the global stop var
        pthread_mutex_lock(&runlock);
        stop = 1;
        save_testcases(cases, output_dir);
        pthread_mutex_unlock(&runlock);
        return -1;
    }
    usleep(500000);

    // If process id is supplied, check it exists and set stop if it doesn't
    check_pid = runpro(server_command);
    if(check_pid<=0){
        printf("server is not running *****************************\n");
        return -1;
    }

    return 0;
}

int pid_exists(int pid){
    struct stat s;
    char path[PATH_MAX];

    sprintf(path, "/proc/%d", pid);
    if(stat(path, &s) == -1){
        // PID not found
        printf("[!!] PID %d not found. Check for server crash\n", pid);
        return -1;
    }

    // PID found
    return 0;
}

int runpro(char *command1)
{
    int res;
    pid_t pid;
    FILE* pipe = 0;
    int s;

    //char *argv[] = {"./bug", ">", "log.txt", "2>&1", 0};
    //char *argv[] = {"./bug", 0};
    char command[] = "{ /home/fuzz/github/cppzmq/demo/server; } > /tmp/log.txt 2>&1";
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
        usleep(50000);
        res = get_percent_used();
        return res;
        //waitpid(pid, &s, 0x00);
    }
    return 0;
}

int run_check(char * script){

    if(access(script, X_OK) < 0){
        fatal("[!] Error accessing check script %s: %s\n", script, strerror(errno));
    }

    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    char ret[2];
    memset(ret, 0x00, 2);

    if(pipe(out_pipe) < 0 || pipe(err_pipe) < 0){
        fatal("[!] Error with pipe: %s\n", strerror(errno));
    }
    if((pid = fork()) == 0){
            dup2(err_pipe[1], 2);
            dup2(out_pipe[1], 1);
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(err_pipe[0]);
            close(err_pipe[1]);

            char *args[] = {script, 0};
            execv(args[0], args);

            exit(0);
    }

    else if(pid < 0){
            fatal("[!] FORK FAILED!\n");
    }
    else{
        close(err_pipe[1]);
        close(out_pipe[1]);
        waitpid(pid, NULL, 0);
        if(read(out_pipe[0], ret, 1) < 0){
            fatal("read() failed");   
        };
        close(err_pipe[0]);
        close(out_pipe[0]);
        return atoi(&ret[0]);
    }

    return -1;
}

int file_exists(char * file){
    struct stat s;
    return(stat(file, &s) == 0);
}

/*
*  Check if a directory exists, returns 0 on success or < 0 on failure.
*/
int directory_exists(char * dir){
    DIR * d = opendir(dir);
    if(d != NULL){
        closedir(d);
        return 0;
    }
    else{
        return -1;
    }
}

void help(){
    // Print the help and exit
    printf("FuzzoTron - A Fuzzing Harness built around OUSPG's Blab and Radamsa.\n\n");
    printf("Usage (crash-detect mode - blab): ./fuzzotron --blab -g http_request -h 127.0.0.1 -p 80 -P tcp -o output\n");
    printf("Usage (crash-detect mode - radamsa): ./fuzzotron --radamsa --directory testcases/ -h 127.0.0.1 -p 80 -P tcp -o output\n");
    printf("Usage (log-monitor mode): ./fuzzotron --blab -g http_request -h 127.0.0.1 -p 80 -P tcp -m /var/log/messages -r 'segfault'\n");
    printf("Usage (process-monitor mode): ./fuzzotron --radamsa --directory testcases/ -h 127.0.0.1 -p 80 -P tcp -c 23123 -o output\n\n");
    printf("Generation Options:\n");
    printf("\t--blab\t\tUse Blab for testcase generation\n");
    printf("\t-g\t\tBlab grammar to use\n");
    printf("\t--radamsa\t\tUse Radamsa for testcase generation\n");
    printf("\t--directory\t\tDirectory with original test cases\n\n");
    printf("Connection Options:\n");
    printf("\t-h\t\tIP of host to connect to REQUIRED\n");
    printf("\t-p\t\tPort to connect to REQUIRED\n");
    printf("\t-P\t\tProtocol to use (tcp,udp) REQUIRED\n");
    printf("\t--ssl\t\tUse SSL for the connection\n");
    printf("\t--destroy\t\tUse TCP_REPAIR mode to immediately destroy the connection, do not send FIN/RST.");
    printf("Monitoring Options:\n");
    printf("\t-c\t\tPID to check - Fuzzotron will halt if this PID dissapears\n");
    printf("\t-m\t\tLogfile to monitor\n");
    printf("\t-r\t\tRegex to use with above logfile\n");
    printf("\t-z\t\tCheck script to execute. Should return 1 on server being okay and anything else otherwise.\n");
    printf("\t--trace\t\tUse AFL style tracing. Single threaded only, see README.md\n");
    exit(0);
}
