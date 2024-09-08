#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>

// EWMH atoms
enum {
    ActiveWindow,
    ClientList,
    ClientListStacking,
    CloseWindow,
    CurrentDesktop,
    DesktopViewport,
    FrameExtents,
    NumberOfDesktops,
    Supported,
    SupportingWMCheck,
    WMDesktop,
    WMFullPlacement,
    WMName,
    Net_N
};

// ICCCM atoms
enum {
    DeleteWindow,
    Protocols,
    State,
    TakeFocus,
    WM_N
};

typedef struct Client {
    const Window w;
    struct Client *next;
} Client;

// Functions
static Client * get_client(Window);
static Client * get_parent(const Client *);
static int xerror(Display *, XErrorEvent *);
static void add(Window);
static void delete(Window);
static void focus(Window);
static void pop(Window);
static void resize(Window);
static void send_event(Window, Atom);
static void set_state(Window, long);
static void signal_handler(int);
static void update_client_list(Window, Bool);
static void update_client_list_stacking(void);

// Event-Handlers
static void client_message(const XClientMessageEvent *);
static void configure_notify(const XConfigureEvent *);
static void configure_request(const XConfigureRequestEvent *);
static void property_notify(const XPropertyEvent *);
static void unmap_notify(const XUnmapEvent *);

// Variables
static Atom wm_atoms[WM_N], net_atoms[Net_N], XA_WM_CMD;
static Client *head;
static Display *d;
static int clients_n = 0;
static int sh, sw; // screen-width and -height
static volatile sig_atomic_t running = True;
static Window r; // root-window

Client * get_client(const Window w) {
    Client *c;
    for (c = head; c && c->w != w; c = c->next);
    return c;
}

Client * get_parent(const Client *c) {
    if (!c || c == head)
        return NULL;
    Client *p;
    for (p = head; p && p->next != c; p = p->next);
    return p;
}

int xerror(Display *dpy, XErrorEvent *e) { (void) dpy; (void) e; return 0; }

void add(const Window w) {
    Client *c = get_client(w);
    if (c)
        return;
    // Configure, map and focus window
    XChangeProperty(d, w, net_atoms[WMDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (int []) {0}, 1);
    XChangeProperty(d, w, net_atoms[FrameExtents], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0, 0, 0}, 4);
    XSetWindowBorderWidth(d, w, 0);
    resize(w);
    set_state(w, NormalState);
    XMapWindow(d, w);
    focus(w);
    // Add window to list
    memcpy(c = malloc(sizeof(Client)), &(Client) {w, head}, sizeof(Client));
    head = c;
    clients_n++;
    update_client_list(w, True);
    update_client_list_stacking();
}

void delete(const Window w) { send_event(w, wm_atoms[DeleteWindow]); }

void focus(const Window w) {
    XSetInputFocus(d, w, RevertToPointerRoot, CurrentTime);
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW,
        32, PropModeReplace, (unsigned char *) &w, 1);
    send_event(w, wm_atoms[TakeFocus]);
}

void pop(const Window w) {
    Client *c = get_client(w);
    if (!c || head == c)
        return;
    focus(w);
    XRaiseWindow(d, w);
    // Update list
    get_parent(c)->next = c->next;
    c->next = head;
    head = c;
    update_client_list_stacking();
}

void resize(const Window w) {
    XMoveResizeWindow(d, w, 0, 0, (unsigned int) sw, (unsigned int) sh);
}

void send_event(const Window w, const Atom protocol) {
    Atom *protocols;
    int n;
    if (!XGetWMProtocols(d, w, &protocols, &n))
        return;
    Bool protocol_exists = False;
    while (!protocol_exists && n--)
        protocol_exists = protocols[n] == protocol;
    XFree(protocols);
    if (!protocol_exists)
        return;
    XEvent e;
    e.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = wm_atoms[Protocols];
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long) protocol;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, w, False, NoEventMask, &e);
}

void set_state(const Window w, const long state) {
    XChangeProperty(d, w, wm_atoms[State], wm_atoms[State], 32,
        PropModeReplace, (unsigned char *) (long []) {state, None}, 2);
}

