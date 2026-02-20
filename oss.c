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
#define MAX_PROCESSES 20

typedef struct {
    int occupied;
    pid_t pid;
    unsigned int startSec;
    unsigned int startNano;
    unsigned int targetSec;
    unsigned int targetNano;
} PCB;

static void normalize_time(unsigned int *sec, unsigned int *nano) {
    while (*nano >= 1000000000) {
        (*sec)++;
        *nano -= 1000000000;
    }
}

static void add_time(unsigned int baseSec, unsigned int baseNano,
                     unsigned int addSec, unsigned int addNano,
                     unsigned int *outSec, unsigned int *outNano) {
    *outSec = baseSec + addSec;
    *outNano = baseNano + addNano;
    normalize_time(outSec, outNano);
}

static int find_free_pcb(PCB table[]) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!table[i].occupied) return i;
    }
    return -1;
}

static int find_pcb_by_pid(PCB table[], pid_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].occupied && table[i].pid == pid) return i;
    }
    return -1;
}

int main(int argc, char* argv[]) {

    // ---------------------------
    // Argument Variables
    // ---------------------------
    int n = 1;             // total processes to launch
    int s = 1;             // max simultaneous
    double t = 0.0;        // total simulation time (unused in this chunk)
    double interval = 0.0; // launch interval (unused in this chunk)

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
    if (s > MAX_PROCESSES) {
        fprintf(stderr, "-s cannot exceed %d\n", MAX_PROCESSES);
        return 1;
    }

    // Float → sec/nano conversion (we will use later)
    unsigned int totalSec = (unsigned int)t;
    unsigned int totalNano = (unsigned int)((t - totalSec) * 1000000000);

    unsigned int intervalSec = (unsigned int)interval;
    unsigned int intervalNano = (unsigned int)((interval - intervalSec) * 1000000000);

    (void)totalSec; (void)totalNano;
    (void)intervalSec; (void)intervalNano;

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

    printf("OSS PID:%d initialized clock: %u:%u\n",
           getpid(), clock->seconds, clock->nanoseconds);

    // ---------------------------
    // PCB Table Init
    // ---------------------------
    PCB table[MAX_PROCESSES];
    for (int i = 0; i < MAX_PROCESSES; i++) {
        table[i].occupied = 0;
        table[i].pid = 0;
        table[i].startSec = table[i].startNano = 0;
        table[i].targetSec = table[i].targetNano = 0;
    }

    int totalLaunched = 0;
    int activeChildren = 0;

    // ---------------------------
    // Main Scheduler Loop
    // ---------------------------
    while (totalLaunched < n || activeChildren > 0) {

        // 1) Advance simulated clock
        clock->nanoseconds += CLOCK_INCREMENT;
        if (clock->nanoseconds >= 1000000000) {
            clock->seconds++;
            clock->nanoseconds -= 1000000000;
        }

        // 2) Reap any terminated children (NONBLOCKING)
        int status;
        pid_t w;
        while ((w = waitpid(-1, &status, WNOHANG)) > 0) {
            int idx = find_pcb_by_pid(table, w);
            if (idx != -1) {
                table[idx].occupied = 0;
            }
            activeChildren--;
            // (optional) printf("OSS: reaped PID %d at %u:%u\n", w, clock->seconds, clock->nanoseconds);
        }
        if (w == -1 && errno != ECHILD && errno != EINTR) {
            perror("waitpid failed");
            break;
        }

        // 3) Launch new children if allowed
        while (totalLaunched < n && activeChildren < s) {
            int slot = find_free_pcb(table);
            if (slot == -1) {
                // Shouldn't happen if s <= MAX_PROCESSES, but safe.
                break;
            }

            // Assign target time: NOW + 1 simulated second (for this chunk)
            unsigned int targetSec, targetNano;
            add_time(clock->seconds, clock->nanoseconds, 1, 0, &targetSec, &targetNano);

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                break;
            }

            if (pid == 0) {
                char secStr[16], nanoStr[16];
                sprintf(secStr, "%u", targetSec);
                sprintf(nanoStr, "%u", targetNano);
                execl("./worker", "worker", secStr, nanoStr, (char*)0);
                perror("exec worker failed");
                exit(1);
            }

            // Parent records PCB
            table[slot].occupied = 1;
            table[slot].pid = pid;
            table[slot].startSec = clock->seconds;
            table[slot].startNano = clock->nanoseconds;
            table[slot].targetSec = targetSec;
            table[slot].targetNano = targetNano;

            totalLaunched++;
            activeChildren++;

            printf("OSS: launched PID %d (totalLaunched=%d active=%d) start %u:%u target %u:%u\n",
                   pid, totalLaunched, activeChildren,
                   table[slot].startSec, table[slot].startNano,
                   table[slot].targetSec, table[slot].targetNano);
        }
    }

    // ---------------------------
    // Cleanup (temporary; signal handling later)
    // ---------------------------
    shmdt(clock);
    shmctl(shmid, IPC_RMID, NULL);

    printf("OSS: finished. totalLaunched=%d\n", totalLaunched);
    return 0;
}