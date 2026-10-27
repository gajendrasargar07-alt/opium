/* models.h - data model and input validation for the Resource Allocation Simulator.
 *
 * The whole system state is one structure with no pointers, so a state can be
 * copied with state_copy() (or plain assignment) and the copy is independent. */
#ifndef MODELS_H
#define MODELS_H

#include <stddef.h>

#define MAX_P      1024      /* most processes the simulator accepts      */
#define MAX_R      8         /* most resource types                        */
#define MAX_REQ    2048      /* most requests in one queue                 */
#define MAX_VALUE  1000000   /* largest number accepted in any input       */
#define ERR_LEN    200       /* size of every error-message buffer         */

typedef struct {
    int n;                          /* number of processes                 */
    int m;                          /* number of resource types            */
    int available[MAX_R];
    int maximum[MAX_P][MAX_R];
    int allocation[MAX_P][MAX_R];
} SystemState;

typedef struct {
    int pid;
    int vec[MAX_R];
} Request;

/* Need[i][j] = Max[i][j] - Allocation[i][j] */
static inline int need_of(const SystemState *s, int i, int j)
{
    return s->maximum[i][j] - s->allocation[i][j];
}

/* Copies only the rows that are in use (a full 64 KB copy per trial grant is wasteful). */
void state_copy(SystemState *dst, const SystemState *src);

/* total[j] = Available[j] + sum of Allocation[i][j] */
void state_total(const SystemState *s, long total[MAX_R]);

/* All parse/validate functions return 1 on success, 0 on failure.
 * On failure a readable message is written into err (size ERR_LEN). */
int parse_vector(const char *text, int *out, int cap, int *count, char *err);
int build_state(const char *avail_txt, const char *max_txt, const char *alloc_txt,
                SystemState *out, char *err);
int parse_requests(const char *text, int n, int m, Request *out, int cap,
                   int *count, char *err);

/* "[3, 3, 2]" style text for the log. Returns number of characters written. */
int fmt_vec(char *buf, size_t cap, const int *v, int m);

#endif
