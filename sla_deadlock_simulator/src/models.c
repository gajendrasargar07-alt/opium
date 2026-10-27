/* models.c - state copy, text parsing and input validation. */
#include "models.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_CAP 64          /* values read from one line before we give up counting */
#define LINE_CAP 512

#ifdef FULL_COPY            /* only used for the copy-cost measurement in the report */
void state_copy(SystemState *dst, const SystemState *src) { *dst = *src; }
#else
void state_copy(SystemState *dst, const SystemState *src)
{
    dst->n = src->n;
    dst->m = src->m;
    memcpy(dst->available, src->available, sizeof dst->available);
    memcpy(dst->maximum, src->maximum, sizeof(src->maximum[0]) * (size_t)src->n);
    memcpy(dst->allocation, src->allocation, sizeof(src->allocation[0]) * (size_t)src->n);
}
#endif

void state_total(const SystemState *s, long total[MAX_R])
{
    for (int j = 0; j < s->m; j++) {
        total[j] = s->available[j];
        for (int i = 0; i < s->n; i++)
            total[j] += s->allocation[i][j];
    }
}

int fmt_vec(char *buf, size_t cap, const int *v, int m)
{
    size_t len = 0;
    if (cap == 0) return 0;
    buf[0] = '\0';
    len += (size_t)snprintf(buf + len, cap - len, "[");
    for (int j = 0; j < m && len < cap; j++)
        len += (size_t)snprintf(buf + len, cap - len, "%s%d", j ? ", " : "", v[j]);
    if (len < cap) len += (size_t)snprintf(buf + len, cap - len, "]");
    return (int)(len < cap ? len : cap - 1);
}

/* Shortened copy of a user string for use inside an error message. */
static void clip(char *dst, size_t cap, const char *src, size_t len)
{
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len > 40) len = 40;
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Reads whole numbers separated by spaces, commas or newlines from text[0..len). */
static int parse_vec_n(const char *text, size_t len, int *out, int cap, int *count, char *err)
{
    char tmp[LINE_CAP + 1];
    char shown[48];
    int c = 0;

    clip(shown, sizeof shown, text, len);
    if (len > LINE_CAP) {
        snprintf(err, ERR_LEN, "'%s...' is too long to be a list of numbers.", shown);
        return 0;
    }
    memcpy(tmp, text, len);
    tmp[len] = '\0';
    for (size_t k = 0; k < len; k++)
        if (tmp[k] == ',') tmp[k] = ' ';

    char *p = tmp;
    for (;;) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || (*end && !isspace((unsigned char)*end))) {
            snprintf(err, ERR_LEN, "'%s' is not a list of whole numbers.", shown);
            return 0;
        }
        if (v > MAX_VALUE || v < -MAX_VALUE) {
            snprintf(err, ERR_LEN, "'%s' contains a number larger than %d.", shown, MAX_VALUE);
            return 0;
        }
        if (c < cap) out[c] = (int)v;
        c++;
        p = end;
    }
    *count = c;
    return 1;
}

int parse_vector(const char *text, int *out, int cap, int *count, char *err)
{
    return parse_vec_n(text, strlen(text), out, cap, count, err);
}

/* One row per non-blank line. Rows are stored (first MAX_R values) in rows[][],
 * their true lengths in lens[]. */
