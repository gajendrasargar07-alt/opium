/* simulator.h - runs one request queue through either algorithm and collects metrics. */
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stddef.h>
#include "models.h"

/* Text log. buf may be NULL, in which case nothing is recorded (used for timing runs). */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    int    truncated;
} Log;

void log_init(Log *lg, char *buf, size_t cap);

typedef struct {
    const char *algorithm;
    int    granted, denied;
    int    ndead, dead[MAX_P];
    int    completed;
    long   ops;            /* 'Need/Request <= Work' comparisons */
    long   checks;         /* times the safety / detection algorithm ran */
    double utilisation;    /* 0.0 .. 1.0, averaged over the steps */
    double time_us;        /* wall-clock time of the run in microseconds */
} Result;

void run_banker(const SystemState *s0, const Request *reqs, int nreq, Log *lg, Result *res);
void run_detection(const SystemState *s0, const Request *reqs, int nreq, Log *lg, Result *res);

typedef enum { ALGO_BANKER = 1, ALGO_DETECTION = 2 } Algo;

/* Median wall-clock time (microseconds) of `reps` silent runs. */
double bench_median_us(Algo a, const SystemState *s0, const Request *reqs, int nreq, int reps);

/* Monotonic clock in microseconds. */
double now_us(void);

#endif
