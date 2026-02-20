/*
oss.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include "shared.h"

#define CLOCK_INCREMENT 10000000   // 10ms

PCB processTable[MAX_PROCS];

int shmid;
SimClock *simClock;

int totalLaunched = 0;
int totalTerminated = 0;
int childrenInSystem = 0;

unsigned long long totalRuntimeNano = 0;

int n = 1;
int simul = 1;
float timelimit = 0;
float interval = 0;

void cleanup(int sig) {

    if (sig == SIGALRM) {
        const char msg[] =
            "\nOSS: 60-second timeout reached. Cleaning up and exiting.\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
    else if (sig == SIGINT) {
        const char msg[] =
            "\nOSS: Caught SIGINT (Ctrl+C). Cleaning up and exiting.\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGKILL);
        }
    }

    if (simClock != NULL)
        shmdt(simClock);

    if (shmid > 0)
        shmctl(shmid, IPC_RMID, NULL);

    _exit(1);
}

void incrementClock() {
    simClock->nanoseconds += CLOCK_INCREMENT;

    while (simClock->nanoseconds >= BILLION) {
        simClock->seconds++;
        simClock->nanoseconds -= BILLION;
    }
}

unsigned long long getSimTimeNano() {
    return ((unsigned long long)simClock->seconds * BILLION)
           + simClock->nanoseconds;
}

int findFreePCB() {
    for (int i = 0; i < MAX_PROCS; i++)
        if (!processTable[i].occupied)
            return i;
    return -1;
}

void printProcessTable() {

    char buffer[8192];
    int len = 0;

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "\nOSS PID: %d SysClockS: %u SysClockNano: %u\n",
        getpid(), simClock->seconds, simClock->nanoseconds);

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "Process Table:\n"
        "%-5s %-8s %-8s %-10s %-12s %-12s %-15s\n",
        "Entry", "Occupied", "PID",
        "StartS", "StartN",
        "EndingTimeS", "EndingTimeNano");

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processTable[i].occupied) {
            len += snprintf(buffer + len, sizeof(buffer) - len,
                "%-5d %-8d %-8d %-10u %-12u %-12u %-15u\n",
                i,
                processTable[i].occupied,
                processTable[i].pid,
                processTable[i].startSeconds,
                processTable[i].startNano,
                processTable[i].endingTimeSeconds,
                processTable[i].endingTimeNano);
        } else {
            len += snprintf(buffer + len, sizeof(buffer) - len,
                "%-5d %-8d %-8d %-10d %-12d %-12d %-15d\n",
                i, 0, 0, 0, 0, 0, 0);
        }
    }

    write(STDOUT_FILENO, buffer, len);
}

void parseArgs(int argc, char *argv[]) {

    int opt;

    while ((opt = getopt(argc, argv, "n:s:t:i:h")) != -1) {

        switch (opt) {

            case 'n':
                n = atoi(optarg);
                if (n < 1) {
                    fprintf(stderr, "Error: -n must be >= 1\n");
                    exit(1);
                }
                break;

            case 's':
                simul = atoi(optarg);
                if (simul < 1) {
                    fprintf(stderr, "Error: -s must be >= 1\n");
                    exit(1);
                }
                break;

            case 't':
                timelimit = atof(optarg);
                if (timelimit < 0) {
                    fprintf(stderr, "Error: -t cannot be negative\n");
                    exit(1);
                }
                break;

            case 'i':
                interval = atof(optarg);
                if (interval < 0) {
                    fprintf(stderr, "Error: -i cannot be negative\n");
                    exit(1);
                }
                break;

            case 'h':
                printf("Usage: ./oss -n x -s x -t x -i x\n\n");
                printf("Options:\n");
                printf("  -n x : Total number of worker processes to launch (minimum 1)\n");
                printf("  -s x : Maximum number of concurrent processes allowed (minimum 1)\n");
                printf("  -t x : Worker runtime duration in seconds (>= 0)\n");
                printf("  -i x : Interval (in seconds) between launching processes (>= 0)\n");
                printf("  -h   : Display this help message\n");
                exit(0);

            default:
                fprintf(stderr, "Usage: ./oss -n x -s x -t x -i x\n");
                exit(1);
        }
    }
}

int main(int argc, char *argv[]) {

    parseArgs(argc, argv);

    printf("OSS starting, PID: %d PPID: %d\n", getpid(), getppid());
    printf("Called with:\n");
    printf("-n %d\n", n);
    printf("-s %d\n", simul);
    printf("-t %.3f\n", timelimit);
    printf("-i %.3f\n\n", interval);

    fflush(stdout);

    signal(SIGALRM, cleanup);
    signal(SIGINT, cleanup);
    alarm(60);

    /* ===== SHARED MEMORY (FIXED KEY) ===== */
    key_t key = getShmKey();

    // Create if not exists. Do NOT IPC_EXCL on opsys (shared environment).
    shmid = shmget(key, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("oss shmget");
        exit(1);
    }

    simClock = (SimClock*) shmat(shmid, NULL, 0);
    if (simClock == (void*) -1) {
        perror("oss shmat");
        exit(1);
    }
    /* ===================================== */

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    for (int i = 0; i < MAX_PROCS; i++)
        processTable[i].occupied = 0;

    unsigned long long lastPrintBoundary = 0;
    unsigned long long nextLaunchTime = 0;
    unsigned long long intervalNano =
        (unsigned long long)(interval * BILLION);

    while (totalLaunched < n || childrenInSystem > 0) {

        incrementClock();

        unsigned long long currentNano = getSimTimeNano();
        unsigned long long boundary = currentNano / BILLION;

        if (boundary > lastPrintBoundary) {
            printProcessTable();
            lastPrintBoundary = boundary;
        }

        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

            for (int i = 0; i < MAX_PROCS; i++) {
                if (processTable[i].occupied &&
                    processTable[i].pid == pid) {

                    unsigned long long start =
                        ((unsigned long long)processTable[i].startSeconds * BILLION)
                        + processTable[i].startNano;

                    unsigned long long end =
                        ((unsigned long long)processTable[i].endingTimeSeconds * BILLION)
                        + processTable[i].endingTimeNano;

                    if (end > start)
                        totalRuntimeNano += (end - start);

                    processTable[i].occupied = 0;
                    childrenInSystem--;
                    totalTerminated++;

                    break;
                }
            }
        }

        if (totalLaunched == n && childrenInSystem == 0) break;

        if (totalLaunched < n &&
            childrenInSystem < simul &&
            currentNano >= nextLaunchTime) {

            int idx = findFreePCB();

            if (idx != -1) {

                int durSec = (int)timelimit;
                int durNano = (int)((timelimit - durSec) * BILLION);

                processTable[idx].startSeconds = simClock->seconds;
                processTable[idx].startNano = simClock->nanoseconds;

                processTable[idx].endingTimeSeconds =
                    processTable[idx].startSeconds + durSec;

                processTable[idx].endingTimeNano =
                    processTable[idx].startNano + durNano;

                if (processTable[idx].endingTimeNano >= BILLION) {
                    processTable[idx].endingTimeSeconds++;
                    processTable[idx].endingTimeNano -= BILLION;
                }

                pid_t cpid = fork();

                if (cpid == 0) {

                    char secStr[32];
                    char nanoStr[32];

                    snprintf(secStr, sizeof(secStr), "%d", durSec);
                    snprintf(nanoStr, sizeof(nanoStr), "%d", durNano);

                    execl("./worker", "worker",
                          secStr, nanoStr, NULL);

                    perror("exec");
                    exit(1);
                }

                processTable[idx].occupied = 1;
                processTable[idx].pid = cpid;

                childrenInSystem++;
                totalLaunched++;
                nextLaunchTime = currentNano + intervalNano;
            }
        }
    }

    printf("\nOSS PID: %d Terminating\n", getpid());
    printf("%d workers were launched and terminated\n", totalLaunched);
    printf("Workers ran for a combined time of %llu seconds %llu nanoseconds.\n",
           totalRuntimeNano / BILLION,
           totalRuntimeNano % BILLION);

    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}