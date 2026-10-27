/* test_sim.c - unit tests for the algorithms, the parser and the simulation engine.
 * Build and run with:  make test */
#include <stdio.h>
#include <string.h>

#include "../src/banker.h"
#include "../src/detection.h"
#include "../src/models.h"
#include "../src/scenarios.h"
#include "../src/simulator.h"

static int failures = 0, tests_run = 0;
static char err[ERR_LEN];

#define CHECK(cond) do { if (!(cond)) { printf("    FAILED: %s (line %d)\n", #cond, __LINE__); return 0; } } while (0)

static SystemState S;                      /* shared scratch state */
static Request     RQ[MAX_REQ];
static int         NRQ;

static int load(int idx)
{
    const Scenario *sc = &SCENARIOS[idx];
    return build_state(sc->avail, sc->max, sc->alloc, &S, err) &&
           parse_requests(sc->reqs, S.n, S.m, RQ, MAX_REQ, &NRQ, err);
}

static int test_textbook_safe(void)
{
    int seq[MAX_P], len = 0;
    CHECK(load(SC_TEXTBOOK));
    CHECK(is_safe(&S, seq, &len, NULL) == 1);
    int want[5] = { 1, 3, 4, 0, 2 };
    CHECK(len == 5);
    CHECK(memcmp(seq, want, sizeof want) == 0);
    return 1;
}

static int test_unsafe_request_denied(void)
{
    CHECK(load(SC_TEXTBOOK));
    int r1[3] = { 1, 0, 2 }, r0[3] = { 0, 2, 0 };
    CHECK(request_resources(&S, 1, r1).granted == 1);
    ReqOutcome o = request_resources(&S, 0, r0);
    CHECK(o.granted == 0);
    CHECK(strstr(o.reason, "unsafe") != NULL);
    return 1;
}

static int test_request_over_need(void)
{
    CHECK(load(SC_TEXTBOOK));
    int r[3] = { 5, 0, 0 };
    ReqOutcome o = request_resources(&S, 1, r);
    CHECK(o.granted == 0);
    CHECK(strstr(o.reason, "maximum") != NULL);
    return 1;
}

static int test_state_unchanged_after_denial(void)
{
    CHECK(load(SC_TEXTBOOK));
    SystemState before;
    state_copy(&before, &S);
    int wait_req[3] = { 3, 3, 0 }, unsafe_req[3] = { 0, 2, 0 }, ok_req[3] = { 1, 0, 2 };
    CHECK(request_resources(&S, 4, wait_req).granted == 0);      /* more than free   */
    CHECK(memcmp(before.available, S.available, sizeof S.available) == 0);
    CHECK(request_resources(&S, 1, ok_req).granted == 1);
    state_copy(&before, &S);
    CHECK(request_resources(&S, 0, unsafe_req).granted == 0);    /* trial was unsafe */
    CHECK(memcmp(before.available, S.available, sizeof S.available) == 0);
    CHECK(memcmp(before.allocation, S.allocation, sizeof(S.allocation[0]) * (size_t)S.n) == 0);
    return 1;
}

static int test_circular_wait_detected(void)
{
    int alloc[2][MAX_R] = { { 1, 0 }, { 0, 1 } };
    int req[2][MAX_R]   = { { 0, 1 }, { 1, 0 } };
    int avail[MAX_R]    = { 0, 0 };
    int dead[2];
    CHECK(detect_deadlock(2, 2, alloc, req, avail, dead, NULL) == 2);
    CHECK(dead[0] == 0 && dead[1] == 1);
    return 1;
}

static int test_no_deadlock_when_release_possible(void)
{
    int alloc[2][MAX_R] = { { 1, 0 }, { 0, 1 } };
    int req[2][MAX_R]   = { { 0, 0 }, { 1, 0 } };      /* P0 is not waiting */
    int avail[MAX_R]    = { 0, 0 };
    int dead[2];
    CHECK(detect_deadlock(2, 2, alloc, req, avail, dead, NULL) == 0);
    return 1;
}

static int test_bad_inputs(void)
{
    int v[8], c;
    SystemState tmp;
    Request rq[4];
    CHECK(parse_vector("3 x 2", v, 8, &c, err) == 0);
    CHECK(strstr(err, "not a list of whole numbers") != NULL);
    CHECK(parse_vector("3 3.5 2", v, 8, &c, err) == 0);
    CHECK(build_state("1", "1", "2", &tmp, err) == 0);                 /* alloc > max   */
    CHECK(strstr(err, "Allocation[P0][R0] is greater than Max[P0][R0]") != NULL);
    CHECK(build_state("-1", "1", "0", &tmp, err) == 0);                /* negative      */
    CHECK(build_state("1 1", "1", "0", &tmp, err) == 0);               /* wrong length  */
    CHECK(strstr(err, "exactly 2 values") != NULL);
    CHECK(build_state("1", "1\n1", "0", &tmp, err) == 0);              /* row counts    */
    CHECK(build_state("", "1", "0", &tmp, err) == 0);                  /* nothing there */
    CHECK(build_state("1", "-1", "0", &tmp, err) == 0);
    int c2;
    CHECK(load(SC_TEXTBOOK));
    CHECK(parse_requests("P1 1 2", S.n, S.m, rq, 4, &c2, err) == 0);   /* no colon      */
    CHECK(parse_requests("P9: 0 0 0", S.n, S.m, rq, 4, &c2, err) == 0);/* no such proc  */
    CHECK(strstr(err, "P0 to P4") != NULL);
    CHECK(parse_requests("P1: 1 2", S.n, S.m, rq, 4, &c2, err) == 0);  /* wrong length  */
    CHECK(parse_requests("P1: -1 0 0", S.n, S.m, rq, 4, &c2, err) == 0);
    CHECK(parse_requests("Q1: 1 0 0", S.n, S.m, rq, 4, &c2, err) == 0);
    CHECK(parse_requests("p1: 1,0,2", S.n, S.m, rq, 4, &c2, err) == 1); /* lower case + commas ok */
    CHECK(c2 == 1 && rq[0].pid == 1 && rq[0].vec[2] == 2);
    return 1;
}

