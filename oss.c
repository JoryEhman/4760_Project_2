#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "shared.h"

#define CLOCK_INCREMENT 10000000  // 10ms per scheduler iteration

int main(int argc, char* argv[]) {

    // ---------------------------
    // Argument Variables
    // ---------------------------
    int n = 1;          // total processes
    int s = 1;          // max simultaneous
    double t = 0.0;     // total simulation time
    double interval = 0.0; // launch interval

    int opt;

    // ---------------------------
    // Argument Parsing
    // ---------------------------
    while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: oss [-n proc] [-s simul] [-t time] [-i interval]\n");
                return 0;
            case 'n':
                n = atoi(optarg);
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 't':
                t = atof(optarg);
                break;
            case 'i':
                interval = atof(optarg);
                break;
            default:
                fprintf(stderr, "Invalid argument\n");
                return 1;
        }
    }

    // ---------------------------
    // Validation
    // ---------------------------
    if (n < 1 || s < 1) {
        fprintf(stderr, "-n and -s must be at least 1\n");
        return 1;
    }

    if (t < 0 || interval < 0) {
        fprintf(stderr, "-t and -i cannot be negative\n");
        return 1;
    }

    // ---------------------------
    // Float → sec/nano conversion
    // ---------------------------
    unsigned int totalSec = (unsigned int)t;
    unsigned int totalNano = (unsigned int)((t - totalSec) * 1000000000);

    unsigned int intervalSec = (unsigned int)interval;
    unsigned int intervalNano = (unsigned int)((interval - intervalSec) * 1000000000);

    // Suppress unused warnings (temporary until used)
    (void)totalSec;
    (void)totalNano;
    (void)intervalSec;
    (void)intervalNano;

    // ---------------------------
    // Shared Memory Setup
    // ---------------------------
    int shmid = shmget(SHM_KEY, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("oss shmget failed");
        exit(1);
    }

    SimClock *clock = (SimClock *) shmat(shmid, NULL, 0);
    if (clock == (void *) -1) {
        perror("oss shmat failed");
        exit(1);
    }

    clock->seconds = 0;
    clock->nanoseconds = 0;

    printf("OSS PID:%d initialized clock: %u seconds, %u nanoseconds\n",
           getpid(), clock->seconds, clock->nanoseconds);

    // ---------------------------
    // Fork One Worker (current stage)
    // ---------------------------
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        shmdt(clock);
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    if (pid == 0) {

        // Terminate 1 simulated second from current time
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

    // ---------------------------
    // Scheduler Loop
    // ---------------------------
    int status;
    pid_t w;

    while (1) {

        // Increment simulated clock
        clock->nanoseconds += CLOCK_INCREMENT;

        if (clock->nanoseconds >= 1000000000) {
            clock->seconds++;
            clock->nanoseconds -= 1000000000;
        }

        w = waitpid(pid, &status, WNOHANG);

        if (w == 0) {
            continue;
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

    // ---------------------------
    // Cleanup
    // ---------------------------
    shmdt(clock);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}