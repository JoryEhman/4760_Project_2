#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

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

    printf("OSS PID:%d initialized clock: %u seconds, %u nanoseconds\n", getpid(), clock -> seconds, clock -> nanoseconds);

    pid_t pid = fork();
    if (pid < 0){
        perror("fork failed");
        shmdt(clock);
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    if (pid == 0){
        execl("./worker", "worker", (char *)0);
        perror("exec worker failed");
        exit(1);
    }

    //Parent: nonblocking wait loop (NO wait(), No sleep)
    int status;
    pid_t w;
    do {
        w = waitpid(pid, &status, WNOHANG);
        if (w == 0){
            //child is still running, just keep spinning for this milestone (later we will increment the simulated clock here)
            continue;
        } else if (w == -1){
            if (errno == EINTR) continue;
            perror("waitpid failed");
            break;
        }
    } while (w == 0);

    printf("OSS noticed worker PID %d terminated.\n", pid);

    //Clean up: now safe to remove shared memory (worker already exited)
    shmdt(clock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}