static int parse_rows(const char *text, int rows[][MAX_R], int lens[], int *nrows, char *err)
{
    int r = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        int tmp[ROW_CAP], cnt = 0;
        int blank = 1;
        for (size_t k = 0; k < len; k++)
            if (!isspace((unsigned char)p[k])) { blank = 0; break; }
        if (!blank) {
            if (r >= MAX_P) {
                snprintf(err, ERR_LEN, "At most %d processes are supported.", MAX_P);
                return 0;
            }
            if (!parse_vec_n(p, len, tmp, ROW_CAP, &cnt, err)) return 0;
            lens[r] = cnt;
            for (int j = 0; j < cnt && j < MAX_R; j++) rows[r][j] = tmp[j];
            r++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    *nrows = r;
    return 1;
}

int build_state(const char *avail_txt, const char *max_txt, const char *alloc_txt,
                SystemState *out, char *err)
{
    int avail[ROW_CAP], acount = 0;
    int lens_max[MAX_P], lens_alloc[MAX_P];
    int nmax = 0, nalloc = 0;

    memset(out, 0, sizeof *out);
    if (!parse_vec_n(avail_txt, strlen(avail_txt), avail, ROW_CAP, &acount, err)) return 0;
    if (!parse_rows(max_txt, out->maximum, lens_max, &nmax, err)) return 0;
    if (!parse_rows(alloc_txt, out->allocation, lens_alloc, &nalloc, err)) return 0;

    if (acount == 0 || nmax == 0) {
        snprintf(err, ERR_LEN, "At least one process and one resource type are required.");
        return 0;
    }
    if (acount > MAX_R) {
        snprintf(err, ERR_LEN, "At most %d resource types are supported.", MAX_R);
        return 0;
    }
    if (nalloc != nmax) {
        snprintf(err, ERR_LEN, "Max and Allocation must have the same number of rows.");
        return 0;
    }
    out->n = nmax;
    out->m = acount;

    for (int j = 0; j < acount; j++) {
        if (avail[j] < 0) {
            snprintf(err, ERR_LEN, "Available must contain non-negative whole numbers.");
            return 0;
        }
        out->available[j] = avail[j];
    }
    for (int i = 0; i < out->n; i++) {
        if (lens_max[i] != out->m) {
            snprintf(err, ERR_LEN, "Max[P%d] must have exactly %d values.", i, out->m);
            return 0;
        }
        for (int j = 0; j < out->m; j++)
            if (out->maximum[i][j] < 0) {
                snprintf(err, ERR_LEN, "Max[P%d] must contain non-negative whole numbers.", i);
                return 0;
            }
    }
    for (int i = 0; i < out->n; i++) {
        if (lens_alloc[i] != out->m) {
            snprintf(err, ERR_LEN, "Allocation[P%d] must have exactly %d values.", i, out->m);
            return 0;
        }
        for (int j = 0; j < out->m; j++)
            if (out->allocation[i][j] < 0) {
                snprintf(err, ERR_LEN, "Allocation[P%d] must contain non-negative whole numbers.", i);
                return 0;
            }
    }
    for (int i = 0; i < out->n; i++)
        for (int j = 0; j < out->m; j++)
            if (out->allocation[i][j] > out->maximum[i][j]) {
                snprintf(err, ERR_LEN, "Allocation[P%d][R%d] is greater than Max[P%d][R%d].",
                         i, j, i, j);
                return 0;
            }
    return 1;
}

int parse_requests(const char *text, int n, int m, Request *out, int cap,
                   int *count, char *err)
{
    int c = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char shown[48];
        clip(shown, sizeof shown, p, len);
        if (shown[0] != '\0') {
            const char *colon = memchr(p, ':', len);
            if (!colon) {
                snprintf(err, ERR_LEN, "Request '%s' must look like  P1: 1 0 2", shown);
                return 0;
            }
            /* process name: optional P/p, then digits */
            const char *q = p;
            while (q < colon && isspace((unsigned char)*q)) q++;
            if (q < colon && (*q == 'P' || *q == 'p')) q++;
            const char *d = q;
            long pid = 0;
            while (d < colon && isdigit((unsigned char)*d) && pid <= MAX_P) {
                pid = pid * 10 + (*d - '0');
                d++;
            }
            const char *after = d;
            while (after < colon && isspace((unsigned char)*after)) after++;
            if (d == q || after != colon) {
                snprintf(err, ERR_LEN, "Bad process name in request '%s'.", shown);
                return 0;
            }
            if (pid >= n) {
                snprintf(err, ERR_LEN,
                         "Request '%s' names a process that does not exist (valid range P0 to P%d).",
                         shown, n - 1);
                return 0;
            }
            int vec[ROW_CAP], cnt = 0;
            if (!parse_vec_n(colon + 1, len - (size_t)(colon + 1 - p), vec, ROW_CAP, &cnt, err))
                return 0;
            if (cnt != m) {
                snprintf(err, ERR_LEN, "Request '%s' needs %d values.", shown, m);
                return 0;
            }
            for (int j = 0; j < m; j++)
                if (vec[j] < 0) {
                    snprintf(err, ERR_LEN, "Request '%s' must contain non-negative whole numbers.",
                             shown);
                    return 0;
                }
            if (c >= cap) {
                snprintf(err, ERR_LEN, "At most %d requests are supported.", cap);
                return 0;
            }
            out[c].pid = (int)pid;
            memset(out[c].vec, 0, sizeof out[c].vec);
            for (int j = 0; j < m; j++) out[c].vec[j] = vec[j];
            c++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    *count = c;
    return 1;
}
