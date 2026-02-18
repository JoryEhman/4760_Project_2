#ifndef SHARED_H
#define SHARED_H

#define SHM_KEY 0x1234

typedef struct{
    unsigned int seconds;
    unsigned int nanoseconds;
} Simclock;

#endif