/* simulator.c - feeds a request list to an algorithm, logs each step, collects metrics. */
#include "simulator.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "banker.h"
#include "detection.h"

double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

void log_init(Log *lg, char *buf, size_t cap)
{
    lg->buf = buf;
    lg->cap = cap;
    lg->len = 0;
    lg->truncated = 0;
    if (buf && cap) buf[0] = '\0';
}

static void log_printf(Log *lg, const char *fmt, ...)
{
    if (!lg || !lg->buf || lg->cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    size_t room = lg->cap - lg->len;
    int w = vsnprintf(lg->buf + lg->len, room, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    if ((size_t)w + 2 > room) {          /* keep room for the newline and terminator */
        lg->truncated = 1;
        lg->len = lg->cap - 1;
        lg->buf[lg->len] = '\0';
        return;
    }
    lg->len += (size_t)w;
    lg->buf[lg->len++] = '\n';
    lg->buf[lg->len] = '\0';
}

/* A process whose allocation equals its Max is assumed to finish and release everything. */
static void release_finished(SystemState *s, unsigned char *done, int *ndone, Log *lg)
{
    for (int i = 0; i < s->n; i++) {
        if (done[i]) continue;
        int full = 1, any_max = 0;
        for (int j = 0; j < s->m; j++) {
            if (s->allocation[i][j] != s->maximum[i][j]) full = 0;
            if (s->maximum[i][j]) any_max = 1;
        }
        if (!full || !any_max) continue;
        for (int j = 0; j < s->m; j++) {
            s->available[j] += s->allocation[i][j];
            s->allocation[i][j] = 0;
        }
        done[i] = 1;
        (*ndone)++;
        if (lg && lg->buf) {
            char v[96];
            fmt_vec(v, sizeof v, s->available, s->m);
            log_printf(lg, "  P%d finished and released its resources -> Available = %s", i, v);
        }
    }
}

static double utilisation(const SystemState *s, long total)
{
    if (total == 0) return 0.0;
    long used = 0;
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            used += s->allocation[i][j];
    return (double)used / (double)total;
}

static long sum_total(const SystemState *s)
{
    long t[MAX_R], sum = 0;
    state_total(s, t);
    for (int j = 0; j < s->m; j++) sum += t[j];
    return sum;
}

void run_banker(const SystemState *s0, const Request *reqs, int nreq, Log *lg, Result *res)
{
    SystemState state;
    unsigned char done[MAX_P];
    int ndone = 0, seq[MAX_P], seq_len = 0;
    double util_sum = 0.0;
    char vec[96];

    memset(res, 0, sizeof *res);
    memset(done, 0, sizeof done);
    state_copy(&state, s0);
    long total = sum_total(&state);
    res->algorithm = "Banker's (avoidance)";

    double t0 = now_us();
    int safe0 = is_safe(&state, seq, &seq_len, &res->ops);
    res->checks++;
    if (safe0) {
        char line[1024];
        size_t k = (size_t)snprintf(line, sizeof line, "Initial state is SAFE, sequence ");
        for (int i = 0; i < seq_len && k < sizeof line - 16; i++)
            k += (size_t)snprintf(line + k, sizeof line - k, "%sP%d", i ? " -> " : "", seq[i]);
        log_printf(lg, "%s", line);
    } else {
        log_printf(lg, "Initial state is UNSAFE");
    }

    for (int step = 1; step <= nreq; step++) {
        const Request *r = &reqs[step - 1];
        ReqOutcome o = request_resources(&state, r->pid, r->vec);
        res->ops += o.ops;
        res->checks += o.safety_ran;
        if (o.granted) res->granted++; else res->denied++;
        if (lg && lg->buf) {
            fmt_vec(vec, sizeof vec, r->vec, state.m);
            log_printf(lg, "Step %d: P%d requests %s -> %s (%s)", step, r->pid, vec,
                       o.granted ? "GRANTED" : "DENIED", o.reason);
        }
        util_sum += utilisation(&state, total);
        if (o.granted) release_finished(&state, done, &ndone, lg);
    }
    res->time_us = now_us() - t0;
    res->completed = ndone;
    res->utilisation = nreq ? util_sum / nreq : 0.0;
}

void run_detection(const SystemState *s0, const Request *reqs, int nreq, Log *lg, Result *res)
{
    SystemState state;
    unsigned char done[MAX_P], is_waiting[MAX_P];
    int wreq[MAX_P][MAX_R];           /* pending request vector of each waiting process */
    int reqm[MAX_P][MAX_R];           /* Request matrix handed to the detection algorithm */
    int wq[MAX_P], nw = 0;            /* waiting processes in the order they blocked */
    int ndone = 0, dead[MAX_P], ndead = 0;
    double util_sum = 0.0, peak = -1.0;
    int steps_counted = 0;
    char vec[96];

    memset(res, 0, sizeof *res);
    memset(done, 0, sizeof done);
    memset(is_waiting, 0, sizeof is_waiting);
    state_copy(&state, s0);
    long total = sum_total(&state);
    int m = state.m;
    res->algorithm = "Detection (allocate, then detect)";

    double t0 = now_us();
    for (int step = 1; step <= nreq; step++) {
        const Request *r = &reqs[step - 1];
        int pid = r->pid;
        if (lg && lg->buf) fmt_vec(vec, sizeof vec, r->vec, m);

        if (is_waiting[pid] || done[pid]) {
            log_printf(lg, "Step %d: P%d requests %s -> IGNORED (already blocked or finished)",
                       step, pid, vec);
            continue;
        }
        int over = 0, fits = 1;
        for (int j = 0; j < m; j++) {
            if (r->vec[j] > need_of(&state, pid, j)) over = 1;
            if (r->vec[j] > state.available[j]) fits = 0;
        }
        if (over) {
            res->denied++;
            log_printf(lg, "Step %d: P%d requests %s -> REJECTED (exceeds declared maximum)",
                       step, pid, vec);
            continue;
        }
        if (fits) {
            for (int j = 0; j < m; j++) {
                state.available[j] -= r->vec[j];
                state.allocation[pid][j] += r->vec[j];
            }
            res->granted++;
            log_printf(lg, "Step %d: P%d requests %s -> GRANTED (no safety look-ahead)",
                       step, pid, vec);
            peak = utilisation(&state, total);
            release_finished(&state, done, &ndone, lg);
        } else {
            memcpy(wreq[pid], r->vec, sizeof(int) * (size_t)m);
            is_waiting[pid] = 1;
            wq[nw++] = pid;
            res->denied++;
            log_printf(lg, "Step %d: P%d requests %s -> BLOCKED (resources not free)",
                       step, pid, vec);
        }

        /* retry blocked processes after any release */
        int snapshot[MAX_P], ns = nw;
        memcpy(snapshot, wq, sizeof(int) * (size_t)nw);
        for (int k = 0; k < ns; k++) {
            int w = snapshot[k], ok = 1;
            for (int j = 0; j < m; j++)
                if (wreq[w][j] > state.available[j]) { ok = 0; break; }
            if (!ok) continue;
            for (int j = 0; j < m; j++) {
                state.available[j] -= wreq[w][j];
                state.allocation[w][j] += wreq[w][j];
            }
            if (lg && lg->buf) {
                fmt_vec(vec, sizeof vec, wreq[w], m);
                log_printf(lg, "  blocked P%d can now proceed with %s", w, vec);
            }
            is_waiting[w] = 0;
            for (int q = 0; q < nw; q++)          /* remove w from the waiting list */
                if (wq[q] == w) { memmove(&wq[q], &wq[q + 1], sizeof(int) * (size_t)(nw - q - 1)); nw--; break; }
            res->granted++;
            release_finished(&state, done, &ndone, lg);
        }

        for (int i = 0; i < state.n; i++) {
            if (is_waiting[i]) memcpy(reqm[i], wreq[i], sizeof(int) * (size_t)m);
            else memset(reqm[i], 0, sizeof(int) * (size_t)m);
        }
        ndead = detect_deadlock(state.n, m, (const int (*)[MAX_R])state.allocation,
                                (const int (*)[MAX_R])reqm, state.available, dead, &res->ops);
        res->checks++;
        if (ndead && lg && lg->buf) {
            char line[1024];
            size_t k = (size_t)snprintf(line, sizeof line, "  DETECTION: deadlock among ");
            for (int i = 0; i < ndead && k < sizeof line - 16; i++)
                k += (size_t)snprintf(line + k, sizeof line - k, "%sP%d", i ? ", " : "", dead[i]);
            log_printf(lg, "%s", line);
        }
        util_sum += (peak >= 0.0) ? peak : utilisation(&state, total);
        steps_counted++;
        peak = -1.0;
    }
    res->time_us = now_us() - t0;
    if (!ndead) log_printf(lg, "No deadlock detected at the end of the run.");
    res->completed = ndone;
    res->ndead = ndead;
    memcpy(res->dead, dead, sizeof(int) * (size_t)ndead);
    res->utilisation = steps_counted ? util_sum / steps_counted : 0.0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

double bench_median_us(Algo a, const SystemState *s0, const Request *reqs, int nreq, int reps)
{
    static Result r;
    Log none;
    double *t = malloc(sizeof(double) * (size_t)reps);
    if (!t) return 0.0;
    log_init(&none, NULL, 0);
    for (int k = 0; k < reps; k++) {
        if (a == ALGO_BANKER) run_banker(s0, reqs, nreq, &none, &r);
        else run_detection(s0, reqs, nreq, &none, &r);
        t[k] = r.time_us;
    }
    qsort(t, (size_t)reps, sizeof(double), cmp_double);
    double med = t[reps / 2];
    free(t);
    return med;
}
