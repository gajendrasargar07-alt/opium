/* gui_x11.c - Xlib front end for the Resource Allocation Simulator.
 *
 * Xlib has no ready-made widgets, so the text boxes, radio buttons, drop-down,
 * scrolling log, comparison table and error dialog are all drawn and handled here.
 * Everything is painted into an off-screen pixmap and copied to the window, so
 * there is no flicker.  The algorithms are called through simulator.h only.
 *
 * Build:  make deadlock_gui        (needs libx11-dev)       Run: ./deadlock_gui [sample 1-4]
 */
#define _GNU_SOURCE
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "models.h"
#include "scenarios.h"
#include "simulator.h"

/* ------------------------------------------------------------------ layout */
#define WIN_W 1120
#define WIN_H 640
#define HDR_H 36
#define LX 12
#define LW 300
#define MX 324
#define MW 420
#define RX 756
#define RW 352
#define LOG_Y 204
#define LOG_H 402
#define SB_W 12                     /* log scroll-bar width */
#define TIMING_RUNS 101             /* silent runs used for the median run time */
#define FIELD_MAX 4096
#define LOGCAP (1 << 18)
#define MAX_DL 20000

typedef struct { int x, y, w, h; } Rect;

enum { F_AVAIL, F_MAX, F_ALLOC, F_REQS, N_FIELDS };
enum { A_BANKER, A_DETECT, A_BOTH };
enum { B_NONE, B_RUN, B_RESET, B_DROP, B_OK };

typedef struct {
    char buf[FIELD_MAX];
    int len, cur, scroll, multi, sel_all;
    Rect r;
    const char *label;
    int label_y;
} Field;

/* ------------------------------------------------------------------- state */
static Display *dpy;
static int scr;
static Window win;
static Pixmap pix;
static GC gc;
static XFontStruct *font, *font_big;
static int cw, lh, asc;                 /* char width, line height, ascent */
static Atom a_clip, a_prop, a_utf8, a_delete, a_proto;

static unsigned long C_BG, C_PANEL, C_BORDER, C_NAVY, C_TEXT, C_GREEN, C_RED, C_AMBER, C_BLUE,
    C_GRAY, C_WHITE, C_BTN, C_BTN_DN, C_FOCUS, C_ROW, C_TRACK, C_THUMB, C_SEL;

static Field F[N_FIELDS];
static int focus = F_AVAIL;
static int algo = A_BOTH;
static int sample_idx = 2;
static int dd_open = 0, dd_hover = -1;
static int pressed = B_NONE;

static Result rb, rd;
static int have_b = 0, have_d = 0;
static char logb[LOGCAP], logd[LOGCAP], disp[2 * LOGCAP + 512];

typedef struct { int off, len, indent; unsigned long color; int bold; } DLine;
static DLine dl[MAX_DL];
static int ndl = 0, log_scroll = 0, sb_drag = 0, sb_drag_off = 0;

static char status[ERR_LEN + 32] = "Ready.";
static unsigned long status_color;
static int dialog_open = 0;
static char dialog_msg[ERR_LEN + 8];

static const Rect R_DROP  = { LX + 120, 42, LW - 120, 24 };
static const Rect R_RUN   = { MX, 142, 120, 30 };
static const Rect R_RESET = { MX + 132, 142, 90, 30 };
static const Rect R_LOG   = { MX, LOG_Y, MW, LOG_H };
static const Rect R_DLG   = { (WIN_W - 460) / 2, (WIN_H - 160) / 2, 460, 160 };
static const Rect R_OK    = { (WIN_W - 460) / 2 + 190, (WIN_H - 160) / 2 + 160 - 44, 80, 28 };

