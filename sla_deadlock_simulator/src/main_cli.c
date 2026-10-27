/* main_cli.c - console front end (same engine as the GUI).
 * Usage: deadlock_cli [scenario 1-4]     (default 1) */
#include <stdio.h>
#include <stdlib.h>

#include "models.h"
#include "scenarios.h"
#include "simulator.h"

static char logbuf[1 << 16];

static void show(const Result *r, const Log *lg)
{
    printf("\n== %s ==\n%s", r->algorithm, lg->buf);
    printf("granted=%d denied/blocked=%d completed=%d deadlocked=", r->granted, r->denied, r->completed);
    if (!r->ndead) printf("none");
    for (int i = 0; i < r->ndead; i++) printf("%sP%d", i ? "," : "", r->dead[i]);
    printf("\nchecks=%ld comparisons=%ld utilisation=%.1f%% time=%.1f us\n",
           r->checks, r->ops, r->utilisation * 100.0, r->time_us);
}

int main(int argc, char **argv)
{
    int idx = argc > 1 ? atoi(argv[1]) - 1 : 0;
    if (idx < 0 || idx >= N_SCENARIOS) {
        fprintf(stderr, "Usage: %s [scenario 1-%d]\n", argv[0], N_SCENARIOS);
        for (int i = 0; i < N_SCENARIOS; i++) fprintf(stderr, "  %d  %s\n", i + 1, SCENARIOS[i].name);
        return 2;
    }
    const Scenario *sc = &SCENARIOS[idx];
    static SystemState st;
    static Request reqs[MAX_REQ];
    static Result res;
    int nreq;
    char err[ERR_LEN];

    if (!build_state(sc->avail, sc->max, sc->alloc, &st, err) ||
        !parse_requests(sc->reqs, st.n, st.m, reqs, MAX_REQ, &nreq, err)) {
        fprintf(stderr, "Input error: %s\n", err);
        return 1;
    }
    printf("Scenario: %s\n", sc->name);
    Log lg;
    log_init(&lg, logbuf, sizeof logbuf);
    run_banker(&st, reqs, nreq, &lg, &res);
    show(&res, &lg);
    log_init(&lg, logbuf, sizeof logbuf);
    run_detection(&st, reqs, nreq, &lg, &res);
    show(&res, &lg);
    return 0;
}
