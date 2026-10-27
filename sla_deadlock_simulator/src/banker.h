/* banker.h - Algorithm 1: Banker's algorithm (deadlock avoidance). */
#ifndef BANKER_H
#define BANKER_H

#include "models.h"

/* Safety algorithm. Returns 1 if the state is safe, 0 if not.
 * seq (may be NULL) receives the safe sequence, *seq_len how many entries are valid.
 * *ops (may be NULL) is increased by one for every 'Need <= Work' comparison made. */
int is_safe(const SystemState *s, int *seq, int *seq_len, long *ops);

typedef struct {
    int         granted;     /* 1 if the request was granted                    */
    const char *reason;      /* short text for the log                          */
    long        ops;         /* comparisons made by the safety algorithm        */
    int         safety_ran;  /* 1 if the safety algorithm was run for this request */
} ReqOutcome;

/* Resource-request algorithm. Changes *s only if the request is granted. */
ReqOutcome request_resources(SystemState *s, int pid, const int *req);

#endif
