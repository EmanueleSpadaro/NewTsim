//
// Created by maffin on 1/31/22.
//

#ifndef NEWTSIM_SO_RANDOM_H
#define NEWTSIM_SO_RANDOM_H
#include <time.h>

/* Returns a randomly generated long included in [minInc, maxExcl-1] */
long so_random(long minInc, long maxExcl);

/* Sleeps for given nanoseconds... */
void nsleep(long nsecs);

/* Sets an alarm timer that sends a SIGALRM each nsecs */
timer_t setalarmtimer(long nsecs);

#endif //NEWTSIM_SO_RANDOM_H
