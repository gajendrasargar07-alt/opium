/* detection.c - Algorithm 2: deadlock detection (allocate freely, detect later). */
#include "detection.h"

#include <string.h>

int detect_deadlock(int n, int m, const int alloc[][MAX_R], const int request[][MAX_R],
                    const int available[], int *dead, long *ops)
{
    int work[MAX_R];
    unsigned char finish[MAX_P];
    long local_ops = 0;

    memcpy(work, available, sizeof(int) * (size_t)m);
    /* a process that holds nothing cannot be part of a deadlock */
    for (int i = 0; i < n; i++) {
        finish[i] = 1;
        for (int j = 0; j < m; j++)
            if (alloc[i][j] != 0) { finish[i] = 0; break; }
    }

    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            local_ops++;
            int fits = 1;
            for (int j = 0; j < m; j++)
                if (request[i][j] > work[j]) { fits = 0; break; }
            if (fits) {
                for (int j = 0; j < m; j++) work[j] += alloc[i][j];
                finish[i] = 1;
                progress = 1;
            }
        }
    }

    int nd = 0;
    for (int i = 0; i < n; i++)
        if (!finish[i]) dead[nd++] = i;
    if (ops) *ops += local_ops;
    return nd;
}
