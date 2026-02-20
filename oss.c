/*
oss.c

This file implements the operating system simulator (OSS) for the project.

The program is responsible for:
- Maintaining a simulated system clock in shared memory.
- Managing a fixed-size Process Control Block (PCB) table.
- Launching worker processes according to command-line parameters.
- Enforcing a maximum number of concurrent processes.
- Tracking process start and termination times.
- Printing the process table at regular simulated time intervals.
- Calculating total runtime statistics for all workers.
- Handling SIGINT (Ctrl+C) and SIGALRM (60-second timeout) to ensure
  proper cleanup of child processes and shared memory.

The main loop advances the simulated clock, checks for terminated
children, launches new workers when allowed, and prints the system
state every 0.5 simulated seconds.

On termination (normal or signal), all remaining child processes are
killed and shared memory is detached and removed.
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

/* ===== Correct Defaults ===== */
int n = 1;
int simul = 1;
float timelimit = 0;
float interval = 0;
/* ============================================================ */

void cleanup(int sig) {

    const char msg[] =
        "\nOSS: 60-second timeout reached. Cleaning up and exiting.\n";

    write(STDOUT_FILENO, msg, sizeof(msg) - 1);

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

    printf("\nOSS PID: %d SysClockS: %u SysClockNano: %u\n",
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

    fflush(stdout);
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
    			printf("  -t x : Total simulated runtime limit in seconds (must be >= 0)\n");
    			printf("  -i x : Interval (in seconds) between launching processes (must be >= 0)\n");
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

    /* ===== REQUIRED STARTUP OUTPUT ===== */
    printf("OSS starting, PID: %d PPID: %d\n", getpid(), getppid());
    printf("Called with:\n");
    printf("-n %d\n", n);
    printf("-s %d\n", simul);
    printf("-t %.3f\n", timelimit);
    printf("-i %.3f\n\n", interval);
    fflush(stdout);
    /* ==================================== */

    signal(SIGALRM, cleanup);
    signal(SIGINT, cleanup);
    alarm(60);

    shmid = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) { perror("shmget"); exit(1); }

    simClock = (SimClock*) shmat(shmid, NULL, 0);
    if (simClock == (void*) -1) { perror("shmat"); exit(1); }

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
        unsigned long long boundary = currentNano / 500000000ULL;

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

                    unsigned long long startNano =
                        ((unsigned long long)processTable[i].startSeconds * BILLION)
                        + processTable[i].startNano;

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

        if (totalLaunched < n &&
            childrenInSystem < simul &&
            currentNano >= nextLaunchTime) {

            int idx = findFreePCB();

            if (idx != -1) {

                int secPart = (int)timelimit;
                int nanoPart =
                    (int)((timelimit - secPart) * BILLION);

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

                pid_t cpid = fork();

                if (cpid == 0) {

                    char secStr[20];
                    char nanoStr[20];

                    sprintf(secStr, "%u",
                            processTable[idx].endingTimeSeconds);
                    sprintf(nanoStr, "%u",
                            processTable[idx].endingTimeNano);

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