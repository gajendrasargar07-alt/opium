/* xdrive.c - tiny GUI test driver. Reads commands from stdin and sends real X events
 * (button, key, motion) to the simulator window, so the GUI can be tested without a person.
 *
 *   click X Y [button]        press and release a mouse button
 *   drag X1 Y1 X2 Y2          press at 1, move, release at 2
 *   wheel X Y up|down [n]     mouse wheel
 *   key NAME [ctrl|shift]     e.g.  key Return   key Tab shift   key v ctrl   key F5
 *   type TEXT                 types TEXT ("\n" in TEXT presses Return)
 *   setclip TEXT              becomes the CLIPBOARD owner for a few seconds (for Ctrl+V tests)
 *   sleep MS
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;
static Window win, root;

static Window find_window(void)
{
    for (int tries = 0; tries < 100; tries++) {
        Window r, p, *kids; unsigned n;
        if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
            for (unsigned i = 0; i < n; i++) {
                char *name = NULL;
                if (XFetchName(dpy, kids[i], &name) && name) {
                    int ok = strstr(name, "Resource Allocation Simulator") != NULL;
                    XFree(name);
                    if (ok) { Window w = kids[i]; XFree(kids); return w; }
                }
            }
            if (kids) XFree(kids);
        }
        usleep(100000);
    }
    return 0;
}

static void send_button(int type, int x, int y, unsigned button, unsigned state)
{
    XEvent e; memset(&e, 0, sizeof e);
    e.xbutton.type = type; e.xbutton.display = dpy; e.xbutton.window = win; e.xbutton.root = root;
    e.xbutton.x = x; e.xbutton.y = y; e.xbutton.x_root = x; e.xbutton.y_root = y;
    e.xbutton.button = button; e.xbutton.state = state; e.xbutton.same_screen = True;
    XSendEvent(dpy, win, True, NoEventMask, &e);
    XSync(dpy, False);
}

static void send_motion(int x, int y, unsigned state)
{
    XEvent e; memset(&e, 0, sizeof e);
    e.xmotion.type = MotionNotify; e.xmotion.display = dpy; e.xmotion.window = win;
    e.xmotion.root = root; e.xmotion.x = x; e.xmotion.y = y; e.xmotion.state = state;
    e.xmotion.same_screen = True;
    XSendEvent(dpy, win, True, NoEventMask, &e);
    XSync(dpy, False);
}

static void send_key_sym(KeySym ks, unsigned mods)
{
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (!kc) { fprintf(stderr, "no keycode for keysym %lx\n", (unsigned long)ks); return; }
    unsigned state = mods;
    if (XkbKeycodeToKeysym(dpy, kc, 0, 0) != ks && XkbKeycodeToKeysym(dpy, kc, 0, 1) == ks) state |= ShiftMask;
    for (int down = 1; down >= 0; down--) {
        XEvent e; memset(&e, 0, sizeof e);
        e.xkey.type = down ? KeyPress : KeyRelease; e.xkey.display = dpy; e.xkey.window = win;
        e.xkey.root = root; e.xkey.keycode = kc; e.xkey.state = state; e.xkey.same_screen = True;
        XSendEvent(dpy, win, True, NoEventMask, &e);
    }
    XSync(dpy, False);
    usleep(15000);
}

static void serve_clipboard(const char *text)
{
    Display *d = XOpenDisplay(NULL);
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    Atom clip = XInternAtom(d, "CLIPBOARD", False), utf8 = XInternAtom(d, "UTF8_STRING", False);
    Atom targets = XInternAtom(d, "TARGETS", False);
    XSetSelectionOwner(d, clip, w, CurrentTime);
    XSetSelectionOwner(d, XA_PRIMARY, w, CurrentTime);
    XFlush(d);
    for (int i = 0; i < 300; i++) {                 /* about 6 seconds */
        while (XPending(d)) {
            XEvent ev; XNextEvent(d, &ev);
            if (ev.type != SelectionRequest) continue;
            XSelectionRequestEvent *rq = &ev.xselectionrequest;
            XSelectionEvent se; memset(&se, 0, sizeof se);
            se.type = SelectionNotify; se.requestor = rq->requestor; se.selection = rq->selection;
            se.target = rq->target; se.time = rq->time; se.property = None;
            if (rq->target == utf8 || rq->target == XA_STRING) {
                XChangeProperty(d, rq->requestor, rq->property, rq->target, 8, PropModeReplace,
                                (const unsigned char *)text, (int)strlen(text));
                se.property = rq->property;
            } else if (rq->target == targets) {
                Atom t[2] = { utf8, XA_STRING };
                XChangeProperty(d, rq->requestor, rq->property, XA_ATOM, 32, PropModeReplace,
                                (unsigned char *)t, 2);
                se.property = rq->property;
            }
            XSendEvent(d, rq->requestor, False, NoEventMask, (XEvent *)&se);
            XFlush(d);
        }
        usleep(20000);
    }
    exit(0);
}

