#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "shared.h"

int main(){
    int shmid = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1){
        perror("shmget failed");
        exit(1);
    }

    SimClock *clock = (SimClock *) shmat(shmid, NULL, 0);
    if (clock == (void *) -1) {
        perror("shmat failed");
        exit(1);
    }

    clock -> seconds = 0;
    clock -> nanoseconds = 0;

    printf("OSS initialized clock: %u seconds, %u nanoseconds\n", clock -> seconds, clock -> nanoseconds);

    shmdt(clock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}