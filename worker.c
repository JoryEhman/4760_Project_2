/*
worker.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shared.h"

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Usage: worker intervalSec intervalNano\n");
        exit(1);
    }

    unsigned int intervalSec  = atoi(argv[1]);
    unsigned int intervalNano = atoi(argv[2]);

    printf("Worker starting, PID:%d PPID:%d\n", getpid(), getppid());
    printf("Called with:\n");
    printf("Interval: %u seconds, %u nanoseconds\n",
           intervalSec, intervalNano);
    fflush(stdout);

    /* ===== SHARED MEMORY (FIXED KEY) ===== */
    key_t key = getShmKey();

    int shmid = shmget(key, sizeof(SimClock), 0666);
    if (shmid == -1) {
        perror("worker shmget");
        exit(1);
    }

    volatile SimClock *clock = (volatile SimClock*) shmat(shmid, NULL, 0);
    if (clock == (void*) -1) {
        perror("worker shmat");
        exit(1);
    }
    /* ===================================== */

    unsigned int startSec = clock->seconds;
    unsigned int startNano = clock->nanoseconds;

    unsigned int targetSec  = startSec + intervalSec;
    unsigned int targetNano = startNano + intervalNano;

    if (targetNano >= BILLION) {
        targetSec++;
        targetNano -= BILLION;
    }

    printf("WORKER PID: %d PPID: %d\n", getpid(), getppid());
    printf("SysClockS: %u SysClockNano: %u TermTimeS: %u TermTimeNano: %u\n",
           startSec, startNano, targetSec, targetNano);
    printf("--Just Starting\n");
    fflush(stdout);

    unsigned int lastPrinted = startSec;

    while (1) {

        unsigned int curSec = clock->seconds;
        unsigned int curNano = clock->nanoseconds;

        if (curSec > targetSec ||
           (curSec == targetSec && curNano >= targetNano)) {

            printf("WORKER PID: %d PPID: %d\n", getpid(), getppid());
            printf("SysClockS: %u SysClockNano: %u TermTimeS: %u TermTimeNano: %u\n",
                   curSec, curNano, targetSec, targetNano);
            printf("--Terminating\n");
            fflush(stdout);
            break;
        }

        if (curSec > lastPrinted) {

            printf("WORKER PID: %d PPID: %d\n", getpid(), getppid());
            printf("SysClockS: %u SysClockNano: %u TermTimeS: %u TermTimeNano: %u\n",
                   curSec, curNano, targetSec, targetNano);
            printf("--%u seconds have passed since starting\n",
                   curSec - startSec);
            fflush(stdout);

            lastPrinted = curSec;
        }
    }

    shmdt((const void *)clock);
    return 0;
}