/* -------------------------------------------------------------- primitives */
static int inside(const Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static unsigned long rgb(int r, int g, int b)
{
    XColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(dpy, DefaultColormap(dpy, scr), &c);
    return c.pixel;
}

static XFontStruct *load_first(const char **names)
{
    for (; *names; names++) {
        XFontStruct *f = XLoadQueryFont(dpy, *names);
        if (f) return f;
    }
    return NULL;
}

static void fill(Rect r, unsigned long c)
{
    XSetForeground(dpy, gc, c);
    XFillRectangle(dpy, pix, gc, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
}

static void frame(Rect r, unsigned long c)
{
    XSetForeground(dpy, gc, c);
    XDrawRectangle(dpy, pix, gc, r.x, r.y, (unsigned)r.w - 1, (unsigned)r.h - 1);
}

static void clip_to(Rect r)
{
    XRectangle xr = { (short)r.x, (short)r.y, (unsigned short)r.w, (unsigned short)r.h };
    XSetClipRectangles(dpy, gc, 0, 0, &xr, 1, Unsorted);
}

static void unclip(void) { XSetClipMask(dpy, gc, None); }

static void text(int x, int y, const char *s, int n, unsigned long c, int bold)
{
    XSetForeground(dpy, gc, c);
    XDrawString(dpy, pix, gc, x, y, s, n);
    if (bold) XDrawString(dpy, pix, gc, x + 1, y, s, n);   /* fake bold: works with any font */
}

static void textz(int x, int y, const char *s, unsigned long c, int bold)
{
    text(x, y, s, (int)strlen(s), c, bold);
}

/* Draws s inside a box, wrapping at maxcols characters. Returns lines used. */
static int draw_wrapped(int x, int y, int maxcols, int maxlines, const char *s, unsigned long c)
{
    int len = (int)strlen(s), pos = 0, lines = 0;
    while (pos < len && lines < maxlines) {
        int n = len - pos;
        if (n > maxcols) {
            int cut = maxcols;
            while (cut > maxcols / 2 && s[pos + cut] != ' ') cut--;
            n = (cut > maxcols / 2) ? cut : maxcols;
        }
        text(x, y + lines * lh, s + pos, n, c, 0);
        pos += n;
        while (pos < len && s[pos] == ' ') pos++;
        lines++;
    }
    return lines;
}

/* ------------------------------------------------------------ text editing */
static int vis_lines(const Field *f) { return (f->r.h - 8) / lh; }

static void cur_linecol(const Field *f, int *line, int *col)
{
    int l = 0, c = 0;
    for (int i = 0; i < f->cur; i++) {
        if (f->buf[i] == '\n') { l++; c = 0; } else c++;
    }
    *line = l; *col = c;
}

static int offset_of(const Field *f, int line, int col)
{
    int l = 0, i = 0;
    while (l < line && i < f->len) {
        if (f->buf[i] == '\n') l++;
        i++;
    }
    int c = 0;
    while (i < f->len && f->buf[i] != '\n' && c < col) { i++; c++; }
    return i;
}

static int count_lines(const Field *f)
{
    int l = 1;
    for (int i = 0; i < f->len; i++) if (f->buf[i] == '\n') l++;
    return l;
}

static void keep_cursor_visible(Field *f)
{
    int line, col, vis = vis_lines(f);
    cur_linecol(f, &line, &col);
    if (line < f->scroll) f->scroll = line;
    if (line >= f->scroll + vis) f->scroll = line - vis + 1;
}

static void field_clear(Field *f)
{
    f->len = f->cur = f->scroll = 0;
    f->buf[0] = '\0';
    f->sel_all = 0;
}

static void field_insert(Field *f, const char *s, int n)
{
    if (f->sel_all && n > 0) field_clear(f);       /* typing or pasting replaces a select-all */
    for (int k = 0; k < n; k++) {
        char ch = s[k];
        if (ch == '\r') continue;
        if (ch == '\t') ch = ' ';
        if (ch == '\n' && !f->multi) ch = ' ';
        if (ch != '\n' && (ch < 32 || ch > 126)) continue;
        if (f->len >= FIELD_MAX - 1) break;
        memmove(f->buf + f->cur + 1, f->buf + f->cur, (size_t)(f->len - f->cur));
        f->buf[f->cur++] = ch;
        f->len++;
    }
    f->buf[f->len] = '\0';
    keep_cursor_visible(f);
}

static void field_set(Field *f, const char *s)
{
    field_clear(f);
    field_insert(f, s, (int)strlen(s));
    f->cur = 0;
    f->scroll = 0;
}

static void field_backspace(Field *f)
{
    if (f->sel_all) { field_clear(f); return; }
    if (f->cur == 0) return;
    memmove(f->buf + f->cur - 1, f->buf + f->cur, (size_t)(f->len - f->cur + 1));
    f->cur--; f->len--;
    keep_cursor_visible(f);
}

static void field_delete(Field *f)
{
    if (f->sel_all) { field_clear(f); return; }
    if (f->cur >= f->len) return;
    memmove(f->buf + f->cur, f->buf + f->cur + 1, (size_t)(f->len - f->cur));
    f->len--;
}

static void field_move(Field *f, KeySym k)
{
    int line, col;
    f->sel_all = 0;
    cur_linecol(f, &line, &col);
    switch (k) {
    case XK_Left:  if (f->cur > 0) f->cur--; break;
    case XK_Right: if (f->cur < f->len) f->cur++; break;
    case XK_Up:    if (line > 0) f->cur = offset_of(f, line - 1, col); break;
    case XK_Down:  if (line + 1 < count_lines(f)) f->cur = offset_of(f, line + 1, col); break;
    case XK_Home:  f->cur = offset_of(f, line, 0); break;
    case XK_End:   f->cur = offset_of(f, line, 100000); break;
    default: break;
    }
    keep_cursor_visible(f);
}

/* --------------------------------------------------------------- log lines */
static int has(const char *s, int len, const char *needle)
{
    return memmem(s, (size_t)len, needle, strlen(needle)) != NULL;
}

static void classify(const char *s, int len, unsigned long *color, int *bold)
{
    *bold = 0;
    if (len >= 2 && s[0] == '=' && s[1] == '=')   { *color = C_NAVY;  *bold = 1; }
    else if (has(s, len, "DETECTION: deadlock"))  { *color = C_RED;   *bold = 1; }
    else if (has(s, len, "UNSAFE") && has(s, len, "Initial")) { *color = C_RED; }
    else if (has(s, len, "DENIED") || has(s, len, "BLOCKED") || has(s, len, "REJECTED")) *color = C_RED;
    else if (has(s, len, "IGNORED"))              *color = C_AMBER;
    else if (has(s, len, "GRANTED") || has(s, len, "can now proceed") ||
             has(s, len, "Initial state is SAFE") || has(s, len, "No deadlock")) *color = C_GREEN;
    else if (has(s, len, "finished and released")) *color = C_BLUE;
    else                                           *color = C_TEXT;
}

static int log_visible(void) { return (R_LOG.h - 8) / lh; }
static int log_max_scroll(void) { int v = ndl - log_visible(); return v > 0 ? v : 0; }

static void clamp_log_scroll(void)
{
    if (log_scroll > log_max_scroll()) log_scroll = log_max_scroll();
    if (log_scroll < 0) log_scroll = 0;
}

static void wrap_log(void)
{
    int cols = (R_LOG.w - 12 - SB_W) / cw;
    int pos = 0, total = (int)strlen(disp);
    ndl = 0;
    while (pos <= total && ndl < MAX_DL) {
        const char *nl = memchr(disp + pos, '\n', (size_t)(total - pos));
        int len = nl ? (int)(nl - (disp + pos)) : total - pos;
        unsigned long color; int bold;
        classify(disp + pos, len, &color, &bold);
        int off = pos, indent = 0, first = 1;
        do {
            int room = cols - indent, n = len;
            if (n > room) {
                int cut = room;
                while (cut > room / 2 && disp[off + cut] != ' ') cut--;
                n = (cut > room / 2) ? cut : room;
            }
            if (ndl >= MAX_DL) break;
            dl[ndl++] = (DLine){ off, n, indent, color, bold };
            off += n; len -= n;
            while (len > 0 && disp[off] == ' ') { off++; len--; }
            indent = 4; first = 0;
        } while (len > 0);
        (void)first;
        if (!nl) break;
        pos = (int)(nl - disp) + 1;
    }
    clamp_log_scroll();
}

static void compose_log(void)
{
    disp[0] = '\0';
    if (have_b) {
        strcat(disp, "== Banker's (avoidance) ==\n");
        strcat(disp, logb);
    }
    if (have_b && have_d) strcat(disp, "\n");
    if (have_d) {
        strcat(disp, "== Detection (allocate, then detect) ==\n");
        strcat(disp, logd);
    }
    wrap_log();
}

/* ------------------------------------------------------------------ actions */
static void set_status(const char *msg, unsigned long color)
{
    snprintf(status, sizeof status, "%s", msg);
    status_color = color;
}

static void show_error(const char *msg)
{
    snprintf(dialog_msg, sizeof dialog_msg, "%s", msg);
    dialog_open = 1;
    dd_open = 0;
    char line[ERR_LEN + 32];
    snprintf(line, sizeof line, "Input error: %s", msg);
    set_status(line, C_RED);
}

static void clear_results(void)
{
    have_b = have_d = 0;
    disp[0] = '\0';
    ndl = 0;
    log_scroll = 0;
}

static void load_sample(int idx)
{
    sample_idx = idx;
    field_set(&F[F_AVAIL], SCENARIOS[idx].avail);
    field_set(&F[F_MAX], SCENARIOS[idx].max);
    field_set(&F[F_ALLOC], SCENARIOS[idx].alloc);
    field_set(&F[F_REQS], SCENARIOS[idx].reqs);
    clear_results();
    char msg[96];
    snprintf(msg, sizeof msg, "Sample loaded: %s.", SCENARIOS[idx].name);
    set_status(msg, C_TEXT);
}

static void do_run(void)
{
    static SystemState st;
    static Request reqs[MAX_REQ];
    int nreq = 0;
    char err[ERR_LEN];
    Log lg;

    if (!build_state(F[F_AVAIL].buf, F[F_MAX].buf, F[F_ALLOC].buf, &st, err) ||
        !parse_requests(F[F_REQS].buf, st.n, st.m, reqs, MAX_REQ, &nreq, err)) {
        show_error(err);
        return;
    }
    if (nreq == 0) {
        show_error("The request queue is empty. Type at least one request such as  P0: 1 0 2");
        return;
    }
    clear_results();
    if (algo == A_BANKER || algo == A_BOTH) {
        log_init(&lg, logb, sizeof logb);
        run_banker(&st, reqs, nreq, &lg, &rb);
        rb.time_us = bench_median_us(ALGO_BANKER, &st, reqs, nreq, TIMING_RUNS);
        if (lg.truncated) strcat(logb, "... log truncated ...\n");
        have_b = 1;
    }
    if (algo == A_DETECT || algo == A_BOTH) {
        log_init(&lg, logd, sizeof logd);
        run_detection(&st, reqs, nreq, &lg, &rd);
        rd.time_us = bench_median_us(ALGO_DETECTION, &st, reqs, nreq, TIMING_RUNS);
        if (lg.truncated) strcat(logd, "... log truncated ...\n");
        have_d = 1;
    }
    compose_log();
    char msg[96];
    snprintf(msg, sizeof msg, "Simulation finished: %d process%s, %d resource type%s, %d request%s.",
             st.n, st.n == 1 ? "" : "es", st.m, st.m == 1 ? "" : "s", nreq, nreq == 1 ? "" : "s");
    set_status(msg, C_GREEN);
}

/* ------------------------------------------------------------------- paint */
static void draw_button(Rect r, const char *label, int down)
{
    fill(r, down ? C_BTN_DN : C_BTN);
    frame(r, C_BORDER);
    int tw = (int)strlen(label) * cw;
    textz(r.x + (r.w - tw) / 2, r.y + (r.h + asc) / 2 - 2, label, C_TEXT, 1);
}

static void draw_field(int idx)
{
    Field *f = &F[idx];
    fill(f->r, C_PANEL);
    frame(f->r, idx == focus ? C_FOCUS : C_BORDER);
    if (idx == focus) {
        Rect in = { f->r.x + 1, f->r.y + 1, f->r.w - 2, f->r.h - 2 };
        frame(in, C_FOCUS);
    }
    textz(f->r.x, f->label_y, f->label, C_TEXT, 1);

    Rect in = { f->r.x + 2, f->r.y + 2, f->r.w - 4, f->r.h - 4 };
    clip_to(in);
    int vis = vis_lines(f), line = 0, start = 0;
    for (int i = 0; i <= f->len; i++) {
        if (i == f->len || f->buf[i] == '\n') {
            if (f->sel_all && line >= f->scroll && line < f->scroll + vis) {
                Rect hl = { f->r.x + 4, f->r.y + 4 + (line - f->scroll) * lh,
                            (i - start) * cw + 2, lh - 1 };
                fill(hl, C_SEL);
            }
            if (line >= f->scroll && line < f->scroll + vis)
                text(f->r.x + 5, f->r.y + 4 + asc + (line - f->scroll) * lh,
                     f->buf + start, i - start, C_TEXT, 0);
            line++;
            start = i + 1;
        }
    }
    if (idx == focus) {
        int cl, cc;
        cur_linecol(f, &cl, &cc);
        XSetForeground(dpy, gc, C_NAVY);
        int x = f->r.x + 4 + cc * cw, y = f->r.y + 4 + (cl - f->scroll) * lh;
        XFillRectangle(dpy, pix, gc, x, y, 2, (unsigned)lh - 1);
    }
    unclip();
    int total = count_lines(f);
    if (total > vis) {                       /* small scroll hint */
        char hint[24];
        snprintf(hint, sizeof hint, "%d/%d", f->scroll + 1, total - vis + 1);
        textz(f->r.x + f->r.w - (int)strlen(hint) * cw - 4, f->label_y, hint, C_GRAY, 0);
    }
}

static void draw_radio(int x, int y, const char *label, int on)
{
    XSetForeground(dpy, gc, C_PANEL);
    XFillArc(dpy, pix, gc, x, y, 14, 14, 0, 360 * 64);
    XSetForeground(dpy, gc, C_BORDER);
    XDrawArc(dpy, pix, gc, x, y, 14, 14, 0, 360 * 64);
    if (on) {
        XSetForeground(dpy, gc, C_NAVY);
        XFillArc(dpy, pix, gc, x + 3, y + 3, 8, 8, 0, 360 * 64);
    }
    textz(x + 22, y + 12, label, C_TEXT, 0);
}

static void fmt_dead(const Result *r, char *out, size_t cap, int maxc)
{
    if (!r->ndead) { snprintf(out, cap, "none"); return; }
    char tmp[512];
    size_t k = 0;
    for (int i = 0; i < r->ndead && k < sizeof tmp - 12; i++)
        k += (size_t)snprintf(tmp + k, sizeof tmp - k, "%sP%d", i ? ", " : "", r->dead[i]);
    if ((int)k > maxc) { tmp[maxc - 2] = '\0'; strcat(tmp, ".."); }
    snprintf(out, cap, "%s", tmp);
}

static void draw_table(void)
{
    static const char *names[8] = {
        "Requests granted", "Denied / blocked", "Processes completed", "Deadlocked processes",
        "Safety/detection checks", "Vector comparisons", "Avg. utilisation", "Run time (us, median)" };
    int cx[4] = { RX, RX + 170, RX + 262, RX + RW };
    int rowh = 24, y0 = 142;
    Rect hdr = { RX, y0, RW, rowh };
    fill(hdr, C_NAVY);
    textz(cx[0] + 6, y0 + 16, "Metric", C_WHITE, 1);
    textz(cx[1] + 6, y0 + 16, "Banker's", C_WHITE, 1);
    textz(cx[2] + 6, y0 + 16, "Detection", C_WHITE, 1);

    for (int r = 0; r < 8; r++) {
        Rect row = { RX, y0 + rowh * (r + 1), RW, rowh };
        fill(row, (r & 1) ? C_ROW : C_PANEL);
        textz(cx[0] + 6, row.y + 16, names[r], C_TEXT, 0);
        for (int side = 0; side < 2; side++) {
            const Result *res = side ? &rd : &rb;
            int have = side ? have_d : have_b;
            char cell[48] = "-";
            unsigned long color = C_GRAY;
            if (have) {
                color = C_TEXT;
                switch (r) {
                case 0: snprintf(cell, sizeof cell, "%d", res->granted); break;
                case 1: snprintf(cell, sizeof cell, "%d", res->denied); break;
                case 2: snprintf(cell, sizeof cell, "%d", res->completed); break;
                case 3: fmt_dead(res, cell, sizeof cell, (cx[side + 2] - cx[side + 1] - 12) / cw);
                        if (res->ndead) color = C_RED;
                        break;
                case 4: snprintf(cell, sizeof cell, "%ld", res->checks); break;
                case 5: snprintf(cell, sizeof cell, "%ld", res->ops); break;
                case 6: snprintf(cell, sizeof cell, "%.1f%%", res->utilisation * 100.0); break;
                case 7: snprintf(cell, sizeof cell, "%.2f", res->time_us); break;
                }
            }
            textz(cx[side + 1] + 6, row.y + 16, cell, color, r == 3 && have && res->ndead);
        }
    }
    Rect all = { RX, y0, RW, rowh * 9 };
    frame(all, C_BORDER);
    XSetForeground(dpy, gc, C_BORDER);
    XDrawLine(dpy, pix, gc, cx[1], y0, cx[1], y0 + rowh * 9);
    XDrawLine(dpy, pix, gc, cx[2], y0, cx[2], y0 + rowh * 9);
}

static void draw_bars(void)
{
    textz(RX, 384, "Average resource utilisation", C_TEXT, 1);
    int maxw = RW - 100 - 56;
    for (int side = 0; side < 2; side++) {
        int have = side ? have_d : have_b;
        const Result *res = side ? &rd : &rb;
        int y = 394 + side * 28;
        textz(RX, y + 14, side ? "Detection" : "Banker's", C_TEXT, 0);
        Rect track = { RX + 100, y, maxw, 18 };
        fill(track, C_PANEL);
        frame(track, C_BORDER);
        if (have) {
            Rect bar = { RX + 100 + 1, y + 1, (int)((maxw - 2) * res->utilisation + 0.5), 16 };
            fill(bar, side ? C_AMBER : C_BLUE);
            char p[16];
            snprintf(p, sizeof p, "%.1f%%", res->utilisation * 100.0);
            textz(RX + 100 + maxw + 6, y + 14, p, C_TEXT, 0);
        }
    }
}

static void draw_verdicts(void)
{
    textz(RX, 470, "Deadlock check", C_TEXT, 1);
    for (int side = 0; side < 2; side++) {
        int have = side ? have_d : have_b;
        const Result *res = side ? &rd : &rb;
        Rect box = { RX, 480 + side * 34, RW, 28 };
        char msg[200];
        unsigned long c = C_GRAY;
        if (!have) snprintf(msg, sizeof msg, "%s: not run", side ? "Detection" : "Banker's");
        else if (!res->ndead) {
            snprintf(msg, sizeof msg, "%s: no deadlock %s", side ? "Detection" : "Banker's",
                     side ? "found" : "(avoided)");
            c = C_GREEN;
        } else {
            char d[64];
            fmt_dead(res, d, sizeof d, 24);
            snprintf(msg, sizeof msg, "Detection: DEADLOCK among %s", d);
            c = C_RED;
        }
        fill(box, C_PANEL);
        frame(box, c);
        Rect stripe = { box.x, box.y, 6, box.h };
        fill(stripe, c);
        textz(box.x + 14, box.y + 18, msg, c, have);
    }
}

static void draw_log(void)
{
    textz(MX, 196, "Execution log", C_TEXT, 1);
    fill(R_LOG, C_PANEL);
    frame(R_LOG, C_BORDER);
    Rect in = { R_LOG.x + 2, R_LOG.y + 2, R_LOG.w - 4 - SB_W, R_LOG.h - 4 };
    clip_to(in);
    int vis = log_visible();
    if (ndl == 0)
        textz(R_LOG.x + 8, R_LOG.y + 4 + asc, "Press Run simulation to see each step here.", C_GRAY, 0);
    for (int k = 0; k < vis && log_scroll + k < ndl; k++) {
        const DLine *d = &dl[log_scroll + k];
        text(R_LOG.x + 8 + d->indent * cw, R_LOG.y + 4 + asc + k * lh, disp + d->off, d->len,
             d->color, d->bold);
    }
    unclip();
    Rect track = { R_LOG.x + R_LOG.w - SB_W - 1, R_LOG.y + 1, SB_W, R_LOG.h - 2 };
    fill(track, C_TRACK);
    if (ndl > vis) {
        int th = track.h * vis / ndl;
        if (th < 24) th = 24;
        int ty = track.y + (track.h - th) * log_scroll / log_max_scroll();
        Rect thumb = { track.x + 1, ty, track.w - 2, th };
        fill(thumb, C_THUMB);
    }
}

static void thumb_geometry(int *ty, int *th)
{
    int trk = R_LOG.h - 2, vis = log_visible();
    *th = ndl > vis ? trk * vis / ndl : trk;
    if (*th < 24) *th = 24;
    *ty = R_LOG.y + 1 + (log_max_scroll() ? (trk - *th) * log_scroll / log_max_scroll() : 0);
}

static void draw_dropdown_list(void)
{
    if (!dd_open) return;
    int ih = 24;
    Rect list = { R_DROP.x, R_DROP.y + R_DROP.h, R_DROP.w, ih * N_SCENARIOS };
    fill(list, C_PANEL);
    for (int i = 0; i < N_SCENARIOS; i++) {
        Rect it = { list.x, list.y + i * ih, list.w, ih };
        if (i == dd_hover) fill(it, C_NAVY);
        textz(it.x + 8, it.y + 16, SCENARIOS[i].name, i == dd_hover ? C_WHITE : C_TEXT, i == sample_idx);
    }
    frame(list, C_BORDER);
}

static void draw_dialog(void)
{
    if (!dialog_open) return;
    Rect shadow = { R_DLG.x + 5, R_DLG.y + 5, R_DLG.w, R_DLG.h };
    fill(shadow, C_GRAY);
    fill(R_DLG, C_PANEL);
    Rect bar = { R_DLG.x, R_DLG.y, R_DLG.w, 28 };
    fill(bar, C_RED);
    textz(bar.x + 12, bar.y + 19, "Input error", C_WHITE, 1);
    frame(R_DLG, C_RED);
    draw_wrapped(R_DLG.x + 16, R_DLG.y + 54, (R_DLG.w - 32) / cw, 4, dialog_msg, C_TEXT);
    draw_button(R_OK, "OK", pressed == B_OK);
}

static void paint(void)
{
    Rect all = { 0, 0, WIN_W, WIN_H };
    fill(all, C_BG);
    Rect hdr = { 0, 0, WIN_W, HDR_H };
    fill(hdr, C_NAVY);
    XSetFont(dpy, gc, font_big->fid);
    textz(14, 24, "Resource Allocation Simulator - Deadlock", C_WHITE, font_big == font);
    XSetFont(dpy, gc, font->fid);
    {
        const char *tag = "Operating Systems | SLA Stage 2 | C + Xlib";
        textz(WIN_W - 14 - (int)strlen(tag) * cw, 23, tag, 0xc5cae9, 0);
    }

    /* left column */
    textz(LX, 58, "Sample scenario", C_TEXT, 1);
    draw_button(R_DROP, "", dd_open);
    {
        int maxc = (R_DROP.w - 30) / cw;
        char nm[64];
        snprintf(nm, sizeof nm, "%.*s", maxc, SCENARIOS[sample_idx].name);
        textz(R_DROP.x + 8, R_DROP.y + 16, nm, C_TEXT, 0);
        XPoint tri[3] = { { (short)(R_DROP.x + R_DROP.w - 18), (short)(R_DROP.y + 9) },
                          { (short)(R_DROP.x + R_DROP.w - 8), (short)(R_DROP.y + 9) },
                          { (short)(R_DROP.x + R_DROP.w - 13), (short)(R_DROP.y + 16) } };
        XSetForeground(dpy, gc, C_TEXT);
        XFillPolygon(dpy, pix, gc, tri, 3, Convex, CoordModeOrigin);
    }
    for (int i = 0; i < N_FIELDS; i++) draw_field(i);

    /* middle column */
    textz(MX, 58, "Algorithm", C_TEXT, 1);
    draw_radio(MX, 68, "Algorithm 1 - Banker's (avoidance)", algo == A_BANKER);
    draw_radio(MX, 92, "Algorithm 2 - Deadlock detection", algo == A_DETECT);
    draw_radio(MX, 116, "Compare both", algo == A_BOTH);
    draw_button(R_RUN, "Run simulation", pressed == B_RUN);
    draw_button(R_RESET, "Reset", pressed == B_RESET);
    draw_log();

    /* right column */
    textz(RX, 58, "Status / messages", C_TEXT, 1);
    Rect sb = { RX, 66, RW, 46 };
    fill(sb, C_PANEL);
    frame(sb, status_color == C_RED ? C_RED : C_BORDER);
    draw_wrapped(RX + 8, 66 + asc + 6, (RW - 16) / cw, 2, status, status_color);
    textz(RX, 134, "Comparison table", C_TEXT, 1);
    draw_table();
    draw_bars();
    draw_verdicts();

    /* footer */
    Rect foot = { 0, WIN_H - 24, WIN_W, 24 };
    fill(foot, C_NAVY);
    textz(12, WIN_H - 8,
          "Tab: next box   Ctrl+A: select all   Ctrl+V / middle-click: paste   F5: run   "
          "Wheel: scroll log   Esc: close", 0xc5cae9, 0);

    draw_dropdown_list();
    draw_dialog();
    XCopyArea(dpy, pix, win, gc, 0, 0, WIN_W, WIN_H, 0, 0);
    XFlush(dpy);
}

/* ------------------------------------------------------------------- input */
static void request_paste(Atom selection, Atom target)
{
    XConvertSelection(dpy, selection, target, a_prop, win, CurrentTime);
}

static void handle_selection_notify(XSelectionEvent *e)
{
    if (e->property == None) {
        if (e->target == a_utf8) request_paste(e->selection, XA_STRING);   /* try plain text */
        return;
    }
    Atom type; int fmt; unsigned long n, after; unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, win, a_prop, 0, 1 << 16, True, AnyPropertyType, &type, &fmt, &n,
                           &after, &data) == Success && data) {
        if (fmt == 8) field_insert(&F[focus], (const char *)data, (int)n);
        XFree(data);
    }
}

