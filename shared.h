/*
shared.h
*/

#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>
#include <sys/ipc.h>   // key_t, ftok
#include <stdio.h>     // perror
#include <stdlib.h>    // exit

#define BILLION 1000000000
#define MAX_PROCS 20

// Generate a unique key based on a file in *this* project directory.
// Using "." means "current directory", so each student/project folder
// gets a different key on opsys.
static inline key_t getShmKey(void) {
    key_t key = ftok(".", 'E');   // 'E' can be any project-specific char
    if (key == -1) {
        perror("ftok");
        exit(1);
    }
    return key;
}

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