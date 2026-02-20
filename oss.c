#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

#include "shared.h"

#define CLOCK_INCREMENT 10000000  // 10ms per scheduler iteration

int main(void) {

    // Create shared memory
    int shmid = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("oss shmget failed");
        exit(1);
    }

    // Attach shared memory
    SimClock *clock = (SimClock *) shmat(shmid, NULL, 0);
    if (clock == (void *) -1) {
        perror("oss shmat failed");
        exit(1);
    }

    // Initialize simulated clock
    clock->seconds = 0;
    clock->nanoseconds = 0;

    printf("OSS PID:%d initialized clock: %u seconds, %u nanoseconds\n",
           getpid(), clock->seconds, clock->nanoseconds);

    // Fork worker
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        shmdt(clock);
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    if (pid == 0) {
        // CHILD PROCESS

        // Terminate 1 simulated second from NOW
        unsigned int targetSec = clock->seconds + 1;
        unsigned int targetNano = clock->nanoseconds;

        char secStr[16];
        char nanoStr[16];

        sprintf(secStr, "%u", targetSec);
        sprintf(nanoStr, "%u", targetNano);

        execl("./worker", "worker", secStr, nanoStr, (char*)0);

        perror("exec worker failed");
        exit(1);
    }

    // PARENT: Scheduler Loop
    int status;
    pid_t w;

    while (1) {

        // Increment simulated clock
        clock->nanoseconds += CLOCK_INCREMENT;

        if (clock->nanoseconds >= 1000000000) {
            clock->seconds++;
            clock->nanoseconds -= 1000000000;
        }

        // Non-blocking wait
        w = waitpid(pid, &status, WNOHANG);

        if (w == 0) {
            continue;  // child still running
        }
        else if (w == pid) {
            printf("OSS noticed worker PID %d terminated at time %u:%u\n",
                   pid, clock->seconds, clock->nanoseconds);
            break;
        }
        else {
            if (errno == EINTR)
                continue;
            perror("waitpid failed");
            break;
        }
    }

    // Cleanup
    shmdt(clock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}