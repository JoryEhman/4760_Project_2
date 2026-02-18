#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shared.h"

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    int shmid = shmget(SHM_KEY, sizeof(SimClock), 0666);
    if (shmid == -1) {
        perror("worker shmget failed");
        return 1;
    }

    SimClock *clock = (SimClock *)shmat(shmid, NULL, 0);
    if (clock == (void *) -1){
        perror("worker shmat failed");
        return 1;
    }

    printf("WORKER starting PID:%d PPID %d\n", getpid(), getppid());
    printf("WORKER sees clock: %u seconds, %u nanoseconds\n", clock -> seconds, clock -> nanoseconds);

    shmdt(clock);
    return 0;
}
