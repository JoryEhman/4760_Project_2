/**************************************************************
 * oss.c
 **************************************************************/

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

/* Defaults */
int n = 5;
int simul = 2;
float timelimit = 4.0;
float interval = 0.5;

void cleanup(int sig) {

    printf("\nOSS: Caught signal %d\n", sig);

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGKILL);
        }
    }

    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);

    exit(1);
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

    printf("\nOSS PID:%d SysClockS:%u SysClockNano:%u\n",
           getpid(), simClock->seconds, simClock->nanoseconds);

    printf("Process Table:\n");
    printf("%-5s %-8s %-8s %-10s %-12s %-12s %-15s\n",
           "Entry", "Occupied", "PID",
           "StartS", "StartN",
           "EndingTimeS", "EndingTimeNano");

    for (int i = 0; i < MAX_PROCS; i++) {

        if (processTable[i].occupied) {
            printf("%-5d %-8d %-8d %-10u %-12u %-12u %-15u\n",
                   i,
                   processTable[i].occupied,
                   processTable[i].pid,
                   processTable[i].startSeconds,
                   processTable[i].startNano,
                   processTable[i].endingTimeSeconds,
                   processTable[i].endingTimeNano);
        } else {
            printf("%-5d %-8d\n", i, 0);
        }
    }
}

void parseArgs(int argc, char *argv[]) {

    int opt;

    while ((opt = getopt(argc, argv, "n:s:t:i:")) != -1) {
        switch (opt) {

            case 'n':
                n = atoi(optarg);
                if (n < 1) {
                    fprintf(stderr, "-n must be >= 1\n");
                    exit(1);
                }
                break;

            case 's':
                simul = atoi(optarg);
                if (simul < 1) {
                    fprintf(stderr, "-s must be >= 1\n");
                    exit(1);
                }
                break;

            case 't':
                timelimit = atof(optarg);
                if (timelimit < 0) {
                    fprintf(stderr, "-t must be >= 0\n");
                    exit(1);
                }
                break;

            case 'i':
                interval = atof(optarg);
                if (interval < 0) {
                    fprintf(stderr, "-i must be >= 0\n");
                    exit(1);
                }
                break;

            default:
                fprintf(stderr, "Usage: ./oss -n x -s x -t x -i x\n");
                exit(1);
        }
    }
}

int main(int argc, char *argv[]) {


    parseArgs(argc, argv);

    printf("OSS starting, PID:%d PPID:%d\n", getpid(), getppid());
    printf("Called with:\n");
    printf("-n %d\n", n);
    printf("-s %d\n", simul);
    printf("-t %.1f\n", timelimit);
    printf("-i %.1f\n", interval);
    printf("\n");

    signal(SIGALRM, cleanup);
    signal(SIGINT, cleanup);
    alarm(60);

    shmid = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) { perror("oss shmget"); exit(1); }

    simClock = (SimClock*) shmat(shmid, NULL, 0);
    if (simClock == (void*) -1) { perror("oss shmat"); exit(1); }

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    for (int i = 0; i < MAX_PROCS; i++)
        processTable[i].occupied = 0;

    unsigned long long lastPrintBoundary = 0;

    while (totalTerminated < n) {

        incrementClock();

        /* --------- FIXED PCB PRINT LOGIC --------- */
        unsigned long long currentNano = getSimTimeNano();
        unsigned long long boundary = currentNano / 500000000ULL;

        if (boundary > lastPrintBoundary) {
            printProcessTable();
            lastPrintBoundary = boundary;
        }
        /* ----------------------------------------- */

        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

            for (int i = 0; i < MAX_PROCS; i++) {

                if (processTable[i].occupied &&
                    processTable[i].pid == pid) {

                    unsigned long long startNano =
                        ((unsigned long long)processTable[i].startSeconds * BILLION)
                        + processTable[i].startNano;

                    /* USE STORED TERMINATION TIME — NOT CURRENT CLOCK */
                    unsigned long long endNano =
                        ((unsigned long long)processTable[i].endingTimeSeconds * BILLION)
                        + processTable[i].endingTimeNano;

                    totalRuntimeNano += (endNano - startNano);

                    processTable[i].occupied = 0;
                    childrenInSystem--;
                    totalTerminated++;
                    break;
                    }
            }
        }

        if (totalLaunched < n && childrenInSystem < simul) {

            int idx = findFreePCB();

            if (idx != -1) {

                int secPart = (int)timelimit;
                int nanoPart = (int)((timelimit - secPart) * BILLION);

                pid_t cpid = fork();

                if (cpid == 0) {

                    char secStr[20];
                    char nanoStr[20];

                    sprintf(secStr, "%d", secPart);
                    sprintf(nanoStr, "%d", nanoPart);

                    execl("./worker", "worker",
                          secStr, nanoStr, NULL);

                    perror("exec");
                    exit(1);
                }

                processTable[idx].occupied = 1;
                processTable[idx].pid = cpid;
                processTable[idx].startSeconds = simClock->seconds;
                processTable[idx].startNano = simClock->nanoseconds;

                processTable[idx].endingTimeSeconds =
                    processTable[idx].startSeconds + secPart;

                processTable[idx].endingTimeNano =
                    processTable[idx].startNano + nanoPart;

                if (processTable[idx].endingTimeNano >= BILLION) {
                    processTable[idx].endingTimeSeconds++;
                    processTable[idx].endingTimeNano -= BILLION;
                }

                childrenInSystem++;
                totalLaunched++;
            }
        }
    }

    printf("\nOSS PID:%d Terminating\n", getpid());
    printf("%d workers were launched and terminated\n", totalLaunched);
    printf("Workers ran for a combined time of %llu seconds %llu nanoseconds.\n",
           totalRuntimeNano / BILLION,
           totalRuntimeNano % BILLION);

    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}