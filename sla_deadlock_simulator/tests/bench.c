/* bench.c - scaling test. Builds the worst case for the safety/detection loops
 * (processes can only finish in reverse index order, one per pass) and prints CSV.
 * Build and run with:  make bench */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/banker.h"
#include "../src/detection.h"
#include "../src/models.h"
#include "../src/simulator.h"

static SystemState S;
static int REQM[MAX_P][MAX_R];

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *t, int k)
{
    qsort(t, (size_t)k, sizeof(double), cmp);
    return t[k / 2];
}

int main(void)
{
    int sizes[] = { 10, 50, 100, 200, 400, 800, 1024 };
    static int seq[MAX_P], dead[MAX_P];

    puts("n,comparisons_safety,comparisons_detect,n(n+1)/2,safety_us,detect_us,random_state_comparisons");
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        int n = sizes[k];
        memset(&S, 0, sizeof S);
        S.n = n; S.m = 1; S.available[0] = 0;
        for (int i = 0; i < n; i++) {
            S.allocation[i][0] = 1;
            S.maximum[i][0] = n - i;        /* Need = n-1-i: P(n-1) first, then P(n-2), ... */
            REQM[i][0] = n - 1 - i;         /* same numbers seen as waiting requests        */
        }
        int reps = 20000000 / (n * n) + 15;
        if (reps > 20000) reps = 20000;
        double *ts = malloc(sizeof(double) * (size_t)reps), *td = malloc(sizeof(double) * (size_t)reps);
        long ops_s = 0, ops_d = 0;
        int len;
        for (int r = 0; r < reps; r++) {
            long o1 = 0, o2 = 0;
            double a = now_us();
            int safe = is_safe(&S, seq, &len, &o1);
            double b = now_us();
            int nd = detect_deadlock(n, 1, (const int (*)[MAX_R])S.allocation,
                                     (const int (*)[MAX_R])REQM, S.available, dead, &o2);
            double c = now_us();
            if (!safe || nd != 0) { fprintf(stderr, "worst-case construction broken\n"); return 1; }
            ts[r] = b - a; td[r] = c - b;
            ops_s = o1; ops_d = o2;
        }
        /* a random state, to show why the constructed one is needed */
        srand(12345u + (unsigned)n);
        SystemState R;
        memset(&R, 0, sizeof R);
        R.n = n; R.m = 3;
        for (int j = 0; j < 3; j++) R.available[j] = rand() % 5;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++) {
                R.allocation[i][j] = rand() % 4;
                R.maximum[i][j] = R.allocation[i][j] + rand() % 6;
            }
        long ops_r = 0;
        is_safe(&R, seq, &len, &ops_r);

        printf("%d,%ld,%ld,%ld,%.2f,%.2f,%ld\n", n, ops_s, ops_d, (long)n * (n + 1) / 2,
               median(ts, reps), median(td, reps), ops_r);
        free(ts); free(td);
    }
    return 0;
}
