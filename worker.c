/**************************************************************
 * worker.c
 **************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shared.h"

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Usage: worker sec nano\n");
        exit(1);
    }

    unsigned int termSec = atoi(argv[1]);
    unsigned int termNano = atoi(argv[2]);

    int shmid = shmget(SHM_KEY, sizeof(SimClock), 0666);
    if (shmid == -1) {
        perror("worker shmget");
        exit(1);
    }

    SimClock *clock = (SimClock*) shmat(shmid, NULL, 0);
    if (clock == (void*) -1) {
        perror("worker shmat");
        exit(1);
    }

    unsigned int startSec = clock->seconds;
    unsigned int startNano = clock->nanoseconds;

    unsigned int targetSec = startSec + termSec;
    unsigned int targetNano = startNano + termNano;

    if (targetNano >= BILLION) {
        targetSec++;
        targetNano -= BILLION;
    }

    printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
    printf("SysClockS:%u SysClockNano:%u TermTimeS:%u TermTimeNano:%u\n",
           startSec, startNano, targetSec, targetNano);
    printf("--Just Starting\n");

    unsigned int lastPrinted = startSec;

    while (1) {

        unsigned int curSec = clock->seconds;
        unsigned int curNano = clock->nanoseconds;

        if (curSec > targetSec ||
           (curSec == targetSec && curNano >= targetNano)) {

            printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
            printf("SysClockS:%u SysClockNano:%u TermTimeS:%u TermTimeNano:%u\n",
                   curSec, curNano, targetSec, targetNano);
            printf("--Terminating\n");

            break;
        }

        if (curSec > lastPrinted) {

            printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
            printf("SysClockS:%u SysClockNano:%u TermTimeS:%u TermTimeNano:%u\n",
                   curSec, curNano, targetSec, targetNano);
            printf("--%u seconds have passed since starting\n",
                   curSec - startSec);

            lastPrinted = curSec;
        }
    }

    shmdt(clock);
    return 0;
}