static void log_scroll_by(int d) { log_scroll += d; clamp_log_scroll(); }

static void on_key(XKeyEvent *e)
{
    char buf[32];
    KeySym k = NoSymbol;
    int n = XLookupString(e, buf, sizeof buf, &k, NULL);
    int ctrl = e->state & ControlMask, shift = e->state & ShiftMask;

    if (dialog_open) {
        if (k == XK_Return || k == XK_KP_Enter || k == XK_Escape || k == XK_space) dialog_open = 0;
        return;
    }
    if (k == XK_Escape) { dd_open = 0; return; }
    if (k == XK_F5 || (ctrl && (k == XK_Return || k == XK_KP_Enter))) { do_run(); return; }
    if ((ctrl && (k == XK_v || k == XK_V)) || (shift && k == XK_Insert)) {
        request_paste(a_clip, a_utf8);
        return;
    }
    if (ctrl && (k == XK_a || k == XK_A)) { F[focus].sel_all = 1; return; }
    if (k == XK_Page_Up)   { log_scroll_by(-log_visible()); return; }
    if (k == XK_Page_Down) { log_scroll_by(log_visible()); return; }
    if (k == XK_Tab || k == XK_ISO_Left_Tab) {
        focus = (shift || k == XK_ISO_Left_Tab) ? (focus + N_FIELDS - 1) % N_FIELDS
                                                : (focus + 1) % N_FIELDS;
        return;
    }
    Field *f = &F[focus];
    switch (k) {
    case XK_BackSpace: field_backspace(f); return;
    case XK_Delete: field_delete(f); return;
    case XK_Left: case XK_Right: case XK_Up: case XK_Down: case XK_Home: case XK_End:
        field_move(f, k); return;
    case XK_Return: case XK_KP_Enter:
        if (f->multi) field_insert(f, "\n", 1); else do_run();
        return;
    default: break;
    }
    if (n > 0 && !ctrl && !(e->state & Mod1Mask))
        field_insert(f, buf, n);
}

