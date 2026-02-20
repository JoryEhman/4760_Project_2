/*
shared.h

This header file defines shared data structures and constants used
by both OSS and worker processes.

It includes:
- The shared memory key used for IPC.
- The simulated clock structure.
- The Process Control Block (PCB) structure.
- Project-wide constants such as BILLION (nanoseconds per second)
  and MAX_PROCS (maximum number of concurrent processes).

This file ensures both OSS and workers use identical structure
definitions when accessing shared memory.
*/

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