void signal_handler(const int signal) {
    (void) signal;
    running = False;
    // Send dummy-event to unblock XNextEvent()
    XSendEvent(d, r, False, SubstructureRedirectMask, (XEvent *) &(XClientMessageEvent) {
        .type = ClientMessage,
        .format = 32,
    });
    XFlush(d);
}

void update_client_list(const Window w, const Bool add) {
    if (add) {
        XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
            PropModeAppend, (unsigned char *) &w, 1);
        return;
    }
    // Get _NET_CLIENT_LIST
    unsigned long n;
    unsigned char *clients;
    XGetWindowProperty(d, r, net_atoms[ClientList],
        0, sizeof(Window), False, XA_WINDOW, &(Atom) {None},
        &(int) {None}, &n, &(unsigned long) {None}, &clients);
    if (!clients)
        return;
    // Remove w from _NET_CLIENT_LIST
    int i;
    for (i = 0; i < clients_n && clients[i] != w; i++);
    for ( ; i < (int) n - 1; i++)
        clients[i] = clients[i + 1];
    n--;
    // Update _NET_CLIENT_LIST
    XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
        PropModeReplace, clients, (int) n);
    XFree(clients);
}

void update_client_list_stacking(void) {
    Window clients[clients_n];
    int i = clients_n - 1;
    for (Client *c = head; c; c = c->next)
        clients[i--] = c->w;
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) clients, clients_n);
}

void client_message(const XClientMessageEvent *e) {
    const Window w = e->window;
    if (!get_client(w))
        return;
    const Atom msg = e->message_type;
    if (msg == net_atoms[ActiveWindow])
        pop(w);
    else if (msg == net_atoms[CloseWindow])
        delete(w);
}

void configure_notify(const XConfigureEvent *e) {
    const Window w = e->window;
    const int wh = e->height;
    const int ww = e->width;
    if (w == r && (wh != sh || ww != sw)) {
        sh = wh;
        sw = ww;
        for (Client *c = head; c; c = c->next)
            resize(c->w);
    } else if (get_client(w)) {
        if (e->border_width != 0)
            XSetWindowBorderWidth(d, w, 0);
        if (e->x != 0 || e->y != 0 || wh != sh || ww != sw)
            resize(w);
    }
}

void configure_request(const XConfigureRequestEvent *e) {
    const Window w = e->window;
    XWindowChanges wc;
    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.border_width = e->border_width;
    wc.sibling = e->above;
    wc.stack_mode = e->detail;
    XConfigureWindow(d, w, (unsigned int) e->value_mask, &wc);
}

void property_notify(const XPropertyEvent *e) {
    if (e->window != r || e->atom != XA_WM_CMD)
        return;
    XTextProperty p;
    XGetTextProperty(d, r, &p, XA_WM_CMD);
    char cmd[16];
    strcpy(cmd, (char *) p.value);
    if (!strcmp(cmd, "last") && clients_n > 1)
        pop(head->next->w);
    else if (!strcmp(cmd, "delete") && clients_n > 0)
        delete(head->w);
    XFree(p.value);
}

void unmap_notify(const XUnmapEvent *e) {
    const Window w = e->window;
    Client *c = get_client(w);
    if (!c)
        return;
    if (clients_n == 1)
        XChangeProperty(d, r, net_atoms[ActiveWindow],
            XA_WINDOW, 32, PropModeReplace, None, 0);
    if (c != head)
        get_parent(c)->next = c->next;
    else if (!head->next)
        head = NULL;
    else {
        head = head->next;
        focus(head->w);
    }
    free(c);
    clients_n--;
    update_client_list(w, False);
    update_client_list_stacking();
    set_state(w, WithdrawnState);
    XDeleteProperty(d, w, net_atoms[WMDesktop]);
}