int main(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    root = DefaultRootWindow(dpy);
    win = find_window();
    if (!win) { fprintf(stderr, "simulator window not found\n"); return 1; }

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = '\0';
        char cmd[32] = "";
        int x = 0, y = 0, x2 = 0, y2 = 0, n = 1;
        char arg[64] = "";
        sscanf(line, "%31s", cmd);
        const char *rest = line + strlen(cmd);
        if (*rest == ' ') rest++;
        if (!strcmp(cmd, "click")) {
            unsigned b = 1;
            sscanf(rest, "%d %d %u", &x, &y, &b);
            send_button(ButtonPress, x, y, b, 0);
            usleep(20000);
            send_button(ButtonRelease, x, y, b, b == 1 ? Button1Mask : Button2Mask);
        } else if (!strcmp(cmd, "drag")) {
            sscanf(rest, "%d %d %d %d", &x, &y, &x2, &y2);
            send_button(ButtonPress, x, y, 1, 0);
            for (int i = 1; i <= 5; i++) send_motion(x + (x2 - x) * i / 5, y + (y2 - y) * i / 5, Button1Mask);
            send_button(ButtonRelease, x2, y2, 1, Button1Mask);
        } else if (!strcmp(cmd, "wheel")) {
            sscanf(rest, "%d %d %63s %d", &x, &y, arg, &n);
            for (int i = 0; i < n; i++) {
                send_button(ButtonPress, x, y, !strcmp(arg, "up") ? 4 : 5, 0);
                send_button(ButtonRelease, x, y, !strcmp(arg, "up") ? 4 : 5, 0);
            }
        } else if (!strcmp(cmd, "key")) {
            char name[32] = "", mod[16] = "";
            sscanf(rest, "%31s %15s", name, mod);
            KeySym ks = XStringToKeysym(name);
            unsigned m = !strcmp(mod, "ctrl") ? ControlMask : !strcmp(mod, "shift") ? ShiftMask : 0;
            if (ks == NoSymbol) fprintf(stderr, "unknown key %s\n", name);
            else send_key_sym(ks, m);
        } else if (!strcmp(cmd, "type")) {
            for (const char *p = rest; *p; p++) {
                if (p[0] == '\\' && p[1] == 'n') { send_key_sym(XK_Return, 0); p++; continue; }
                if (*p == ' ') { send_key_sym(XK_space, 0); continue; }
                char s[2] = { *p, 0 };
                KeySym ks = XStringToKeysym(s);
                if (*p == ':') ks = XK_colon;
                if (*p == ',') ks = XK_comma;
                if (*p == '-') ks = XK_minus;
                if (*p == '.') ks = XK_period;
                if (ks != NoSymbol) send_key_sym(ks, 0);
            }
        } else if (!strcmp(cmd, "setclip")) {
            char *text = strdup(rest);
            for (char *p; (p = strstr(text, "\\n")); ) { *p = '\n'; memmove(p + 1, p + 2, strlen(p + 2) + 1); }
            if (fork() == 0) serve_clipboard(text);
            usleep(300000);
        } else if (!strcmp(cmd, "sleep")) {
            usleep((useconds_t)atoi(rest) * 1000);
        }
        XSync(dpy, False);
        usleep(60000);
    }
    return 0;
}