static int test_edge_zero_request(void)
{
    CHECK(load(SC_TEXTBOOK));
    int z[3] = { 0, 0, 0 };
    CHECK(request_resources(&S, 0, z).granted == 1);
    return 1;
}

static int test_edge_exact_need_and_single_resource(void)
{
    SystemState one;
    CHECK(build_state("3", "3", "0", &one, err));
    int r[1] = { 3 };
    CHECK(request_resources(&one, 0, r).granted == 1);      /* exactly the remaining Need */
    return 1;
}

static int test_banker_never_deadlocks_detection_can(void)
{
    static Result rb, rd;
    CHECK(load(SC_CIRCULAR));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.ndead == 0);
    CHECK(rd.ndead == 2 && rd.dead[0] == 0 && rd.dead[1] == 1);
    CHECK(load(SC_DEADLOCK));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.ndead == 0 && rb.completed == 1);
    CHECK(rd.ndead == 2 && rd.completed == 0);
    return 1;
}

static int test_light_load_all_complete(void)
{
    static Result rb, rd;
    CHECK(load(SC_LIGHT));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.completed == 3 && rd.completed == 3);
    return 1;
}

static int test_initial_unsafe_state_reported(void)
{
    static Result r;
    static char buf[4096];
    Log lg;
    log_init(&lg, buf, sizeof buf);
    CHECK(load(SC_CIRCULAR));
    run_banker(&S, RQ, NRQ, &lg, &r);
    CHECK(strstr(buf, "Initial state is UNSAFE") != NULL);
    return 1;
}

static int test_detection_ignores_blocked_and_retries(void)
{
    static Result r;
    static char buf[4096];
    Log lg;
    SystemState st;
    Request rq[3];
    int n;
    /* P0 holds the only unit, so P1 blocks; asking again is ignored. P0 is not waiting,
     * so it could still finish and release the unit: detection must report no deadlock. */
    CHECK(build_state("0", "1\n1", "1\n0", &st, err));
    CHECK(parse_requests("P1: 1\nP1: 1", st.n, st.m, rq, 3, &n, err));
    log_init(&lg, buf, sizeof buf);
    run_detection(&st, rq, n, &lg, &r);
    CHECK(strstr(buf, "BLOCKED") != NULL);
    CHECK(strstr(buf, "IGNORED") != NULL);
    CHECK(r.ndead == 0);
    return 1;
}

static int test_struct_copy_is_independent(void)
{
    SystemState a, b;
    CHECK(load(SC_TEXTBOOK));
    state_copy(&a, &S);
    b = S;                                   /* plain assignment also works */
    a.available[0] = 99;
    a.allocation[2][1] = 7;
    CHECK(S.available[0] == 3 && S.allocation[2][1] == 0);
    CHECK(b.available[0] == 3);
    return 1;
}

static int test_scenario_numbers(void)
{
    /* regression test for the numbers printed in the report (Table 5) */
    static Result rb, rd;
    CHECK(load(SC_TEXTBOOK));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.granted == 1 && rb.denied == 2 && rb.completed == 0 && rb.checks == 3 && rb.ops == 19);
    CHECK(rd.granted == 2 && rd.denied == 1 && rd.completed == 0 && rd.checks == 3 && rd.ops == 15);
    CHECK((int)(rb.utilisation * 1000 + 0.5) == 773 && (int)(rd.utilisation * 1000 + 0.5) == 803);
    CHECK(load(SC_LIGHT));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.granted == 3 && rb.ops == 14 && rb.checks == 4 && rd.ops == 0 && rd.checks == 3);
    CHECK(load(SC_DEADLOCK));
    run_banker(&S, RQ, NRQ, NULL, &rb);
    run_detection(&S, RQ, NRQ, NULL, &rd);
    CHECK(rb.granted == 3 && rb.denied == 1 && rb.completed == 1 && rb.checks == 5 && rb.ops == 16);
    CHECK(rd.granted == 2 && rd.denied == 2 && rd.checks == 4 && rd.ops == 8);
    CHECK((int)(rb.utilisation * 1000 + 0.5) == 625 && (int)(rd.utilisation * 1000 + 0.5) == 875);
    return 1;
}

#define RUN(t) do { tests_run++; int ok = t(); printf("%s %s\n", ok ? "PASS" : "FAIL", #t); if (!ok) failures++; } while (0)

int main(void)
{
    RUN(test_textbook_safe);
    RUN(test_unsafe_request_denied);
    RUN(test_request_over_need);
    RUN(test_state_unchanged_after_denial);
    RUN(test_circular_wait_detected);
    RUN(test_no_deadlock_when_release_possible);
    RUN(test_bad_inputs);
    RUN(test_edge_zero_request);
    RUN(test_edge_exact_need_and_single_resource);
    RUN(test_banker_never_deadlocks_detection_can);
    RUN(test_light_load_all_complete);
    RUN(test_initial_unsafe_state_reported);
    RUN(test_detection_ignores_blocked_and_retries);
    RUN(test_struct_copy_is_independent);
    RUN(test_scenario_numbers);
    printf("\n%d tests, %d failed\n", tests_run, failures);
    return failures ? 1 : 0;
}