static void on_press(XButtonEvent *e)
{
    int x = e->x, y = e->y;

    if (dialog_open) {
        if (e->button == Button1 && inside(&R_OK, x, y)) pressed = B_OK;
        return;
    }
    if (e->button == Button4 || e->button == Button5) {
        int d = e->button == Button4 ? -3 : 3;
        if (inside(&R_LOG, x, y)) log_scroll_by(d);
        for (int i = 0; i < N_FIELDS; i++)
            if (inside(&F[i].r, x, y)) {
                int mx = count_lines(&F[i]) - vis_lines(&F[i]);
                F[i].scroll += d > 0 ? 1 : -1;
                if (F[i].scroll > mx) F[i].scroll = mx;
                if (F[i].scroll < 0) F[i].scroll = 0;
            }
        return;
    }
    if (dd_open) {
        int ih = 24, idx = (y - (R_DROP.y + R_DROP.h)) / ih;
        if (e->button == Button1 && x >= R_DROP.x && x < R_DROP.x + R_DROP.w &&
            y >= R_DROP.y + R_DROP.h && idx >= 0 && idx < N_SCENARIOS)
            load_sample(idx);
        dd_open = 0;
        return;
    }
    if (e->button != Button1 && e->button != Button2) return;

    if (e->button == Button1) {
        if (inside(&R_DROP, x, y)) { pressed = B_DROP; return; }
        if (inside(&R_RUN, x, y)) { pressed = B_RUN; return; }
        if (inside(&R_RESET, x, y)) { pressed = B_RESET; return; }
        if (x >= MX && x < MX + 320) {
            if (y >= 68 && y < 82) algo = A_BANKER;
            else if (y >= 92 && y < 106) algo = A_DETECT;
            else if (y >= 116 && y < 130) algo = A_BOTH;
        }
        /* log scroll-bar */
        if (x >= R_LOG.x + R_LOG.w - SB_W - 1 && x < R_LOG.x + R_LOG.w && inside(&R_LOG, x, y) &&
            ndl > log_visible()) {
            int ty, th;
            thumb_geometry(&ty, &th);
            if (y >= ty && y < ty + th) { sb_drag = 1; sb_drag_off = y - ty; }
            else log_scroll_by(y < ty ? -log_visible() : log_visible());
            return;
        }
    }
    for (int i = 0; i < N_FIELDS; i++) {
        if (!inside(&F[i].r, x, y)) continue;
        focus = i;
        Field *f = &F[i];
        f->sel_all = 0;
        int line = f->scroll + (y - f->r.y - 4) / lh;
        int col = (x - f->r.x - 5 + cw / 2) / cw;
        if (line < 0) line = 0;
        if (col < 0) col = 0;
        if (line >= count_lines(f)) line = count_lines(f) - 1;
        f->cur = offset_of(f, line, col);
        if (e->button == Button2) request_paste(XA_PRIMARY, a_utf8);
        return;
    }
}

