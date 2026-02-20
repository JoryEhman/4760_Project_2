#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

#define SHM_KEY 0x1234
#define BILLION 1000000000
#define MAX_PROCS 20

typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SimClock;

typedef struct {
    int occupied;
    pid_t pid;
    unsigned int startSeconds;
    unsigned int startNano;
    unsigned int endingTimeSeconds;
    unsigned int endingTimeNano;
} PCB;

#endif