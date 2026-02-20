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

int n = 5;
int simul = 2;
float timelimit = 4.0;
float interval = 0.5;

void cleanup(int sig) {
    printf("\nOSS: Caught signal %d\n", sig);

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
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

int findFreePCB() {
    for (int i = 0; i < MAX_PROCS; i++)
        if (!processTable[i].occupied)
            return i;
    return -1;
}

int main(int argc, char *argv[]) {

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

    while (totalTerminated < n) {

        incrementClock();

        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < MAX_PROCS; i++) {
                if (processTable[i].occupied &&
                    processTable[i].pid == pid) {

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

                pid_t cpid = fork();

                if (cpid == 0) {
                    execl("./worker", "worker", "2", "500000000", NULL);
                    perror("exec");
                    exit(1);
                }

                processTable[idx].occupied = 1;
                processTable[idx].pid = cpid;
                processTable[idx].startSeconds = simClock->seconds;
                processTable[idx].startNano = simClock->nanoseconds;

                childrenInSystem++;
                totalLaunched++;
            }
        }
    }

    printf("OSS terminating.\n");

    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}