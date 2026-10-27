/* scenario_table.c - runs the built-in scenarios through both algorithms and prints the
 * numbers used in the report (median of 301 timed runs, log switched off).
 * Build and run with:  make table      (make copycost compares the two state-copy strategies) */
#include <stdio.h>

#include "../src/models.h"
#include "../src/scenarios.h"
#include "../src/simulator.h"

int main(void)
{
    static SystemState st;
    static Request rq[MAX_REQ];
    static Result r[2];
    char err[ERR_LEN];
    int n;

    printf("%-16s %-10s %7s %7s %7s %-10s %7s %7s %8s %9s\n", "scenario", "algorithm", "granted",
           "denied", "done", "deadlock", "checks", "compar", "util %", "median us");
    for (int s = 0; s < N_SCENARIOS; s++) {
        if (!build_state(SCENARIOS[s].avail, SCENARIOS[s].max, SCENARIOS[s].alloc, &st, err) ||
            !parse_requests(SCENARIOS[s].reqs, st.n, st.m, rq, MAX_REQ, &n, err)) {
            fprintf(stderr, "%s\n", err);
            return 1;
        }
        run_banker(&st, rq, n, NULL, &r[0]);
        run_detection(&st, rq, n, NULL, &r[1]);
        for (int a = 0; a < 2; a++) {
            char dead[64] = "none";
            if (r[a].ndead) {
                int k = 0;
                for (int i = 0; i < r[a].ndead; i++)
                    k += snprintf(dead + k, sizeof dead - (size_t)k, "%sP%d", i ? "," : "", r[a].dead[i]);
            }
            double t = bench_median_us(a ? ALGO_DETECTION : ALGO_BANKER, &st, rq, n, 301);
            printf("%-16s %-10s %7d %7d %7d %-10s %7ld %7ld %8.1f %9.2f\n", SCENARIOS[s].name,
                   a ? "detection" : "banker", r[a].granted, r[a].denied, r[a].completed, dead,
                   r[a].checks, r[a].ops, r[a].utilisation * 100.0, t);
        }
    }
    return 0;
}