static void on_release(XButtonEvent *e)
{
    int was = pressed;
    pressed = B_NONE;
    sb_drag = 0;
    if (e->button != Button1) return;
    switch (was) {
    case B_OK:    if (inside(&R_OK, e->x, e->y)) dialog_open = 0; break;
    case B_RUN:   if (inside(&R_RUN, e->x, e->y)) do_run(); break;
    case B_RESET: if (inside(&R_RESET, e->x, e->y)) load_sample(sample_idx); break;
    case B_DROP:  if (inside(&R_DROP, e->x, e->y)) { dd_open = 1; dd_hover = -1; } break;
    default: break;
    }
}

static void on_motion(XMotionEvent *e)
{
    if (dd_open) {
        int idx = (e->y - (R_DROP.y + R_DROP.h)) / 24;
        dd_hover = (e->x >= R_DROP.x && e->x < R_DROP.x + R_DROP.w &&
                    e->y >= R_DROP.y + R_DROP.h && idx >= 0 && idx < N_SCENARIOS) ? idx : -1;
    }
    if (sb_drag && ndl > log_visible()) {
        int ty, th;
        thumb_geometry(&ty, &th);
        int span = (R_LOG.h - 2) - th;
        if (span > 0) {
            log_scroll = (e->y - R_LOG.y - 1 - sb_drag_off) * log_max_scroll() / span;
            clamp_log_scroll();
        }
    }
}