int main(const int argc, const char *argv[]) {
    if (!(d = XOpenDisplay(NULL)))
        return EXIT_FAILURE;
    // Remote-Control by setting the XSWM_CMD property on the root-window
    // and catching it with property_notify()
    // Check property_notify() to see which commands are supported
    r = XDefaultRootWindow(d);
    XA_WM_CMD = XInternAtom(d, "XSWM_CMD", False);
    if (argc > 1) {
        XChangeProperty(d, r, XA_WM_CMD, XA_STRING, 8,
            PropModeReplace, (unsigned char *) argv[1], (int) strlen(argv[1]));
        XCloseDisplay(d);
        return EXIT_SUCCESS;
    }
    // Handle signals
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, signal_handler);
    // Fixes libreoffice-recovery-crash
    XSetErrorHandler(xerror);
    // Variables
    const int s = XDefaultScreen(d);
    sh = XDisplayHeight(d, s);
    sw = XDisplayWidth(d, s);
    // ICCCM atoms
    char *wm_atom_names[WM_N];
    wm_atom_names[DeleteWindow] = "WM_DELETE_WINDOW";
    wm_atom_names[Protocols] = "WM_PROTOCOLS";
    wm_atom_names[State] = "WM_STATE";
    wm_atom_names[TakeFocus] = "WM_TAKE_FOCUS";
    XInternAtoms(d, wm_atom_names, WM_N, False, wm_atoms);
    // EWMH atoms
    char *net_atom_names[Net_N];
    net_atom_names[ActiveWindow] = "_NET_ACTIVE_WINDOW";
    net_atom_names[ClientList] = "_NET_CLIENT_LIST";
    net_atom_names[ClientListStacking] = "_NET_CLIENT_LIST_STACKING";
    net_atom_names[CloseWindow] = "_NET_CLOSE_WINDOW";
    net_atom_names[CurrentDesktop] = "_NET_CURRENT_DESKTOP";
    net_atom_names[DesktopViewport] = "_NET_DESKTOP_VIEWPORT";
    net_atom_names[FrameExtents] = "_NET_FRAME_EXTENTS";
    net_atom_names[NumberOfDesktops] = "_NET_NUMBER_OF_DESKTOPS";
    net_atom_names[Supported] = "_NET_SUPPORTED";
    net_atom_names[SupportingWMCheck] = "_NET_SUPPORTING_WM_CHECK";
    net_atom_names[WMDesktop] = "_NET_WM_DESKTOP";
    net_atom_names[WMFullPlacement] = "_NET_WM_FULL_PLACEMENT";
    net_atom_names[WMName] = "_NET_WM_NAME";
    XInternAtoms(d, net_atom_names, Net_N, False, net_atoms);
    // EWMH configuration
    const Window wm_check = XCreateSimpleWindow(d, r, 0, 0, 1, 1, 0, 0, 0);
    const Atom utf8string = XInternAtom(d, "UTF8_STRING", False);
    const char wm_name[] = "xswm";
    XChangeProperty(d, wm_check, net_atoms[WMName], utf8string, 8,
        PropModeReplace, (unsigned char *) &wm_name, (int) strlen(wm_name));
    XChangeProperty(d, r, net_atoms[Supported], XA_ATOM, 32,
        PropModeReplace, (unsigned char *) &net_atoms, Net_N);
    XChangeProperty(d, r, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, wm_check, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[CurrentDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0}, 1);
    XChangeProperty(d, r, net_atoms[DesktopViewport], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0}, 2);
    XChangeProperty(d, r, net_atoms[NumberOfDesktops], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {1}, 1);
    // WM configuration
    XSelectInput(d, r, SubstructureRedirectMask | SubstructureNotifyMask
        | StructureNotifyMask | PropertyChangeMask);
    XDefineCursor(d, r, XCreateFontCursor(d, 68));
    system("\"$XDG_CONFIG_HOME\"/xswm/autostart.sh &");
    // Scan for windows at startup
    Window *wins;
    unsigned int n;
    XQueryTree(d, r, &(Window) {None}, &(Window) {None}, &wins, &n);
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(d, wins[i], &wa)
        && !wa.override_redirect && wa.map_state == IsViewable)
            add(wins[i]);
    }
    if (wins)
        XFree(wins);
    // Main-Loop
    XEvent e;
    while (running) {
        XNextEvent(d, &e);
        switch (e.type) {
            case ClientMessage: client_message(&e.xclient); break;
            case ConfigureNotify: configure_notify(&e.xconfigure); break;
            case ConfigureRequest: configure_request(&e.xconfigurerequest); break;
            case MapRequest: add(e.xmaprequest.window); break;
            case PropertyNotify: property_notify(&e.xproperty); break;
            case UnmapNotify: unmap_notify(&e.xunmap); break;
        }
    }
    // Clean-Up (clients are freed by OS)
    XCloseDisplay(d);
}
