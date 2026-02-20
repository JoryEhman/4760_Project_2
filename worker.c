#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shared.h"

int main(int argc, char *argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Usage: worker seconds nanoseconds\n");
        return 1;
    }

    unsigned int termSec = atoi(argv[1]);
    unsigned int termNano = atoi(argv[2]);

    int shmid = shmget(SHM_KEY, sizeof(SimClock), 0666);
    if (shmid == -1) {
        perror("worker shmget failed");
        return 1;
    }

    SimClock *clock = (SimClock *) shmat(shmid, NULL, 0);
    if (clock == (void *) -1) {
        perror("worker shmat failed");
        return 1;
    }

    printf("WORKER PID:%d starting at %u:%u\n",
           getpid(), clock->seconds, clock->nanoseconds);

    while (1) {

        // Check if current simulated time reached termination time
        if ((clock->seconds > termSec) ||
            (clock->seconds == termSec && clock->nanoseconds >= termNano)) {
            break;
            }
    }

    printf("WORKER PID:%d terminating at %u:%u\n",
           getpid(), clock->seconds, clock->nanoseconds);

    shmdt(clock);
    return 0;
}