/* -------------------------------------------------------------------- main */
int main(int argc, char **argv)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open the X display. Run this from a desktop session "
                        "(or set DISPLAY).\n");
        return 1;
    }
    scr = DefaultScreen(dpy);

    static const char *mono[] = { "7x13", "-misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1",
                                  "6x13", "fixed", NULL };
    static const char *big[]  = { "9x15B", "10x20", "9x15", NULL };
    font = load_first(mono);
    if (!font) { fprintf(stderr, "No usable X font found.\n"); return 1; }
    font_big = load_first(big);
    if (!font_big) font_big = font;
    cw = font->max_bounds.width;
    asc = font->ascent;
    lh = font->ascent + font->descent + 2;

    C_BG = rgb(238, 240, 245);  C_PANEL = rgb(255, 255, 255); C_BORDER = rgb(150, 156, 170);
    C_NAVY = rgb(26, 35, 126);  C_TEXT = rgb(28, 28, 32);     C_GREEN = rgb(24, 122, 58);
    C_RED = rgb(190, 36, 36);   C_AMBER = rgb(176, 108, 0);   C_BLUE = rgb(36, 92, 176);
    C_GRAY = rgb(120, 124, 134); C_WHITE = rgb(255, 255, 255); C_BTN = rgb(226, 229, 238);
    C_BTN_DN = rgb(186, 193, 214); C_FOCUS = rgb(40, 90, 200); C_ROW = rgb(240, 242, 248);
    C_TRACK = rgb(226, 228, 236); C_THUMB = rgb(150, 156, 180); C_SEL = rgb(184, 206, 250);
    status_color = C_TEXT;

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, WIN_W, WIN_H, 0, 0, C_BG);
    XStoreName(dpy, win, "Resource Allocation Simulator - Deadlock");
    XSizeHints *sh = XAllocSizeHints();
    sh->flags = PMinSize | PMaxSize;
    sh->min_width = sh->max_width = WIN_W;
    sh->min_height = sh->max_height = WIN_H;
    XSetWMNormalHints(dpy, win, sh);
    XFree(sh);
    a_proto = XInternAtom(dpy, "WM_PROTOCOLS", False);
    a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &a_delete, 1);
    a_clip = XInternAtom(dpy, "CLIPBOARD", False);
    a_prop = XInternAtom(dpy, "SLA_PASTE", False);
    a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                               PointerMotionMask | StructureNotifyMask);
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetFont(dpy, gc, font->fid);
    pix = XCreatePixmap(dpy, win, WIN_W, WIN_H, (unsigned)DefaultDepth(dpy, scr));

    int ys[N_FIELDS] = { 94, 148, 294, 440 };
    int hs[N_FIELDS] = { 26, 118, 118, 118 };
    const char *lab[N_FIELDS] = { "Available (one value per resource type)",
                                  "Max matrix (one row per process)", "Allocation matrix",
                                  "Request queue  (P1: 1 0 2)" };
    for (int i = 0; i < N_FIELDS; i++) {
        F[i].r = (Rect){ LX, ys[i], LW, hs[i] };
        F[i].label = lab[i];
        F[i].label_y = ys[i] - 6;
        F[i].multi = (i != F_AVAIL);
    }
    int start = 2;
    if (argc > 1) {
        int a = atoi(argv[1]);
        if (a >= 1 && a <= N_SCENARIOS) start = a - 1;
    }
    load_sample(start);
    set_status("Ready. Pick a sample or type your own system, then press Run simulation.", C_TEXT);

    XMapWindow(dpy, win);
    for (;;) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        int redraw = 1;
        switch (ev.type) {
        case Expose: if (ev.xexpose.count) redraw = 0; break;
        case KeyPress: on_key(&ev.xkey); break;
        case ButtonPress: on_press(&ev.xbutton); break;
        case ButtonRelease: on_release(&ev.xbutton); break;
        case MotionNotify: on_motion(&ev.xmotion); redraw = dd_open || sb_drag; break;
        case SelectionNotify: handle_selection_notify(&ev.xselection); break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == a_delete) goto quit;
            redraw = 0;
            break;
        default: redraw = 0; break;
        }
        if (redraw) paint();
    }
quit:
    XFreePixmap(dpy, pix);
    XFreeGC(dpy, gc);
    XCloseDisplay(dpy);
    return 0;
}
