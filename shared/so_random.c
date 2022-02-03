/*
 Created by maffin on 1/31/22.
*/

#include "so_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

long so_random(long minInc, long maxExcl)
{
    if (minInc > maxExcl) {
        fprintf(stderr, "MinInc > MaxExcl @ so_random\n");
        exit(EXIT_FAILURE);
    }
    if (minInc == maxExcl)
        return minInc;
    return (random() % (maxExcl - minInc)) + minInc;
}

/* Sleeps for given nanoseconds... */
void nsleep(long nsecs)
{
    extern char sigint;
    struct timespec ts;

    ts.tv_sec = nsecs / 1000000000;
    ts.tv_nsec = nsecs % 1000000000;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR && !sigint);
}

timer_t setalarmtimer(long nsecs)
{
    timer_t timerid;
    struct sigevent sev;
    struct itimerspec its;

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = &timerid;

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
        return (timer_t)-1;

    its.it_value.tv_sec = nsecs / 1000000000;
    its.it_value.tv_nsec = nsecs % 1000000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
        return (timer_t)-1;

    return timerid;
}