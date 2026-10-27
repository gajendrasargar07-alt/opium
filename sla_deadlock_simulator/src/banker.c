/* banker.c - Algorithm 1: Banker's algorithm (deadlock avoidance). */
#include "banker.h"

#include <string.h>

int is_safe(const SystemState *s, int *seq, int *seq_len, long *ops)
{
    int n = s->n, m = s->m;
    int work[MAX_R];
    unsigned char finish[MAX_P];
    int count = 0;
    long local_ops = 0;

    memcpy(work, s->available, sizeof(int) * (size_t)m);
    memset(finish, 0, (size_t)n);

    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            local_ops++;
            int fits = 1;
            for (int j = 0; j < m; j++)
                if (need_of(s, i, j) > work[j]) { fits = 0; break; }
            if (fits) {
                for (int j = 0; j < m; j++) work[j] += s->allocation[i][j];
                finish[i] = 1;
                if (seq) seq[count] = i;
                count++;
                progress = 1;
            }
        }
    }
    if (seq_len) *seq_len = count;
    if (ops) *ops += local_ops;
    return count == n;
}

ReqOutcome request_resources(SystemState *s, int pid, const int *req)
{
    ReqOutcome out = { 0, "", 0, 0 };
    int m = s->m;

    if (pid < 0 || pid >= s->n) {
        out.reason = "no such process";
        return out;
    }
    for (int j = 0; j < m; j++)
        if (req[j] > need_of(s, pid, j)) {
            out.reason = "exceeds declared maximum claim";
            return out;
        }
    for (int j = 0; j < m; j++)
        if (req[j] > s->available[j]) {
            out.reason = "not enough free resources, process must wait";
            return out;
        }

    /* pretend to grant on a copy; the real state is touched only if the copy is safe */
    SystemState trial;
    state_copy(&trial, s);
    for (int j = 0; j < m; j++) {
        trial.available[j] -= req[j];
        trial.allocation[pid][j] += req[j];
    }
    out.safety_ran = 1;
    if (!is_safe(&trial, NULL, NULL, &out.ops)) {
        out.reason = "would lead to an unsafe state";
        return out;
    }
    state_copy(s, &trial);
    out.granted = 1;
    out.reason = "granted, state stays safe";
    return out;
}
