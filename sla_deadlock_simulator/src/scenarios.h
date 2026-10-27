/* scenarios.h - sample inputs used in the report and in the GUI drop-down. */
#ifndef SCENARIOS_H
#define SCENARIOS_H

typedef struct {
    const char *name;
    const char *avail, *max, *alloc, *reqs;
} Scenario;

#define N_SCENARIOS 4
extern const Scenario SCENARIOS[N_SCENARIOS];

enum { SC_TEXTBOOK = 0, SC_LIGHT = 1, SC_DEADLOCK = 2, SC_CIRCULAR = 3 };

#endif
