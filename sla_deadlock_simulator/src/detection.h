/* detection.h - Algorithm 2: deadlock detection for multiple resource instances. */
#ifndef DETECTION_H
#define DETECTION_H

#include "models.h"

/* alloc[i]   = what process i holds
 * request[i] = what process i is waiting for right now (all zeros if not waiting)
 * dead[]     receives the ids of deadlocked processes (room for n entries)
 * *ops       is increased by one for every 'Request <= Work' comparison made.
 * Returns the number of deadlocked processes. */
int detect_deadlock(int n, int m, const int alloc[][MAX_R], const int request[][MAX_R],
                    const int available[], int *dead, long *ops);

#endif
